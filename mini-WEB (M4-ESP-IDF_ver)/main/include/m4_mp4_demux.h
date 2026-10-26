#pragma once
#include "m4_mp4.h"
typedef struct m4_mp4_demux m4_mp4_demux_t;
typedef struct {
    unsigned width, height, sample_rate, channels, nal_length;
    unsigned video_samples, audio_samples;
    uint32_t duration_ms;
} m4_mp4_info_t;
typedef struct { uint64_t offset, duration_us; int64_t pts_us; unsigned bytes; } m4_mp4_sample_t;
m4_mp4_result_t m4_mp4_demux_open(const char *relative, unsigned job, bool (*current)(unsigned), m4_mp4_demux_t **out, m4_mp4_info_t *info);
void m4_mp4_demux_close(m4_mp4_demux_t *demux);
bool m4_mp4_demux_sample(m4_mp4_demux_t *demux, bool video, unsigned index, m4_mp4_sample_t *sample);
bool m4_mp4_demux_read(m4_mp4_demux_t *demux, const m4_mp4_sample_t *sample, uint8_t *out, size_t capacity);
const uint8_t *m4_mp4_demux_parameter(m4_mp4_demux_t *demux, bool pps, unsigned index, int *size);
