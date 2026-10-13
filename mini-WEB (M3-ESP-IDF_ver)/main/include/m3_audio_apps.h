#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Called only by the one audio worker. No LVGL or second codec owner. */
typedef struct {
    bool (*begin)(uint32_t rate);
    bool (*write)(void *samples, unsigned bytes);
    bool (*cancelled)(void);
    bool (*drain)(void);
    void (*message)(const char *text, bool playing, unsigned progress);
} m3_audio_output_t;
bool m3_radio_url_valid(const char *url);
void m3_radio_run(const char *url, bool aac, const m3_audio_output_t *output);
void m3_tts_run(const char *text, unsigned speed, const m3_audio_output_t *output);
bool m3_tts_text_valid(const char *text);
bool m3_audio_decoders_ready(void); /* Sole audio worker; shared MP3/AAC registry. */
int m3_tts_data_mount(void);
