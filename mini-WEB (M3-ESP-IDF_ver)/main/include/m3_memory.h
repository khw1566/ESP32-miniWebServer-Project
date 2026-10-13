#pragma once
/* Host previews have no ESP memory regions. Firmware requires the external-BSS
 * configuration and PSRAM boot initialization before these objects are used. */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "sdkconfig.h"
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "M3 requires external BSS in PSRAM; run idf.py reconfigure"
#endif
#define M3_PSRAM_BSS EXT_RAM_BSS_ATTR
#else
#define M3_PSRAM_BSS
#endif
