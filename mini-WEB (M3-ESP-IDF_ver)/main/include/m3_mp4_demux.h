#pragma once
#include "m3_mp4.h"
typedef struct m3_mp4_demux m3_mp4_demux_t;
typedef struct {
    unsigned width, height, sample_rate, channels, nal_length;
    unsigned video_samples, audio_samples;
    uint32_t duration_ms;
} m3_mp4_info_t;
typedef struct { uint64_t offset, duration_us; int64_t pts_us; unsigned bytes; } m3_mp4_sample_t;
m3_mp4_result_t m3_mp4_demux_open(const char *relative, unsigned job, bool (*current)(unsigned), m3_mp4_demux_t **out, m3_mp4_info_t *info);
void m3_mp4_demux_close(m3_mp4_demux_t *demux);
bool m3_mp4_demux_sample(m3_mp4_demux_t *demux, bool video, unsigned index, m3_mp4_sample_t *sample);
bool m3_mp4_demux_read(m3_mp4_demux_t *demux, const m3_mp4_sample_t *sample, uint8_t *out, size_t capacity);
/* Last video sync sample after index whose presentation time is <= until_us, else index. */
unsigned m3_mp4_demux_keyframe(m3_mp4_demux_t *demux, unsigned index, int64_t until_us);
const uint8_t *m3_mp4_demux_parameter(m3_mp4_demux_t *demux, bool pps, unsigned index, int *size);
