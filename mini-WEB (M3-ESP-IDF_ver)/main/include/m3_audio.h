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
bool m3_audio_record(void);
bool m3_audio_recording_play(const char *filename);
bool m3_audio_mp4(const char *relative, unsigned gallery_job);
bool m3_audio_radio(const char *url, bool aac);
bool m3_audio_cloud_tts(const char *text);
void m3_audio_tts_provider(unsigned provider);
bool m3_audio_speak(const char *text);
bool m3_audio_voice(void);
void m3_audio_stop(void);
void m3_audio_volume(unsigned percent);
void m3_audio_state(m3_audio_state_t *out);
/* Best-effort UI feedback; never interrupts media, recording, or queued requests. */
bool m3_audio_click(void);
