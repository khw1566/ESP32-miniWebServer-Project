idf_component_register(
    SRCS "m4_web_assets.c" "web_pages_gzip.c" "m4_mp4.c" "m4_mp4_demux.c" "m4_mp4_pixels.c" "m4_calculator.c" "m4_gallery.c" "m4_photos.c" "m4_photo_pixels.c" "m4_calendar.c" "m4_kana.c" "m4_music.c" "m4_pcm.c" "m4_google_audio.c" "m4_azure_audio.c" "web_page_static.c" "web_page_admin.c" "web_page_ai.c" "web_page_factory.c" "qr_printer.c" "qrcode.c" "main.c" "network_manager.c" "m4_c6_update.c" "m4_netif_guard.c" "dns_server.c" "web_server.c" "cloud_relay.c" "https_memory.c" "sd_media.c" "m4_sd_settings.c" "storage_view.c" "m4_board.c" "m4_panel_init.c" "m4_layout.c" "m4_power.c" "m4_reader.c" "m4_font_reader.c" "m4_boot_splash.c" "m4_ui.c" "m4_runtime.c" "m4_audio.c" "m4_wav.c"
         "m4_radio.c" "m4_tts.c" "m4_tts_data.c" "m4_crossing.c" "m4_2048.c" "m4_hangul.c" "m4_font_korean.c" "m4_font_cjk.c" "m4_font_bitmap.c" "m4_voice.c" "m4_recorder.c" "m4_preferences.c" "m4_feedback.c" "m4_sd_apps.c" "m4_agent.c" "m4_i18n.c" "m4_font_large.c" "m4_font_title.c" "m4_font_clock.c" "m4_font_splash.c" "m4_font_splash_small.c" "m4_tube.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_wifi_remote esp_hosted esp_netif esp_http_server nvs_flash lwip esp_event esp_timer app_update esp_partition driver json fatfs sdmmc esp_driver_sdmmc esp_psram esp_lcd esp_lcd_jd9165 esp_lvgl_port esp_lcd_touch_gt911 esp_codec_dev lvgl
    PRIV_REQUIRES m4_lua esp_adc esp_driver_ledc esp_driver_jpeg esp_app_format spi_flash esp_hw_support mdns esp_http_client mbedtls esp_audio_codec esp_h264 espeak_ng vfs
)
target_sources(${COMPONENT_LIB} PRIVATE "m4_wifi_performance.c" "m4_hosted_boot.c")

# --wrap alone does not make the wrapper an archive extraction root. Keep it
# even when libmain is scanned before the SDK archive that references it.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_netif_action_start"
    "-Wl,--undefined=__wrap_esp_netif_action_start"
    "-Wl,--wrap=esp_hosted_init"
    "-Wl,--undefined=__wrap_esp_hosted_init"
)

set_source_files_properties(qrcode.c PROPERTIES COMPILE_FLAGS "-Wno-error=unknown-pragmas -Wno-error=type-limits")
