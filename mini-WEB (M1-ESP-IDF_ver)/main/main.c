#include "board_display.h"
#include "cloud_relay.h"
#include "memory_monitor.h"
#include "sd_media.h"
#include "network_manager.h"
#include "web_server.h"
#include "nvs_flash.h"
#include "esp_log.h"
void app_main(void) {
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());err=nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(web_server_log_init());
    err=web_server_storage_init();
    if(err!=ESP_OK) ESP_LOGW("M1","Storage unavailable: %s",esp_err_to_name(err));
    ESP_ERROR_CHECK(network_init());
    err=sd_media_init();
    if(err!=ESP_OK) ESP_LOGW("M1","SD card unavailable: %s",esp_err_to_name(err));
    err=cloud_relay_start();
    if(err!=ESP_OK) ESP_LOGW("M1","Cloud task unavailable: %s",esp_err_to_name(err));
    ESP_ERROR_CHECK(board_display_init());
    onboard_ui_start();
    err=memory_monitor_start(60);
    if(err!=ESP_OK) ESP_LOGW("M1","Memory monitor unavailable: %s",esp_err_to_name(err));
}
