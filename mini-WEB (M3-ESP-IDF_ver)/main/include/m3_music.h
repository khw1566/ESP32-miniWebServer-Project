#pragma once
#include "m3_audio_apps.h"
#include <stdbool.h>
enum { M3_MUSIC_ROWS = 5, M3_MUSIC_TRACKS = 64 };
typedef struct { char name[192]; unsigned index; } m3_music_item_t;
typedef struct {
    bool sd, scanning, active, paused, repeat;
    unsigned revision, catalog_revision, count, page, shown, selected, progress, seconds;
    char title[192], message[96];
    m3_music_item_t items[M3_MUSIC_ROWS];
} m3_music_state_t;
bool m3_music_path_valid(const char *path);
void m3_music_refresh(bool sd);
void m3_music_poll(void); /* Existing service worker: filesystem access, no LVGL. */
void m3_music_page(int direction);
bool m3_music_play(unsigned index, unsigned catalog_revision);
bool m3_music_next(int direction);
void m3_music_pause(void);
void m3_music_repeat(bool enabled);
void m3_music_stop(void);
void m3_music_state(m3_music_state_t *out);
void m3_music_run(const char *path, const m3_audio_output_t *out);
