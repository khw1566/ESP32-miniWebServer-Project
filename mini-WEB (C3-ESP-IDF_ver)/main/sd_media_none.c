/* This board has no SD slot. The cloud relay and public file stream share
 * code with the SD edition, so the SD API reports "not available" here. */
#include "sd_media.h"

esp_err_t sd_media_init(void) { return ESP_ERR_NOT_SUPPORTED; }
bool sd_media_ready(void) { return false; }
bool sd_media_admin_lock(void) { return false; }
void sd_media_admin_unlock(void) {}
bool sd_media_name_valid(const char *name) { (void)name; return false; }
cJSON *sd_media_catalog(void) { return NULL; }
cJSON *sd_media_command(const cJSON *command) {
    (void)command;
    cJSON *out = cJSON_CreateObject();
    if (!out) return NULL;
    cJSON_AddNumberToObject(out, "status", 503);
    cJSON_AddStringToObject(out, "message", "This board has no SD card slot");
    cJSON_AddNumberToObject(out, "offset", 0);
    return out;
}
