#pragma once
#include "esp_err.h"

/* Periodically logs internal heap and task stack headroom.
 * Used to size task stacks from measurements instead of estimates. */
esp_err_t memory_monitor_start(unsigned period_seconds);
