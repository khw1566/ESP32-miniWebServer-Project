#pragma once
#include "esp_err.h"
#include "m3_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { M3_GALLERY_IDLE, M3_GALLERY_LOADING, M3_GALLERY_READY, M3_GALLERY_ENDED,
    M3_GALLERY_BAD_FORMAT, M3_GALLERY_STORAGE, M3_GALLERY_MEMORY, M3_GALLERY_MP4_FORMAT,
    M3_GALLERY_AUDIO_BUSY, M3_GALLERY_DECODE_ERROR, M3_GALLERY_AUDIO_ERROR, M3_GALLERY_STOPPED } m3_gallery_status_t;
typedef struct { m3_gallery_status_t status; bool video, paused, mp4; unsigned frames, revision;
    uint32_t position_ms, duration_ms; } m3_gallery_state_t;
esp_err_t m3_gallery_start(void);
bool m3_gallery_supported(const char *name);
bool m3_gallery_open(const char *relative);
void m3_gallery_close(void);
void m3_gallery_pause(void);
bool m3_gallery_active(void);
void m3_gallery_state(m3_gallery_state_t *out);
/* UI owner pins the returned buffer until its next call. */
const uint8_t *m3_gallery_frame(void);
void m3_gallery_mp4_run(const char *relative, unsigned job, const m3_audio_output_t *audio);
void m3_gallery_audio_cancel(void);
