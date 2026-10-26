#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool busy, recording, key_ready;
    unsigned revision, job, provider;
    char text[385], message[96];
} m4_voice_state_t;
typedef struct {
    bool (*begin)(void);
    bool (*read)(int16_t *mono, unsigned samples);
    void (*end)(void);
    bool (*cancelled)(void);
} m4_voice_capture_t;
/* 0 OpenAI mini-transcribe; 1 Gemini 3.1; 2 Gemini 3.5; 3 Azure Speech. */
bool m4_voice_set_provider(unsigned provider);
bool m4_voice_set_key(const char *key);
void m4_voice_state(m4_voice_state_t *out);
/* Live microphone loudness while recording, updated every 10 ms block.
 * level: 0..100 (-60 dBFS .. -10 dBFS, fast attack, slow release); dbfs: whole dB, -90 when silent/idle. */
unsigned m4_voice_level(void);
int m4_voice_dbfs(void);
unsigned m4_voice_prepare(void);
void m4_voice_finish(void);
void m4_voice_cancel(void);
void m4_voice_run(unsigned job, const m4_voice_capture_t *capture);
