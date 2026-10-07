idf_component_register(
    SRCS "https_memory.c" "m1_dashboard.c" "m1_keyboard.c" "storage_view.c" "sd_media.c" "cloud_relay.c" "board_display.c" "onboard_ui.c" "font_m1.c" "web_page_static.c" "web_page_admin.c" "web_page_ai.c" "qr_printer.c" "qrcode.c" "main.c" "network_manager.c" "dns_server.c" "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_http_server nvs_flash lwip esp_event esp_timer spiffs app_update esp_partition driver json
    PRIV_REQUIRES fatfs sdmmc esp_app_format spi_flash esp_hw_support mdns esp_http_client mbedtls esp_lcd esp_lcd_ili9341 lvgl
)

set_source_files_properties(qrcode.c PROPERTIES COMPILE_FLAGS "-Wno-error=unknown-pragmas -Wno-error=type-limits")
