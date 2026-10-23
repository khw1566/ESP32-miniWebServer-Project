#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
/* Blobs (theme settings) are not persisted by the simulator; defaults are used. */
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length) {
    (void)h; (void)key; (void)out; (void)length; return ESP_FAIL;
}
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length) {
    (void)h; (void)key; (void)value; (void)length; return ESP_OK;
}
