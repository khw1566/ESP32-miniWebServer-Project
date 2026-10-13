#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool ready, playing;
    unsigned volume, progress;
    char message[96];
} m3_audio_state_t;
esp_err_t m3_audio_start(void);
bool m3_audio_play(const char *media_filename);
bool m3_audio_music(const char *path);
bool m3_audio_music_follow(const char *path); /* Audio worker only; preserves cancel token and queued user actions. */
bool m3_audio_test(void);
bool m3_audio_radio(const char *url, bool aac);
bool m3_audio_tts(const char *text, unsigned speed);
bool m3_audio_cloud_tts(const char *text);
void m3_audio_tts_provider(unsigned provider);
bool m3_audio_speak(const char *text, unsigned speed);
bool m3_audio_voice(void);
void m3_audio_stop(void);
void m3_audio_volume(unsigned percent);
void m3_audio_state(m3_audio_state_t *out);
