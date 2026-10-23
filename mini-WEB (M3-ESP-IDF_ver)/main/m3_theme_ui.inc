/* ---------- Theme: accent colors, outlines, corners and the home wallpaper. Included by m3_ui.c. ---------- */
static const uint32_t theme_swatches[] = {0x00e5ef, 0x3b82f6, 0x8b5cf6, 0xf548ba, 0xef4444, 0xf97316, 0xfacc15, 0x22c55e, 0x2dd4be, 0xd4d4d8};
enum { THEME_SWATCHES = sizeof(theme_swatches) / sizeof(theme_swatches[0]) };
static m3_theme_t ui_theme;
static lv_obj_t *home_wallpaper;
static lv_obj_t *theme_swatch[2][THEME_SWATCHES], *theme_hue[2], *theme_hex[2];
static lv_obj_t *theme_outline_tabs, *theme_corner_tabs, *theme_walls[M3_WALLPAPER_COUNT];
static lv_timer_t *theme_save_timer;

static uint32_t theme_rgb(lv_color_t color) { return lv_color_to_u32(color) & 0xffffff; }
/* A runtime color change finds the old accents by value in local styles, so the
 * accents must never equal a neutral or icon color; nudge them by one bit instead. */
static uint32_t theme_distinct(uint32_t color, uint32_t avoid_a, uint32_t avoid_b) {
    static const uint32_t neutral[] = {INK, SURFACE, SURFACE_HI, LINE, TEXT, MUTED, GREEN, AMBER, RED, FIELD, 0xffffff, 0x000000,
        0x526175, 0xaab6c6, 0xc8d2e0, 0x3a4656, 0x0b1424, 0x0b1220, 0x08202b, 0x0d121a, 0x2a3445, 0x18202c, 0xc3cede, 0x3a1d27, 0xff9aa8};
    color &= 0xffffff;
    for (unsigned step = 0; step < 16; ++step) {
        bool clash = color == avoid_a || color == avoid_b;
        for (unsigned i = 0; !clash && i < sizeof(neutral) / sizeof(neutral[0]); ++i) clash = color == neutral[i];
        for (unsigned i = 0; !clash && i < sizeof(app_icons) / sizeof(app_icons[0]); ++i)
            clash = color == app_icons[i].top || color == app_icons[i].bottom;
        if (!clash) break;
        color ^= 1u << (step % 3 * 8);
    }
    return color;
}
static bool theme_near(uint32_t a, uint32_t b) {
    for (unsigned shift = 0; shift < 24; shift += 8) {
        int d = (int)((a >> shift) & 0xff) - (int)((b >> shift) & 0xff);
        if (d < -1 || d > 1) return false;
    }
    return true;
}
static void theme_resolve(void) {
    ui_theme.primary = theme_distinct(ui_theme.primary, UINT32_MAX, UINT32_MAX);
    ui_theme.accent = theme_distinct(ui_theme.accent, ui_theme.primary, UINT32_MAX);
    theme_primary = ui_theme.primary; theme_accent = ui_theme.accent;
    lv_color_t primary = lv_color_hex(theme_primary);
    uint32_t on = theme_primary == M3_THEME_PRIMARY ? 0x00272b :
        lv_color_luminance(primary) > 140 ? theme_rgb(lv_color_mix(primary, lv_color_black(), 44)) : 0xffffff;
    theme_on_accent = theme_distinct(on, theme_primary, theme_accent);
}
static void theme_styles(void) {
    lv_color_t primary = lv_color_hex(CYAN);
    lv_style_set_bg_color(&st_primary, primary); lv_style_set_text_color(&st_primary, lv_color_hex(ON_ACCENT));
    lv_style_set_bg_color(&st_primary_pressed, lv_color_darken(primary, 54));
    lv_style_set_border_color(&st_field_focus, primary);
    lv_style_set_text_color(&st_seg_checked, primary);
    lv_style_set_border_color(&st_list_checked, primary);
    lv_style_set_bg_color(&st_list_checked, lv_color_mix(primary, lv_color_hex(SURFACE), 27));
    lv_style_set_bg_color(&st_indicator, primary);
    static const uint8_t radii[M3_CORNER_COUNT][6] = {{18, 14, 12, 12, 14, 10}, {10, 8, 8, 8, 8, 6}, {4, 3, 3, 3, 3, 2}};
    lv_style_t *rounded[] = {&st_card, &st_btn, &st_list, &st_field, &st_seg, &st_seg_item};
    for (unsigned i = 0; i < 6; ++i) lv_style_set_radius(rounded[i], radii[ui_theme.corners][i]);
    bool accent = ui_theme.outline == M3_OUTLINE_ACCENT;
    lv_style_t *framed[] = {&st_card, &st_btn, &st_list};
    for (unsigned i = 0; i < 3; ++i) {
        lv_style_set_border_width(framed[i], ui_theme.outline == M3_OUTLINE_NONE ? 0 : 1);
        lv_style_set_border_color(framed[i], accent ? primary : lv_color_hex(LINE));
        lv_style_set_border_opa(framed[i], accent ? LV_OPA_60 : LV_OPA_COVER);
    }
    lv_style_t *changed[] = {&st_primary, &st_primary_pressed, &st_field_focus, &st_seg_checked, &st_list_checked,
        &st_indicator, &st_card, &st_btn, &st_list, &st_field, &st_seg, &st_seg_item};
    for (unsigned i = 0; i < sizeof(changed) / sizeof(changed[0]); ++i) lv_obj_report_style_change(changed[i]);
}
/* Walks local styles once per color change; shared styles were updated above. */
static void theme_recolor(lv_obj_t *obj, const lv_color_t *from, const lv_color_t *to) {
    static const lv_style_prop_t props[] = {LV_STYLE_TEXT_COLOR, LV_STYLE_BG_COLOR, LV_STYLE_BG_GRAD_COLOR, LV_STYLE_BORDER_COLOR,
        LV_STYLE_SHADOW_COLOR, LV_STYLE_ARC_COLOR, LV_STYLE_LINE_COLOR, LV_STYLE_OUTLINE_COLOR};
    static const lv_style_selector_t selectors[] = {LV_PART_MAIN, LV_PART_MAIN | LV_STATE_PRESSED, LV_PART_MAIN | LV_STATE_CHECKED,
        LV_PART_INDICATOR, LV_PART_INDICATOR | LV_STATE_CHECKED, LV_PART_KNOB, LV_PART_ITEMS, LV_PART_ITEMS | LV_STATE_PRESSED,
        LV_PART_ITEMS | LV_STATE_CHECKED};
    for (unsigned s = 0; s < sizeof(selectors) / sizeof(selectors[0]); ++s)
        for (unsigned p = 0; p < sizeof(props) / sizeof(props[0]); ++p) {
            lv_style_value_t value;
            if (lv_obj_get_local_style_prop(obj, props[p], &value, selectors[s]) != LV_STYLE_RES_FOUND) continue;
            for (unsigned k = 0; k < 3; ++k) if (lv_color_eq(value.color, from[k])) {
                value.color = to[k]; lv_obj_set_local_style_prop(obj, props[p], value, selectors[s]); break;
            }
        }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) theme_recolor(lv_obj_get_child(obj, i), from, to);
}
static void theme_wallpaper_sync(void) {
    m3_wallpaper_set_active(current_app == APP_HOME && !screen_blanked && ui_preferences.animations);
}
static void theme_controls_refresh(void) {
    if (!theme_hex[0]) return;
    const uint32_t colors[2] = {CYAN, MAGENTA};
    for (unsigned c = 0; c < 2; ++c) {
        for (unsigned i = 0; i < THEME_SWATCHES; ++i) {
            /* Restore the fixed swatch color in case the recolor walk matched it. */
            lv_obj_set_style_bg_color(theme_swatch[c][i], lv_color_hex(theme_swatches[i]), 0);
            lv_obj_set_style_border_width(theme_swatch[c][i], theme_near(theme_swatches[i], colors[c]) ? 3 : 0, 0);
        }
        char hex[12]; snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)colors[c]);
        set_text(theme_hex[c], hex); set_ink(theme_hex[c], colors[c]);
        lv_obj_set_style_bg_color(theme_hue[c], lv_color_hex(colors[c]), LV_PART_KNOB);
        if (!lv_obj_has_state(theme_hue[c], LV_STATE_PRESSED))
            lv_slider_set_value(theme_hue[c], lv_color_to_hsv(lv_color_hex(colors[c])).h, LV_ANIM_OFF);
    }
    if (segment_value(theme_outline_tabs) != ui_theme.outline) segment_select(theme_outline_tabs, ui_theme.outline);
    if (segment_value(theme_corner_tabs) != ui_theme.corners) segment_select(theme_corner_tabs, ui_theme.corners);
    for (unsigned i = 0; i < M3_WALLPAPER_COUNT; ++i) {
        uint32_t preview[3]; m3_wallpaper_preview(i, CYAN, MAGENTA, preview);
        lv_obj_set_style_bg_color(lv_obj_get_child(theme_walls[i], 0), lv_color_hex(preview[2]), 0);
        lv_obj_set_style_border_color(theme_walls[i], lv_color_hex(CYAN), 0);
        lv_obj_set_style_border_width(theme_walls[i], i == ui_theme.wallpaper ? 3 : 0, 0);
    }
}
static void theme_apply(const m3_theme_t *next) {
    if (!m3_theme_valid(next)) return;
    const lv_color_t from[3] = {lv_color_hex(CYAN), lv_color_hex(MAGENTA), lv_color_hex(ON_ACCENT)};
    uint32_t old_primary = CYAN, old_accent = MAGENTA; unsigned old_wallpaper = ui_theme.wallpaper;
    ui_theme = *next; theme_resolve();
    const lv_color_t to[3] = {lv_color_hex(CYAN), lv_color_hex(MAGENTA), lv_color_hex(ON_ACCENT)};
    theme_styles();
    bool recolor = old_primary != CYAN || old_accent != MAGENTA;
    if (recolor) {
        lv_theme_default_init(m3_board_display(), to[0], to[1], true, &m3_font_korean);
        theme_recolor(lv_display_get_screen_active(m3_board_display()), from, to);
        lv_chart_set_series_color(cpu_chart, cpu_series[0], to[0]); lv_chart_set_series_color(cpu_chart, cpu_series[1], to[1]);
        lv_chart_set_series_color(chart, heap_series, to[0]); lv_chart_set_series_color(chart, psram_series, to[1]);
        calendar_stamp = 0; update_calendar(); home_page_update();
    }
    if (recolor || old_wallpaper != ui_theme.wallpaper) m3_wallpaper_build(home_wallpaper, ui_theme.wallpaper, CYAN, MAGENTA);
    theme_controls_refresh();
    if (theme_save_timer) { lv_timer_reset(theme_save_timer); lv_timer_resume(theme_save_timer); }
}
static void theme_save_tick(lv_timer_t *timer) { lv_timer_pause(timer); m3_theme_save(&ui_theme); }
static uint32_t theme_hue_color(int32_t hue) { return theme_rgb(lv_color_hsv_to_rgb((uint16_t)hue, 80, 96)); }
static void theme_swatch_event(lv_event_t *event) {
    unsigned code = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    m3_theme_t next = ui_theme;
    if (code >> 8) next.accent = theme_swatches[code & 0xff]; else next.primary = theme_swatches[code & 0xff];
    theme_apply(&next);
}
static void theme_hue_event(lv_event_t *event) {
    unsigned which = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    uint32_t color = theme_hue_color(lv_slider_get_value(theme_hue[which]));
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        /* Preview while dragging; the whole UI recolors once on release. */
        char hex[12]; snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)color);
        set_text(theme_hex[which], hex); set_ink(theme_hex[which], color);
        lv_obj_set_style_bg_color(theme_hue[which], lv_color_hex(color), LV_PART_KNOB);
        return;
    }
    m3_theme_t next = ui_theme;
    if (which) next.accent = color; else next.primary = color;
    theme_apply(&next);
}
static void theme_option_event(lv_event_t *event) {
    m3_theme_t next = ui_theme;
    next.outline = (uint8_t)segment_value(theme_outline_tabs); next.corners = (uint8_t)segment_value(theme_corner_tabs);
    (void)event; theme_apply(&next);
}
static void theme_wall_event(lv_event_t *event) {
    m3_theme_t next = ui_theme;
    next.wallpaper = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    theme_apply(&next);
}
static void theme_reset(lv_event_t *event) { (void)event; m3_theme_t next; m3_theme_defaults(&next); theme_apply(&next); }
static void theme_color_picker(lv_obj_t *box, m3_text_t title, unsigned which) {
    lv_obj_t *line = row(box); grow(text(line, title, TEXT));
    theme_hex[which] = label(line, "", TEXT); lv_obj_set_width(theme_hex[which], LV_SIZE_CONTENT);
    lv_obj_t *swatches = row(box); lv_obj_set_style_pad_column(swatches, 6, 0);
    for (unsigned i = 0; i < THEME_SWATCHES; ++i) {
        lv_obj_t *swatch = bare(swatches); lv_obj_set_size(swatch, 34, 34);
        lv_obj_add_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0xffffff), 0);
        lv_obj_set_ext_click_area(swatch, 3);
        lv_obj_add_event_cb(swatch, sound_tap, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(swatch, theme_swatch_event, LV_EVENT_CLICKED, (void *)(uintptr_t)(which << 8 | i));
        theme_swatch[which][i] = swatch;
    }
    theme_hue[which] = lv_slider_create(box); style_slider(theme_hue[which]);
    lv_obj_set_width(theme_hue[which], LV_PCT(100)); lv_slider_set_range(theme_hue[which], 0, 359);
    lv_obj_set_style_bg_opa(theme_hue[which], LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(theme_hue[which], 10, LV_PART_KNOB);
    lv_obj_add_event_cb(theme_hue[which], theme_hue_event, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)which);
    lv_obj_add_event_cb(theme_hue[which], theme_hue_event, LV_EVENT_RELEASED, (void *)(uintptr_t)which);
}
static void theme_card(lv_obj_t *system) {
    lv_obj_t *box = card(system, T_THEME);
    theme_color_picker(box, T_THEME_PRIMARY, 0);
    theme_color_picker(box, T_THEME_ACCENT, 1);
    text(box, T_THEME_OUTLINE, TEXT);
    static const m3_text_t outlines[] = {T_OUTLINE_NONE, T_OUTLINE_SUBTLE, T_OUTLINE_ACCENT};
    theme_outline_tabs = segmented(box, outlines, NULL, M3_OUTLINE_COUNT, ui_theme.outline, theme_option_event);
    text(box, T_THEME_CORNERS, TEXT);
    static const m3_text_t corners[] = {T_CORNER_ROUND, T_CORNER_COMPACT, T_CORNER_SQUARE};
    theme_corner_tabs = segmented(box, corners, NULL, M3_CORNER_COUNT, ui_theme.corners, theme_option_event);
    text(box, T_THEME_WALLPAPER, TEXT);
    static const m3_text_t names[M3_WALLPAPER_COUNT] = {T_WALL_AURORA, T_WALL_MIDNIGHT, T_WALL_SUNSET, T_WALL_OCEAN,
        T_WALL_MONO, T_WALL_STARS, T_WALL_DRIFT, T_WALL_BUBBLES};
    lv_obj_t *grid = bare(box); lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 8, 0); lv_obj_set_style_pad_row(grid, 8, 0);
    for (unsigned i = 0; i < M3_WALLPAPER_COUNT; ++i) {
        uint32_t preview[3]; m3_wallpaper_preview(i, CYAN, MAGENTA, preview);
        lv_obj_t *tile = bare(grid); lv_obj_set_size(tile, 98, 118);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(tile, 12, 0); lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(preview[0]), 0);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(preview[1]), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_clip_corner(tile, true, 0);
        lv_obj_t *mark = bare(tile); lv_obj_set_pos(mark, 12, 14); lv_obj_set_size(mark, 28, 28);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(mark, LV_OPA_70, 0);
        lv_obj_t *name = text(tile, names[i], TEXT); lv_obj_set_pos(name, 0, 84); lv_obj_set_width(name, 98);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0); label_ellipsis(name);
        if (m3_wallpaper_live(i)) {
            lv_obj_t *live = caption(tile, T_WALL_LIVE); lv_obj_set_pos(live, 44, 18); lv_obj_set_width(live, 46);
            lv_obj_set_style_text_align(live, LV_TEXT_ALIGN_RIGHT, 0); label_ellipsis(live);
        }
        lv_obj_add_event_cb(tile, sound_tap, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(tile, theme_wall_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        theme_walls[i] = tile;
    }
    caption(box, T_THEME_HINT);
    button(box, LV_SYMBOL_REFRESH, T_THEME_RESET, BTN_SECONDARY, theme_reset, NULL);
    theme_save_timer = lv_timer_create(theme_save_tick, 1500, NULL); lv_timer_pause(theme_save_timer);
    theme_controls_refresh();
}
