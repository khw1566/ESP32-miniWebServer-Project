#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { const uint8_t *data; size_t bytes; const char *mime; } m3_web_asset_t;
/* Boot service task only, before HTTP/cloud workers start. Immutable until reboot. */
esp_err_t m3_web_assets_load(void);
bool m3_web_assets_ready(void);
unsigned m3_web_assets_count(void);
size_t m3_web_assets_bytes(void);
const m3_web_asset_t *m3_web_assets_find(const char *path);
bool m3_web_assets_path_valid(const char *path);
bool m3_web_assets_slice(size_t bytes, const char *range, size_t *offset, size_t *length);
