#pragma once
#include "lvgl.h"
#define M3_READER_FONT_COUNT 3
/* LVGL task only. Nanum Gothic, Gothic Bold, Myeongjo; CJK fallback retained. */
const lv_font_t *m3_reader_font(unsigned pixels, unsigned family);
/* Shares immutable metrics/atlases while pinning the selected system family. */
const lv_font_t *m3_system_font(unsigned pixels, unsigned family);
