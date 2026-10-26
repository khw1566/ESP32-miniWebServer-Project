#pragma once
/* YouTube through the PC relay (tube-relay/, https://tube.audreaming.com).
 * Listings and thumbnails run on the m4_tube worker; playback runs on the audio
 * worker, which owns the codec. No function here calls LVGL. */
#include "esp_err.h"
#include "m4_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>

#define M4_TUBE_ITEMS 10
#define M4_TUBE_QUERY_BYTES 385
#define M4_TUBE_THUMB_W 160
#define M4_TUBE_THUMB_H 90
#define M4_TUBE_VIDEO_W 480
#define M4_TUBE_VIDEO_H 272

typedef struct {
    char id[12], title[160], channel[64], channel_id[25];
    uint32_t duration, views;
    bool live;
} m4_tube_item_t;

typedef enum { M4_TUBE_LIST_IDLE, M4_TUBE_LIST_LOADING, M4_TUBE_LIST_READY, M4_TUBE_LIST_ERROR } m4_tube_list_status_t;
typedef struct {
    m4_tube_list_status_t status;
    unsigned revision, page, count;
    bool more, channel;           /* channel: listing of one channel instead of a search */
    bool music;                   /* separate song listing and thumbnail buffers */
    char query[M4_TUBE_QUERY_BYTES], title[64];   /* title: channel name for channel listings */
    char message[96];             /* English service status; the UI translates it */
    m4_tube_item_t items[M4_TUBE_ITEMS];
    uint16_t thumbs;              /* bit i: thumbnail i is ready in m4_tube_thumb(list, i) */
    uint8_t thumb_set;            /* video 0/1, music 2/3; double buffers per app */
} m4_tube_list_t;

typedef struct {
    bool active, buffering, paused, video;
    bool audio_only;              /* started by the YouTube Music app */
    bool ended;                   /* last session reached the end of the track */
    unsigned revision, position_ms, duration_s, buffer_percent, dropped;
    char id[12], title[160], channel[64], channel_id[25];
} m4_tube_player_t;

bool m4_tube_configured(void);
typedef enum { M4_TUBE_LOW, M4_TUBE_STANDARD, M4_TUBE_HIGH } m4_tube_quality_t;
typedef struct { bool checking, ready; int http_status; uint32_t age_s; char message[96]; } m4_tube_server_t;
typedef struct { bool busy, saved, music; unsigned percent; uint32_t bytes, total; char message[96], filename[64]; } m4_tube_download_t;
typedef struct { bool ready, loading; unsigned percent; uint32_t bytes, total; char message[96]; } m4_tube_music_cache_t;
/* Nonblocking desired pair. A worker loads compressed MP3 into two bounded PSRAM slots. */
void m4_tube_music_prepare(const char *current, const char *next);
void m4_tube_music_autonext(bool enabled);
void m4_tube_music_cache(const char *id, m4_tube_music_cache_t *out);
bool m4_tube_music_download(const char *id, const char *title);
void m4_tube_quality(m4_tube_quality_t quality);
m4_tube_quality_t m4_tube_get_quality(void);
/* Nonblocking; probes are throttled and performed by the listing worker. */
void m4_tube_probe(void);
void m4_tube_server(m4_tube_server_t *out);
bool m4_tube_download(const char *id, const char *title);
void m4_tube_download_cancel(void);
void m4_tube_download_state(m4_tube_download_t *out);
#define M4_TUBE_DOWNLOAD_FOLDER "/youtube"
esp_err_t m4_tube_start(void);
bool m4_tube_search(const char *query, unsigned page);
bool m4_tube_channel(const char *channel_id, unsigned page);
void m4_tube_list(m4_tube_list_t *out);
/* RGB565 160x90 image of a listing copy; valid while that copy's thumbs bit is set.
 * A newer listing writes the other buffer set, so a displayed set is never overwritten. */
const uint8_t *m4_tube_thumb(const m4_tube_list_t *list, unsigned index);

/* Playback is queued on the audio worker; stop it with m4_audio_stop(). */
bool m4_tube_play(const char *id, unsigned start_seconds);
/* YouTube Music: song search (separate from the video listing) and audio-only playback. */
bool m4_tube_music_search(const char *query, unsigned page);
void m4_tube_music_list(m4_tube_list_t *out);
bool m4_tube_play_audio(const char *id, unsigned start_seconds);
void m4_tube_pause(bool paused);
void m4_tube_player(m4_tube_player_t *out);
/* Newest decoded 480x272 RGB565 frame that is due at the audio clock, or NULL.
 * The returned buffer stays untouched until the next call. UI task only. */
const uint8_t *m4_tube_frame(void);
/* Audio worker entry point. */
void m4_tube_run(const char *id, unsigned start_seconds, bool audio_only, const m4_audio_output_t *output);
bool m4_audio_tube(const char *id, unsigned start_seconds, bool audio_only);
bool m4_audio_tube_follow(const char *id); /* Sole audio worker; preserves stop epoch and queued user actions. */
