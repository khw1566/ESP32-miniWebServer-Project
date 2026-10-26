#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "m4_voice.h"

#define M4_RECORDER_FOLDER "/recordings"
#define M4_RECORDER_RATE 16000U
#define M4_RECORDER_MAX_SECONDS 1800U
typedef struct {
    bool busy, recording, saved;
    unsigned job, revision, seconds, level;
    uint32_t bytes;
    char filename[64], message[96];
    int error_code;
    char error_stage[24];
} m4_recorder_state_t;
void m4_recorder_state(m4_recorder_state_t *out);
unsigned m4_recorder_prepare(void);
void m4_recorder_finish(void);
void m4_recorder_cancel(void);
void m4_recorder_run(unsigned job, const m4_voice_capture_t *capture);
void m4_recorder_wav_header(uint8_t out[44], uint32_t pcm_bytes);
