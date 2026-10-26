#include "m4_wifi_performance.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Mutex and reference count remain in internal RAM. RPCs never run in a spinlock. */
static SemaphoreHandle_t lock;
static unsigned users;
static wifi_ps_type_t original_ps;
static bool changed;
esp_err_t m4_wifi_performance_init(void) {
    if (lock) return ESP_OK;
    lock = xSemaphoreCreateMutex();
    return lock ? ESP_OK : ESP_ERR_NO_MEM;
}
bool m4_wifi_performance_acquire(void) {
    if (!lock || xSemaphoreTake(lock,portMAX_DELAY) != pdTRUE) return false;
    bool ok = true;
    if (!users) {
        ok = esp_wifi_get_ps(&original_ps) == ESP_OK;
        changed = ok && original_ps != WIFI_PS_NONE;
        if (changed) ok = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
    }
    if (ok) ++users;
    xSemaphoreGive(lock);
    return ok;
}
void m4_wifi_performance_release(void) {
    if (!lock || xSemaphoreTake(lock,portMAX_DELAY) != pdTRUE) return;
    if (users && !--users && changed) {
        esp_err_t err = esp_wifi_set_ps(original_ps);
        if (err != ESP_OK) ESP_LOGW("WIFI_PERF","Power policy restore failed: %s",esp_err_to_name(err));
        changed = false;
    }
    xSemaphoreGive(lock);
}
