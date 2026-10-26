#pragma once
#include "esp_err.h"
#include "m3_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum { M3_GALLERY_TILES = 9, M3_GALLERY_THUMB_W = 120, M3_GALLERY_THUMB_H = 120,
    M3_GALLERY_THUMB_BYTES = M3_GALLERY_THUMB_W * M3_GALLERY_THUMB_H * 2 };
typedef enum { M3_GALLERY_IDLE, M3_GALLERY_LOADING, M3_GALLERY_READY, M3_GALLERY_ENDED,
    M3_GALLERY_BAD_FORMAT, M3_GALLERY_STORAGE, M3_GALLERY_MEMORY, M3_GALLERY_MP4_FORMAT,
    M3_GALLERY_AUDIO_BUSY, M3_GALLERY_DECODE_ERROR, M3_GALLERY_AUDIO_ERROR, M3_GALLERY_STOPPED } m3_gallery_status_t;
typedef struct { m3_gallery_status_t status; bool video, paused, mp4; unsigned frames, revision;
    uint32_t position_ms, duration_ms; } m3_gallery_state_t;
esp_err_t m3_gallery_start(void);
bool m3_gallery_supported(const char *name);
bool m3_gallery_media(const char *name); /* Known media types, including codecs needing conversion. */
bool m3_gallery_video(const char *name);
bool m3_gallery_open(const char *relative);
void m3_gallery_close(void);
void m3_gallery_pause(void);
bool m3_gallery_active(void);
void m3_gallery_state(m3_gallery_state_t *out);
/* UI owner pins the returned buffer until its next call. */
const uint8_t *m3_gallery_frame(void);
void m3_gallery_mp4_run(const char *relative, unsigned job, const m3_audio_output_t *audio);
void m3_gallery_audio_cancel(void);
typedef struct { char path[256]; bool video; } m3_gallery_item_t;
typedef struct {
    m3_gallery_status_t status;
    unsigned generation, revision, page, count, shown;
    uint16_t done_mask, error_mask;
    m3_gallery_item_t items[M3_GALLERY_TILES];
} m3_gallery_library_t;
bool m3_gallery_library_request(unsigned page);
void m3_gallery_library_state(m3_gallery_library_t *out);
/* UI copies the single immutable mailbox into its own persistent canvas buffer. */
bool m3_gallery_thumbnail_copy(unsigned generation, unsigned index, uint8_t *out, size_t capacity);
bool m3_gallery_path_valid(const char *path);
