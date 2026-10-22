#pragma once
#include "m3_audio_apps.h"
#include <stddef.h>
bool m3_azure_audio_ready(void);
void m3_azure_tts_run(const char *text, const m3_audio_output_t *output);
bool m3_azure_transcribe(const unsigned char *wav, size_t bytes, char text[385],
                         bool (*cancelled)(void *), void *context, const char **error);
