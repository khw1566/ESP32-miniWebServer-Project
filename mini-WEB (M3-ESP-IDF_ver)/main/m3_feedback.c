#include "m3_feedback.h"
#include "m3_audio.h"
#include "m3_preferences.h"
#include "m3_i18n.h"
#include "m3_wav.h"
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
static m3_feedback_settings_t current = {1, 1, 45, 1, 35, 1, 55, 0};
static unsigned revision, pack;
static bool dirty;
static int64_t next_save;
static QueueHandle_t cues;
typedef struct { unsigned cue, language; int64_t expires; } pending_t;
bool m3_feedback_valid(const m3_feedback_settings_t *p) {
    return p && p->version == 1 && p->boot_enabled <= 1 && p->effects_enabled <= 1 &&
        p->guidance_enabled <= 1 && p->guidance_mode <= 1 && p->boot_volume <= 100 &&
        p->effects_volume <= 100 && p->guidance_volume <= 100;
}
void m3_feedback_get(m3_feedback_settings_t *out) {
    portENTER_CRITICAL(&lock); *out = current; portEXIT_CRITICAL(&lock);
}
bool m3_feedback_update(const m3_feedback_settings_t *p) {
    if (!m3_feedback_valid(p)) return false;
    portENTER_CRITICAL(&lock);
    if (memcmp(&current, p, sizeof(current))) { current = *p; ++revision; dirty = true; }
    portEXIT_CRITICAL(&lock); return true;
}
unsigned m3_feedback_pack(void) {
    portENTER_CRITICAL(&lock); unsigned value = pack; portEXIT_CRITICAL(&lock); return value;
}
void m3_feedback_set_pack(unsigned value) {
    if (value >= M3_SOUND_PACK_COUNT) return;
    portENTER_CRITICAL(&lock);
    if (pack != value) { pack = value; ++revision; dirty = true; }
    portEXIT_CRITICAL(&lock);
}
esp_err_t m3_feedback_init(void) {
    nvs_handle_t nvs; m3_feedback_settings_t saved; size_t size = sizeof(saved);
    if (nvs_open("m3_device", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "sound_settings", &saved, &size) == ESP_OK &&
            size == sizeof(saved) && m3_feedback_valid(&saved)) current = saved;
        uint8_t selected; /* Separate key keeps the eight-byte sound_settings blob readable. */
        if (nvs_get_u8(nvs, "sound_pack", &selected) == ESP_OK && selected < M3_SOUND_PACK_COUNT) pack = selected;
        nvs_close(nvs);
    }
    cues = xQueueCreate(4, sizeof(pending_t));
    return cues ? ESP_OK : ESP_ERR_NO_MEM;
}
void m3_feedback_poll(void) {
    if (esp_timer_get_time() < next_save) return;
    next_save = esp_timer_get_time() + 2000000;
    m3_feedback_settings_t p; bool pending; unsigned captured, selected;
    portENTER_CRITICAL(&lock); p = current; pending = dirty; captured = revision; selected = pack; portEXIT_CRITICAL(&lock);
    if (!pending) return;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("m3_device", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "sound_settings", &p, sizeof(p));
        if (err == ESP_OK) err = nvs_set_u8(nvs, "sound_pack", (uint8_t)selected);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        bool completed = false;
        portENTER_CRITICAL(&lock);
        if (revision == captured) { dirty = false; completed = true; }
        portEXIT_CRITICAL(&lock);
        if (completed) m3_feedback_emit(M3_CUE_SAVED, m3_i18n_language());
    } else ESP_LOGW("M3_SOUND", "Sound settings save failed: %s", esp_err_to_name(err));
}
void m3_feedback_clear(void) { if (cues) xQueueReset(cues); }
void m3_feedback_emit(unsigned cue, unsigned language) {
    if (!cues || cue >= M3_CUE_COUNT || language >= 3 || m3_preferences_restarting() || m3_preferences_web_active()) return;
    m3_audio_state_t state; m3_audio_state(&state);
    if (!state.ready || state.playing || !state.volume) return;
    pending_t p = {cue, language, esp_timer_get_time() + 3000000};
    xQueueSend(cues, &p, 0); /* Drop on congestion; never delay touch or Wi-Fi. */
}
static bool resolve(unsigned cue, unsigned language, m3_feedback_job_t *job) {
    m3_feedback_settings_t p; m3_feedback_get(&p);
    unsigned channel = cue == M3_CUE_BOOT ? 0 : cue == M3_CUE_TAP ? 1 : p.guidance_enabled ? 2 : 1;
    bool enabled = channel == 0 ? p.boot_enabled : channel == 1 ? p.effects_enabled : p.guidance_enabled;
    unsigned volume = channel == 0 ? p.boot_volume : channel == 1 ? p.effects_volume : p.guidance_volume;
    if (!enabled || !volume) return false;
    *job = (m3_feedback_job_t){cue, language, volume, channel, channel == 2 && !p.guidance_mode};
    return true;
}
bool m3_feedback_take(m3_feedback_job_t *job) {
    pending_t p;
    while (cues && xQueueReceive(cues, &p, 0) == pdTRUE) {
        if (esp_timer_get_time() <= p.expires && resolve(p.cue, p.language, job)) return true;
    }
    return false;
}
bool m3_feedback_boot(m3_feedback_job_t *job) { return resolve(M3_CUE_BOOT, 0, job); }

typedef struct { uint16_t hz, ms; uint8_t wave, gain; } m3_tone_t;
typedef struct { const m3_tone_t *notes; uint8_t count; } m3_tone_seq_t;
typedef struct { m3_tone_seq_t boot, tap, confirm, error, setup; m3_tone_t app; uint16_t app_step; } m3_sound_pack_t;
#include "m3_sound_packs.inc"

/* One period sample for phase 0..1. Square is softened; bell adds an octave partial. */
static float wave_sample(unsigned wave, float phase) {
    if (wave == M3_WAVE_TRIANGLE) return 4.0f * fabsf(phase - 0.5f) - 1.0f;
    if (wave == M3_WAVE_SQUARE) return phase < 0.5f ? 0.6f : -0.6f;
    float base = sinf(6.28318530718f * phase);
    if (wave == M3_WAVE_BELL) return 0.75f * base + 0.25f * sinf(12.5663706144f * phase);
    return base;
}
/* Short envelopes avoid clicks. The worker is the only owner of PCM and codec.
 * tools/generate_sound_packs.py and the m3-sound-design skill preview this exact synthesis. */
static bool note(const m3_tone_t *tone, const m3_audio_output_t *out) {
    enum { RATE = 24000, BLOCK = 240 };
    int16_t pcm[BLOCK * 2];
    unsigned total = RATE * tone->ms / 1000, edge = total / 4 < 240 ? total / 4 : 240;
    float step = (float)tone->hz / RATE, phase = 0, amplitude = 90.0f * tone->gain;
    for (unsigned pos = 0; pos < total && !out->cancelled();) {
        unsigned frames = total - pos; if (frames > BLOCK) frames = BLOCK;
        for (unsigned i = 0; i < frames; ++i) {
            unsigned n = pos + i, distance = n < total - n ? n : total - n;
            float envelope = edge && distance < edge ? (float)distance / edge : 1.0f;
            if (tone->wave == M3_WAVE_BELL) envelope *= expf(-3.0f * n / total);
            int16_t value = tone->hz ? (int16_t)(amplitude * envelope * wave_sample(tone->wave, phase)) : 0;
            pcm[i * 2] = pcm[i * 2 + 1] = value;
            phase += step; if (phase >= 1.0f) phase -= 1.0f;
        }
        if (!out->write(pcm, frames * sizeof(int16_t) * 2)) return false;
        pos += frames;
    }
    return !out->cancelled();
}
static bool sequence(const m3_tone_seq_t *seq, const m3_audio_output_t *out) {
    bool ok = true;
    for (unsigned i = 0; i < seq->count && ok; ++i) ok = note(&seq->notes[i], out);
    return ok;
}
static bool tones(unsigned cue, const m3_audio_output_t *out) {
    if (!out->begin(24000)) return false;
    const m3_sound_pack_t *set = &sound_packs[m3_feedback_pack()];
    bool ok;
    if (cue == M3_CUE_BOOT) ok = sequence(&set->boot, out);
    else if (cue == M3_CUE_TAP) ok = sequence(&set->tap, out);
    else if (cue == M3_CUE_ERROR || cue == M3_CUE_WIFI_FAILED) ok = sequence(&set->error, out);
    else if (cue == M3_CUE_WIFI_SETUP) ok = sequence(&set->setup, out);
    else if (cue >= M3_CUE_APP_HOME) {
        m3_tone_t tone = set->app;
        tone.hz = (uint16_t)(tone.hz + set->app_step * (cue - M3_CUE_APP_HOME));
        ok = note(&tone, out);
    } else ok = sequence(&set->confirm, out);
    return ok && out->drain();
}
/* 0: unavailable before opening codec; -1: output failed; 1: completed. */
static int speech(const m3_feedback_job_t *job, const m3_audio_output_t *out) {
    if (!sd_media_admin_lock()) return 0;
    static const char *languages[] = {"ko", "en", "ja"};
    char path[96];
    snprintf(path, sizeof(path), SD_MEDIA_ROOT "/sounds/m3/%s/%u.wav", languages[job->language], job->cue);
    FILE *file = fopen(path, "rb"); m3_wav_info_t wav; bool ok = false, opened = false;
    if (!file || !m3_wav_parse_limit(file, &wav, 512 * 1024)) goto done;
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
void m3_feedback_run(const m3_feedback_job_t *job, const m3_audio_output_t *out) {
    out->message("System sound", true, 0);
    int result = job->speech ? speech(job, out) : 0;
    bool ok = result > 0;
    /* Missing voice packs/card use the same guidance volume and never need TLS. */
    if (result == 0 && !out->cancelled()) ok = tones(job->cue, out);
    out->message(ok ? "System sound complete" : "System sound stopped", false, ok ? 100 : 0);
}
