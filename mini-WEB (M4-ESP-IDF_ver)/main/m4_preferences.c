#include "m4_preferences.h"
#include "m4_audio.h"
#include "m4_agent.h"
#include "m4_recorder.h"
#include "m4_tube.h"
#include "sd_media.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdatomic.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t storage_lock;
static m4_preferences_t current = {1, 100, 30, 1, 0, 40, 40, 0};
static unsigned revision;
static bool dirty, web_active;
static atomic_int restart_phase;
static int64_t next_save;

bool m4_preferences_valid(const m4_preferences_t *p) {
    return p && p->version == 1 && p->brightness >= 5 && p->brightness <= 100 &&
        p->max_fps >= 5 && p->max_fps <= 60 && p->animations <= 1 && p->web_focus <= 1 &&
        p->volume <= 150 && p->last_volume >= 1 && p->last_volume <= 150 && p->reserved == 0;
}
unsigned m4_preferences_frame_ms(unsigned fps) {
    if (fps < 5) fps = 5;
    if (fps > 60) fps = 60;
    return (1000U + fps - 1U) / fps;
}
void m4_preferences_get(m4_preferences_t *out) {
    portENTER_CRITICAL(&lock); *out = current; portEXIT_CRITICAL(&lock);
}
bool m4_preferences_update(const m4_preferences_t *p) {
    if (!m4_preferences_valid(p)) return false;
    portENTER_CRITICAL(&lock);
    if (memcmp(&current, p, sizeof(current))) { current = *p; dirty = true; ++revision; }
    portEXIT_CRITICAL(&lock);
    return true;
}
void m4_preferences_volume(unsigned percent) {
    unsigned value = percent > 150 ? 150 : percent;
    portENTER_CRITICAL(&lock);
    if (current.volume != value || (value && current.last_volume != value)) {
        current.volume = value; if (value) current.last_volume = value;
        dirty = true; ++revision;
    }
    portEXIT_CRITICAL(&lock);
}
static esp_err_t write_preferences(const m4_preferences_t *p) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("m4_device", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "preferences", p, sizeof(*p));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs); return err;
}
esp_err_t m4_preferences_init(void) {
    storage_lock = xSemaphoreCreateMutex();
    if (!storage_lock) return ESP_ERR_NO_MEM;
    nvs_handle_t nvs; size_t length = sizeof(current); m4_preferences_t saved;
    bool found = false;
    if (nvs_open("m4_device", NVS_READONLY, &nvs) == ESP_OK) {
        found = nvs_get_blob(nvs, "preferences", &saved, &length) == ESP_OK &&
            length == sizeof(saved) && m4_preferences_valid(&saved);
        nvs_close(nvs);
    }
    if (found) current = saved;
    else if (nvs_open("m4_audio", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t volume = 40;
        if (nvs_get_u8(nvs, "volume", &volume) == ESP_OK && volume <= 150) {
            current.volume = volume; if (volume) current.last_volume = volume;
        }
        nvs_close(nvs);
    }
    return ESP_OK;
}
esp_err_t m4_preferences_save(const m4_preferences_t *p) {
    if (!m4_preferences_valid(p)) return ESP_ERR_INVALID_ARG;
    if (!storage_lock || xSemaphoreTake(storage_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = write_preferences(p);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&lock); current = *p; ++revision; dirty = false; portEXIT_CRITICAL(&lock);
    }
    xSemaphoreGive(storage_lock); return err;
}
void m4_preferences_poll(void) {
    if (esp_timer_get_time() < next_save || !storage_lock) return;
    next_save = esp_timer_get_time() + 2000000;
    if (xSemaphoreTake(storage_lock, 0) != pdTRUE) return;
    m4_preferences_t p; unsigned captured; bool pending;
    portENTER_CRITICAL(&lock); p = current; captured = revision; pending = dirty; portEXIT_CRITICAL(&lock);
    if (pending) {
        esp_err_t err = write_preferences(&p);
        if (err == ESP_OK) {
            portENTER_CRITICAL(&lock); if (captured == revision) dirty = false; portEXIT_CRITICAL(&lock);
        } else ESP_LOGW("M4_SETTINGS", "Settings save failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(storage_lock);
}
void m4_preferences_boot_mode(bool force_setup) {
    web_active = current.web_focus && !force_setup;
}
bool m4_preferences_web_active(void) { return web_active; }
bool m4_preferences_restarting(void) { return atomic_load(&restart_phase) == 2; }
static void restart_device(void *arg) {
    (void)arg; vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart();
}
m4_mode_result_t m4_preferences_request_mode(bool focus) {
    m4_preferences_t p; m4_preferences_get(&p); p.web_focus = focus;
    return m4_preferences_restart_with(&p);
}
m4_mode_result_t m4_preferences_restart_with(const m4_preferences_t *p) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&restart_phase, &expected, 1)) return expected == 2 ? M4_MODE_OK : M4_MODE_BUSY;
    m4_audio_state_t audio; m4_audio_state(&audio);
    m4_recorder_state_t recorder; m4_recorder_state(&recorder);
    m4_agent_state_t agent; m4_agent_state(&agent);
    m4_tube_download_t download; m4_tube_download_state(&download);
    if (audio.playing || recorder.busy || agent.busy || download.busy) { atomic_store(&restart_phase, 0); return M4_MODE_BUSY; }
    bool locked = sd_media_ready();
    if (locked && !sd_media_admin_lock()) { atomic_store(&restart_phase, 0); return M4_MODE_BUSY; }
    esp_err_t err = m4_preferences_save(p);
    if (err == ESP_OK) atomic_store(&restart_phase, 2);
    if (locked) sd_media_admin_unlock();
    if (err != ESP_OK) { atomic_store(&restart_phase, 0); return M4_MODE_SAVE_FAILED; }
    if (xTaskCreate(restart_device, "mode_restart", 2048, NULL, 5, NULL) != pdPASS) {
        atomic_store(&restart_phase, 0); return M4_MODE_RESTART_FAILED;
    }
    atomic_store(&restart_phase, 2); return M4_MODE_OK;
}
