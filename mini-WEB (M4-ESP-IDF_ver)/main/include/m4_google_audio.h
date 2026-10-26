#pragma once
#include "m4_audio_apps.h"
#include <stddef.h>
bool m4_google_tts_ready(void);
bool m4_google_stt_ready(void);
void m4_google_tts_run(const char *text, const m4_audio_output_t *output);
bool m4_google_transcribe(const unsigned char *wav, size_t bytes, unsigned model,
                          char text[385], bool (*cancelled)(void *), void *context,
                          const char **error);

bool m4_openai_audio_ready(void);
void m4_openai_tts_run(const char *text, const m4_audio_output_t *output);
