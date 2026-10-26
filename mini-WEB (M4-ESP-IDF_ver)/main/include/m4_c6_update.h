#pragma once
#include "esp_err.h"

/* Optional C6 co-processor update staged on the SD card. Call after the SD
 * card is mounted and before esp_wifi_init(). Returns ESP_ERR_NOT_FOUND when
 * nothing is staged; on a successful flash the host restarts and never returns. */
esp_err_t m4_c6_update_from_sd(void);
