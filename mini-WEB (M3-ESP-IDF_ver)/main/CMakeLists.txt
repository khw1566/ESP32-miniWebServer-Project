idf_component_register(
    SRCS "m3_photos.c" "m3_photo_pixels.c" "m3_calendar.c" "m3_kana.c" "m3_music.c" "m3_pcm.c" "m3_google_audio.c" "web_page_static.c" "web_page_admin.c" "web_page_ai.c" "web_page_factory.c" "qr_printer.c" "qrcode.c" "main.c" "network_manager.c" "m3_netif_guard.c" "dns_server.c" "web_server.c" "cloud_relay.c" "https_memory.c" "sd_media.c" "storage_view.c" "m3_board.c" "m3_boot_splash.c" "m3_ui.c" "m3_runtime.c" "m3_audio.c" "m3_wav.c"
         "m3_radio.c" "m3_tts.c" "m3_tts_data.c" "m3_2048.c" "m3_hangul.c" "m3_font_korean.c" "m3_font_cjk.c" "m3_font_bitmap.c" "m3_voice.c" "m3_agent.c" "m3_i18n.c" "m3_font_large.c" "m3_font_title.c" "m3_font_clock.c" "m3_font_splash.c" "m3_font_splash_small.c" "m3_tube.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_wifi_remote esp_hosted esp_netif esp_http_server nvs_flash lwip esp_event esp_timer spiffs app_update esp_partition driver json fatfs sdmmc esp_driver_sdmmc esp_psram esp_lcd esp_lvgl_port esp_lcd_touch_gt911 esp_codec_dev lvgl
    PRIV_REQUIRES esp_driver_jpeg esp_app_format spi_flash esp_hw_support mdns esp_http_client mbedtls esp_audio_codec espeak_ng vfs
)

# --wrap alone does not make the wrapper an archive extraction root. Keep it
# even when libmain is scanned before the SDK archive that references it.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_netif_action_start"
    "-Wl,--undefined=__wrap_esp_netif_action_start"
)

set_source_files_properties(qrcode.c PROPERTIES COMPILE_FLAGS "-Wno-error=unknown-pragmas -Wno-error=type-limits")
