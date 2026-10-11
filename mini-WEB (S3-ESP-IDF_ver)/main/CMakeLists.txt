idf_component_register(
    SRCS "web_page_static.c" "web_page_admin.c" "web_page_ai.c" "web_page_factory.c" "qr_printer.c" "qrcode.c" "main.c" "network_manager.c" "dns_server.c" "web_server.c" "cloud_relay.c" "https_memory.c" "sd_media_none.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_http_server nvs_flash lwip esp_event esp_timer spiffs app_update esp_partition driver json
    PRIV_REQUIRES esp_app_format spi_flash esp_hw_support mdns esp_http_client mbedtls
)

set_source_files_properties(qrcode.c PROPERTIES COMPILE_FLAGS "-Wno-error=unknown-pragmas -Wno-error=type-limits")
