#pragma once
#include "esp_err.h"
#include "m3_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t version, boot_enabled, boot_volume, effects_enabled, effects_volume;
    uint8_t guidance_enabled, guidance_volume, guidance_mode; /* 0 voice, 1 tones */
} m3_feedback_settings_t;
typedef enum {
    M3_CUE_BOOT, M3_CUE_TAP, M3_CUE_READY, M3_CUE_WIFI_CONNECTED, M3_CUE_WIFI_SETUP,
    M3_CUE_WIFI_FAILED, M3_CUE_SAVED, M3_CUE_ERROR, M3_CUE_ACCESSIBILITY, M3_CUE_LANGUAGE,
    M3_CUE_APP_HOME, M3_CUE_APP_WIFI, M3_CUE_APP_FILES, M3_CUE_APP_RECORDER, M3_CUE_APP_MONITOR,
    M3_CUE_APP_SETTINGS, M3_CUE_APP_RADIO, M3_CUE_APP_GAMES, M3_CUE_APP_TTS, M3_CUE_APP_AGENT,
    M3_CUE_APP_TUBE, M3_CUE_APP_MUSIC, M3_CUE_APP_GALLERY, M3_CUE_APP_CALCULATOR, M3_CUE_APP_YTMUSIC, M3_CUE_APP_READER,
    M3_CUE_COUNT
} m3_cue_t;
typedef struct { unsigned cue, language, volume, channel; bool speech; } m3_feedback_job_t;
/* Synthesized sound sets (tools/sound_packs.json). The choice is NVS m3_device/sound_pack. */
enum { M3_WAVE_SINE, M3_WAVE_TRIANGLE, M3_WAVE_SQUARE, M3_WAVE_BELL };
#define M3_SOUND_PACK_COUNT 5
unsigned m3_feedback_pack(void);
void m3_feedback_set_pack(unsigned pack);
esp_err_t m3_feedback_init(void);
bool m3_feedback_valid(const m3_feedback_settings_t *settings);
void m3_feedback_get(m3_feedback_settings_t *settings);
bool m3_feedback_update(const m3_feedback_settings_t *settings);
void m3_feedback_poll(void); /* Service task: debounced NVS writes. */
void m3_feedback_emit(unsigned cue, unsigned language); /* Nonblocking, no filesystem/codec calls. */
void m3_feedback_clear(void);
bool m3_feedback_take(m3_feedback_job_t *job); /* Sole audio worker only. */
bool m3_feedback_boot(m3_feedback_job_t *job);
void m3_feedback_run(const m3_feedback_job_t *job, const m3_audio_output_t *output);
