#include "m3_google_audio.h"
#include "m3_gemini_config.h"
#include "m3_voice.h"
#include "m3_audio_apps.h"
#include "network_manager.h"
#include "https_memory.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VOICE_RATE = 16000, VOICE_SECONDS = 8, VOICE_SAMPLES = VOICE_RATE * VOICE_SECONDS };
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static m3_voice_state_t state = {.key_ready = sizeof(M3_OPENAI_API_KEY) > 20};
static char api_key[256] = M3_OPENAI_API_KEY;
static atomic_bool finish_recording;
static atomic_uint mic_level;
static atomic_int mic_dbfs = ATOMIC_VAR_INIT(-90);
unsigned m3_voice_level(void) { return atomic_load(&mic_level); }
int m3_voice_dbfs(void) { return atomic_load(&mic_dbfs); }
/* RMS loudness of one 10 ms block in dBFS. The meter rises at once and falls
 * with a ~100 ms time constant, like a VU meter; -60..-10 dBFS maps to 0..100. */
static void meter_update(const int16_t *block, unsigned count, float *smoothed_db) {
    uint64_t energy = 0;
    for (unsigned i = 0; i < count; ++i) energy += (uint64_t)((int32_t)block[i] * block[i]);
    float rms = sqrtf((float)energy / (float)count);
    float db = rms >= 1.0f ? 20.0f * log10f(rms / 32768.0f) : -90.0f;
    if (db < -90.0f) db = -90.0f;
    *smoothed_db = db > *smoothed_db ? db : *smoothed_db * 0.9f + db * 0.1f;
    float level = (*smoothed_db + 60.0f) * 2.0f;
    atomic_store(&mic_level, level <= 0.0f ? 0u : level >= 100.0f ? 100u : (unsigned)(level + 0.5f));
    atomic_store(&mic_dbfs, (int)lroundf(*smoothed_db));
}
static void meter_reset(void) { atomic_store(&mic_level, 0); atomic_store(&mic_dbfs, -90); }
static void wipe(void *p, size_t size) { mbedtls_platform_zeroize(p, size); }
void m3_voice_state(m3_voice_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock);
}
bool m3_voice_set_key(const char *key) {
    size_t length = key ? strlen(key) : 0;
    if (length && (length < 20 || length >= sizeof(api_key))) return false;
    for (size_t i = 0; i < length; ++i) if ((unsigned char)key[i] <= 32 || (unsigned char)key[i] >= 127) return false;
    portENTER_CRITICAL(&lock);
    if (state.busy) { portEXIT_CRITICAL(&lock); return false; }
    wipe(api_key, sizeof(api_key));
    if (length) memcpy(api_key, key, length);
    state.key_ready = state.provider ? m3_google_stt_ready() : length != 0; ++state.revision;
    portEXIT_CRITICAL(&lock); return true;
}
bool m3_voice_set_provider(unsigned provider) {
    if (provider > 2) return false;
    portENTER_CRITICAL(&lock);
    if (state.busy) { portEXIT_CRITICAL(&lock); return false; }
    state.provider = provider;
    state.key_ready = provider ? m3_google_stt_ready() : api_key[0] != 0;
    ++state.revision;
    portEXIT_CRITICAL(&lock); return true;
}
unsigned m3_voice_prepare(void) {
    if (network_get_mode() != NET_MODE_STA_CONNECTED) return 0;
    portENTER_CRITICAL(&lock);
    if (!state.key_ready || state.busy) { portEXIT_CRITICAL(&lock); return 0; }
    state.busy = true; state.recording = false; state.text[0] = 0;
    if (!++state.job) ++state.job;
    unsigned job = state.job; ++state.revision;
    snprintf(state.message, sizeof(state.message), "Microphone queued...");
    atomic_store(&finish_recording, false);
    portEXIT_CRITICAL(&lock); return job;
}
void m3_voice_finish(void) { atomic_store(&finish_recording, true); }
void m3_voice_cancel(void) {
    portENTER_CRITICAL(&lock);
    ++state.job; state.busy = state.recording = false;
    wipe(state.text, sizeof(state.text)); ++state.revision;
    snprintf(state.message, sizeof(state.message), "Voice input cancelled");
    portEXIT_CRITICAL(&lock);
    atomic_store(&finish_recording, true);
}
static bool active(unsigned job, const m3_voice_capture_t *capture) {
    portENTER_CRITICAL(&lock); bool same = state.job == job; portEXIT_CRITICAL(&lock);
    return same && !capture->cancelled();
}
static void publish(unsigned job, const char *message, bool busy, bool recording, const char *text) {
    portENTER_CRITICAL(&lock);
    if (state.job == job) {
        state.busy = busy; state.recording = recording;
        snprintf(state.message, sizeof(state.message), "%s", message);
        if (text) snprintf(state.text, sizeof(state.text), "%s", text);
        ++state.revision;
    }
    portEXIT_CRITICAL(&lock);
}
static void le16(uint8_t *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void le32(uint8_t *p, unsigned value) { le16(p, value); le16(p + 2, value >> 16); }
static void wav_header(uint8_t *p, unsigned bytes) {
    memset(p, 0, 44); memcpy(p, "RIFF", 4); le32(p + 4, bytes + 36);
    memcpy(p + 8, "WAVEfmt ", 8); le32(p + 16, 16); le16(p + 20, 1); le16(p + 22, 1);
    le32(p + 24, VOICE_RATE); le32(p + 28, VOICE_RATE * 2); le16(p + 32, 2); le16(p + 34, 16);
    memcpy(p + 36, "data", 4); le32(p + 40, bytes);
}
static bool send_bytes(esp_http_client_handle_t http, const void *data, unsigned bytes,
                       unsigned job, const m3_voice_capture_t *capture) {
    unsigned offset = 0;
    while (offset < bytes && active(job, capture)) {
        unsigned count = bytes - offset; if (count > 4096) count = 4096;
        int sent = esp_http_client_write(http, (const char *)data + offset, count);
        if (sent <= 0) return false;
        offset += sent;
    }
    return offset == bytes;
}
typedef struct { unsigned job; const m3_voice_capture_t *capture; } google_capture_t;
static bool google_cancelled(void *context) {
    google_capture_t *current = context;
    return !active(current->job, current->capture);
}
void m3_voice_run(unsigned job, const m3_voice_capture_t *capture) {
    static const char prefix[] =
        "--m3_voice_upload\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\ngpt-4o-mini-transcribe\r\n"
        "--m3_voice_upload\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nko\r\n"
        "--m3_voice_upload\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
        "--m3_voice_upload\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    static const char suffix[] = "\r\n--m3_voice_upload--\r\n";
    const char *error = "Voice input failed";
    uint8_t *wav = NULL; char *reply = NULL;
    char authorization[sizeof(api_key) + 8] = {0};
    bool opened = false, leased = false, success = false;
    esp_http_client_handle_t http = NULL;
    cJSON *json = NULL;
    unsigned samples = 0, peak = 0;
    float meter_db = -90.0f;
    meter_reset();
    if (!active(job, capture)) goto done;
    wav = heap_caps_malloc(44 + VOICE_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    reply = heap_caps_calloc(1, 4097, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav || !reply) { error = "Voice PSRAM allocation failed"; goto done; }
    publish(job, "Recording Korean / up to 8 seconds", true, true, NULL);
    /* begin can partially enable I2S, so always close it even on failure. */
    opened = true;
    if (!capture->begin()) { error = "Microphone open failed"; goto done; }
    while (samples < VOICE_SAMPLES && !atomic_load(&finish_recording) && active(job, capture)) {
        int16_t block[160];
        if (!capture->read(block, 160)) { error = "Microphone read failed"; goto done; }
        for (unsigned i = 0; i < 160; ++i) {
            int value = block[i]; unsigned magnitude = value < 0 ? -value : value;
            if (magnitude > peak) peak = magnitude;
            le16(wav + 44 + (samples + i) * 2, (uint16_t)block[i]);
        }
        meter_update(block, 160, &meter_db);
        samples += 160;
        if (samples % VOICE_RATE == 0) {
            char text[96]; snprintf(text, sizeof(text), "Recording %u / 8 seconds / level %u", samples / VOICE_RATE, peak);
            publish(job, text, true, true, NULL);
        }
    }
    capture->end(); opened = false;
    meter_reset();
    if (!active(job, capture)) goto done;
    if (samples < VOICE_RATE / 2 || peak < 64) { error = "No clear audio; speak closer and retry"; goto done; }
    wav_header(wav, samples * 2);
    portENTER_CRITICAL(&lock); unsigned provider = state.provider; portEXIT_CRITICAL(&lock);
    if (provider) {
        publish(job, "Sending audio to Gemini / Korean recognition...", true, false, NULL);
        char recognized[385]; google_capture_t context = {job, capture};
        bool recognized_ok = m3_google_transcribe(wav, 44 + samples * 2, provider - 1,
            recognized, google_cancelled, &context, &error);
        if (recognized_ok && active(job, capture)) {
            publish(job, "Review recognized text, then Use text", false, false, recognized); success = true;
        }
        wipe(recognized, sizeof(recognized)); goto done;
    }
    publish(job, "Sending audio to OpenAI / Korean recognition...", true, false, NULL);
    leased = https_memory_exclusive_begin(5000);
    if (!leased) { error = "TLS memory busy; stop other HTTPS work"; goto done; }
    portENTER_CRITICAL(&lock); snprintf(authorization, sizeof(authorization), "Bearer %s", api_key); portEXIT_CRITICAL(&lock);
    esp_http_client_config_t config = {.url = "https://api.openai.com/v1/audio/transcriptions",
        .method = HTTP_METHOD_POST, .timeout_ms = 3000, .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true};
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    esp_log_level_set("HTTP_HEADER", ESP_LOG_WARN);
    http = esp_http_client_init(&config);
    if (!http) goto done;
    esp_http_client_set_header(http, "Authorization", authorization);
    esp_http_client_set_header(http, "Content-Type", "multipart/form-data; boundary=m3_voice_upload");
    unsigned length = sizeof(prefix) - 1 + 44 + samples * 2 + sizeof(suffix) - 1;
    if (!active(job, capture) || esp_http_client_open(http, length) != ESP_OK) { error = "Voice HTTPS connection failed"; goto done; }
    if (!send_bytes(http, prefix, sizeof(prefix) - 1, job, capture) ||
        !send_bytes(http, wav, 44 + samples * 2, job, capture) ||
        !send_bytes(http, suffix, sizeof(suffix) - 1, job, capture)) { error = "Voice upload interrupted"; goto done; }
    int64_t deadline = esp_timer_get_time() + 60000000;
    int64_t headers;
    do {
        if (!active(job, capture) || esp_timer_get_time() > deadline) { error = "Voice recognition timeout"; goto done; }
        headers = esp_http_client_fetch_headers(http);
    } while (headers == -ESP_ERR_HTTP_EAGAIN);
    if (headers < 0) { error = "Voice response headers failed"; goto done; }
    int status = esp_http_client_get_status_code(http);
    if (status != 200) {
        error = status == 401 ? "API key rejected; check Settings" : status == 403 ? "API access denied; check key/project" : status == 429 ? "API quota/rate limit; check account" : "Cloud speech request failed";
        goto done;
    }
    unsigned received = 0;
    while (!esp_http_client_is_complete_data_received(http)) {
        if (!active(job, capture) || esp_timer_get_time() > deadline) { error = "Voice response timeout"; goto done; }
        if (received >= 4096) { error = "Voice response too large"; goto done; }
        int count = esp_http_client_read(http, reply + received, 4096 - received);
        if (count == -ESP_ERR_HTTP_EAGAIN || count == 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (count < 0) { error = "Voice response read failed"; goto done; }
        received += count;
    }
    reply[received] = 0;
    json = cJSON_Parse(reply);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
    if (!cJSON_IsString(text) || !m3_tts_text_valid(text->valuestring)) { error = "Recognition empty/too long; use shorter speech"; goto done; }
    if (active(job, capture)) { publish(job, "Review recognized text, then Use text", false, false, text->valuestring); success = true; }
done:
    if (opened) capture->end();
    meter_reset();
    if (http) esp_http_client_cleanup(http);
    if (leased) https_memory_exclusive_end();
    if (json) cJSON_Delete(json);
    if (wav) { wipe(wav, 44 + VOICE_SAMPLES * 2); free(wav); }
    if (reply) { wipe(reply, 4097); free(reply); }
    wipe(authorization, sizeof(authorization));
    if (!success && active(job, capture)) publish(job, error, false, false, "");
}
