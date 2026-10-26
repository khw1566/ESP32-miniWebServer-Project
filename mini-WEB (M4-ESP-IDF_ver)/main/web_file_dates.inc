/* Persistent file dates: SPIFFS does not retain native modification times. */
#include <time.h>
#include <stdint.h>
#define FILE_DATE_NAMESPACE "web_dates"
typedef struct { int64_t milliseconds; char name[128]; } file_date_record_t;
static void file_date_key(const char *name, char key[16]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        hash ^= *p; hash *= UINT64_C(1099511628211);
    }
    snprintf(key, 16, "m%014llx", (unsigned long long)(hash & UINT64_C(0x00ffffffffffffff)));
}
static int64_t file_date_valid(double value) {
    /* Ignore invalid clocks; dates through year 2100 fit exactly in JSON numbers. */
    return value >= 946684800000.0 && value < 4102444800000.0 ? (int64_t)value : 0;
}
static int64_t file_date_now(void) { return file_date_valid((double)time(NULL) * 1000.0); }
static int64_t file_date_read(const char *name) {
    char key[16]; file_date_key(name, key);
    nvs_handle_t h;
    if (nvs_open(FILE_DATE_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    file_date_record_t record = {0}; size_t size = sizeof(record);
    esp_err_t err = nvs_get_blob(h, key, &record, &size); nvs_close(h);
    if (err != ESP_OK || size != sizeof(record) || !memchr(record.name, 0, sizeof(record.name)) || strcmp(record.name, name)) return 0;
    return file_date_valid((double)record.milliseconds);
}
static void file_date_remove(const char *name) {
    char key[16]; file_date_key(name, key); nvs_handle_t h;
    if (nvs_open(FILE_DATE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "File date removal failed: %s", esp_err_to_name(err));
}
static void file_date_write(const char *name, int64_t milliseconds) {
    if (!name || strlen(name) >= 128) return;
    if (!milliseconds) { file_date_remove(name); return; }
    file_date_record_t record = {0}; record.milliseconds = milliseconds;
    snprintf(record.name, sizeof(record.name), "%s", name);
    char key[16]; file_date_key(name, key); nvs_handle_t h;
    esp_err_t err = nvs_open(FILE_DATE_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, &record, sizeof(record));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        file_date_remove(name); /* Do not show an earlier date for a new revision. */
        ESP_LOGW(TAG, "File saved but modification date could not be saved: %s", esp_err_to_name(err));
    }
}
static esp_err_t file_dates_clear(void) {
    nvs_handle_t h; esp_err_t err = nvs_open(FILE_DATE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err;
}
