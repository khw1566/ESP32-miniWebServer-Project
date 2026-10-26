/* Included after the shared form helpers. All LVGL calls stay on the UI task. */
static lv_obj_t *weather_place, *weather_temperature, *weather_details, *weather_status, *weather_saved;
static lv_obj_t *weather_query, *weather_search_button, *weather_refresh_button;
static lv_obj_t *weather_results[M3_WEATHER_RESULTS], *weather_result_labels[M3_WEATHER_RESULTS];
static lv_obj_t *weather_presets[8], *weather_days[M3_WEATHER_DAYS];
static M3_PSRAM_BSS m3_weather_state_t weather_snapshot;
static bool weather_action_failed;
static unsigned weather_seen_revision;
static m3_text_t weather_condition(int code) {
    switch (code) {
        case 0: return T_WEATHER_CLEAR;
        case 1: case 2: return T_WEATHER_PARTLY;
        case 3: return T_WEATHER_CLOUDY;
        case 45: case 48: return T_WEATHER_FOG;
        case 51: case 53: case 55: return T_WEATHER_DRIZZLE;
        case 56: case 57: case 66: case 67: return T_WEATHER_FREEZING;
        case 61: case 63: case 65: return T_WEATHER_RAIN;
        case 71: case 73: case 75: case 77: return T_WEATHER_SNOW;
        case 80: case 81: case 82: return T_WEATHER_SHOWERS;
        case 85: case 86: return T_WEATHER_SNOW;
        case 95: case 96: case 99: return T_WEATHER_STORM;
        default: return T_WEATHER_UNKNOWN;
    }
}
static const char *weather_preset_name(unsigned index) {
    static const m3_text_t names[] = {T_WEATHER_SEOUL, T_WEATHER_BUSAN, T_WEATHER_ULSAN, T_WEATHER_INCHEON,
        T_WEATHER_DAEGU, T_WEATHER_DAEJEON, T_WEATHER_GWANGJU, T_WEATHER_JEJU};
    return index < sizeof(names) / sizeof(names[0]) ? tr(names[index]) : "";
}
static const char *weather_place_name(const m3_weather_place_t *place) {
    for (unsigned i = 0; i < m3_weather_preset_count(); ++i) {
        const m3_weather_place_t *p = m3_weather_preset(i);
        if (fabs(p->latitude - place->latitude) < 0.000001 && fabs(p->longitude - place->longitude) < 0.000001)
            return weather_preset_name(i);
    }
    return place->name;
}
static void weather_search(lv_event_t *event) {
    (void)event; hide_keyboard();
    const char *lang = m3_i18n_language() == M3_LANG_KO ? "ko" : m3_i18n_language() == M3_LANG_JA ? "ja" : "en";
    weather_action_failed = !m3_weather_search(lv_textarea_get_text(weather_query), lang);
    update_weather();
}
static void weather_refresh(lv_event_t *event) {
    (void)event; weather_action_failed = !m3_weather_refresh(); update_weather();
}
static void weather_choose(lv_event_t *event) {
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    m3_weather_state(&weather_snapshot);
    const m3_weather_place_t *place = index < 8 ? m3_weather_preset(index) :
        index - 8 < weather_snapshot.count ? &weather_snapshot.results[index - 8] : NULL;
    weather_action_failed = !place || !m3_weather_select(place);
    hide_keyboard(); update_weather();
}
static void build_weather(lv_obj_t *page) {
    lv_obj_t *summary = card(page, T_NONE);
    lv_obj_t *line = row(summary);
    weather_place = grow(label(line, "", CYAN));
    weather_refresh_button = refresh_button(line, T_WEATHER_REFRESH, weather_refresh, NULL);
    weather_temperature = label(summary, "-- C", TEXT);
    lv_obj_set_style_text_font(weather_temperature, FONT_TITLE, 0);
    weather_details = label(summary, "", MUTED);
    weather_status = label(summary, "", MUTED);
    weather_saved = label(summary, "", MUTED);
    lv_obj_t *search = card(page, T_WEATHER_LOCATION);
    caption(search, T_WEATHER_SEARCH_HINT);
    weather_query = input_field(search, T_NONE, 48, INPUT_KOREAN, false);
    bind_text(weather_query, T_WEATHER_PLACEHOLDER, NULL, BIND_PLACEHOLDER);
    weather_search_button = button(search, NULL, T_WEATHER_SEARCH, BTN_PRIMARY, weather_search, NULL);
    for (unsigned i = 0; i < M3_WEATHER_RESULTS; ++i) {
        weather_results[i] = button(search, NULL, T_NONE, BTN_SECONDARY, weather_choose, (void *)(uintptr_t)(8 + i));
        lv_obj_set_height(weather_results[i], LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(weather_results[i], 78, 0);
        lv_obj_set_style_pad_ver(weather_results[i], 12, 0);
        weather_result_labels[i] = lv_obj_get_child(weather_results[i], 0);
        lv_obj_set_height(weather_result_labels[i], LV_SIZE_CONTENT);
        lv_label_set_long_mode(weather_result_labels[i], LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(weather_results[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *presets = card(page, T_WEATHER_PRESETS);
    for (unsigned i = 0; i < 8; ++i) {
        if (i % 4 == 0) line = row(presets);
        weather_presets[i] = grow(button(line, NULL, T_NONE, BTN_SECONDARY, weather_choose, (void *)(uintptr_t)i));
    }
    lv_obj_t *forecast = card(page, T_WEATHER_FORECAST);
    for (unsigned i = 0; i < M3_WEATHER_DAYS; ++i) weather_days[i] = label(forecast, "--", TEXT);
    caption(page, T_WEATHER_SOURCE);
}
static void update_weather(void) {
    if (!home_weather || !weather_place) return;
    m3_weather_state(&weather_snapshot);
    m3_weather_state_t *s = &weather_snapshot;
    if (weather_seen_revision != s->revision) { weather_action_failed = false; weather_seen_revision = s->revision; }
    char value[512];
    const char *name = weather_place_name(&s->place);
    if (s->valid) snprintf(value, sizeof(value), "%s  %.1f C  %s%s", name, s->data.temperature,
        tr(weather_condition(s->data.code)), s->stale ? tr(T_WEATHER_STALE_SHORT) : "");
    else snprintf(value, sizeof(value), tr(T_WEATHER_HOME), name);
    set_text(home_weather, value);
    /* Bound strings and significant digits so every coordinate stays within value[]. */
    snprintf(value, sizeof(value), "%.95s\n%.127s  (%.8g, %.8g)", name, s->place.area, s->place.latitude, s->place.longitude);
    set_text(weather_place, value);
    if (s->valid) {
        snprintf(value, sizeof(value), "%.1f C  %s", s->data.temperature, tr(weather_condition(s->data.code)));
        set_text(weather_temperature, value);
        snprintf(value, sizeof(value), tr(T_WEATHER_DETAILS), s->data.feels_like, s->data.humidity, s->data.wind, s->data.time);
        set_text(weather_details, value);
    } else { set_text(weather_temperature, "-- C"); set_text(weather_details, tr(T_WEATHER_NO_DATA)); }
    m3_text_t status = s->busy ? T_WEATHER_LOADING : T_WEATHER_READY;
    if (!s->busy) {
        switch (s->status) {
            case M3_WEATHER_IDLE: status = T_WEATHER_NO_DATA; break;
            case M3_WEATHER_OFFLINE: status = T_WEATHER_OFFLINE; break;
            case M3_WEATHER_NETWORK: case M3_WEATHER_INVALID: status = T_WEATHER_ERROR; break;
            case M3_WEATHER_EMPTY: status = T_WEATHER_EMPTY; break;
            case M3_WEATHER_MEMORY: status = T_WEATHER_MEMORY; break;
            case M3_WEATHER_WAIT: status = T_WEATHER_WAIT; break;
            default: break;
        }
    }
    if (weather_action_failed) status = T_WEATHER_WAIT;
    snprintf(value, sizeof(value), "%s%s", tr(status), s->stale ? tr(T_WEATHER_STALE) : "");
    set_text(weather_status, value);
    set_text(weather_saved, tr(s->saved ? T_WEATHER_SAVED : T_WEATHER_UNSAVED));
    for (unsigned i = 0; i < M3_WEATHER_RESULTS; ++i) {
        if (i < s->count) {
            lv_obj_remove_flag(weather_results[i], LV_OBJ_FLAG_HIDDEN);
            snprintf(value, sizeof(value), "%.95s\n%.127s (%.8g, %.8g)", s->results[i].name, s->results[i].area,
                s->results[i].latitude, s->results[i].longitude);
            set_text(weather_result_labels[i], value);
        } else lv_obj_add_flag(weather_results[i], LV_OBJ_FLAG_HIDDEN);
        if (s->busy) lv_obj_add_state(weather_results[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(weather_results[i], LV_STATE_DISABLED);
    }
    for (unsigned i = 0; i < 8; ++i) {
        set_text(lv_obj_get_child(weather_presets[i], 0), weather_preset_name(i));
        if (s->busy) lv_obj_add_state(weather_presets[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(weather_presets[i], LV_STATE_DISABLED);
    }
    for (unsigned i = 0; i < M3_WEATHER_DAYS; ++i) {
        const m3_weather_day_t *d = &s->data.days[i];
        if (s->valid && d->valid) snprintf(value, sizeof(value), tr(T_WEATHER_DAY), d->date + 5,
            tr(weather_condition(d->code)), d->low, d->high, d->rain);
        else snprintf(value, sizeof(value), "%s", tr(T_WEATHER_DAY_MISSING));
        set_text(weather_days[i], value);
    }
    if (s->busy) { lv_obj_add_state(weather_search_button, LV_STATE_DISABLED); lv_obj_add_state(weather_refresh_button, LV_STATE_DISABLED); }
    else { lv_obj_remove_state(weather_search_button, LV_STATE_DISABLED); lv_obj_remove_state(weather_refresh_button, LV_STATE_DISABLED); }
}
