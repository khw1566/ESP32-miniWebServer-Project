/* Included by m3_ui.c after its shared controls and browser callbacks. */
static lv_obj_t *calculator_input, *calculator_result, *calculator_mode;
static bool calculator_degrees = true;
static double calculator_answer;
static lv_obj_t *file_actions, *file_action_name, *file_action_info, *file_destination;
static char file_source[256];
static bool delete_armed, file_source_sd;
static unsigned seen_mutation;
static lv_obj_t *gallery_library, *gallery_viewer, *gallery_canvas, *gallery_info, *gallery_name;
static lv_obj_t *gallery_rows[STORAGE_VIEW_ROWS];
static unsigned gallery_row;

static void calculator_key(lv_event_t *event) {
    const char *key = lv_event_get_user_data(event);
    const char *value = lv_textarea_get_text(calculator_input);
    if (!strcmp(key, "AC")) { lv_obj_set_style_text_font(calculator_result, FONT_ICON, 0); lv_textarea_set_text(calculator_input, ""); lv_label_set_text(calculator_result, "0"); }
    else if (!strcmp(key, "DEL")) lv_textarea_delete_char(calculator_input);
    else if (!strcmp(key, "DEG")) {
        calculator_degrees = !calculator_degrees;
        lv_label_set_text(lv_obj_get_child(calculator_mode, 0), calculator_degrees ? "DEG" : "RAD");
    } else if (!strcmp(key, "=")) {
        double result;
        if (m3_calculator_eval(value, calculator_degrees, calculator_answer, &result)) {
            lv_obj_set_style_text_font(calculator_result, FONT_ICON, 0);
            calculator_answer = result; char number[40]; snprintf(number, sizeof(number), "%.12g", result);
            lv_label_set_text(calculator_result, number);
        } else { lv_obj_set_style_text_font(calculator_result, &m3_font_korean, 0); lv_label_set_text(calculator_result, tr(T_CALC_ERROR)); }
    } else lv_textarea_add_text(calculator_input, key);
}
static void build_calculator(lv_obj_t *page) {
    lv_obj_t *box = card(page, T_APP_CALCULATOR);
    calculator_input = lv_textarea_create(box); lv_obj_set_width(calculator_input, LV_PCT(100));
    lv_textarea_set_max_length(calculator_input, 192); lv_textarea_set_one_line(calculator_input, true);
    lv_textarea_set_text(calculator_input, "");
    calculator_result = label(box, "0", CYAN); lv_obj_set_style_text_font(calculator_result, FONT_ICON, 0);
    lv_obj_set_width(calculator_result, LV_PCT(100)); lv_label_set_long_mode(calculator_result, LV_LABEL_LONG_SCROLL_CIRCULAR);
    static const char *const keys[] = {
        "DEG", "(", ")", "Ans", "AC",
        "sin(", "cos(", "tan(", "log(", "ln(",
        "asin(", "acos(", "atan(", "sqrt(", "^",
        "7", "8", "9", "/", "DEL",
        "4", "5", "6", "*", "!",
        "1", "2", "3", "-", "%",
        "0", ".", "pi", "+", "=",
        "e", "exp(", "abs(", NULL, NULL};
    for (unsigned r = 0; r < 8; ++r) {
        lv_obj_t *line = row(box);
        for (unsigned c = 0; c < 5; ++c) {
            const char *key = keys[r * 5 + c]; if (!key) continue;
            lv_obj_t *control = button(line, NULL, T_NONE, !strcmp(key, "=") ? BTN_PRIMARY : BTN_SECONDARY, calculator_key, (void *)key);
            lv_obj_set_width(control, 73); lv_obj_set_height(control, 48);
            lv_obj_set_style_pad_all(control, 2, 0); lv_label_set_text(lv_obj_get_child(control, 0), key);
            if (!r && !c) calculator_mode = control;
        }
    }
    caption(page, T_CALC_HINT);
}
static void file_action_cancel(lv_event_t *event) {
    (void)event; delete_armed = false; file_source[0] = 0; hide_keyboard(); lv_obj_add_flag(file_actions, LV_OBJ_FLAG_HIDDEN);
}
static void file_action_select(lv_event_t *event) {
    unsigned row = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (snapshot.files.error != ESP_OK || row >= snapshot.files.shown || snapshot.mutation_busy) return;
    int n = snprintf(file_source, sizeof(file_source), "%s/%s", snapshot.folder, snapshot.files.entries[row].name);
    if (n <= 0 || (size_t)n >= sizeof(file_source)) { file_source[0] = 0; return; }
    file_source_sd = snapshot.browse_sd;
    delete_armed = false; lv_label_set_text(file_action_name, file_source);
    lv_label_set_text(file_action_info, tr(T_FILE_ACTION_HINT));
    lv_textarea_set_text(file_destination, file_source);
    lv_obj_remove_flag(file_actions, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view(file_actions, ui_animation());
}
static void file_delete(lv_event_t *event) {
    (void)event; if (!file_source[0] || snapshot.mutation_busy) return;
    if (!delete_armed) { delete_armed = true; lv_label_set_text(file_action_info, tr(T_FILE_CONFIRM_DELETE)); return; }
    delete_armed = false;
    lv_label_set_text(file_action_info, tr(m3_runtime_file_action(file_source_sd, file_source, NULL, true) ? T_FILE_WORKING : T_FILE_BUSY));
}
static void file_move(lv_event_t *event) {
    (void)event; delete_armed = false;
    if (!file_source[0] || snapshot.mutation_busy) return;
    lv_label_set_text(file_action_info, tr(m3_runtime_file_action(file_source_sd, file_source, lv_textarea_get_text(file_destination), false) ? T_FILE_WORKING : T_FILE_BUSY));
    hide_keyboard();
}
static void file_recover(lv_event_t *event) {
    (void)event; size_t length = strlen(file_source);
    if (length <= 9 || strcmp(file_source + length - 9, ".wav.part")) { lv_label_set_text(file_action_info, tr(T_FILE_FAILED)); return; }
    char destination[256]; memcpy(destination, file_source, length - 5); destination[length - 5] = 0;
    lv_label_set_text(file_action_info, tr(m3_runtime_file_action(file_source_sd, file_source, destination, false) ? T_FILE_WORKING : T_FILE_BUSY));
}
static void build_file_actions(lv_obj_t *page) {
    caption(page, T_FILE_LONG_PRESS);
    file_actions = card(page, T_FILE_ACTIONS);
    file_action_name = label(file_actions, "", CYAN); lv_label_set_long_mode(file_action_name, LV_LABEL_LONG_WRAP);
    file_destination = input_field(file_actions, T_FILE_DESTINATION, 255, INPUT_TEXT, false);
    file_action_info = text(file_actions, T_FILE_ACTION_HINT, MUTED);
    lv_obj_t *line = row(file_actions);
    grow(button(line, LV_SYMBOL_TRASH, T_FILE_DELETE, BTN_DANGER, file_delete, NULL));
    grow(button(line, LV_SYMBOL_RIGHT, T_FILE_MOVE, BTN_PRIMARY, file_move, NULL));
    button(file_actions, LV_SYMBOL_SAVE, T_FILE_RECOVER, BTN_SECONDARY, file_recover, NULL);
    button(file_actions, LV_SYMBOL_CLOSE, T_VOICE_CANCEL, BTN_SECONDARY, file_action_cancel, NULL);
    lv_obj_add_flag(file_actions, LV_OBJ_FLAG_HIDDEN);
}
static void gallery_load(unsigned index) {
    if (index >= snapshot.files.shown || !snapshot.browse_sd || snapshot.files.error != ESP_OK) return;
    const storage_view_entry_t *entry = &snapshot.files.entries[index];
    char path[256]; int n = snprintf(path, sizeof(path), "%s/%s", snapshot.folder, entry->name);
    if (n <= 0 || (size_t)n >= sizeof(path)) return;
    if (entry->directory) {
        snprintf(selected_folder, sizeof(selected_folder), "%s", path); selected_sd = true; selected_offset = 0; browse_refresh(); return;
    }
    if (!m3_gallery_supported(entry->name)) { lv_label_set_text(gallery_info, tr(T_GALLERY_UNSUPPORTED)); return; }
    if (!m3_gallery_open(path)) { lv_label_set_text(gallery_info, tr(T_FILE_BUSY)); return; }
    gallery_row = index;
    lv_label_set_text(gallery_name, entry->name);
    lv_obj_add_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gallery_library, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(pages[APP_GALLERY], 0, LV_ANIM_OFF);
}
static void gallery_pick(lv_event_t *event) { gallery_load((unsigned)(uintptr_t)lv_event_get_user_data(event)); }
static void gallery_library_open(lv_event_t *event) {
    (void)event; m3_gallery_close(); lv_obj_add_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gallery_library, LV_OBJ_FLAG_HIDDEN); browse_refresh();
}
static void gallery_pause(lv_event_t *event) { (void)event; m3_gallery_pause(); }
static void gallery_step(lv_event_t *event) {
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    if (!snapshot.files.shown) return;
    for (unsigned i = 1; i <= snapshot.files.shown; ++i) {
        unsigned index = (gallery_row + snapshot.files.shown + direction * (int)i) % snapshot.files.shown;
        if (!snapshot.files.entries[index].directory && m3_gallery_supported(snapshot.files.entries[index].name)) { gallery_load(index); return; }
    }
}
static void build_gallery(lv_obj_t *page, unsigned volume) {
    gallery_info = text(page, T_GALLERY_HINT, MUTED);
    gallery_viewer = card(page, T_NONE); lv_obj_set_style_pad_all(gallery_viewer, 0, 0);
    gallery_name = label(gallery_viewer, "", TEXT); lv_label_set_long_mode(gallery_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    gallery_canvas = lv_canvas_create(gallery_viewer); lv_obj_set_size(gallery_canvas, M3_PHOTO_W, M3_PHOTO_H);
    lv_obj_add_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *line = row(gallery_viewer);
    button(line, LV_SYMBOL_PREV, T_NONE, BTN_SECONDARY, gallery_step, (void *)(intptr_t)-1);
    grow(button(line, LV_SYMBOL_PAUSE, T_MUSIC_PAUSE, BTN_SECONDARY, gallery_pause, NULL));
    button(line, LV_SYMBOL_NEXT, T_NONE, BTN_SECONDARY, gallery_step, (void *)(intptr_t)1);
    button(gallery_viewer, LV_SYMBOL_DIRECTORY, T_FILE_LIST, BTN_SECONDARY, gallery_library_open, NULL);
    volume_row(gallery_viewer, 6, volume);
    lv_obj_add_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN);
    gallery_library = card(page, T_APP_GALLERY);
    button(gallery_library, LV_SYMBOL_UP, T_FILE_PARENT, BTN_SECONDARY, parent_folder, NULL);
    for (unsigned i = 0; i < STORAGE_VIEW_ROWS; ++i) gallery_rows[i] = list_row(gallery_library, gallery_pick, (void *)(uintptr_t)i);
    line = row(gallery_library);
    grow(button(line, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, previous_files, NULL));
    grow(button(line, LV_SYMBOL_RIGHT, T_FILES_NEXT, BTN_SECONDARY, next_files, NULL));
}
static void gallery_tick(lv_timer_t *timer) {
    (void)timer; if (current_app != APP_GALLERY || !gallery_canvas || lv_obj_has_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN)) return;
    m3_gallery_state_t state; m3_gallery_state(&state);
    const uint8_t *frame = m3_gallery_frame();
    if (frame && (state.status == M3_GALLERY_READY || state.status == M3_GALLERY_ENDED)) {
        lv_canvas_set_buffer(gallery_canvas, (void *)frame, M3_PHOTO_W, M3_PHOTO_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_remove_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(gallery_canvas);
    }
    m3_text_t message = state.status == M3_GALLERY_BAD_FORMAT ? T_GALLERY_UNSUPPORTED : state.status == M3_GALLERY_STORAGE ? T_GALLERY_STORAGE :
        state.status == M3_GALLERY_MEMORY ? T_GALLERY_MEMORY : state.status == M3_GALLERY_LOADING ? T_GALLERY_LOADING :
        state.status == M3_GALLERY_MP4_FORMAT ? T_GALLERY_MP4_FORMAT : state.status == M3_GALLERY_AUDIO_BUSY ? T_GALLERY_AUDIO_BUSY :
        state.status == M3_GALLERY_DECODE_ERROR ? T_GALLERY_DECODE_ERROR : state.status == M3_GALLERY_AUDIO_ERROR ? T_GALLERY_AUDIO_ERROR :
        state.status == M3_GALLERY_STOPPED ? T_GALLERY_STOPPED : state.status == M3_GALLERY_ENDED ? T_GALLERY_ENDED : state.paused ? T_MUSIC_PAUSE : T_GALLERY_HINT;
    if (state.mp4 && state.status == M3_GALLERY_READY && !state.paused) {
        char progress[64]; unsigned position = state.position_ms / 1000, duration = state.duration_ms / 1000;
        snprintf(progress, sizeof(progress), "MP4  %02u:%02u / %02u:%02u", position / 60, position % 60, duration / 60, duration % 60);
        set_text(gallery_info, progress);
    } else set_text(gallery_info, tr(message));
}
static void phone_update(void) {
    if (snapshot.mutation_revision != seen_mutation) {
        seen_mutation = snapshot.mutation_revision;
        lv_label_set_text(file_action_info, tr(snapshot.mutation_result == ESP_OK ? T_FILE_DONE : T_FILE_FAILED));
        file_source[0] = 0; delete_armed = false;
    }
    if (!snapshot.initialized) return;
    if (current_app != APP_GALLERY || !gallery_rows[0]) return;
    for (unsigned i = 0; i < STORAGE_VIEW_ROWS; ++i) {
        if (snapshot.browse_sd && snapshot.files.error == ESP_OK && i < snapshot.files.shown) {
            const storage_view_entry_t *entry = &snapshot.files.entries[i];
            lv_label_set_text(lv_obj_get_child(gallery_rows[i], 0), entry->directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_IMAGE);
            set_text(lv_obj_get_child(gallery_rows[i], 1), entry->name);
            lv_label_set_text(lv_obj_get_child(gallery_rows[i], 2), "");
            lv_obj_remove_flag(gallery_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(gallery_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void phone_enter(app_id_t app) {
    if (app == APP_GALLERY) {
        selected_sd = true;
        if (!selected_folder[0]) strcpy(selected_folder, "/media");
        browse_refresh();
    }
}
