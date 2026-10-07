#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Releases the on-board UI while the display is off. All functions must be
 * called from the LVGL/UI task. */
typedef void (*screen_saver_teardown_t)(void);

/* Safe inside LVGL event callbacks: entry is deferred to the UI loop. */
void screen_saver_request(void);
bool screen_saver_active(void);
/* Drives entry and wake. Returns true once after a touch restored the display
 * and the finger was lifted; the caller then rebuilds a page and calls
 * screen_saver_reveal(). */
bool screen_saver_service(bool can_enter, screen_saver_teardown_t teardown);
void screen_saver_reveal(void);
