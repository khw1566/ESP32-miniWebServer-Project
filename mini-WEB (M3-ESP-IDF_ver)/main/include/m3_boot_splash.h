#pragma once
#include "esp_err.h"
/* Waving dot-logo boot splash on the LVGL top layer. It removes itself after a
 * few seconds or on touch, so the home UI can be built underneath meanwhile. */
esp_err_t m3_boot_splash_start(void);
