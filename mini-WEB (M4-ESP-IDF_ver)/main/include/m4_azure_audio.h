#pragma once
#include "m4_audio_apps.h"
#include <stddef.h>
bool m4_azure_audio_ready(void);
void m4_azure_tts_run(const char *text, const m4_audio_output_t *output);
bool m4_azure_transcribe(const unsigned char *wav, size_t bytes, char text[385],
                         bool (*cancelled)(void *), void *context, const char **error);
