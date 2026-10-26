#pragma once
#include "esp_err.h"
#include "m4_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t version, boot_enabled, boot_volume, effects_enabled, effects_volume;
    uint8_t guidance_enabled, guidance_volume, guidance_mode; /* 0 voice, 1 tones */
} m4_feedback_settings_t;
typedef enum {
    M4_CUE_BOOT, M4_CUE_TAP, M4_CUE_READY, M4_CUE_WIFI_CONNECTED, M4_CUE_WIFI_SETUP,
    M4_CUE_WIFI_FAILED, M4_CUE_SAVED, M4_CUE_ERROR, M4_CUE_ACCESSIBILITY, M4_CUE_LANGUAGE,
    M4_CUE_APP_HOME, M4_CUE_APP_WIFI, M4_CUE_APP_FILES, M4_CUE_APP_RECORDER, M4_CUE_APP_MONITOR,
    M4_CUE_APP_SETTINGS, M4_CUE_APP_RADIO, M4_CUE_APP_GAMES, M4_CUE_APP_TTS, M4_CUE_APP_AGENT,
    M4_CUE_APP_TUBE, M4_CUE_APP_MUSIC, M4_CUE_APP_GALLERY, M4_CUE_APP_CALCULATOR, M4_CUE_APP_YTMUSIC, M4_CUE_APP_READER,
    M4_CUE_COUNT
} m4_cue_t;
typedef struct { unsigned cue, language, volume, channel; bool speech; } m4_feedback_job_t;
esp_err_t m4_feedback_init(void);
bool m4_feedback_valid(const m4_feedback_settings_t *settings);
void m4_feedback_get(m4_feedback_settings_t *settings);
bool m4_feedback_update(const m4_feedback_settings_t *settings);
void m4_feedback_poll(void); /* Service task: debounced NVS writes. */
void m4_feedback_emit(unsigned cue, unsigned language); /* Nonblocking, no filesystem/codec calls. */
void m4_feedback_clear(void);
bool m4_feedback_take(m4_feedback_job_t *job); /* Sole audio worker only. */
bool m4_feedback_boot(m4_feedback_job_t *job);
void m4_feedback_run(const m4_feedback_job_t *job, const m4_audio_output_t *output);
