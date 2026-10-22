/* Included after shared UI controls and file browser. LVGL task only. */
#include "m3_reader_font.h"
#include "src/misc/lv_text_private.h" /* Installed LVGL 9.2.2: byte-based line wrapping. */
static lv_obj_t *reader_surface, *reader_canvas, *reader_text, *reader_toolbar, *reader_footer;
static lv_obj_t *reader_title, *reader_position, *reader_notice, *reader_options, *reader_seek;
static lv_obj_t *reader_sliders[4], *reader_values[4], *reader_theme, *reader_background, *reader_lines[32];
static m3_reader_settings_t reader_config;
static m3_reader_state_t reader_snapshot;
static M3_PSRAM_BSS char reader_window[M3_READER_WINDOW];
static M3_PSRAM_BSS unsigned reader_history[256];
static unsigned reader_generation, reader_offset, reader_next, reader_history_count;
static bool reader_dirty = true, reader_controls = true, reader_options_open, reader_pressed;
static lv_point_t reader_origin;
static int64_t reader_pressed_at;

static unsigned reader_boundary(const char *text, unsigned index, unsigned size) {
    while (index < size && ((unsigned char)text[index] & 0xc0) == 0x80) ++index;
    return index;
}
/* Gestures use the dominant axis. Edge taps also support vertical navigation. */
static int reader_direction(int dx, int dy, int x, int y, int width, int height, bool drag) {
    int ax = abs(dx), ay = abs(dy);
    if (drag) {
        if (ax < 40 && ay < 40) return 0;
        return ax >= ay ? dx < 0 ? 1 : -1 : dy < 0 ? 1 : -1;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    if (x < width / 4) return -1;
    if (x >= width * 3 / 4) return 1;
    if (y < height / 4) return -1;
    if (y >= height * 3 / 4) return 1;
    return 0;
}
static m3_text_t reader_status_text(m3_reader_status_t status) {
    static const m3_text_t ids[] = {T_READER_HINT, T_READER_LOADING, T_NONE, T_READER_NO_SD,
        T_READER_BUSY, T_READER_IO, T_READER_LARGE, T_READER_ENCODING, T_READER_MEMORY};
    return (unsigned)status < sizeof(ids) / sizeof(ids[0]) ? ids[status] : T_READER_IO;
}
static void reader_skin(void) {
    static const uint32_t paper[] = {0xfaf9f6, 0xf3e8cf, 0x14191f, 0xdae8d8};
    static const uint32_t ink[] = {0x20242a, 0x44382b, 0xd4d7d1, 0x26392c};
    unsigned theme = reader_config.theme;
    lv_obj_set_style_bg_color(reader_surface, lv_color_hex(paper[theme]), 0);
    set_ink(reader_text, ink[theme]); set_ink(reader_title, ink[theme]); set_ink(reader_position, ink[theme]);
    lv_obj_set_style_text_font(reader_text, m3_reader_font(reader_config.font_px), 0);
    lv_obj_set_style_text_letter_space(reader_text, reader_config.letter_space, 0);
    lv_obj_set_style_text_line_space(reader_text, reader_config.line_space, 0);
    lv_obj_set_pos(reader_canvas, reader_config.margin, 62);
    lv_obj_set_size(reader_canvas, M3_WIDTH - 2 * reader_config.margin, 526);
    lv_obj_set_width(reader_text, M3_WIDTH - 2 * reader_config.margin);
    lv_obj_set_height(reader_text, 526);
    const lv_font_t *font = m3_reader_font(reader_config.font_px);
    for (unsigned i = 0; i < 32; ++i) {
        unsigned pitch = reader_config.background == 2 ? font->line_height + reader_config.line_space : 16;
        unsigned y = reader_config.background == 2 ? (i + 1) * pitch - 4 : i * pitch;
        lv_obj_set_pos(reader_lines[i], 0, y);
        lv_obj_set_width(reader_lines[i], M3_WIDTH - 2 * reader_config.margin);
        set_bg(reader_lines[i], ink[theme]);
        lv_obj_set_style_bg_opa(reader_lines[i], reader_config.background == 2 ? LV_OPA_20 : LV_OPA_10, 0);
        power_visible(reader_lines[i], reader_config.background && y < 526);
    }
    lv_obj_invalidate(reader_surface);
}
static void reader_render(void) {
    if (reader_snapshot.status != M3_READER_READY) return;
    size_t length = m3_reader_copy(reader_generation, reader_offset, reader_window, sizeof(reader_window));
    if (!length) return;
    const lv_font_t *font = m3_reader_font(reader_config.font_px);
    unsigned lines = (526u + reader_config.line_space) / (font->line_height + reader_config.line_space);
    unsigned used = 0;
    for (unsigned i = 0; i < lines && used < length; ++i) {
        unsigned next = lv_text_get_next_line(reader_window + used, font, reader_config.letter_space,
            M3_WIDTH - 2 * reader_config.margin, NULL, LV_TEXT_FLAG_NONE);
        if (!next || next > length - used) break;
        used += next;
    }
    if (!used) used = reader_boundary(reader_window, 1, length);
    reader_next = reader_offset + used;
    /* Consumed newlines belong to this page, but do not add an empty clipped row. */
    while (used && reader_window[used - 1] == '\n') --used;
    reader_window[used] = 0;
    lv_label_set_text(reader_text, reader_window);
    unsigned progress = reader_snapshot.bytes ? (unsigned)((uint64_t)reader_offset * 100 / reader_snapshot.bytes) : 0;
    if (reader_next >= reader_snapshot.bytes) progress = 100;
    char text[32]; snprintf(text, sizeof(text), "%u%%", progress); set_text(reader_position, text);
    lv_slider_set_value(reader_seek, progress, LV_ANIM_OFF);
    m3_reader_progress(reader_generation, reader_offset);
    reader_dirty = false;
}
static unsigned reader_previous(void) {
    unsigned begin = reader_offset > M3_READER_WINDOW - 4 ? reader_offset - (M3_READER_WINDOW - 4) : 0;
    begin = m3_reader_align(reader_generation, begin);
    size_t size = m3_reader_copy(reader_generation, begin, reader_window, sizeof(reader_window));
    if (!size || reader_offset <= begin) return 0;
    unsigned end = reader_offset - begin;
    if (end > size) return reader_offset;
    reader_window[end] = 0;
    const lv_font_t *font = m3_reader_font(reader_config.font_px);
    unsigned low = 0, high = end;
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        unsigned boundary = reader_boundary(reader_window, middle, end);
        lv_point_t measured;
        lv_text_get_size(&measured, reader_window + boundary, font, reader_config.letter_space,
            reader_config.line_space, M3_WIDTH - 2 * reader_config.margin, LV_TEXT_FLAG_NONE);
        if (measured.y <= 526) high = middle; else low = middle + 1;
    }
    unsigned result = begin + reader_boundary(reader_window, low, end);
    if (result >= reader_offset) result = m3_reader_align(reader_generation, reader_offset - 1);
    return result;
}
static void reader_turn(int direction) {
    if (reader_snapshot.status != M3_READER_READY || reader_options_open) return;
    if (reader_dirty) reader_render();
    if (direction > 0 && reader_next < reader_snapshot.bytes) {
        if (reader_history_count == 256) {
            memmove(reader_history, reader_history + 1, 255 * sizeof(reader_history[0])); reader_history_count = 255;
        }
        reader_history[reader_history_count++] = reader_offset; reader_offset = reader_next;
    } else if (direction < 0 && reader_offset) {
        reader_offset = reader_history_count ? reader_history[--reader_history_count] : reader_previous();
    } else return;
    reader_dirty = true; reader_render();
}
static void reader_step(lv_event_t *event) { reader_turn((int)(intptr_t)lv_event_get_user_data(event)); }
static void reader_touch(lv_event_t *event) {
    lv_indev_t *input = lv_indev_active(); if (!input || reader_options_open) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) { reader_origin = point; reader_pressed = true; reader_pressed_at = esp_timer_get_time(); }
    else if (code == LV_EVENT_PRESS_LOST) reader_pressed = false;
    else if (code == LV_EVENT_RELEASED && reader_pressed) {
        reader_pressed = false;
        int dx = point.x - reader_origin.x, dy = point.y - reader_origin.y;
        bool drag = abs(dx) >= 40 || abs(dy) >= 40;
        if (!drag && (abs(dx) > 12 || abs(dy) > 12 || esp_timer_get_time() - reader_pressed_at > 600000)) return;
        lv_area_t area; lv_obj_get_coords(reader_canvas, &area);
        int direction = reader_direction(dx, dy, point.x - area.x1, point.y - area.y1,
            lv_obj_get_width(reader_canvas), lv_obj_get_height(reader_canvas), drag);
        if (direction) reader_turn(direction);
        else if (!drag) {
            reader_controls = !reader_controls;
            power_visible(reader_toolbar, reader_controls); power_visible(reader_footer, reader_controls);
        }
    }
}
static void reader_browse(lv_event_t *event) {
    (void)event; selected_sd = true; selected_offset = 0;
    selected_folder[0] = 0;
    segment_select(storage_tabs, 1); browse_refresh(); show_app(APP_FILES);
}
static void reader_start_again(lv_event_t *event) {
    (void)event;
    if (!m3_reader_resume()) set_text(reader_notice, tr(T_READER_HINT));
}
static void reader_seek_changed(lv_event_t *event) {
    (void)event;
    if (reader_snapshot.status != M3_READER_READY) return;
    unsigned progress = lv_slider_get_value(reader_seek);
    unsigned target = (unsigned)((uint64_t)reader_snapshot.bytes * progress / 100);
    if (progress == 100) { reader_offset = reader_snapshot.bytes; reader_offset = reader_previous(); }
    else reader_offset = m3_reader_align(reader_generation, target);
    reader_history_count = 0;
    reader_dirty = true; reader_render();
}
static void reader_first(lv_event_t *event) {
    (void)event; reader_offset = reader_history_count = 0; reader_dirty = true; reader_render();
}
static void reader_configure_ui(lv_event_t *event) {
    (void)event;
    reader_config.font_px = lv_slider_get_value(reader_sliders[0]);
    reader_config.letter_space = lv_slider_get_value(reader_sliders[1]);
    reader_config.line_space = lv_slider_get_value(reader_sliders[2]);
    reader_config.margin = lv_slider_get_value(reader_sliders[3]);
    reader_config.theme = segment_value(reader_theme); reader_config.background = segment_value(reader_background);
    if (!m3_reader_configure(&reader_config)) return;
    for (unsigned i = 0; i < 4; ++i) {
        char value[16]; snprintf(value, sizeof(value), "%ld px", (long)lv_slider_get_value(reader_sliders[i]));
        set_text(reader_values[i], value);
    }
    reader_history_count = 0; reader_dirty = true; reader_skin();
}
static void reader_settings_toggle(lv_event_t *event) {
    (void)event; reader_pressed = false; reader_options_open = !reader_options_open;
    power_visible(reader_options, reader_options_open);
    if (!reader_options_open) { reader_dirty = true; reader_render(); }
}
static void reader_tick(void) {
    if (current_app != APP_READER || !reader_surface) return;
    m3_reader_state(&reader_snapshot);
    bool ready = reader_snapshot.status == M3_READER_READY;
    power_visible(reader_text, ready);
    power_visible(reader_notice, !ready || reader_snapshot.save_failed);
    if (ready) {
        if (reader_generation != reader_snapshot.generation) {
            reader_generation = reader_snapshot.generation; reader_offset = reader_snapshot.resume;
            reader_history_count = 0; reader_dirty = true;
        }
        const char *title = strrchr(reader_snapshot.path, '/');
        set_text(reader_title, title ? title + 1 : reader_snapshot.path);
        if (reader_snapshot.save_failed) set_text(reader_notice, tr(T_READER_SAVE_FAIL));
        if (reader_dirty && !reader_options_open) reader_render();
    } else {
        set_text(reader_title, tr(T_APP_READER));
        set_text(reader_notice, tr(reader_snapshot.available ? reader_status_text(reader_snapshot.status) : T_READER_UNAVAILABLE));
        set_text(reader_position, "--");
    }
}
static void reader_enter(void) {
    m3_reader_state_t state; m3_reader_state(&state);
    if (state.status == M3_READER_IDLE && state.available) (void)m3_reader_resume();
    reader_pressed = false; reader_controls = true;
    power_visible(reader_toolbar, true); power_visible(reader_footer, true); reader_tick();
}
static void reader_leave(void) {
    reader_pressed = false; reader_options_open = false; power_visible(reader_options, false);
    m3_reader_close(); reader_generation = 0; reader_history_count = 0;
}
static void reader_open_path(const char *folder, const char *name) {
    char path[256]; int n = snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (n > 0 && (size_t)n < sizeof(path) && m3_reader_open(path)) show_app(APP_READER);
    else set_text(file_info, tr(T_READER_IO));
}
static void build_reader(lv_obj_t *page) {
    lv_obj_remove_style_all(page); lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    /* Removing all styles also clears app_page's local width/height in LVGL. */
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    reader_surface = bare(page); lv_obj_set_size(reader_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(reader_surface, LV_OPA_COVER, 0);
    reader_canvas = bare(reader_surface); lv_obj_add_flag(reader_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(reader_canvas, reader_touch, LV_EVENT_ALL, NULL);
    for (unsigned i = 0; i < 32; ++i) reader_lines[i] = icon_part(reader_canvas, 0, 0, 440, 1, 0);
    reader_text = label(reader_canvas, "", TEXT); lv_obj_remove_flag(reader_text, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_long_mode(reader_text, LV_LABEL_LONG_CLIP);
    reader_notice = label(reader_surface, tr(T_READER_HINT), MUTED);
    lv_obj_set_pos(reader_notice, 28, 100); lv_obj_set_size(reader_notice, 424, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(reader_notice, LV_TEXT_ALIGN_CENTER, 0);
    reader_toolbar = bare(reader_surface); lv_obj_set_pos(reader_toolbar, 16, 6); lv_obj_set_size(reader_toolbar, 448, 48);
    reader_title = label(reader_toolbar, "", TEXT); lv_obj_set_pos(reader_title, 0, 13); lv_obj_set_width(reader_title, 330);
    lv_label_set_long_mode(reader_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t *open = icon_button(reader_toolbar, LV_SYMBOL_DIRECTORY, reader_browse, NULL);
    lv_obj_set_pos(open, 340, 2); lv_obj_set_size(open, 44, 44);
    lv_obj_t *settings_button = icon_button(reader_toolbar, LV_SYMBOL_SETTINGS, reader_settings_toggle, NULL);
    lv_obj_set_pos(settings_button, 398, 2); lv_obj_set_size(settings_button, 44, 44);
    reader_footer = bare(reader_surface); lv_obj_set_pos(reader_footer, 16, 602); lv_obj_set_size(reader_footer, 448, 48);
    lv_obj_t *prev = icon_button(reader_footer, LV_SYMBOL_LEFT, reader_step, (void *)(intptr_t)-1);
    lv_obj_set_pos(prev, 0, 0); lv_obj_set_size(prev, 44, 44);
    reader_seek = lv_slider_create(reader_footer); style_slider(reader_seek);
    lv_obj_set_pos(reader_seek, 66, 19); lv_obj_set_size(reader_seek, 238, 8); lv_slider_set_range(reader_seek, 0, 100);
    lv_obj_add_event_cb(reader_seek, reader_seek_changed, LV_EVENT_RELEASED, NULL);
    reader_position = label(reader_footer, "--", TEXT); lv_obj_set_pos(reader_position, 322, 13); lv_obj_set_width(reader_position, 68);
    lv_obj_t *next = icon_button(reader_footer, LV_SYMBOL_RIGHT, reader_step, (void *)(intptr_t)1);
    lv_obj_set_pos(next, 398, 0); lv_obj_set_size(next, 44, 44);
    reader_options = app_page(page); lv_obj_add_flag(reader_options, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *box = card(reader_options, T_READER_SETTINGS);
    static const m3_text_t themes[] = {T_READER_DAY, T_READER_SEPIA, T_READER_NIGHT, T_READER_GREEN};
    static const m3_text_t backgrounds[] = {T_READER_PLAIN, T_READER_PAPER, T_READER_RULED};
    m3_reader_settings(&reader_config);
    caption(box, T_READER_THEME); reader_theme = segmented(box, themes, NULL, 4, reader_config.theme, reader_configure_ui);
    caption(box, T_READER_BG); reader_background = segmented(box, backgrounds, NULL, 3, reader_config.background, reader_configure_ui);
    static const m3_text_t ids[] = {T_READER_FONT, T_READER_LETTER, T_READER_LINE, T_READER_MARGIN};
    const unsigned values[] = {reader_config.font_px, reader_config.letter_space, reader_config.line_space, reader_config.margin};
    static const unsigned low[] = {16, 0, 0, 12}, high[] = {36, 6, 24, 40};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *line = row(box); grow(text(line, ids[i], TEXT));
        reader_values[i] = label(line, "", CYAN); lv_obj_set_width(reader_values[i], 76);
        reader_sliders[i] = lv_slider_create(box); style_slider(reader_sliders[i]);
        lv_obj_set_width(reader_sliders[i], LV_PCT(100)); lv_slider_set_range(reader_sliders[i], low[i], high[i]);
        lv_slider_set_value(reader_sliders[i], values[i], LV_ANIM_OFF);
        lv_obj_add_event_cb(reader_sliders[i], reader_configure_ui, LV_EVENT_VALUE_CHANGED, NULL);
    }
    button(box, LV_SYMBOL_OK, T_READER_DONE, BTN_PRIMARY, reader_settings_toggle, NULL);
    box = card(reader_options, T_READER_OPEN);
    button(box, LV_SYMBOL_DIRECTORY, T_READER_OPEN, BTN_SECONDARY, reader_browse, NULL);
    button(box, LV_SYMBOL_PLAY, T_READER_CONTINUE, BTN_SECONDARY, reader_start_again, NULL);
    button(box, LV_SYMBOL_LEFT, T_READER_FIRST, BTN_SECONDARY, reader_first, NULL);
    caption(box, T_READER_HINT);
    reader_configure_ui(NULL); reader_skin();
}
