/* Included by m3_ui.c after the YouTube section: the YouTube Music app.
 * Songs come from the same PC relay as audio only (m3_tube_play_audio). Playback runs
 * on the audio worker and keeps going after leaving the app, like the music player;
 * the next song of the list starts when one ends. */
typedef struct { lv_obj_t *button, *title, *meta, *thumb, *canvas; } ytm_row_t;
static M3_PSRAM_BSS ytm_row_t ytm_rows[M3_TUBE_ITEMS];
static lv_obj_t *ytm_query, *ytm_heading, *ytm_status, *ytm_prev, *ytm_next, *ytm_page, *ytm_pager;
static lv_obj_t *ytm_title, *ytm_artist, *ytm_seek, *ytm_time, *ytm_total, *ytm_toggle, *ytm_state, *ytm_wait, *ytm_autonext;
static lv_obj_t *ytm_prev_song, *ytm_next_song;
static lv_obj_t *ytm_cache_info[2], *ytm_download_info, *ytm_save, *ytm_save_next;
static M3_PSRAM_BSS m3_tube_list_t ytm_list;
/* Songs of the listing the current track came from; turning pages does not change it. */
static M3_PSRAM_BSS m3_tube_item_t ytm_queue[M3_TUBE_ITEMS];
static unsigned ytm_queue_count, ytm_index, ytm_start_s, ytm_duration_s;
static unsigned seen_ytm_list = ~0u;
static unsigned ytm_request_revision;
static bool ytm_pending, ytm_submitted;
static m3_text_t ytm_error = T_NONE;
static uint16_t seen_ytm_thumbs;
static lv_obj_t *ytm_cover, *ytm_cover_canvas;
static bool ytm_cover_ready;
static M3_PSRAM_BSS uint8_t ytm_cover_pixels[M3_TUBE_THUMB_W * M3_TUBE_THUMB_H * 2];
static int64_t ytm_pending_until;
static char ytm_last_query[M3_TUBE_QUERY_BYTES];
static bool ytm_download_error;

static void ytm_prepare_pair(void) {
    if (!ytm_queue_count) return;
    m3_tube_music_prepare(ytm_queue[ytm_index].id,
        ytm_index+1<ytm_queue_count ? ytm_queue[ytm_index+1].id : NULL);
}
static void ytm_autonext_changed(lv_event_t *event) { (void)event; m3_tube_music_autonext(segment_value(ytm_autonext)==1); }
static void ytm_save_event(lv_event_t *event) {
    m3_tube_download_t download; m3_tube_download_state(&download);
    if (download.busy && download.music) { m3_tube_download_cancel(); return; }
    unsigned index=ytm_index+(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index>=ytm_queue_count || download.busy) return;
    ytm_download_error=!m3_tube_music_download(ytm_queue[index].id,ytm_queue[index].title);
}
static void ytm_update_cache(void) {
    char text[192];
    for (unsigned i=0;i<2;++i) {
        unsigned index=ytm_index+i; m3_tube_music_cache_t cache;
        if (index>=ytm_queue_count) { set_text(ytm_cache_info[i],""); continue; }
        m3_tube_music_cache(ytm_queue[index].id,&cache);
        snprintf(text,sizeof(text),"%s: %s  %u%%",tr(i?T_YTM_NEXT_CACHE:T_YTM_CURRENT_CACHE),status_text(cache.message),cache.percent);
        set_text(ytm_cache_info[i],text);
    }
    m3_tube_download_t download; m3_tube_download_state(&download);
    const char *message=ytm_download_error?tr(T_TUBE_BUSY):download.music?status_text(download.message):"";
    if (download.busy && download.music && download.total && !ytm_download_error) {
        snprintf(text,sizeof(text),"%s / %u%%",message,download.percent); set_text(ytm_download_info,text);
    } else set_text(ytm_download_info,message);
    set_text(lv_obj_get_child(ytm_save,0),tr(download.busy && download.music?T_TUBE_SAVE_CANCEL:T_TUBE_SAVE));
    if (ytm_queue_count && (!download.busy || download.music)) lv_obj_remove_state(ytm_save,LV_STATE_DISABLED);
    else lv_obj_add_state(ytm_save,LV_STATE_DISABLED);
    if (!download.busy && ytm_index+1<ytm_queue_count) lv_obj_remove_state(ytm_save_next,LV_STATE_DISABLED);
    else lv_obj_add_state(ytm_save_next,LV_STATE_DISABLED);
}

static void ytm_update_cover(void) {
    if (ytm_cover_ready || !ytm_queue_count) return;
    for (unsigned i = 0; i < ytm_list.count; ++i) {
        if (strcmp(ytm_list.items[i].id, ytm_queue[ytm_index].id)) continue;
        const uint8_t *pixels = m3_tube_thumb(&ytm_list, i);
        if (!pixels) return;
        /* Independent UI-owned cover survives another search or list page. */
        memcpy(ytm_cover_pixels, pixels, sizeof(ytm_cover_pixels));
        lv_canvas_set_buffer(ytm_cover_canvas, ytm_cover_pixels, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_remove_flag(ytm_cover_canvas, LV_OBJ_FLAG_HIDDEN); ytm_cover_ready = true; return;
    }
}

/* Queues the song at queue index; the audio worker takes it once the previous source stopped. */
static void ytm_start(unsigned index, unsigned seconds) {
    if (index >= ytm_queue_count) return;
    const m3_tube_item_t *item = &ytm_queue[index];
    ytm_index = index; ytm_start_s = seconds; ytm_duration_s = item->duration;
    ytm_prepare_pair();
    set_text(ytm_title, item->title); set_text(ytm_artist, item->channel);
    lv_slider_set_range(ytm_seek, 0, item->duration ? (int32_t)item->duration : 1);
    lv_slider_set_value(ytm_seek, (int32_t)seconds, LV_ANIM_OFF);
    m3_audio_state_t audio; m3_audio_state(&audio);
    if (audio.playing) m3_audio_stop();
    ytm_pending = true; ytm_submitted = false; ytm_error = T_NONE;
    ytm_cover_ready = false; lv_obj_add_flag(ytm_cover_canvas, LV_OBJ_FLAG_HIDDEN); ytm_update_cover();
    ytm_pending_until = esp_timer_get_time() + 8000000;
    set_text(ytm_state, tr(T_TUBE_CONNECTING));
}
static void ytm_submit(void) {
    const char *query = lv_textarea_get_text(ytm_query);
    if (!query[0]) return;
    if (strlen(query) >= M3_TUBE_QUERY_BYTES || !m3_utf8_valid(query)) { set_text(ytm_status, tr(T_TUBE_QUERY_LONG)); return; }
    hide_keyboard();
    snprintf(ytm_last_query, sizeof(ytm_last_query), "%s", query);
    if (!m3_tube_music_search(query, 1)) set_text(ytm_status, tr(T_TUBE_NOT_CONFIGURED));
}
static void ytm_search_event(lv_event_t *event) { (void)event; ytm_submit(); }
static void ytm_chip(lv_event_t *event) {
    lv_textarea_set_text(ytm_query, tr((m3_text_t)(uintptr_t)lv_event_get_user_data(event)));
    ytm_submit();
}
static void ytm_page_turn(lv_event_t *event) {
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    unsigned page = (unsigned)((int)ytm_list.page + step);
    if (page < 1 || (step > 0 && !ytm_list.more)) return;
    m3_tube_music_search(ytm_list.query, page);
}
static void ytm_open(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= ytm_list.count) return;
    memcpy(ytm_queue, ytm_list.items, sizeof(ytm_queue)); ytm_queue_count = ytm_list.count;
    ytm_start(index, 0);
    lv_obj_scroll_to_y(pages[APP_YTMUSIC], 0, ui_animation());
}
static void ytm_toggle_event(lv_event_t *event) {
    (void)event;
    m3_tube_player_t player; m3_tube_player(&player);
    if (player.active && player.audio_only) m3_tube_pause(!player.paused);
    else if (!ytm_pending && ytm_queue_count) {
        unsigned position = player.audio_only ? player.position_ms / 1000 : 0;
        ytm_start(ytm_index, ytm_duration_s && position + 1 >= ytm_duration_s ? 0 : position);
    }
}
static void ytm_step_event(lv_event_t *event) {
    int target = (int)ytm_index + (int)(intptr_t)lv_event_get_user_data(event);
    if (target >= 0 && target < (int)ytm_queue_count) ytm_start((unsigned)target, 0);
}
static void ytm_seek_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    char text[16]; format_clock((unsigned)lv_slider_get_value(ytm_seek), text, sizeof(text));
    if (code == LV_EVENT_VALUE_CHANGED) set_text(ytm_time, text);
    else if (code == LV_EVENT_RELEASED && ytm_queue_count) ytm_start(ytm_index, (unsigned)lv_slider_get_value(ytm_seek));
}
static void ytm_render_rows(void) {
    char text[96], clock[16];
    for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) {
        ytm_row_t *row = &ytm_rows[i];
        if (i >= ytm_list.count) { lv_obj_add_flag(row->button, LV_OBJ_FLAG_HIDDEN); continue; }
        const m3_tube_item_t *item = &ytm_list.items[i];
        lv_label_set_text(row->title, item->title);
        if (item->duration) { format_clock(item->duration, clock, sizeof(clock)); snprintf(text, sizeof(text), "%s  %s", item->channel, clock); }
        else snprintf(text, sizeof(text), "%s", item->channel);
        lv_label_set_text(row->meta, text);
        lv_obj_remove_flag(row->button, LV_OBJ_FLAG_HIDDEN);
    }
}
static void update_ytm_list(void) {
    m3_tube_music_list(&ytm_list);
    if (ytm_list.revision != seen_ytm_list) {
    seen_ytm_list = ytm_list.revision;
    seen_ytm_thumbs = 0;
    for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) lv_obj_add_flag(ytm_rows[i].canvas, LV_OBJ_FLAG_HIDDEN);
    char text[512];
    ytm_render_rows();
    if (ytm_list.count) snprintf(text, sizeof(text), tr(T_YTM_RESULTS_FOR), ytm_list.query); else text[0] = 0;
    lv_label_set_text(ytm_heading, text);
    const char *status = !m3_tube_configured() ? tr(T_TUBE_NOT_CONFIGURED) :
        ytm_list.status == M3_TUBE_LIST_LOADING ? tr(T_YTM_LOADING) :
        ytm_list.status == M3_TUBE_LIST_ERROR ? status_text(ytm_list.message) :
        ytm_list.status == M3_TUBE_LIST_READY && !ytm_list.count ? tr(T_YTM_NO_RESULTS) :
        ytm_list.status == M3_TUBE_LIST_IDLE ? tr(T_YTM_EMPTY) : "";
    lv_label_set_text(ytm_status, status);
    if (status[0]) lv_obj_remove_flag(ytm_status, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(ytm_status, LV_OBJ_FLAG_HIDDEN);
    set_ink(ytm_status, ytm_list.status == M3_TUBE_LIST_ERROR ? RED : MUTED);
    snprintf(text, sizeof(text), tr(T_TUBE_PAGE), ytm_list.page);
    lv_label_set_text(ytm_page, text);
    if (ytm_list.count && (ytm_list.page > 1 || ytm_list.more)) lv_obj_remove_flag(ytm_pager, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ytm_pager, LV_OBJ_FLAG_HIDDEN);
    if (ytm_list.page > 1) lv_obj_remove_state(ytm_prev, LV_STATE_DISABLED); else lv_obj_add_state(ytm_prev, LV_STATE_DISABLED);
    if (ytm_list.more) lv_obj_remove_state(ytm_next, LV_STATE_DISABLED); else lv_obj_add_state(ytm_next, LV_STATE_DISABLED);
    }
    /* Thumbnail bits arrive after READY without a metadata revision. */
    if (ytm_list.thumbs != seen_ytm_thumbs) {
        for (unsigned i = 0; i < ytm_list.count; ++i) {
            if (seen_ytm_thumbs & (1u << i)) continue;
            const uint8_t *pixels = m3_tube_thumb(&ytm_list, i); if (!pixels) continue;
            lv_canvas_set_buffer(ytm_rows[i].canvas, (void *)pixels, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H, LV_COLOR_FORMAT_RGB565);
            lv_obj_remove_flag(ytm_rows[i].canvas, LV_OBJ_FLAG_HIDDEN);
        }
        seen_ytm_thumbs = ytm_list.thumbs;
    }
    ytm_update_cover();
}
/* Runs every refresh even when the app is hidden, so the next song follows in the background. */
static void update_ytm(const m3_audio_state_t *audio) {
    update_ytm_list();
    m3_tube_player_t player; m3_tube_player(&player);
    bool mine = player.audio_only, active = mine && player.active;
    if (ytm_pending && !ytm_submitted && !audio->playing && !player.active) {
        if (!m3_tube_configured()) { ytm_pending = false; ytm_error = T_TUBE_NOT_CONFIGURED; }
        else {
            ytm_request_revision = player.revision;
            ytm_submitted = m3_tube_play_audio(ytm_queue[ytm_index].id, ytm_start_s);
        }
    }
    if (ytm_pending && ytm_submitted && mine && !strcmp(player.id, ytm_queue[ytm_index].id) &&
        (active || player.revision != ytm_request_revision)) ytm_pending = false;
    if (ytm_pending && esp_timer_get_time() > ytm_pending_until) {
        ytm_pending = false; ytm_error = T_TUBE_BUSY;
    }
    /* The audio worker queues EOF successors. Adopt its song without stopping or
     * submitting it again; this also updates the next prefetch while the app is hidden. */
    if (active && !ytm_pending && ytm_queue_count && strcmp(player.id,ytm_queue[ytm_index].id)) {
        for (unsigned i=0;i<ytm_queue_count;++i) if (!strcmp(player.id,ytm_queue[i].id)) {
            ytm_index=i; ytm_duration_s=player.duration_s?player.duration_s:ytm_queue[i].duration;
            set_text(ytm_title,ytm_queue[i].title); set_text(ytm_artist,ytm_queue[i].channel);
            lv_slider_set_range(ytm_seek,0,ytm_duration_s?(int32_t)ytm_duration_s:1);
            ytm_cover_ready=false; lv_obj_add_flag(ytm_cover_canvas,LV_OBJ_FLAG_HIDDEN); ytm_update_cover();
            ytm_prepare_pair(); break;
        }
    }
    if (current_app != APP_YTMUSIC) return;
    ytm_update_cache();
    char text[64];
    if (!ytm_queue_count) set_text(ytm_title, tr(T_YTM_IDLE)); /* follows a language change */
    if (active && !strcmp(player.id, ytm_queue[ytm_index].id)) {
        if (player.duration_s && player.duration_s != ytm_duration_s) {
            ytm_duration_s = player.duration_s; lv_slider_set_range(ytm_seek, 0, (int32_t)ytm_duration_s);
        }
        if (!lv_obj_has_state(ytm_seek, LV_STATE_PRESSED)) {
            lv_slider_set_value(ytm_seek, (int32_t)(player.position_ms / 1000), LV_ANIM_OFF);
            format_clock(player.position_ms / 1000, text, sizeof(text)); set_text(ytm_time, text);
        }
    }
    format_clock(ytm_duration_s, text, sizeof(text)); set_text(ytm_total, ytm_duration_s ? text : "--:--");
    set_text(lv_obj_get_child(ytm_toggle, 0), active && !player.paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (ytm_index > 0) lv_obj_remove_state(ytm_prev_song, LV_STATE_DISABLED); else lv_obj_add_state(ytm_prev_song, LV_STATE_DISABLED);
    if (ytm_index + 1 < ytm_queue_count) lv_obj_remove_state(ytm_next_song, LV_STATE_DISABLED);
    else lv_obj_add_state(ytm_next_song, LV_STATE_DISABLED);
    const char *state = ytm_error != T_NONE ? tr(ytm_error) : ytm_pending ? tr(T_TUBE_CONNECTING) : !active ? (mine ? status_text(audio->message) : "") :
        player.buffering ? tr(T_TUBE_BUFFERING) : player.paused ? tr(T_TUBE_PAUSED) : "";
    if (active && player.buffering && !ytm_pending) { snprintf(text, sizeof(text), "%s  %u%%", state, player.buffer_percent); state = text; }
    set_text(ytm_state, state);
    activity_set(ytm_wait, ytm_pending || (active && player.buffering));
}
static void build_ytmusic(lv_obj_t *page, unsigned volume) {
    lv_obj_t *box = card(page, T_NOW_PLAYING);
    lv_obj_t *heading = row(box); lv_obj_set_style_flex_cross_place(heading, LV_FLEX_ALIGN_CENTER, 0);
    ytm_cover = bare(heading); lv_obj_set_size(ytm_cover, 96, 96);
    lv_obj_set_style_radius(ytm_cover, 12, 0); lv_obj_set_style_clip_corner(ytm_cover, true, 0);
    lv_obj_set_style_bg_opa(ytm_cover, LV_OPA_COVER, 0); set_bg(ytm_cover, 0xff0033);
    lv_obj_t *cover_placeholder = label(ytm_cover, LV_SYMBOL_PLAY, 0xffffff);
    lv_obj_set_width(cover_placeholder, LV_SIZE_CONTENT); lv_obj_set_style_text_font(cover_placeholder, FONT_ICON, 0); lv_obj_center(cover_placeholder);
    ytm_cover_canvas = lv_canvas_create(ytm_cover); lv_obj_add_flag(ytm_cover_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ytm_cover_canvas, -37, 0); lv_obj_set_style_transform_scale(ytm_cover_canvas, 273, 0);
    lv_obj_t *heading_text = bare(heading); grow(heading_text); lv_obj_set_height(heading_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading_text, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(heading_text, 6, 0);
    ytm_title = label(heading_text, tr(T_YTM_IDLE), TEXT); lv_obj_add_style(ytm_title, &st_title, 0);
    label_ellipsis(ytm_title);
    ytm_artist = label(heading_text, "", MUTED); label_ellipsis(ytm_artist);
    lv_obj_t *line = row(box);
    ytm_time = label(line, "0:00", MUTED); lv_obj_set_width(ytm_time, 64);
    ytm_seek = lv_slider_create(line); style_slider(ytm_seek); grow(ytm_seek);
    lv_slider_set_range(ytm_seek, 0, 1);
    lv_obj_add_event_cb(ytm_seek, ytm_seek_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ytm_seek, ytm_seek_event, LV_EVENT_RELEASED, NULL);
    ytm_total = label(line, "--:--", MUTED); lv_obj_set_width(ytm_total, 64); lv_obj_set_style_text_align(ytm_total, LV_TEXT_ALIGN_RIGHT, 0);
    line = row(box); lv_obj_set_style_flex_main_place(line, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_flex_cross_place(line, LV_FLEX_ALIGN_CENTER, 0);
    ytm_prev_song = icon_button(line, LV_SYMBOL_PREV, ytm_step_event, (void *)(intptr_t)-1); lv_obj_set_size(ytm_prev_song, 64, 56);
    ytm_toggle = icon_button(line, LV_SYMBOL_PLAY, ytm_toggle_event, NULL);
    lv_obj_add_style(ytm_toggle, &st_primary, 0); lv_obj_add_style(ytm_toggle, &st_primary_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(ytm_toggle, 72, 72); lv_obj_set_style_radius(ytm_toggle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(ytm_toggle, 0), FONT_ICON, 0);
    ytm_next_song = icon_button(line, LV_SYMBOL_NEXT, ytm_step_event, (void *)(intptr_t)1); lv_obj_set_size(ytm_next_song, 64, 56);
    lv_obj_add_state(ytm_prev_song, LV_STATE_DISABLED); lv_obj_add_state(ytm_next_song, LV_STATE_DISABLED);
    line = row(box);
    ytm_state = label(line, "", MUTED); grow(ytm_state); label_ellipsis(ytm_state);
    ytm_wait = activity_create(line, ACTIVITY_DOTS, CYAN, T_NONE);
    static const m3_text_t modes[] = {T_YTM_AUTONEXT_OFF, T_YTM_AUTONEXT_ON};
    ytm_autonext = segmented(box, modes, NULL, 2, 1, ytm_autonext_changed);
    ytm_cache_info[0]=label(box,"",MUTED); ytm_cache_info[1]=label(box,"",MUTED);
    for (unsigned i=0;i<2;++i) label_ellipsis(ytm_cache_info[i]);
    line=row(box);
    ytm_save=grow(button(line,LV_SYMBOL_DOWNLOAD,T_TUBE_SAVE,BTN_SECONDARY,ytm_save_event,NULL));
    ytm_save_next=grow(button(line,LV_SYMBOL_DOWNLOAD,T_YTM_SAVE_NEXT,BTN_SECONDARY,ytm_save_event,(void *)(uintptr_t)1));
    ytm_download_info=label(box,"",MUTED); label_ellipsis(ytm_download_info);
    caption(box,T_YTM_CACHE_HINT);
    box = card(page, T_SEC_VOLUME); volume_row(box, 7, volume);

    ytm_query = input_field(page, T_NONE, 100, INPUT_KOREAN, true);
    bind_text(ytm_query, T_YTM_PLACEHOLDER, NULL, BIND_PLACEHOLDER);
    lv_obj_add_event_cb(ytm_query, ytm_search_event, LV_EVENT_READY, NULL);
    lv_obj_t *go = icon_button(lv_obj_get_parent(ytm_query), LV_SYMBOL_RIGHT, ytm_search_event, NULL);
    lv_obj_add_style(go, &st_primary, 0); lv_obj_add_style(go, &st_primary_pressed, LV_STATE_PRESSED);
    lv_obj_t *chips = row(page);
    lv_obj_add_flag(chips, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(chips, LV_SCROLLBAR_MODE_OFF); lv_obj_set_style_pad_column(chips, 8, 0);
    for (unsigned i = 0; i < 6; ++i) {
        m3_text_t topic = (m3_text_t)(T_YTM_TOPIC_1 + i);
        lv_obj_t *chip = button(chips, NULL, topic, BTN_SECONDARY, ytm_chip, (void *)(uintptr_t)topic);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 40); lv_obj_set_style_radius(chip, 20, 0); lv_obj_set_style_pad_hor(chip, 18, 0);
        /* A 100% wide label inside a content-sized button resolves to zero width (no text). */
        lv_obj_t *chip_text = lv_obj_get_child(chip, 0);
        lv_obj_set_width(chip_text, LV_SIZE_CONTENT); lv_label_set_long_mode(chip_text, LV_LABEL_LONG_CLIP);
        lv_obj_center(chip_text);
    }
    ytm_heading = label(page, "", TEXT); label_ellipsis(ytm_heading);
    ytm_status = caption(page, T_YTM_EMPTY);
    for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) {
        ytm_row_t *item = &ytm_rows[i];
        item->button = lv_button_create(page); lv_obj_remove_style_all(item->button);
        lv_obj_add_style(item->button, &st_card, 0); lv_obj_add_style(item->button, &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(item->button, 10, 0); lv_obj_set_style_radius(item->button, 14, 0);
        lv_obj_set_flex_flow(item->button, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(item->button, 12, 0);
        lv_obj_set_style_flex_cross_place(item->button, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(item->button, ytm_open, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        item->thumb = bare(item->button); lv_obj_set_size(item->thumb, 64, 64);
        lv_obj_set_style_radius(item->thumb, 8, 0); lv_obj_set_style_clip_corner(item->thumb, true, 0);
        lv_obj_set_style_bg_opa(item->thumb, LV_OPA_COVER, 0); set_bg(item->thumb, 0xff0033);
        lv_obj_t *glyph = label(item->thumb, LV_SYMBOL_PLAY, 0xffffff); lv_obj_set_width(glyph, LV_SIZE_CONTENT); lv_obj_center(glyph);
        item->canvas = lv_canvas_create(item->thumb); lv_obj_add_flag(item->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(item->canvas, -25, 0); lv_obj_set_style_transform_scale(item->canvas, 182, 0);
        lv_obj_t *texts = bare(item->button); grow(texts); lv_obj_set_height(texts, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(texts, 3, 0);
        item->title = label(texts, "", TEXT); label_ellipsis(item->title);
        item->meta = label(texts, "", MUTED); label_ellipsis(item->meta);
        lv_obj_add_flag(item->button, LV_OBJ_FLAG_HIDDEN);
    }
    ytm_pager = row(page);
    ytm_prev = grow(button(ytm_pager, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, ytm_page_turn, (void *)(intptr_t)-1));
    ytm_page = label(ytm_pager, "", MUTED); lv_obj_set_width(ytm_page, 90); lv_obj_set_style_text_align(ytm_page, LV_TEXT_ALIGN_CENTER, 0);
    ytm_next = grow(button(ytm_pager, NULL, T_FILES_NEXT, BTN_SECONDARY, ytm_page_turn, (void *)(intptr_t)1));
    lv_obj_add_flag(ytm_pager, LV_OBJ_FLAG_HIDDEN);
    caption(page, T_YTM_NOTES);
}
