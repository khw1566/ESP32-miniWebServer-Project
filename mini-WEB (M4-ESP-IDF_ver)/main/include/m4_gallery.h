#pragma once
#include "esp_err.h"
#include "m4_audio_apps.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum { M4_GALLERY_TILES = 9, M4_GALLERY_THUMB_W = 120, M4_GALLERY_THUMB_H = 120,
    M4_GALLERY_THUMB_BYTES = M4_GALLERY_THUMB_W * M4_GALLERY_THUMB_H * 2 };
typedef enum { M4_GALLERY_IDLE, M4_GALLERY_LOADING, M4_GALLERY_READY, M4_GALLERY_ENDED,
    M4_GALLERY_BAD_FORMAT, M4_GALLERY_STORAGE, M4_GALLERY_MEMORY, M4_GALLERY_MP4_FORMAT,
    M4_GALLERY_AUDIO_BUSY, M4_GALLERY_DECODE_ERROR, M4_GALLERY_AUDIO_ERROR, M4_GALLERY_STOPPED } m4_gallery_status_t;
typedef struct { m4_gallery_status_t status; bool video, paused, mp4; unsigned frames, revision;
    uint32_t position_ms, duration_ms; } m4_gallery_state_t;
esp_err_t m4_gallery_start(void);
bool m4_gallery_supported(const char *name);
bool m4_gallery_media(const char *name); /* Known media types, including codecs needing conversion. */
bool m4_gallery_video(const char *name);
bool m4_gallery_open(const char *relative);
void m4_gallery_close(void);
void m4_gallery_pause(void);
bool m4_gallery_active(void);
void m4_gallery_state(m4_gallery_state_t *out);
/* UI owner pins the returned buffer until its next call. */
const uint8_t *m4_gallery_frame(void);
void m4_gallery_mp4_run(const char *relative, unsigned job, const m4_audio_output_t *audio);
void m4_gallery_audio_cancel(void);
typedef struct { char path[256]; bool video; } m4_gallery_item_t;
typedef struct {
    m4_gallery_status_t status;
    unsigned generation, revision, page, count, shown;
    uint16_t done_mask, error_mask;
    m4_gallery_item_t items[M4_GALLERY_TILES];
} m4_gallery_library_t;
bool m4_gallery_library_request(unsigned page);
void m4_gallery_library_state(m4_gallery_library_t *out);
/* UI copies the single immutable mailbox into its own persistent canvas buffer. */
bool m4_gallery_thumbnail_copy(unsigned generation, unsigned index, uint8_t *out, size_t capacity);
bool m4_gallery_path_valid(const char *path);
