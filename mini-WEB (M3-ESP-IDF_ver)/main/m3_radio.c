#include "m3_cores.h"
#include "m3_wifi_performance.h"
#include "m3_audio_apps.h"
#include "m3_tube_config.h"
#include "network_manager.h"
#include "https_memory.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

typedef struct { unsigned interval, audio_left, metadata_left, bitrate; bool length_next; } icy_t;
bool m3_radio_url_valid(const char *url) {
    if (!url || strlen(url) > 512) return false;
    unsigned prefix = !strncmp(url, "https://", 8) ? 8 : !strncmp(url, "http://", 7) ? 7 : 0;
    if (!prefix || !url[prefix] || url[prefix] == '/') return false;
    for (const char *p = url; *p; ++p) if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '@') return false;
    return true;
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    icy_t *icy = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "icy-metaint")) {
        char *end; unsigned long value = strtoul(event->header_value, &end, 10);
        if (*end || !value || value > 16 * 1024 * 1024) return ESP_FAIL;
        icy->interval = icy->audio_left = (unsigned)value;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "icy-br")) {
        char *end; unsigned long value = strtoul(event->header_value, &end, 10);
        if (!*end && value >= 16 && value <= 512) icy->bitrate = value * 1000;
    }
    return ESP_OK;
}
/* Compact in place; metadata length bytes and tags can span any HTTP read. */
static unsigned icy_filter(icy_t *icy, uint8_t *buffer, unsigned size) {
    if (!icy->interval) return size;
    unsigned written = 0;
    for (unsigned i = 0; i < size; ++i) {
        if (icy->metadata_left) {
            if (!--icy->metadata_left) icy->audio_left = icy->interval;
        } else if (icy->length_next) {
            icy->length_next = false; icy->metadata_left = (unsigned)buffer[i] * 16;
            if (!icy->metadata_left) icy->audio_left = icy->interval;
        } else {
            buffer[written++] = buffer[i];
            if (!--icy->audio_left) icy->length_next = true;
        }
    }
    return written;
}
/* Three stages: HTTP -> compressed PSRAM -> decoder -> stereo PCM PSRAM -> I2S.
 * The decoder never opens the codec; only the audio worker writes to I2S. */
enum { RADIO_BUFFER_BYTES = 512 * 1024, RADIO_PREBUFFER_BYTES = 96 * 1024,
       RADIO_PCM_BYTES = 256 * 1024, RADIO_PCM_PREBUFFER_MS = 750,
       RADIO_READ_BYTES = 4096, RADIO_PCM_BLOCK_BYTES = 1920 };
static const char *TAG = "M3_RADIO";
typedef struct {
    StreamBufferHandle_t stream;
    StaticStreamBuffer_t control;
    esp_http_client_handle_t http;
    icy_t *icy;
    uint8_t *storage, *chunk;
    atomic_bool stop, done;
    atomic_uint received;
    const char *error;
} radio_rx_t;
typedef struct {
    StreamBufferHandle_t stream;
    StaticStreamBuffer_t control;
    radio_rx_t *rx;
    esp_audio_simple_dec_handle_t decoder;
    uint8_t *storage, *input, *pcm;
    int16_t *stereo;
    atomic_bool stop, done, format_ready;
    uint32_t rate;
    const char *error;
} radio_decode_t;
static unsigned radio_prebuffer_bytes(unsigned bitrate) {
    if (!bitrate) return RADIO_PREBUFFER_BYTES;
    unsigned reserve = bitrate / 8 * 6;
    if (reserve < 32 * 1024) reserve = 32 * 1024;
    if (reserve > RADIO_BUFFER_BYTES / 2) reserve = RADIO_BUFFER_BYTES / 2;
    return reserve;
}
static unsigned radio_pcm_target(unsigned rate, unsigned milliseconds) {
    unsigned bytes = rate * 4 * milliseconds / 1000;
    if (bytes > RADIO_PCM_BYTES * 3 / 4) bytes = RADIO_PCM_BYTES * 3 / 4;
    return bytes & ~3U;
}
static bool radio_buffer_ready(size_t available, bool finished, unsigned target) {
    return available >= target || (finished && available > 0);
}
/* Sole HTTP reader. A short body timeout publishes partial reads promptly;
 * a sequence of timeouts is tolerated for up to 20s while PCM keeps playing. */
static void radio_receive(void *arg) {
    radio_rx_t *rx = arg;
    int64_t last_data = esp_timer_get_time();
    while (!atomic_load(&rx->stop)) {
        int bytes = esp_http_client_read(rx->http, (char *)rx->chunk, RADIO_READ_BYTES);
        if (bytes <= 0) {
            if (esp_http_client_is_complete_data_received(rx->http)) { rx->error = "Radio stream ended"; break; }
            if (network_get_mode() != NET_MODE_STA_CONNECTED) { rx->error = "Radio Wi-Fi disconnected"; break; }
            if (bytes < 0 && bytes != -ESP_ERR_HTTP_EAGAIN) { rx->error = "Radio receive connection failed"; break; }
            if (esp_timer_get_time() - last_data > 20000000) { rx->error = "Radio receive stalled for 20 seconds"; break; }
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        last_data = esp_timer_get_time();
        unsigned length = icy_filter(rx->icy, rx->chunk, (unsigned)bytes), offset = 0;
        while (offset < length && !atomic_load(&rx->stop)) {
            unsigned sent = xStreamBufferSend(rx->stream, rx->chunk + offset, length - offset, pdMS_TO_TICKS(50));
            offset += sent; atomic_fetch_add(&rx->received, sent);
        }
        /* Full compressed ring is backpressure, not a receive stall. */
        last_data = esp_timer_get_time();
    }
    atomic_store(&rx->done, true);
    vTaskDelete(NULL);
}
static bool radio_wait_buffer(radio_rx_t *rx, const m3_audio_output_t *out, unsigned target) {
    int64_t last_status = 0;
    int64_t deadline = esp_timer_get_time() + 30000000;
    while (!out->cancelled()) {
        size_t available = xStreamBufferBytesAvailable(rx->stream);
        bool finished = atomic_load(&rx->done);
        if (radio_buffer_ready(available, finished, target)) return true;
        if (finished || esp_timer_get_time() > deadline) return false;
        if (esp_timer_get_time() - last_status >= 250000) {
            char text[96]; unsigned percent = available * 100 / target;
            snprintf(text, sizeof(text), "Radio buffering / %u%% / %u KiB", percent, (unsigned)(available / 1024));
            out->message(text, true, percent); last_status = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
static void radio_decode(void *arg) {
    radio_decode_t *dec = arg;
    int64_t last_pcm = esp_timer_get_time();
    while (!atomic_load(&dec->stop)) {
        unsigned length = xStreamBufferReceive(dec->rx->stream, dec->input, RADIO_READ_BYTES, pdMS_TO_TICKS(50));
        if (!length) {
            if (atomic_load(&dec->rx->done)) { dec->error = dec->rx->error; break; }
            if (esp_timer_get_time() - last_pcm > 20000000) { dec->error = "Radio decoder starved for 20 seconds"; break; }
            continue;
        }
        unsigned offset = 0, stalled = 0;
        while (offset < length && !atomic_load(&dec->stop)) {
            esp_audio_simple_dec_raw_t raw = {.buffer = dec->input + offset, .len = length - offset,
                .eos = atomic_load(&dec->rx->done) && !xStreamBufferBytesAvailable(dec->rx->stream)};
            esp_audio_simple_dec_out_t frame = {.buffer = dec->pcm, .len = 32768};
            esp_audio_err_t result = esp_audio_simple_dec_process(dec->decoder, &raw, &frame);
            if (result != ESP_AUDIO_ERR_OK || raw.consumed > raw.len || frame.decoded_size > 32768) {
                dec->error = "Radio decode error; check MP3/AAC selection"; goto done;
            }
            if (frame.decoded_size) {
                esp_audio_simple_dec_info_t info = {0};
                if (esp_audio_simple_dec_get_info(dec->decoder, &info) != ESP_AUDIO_ERR_OK ||
                    info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) ||
                    info.sample_rate < 8000 || info.sample_rate > 48000 || frame.decoded_size % (info.channel * 2)) {
                    dec->error = "Radio requires PCM16 mono/stereo, 8-48 kHz"; goto done;
                }
                if (atomic_load(&dec->format_ready) && dec->rate != info.sample_rate) {
                    dec->error = "Radio sample rate changed; restart stream"; goto done;
                }
                if (!atomic_load(&dec->format_ready)) {
                    dec->rate = info.sample_rate;
                    atomic_store(&dec->format_ready, true);
                }
                uint8_t *samples = dec->pcm; unsigned count = frame.decoded_size;
                if (info.channel == 1) {
                    int16_t *mono = (int16_t *)dec->pcm;
                    for (unsigned i = 0; i < count / 2; ++i) dec->stereo[i * 2] = dec->stereo[i * 2 + 1] = mono[i];
                    samples = (uint8_t *)dec->stereo; count *= 2;
                }
                unsigned sent = 0;
                while (sent < count && !atomic_load(&dec->stop))
                    sent += xStreamBufferSend(dec->stream, samples + sent, count - sent, pdMS_TO_TICKS(50));
                last_pcm = esp_timer_get_time();
            }
            if (!raw.consumed) {
                if (!frame.decoded_size || ++stalled > 8) { dec->error = "Radio decoder made no progress"; goto done; }
            } else { offset += raw.consumed; stalled = 0; }
            if (esp_timer_get_time() - last_pcm > 20000000) { dec->error = "No decodable radio audio for 20 seconds"; goto done; }
        }
    }
done:
    atomic_store(&dec->done, true);
    vTaskDelete(NULL);
}
static bool radio_wait_pcm(radio_decode_t *dec, const m3_audio_output_t *out, unsigned milliseconds, bool refill) {
    int64_t last_status = 0;
    while (!out->cancelled()) {
        size_t available = xStreamBufferBytesAvailable(dec->stream);
        bool finished = atomic_load(&dec->done);
        bool format = atomic_load(&dec->format_ready);
        unsigned target = format ? radio_pcm_target(dec->rate, milliseconds) : 1;
        if (format && radio_buffer_ready(available, finished, target)) return true;
        if (finished) return false;
        if (esp_timer_get_time() - last_status >= 250000) {
            char text[96]; unsigned percent = format ? available * 100 / target : 0;
            snprintf(text, sizeof(text), "Radio %s / PCM %u%% / net %u KiB", refill ? "rebuffering" : "decoding",
                     percent, (unsigned)(xStreamBufferBytesAvailable(dec->rx->stream) / 1024));
            out->message(text, true, percent); last_status = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}
void m3_radio_run(const char *url, bool aac, const m3_audio_output_t *out) {
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { out->message("Connect Wi-Fi before starting radio", false, 0); return; }
    bool leased = false, opened = false, ps_changed = false;
    const char *error = "Radio connection failed";
    esp_http_client_handle_t http = NULL;
    uint8_t *output = NULL;
    icy_t icy = {0};
    radio_rx_t rx = {.stop = ATOMIC_VAR_INIT(false), .done = ATOMIC_VAR_INIT(false), .received = ATOMIC_VAR_INIT(0)};
    radio_decode_t dec = {.rx = &rx, .stop = ATOMIC_VAR_INIT(false), .done = ATOMIC_VAR_INIT(false), .format_ready = ATOMIC_VAR_INIT(false)};
    bool receiver_started = false, decoder_started = false;
    out->message("Radio connecting... / Stop cancels playback", true, 0);
    if (!strncmp(url, "https://", 8)) {
        leased = https_memory_exclusive_begin(5000);
        if (!leased) { error = "TLS memory busy; stop AI request and retry"; goto done; }
    }
    if (out->cancelled()) goto done;
    ps_changed = m3_wifi_performance_acquire();
    if (!ps_changed) ESP_LOGW(TAG, "Wi-Fi power-save disable unavailable");
    rx.storage = heap_caps_malloc(RADIO_BUFFER_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rx.chunk = heap_caps_malloc(RADIO_READ_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    dec.storage = heap_caps_malloc(RADIO_PCM_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    dec.input = heap_caps_malloc(RADIO_READ_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    dec.pcm = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    dec.stereo = heap_caps_malloc(65536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    output = heap_caps_malloc(RADIO_PCM_BLOCK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx.storage || !rx.chunk || !dec.storage || !dec.input || !dec.pcm || !dec.stereo || !output) {
        error = "Radio buffer allocation failed"; goto done;
    }
    rx.stream = xStreamBufferCreateStatic(RADIO_BUFFER_BYTES + 1, 1, rx.storage, &rx.control);
    dec.stream = xStreamBufferCreateStatic(RADIO_PCM_BYTES + 1, 4, dec.storage, &dec.control);
    if (!rx.stream || !dec.stream) { error = "Radio ring-buffer allocation failed"; goto done; }
    if (!m3_audio_decoders_ready()) { error = "Decoder registration failed"; goto done; }
    esp_aac_dec_cfg_t aac_config = {.aac_plus_enable = true};
    esp_audio_simple_dec_cfg_t decode_config = {
        .dec_type = aac ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = aac ? &aac_config : NULL, .cfg_size = aac ? sizeof(aac_config) : 0
    };
    if (esp_audio_simple_dec_open(&decode_config, &dec.decoder) != ESP_AUDIO_ERR_OK) { error = "Radio decoder unavailable"; goto done; }
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 3000, .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = http_event, .user_data = &icy,
        .max_redirection_count = 3, .disable_auto_redirect = true, .user_agent = "Mini-WebServer-M3/1.1"
    };
    http = esp_http_client_init(&config);
    if (!http) goto done;
    bool relay = !strncmp(url, M3_TUBE_URL "/api/radio?", sizeof(M3_TUBE_URL "/api/radio?") - 1);
    if (relay) esp_http_client_set_header(http, "x-m3-token", M3_TUBE_TOKEN);
    esp_http_client_set_header(http, "Icy-MetaData", "0");
    esp_http_client_set_header(http, "Accept-Encoding", "identity");
    for (unsigned redirect = 0;; ++redirect) {
        icy = (icy_t){0};
        if (out->cancelled() || esp_http_client_open(http, 0) != ESP_OK) goto done;
        if (esp_http_client_fetch_headers(http) < 0) goto done;
        int status = esp_http_client_get_status_code(http);
        if (status == 200) break;
        if (relay || !leased || redirect >= 3 ||
            (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) ||
            esp_http_client_set_redirection(http) != ESP_OK) {
            error = "Use a direct HTTPS MP3/AAC stream, not a playlist"; goto done;
        }
        esp_http_client_close(http);
    }
    /* Connection/headers keep a 3s timeout; only steady-state body reads use
     * 250ms so an incomplete 4KiB read cannot hide incoming audio for 3s. */
    if (esp_http_client_set_timeout_ms(http, 250) != ESP_OK) { error = "Radio read timeout setup failed"; goto done; }
    rx.http = http; rx.icy = &icy;
    if (xTaskCreatePinnedToCore(radio_receive, "m3_radio_rx", 12288, &rx, 5, NULL, 0) != pdPASS) {
        error = "Radio receiver task allocation failed"; goto done;
    }
    receiver_started = true;
    if (!radio_wait_buffer(&rx, out, radio_prebuffer_bytes(icy.bitrate))) {
        error = atomic_load(&rx.done) ? rx.error : "Radio prebuffer timeout; stream too slow";
        if (!error) error = "Radio ended before buffering";
        goto done;
    }
    /* Same priority as LVGL on core 1 so UI redraws time-slice with decoding instead of starving it. */
    if (xTaskCreatePinnedToCore(radio_decode, "m3_radio_dec", 24576, &dec, 4, NULL, M3_CORE_MEDIA) != pdPASS) {
        error = "Radio decoder task allocation failed"; goto done;
    }
    decoder_started = true;
    if (!radio_wait_pcm(&dec, out, RADIO_PCM_PREBUFFER_MS, false)) { error = dec.error ? dec.error : "Radio ended before PCM ready"; goto done; }
    if (!out->begin(dec.rate)) { error = "Radio codec open failed"; goto done; }
    opened = true;
    ESP_LOGI(TAG, "Started %s at %lu Hz; compressed=%u KiB, PCM=%u KiB; initial reserve=%u bytes",
        aac ? "AAC" : "MP3", (unsigned long)dec.rate, RADIO_BUFFER_BYTES / 1024, RADIO_PCM_BYTES / 1024, radio_prebuffer_bytes(icy.bitrate));
    int64_t last_status = 0, last_stats = esp_timer_get_time(); unsigned last_received = atomic_load(&rx.received), underruns = 0;
    while (!out->cancelled()) {
        /* Short network gaps are handled by the decoder/PCM ring. A compressed
         * ring empty event alone must never pause the speaker. */
        unsigned length = xStreamBufferReceive(dec.stream, output, RADIO_PCM_BLOCK_BYTES, pdMS_TO_TICKS(20));
        if (!length) {
            if (atomic_load(&dec.done)) { error = dec.error ? dec.error : "Radio stream ended"; break; }
            ++underruns;
            ESP_LOGW(TAG, "PCM underrun #%u: compressed=%u bytes, received=%u", underruns,
                (unsigned)xStreamBufferBytesAvailable(rx.stream), atomic_load(&rx.received));
            if (!out->drain()) { error = "Radio output drain failed"; goto done; }
            if (!radio_wait_pcm(&dec, out, 1500, true)) { error = dec.error ? dec.error : "Radio stream ended / network timeout"; break; }
            continue;
        }
        if (length % 4 || !out->write(output, length)) { error = "Radio stopped / I2S write failed"; goto done; }
        int64_t now = esp_timer_get_time();
        if (now - last_status >= 500000) {
            unsigned compressed = xStreamBufferBytesAvailable(rx.stream), pcm = xStreamBufferBytesAvailable(dec.stream);
            unsigned pcm_ms = pcm * 1000 / (dec.rate * 4);
            char text[96]; snprintf(text, sizeof(text), "Radio / %s / net %u KiB / PCM %u ms / gaps %u",
                aac ? "AAC" : "MP3", compressed / 1024, pcm_ms, underruns);
            out->message(text, true, pcm * 100 / RADIO_PCM_BYTES); last_status = now;
        }
        if (now - last_stats >= 5000000) {
            unsigned received = atomic_load(&rx.received);
            ESP_LOGI(TAG, "rx=%lu B/s, compressed=%u/%u, PCM=%u/%u, underruns=%u",
                (unsigned long)((uint64_t)(unsigned)(received - last_received) * 1000000 / (now - last_stats)),
                (unsigned)xStreamBufferBytesAvailable(rx.stream), RADIO_BUFFER_BYTES,
                (unsigned)xStreamBufferBytesAvailable(dec.stream), RADIO_PCM_BYTES, underruns);
            last_received = received; last_stats = now;
        }
    }
    if (opened && !out->cancelled()) out->drain();
done:
    /* Cancel both producers first, then join both before freeing any shared
     * buffer, stack-owned control block, decoder or HTTP/ICY state. */
    atomic_store(&rx.stop, true); atomic_store(&dec.stop, true);
    while (decoder_started && !atomic_load(&dec.done)) vTaskDelay(pdMS_TO_TICKS(10));
    while (receiver_started && !atomic_load(&rx.done)) vTaskDelay(pdMS_TO_TICKS(10));
    if (rx.stream) vStreamBufferDelete(rx.stream);
    if (dec.stream) vStreamBufferDelete(dec.stream);
    if (http) esp_http_client_cleanup(http);
    if (dec.decoder) esp_audio_simple_dec_close(dec.decoder);
    free(output); free(rx.storage); free(rx.chunk); free(dec.storage); free(dec.input); free(dec.pcm); free(dec.stereo);
    if (ps_changed) m3_wifi_performance_release();
    if (leased) https_memory_exclusive_end();
    out->message(out->cancelled() ? "Radio stopped" : error, false, 0);
}
