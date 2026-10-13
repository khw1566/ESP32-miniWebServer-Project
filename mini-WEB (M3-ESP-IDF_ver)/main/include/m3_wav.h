#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    uint32_t sample_rate, data_bytes;
    uint16_t channels;
    long data_offset;
} m3_wav_info_t;
/* Accept bounded PCM16 RIFF WAV only. Leaves file at the start of PCM data. */
bool m3_wav_parse(FILE *file, m3_wav_info_t *info);
bool m3_wav_parse_limit(FILE *file, m3_wav_info_t *info, unsigned max_bytes);
