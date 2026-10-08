#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#define SD_MEDIA_ROOT "/sdcard"
#define SD_MEDIA_MAX_BYTES (8U * 1024U * 1024U)
esp_err_t sd_media_init(void);
bool sd_media_ready(void);
uint32_t sd_media_revision(void);
void sd_media_changed(void);
/* Serialize local management with cloud uploads. */
bool sd_media_admin_lock(void);
void sd_media_admin_unlock(void);
bool sd_media_name_valid(const char *name);
cJSON *sd_media_catalog(void);
/* Cloud device-authenticated commands only; never expose this to public GET. */
cJSON *sd_media_command(const cJSON *command);
