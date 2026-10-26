#pragma once
#include "esp_err.h"
/* Waving dot-logo boot splash on the LVGL top layer. It stays up with a progress
 * bar until boot reaches M4_BOOT_READY (or a safety timeout), then fades out. */
typedef enum {
    M4_BOOT_DISPLAY, M4_BOOT_WORKERS, M4_BOOT_UI, M4_BOOT_STORAGE,
    M4_BOOT_NETWORK, M4_BOOT_CONNECT, M4_BOOT_WEB, M4_BOOT_READY,
} m4_boot_step_t;
esp_err_t m4_boot_splash_start(void);
/* Any task may call this; it only records the step and never takes the LVGL lock.
 * Steps only move forward, and the call is a no-op when no splash is shown. */
void m4_boot_splash_step(m4_boot_step_t step);
