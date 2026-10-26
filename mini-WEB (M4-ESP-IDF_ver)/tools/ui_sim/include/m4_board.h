#pragma once
/* Host simulator replacement for the board header. */
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#define M4_WIDTH 480
#define M4_HEIGHT 800
lv_display_t *m4_board_display(void);
bool m4_board_touch_ready(void);
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
