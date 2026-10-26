#pragma once
#include "esp_err.h"

/* Call once from app_main, after IDF has reclaimed the startup stack heaps. */
esp_err_t m4_hosted_start(void);
