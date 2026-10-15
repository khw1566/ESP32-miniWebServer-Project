#pragma once
#include "esp_err.h"
/* Waving dot-logo boot splash on the LVGL top layer. It stays up with a progress
 * bar until boot reaches M3_BOOT_READY (or a safety timeout), then fades out. */
typedef enum {
    M3_BOOT_DISPLAY, M3_BOOT_WORKERS, M3_BOOT_UI, M3_BOOT_STORAGE,
    M3_BOOT_NETWORK, M3_BOOT_CONNECT, M3_BOOT_WEB, M3_BOOT_READY,
} m3_boot_step_t;
esp_err_t m3_boot_splash_start(void);
/* Any task may call this; it only records the step and never takes the LVGL lock.
 * Steps only move forward, and the call is a no-op when no splash is shown. */
void m3_boot_splash_step(m3_boot_step_t step);
