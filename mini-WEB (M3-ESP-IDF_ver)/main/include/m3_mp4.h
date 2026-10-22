#pragma once
#include "m3_audio_apps.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { M3_MP4_OK, M3_MP4_STOPPED, M3_MP4_FORMAT, M3_MP4_STORAGE,
    M3_MP4_MEMORY, M3_MP4_VIDEO_ERROR, M3_MP4_AUDIO_ERROR } m3_mp4_result_t;
typedef struct {
    bool (*current)(unsigned job);
    bool (*paused)(void);
    uint8_t *(*acquire)(unsigned job, int *slot);
    void (*publish)(unsigned job, int slot, uint32_t position_ms, uint32_t duration_ms);
} m3_mp4_view_t;
/* The sole audio worker calls this and joins its video worker before returning. */
m3_mp4_result_t m3_mp4_play(const char *relative, unsigned job, const m3_audio_output_t *audio, const m3_mp4_view_t *view);
/* Worker only: decode the first video picture without audio, clocks or a playback task. */
m3_mp4_result_t m3_mp4_thumbnail(const char *relative, unsigned job, bool (*current)(unsigned), uint8_t *canvas);
bool m3_mp4_i420_fit(const uint8_t *data, size_t bytes, unsigned width, unsigned height, uint8_t *canvas);
bool m3_mp4_avcc_sample(uint8_t *data, size_t bytes, unsigned length_bytes);
bool m3_mp4_sps_valid(const uint8_t *data, size_t bytes, unsigned *width, unsigned *height);
