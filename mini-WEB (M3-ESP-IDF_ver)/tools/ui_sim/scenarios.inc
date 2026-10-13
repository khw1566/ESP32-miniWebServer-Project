/* Screens captured by sim_main.c. Each step uses the UI's own handlers. */
static void focus(lv_obj_t *field) { lv_obj_send_event(field, LV_EVENT_FOCUSED, NULL); settle(400); }
static void scroll_page(app_id_t app, int32_t y) { lv_obj_scroll_to_y(pages[app], y, LV_ANIM_OFF); settle(100); }
static void run_scenarios(const char *dir) {
    const char *lang = m3_i18n_language() == M3_LANG_EN ? "en" : m3_i18n_language() == M3_LANG_JA ? "ja" : "ko";
    char name[48];
#define SHOT(label) do { snprintf(name, sizeof(name), "%s_%s", lang, label); capture(dir, name); } while (0)
    SHOT("00_home");
    lv_tileview_set_tile_by_index(home_tiles, 0, 0, LV_ANIM_OFF); home_tile_changed(NULL);
    update_metrics(NULL); settle(200); SHOT("47_home_calendar");
    m3_calendar_shift(&calendar_year, &calendar_month, 1); calendar_follow = false; calendar_stamp = 0;
    update_calendar(); settle(200); SHOT("48_calendar_next_month");
    calendar_follow = true;
    lv_tileview_set_tile_by_index(home_tiles, 2, 0, LV_ANIM_OFF); home_tile_changed(NULL); settle(200); SHOT("49_home_more_apps");
    lv_tileview_set_tile_by_index(home_tiles, HOME_PHOTOS, 0, LV_ANIM_OFF); home_tile_changed(NULL);
    update_metrics(NULL); settle(200); SHOT("50_home_photos");
    m3_photos_pause(); update_metrics(NULL); settle(100); SHOT("51_photos_paused");
    sim_photos_empty = true; update_metrics(NULL); settle(100); SHOT("52_photos_empty");
    sim_photos_empty = false; sim_photos_busy = true; update_metrics(NULL); settle(100); SHOT("53_photos_sd_busy");
    sim_photos_busy = false; show_app(APP_HOME); settle(200);
    static const char *names[] = {"home", "wifi", "files", "audio", "monitor", "settings", "radio", "games", "tts", "agent", "tube", "music"};
    _Static_assert(sizeof(names) / sizeof(names[0]) == APP_COUNT, "Simulator app names must match the firmware");
    for (unsigned app = 1; app < APP_COUNT; ++app) {
        char label[32]; show_app((app_id_t)app); settle(400);
        snprintf(label, sizeof(label), "%02u_%s", app, names[app]); SHOT(label);
    }
    show_app(APP_MUSIC); settle(300); SHOT("40_music_sd");
    show_app(APP_FILES); settle(300); SHOT("46_files_unicode");
    show_app(APP_MUSIC); settle(300);
    m3_music_play(0, 1); update_metrics(NULL); settle(300); SHOT("41_music_playing");
    m3_music_pause(); update_metrics(NULL); settle(200); SHOT("42_music_paused");
    m3_audio_volume(150); update_metrics(NULL); settle(200); SHOT("43_music_boost150");
    m3_music_stop(); m3_music_refresh(false); segment_select(music_source, 1);
    update_metrics(NULL); settle(200); SHOT("44_music_flash");
    m3_music_page(1); update_metrics(NULL); settle(200); SHOT("45_music_page2");
    m3_audio_volume(40);
    show_app(APP_WIFI); settle(300); scroll_page(APP_WIFI, 560); SHOT("11_wifi_form");
    lv_obj_add_state(static_switch, LV_STATE_CHECKED); lv_obj_send_event(static_switch, LV_EVENT_VALUE_CHANGED, NULL);
    settle(100); scroll_page(APP_WIFI, 820); SHOT("12_wifi_static");
    focus(ssid_field); SHOT("13_keyboard_en");
    show_app(APP_TTS); settle(300); focus(tts_text); SHOT("14_keyboard_ko");
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_SPECIAL); settle(100); SHOT("15_keyboard_symbols");
    hide_keyboard(); show_app(APP_RADIO); settle(300); scroll_page(APP_RADIO, 360); SHOT("16_radio_stations");
    show_app(APP_GAMES); settle(300);
    for (int i = 0; i < 12; ++i) game_move((m3_game_direction_t)(i % 4));
    settle(100); SHOT("17_games_2048");
    segment_select(game_tabs, 1); choose_game(NULL);
    memory_visible[0] = memory_visible[5] = true; memory_solved[2] = memory_solved[9] = true; render_game(); settle(100);
    SHOT("18_games_memory");
    show_app(APP_AGENT); settle(300); scroll_page(APP_AGENT, 400); SHOT("19_agent_input");
    show_app(APP_TTS); settle(300);
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_parent(tts_text), 1), LV_EVENT_CLICKED, NULL);
    seen_voice = ~0u; update_metrics(NULL); settle(400);
    SHOT("20_voice_sheet");
    voice_cancel(NULL); show_app(APP_HOME); settle(300); shade_set(true); settle(500); SHOT("21_shade");
    shade_set(false); settle(400); show_app(APP_FILES); settle(300); scroll_page(APP_FILES, 300); SHOT("22_files_bottom");
    /* YouTube: empty, results, player and channel listing. */
    sim_playing = false;
    show_app(APP_TUBE); settle(300); update_metrics(NULL); settle(100); SHOT("26_tube_empty");
    lv_textarea_set_text(tube_query, m3_i18n_language() == M3_LANG_EN ? "NewJeans" : "뉴진스");
    tube_submit(); update_metrics(NULL); settle(300); SHOT("27_tube_results");
    scroll_page(APP_TUBE, 900); SHOT("28_tube_results_more");
    lv_obj_send_event(tube_rows[6].button, LV_EVENT_CLICKED, NULL);
    update_metrics(NULL); update_metrics(NULL); tube_frame_tick(NULL); settle(300); SHOT("29_tube_player");
    tube_channel_event(NULL); update_metrics(NULL); settle(300); SHOT("30_tube_channel");
    show_app(APP_HOME); settle(300);
    /* Monitor web QR (local and internet) and waiting animations. */
    sim_playing = false; show_app(APP_MONITOR); settle(300); update_metrics(NULL); settle(100); SHOT("31_monitor_qr");
    segment_select(web_tabs, M3_WEB_IPV6); web_tab_changed(NULL); update_metrics(NULL); settle(100); SHOT("32_monitor_qr_ipv6");
    sim_ipv6_ready = false; update_metrics(NULL); settle(100); SHOT("62_monitor_ipv6_pending");
    sim_ipv6_ready = true;
    segment_select(web_tabs, M3_WEB_IPV4); web_tab_changed(NULL); update_metrics(NULL); settle(100); SHOT("63_monitor_qr_ipv4");
    segment_select(web_tabs, M3_WEB_MDNS); web_tab_changed(NULL); update_metrics(NULL); settle(100); SHOT("64_monitor_qr_mdns");
    segment_select(web_tabs, M3_WEB_DOMAIN); web_tab_changed(NULL);
    sim_online = false; update_metrics(NULL); settle(100); SHOT("65_monitor_domain_offline"); sim_online = true;
    segment_select(web_tabs, 0); web_tab_changed(NULL);
    sim_agent_busy = true; show_app(APP_AGENT); settle(300); update_metrics(NULL); settle(330); SHOT("33_agent_thinking");
    sim_agent_busy = false; update_metrics(NULL);
    sim_playing = true; show_app(APP_TTS); settle(200);
    sim_audio_message = "OpenAI TTS / downloading Korean audio..."; update_metrics(NULL); settle(330); SHOT("34_tts_preparing");
    sim_audio_message = "OpenAI TTS / AI-generated Korean speech"; update_metrics(NULL); settle(410); SHOT("35_tts_speaking");
    sim_audio_message = NULL; sim_playing = false; update_metrics(NULL);
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_parent(tts_text), 1), LV_EVENT_CLICKED, NULL);
    sim_voice_recording = true; sim_mic_level = 0; update_metrics(NULL); settle(200); SHOT("36_voice_silent");
    sim_mic_level = 78; settle(200); SHOT("36_voice_speaking");
    sim_mic_level = 35; settle(120); SHOT("36_voice_quiet"); sim_mic_level = 0;
    sim_voice_recording = false; sim_voice_busy = true; update_metrics(NULL); settle(330); SHOT("37_voice_recognizing");
    sim_voice_busy = false; sim_voice_recording = true; voice_cancel(NULL); show_app(APP_HOME); settle(200);
    /* Switch language at runtime the way the Settings control does. */
    show_app(APP_SYSTEM); settle(200);
    segment_select(language_tabs, m3_i18n_language() == M3_LANG_EN ? 0 : 1); language_changed(NULL); settle(300);
    SHOT("23_switched_settings");
    show_app(APP_WIFI); settle(300); SHOT("24_switched_wifi");
    show_app(APP_HOME); settle(300); SHOT("25_switched_home");
    show_app(APP_SYSTEM); segment_select(language_tabs, M3_LANG_JA); language_changed(NULL); settle(200);
    capture(dir, "ja_54_settings");
    show_app(APP_HOME); settle(200); capture(dir, "ja_55_home");
    lv_tileview_set_tile_by_index(home_tiles, 0, 0, LV_ANIM_OFF); home_tile_changed(NULL);
    update_metrics(NULL); settle(200); capture(dir, "ja_56_calendar");
    show_app(APP_FILES); settle(200); capture(dir, "ja_57_files");
    show_app(APP_MUSIC); settle(300); capture(dir, "ja_58_music_marquee_start");
    settle(2500); capture(dir, "ja_59_music_marquee_later");
    show_app(APP_TUBE); settle(200); focus(tube_query);
    lv_textarea_set_text(tube_query, "ひらがなとカタカナ"); capture(dir, "ja_60_keyboard_hiragana");
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_4); settle(200); capture(dir, "ja_61_keyboard_katakana");
    hide_keyboard();
#undef SHOT
}
