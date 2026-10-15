#include "m3_c6_update.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_media.h"

#define C6_UPDATE_PATH SD_MEDIA_ROOT "/c6_update.bin"
#define C6_DONE_PATH   SD_MEDIA_ROOT "/c6_update.done"
#define C6_FAILED_PATH SD_MEDIA_ROOT "/c6_update.failed"
/* OTA writes into the C6 partition table that is already in its flash, which
 * cannot change over OTA: two 1536 KiB app slots (m3_c6_partitions.csv). */
#define C6_SLOT_BYTES  (1536U * 1024U)
/* Chunk size accepted by every Hosted slave since 1.4.1. */
#define C6_CHUNK_BYTES 1400U

static const char *TAG = "C6_UPDATE";

static void retire(const char *target) {
    remove(target);
    if (rename(C6_UPDATE_PATH, target) != 0) {
        /* Never retry the same image on every boot. */
        if (remove(C6_UPDATE_PATH) != 0) ESP_LOGE(TAG, "Cannot retire %s; remove it manually", C6_UPDATE_PATH);
    }
}

static esp_err_t read_image_version(FILE *file, char *version, size_t size) {
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
    if (fread(&image, sizeof(image), 1, file) != 1 || fread(&segment, sizeof(segment), 1, file) != 1 ||
        fread(&app, sizeof(app), 1, file) != 1) return ESP_ERR_INVALID_SIZE;
    if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.chip_id != ESP_CHIP_ID_ESP32C6 ||
        app.magic_word != ESP_APP_DESC_MAGIC_WORD) return ESP_ERR_INVALID_VERSION;
    snprintf(version, size, "%.*s", (int)sizeof(app.version), app.version);
    ESP_LOGI(TAG, "Staged image: project=%.*s version=%s", (int)sizeof(app.project_name), app.project_name, version);
    return fseek(file, 0, SEEK_SET) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t m3_c6_update_from_sd(void) {
    struct stat st;
    if (!sd_media_ready() || stat(C6_UPDATE_PATH, &st) != 0) return ESP_ERR_NOT_FOUND;
    if (st.st_size <= 0 || (size_t)st.st_size > C6_SLOT_BYTES) {
        ESP_LOGE(TAG, "%s is %ld bytes; the C6 app slot holds %u bytes", C6_UPDATE_PATH, (long)st.st_size, C6_SLOT_BYTES);
        retire(C6_FAILED_PATH);
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = fopen(C6_UPDATE_PATH, "rb");
    if (!file) return ESP_FAIL;
    char staged[33] = {0};
    esp_err_t err = read_image_version(file, staged, sizeof(staged));
    if (err != ESP_OK) {
        fclose(file);
        ESP_LOGE(TAG, "%s is not an ESP32-C6 application image (%s)", C6_UPDATE_PATH, esp_err_to_name(err));
        retire(C6_FAILED_PATH);
        return err;
    }
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        fclose(file);
        ESP_LOGE(TAG, "C6 transport is down; keeping %s for the next boot", C6_UPDATE_PATH);
        return ESP_ERR_INVALID_STATE;
    }
    esp_hosted_coprocessor_fwver_t running = {0};
    bool known = esp_hosted_get_coprocessor_fwversion(&running) == ESP_OK;
    char current[33] = "unknown";
    if (known) snprintf(current, sizeof(current), "%" PRIu32 ".%" PRIu32 ".%" PRIu32, running.major1, running.minor1, running.patch1);
    ESP_LOGI(TAG, "C6 firmware %s, staged %s", current, staged);
    if (known && !strcmp(current, staged)) {
        fclose(file);
        retire(C6_DONE_PATH);
        return ESP_OK;
    }

    uint8_t *chunk = malloc(C6_CHUNK_BYTES);
    if (!chunk) { fclose(file); return ESP_ERR_NO_MEM; }
    ESP_LOGW(TAG, "Updating C6 %s -> %s; do not power off", current, staged);
    err = esp_hosted_slave_ota_begin();
    size_t sent = 0;
    unsigned chunks = 0;
    while (err == ESP_OK) {
        size_t n = fread(chunk, 1, C6_CHUNK_BYTES, file);
        if (n == 0) { if (ferror(file)) err = ESP_FAIL; break; }
        err = esp_hosted_slave_ota_write(chunk, n);
        sent += n;
        if (err == ESP_OK && ++chunks % 128 == 0) ESP_LOGI(TAG, "Sent %u / %ld bytes", (unsigned)sent, (long)st.st_size);
    }
    /* end validates the image and only then switches the C6 boot slot. */
    if (err == ESP_OK) err = esp_hosted_slave_ota_end();
    free(chunk);
    fclose(file);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 update failed after %u bytes (%s); the previous C6 firmware stays active", (unsigned)sent, esp_err_to_name(err));
        retire(C6_FAILED_PATH);
        return err;
    }
    retire(C6_DONE_PATH);
    /* Slaves before 2.6 reboot themselves after OTA end; newer ones wait for activate. */
    if (known && (running.major1 > 2 || (running.major1 == 2 && running.minor1 > 5))) {
        esp_err_t activate = esp_hosted_slave_ota_activate();
        if (activate != ESP_OK) ESP_LOGE(TAG, "C6 activate failed: %s", esp_err_to_name(activate));
    }
    ESP_LOGW(TAG, "C6 updated to %s; restarting to resynchronize", staged);
    vTaskDelay(pdMS_TO_TICKS(6000));
    esp_restart();
    return ESP_OK;
}
