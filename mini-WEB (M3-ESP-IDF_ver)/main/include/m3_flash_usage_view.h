#pragma once
#include "lvgl.h"

/* Settings card content: app slot usage, a stacked bar by group and a per-function list. */
typedef struct {
    uint32_t text, muted, accent, track;
    lv_style_t *button, *button_pressed;
} m3_flash_view_style_t;

void m3_flash_usage_view_build(lv_obj_t *box, const m3_flash_view_style_t *style);
/* Re-apply the current UI language to every label of the view. */
void m3_flash_usage_view_refresh(void);
