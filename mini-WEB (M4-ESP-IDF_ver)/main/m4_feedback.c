#include "m4_feedback.h"
#include "m4_audio.h"
#include "m4_preferences.h"
#include "m4_i18n.h"
#include "m4_wav.h"
#include "sd_media.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static m4_feedback_settings_t current = {1, 1, 45, 1, 35, 1, 55, 0};
static unsigned revision;
static bool dirty;
static int64_t next_save;
static QueueHandle_t cues;
typedef struct { unsigned cue, language; int64_t expires; } pending_t;
bool m4_feedback_valid(const m4_feedback_settings_t *p) {
    return p && p->version == 1 && p->boot_enabled <= 1 && p->effects_enabled <= 1 &&
        p->guidance_enabled <= 1 && p->guidance_mode <= 1 && p->boot_volume <= 100 &&
        p->effects_volume <= 100 && p->guidance_volume <= 100;
}
void m4_feedback_get(m4_feedback_settings_t *out) {
    portENTER_CRITICAL(&lock); *out = current; portEXIT_CRITICAL(&lock);
}
bool m4_feedback_update(const m4_feedback_settings_t *p) {
    if (!m4_feedback_valid(p)) return false;
    portENTER_CRITICAL(&lock);
    if (memcmp(&current, p, sizeof(current))) { current = *p; ++revision; dirty = true; }
    portEXIT_CRITICAL(&lock); return true;
}
esp_err_t m4_feedback_init(void) {
    nvs_handle_t nvs; m4_feedback_settings_t saved; size_t size = sizeof(saved);
    if (nvs_open("m4_device", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "sound_settings", &saved, &size) == ESP_OK &&
            size == sizeof(saved) && m4_feedback_valid(&saved)) current = saved;
        nvs_close(nvs);
    }
    cues = xQueueCreate(4, sizeof(pending_t));
    return cues ? ESP_OK : ESP_ERR_NO_MEM;
}
void m4_feedback_poll(void) {
    if (esp_timer_get_time() < next_save) return;
    next_save = esp_timer_get_time() + 2000000;
    m4_feedback_settings_t p; bool pending; unsigned captured;
    portENTER_CRITICAL(&lock); p = current; pending = dirty; captured = revision; portEXIT_CRITICAL(&lock);
    if (!pending) return;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("m4_device", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "sound_settings", &p, sizeof(p));
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        bool completed = false;
        portENTER_CRITICAL(&lock);
        if (revision == captured) { dirty = false; completed = true; }
        portEXIT_CRITICAL(&lock);
        if (completed) m4_feedback_emit(M4_CUE_SAVED, m4_i18n_language());
    } else ESP_LOGW("M4_SOUND", "Sound settings save failed: %s", esp_err_to_name(err));
}
void m4_feedback_clear(void) { if (cues) xQueueReset(cues); }
void m4_feedback_emit(unsigned cue, unsigned language) {
    if (!cues || cue >= M4_CUE_COUNT || language >= 3 || m4_preferences_restarting() || m4_preferences_web_active()) return;
    m4_audio_state_t state; m4_audio_state(&state);
    if (!state.ready || state.playing || !state.volume) return;
    pending_t p = {cue, language, esp_timer_get_time() + 3000000};
    xQueueSend(cues, &p, 0); /* Drop on congestion; never delay touch or Wi-Fi. */
}
static bool resolve(unsigned cue, unsigned language, m4_feedback_job_t *job) {
    m4_feedback_settings_t p; m4_feedback_get(&p);
    unsigned channel = cue == M4_CUE_BOOT ? 0 : cue == M4_CUE_TAP ? 1 : p.guidance_enabled ? 2 : 1;
    bool enabled = channel == 0 ? p.boot_enabled : channel == 1 ? p.effects_enabled : p.guidance_enabled;
    unsigned volume = channel == 0 ? p.boot_volume : channel == 1 ? p.effects_volume : p.guidance_volume;
    if (!enabled || !volume) return false;
    *job = (m4_feedback_job_t){cue, language, volume, channel, channel == 2 && !p.guidance_mode};
    return true;
}
bool m4_feedback_take(m4_feedback_job_t *job) {
    pending_t p;
    while (cues && xQueueReceive(cues, &p, 0) == pdTRUE) {
        if (esp_timer_get_time() <= p.expires && resolve(p.cue, p.language, job)) return true;
    }
    return false;
}
bool m4_feedback_boot(m4_feedback_job_t *job) { return resolve(M4_CUE_BOOT, 0, job); }

/* Short envelopes avoid clicks. The worker is the only owner of PCM and codec. */
static bool note(float frequency, unsigned ms, const m4_audio_output_t *out) {
    enum { RATE = 24000, BLOCK = 240 };
    int16_t pcm[BLOCK * 2];
    unsigned total = RATE * ms / 1000;
    for (unsigned pos = 0; pos < total && !out->cancelled();) {
        unsigned frames = total - pos; if (frames > BLOCK) frames = BLOCK;
        for (unsigned i = 0; i < frames; ++i) {
            unsigned n = pos + i, distance = n < total - n ? n : total - n;
            float envelope = distance < 240 ? distance / 240.0f : 1.0f;
            int16_t value = (int16_t)(9000.0f * envelope * sinf(6.28318530718f * frequency * n / RATE));
            pcm[i * 2] = pcm[i * 2 + 1] = value;
        }
        if (!out->write(pcm, frames * sizeof(int16_t) * 2)) return false;
        pos += frames;
    }
    return !out->cancelled();
}
static bool tones(unsigned cue, const m4_audio_output_t *out) {
    if (!out->begin(24000)) return false;
    bool ok;
    if (cue == M4_CUE_BOOT) {
        const float melody[] = {523.251f, 659.255f, 783.991f, 1046.502f, 987.767f, 1046.502f};
        const unsigned duration[] = {140, 140, 180, 240, 120, 400};
        ok = true;
        for (unsigned i = 0; i < 6 && ok; ++i) ok = note(melody[i], duration[i], out);
    } else if (cue == M4_CUE_TAP) ok = note(880, 35, out);
    else if (cue == M4_CUE_ERROR || cue == M4_CUE_WIFI_FAILED) ok = note(330, 110, out) && note(220, 180, out);
    else if (cue == M4_CUE_WIFI_SETUP) ok = note(440, 110, out) && note(660, 110, out) && note(440, 160, out);
    else if (cue >= M4_CUE_APP_HOME) ok = note(600 + 35 * (cue - M4_CUE_APP_HOME), 90, out);
    else ok = note(659.255f, 90, out) && note(987.767f, 150, out);
    return ok && out->drain();
}
/* 0: unavailable before opening codec; -1: output failed; 1: completed. */
static int speech(const m4_feedback_job_t *job, const m4_audio_output_t *out) {
    if (!sd_media_admin_lock()) return 0;
    static const char *languages[] = {"ko", "en", "ja"};
    char path[96];
    snprintf(path, sizeof(path), SD_MEDIA_ROOT "/sounds/m4/%s/%u.wav", languages[job->language], job->cue);
    FILE *file = fopen(path, "rb"); m4_wav_info_t wav; bool ok = false, opened = false;
    if (!file || !m4_wav_parse_limit(file, &wav, 512 * 1024)) goto done;
    opened = true; /* A failed begin can still leave a partially opened codec. */
    if (!out->begin(wav.sample_rate)) goto done;
    unsigned remaining = wav.data_bytes;
    int16_t input[240], output[480];
    ok = true;
    while (remaining && !out->cancelled()) {
        unsigned count = remaining < sizeof(input) ? remaining : sizeof(input);
        if (fread(input, 1, count, file) != count) { ok = false; break; }
        if (wav.channels == 1) {
            for (unsigned i = 0; i < count / 2; ++i) output[i * 2] = output[i * 2 + 1] = input[i];
            ok = out->write(output, count * 2);
        } else ok = out->write(input, count);
        if (!ok) break;
        remaining -= count;
    }
    ok = ok && !remaining && out->drain();
done:
    if (file) fclose(file);
    sd_media_admin_unlock(); return opened ? ok ? 1 : -1 : 0;
}
void m4_feedback_run(const m4_feedback_job_t *job, const m4_audio_output_t *out) {
    out->message("System sound", true, 0);
    int result = job->speech ? speech(job, out) : 0;
    bool ok = result > 0;
    /* Missing voice packs/card use the same guidance volume and never need TLS. */
    if (result == 0 && !out->cancelled()) ok = tones(job->cue, out);
    out->message(ok ? "System sound complete" : "System sound stopped", false, ok ? 100 : 0);
}
