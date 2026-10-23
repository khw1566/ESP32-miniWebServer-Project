idf_component_register(
    SRCS "m3_web_assets.c" "web_pages_gzip.c" "m3_mp4.c" "m3_mp4_demux.c" "m3_mp4_pixels.c" "m3_calculator.c" "m3_gallery.c" "m3_photos.c" "m3_photo_pixels.c" "m3_calendar.c" "m3_kana.c" "m3_music.c" "m3_pcm.c" "m3_google_audio.c" "m3_azure_audio.c" "web_page_static.c" "web_page_admin.c" "web_page_ai.c" "web_page_factory.c" "qr_printer.c" "qrcode.c" "main.c" "network_manager.c" "m3_c6_update.c" "m3_netif_guard.c" "dns_server.c" "web_server.c" "cloud_relay.c" "https_memory.c" "sd_media.c" "m3_sd_settings.c" "storage_view.c" "m3_board.c" "m3_power.c" "m3_reader.c" "m3_font_reader.c" "m3_boot_splash.c" "m3_ui.c" "m3_runtime.c" "m3_audio.c" "m3_wav.c"
         "m3_radio.c" "m3_tts.c" "m3_tts_data.c" "m3_crossing.c" "m3_2048.c" "m3_hangul.c" "m3_font_korean.c" "m3_font_cjk.c" "m3_font_bitmap.c" "m3_voice.c" "m3_recorder.c" "m3_preferences.c" "m3_feedback.c" "m3_sd_apps.c" "m3_agent.c" "m3_i18n.c" "m3_font_large.c" "m3_font_title.c" "m3_font_clock.c" "m3_font_splash.c" "m3_font_splash_small.c" "m3_tube.c" "m3_flash_usage.c" "m3_flash_usage_view.c" "m3_inflate.c" "m3_theme.c" "m3_wallpaper.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_wifi_remote esp_hosted esp_netif esp_http_server nvs_flash lwip esp_event esp_timer app_update esp_partition driver json fatfs sdmmc esp_driver_sdmmc esp_psram esp_lcd esp_lvgl_port esp_lcd_touch_gt911 esp_codec_dev lvgl
    PRIV_REQUIRES m3_lua esp_adc esp_driver_ledc esp_driver_jpeg esp_app_format spi_flash esp_hw_support mdns esp_http_client mbedtls esp_audio_codec esp_h264 espeak_ng vfs
)
target_sources(${COMPONENT_LIB} PRIVATE "m3_wifi_performance.c")

# --wrap alone does not make the wrapper an archive extraction root. Keep it
# even when libmain is scanned before the SDK archive that references it.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_netif_action_start"
    "-Wl,--undefined=__wrap_esp_netif_action_start"
)

set_source_files_properties(qrcode.c PROPERTIES COMPILE_FLAGS "-Wno-error=unknown-pragmas -Wno-error=type-limits")
