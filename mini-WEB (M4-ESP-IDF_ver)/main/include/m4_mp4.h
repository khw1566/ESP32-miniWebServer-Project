#pragma once
#include "m4_audio_apps.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { M4_MP4_OK, M4_MP4_STOPPED, M4_MP4_FORMAT, M4_MP4_STORAGE,
    M4_MP4_MEMORY, M4_MP4_VIDEO_ERROR, M4_MP4_AUDIO_ERROR } m4_mp4_result_t;
typedef struct {
    bool (*current)(unsigned job);
    bool (*paused)(void);
    uint8_t *(*acquire)(unsigned job, int *slot);
    void (*publish)(unsigned job, int slot, uint32_t position_ms, uint32_t duration_ms);
} m4_mp4_view_t;
/* The sole audio worker calls this and joins its video worker before returning. */
m4_mp4_result_t m4_mp4_play(const char *relative, unsigned job, const m4_audio_output_t *audio, const m4_mp4_view_t *view);
/* Worker only: decode the first video picture without audio, clocks or a playback task. */
m4_mp4_result_t m4_mp4_thumbnail(const char *relative, unsigned job, bool (*current)(unsigned), uint8_t *canvas);
bool m4_mp4_i420_fit(const uint8_t *data, size_t bytes, unsigned width, unsigned height, uint8_t *canvas);
bool m4_mp4_avcc_sample(uint8_t *data, size_t bytes, unsigned length_bytes);
bool m4_mp4_sps_valid(const uint8_t *data, size_t bytes, unsigned *width, unsigned *height);
