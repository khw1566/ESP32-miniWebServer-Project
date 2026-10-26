#pragma once
#include "lvgl.h"

/* Preserve app geometry and media buffers at 480x800. Uniform 5/4 scaling
 * fills 600x1000 of the 600x1024 portrait display, with 12 px margins. */
enum { M4_LAYOUT_WIDTH = 480, M4_LAYOUT_HEIGHT = 800,
       M4_LAYOUT_SCALE = 320, M4_LAYOUT_TOP = 12 };
lv_obj_t *m4_layout_create_root(lv_obj_t *parent);
void m4_layout_get_point(const lv_indev_t *indev, lv_point_t *point);
