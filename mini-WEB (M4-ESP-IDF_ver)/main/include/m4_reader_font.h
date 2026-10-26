#pragma once
#include "lvgl.h"
/* LVGL task only. Shared Nanum/Unifont/Montserrat data, no new glyph atlas. */
const lv_font_t *m4_reader_font(unsigned pixels);
