#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool ready, playing;
    unsigned volume, progress;
    char message[96];
} m4_audio_state_t;
esp_err_t m4_audio_start(void);
bool m4_audio_play(const char *media_filename);
bool m4_audio_music(const char *path);
bool m4_audio_music_follow(const char *path); /* Audio worker only; preserves cancel token and queued user actions. */
bool m4_audio_record(void);
bool m4_audio_recording_play(const char *filename);
bool m4_audio_mp4(const char *relative, unsigned gallery_job);
bool m4_audio_radio(const char *url, bool aac);
bool m4_audio_radio_preset(const char *station, unsigned profile); /* 0:32k mono, 1:64k mono, 2:128k stereo. */
bool m4_audio_tts(const char *text, unsigned speed);
bool m4_audio_cloud_tts(const char *text);
void m4_audio_tts_provider(unsigned provider);
unsigned m4_audio_tts_selected(void);
bool m4_audio_speak(const char *text, unsigned speed);
bool m4_audio_voice(void);
void m4_audio_stop(void);
void m4_audio_volume(unsigned percent);
void m4_audio_state(m4_audio_state_t *out);
