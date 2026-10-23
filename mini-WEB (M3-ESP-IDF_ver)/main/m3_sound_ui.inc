/* ---------- Sound set picker in the Settings sound card. Included by m3_ui.c. ---------- */
static lv_obj_t *sound_pack_rows[M3_SOUND_PACK_COUNT];
static void sound_pack_refresh(void) {
    unsigned selected = m3_feedback_pack();
    for (unsigned i = 0; i < M3_SOUND_PACK_COUNT; ++i) {
        if (i == selected) lv_obj_add_state(sound_pack_rows[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(sound_pack_rows[i], LV_STATE_CHECKED);
    }
}
static void sound_pack_select(lv_event_t *event) {
    m3_feedback_set_pack((unsigned)(uintptr_t)lv_event_get_user_data(event));
    sound_pack_refresh();
    /* The boot melody is each set's signature; preview it through the boot channel. */
    m3_feedback_clear(); m3_feedback_emit(M3_CUE_BOOT, m3_i18n_language());
}
static void sound_pack_list(lv_obj_t *box) {
    static const m3_text_t names[M3_SOUND_PACK_COUNT] = {T_PACK_CLASSIC, T_PACK_CRYSTAL, T_PACK_RETRO, T_PACK_SOFT, T_PACK_MINIMAL};
    text(box, T_SOUND_PACK, TEXT);
    for (unsigned i = 0; i < M3_SOUND_PACK_COUNT; ++i) {
        lv_obj_t *item = lv_button_create(box); lv_obj_remove_style_all(item);
        lv_obj_add_style(item, &st_list, 0); lv_obj_add_style(item, &st_list_checked, LV_STATE_CHECKED);
        lv_obj_add_style(item, &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_t *icon = label(item, LV_SYMBOL_AUDIO, MUTED); lv_obj_set_width(icon, LV_SIZE_CONTENT);
        grow(text(item, names[i], TEXT));
        lv_obj_add_event_cb(item, sound_pack_select, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        sound_pack_rows[i] = item;
    }
    caption(box, T_SOUND_PACK_HINT);
    sound_pack_refresh();
}
