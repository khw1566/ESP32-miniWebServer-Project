/* Gallery UI owns all thumbnail canvases; no file or codec calls from LVGL. */
typedef struct { lv_obj_t *tile, *canvas, *name, *placeholder, *play; } gallery_tile_t;
static gallery_tile_t gallery_tiles[M4_GALLERY_TILES];
static M4_PSRAM_BSS uint8_t gallery_thumb_pixels[M4_GALLERY_TILES][M4_GALLERY_THUMB_BYTES];
static M4_PSRAM_BSS m4_gallery_library_t gallery_catalog;
static lv_obj_t *gallery_grid_info, *gallery_page_label, *gallery_page_prev, *gallery_page_next;
static lv_obj_t *gallery_pause_button, *gallery_prev_media, *gallery_next_media;
static unsigned gallery_row = UINT_MAX, gallery_seen_generation, gallery_seen_revision, gallery_frame_revision;
static unsigned gallery_seen_language = UINT_MAX;
static bool gallery_library_requested;
static bool gallery_view_unsupported;

static void gallery_show_path(const char *path, unsigned index) {
    gallery_view_unsupported = !m4_gallery_supported(path);
    if (gallery_view_unsupported) m4_gallery_close();
    else if (!m4_gallery_open(path)) { set_text(gallery_grid_info, tr(T_FILE_BUSY)); return; }
    gallery_row = index; gallery_frame_revision = 0;
    const char *name = strrchr(path, '/'); set_text(gallery_name, name ? name + 1 : path);
    lv_obj_add_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gallery_library, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN);
    bool adjacent = index < gallery_catalog.shown;
    if (adjacent) { lv_obj_remove_state(gallery_prev_media, LV_STATE_DISABLED); lv_obj_remove_state(gallery_next_media, LV_STATE_DISABLED); }
    else { lv_obj_add_state(gallery_prev_media, LV_STATE_DISABLED); lv_obj_add_state(gallery_next_media, LV_STATE_DISABLED); }
}
/* File explorer handoff stays separate from the gallery's independent SD catalog. */
static void gallery_load(unsigned index) {
    if (index >= snapshot.files.shown || !snapshot.browse_sd || snapshot.files.error != ESP_OK || snapshot.files.entries[index].directory) return;
    char path[256]; int n = snprintf(path, sizeof(path), "%s/%s", snapshot.folder, snapshot.files.entries[index].name);
    if (n > 0 && (size_t)n < sizeof(path)) gallery_show_path(path, UINT_MAX);
}
static void gallery_pick(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index < gallery_catalog.shown) gallery_show_path(gallery_catalog.items[index].path, index);
}
static void gallery_grid_enter(unsigned page) {
    lv_obj_add_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gallery_library, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < M4_GALLERY_TILES; ++i) lv_obj_add_flag(gallery_tiles[i].tile, LV_OBJ_FLAG_HIDDEN);
    gallery_library_requested = m4_gallery_library_request(page);
    if (!gallery_library_requested) {
        set_text(gallery_grid_info, tr(T_GALLERY_STORAGE)); set_text(gallery_page_label, "--");
        lv_obj_add_state(gallery_page_prev, LV_STATE_DISABLED); lv_obj_add_state(gallery_page_next, LV_STATE_DISABLED);
    }
    else set_text(gallery_grid_info, tr(T_GALLERY_SCAN));
}
static void gallery_library_open(lv_event_t *event) { (void)event; gallery_grid_enter(gallery_catalog.page); }
static void gallery_refresh(lv_event_t *event) { (void)event; gallery_grid_enter(0); }
static void gallery_page(lv_event_t *event) {
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    if (gallery_catalog.status == M4_GALLERY_LOADING) return;
    if (direction < 0 && gallery_catalog.page) gallery_grid_enter(gallery_catalog.page - 1);
    if (direction > 0 && (gallery_catalog.page + 1) * M4_GALLERY_TILES < gallery_catalog.count) gallery_grid_enter(gallery_catalog.page + 1);
}
static void gallery_pause(lv_event_t *event) { (void)event; m4_gallery_pause(); }
static void gallery_step(lv_event_t *event) {
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    if (gallery_row >= gallery_catalog.shown || !gallery_catalog.shown) return;
    unsigned index = (gallery_row + gallery_catalog.shown + direction) % gallery_catalog.shown;
    gallery_show_path(gallery_catalog.items[index].path, index);
}
static void build_gallery(lv_obj_t *page, unsigned volume) {
    lv_obj_remove_style_all(page); lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    gallery_library = bare(page); lv_obj_set_size(gallery_library, LV_PCT(100), LV_PCT(100));
    gallery_grid_info = label(gallery_library, "", MUTED); lv_obj_set_pos(gallery_grid_info, 16, 48); lv_obj_set_size(gallery_grid_info, 448, 24);
    lv_label_set_long_mode(gallery_grid_info, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t *refresh = refresh_button(gallery_library,T_GALLERY_REFRESH,gallery_refresh,NULL);
    lv_obj_set_pos(refresh,416,0); lv_obj_set_size(refresh,48,44);
    lv_obj_t *grid = bare(gallery_library); lv_obj_set_pos(grid, 16, 80); lv_obj_set_size(grid, 448, 472);
    for (unsigned i = 0; i < M4_GALLERY_TILES; ++i) {
        gallery_tile_t *item = &gallery_tiles[i];
        item->tile = lv_button_create(grid); lv_obj_remove_style_all(item->tile);
        lv_obj_set_pos(item->tile, 4 + (i % 3) * 148, (i / 3) * 160); lv_obj_set_size(item->tile, 140, 152);
        lv_obj_set_style_radius(item->tile, 12, 0); lv_obj_set_style_bg_opa(item->tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item->tile, lv_color_hex(SURFACE), 0);
        lv_obj_set_style_bg_color(item->tile, lv_color_hex(SURFACE_HI), LV_STATE_PRESSED);
        lv_obj_remove_flag(item->tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item->tile, gallery_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        item->canvas = lv_canvas_create(item->tile); lv_obj_set_pos(item->canvas, 10, 6);
        lv_obj_set_size(item->canvas, M4_GALLERY_THUMB_W, M4_GALLERY_THUMB_H);
        lv_obj_remove_flag(item->canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        item->placeholder = label(item->tile, LV_SYMBOL_REFRESH, MUTED); lv_obj_set_pos(item->placeholder, 52, 48);
        lv_obj_set_size(item->placeholder, 36, 36); lv_obj_set_style_text_font(item->placeholder, FONT_ICON, 0);
        item->play = bare(item->tile); lv_obj_set_pos(item->play, 94, 90); lv_obj_set_size(item->play, 32, 32);
        lv_obj_set_style_radius(item->play, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(item->play, lv_color_hex(0), 0);
        lv_obj_set_style_bg_opa(item->play, LV_OPA_70, 0);
        lv_obj_t *triangle = label(item->play, LV_SYMBOL_PLAY, 0xffffff); lv_obj_set_width(triangle, LV_SIZE_CONTENT);
        lv_obj_align(triangle, LV_ALIGN_CENTER, 1, 0);
        item->name = label(item->tile, "", TEXT); lv_obj_set_pos(item->name, 6, 128); lv_obj_set_size(item->name, 128, 22);
        lv_label_set_long_mode(item->name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_flag(item->tile, LV_OBJ_FLAG_HIDDEN);
    }
    gallery_page_prev = button(gallery_library, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, gallery_page, (void *)(intptr_t)-1);
    lv_obj_set_pos(gallery_page_prev, 16, 568); lv_obj_set_size(gallery_page_prev, 130, 44);
    gallery_page_next = button(gallery_library, LV_SYMBOL_RIGHT, T_FILES_NEXT, BTN_SECONDARY, gallery_page, (void *)(intptr_t)1);
    lv_obj_set_pos(gallery_page_next, 334, 568); lv_obj_set_size(gallery_page_next, 130, 44);
    gallery_page_label = label(gallery_library, "", CYAN); lv_obj_set_pos(gallery_page_label, 150, 580); lv_obj_set_size(gallery_page_label, 180, 24);
    lv_obj_set_style_text_align(gallery_page_label, LV_TEXT_ALIGN_CENTER, 0);
    gallery_viewer = bare(page); lv_obj_set_size(gallery_viewer, LV_PCT(100), LV_PCT(100));
    gallery_name = label(gallery_viewer, "", TEXT); lv_obj_set_pos(gallery_name, 16, 10); lv_obj_set_size(gallery_name, 390, 26);
    lv_label_set_long_mode(gallery_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t *back = icon_button(gallery_viewer, LV_SYMBOL_LEFT, gallery_library_open, NULL);
    lv_obj_set_pos(back, 420, 0); lv_obj_set_size(back, 44, 40);
    gallery_canvas = lv_canvas_create(gallery_viewer); lv_obj_set_pos(gallery_canvas, 24, 44);
    lv_obj_set_size(gallery_canvas, M4_PHOTO_W, M4_PHOTO_H); lv_obj_add_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN);
    gallery_prev_media = icon_button(gallery_viewer, LV_SYMBOL_PREV, gallery_step, (void *)(intptr_t)-1);
    lv_obj_set_pos(gallery_prev_media, 36, 536); lv_obj_set_size(gallery_prev_media, 52, 44);
    gallery_pause_button = button(gallery_viewer, LV_SYMBOL_PAUSE, T_MUSIC_PAUSE, BTN_SECONDARY, gallery_pause, NULL);
    lv_obj_set_pos(gallery_pause_button, 128, 536); lv_obj_set_size(gallery_pause_button, 224, 44);
    gallery_next_media = icon_button(gallery_viewer, LV_SYMBOL_NEXT, gallery_step, (void *)(intptr_t)1);
    lv_obj_set_pos(gallery_next_media, 392, 536); lv_obj_set_size(gallery_next_media, 52, 44);
    lv_obj_t *line = volume_row(gallery_viewer, 6, volume); lv_obj_set_pos(line, 16, 590); lv_obj_set_size(line, 448, 30);
    gallery_info = label(gallery_viewer, "", MUTED); lv_obj_set_pos(gallery_info, 16, 630); lv_obj_set_size(gallery_info, 448, 22);
    lv_label_set_long_mode(gallery_info, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(gallery_viewer, LV_OBJ_FLAG_HIDDEN);
}
static void gallery_grid_tick(void) {
    if (!gallery_library_requested) return;
    m4_gallery_library_state(&gallery_catalog);
    if (gallery_seen_generation != gallery_catalog.generation) {
        gallery_seen_generation = gallery_catalog.generation;
        for (unsigned i = 0; i < M4_GALLERY_TILES; ++i) {
            lv_obj_add_flag(gallery_tiles[i].canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(gallery_tiles[i].placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (gallery_seen_revision != gallery_catalog.revision) {
        gallery_seen_revision = gallery_catalog.revision;
        for (unsigned i = 0; i < M4_GALLERY_TILES; ++i) {
            gallery_tile_t *item = &gallery_tiles[i];
            power_visible(item->tile, i < gallery_catalog.shown);
            if (i >= gallery_catalog.shown) continue;
            const char *name = strrchr(gallery_catalog.items[i].path, '/');
            set_text(item->name, name ? name + 1 : gallery_catalog.items[i].path);
            power_visible(item->play, gallery_catalog.items[i].video);
            set_text(item->placeholder, gallery_catalog.error_mask & (1u << i) ? LV_SYMBOL_WARNING : LV_SYMBOL_REFRESH);
        }
        char text[80]; unsigned pages = (gallery_catalog.count + M4_GALLERY_TILES - 1) / M4_GALLERY_TILES;
        snprintf(text, sizeof(text), "%u / %u", pages ? gallery_catalog.page + 1 : 0, pages); set_text(gallery_page_label, text);
        if (gallery_catalog.status == M4_GALLERY_LOADING) set_text(gallery_grid_info, tr(T_GALLERY_SCAN));
        else if (gallery_catalog.status == M4_GALLERY_STORAGE) set_text(gallery_grid_info, tr(T_GALLERY_STORAGE));
        else if (gallery_catalog.status == M4_GALLERY_MEMORY) set_text(gallery_grid_info, tr(T_GALLERY_MEMORY));
        else if (!gallery_catalog.count) set_text(gallery_grid_info, tr(T_GALLERY_EMPTY));
        else { snprintf(text, sizeof(text), tr(T_GALLERY_COUNT), gallery_catalog.count); set_text(gallery_grid_info, text); }
        if (gallery_catalog.status != M4_GALLERY_LOADING && gallery_catalog.page) lv_obj_remove_state(gallery_page_prev, LV_STATE_DISABLED);
        else lv_obj_add_state(gallery_page_prev, LV_STATE_DISABLED);
        if (gallery_catalog.status != M4_GALLERY_LOADING && gallery_catalog.page + 1 < pages) lv_obj_remove_state(gallery_page_next, LV_STATE_DISABLED);
        else lv_obj_add_state(gallery_page_next, LV_STATE_DISABLED);
    }
    for (unsigned i = 0; i < gallery_catalog.shown; ++i) if (m4_gallery_thumbnail_copy(gallery_catalog.generation, i, gallery_thumb_pixels[i], M4_GALLERY_THUMB_BYTES)) {
        lv_canvas_set_buffer(gallery_tiles[i].canvas, gallery_thumb_pixels[i], M4_GALLERY_THUMB_W, M4_GALLERY_THUMB_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_remove_flag(gallery_tiles[i].canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gallery_tiles[i].placeholder, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(gallery_tiles[i].canvas);
    }
}
static void gallery_tick(lv_timer_t *timer) {
    (void)timer; if (current_app != APP_GALLERY || !gallery_canvas) return;
    unsigned language = m4_i18n_language();
    if (gallery_seen_language != language) {
        gallery_seen_language = language; gallery_seen_revision = gallery_frame_revision = UINT_MAX;
    }
    if (!lv_obj_has_flag(gallery_library, LV_OBJ_FLAG_HIDDEN)) { gallery_grid_tick(); return; }
    if (gallery_view_unsupported) {
        set_text(gallery_info, tr(T_GALLERY_UNSUPPORTED)); lv_obj_add_state(gallery_pause_button, LV_STATE_DISABLED); return;
    }
    m4_gallery_state_t state; m4_gallery_state(&state);
    if (state.revision == gallery_frame_revision) return;
    gallery_frame_revision = state.revision;
    const uint8_t *frame = m4_gallery_frame();
    if (frame && (state.status == M4_GALLERY_READY || state.status == M4_GALLERY_ENDED)) {
        lv_canvas_set_buffer(gallery_canvas, (void *)frame, M4_PHOTO_W, M4_PHOTO_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_remove_flag(gallery_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(gallery_canvas);
    }
    if (state.video) lv_obj_remove_state(gallery_pause_button, LV_STATE_DISABLED); else lv_obj_add_state(gallery_pause_button, LV_STATE_DISABLED);
    set_text(lv_obj_get_child(gallery_pause_button, 0), tr(state.paused ? T_MUSIC_RESUME : T_MUSIC_PAUSE));
    m4_text_t message = state.status == M4_GALLERY_BAD_FORMAT ? T_GALLERY_UNSUPPORTED : state.status == M4_GALLERY_STORAGE ? T_GALLERY_STORAGE :
        state.status == M4_GALLERY_MEMORY ? T_GALLERY_MEMORY : state.status == M4_GALLERY_LOADING ? T_GALLERY_LOADING :
        state.status == M4_GALLERY_MP4_FORMAT ? T_GALLERY_MP4_FORMAT : state.status == M4_GALLERY_AUDIO_BUSY ? T_GALLERY_AUDIO_BUSY :
        state.status == M4_GALLERY_DECODE_ERROR ? T_GALLERY_DECODE_ERROR : state.status == M4_GALLERY_AUDIO_ERROR ? T_GALLERY_AUDIO_ERROR :
        state.status == M4_GALLERY_STOPPED ? T_GALLERY_STOPPED : state.status == M4_GALLERY_ENDED ? T_GALLERY_ENDED : state.paused ? T_MUSIC_PAUSE : T_GALLERY_HINT;
    if (state.mp4 && state.status == M4_GALLERY_READY && !state.paused) {
        char progress[64]; unsigned position = state.position_ms / 1000, duration = state.duration_ms / 1000;
        snprintf(progress, sizeof(progress), "MP4  %02u:%02u / %02u:%02u", position / 60, position % 60, duration / 60, duration % 60);
        set_text(gallery_info, progress);
    } else set_text(gallery_info, tr(message));
}
