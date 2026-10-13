#pragma once
#include "m3_audio_apps.h"
#include <stddef.h>
bool m3_google_tts_ready(void);
bool m3_google_stt_ready(void);
void m3_google_tts_run(const char *text, const m3_audio_output_t *output);
bool m3_google_transcribe(const unsigned char *wav, size_t bytes, unsigned model,
                          char text[385], bool (*cancelled)(void *), void *context,
                          const char **error);

bool m3_openai_audio_ready(void);
void m3_openai_tts_run(const char *text, const m3_audio_output_t *output);
