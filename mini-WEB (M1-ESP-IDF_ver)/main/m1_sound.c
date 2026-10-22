#include "m1_sound.h"
#include "sdkconfig.h"

#ifdef CONFIG_M1_SYSTEM_SOUNDS
#include <stdint.h>
#include <stdatomic.h>
#include "nvs.h"
#include "driver/dac_cosine.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct { uint16_t hz, ms; } note_t;
typedef struct { const note_t *notes; size_t count; uint32_t cooldown_ms; } melody_t;
static const note_t boot[] = {{523,100},{659,100},{784,100},{1047,180}};
static const note_t wifi[] = {{659,100},{988,160}};
static const note_t lost[] = {{784,100},{523,160}};
static const note_t failed[] = {{392,150},{0,60},{392,150},{262,200}};
static const note_t ap[] = {{523,90},{0,40},{523,90},{784,140}};
static const note_t server_on[] = {{523,90},{784,140}};
static const note_t server_off[] = {{784,90},{392,160}};
static const note_t domain[] = {{784,80},{988,80},{1175,80},{1568,180}};
static const note_t error[] = {{330,120},{0,50},{330,180}};
static const note_t volume_preview[] = {{784,100}};
#define MELODY(name, cooldown) {name, sizeof(name)/sizeof(name[0]), cooldown}
static const melody_t melodies[M1_SOUND_COUNT] = {
    [M1_SOUND_BOOT] = MELODY(boot, 5000),
    [M1_SOUND_WIFI_CONNECTED] = MELODY(wifi, 5000),
    [M1_SOUND_WIFI_LOST] = MELODY(lost, 15000),
    [M1_SOUND_WIFI_FAILED] = MELODY(failed, 30000),
    [M1_SOUND_AP_READY] = MELODY(ap, 5000),
    [M1_SOUND_SERVER_START] = MELODY(server_on, 1000),
    [M1_SOUND_SERVER_STOP] = MELODY(server_off, 1000),
    [M1_SOUND_DOMAIN_CONNECTED] = MELODY(domain, 60000),
    [M1_SOUND_ERROR] = MELODY(error, 5000),
    [M1_SOUND_VOLUME] = MELODY(volume_preview, 100),
};
static QueueHandle_t sound_queue;
static portMUX_TYPE sound_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t next_allowed[M1_SOUND_COUNT];
static atomic_uint volume = 4 - CONFIG_M1_SOUND_ATTENUATION;

unsigned m1_sound_get_volume(void) { return atomic_load(&volume); }

esp_err_t m1_sound_set_volume(unsigned level) {
    if (level > 4) return ESP_ERR_INVALID_ARG;
    if (level == m1_sound_get_volume()) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m1_sound", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "volume", (uint8_t)level);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        atomic_store(&volume, level);
        if (level) m1_sound_play(M1_SOUND_VOLUME);
    }
    return err;
}

static esp_err_t play_note(note_t note) {
    unsigned level = m1_sound_get_volume();
    if (!level) return ESP_OK;
    if (!note.hz) { vTaskDelay(pdMS_TO_TICKS(note.ms)); return ESP_OK; }
    /* ESP32 DAC_CHAN_1 is GPIO26. GPIO25 belongs to the touch clock. */
    const dac_cosine_config_t cfg = {
        .chan_id = DAC_CHAN_1, .freq_hz = note.hz,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten = (dac_cosine_atten_t)(4 - level),
        .phase = DAC_COSINE_PHASE_0, .offset = 0,
    };
    dac_cosine_handle_t channel = NULL;
    esp_err_t err = dac_cosine_new_channel(&cfg, &channel);
    if (err != ESP_OK) return err;
    err = dac_cosine_start(channel);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(note.ms));
        err = dac_cosine_stop(channel);
        /* A stop failure leaves ownership uncertain: disable further playback. */
        if (err != ESP_OK) return err;
    }
    esp_err_t cleanup = dac_cosine_del_channel(channel);
    return err != ESP_OK ? err : cleanup;
}

static void sound_worker(void *arg) {
    (void)arg;
    m1_sound_event_t event;
    bool available = true;
    while (xQueueReceive(sound_queue, &event, portMAX_DELAY) == pdTRUE) {
        if (!available || !m1_sound_get_volume()) continue;
        const melody_t *melody = &melodies[event];
        for (size_t i = 0; i < melody->count; ++i) {
            if (!m1_sound_get_volume()) break;
            esp_err_t err = play_note(melody->notes[i]);
            if (err != ESP_OK) {
                ESP_LOGW("M1_SOUND", "Sound disabled after DAC error: %s", esp_err_to_name(err));
                available = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

esp_err_t m1_sound_init(void) {
    if (sound_queue) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t load_err = nvs_open("m1_sound", NVS_READONLY, &handle);
    if (load_err == ESP_OK) {
        uint8_t saved = 0;
        load_err = nvs_get_u8(handle, "volume", &saved);
        nvs_close(handle);
        if (load_err == ESP_OK && saved <= 4) atomic_store(&volume, saved);
        else if (load_err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW("M1_SOUND", "Invalid saved volume; using configured default");
    } else if (load_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("M1_SOUND", "Volume load failed: %s", esp_err_to_name(load_err));
    }
    sound_queue = xQueueCreate(6, sizeof(m1_sound_event_t));
    if (!sound_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_worker, "m1_sound", 3072, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(sound_queue);
        sound_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    m1_sound_play(M1_SOUND_BOOT);
    return ESP_OK;
}

void m1_sound_play(m1_sound_event_t event) {
    if (!sound_queue || !m1_sound_get_volume() || (unsigned)event >= M1_SOUND_COUNT) return;
    int64_t now = esp_timer_get_time();
    bool allowed = false;
    portENTER_CRITICAL(&sound_lock);
    if (now >= next_allowed[event]) {
        next_allowed[event] = now + (int64_t)melodies[event].cooldown_ms * 1000;
        allowed = true;
    }
    portEXIT_CRITICAL(&sound_lock);
    /* A full queue drops the sound and retains its retry cooldown. */
    if (allowed) (void)xQueueSend(sound_queue, &event, 0);
}
#else
esp_err_t m1_sound_init(void) { return ESP_OK; }
void m1_sound_play(m1_sound_event_t event) { (void)event; }
unsigned m1_sound_get_volume(void) { return 0; }
esp_err_t m1_sound_set_volume(unsigned level) { (void)level; return ESP_ERR_NOT_SUPPORTED; }
#endif
