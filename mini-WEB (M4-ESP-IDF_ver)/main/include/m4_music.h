#pragma once
#include "m4_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { M4_MUSIC_REPEAT_OFF, M4_MUSIC_REPEAT_ALL, M4_MUSIC_REPEAT_ONE } m4_music_repeat_t;
enum { M4_MUSIC_ROWS = 5, M4_MUSIC_TRACKS = 64 };
typedef struct { char name[192]; unsigned index; } m4_music_item_t;
typedef struct {
    bool sd, scanning, active, paused, repeat, shuffle;
    m4_music_repeat_t repeat_mode;
    int eq_db[3];
    uint8_t bands[3];
    unsigned revision, catalog_revision, count, page, shown, selected, progress, seconds;
    char title[192], message[96];
    m4_music_item_t items[M4_MUSIC_ROWS];
} m4_music_state_t;
bool m4_music_path_valid(const char *path);
void m4_music_refresh(bool sd);
void m4_music_poll(void); /* Existing service worker: filesystem access, no LVGL. */
void m4_music_page(int direction);
bool m4_music_play(unsigned index, unsigned catalog_revision);
bool m4_music_next(int direction);
void m4_music_pause(void);
void m4_music_repeat(bool enabled);
void m4_music_repeat_mode(m4_music_repeat_t mode);
void m4_music_shuffle(bool enabled);
void m4_music_equalizer(unsigned band, int db);
void m4_music_stop(void);
void m4_music_state(m4_music_state_t *out);
void m4_music_run(const char *path, const m4_audio_output_t *out);
