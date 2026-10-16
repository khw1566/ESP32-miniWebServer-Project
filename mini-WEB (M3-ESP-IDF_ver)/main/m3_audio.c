#include "m3_cores.h"
#include "network_manager.h"
#include "m3_google_audio.h"
#include "m3_azure_audio.h"
#include "m3_audio.h"
#include "m3_board.h"
#include "m3_wav.h"
#include "m3_audio_apps.h"
#include "m3_voice.h"
#include "m3_recorder.h"
#include "m3_preferences.h"
#include "m3_feedback.h"
#include "m3_tube.h"
#include "m3_tube_config.h"
#include "m3_music.h"
#include "m3_gallery.h"
#include "m3_memory.h"
#include "m3_pcm.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "sd_media.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "nvs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

static const char *TAG = "M3_AUDIO";
static QueueHandle_t requests;
static esp_codec_dev_handle_t codec;
static i2s_chan_handle_t tx, rx;
static const audio_codec_if_t *codec_chip;
/* DMA must remain small: C6 SDIO packets share this internal-RAM heap.
 * Radio jitter is absorbed by its PSRAM PCM ring rather than large I2S DMA. */
enum { AUDIO_DMA_DESCS = 6, AUDIO_DMA_FRAMES = 240 };
static uint32_t playback_rate;
static unsigned applied_volume = UINT_MAX;
static atomic_uint cancel_epoch;
/* Only the audio worker writes active_epoch. A stop cannot be lost while dequeuing. */
static unsigned active_epoch;
static atomic_uint volume = 40;
static atomic_bool feedback_active;
static unsigned feedback_volume = UINT_MAX; /* Audio worker only; never writes media preferences. */
static unsigned output_volume(void) {
    unsigned master = atomic_load(&volume);
    return !master ? 0 : feedback_volume != UINT_MAX ? feedback_volume : master;
}
static atomic_uint tts_provider; /* 0 OpenAI, 1 Google, 2 offline eSpeak, 3 Azure */
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static m3_audio_state_t state;
static M3_PSRAM_BSS int16_t boost_pcm[960];
bool m3_audio_decoders_ready(void) {
    static bool initialized;
    if (!initialized) {
        if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK ||
            esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) return false;
        initialized = true;
    }
    return true;
}
typedef enum { PLAY_FILE, PLAY_RADIO, PLAY_TTS, PLAY_CLOUD_TTS, PLAY_OPENAI_TTS, PLAY_VOICE, PLAY_TUBE, PLAY_MUSIC, PLAY_RECORD, PLAY_RECORDING, PLAY_MP4, PLAY_AZURE_TTS } request_kind_t;
typedef struct { char name[513]; unsigned epoch, speed; request_kind_t kind; bool aac; } request_t;
static bool cancelled(void) { return atomic_load(&cancel_epoch) != active_epoch; }
static void message(const char *text, bool playing, unsigned progress) {
    portENTER_CRITICAL(&state_lock);
    snprintf(state.message, sizeof(state.message), "%s", text);
    state.playing = playing; state.progress = progress;
    portEXIT_CRITICAL(&state_lock);
}
void m3_audio_state(m3_audio_state_t *out) {
    portENTER_CRITICAL(&state_lock); *out = state; portEXIT_CRITICAL(&state_lock);
    out->volume = atomic_load(&volume);
}
void m3_audio_volume(unsigned percent) {
    atomic_store(&volume, percent > 150 ? 150 : percent);
    m3_preferences_volume(percent);
}
void m3_audio_stop(void) {
    atomic_fetch_add(&cancel_epoch, 1);
    m3_voice_cancel();
    m3_recorder_cancel();
    m3_gallery_audio_cancel();
    if (requests) xQueueReset(requests);
    m3_feedback_clear();
}
static void interrupt_feedback(void) {
    /* Only a cue owns the codec here. Do not invalidate a microphone/gallery
     * job that the caller has just prepared while the cue was playing. */
    atomic_fetch_add(&cancel_epoch, 1);
    if (requests) xQueueReset(requests);
    m3_feedback_clear();
}
bool m3_audio_play(const char *name) {
    if (m3_preferences_restarting()) return false;
    if (!requests || !name || !sd_media_name_valid(name)) return false;
    m3_audio_state_t current; m3_audio_state(&current);
    if (!current.ready || (current.playing && !atomic_load(&feedback_active))) return false;
    if (atomic_load(&feedback_active)) interrupt_feedback();
    const char *ext = strrchr(name, '.');
    if (!ext || strcasecmp(ext, ".wav")) return false;
    request_t request = {.epoch = atomic_load(&cancel_epoch), .kind = PLAY_FILE}; snprintf(request.name, sizeof(request.name), "%s", name);
    return xQueueSend(requests, &request, 0) == pdTRUE;
}
static bool enqueue_app(request_kind_t kind, const char *text, unsigned speed, bool aac) {
    if (m3_preferences_restarting()) return false;
    m3_audio_state_t current; m3_audio_state(&current);
    if (!requests || !current.ready || (current.playing && !atomic_load(&feedback_active))) return false;
    if (atomic_load(&feedback_active)) interrupt_feedback();
    request_t request = {.kind = kind, .epoch = atomic_load(&cancel_epoch), .speed = speed, .aac = aac};
    snprintf(request.name, sizeof(request.name), "%s", text);
    bool accepted = xQueueSend(requests, &request, 0) == pdTRUE;
    memset(&request, 0, sizeof(request)); return accepted;
}
bool m3_audio_radio(const char *url, bool aac) {
    return m3_radio_url_valid(url) && enqueue_app(PLAY_RADIO, url, 0, aac);
}
bool m3_audio_radio_preset(const char *station, unsigned profile) {
    static const char *const profiles[] = {"economy", "balanced", "high"};
    if (!m3_tube_configured() || !station || !station[0] || strlen(station) > 40 || profile >= 3) return false;
    for (const char *p = station; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
    char url[513];
    int n = snprintf(url, sizeof(url), "%s/api/radio?station=%s&profile=%s", M3_TUBE_URL, station, profiles[profile]);
    return n > 0 && (size_t)n < sizeof(url) && m3_audio_radio(url, false);
}
bool m3_audio_mp4(const char *relative, unsigned gallery_job) {
    if (!relative || relative[0] != '/' || strlen(relative) > 255 || strstr(relative, "..") || strchr(relative, '\\')) return false;
    m3_recorder_state_t recording; m3_recorder_state(&recording);
    return !recording.busy && enqueue_app(PLAY_MP4, relative, gallery_job, false);
}
bool m3_audio_record(void) {
    m3_audio_state_t current; m3_audio_state(&current);
    if (!requests || !current.ready || (current.playing && !atomic_load(&feedback_active))) return false;
    unsigned job = m3_recorder_prepare();
    if (!job) return false;
    if (enqueue_app(PLAY_RECORD, "", job, false)) return true;
    m3_recorder_cancel(); return false;
}
bool m3_audio_recording_play(const char *name) {
    m3_recorder_state_t recording; m3_recorder_state(&recording);
    if (recording.busy || !sd_media_name_valid(name)) return false;
    const char *ext = strrchr(name, '.');
    return ext && !strcasecmp(ext, ".wav") && enqueue_app(PLAY_RECORDING, name, 0, false);
}
bool m3_audio_music(const char *path) {
    if (m3_preferences_restarting()) return false;
    if (!requests || !m3_music_path_valid(path)) return false;
    m3_audio_state_t current; m3_audio_state(&current);
    if (!current.ready) return false;
    m3_audio_stop();
    request_t request = {.kind = PLAY_MUSIC, .epoch = atomic_load(&cancel_epoch)};
    snprintf(request.name, sizeof(request.name), "%s", path);
    return xQueueSend(requests, &request, 0) == pdTRUE;
}
bool m3_audio_music_follow(const char *path) {
    if (m3_preferences_restarting()) return false;
    if (!requests || cancelled() || !m3_music_path_valid(path)) return false;
    request_t request = {.kind = PLAY_MUSIC, .epoch = active_epoch};
    snprintf(request.name, sizeof(request.name), "%s", path);
    return xQueueSend(requests, &request, 0) == pdTRUE;
}
/* YouTube via the PC relay; speed carries the start position in seconds. */
bool m3_audio_tube(const char *id, unsigned start_seconds, bool audio_only) {
    /* The request's aac flag carries audio-only (YouTube Music) mode. */
    bool ready = network_get_mode() == NET_MODE_STA_CONNECTED;
    if (!ready && audio_only) { m3_tube_music_cache_t cache; m3_tube_music_cache(id,&cache); ready=cache.ready; }
    return ready && enqueue_app(PLAY_TUBE, id, start_seconds, audio_only);
}
bool m3_audio_tube_follow(const char *id) {
    if (!requests || cancelled() || m3_preferences_restarting() || !id || strlen(id)!=11) return false;
    request_t request={.kind=PLAY_TUBE,.epoch=active_epoch,.aac=true};
    snprintf(request.name,sizeof(request.name),"%s",id);
    return xQueueSend(requests,&request,0)==pdTRUE;
}
bool m3_audio_tts(const char *text, unsigned speed) {
    return m3_tts_text_valid(text) && enqueue_app(PLAY_TTS, text, speed, false);
}
bool m3_audio_cloud_tts(const char *text) {
    return m3_google_tts_ready() && network_get_mode() == NET_MODE_STA_CONNECTED &&
        m3_tts_text_valid(text) && enqueue_app(PLAY_CLOUD_TTS, text, 0, false);
}
void m3_audio_tts_provider(unsigned provider) { if (provider < 4) atomic_store(&tts_provider, provider); }
unsigned m3_audio_tts_selected(void) { return atomic_load(&tts_provider); }
bool m3_audio_speak(const char *text, unsigned speed) {
    unsigned provider = atomic_load(&tts_provider);
    if (provider == 3) return m3_azure_audio_ready() && network_get_mode() == NET_MODE_STA_CONNECTED &&
        m3_tts_text_valid(text) && enqueue_app(PLAY_AZURE_TTS, text, 0, false);
    if (provider == 2) return m3_audio_tts(text, speed);
    if (provider == 1) return m3_audio_cloud_tts(text);
    return m3_openai_audio_ready() && network_get_mode() == NET_MODE_STA_CONNECTED &&
        m3_tts_text_valid(text) && enqueue_app(PLAY_OPENAI_TTS, text, 0, false);
}
bool m3_audio_voice(void) {
    m3_audio_state_t current; m3_audio_state(&current);
    if (!requests || !current.ready || (current.playing && !atomic_load(&feedback_active))) return false;
    unsigned job = m3_voice_prepare();
    if (!job) return false;
    if (!enqueue_app(PLAY_VOICE, "", job, false)) { m3_voice_cancel(); return false; }
    return true;
}
static esp_err_t codec_init(void) {
    if (!m3_board_i2c()) return ESP_ERR_INVALID_STATE;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = AUDIO_DMA_DESCS;
    channel.dma_frame_num = AUDIO_DMA_FRAMES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &tx, &rx), TAG, "I2S channel");
    i2s_std_config_t i2s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = M3_I2S_MCLK, .bclk = M3_I2S_BCLK, .ws = M3_I2S_LRCK,
                     .dout = M3_I2S_DOUT, .din = M3_I2S_DIN}
    };
    i2s.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &i2s), TAG, "I2S mode");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &i2s), TAG, "I2S RX mode");
    audio_codec_i2s_cfg_t data_config = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_config);
    audio_codec_i2c_cfg_t control_config = {
        .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = m3_board_i2c()
    };
    const audio_codec_ctrl_if_t *control = audio_codec_new_i2c_ctrl(&control_config);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!data || !control || !gpio) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t es8311 = {
        .ctrl_if = control, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = M3_AMP_ENABLE, .pa_reverted = false, .use_mclk = true, .mclk_div = 256,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3}
    };
    codec_chip = es8311_codec_new(&es8311);
    if (!codec_chip) return ESP_FAIL;
    esp_codec_dev_cfg_t config = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec_chip, .data_if = data};
    codec = esp_codec_dev_new(&config);
    return codec ? ESP_OK : ESP_FAIL;
}
static bool codec_result(const char *operation, int result) {
    if (result == ESP_CODEC_DEV_OK) return true;
    ESP_LOGE(TAG, "%s failed: codec result %d", operation, result);
    return false;
}
static bool apply_volume(void) {
    unsigned requested = output_volume();
    if (requested == applied_volume) return true;
    if (!codec_result("set volume", esp_codec_dev_set_out_vol(codec, requested > 100 ? 100 : requested))) return false;
    if (!codec_result("set mute", esp_codec_dev_set_out_mute(codec, requested == 0))) return false;
    applied_volume = requested;
    return true;
}
static bool open_rate(uint32_t rate) {
    /* Codec 1.2.0 stops TX before reconfiguring it. Start the initialized TX
     * first, as in its reference setup, so this stop has a valid state.
     * The codec data interface owns subsequent disable/reconfigure/enable. */
    esp_err_t start = i2s_channel_enable(tx);
    if (start != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX start failed: %s", esp_err_to_name(start));
        return false;
    }
    if (i2s_channel_enable(rx) != ESP_OK) { i2s_channel_disable(tx); return false; }
    esp_codec_dev_sample_info_t format = {.sample_rate = rate, .channel = 2, .bits_per_sample = 16};
    if (!codec_result("open", esp_codec_dev_open(codec, &format))) return false;
    if (!codec_result("unmute", esp_codec_dev_set_out_mute(codec, false))) return false;
    playback_rate = rate;
    applied_volume = UINT_MAX;
    if (!apply_volume()) return false;
    ESP_LOGI(TAG, "Output: %lu Hz, PCM16 stereo, MCLK=%lu Hz, volume=%u, PA GPIO%d=%d",
             (unsigned long)rate, (unsigned long)(rate * 256), applied_volume,
             M3_AMP_ENABLE, gpio_get_level(M3_AMP_ENABLE));
    const int registers[] = {0x00, 0x01, 0x09, 0x0d, 0x12, 0x31, 0x32};
    if (codec_chip->get_reg) for (unsigned i = 0; i < sizeof(registers)/sizeof(registers[0]); ++i) {
        int value = 0;
        int ret = codec_chip->get_reg(codec_chip, registers[i], &value);
        if (!codec_result("read ES8311 register", ret)) return false;
        ESP_LOGI(TAG, "ES8311 reg 0x%02x = 0x%02x", registers[i], value);
    }
    return true;
}
static bool write_pcm(void *samples, unsigned bytes) {
    if (cancelled()) return false;
    if (!apply_volume()) return false;
    unsigned gain = output_volume();
    if (gain <= 100) return codec_result("PCM write", esp_codec_dev_write(codec, samples, (int)bytes));
    if (bytes % sizeof(int16_t)) return false;
    const int16_t *input = samples;
    unsigned remaining = bytes / sizeof(int16_t);
    while (remaining && !cancelled()) {
        unsigned count = remaining > 960 ? 960 : remaining;
        for (unsigned i = 0; i < count; ++i) boost_pcm[i] = m3_pcm_gain(input[i], gain);
        if (!codec_result("boost PCM write", esp_codec_dev_write(codec, boost_pcm, count * sizeof(int16_t)))) return false;
        input += count; remaining -= count;
    }
    return !remaining;
}
static bool capture_begin(void) {
    if (i2s_channel_enable(tx) != ESP_OK) return false;
    if (i2s_channel_enable(rx) != ESP_OK) { i2s_channel_disable(tx); return false; }
    esp_codec_dev_sample_info_t format = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16};
    if (!codec_result("microphone open", esp_codec_dev_open(codec, &format))) return false;
    gpio_set_level(M3_AMP_ENABLE, 0);
    /* ES8311 1.2.0 has no mute_mic callback; enabling its ADC is sufficient. */
    return codec_result("speaker mute", esp_codec_dev_set_out_mute(codec, true)) &&
           codec_result("microphone gain", esp_codec_dev_set_in_gain(codec, 30.0f));
}
static bool capture_read(int16_t *mono, unsigned samples) {
    if (samples > 160 || cancelled()) return false;
    int16_t stereo[320];
    if (!codec_result("microphone read", esp_codec_dev_read(codec, stereo, samples * 4))) return false;
    /* ES8311 is mono; select the populated slot without halving its amplitude. */
    unsigned energy[2] = {0};
    for (unsigned i = 0; i < samples * 2; ++i) {
        int value = stereo[i]; energy[i % 2] += value < 0 ? -value : value;
    }
    unsigned slot = energy[1] > energy[0];
    for (unsigned i = 0; i < samples; ++i) mono[i] = stereo[i * 2 + slot];
    return !cancelled();
}
static void capture_end(void) { esp_codec_dev_close(codec); }
static bool drain_output(void) {
    /* write() queues DMA; immediate close would cut off the queued tail.
     * Queue silence, then allow at most one whole DMA ring to drain. */
    int16_t silence[AUDIO_DMA_FRAMES * 2] = {0};
    for (unsigned i = 0; i < AUDIO_DMA_DESCS && !cancelled(); ++i)
        if (!write_pcm(silence, sizeof(silence))) return false;
    unsigned ms = (AUDIO_DMA_DESCS * AUDIO_DMA_FRAMES * 1000U + playback_rate - 1) / playback_rate + 10;
    for (unsigned elapsed = 0; elapsed < ms && !cancelled(); elapsed += 10)
        vTaskDelay(pdMS_TO_TICKS(10));
    return !cancelled();
}
static void play_file(const char *name, bool recording) {
    if (!sd_media_admin_lock()) { message("SD unavailable or busy", false, 0); return; }
    char path[128]; snprintf(path, sizeof(path), "%s%s/%s", SD_MEDIA_ROOT, recording ? M3_RECORDER_FOLDER : "/media", name);
    FILE *file = fopen(path, "rb");
    m3_wav_info_t wav;
    bool ok = false;
    if (!file || !m3_wav_parse_limit(file, &wav, recording ? M3_RECORDER_MAX_SECONDS * M3_RECORDER_RATE * 2 + 44 : SD_MEDIA_MAX_BYTES)) { message("Use PCM16 WAV, 16-48 kHz, <=8 MiB", false, 0); goto done; }
    if (!open_rate(wav.sample_rate)) { message("Codec open failed", false, 0); goto done; }
    unsigned remaining = wav.data_bytes;
    int16_t input[1024], output[2048];
    ok = true;
    while (remaining && !cancelled()) {
        unsigned count = remaining < sizeof(input) ? remaining : sizeof(input);
        if (fread(input, 1, count, file) != count) { ok = false; break; }
        if (wav.channels == 1) {
            for (unsigned i = 0; i < count / 2; ++i) output[i * 2] = output[i * 2 + 1] = input[i];
            ok = write_pcm(output, count * 2);
        } else ok = write_pcm(input, count);
        if (!ok) break;
        remaining -= count;
        message(name, true, (unsigned)((uint64_t)(wav.data_bytes - remaining) * 100 / wav.data_bytes));
        taskYIELD();
    }
    if (!remaining && ok) ok = drain_output();
    message(!remaining && ok ? "Playback complete" : "Playback stopped / read error", false, !remaining && ok ? 100 : 0);
done:
    /* Close even after a partial open failure, ensuring PA is disabled. */
    esp_codec_dev_close(codec);
    if (file) fclose(file);
    sd_media_admin_unlock();
}
static void audio_worker(void *unused) {
    (void)unused;
    esp_err_t result = codec_init();
    portENTER_CRITICAL(&state_lock); state.ready = result == ESP_OK; portEXIT_CRITICAL(&state_lock);
    if (result != ESP_OK) { message("ES8311 initialization failed", false, 0); vTaskDelete(NULL); return; }
    message("Ready / PCM16 WAV", false, 0);
    request_t request;
    const m3_audio_output_t output = {.begin = open_rate, .write = write_pcm, .cancelled = cancelled,
                                     .drain = drain_output, .message = message};
    const m3_voice_capture_t capture = {.begin = capture_begin, .read = capture_read, .end = capture_end, .cancelled = cancelled};
    unsigned saved_volume = atomic_load(&volume);
    m3_feedback_job_t cue;
    if (m3_feedback_boot(&cue) && atomic_load(&volume)) {
        active_epoch = atomic_load(&cancel_epoch);
        feedback_volume = cue.volume; atomic_store(&feedback_active, true);
        if (!uxQueueMessagesWaiting(requests) && !cancelled()) m3_feedback_run(&cue, &output);
        esp_codec_dev_close(codec);
        feedback_volume = UINT_MAX; atomic_store(&feedback_active, false);
    }
    for (;;) {
        if (xQueueReceive(requests, &request, pdMS_TO_TICKS(20)) == pdTRUE) {
            m3_feedback_clear();
            active_epoch = request.epoch;
            if (cancelled()) continue;
            if (request.kind == PLAY_VOICE) {
                message("Voice input active", true, 0);
                m3_voice_run(request.speed, &capture);
                message("Voice input finished", false, 0);
            }
            else if (request.kind == PLAY_FILE) play_file(request.name, false);
            else if (request.kind == PLAY_MUSIC) { m3_music_run(request.name, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_RADIO) { m3_radio_run(request.name, request.aac, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_TUBE) { m3_tube_run(request.name, request.speed, request.aac, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_AZURE_TTS) { m3_azure_tts_run(request.name, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_OPENAI_TTS) { m3_openai_tts_run(request.name, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_CLOUD_TTS) { m3_google_tts_run(request.name, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_TTS) { m3_tts_run(request.name, request.speed, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_RECORDING) play_file(request.name, true);
            else if (request.kind == PLAY_MP4) { m3_gallery_mp4_run(request.name, request.speed, &output); esp_codec_dev_close(codec); }
            else if (request.kind == PLAY_RECORD) {
                message("SD recording active", true, 0);
                m3_recorder_run(request.speed, &capture);
                message("SD recording finished", false, 0);
            }
            memset(&request, 0, sizeof(request));
        } else if (m3_feedback_take(&cue) && atomic_load(&volume)) {
            active_epoch = atomic_load(&cancel_epoch);
            feedback_volume = cue.volume; atomic_store(&feedback_active, true);
            if (!uxQueueMessagesWaiting(requests) && !cancelled()) m3_feedback_run(&cue, &output);
            esp_codec_dev_close(codec);
            feedback_volume = UINT_MAX; atomic_store(&feedback_active, false);
        }
        unsigned current = atomic_load(&volume);
        if (current != saved_volume) {
            nvs_handle_t nvs;
            if (nvs_open("m3_audio", NVS_READWRITE, &nvs) == ESP_OK) {
                if (nvs_set_u8(nvs, "volume", current) == ESP_OK && nvs_commit(nvs) == ESP_OK) saved_volume = current;
                nvs_close(nvs);
            }
        }
    }
}
esp_err_t m3_audio_start(void) {
    m3_audio_tts_provider(m3_azure_audio_ready() ? 3 : 0);
    m3_preferences_t preferences; m3_preferences_get(&preferences);
    m3_audio_volume(preferences.volume);
    requests = xQueueCreate(1, sizeof(request_t));
    if (!requests) return ESP_ERR_NO_MEM;
    /* eSpeak's translation/synthesis calls need a larger C stack than WAV playback. */
    if (xTaskCreatePinnedToCore(audio_worker, "m3_audio", 65536, NULL, 4, NULL, M3_CORE_MEDIA) != pdPASS) {
        vQueueDelete(requests); requests = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
