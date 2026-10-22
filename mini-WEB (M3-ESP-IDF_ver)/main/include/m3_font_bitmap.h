#pragma once
#include "lvgl.h"
/* Korean/CJK 1bpp glyphs are stored zlib-packed in Flash (tools/font_pack.py) and
 * inflated once into PSRAM on first use; font->dsc points at this blob. */
typedef struct {
    const uint8_t *packed;
    uint32_t packed_size, size;
    uint8_t *data;
} m3_font_blob_t;
/* Raw bitmap of a packed font, or NULL while memory is unavailable (retried later). */
const uint8_t *m3_font_blob_data(const lv_font_t *font);
/* Expand 1bpp glyph data into LVGL's own draw buffer, no per-glyph heap. */
const void *m3_font_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer);
