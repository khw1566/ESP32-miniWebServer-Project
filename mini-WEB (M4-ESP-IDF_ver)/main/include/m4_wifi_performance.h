#pragma once
#include <stdbool.h>
#include "esp_err.h"
/* Initialize once in network_init, before network/audio/relay workers run. */
esp_err_t m4_wifi_performance_init(void);
/* Task context only. A failed/unsupported Hosted RPC is non-fatal. */
bool m4_wifi_performance_acquire(void);
void m4_wifi_performance_release(void);
