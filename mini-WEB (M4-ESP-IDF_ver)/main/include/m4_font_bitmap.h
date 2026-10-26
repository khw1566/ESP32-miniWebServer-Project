#pragma once
#include "lvgl.h"
/* Expand immutable 1bpp Flash data into LVGL's own draw buffer, no persistent heap. */
const void *m4_font_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer);
