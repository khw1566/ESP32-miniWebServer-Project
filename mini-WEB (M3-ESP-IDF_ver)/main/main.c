#include "m3_board.h"
#include "m3_boot_splash.h"
#include "m3_ui.h"
#include "m3_audio.h"
#include "m3_runtime.h"
#include "m3_agent.h"
#include "m3_tube.h"
#include "web_server.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

void app_main(void) {
    /* UI objects and JSON do not require DMA RAM. Enforce this at runtime as
     * well as in sdkconfig so an older saved configuration cannot keep the
     * previous internal preference and starve the C6 SDIO receive allocator. */
    heap_caps_malloc_extmem_enable(0);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    web_server_log_init();
    ESP_LOGI("M3", "GUITION JC-ESP32P4-M3 / JC4880P443C_I_W, PSRAM=%u bytes", (unsigned)esp_psram_get_size());
    gpio_config_t boot = {.pin_bit_mask = 1ULL << 35, .mode = GPIO_MODE_INPUT, .pull_up_en = true};
    ESP_ERROR_CHECK(gpio_config(&boot));
    bool force_setup = gpio_get_level(35) == 0;
    err = m3_board_init();
    if (err == ESP_OK) {
        if (m3_boot_splash_start() != ESP_OK) ESP_LOGW("M3", "Boot splash skipped");
        m3_boot_splash_step(M3_BOOT_WORKERS);
        if (m3_agent_start() != ESP_OK) ESP_LOGW("M3", "Agent task unavailable");
        if (m3_audio_start() != ESP_OK) ESP_LOGW("M3", "Audio task unavailable");
        if (m3_tube_start() != ESP_OK) ESP_LOGW("M3", "YouTube worker unavailable");
        m3_boot_splash_step(M3_BOOT_UI);
        if (m3_ui_start() != ESP_OK) ESP_LOGW("M3", "UI unavailable");
    } else ESP_LOGE("M3", "Display failed: %s; continuing web setup", esp_err_to_name(err));
    /* File mount, C6 boot and Wi-Fi retries must not hold the LVGL mutex. */
    ESP_LOGI("M3", "Before Wi-Fi: internal=%u, DMA=%u, largest DMA=%u, PSRAM=%u bytes",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(m3_runtime_start(force_setup));
}
