#pragma once
/* YouTube through the PC relay (tube-relay/, https://tube.audreaming.com).
 * Listings and thumbnails run on the m3_tube worker; playback runs on the audio
 * worker, which owns the codec. No function here calls LVGL. */
#include "esp_err.h"
#include "m3_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>

#define M3_TUBE_ITEMS 10
#define M3_TUBE_THUMB_W 160
#define M3_TUBE_THUMB_H 90
#define M3_TUBE_VIDEO_W 480
#define M3_TUBE_VIDEO_H 272

typedef struct {
    char id[12], title[160], channel[64], channel_id[25];
    uint32_t duration, views;
    bool live;
} m3_tube_item_t;

typedef enum { M3_TUBE_LIST_IDLE, M3_TUBE_LIST_LOADING, M3_TUBE_LIST_READY, M3_TUBE_LIST_ERROR } m3_tube_list_status_t;
typedef struct {
    m3_tube_list_status_t status;
    unsigned revision, page, count;
    bool more, channel;           /* channel: listing of one channel instead of a search */
    bool music;                   /* song listing for the YouTube Music app (no thumbnails) */
    char query[128], title[64];   /* title: channel name for channel listings */
    char message[96];             /* English service status; the UI translates it */
    m3_tube_item_t items[M3_TUBE_ITEMS];
    uint16_t thumbs;              /* bit i: thumbnail i is ready in m3_tube_thumb(list, i) */
    uint8_t thumb_set;            /* double buffer in use by this listing */
} m3_tube_list_t;

typedef struct {
    bool active, buffering, paused, video;
    bool audio_only;              /* started by the YouTube Music app */
    bool ended;                   /* last session reached the end of the track */
    unsigned revision, position_ms, duration_s, buffer_percent, dropped;
    char id[12], title[160], channel[64], channel_id[25];
} m3_tube_player_t;

bool m3_tube_configured(void);
typedef enum { M3_TUBE_LOW, M3_TUBE_STANDARD, M3_TUBE_HIGH } m3_tube_quality_t;
typedef struct { bool checking, ready; int http_status; uint32_t age_s; char message[96]; } m3_tube_server_t;
typedef struct { bool busy, saved; unsigned percent; uint32_t bytes, total; char message[96], filename[64]; } m3_tube_download_t;
void m3_tube_quality(m3_tube_quality_t quality);
m3_tube_quality_t m3_tube_get_quality(void);
/* Nonblocking; probes are throttled and performed by the listing worker. */
void m3_tube_probe(void);
void m3_tube_server(m3_tube_server_t *out);
bool m3_tube_download(const char *id, const char *title);
void m3_tube_download_cancel(void);
void m3_tube_download_state(m3_tube_download_t *out);
#define M3_TUBE_DOWNLOAD_FOLDER "/youtube"
esp_err_t m3_tube_start(void);
bool m3_tube_search(const char *query, unsigned page);
bool m3_tube_channel(const char *channel_id, unsigned page);
void m3_tube_list(m3_tube_list_t *out);
/* RGB565 160x90 image of a listing copy; valid while that copy's thumbs bit is set.
 * A newer listing writes the other buffer set, so a displayed set is never overwritten. */
const uint8_t *m3_tube_thumb(const m3_tube_list_t *list, unsigned index);

/* Playback is queued on the audio worker; stop it with m3_audio_stop(). */
bool m3_tube_play(const char *id, unsigned start_seconds);
/* YouTube Music: song search (separate from the video listing) and audio-only playback. */
bool m3_tube_music_search(const char *query, unsigned page);
void m3_tube_music_list(m3_tube_list_t *out);
bool m3_tube_play_audio(const char *id, unsigned start_seconds);
void m3_tube_pause(bool paused);
void m3_tube_player(m3_tube_player_t *out);
/* Newest decoded 480x272 RGB565 frame that is due at the audio clock, or NULL.
 * The returned buffer stays untouched until the next call. UI task only. */
const uint8_t *m3_tube_frame(void);
/* Audio worker entry point. */
void m3_tube_run(const char *id, unsigned start_seconds, bool audio_only, const m3_audio_output_t *output);
bool m3_audio_tube(const char *id, unsigned start_seconds, bool audio_only);
