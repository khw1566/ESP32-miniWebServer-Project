#pragma once
#include "esp_err.h"

typedef enum {
    M1_SOUND_BOOT,
    M1_SOUND_WIFI_CONNECTED,
    M1_SOUND_WIFI_LOST,
    M1_SOUND_WIFI_FAILED,
    M1_SOUND_AP_READY,
    M1_SOUND_SERVER_START,
    M1_SOUND_SERVER_STOP,
    M1_SOUND_DOMAIN_CONNECTED,
    M1_SOUND_ERROR,
    M1_SOUND_VOLUME,
    M1_SOUND_COUNT
} m1_sound_event_t;

/* Initialize once from app_main, before network/UI producers start. */
esp_err_t m1_sound_init(void);
/* Task context only. Never waits for playback; drops excess notifications. */
void m1_sound_play(m1_sound_event_t event);
/* 0=mute, 1=-18dB, 2=-12dB, 3=-6dB, 4=full amplitude. */
unsigned m1_sound_get_volume(void);
/* Call from a worker task: commits to NVS before changing the live volume. */
esp_err_t m1_sound_set_volume(unsigned level);
