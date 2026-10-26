#include "m4_board.h"
#include "m4_boot_splash.h"
#include "m4_ui.h"
#include "m4_audio.h"
#include "m4_runtime.h"
#include "m4_agent.h"
#include "m4_tube.h"
#include "m4_gallery.h"
#include "m4_reader.h"
#include "m4_preferences.h"
#include "m4_feedback.h"
#include "m4_hosted_boot.h"
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
    /* IDF main_task has now released its startup stacks and reserved DMA RAM. */
    err = m4_hosted_start();
    if (err != ESP_OK) ESP_LOGE("M4", "C6 transport initialization failed: %s", esp_err_to_name(err));
    web_server_log_init();
    ESP_LOGI("M4", "GUITION Mini WebServer M4 / JC1060P470C, PSRAM=%u bytes", (unsigned)esp_psram_get_size());
    gpio_config_t boot = {.pin_bit_mask = 1ULL << 35, .mode = GPIO_MODE_INPUT, .pull_up_en = true};
    ESP_ERROR_CHECK(gpio_config(&boot));
    bool force_setup = gpio_get_level(35) == 0;
    esp_err_t settings_error = m4_preferences_init();
    if (settings_error != ESP_OK) ESP_LOGW("M4", "Device settings unavailable: %s", esp_err_to_name(settings_error));
    if (m4_feedback_init() != ESP_OK) ESP_LOGW("M4", "System sound queue unavailable");
    m4_preferences_boot_mode(force_setup);
    if (m4_preferences_web_active()) {
        gpio_set_direction(M4_LCD_BACKLIGHT, GPIO_MODE_OUTPUT); gpio_set_level(M4_LCD_BACKLIGHT, 0);
        gpio_set_direction(M4_AMP_ENABLE, GPIO_MODE_OUTPUT); gpio_set_level(M4_AMP_ENABLE, 0);
        ESP_LOGI("M4", "Web focus: display, audio and application workers are disabled");
        ESP_ERROR_CHECK(m4_runtime_start(false));
        return;
    }
    err = m4_board_init();
    if (err == ESP_OK) {
        if (m4_boot_splash_start() != ESP_OK) ESP_LOGW("M4", "Boot splash skipped");
        m4_boot_splash_step(M4_BOOT_WORKERS);
        if (m4_agent_start() != ESP_OK) ESP_LOGW("M4", "Agent task unavailable");
        if (m4_audio_start() != ESP_OK) ESP_LOGW("M4", "Audio task unavailable");
        if (m4_tube_start() != ESP_OK) ESP_LOGW("M4", "YouTube worker unavailable");
        if (m4_reader_start() != ESP_OK) ESP_LOGW("M4", "Reader worker unavailable");
        if (m4_gallery_start() != ESP_OK) ESP_LOGW("M4", "Gallery worker unavailable");
        m4_boot_splash_step(M4_BOOT_UI);
        if (m4_ui_start() != ESP_OK) ESP_LOGW("M4", "UI unavailable");
    } else ESP_LOGE("M4", "Display failed: %s; continuing web setup", esp_err_to_name(err));
    /* File mount, C6 boot and Wi-Fi retries must not hold the LVGL mutex. */
    ESP_LOGI("M4", "Before Wi-Fi: internal=%u, DMA=%u, largest DMA=%u, PSRAM=%u bytes",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(m4_runtime_start(force_setup));
}
