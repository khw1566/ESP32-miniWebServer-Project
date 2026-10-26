#include "m3_music.h"
#include "m3_reader.h"
#include "m3_reader_font.h"
#include "m3_sd_apps.h"
#include "m3_photos.h"
#include "m3_calendar.h"
#include "m3_kana.h"
#include "m3_utf8.h"
#include "m3_memory.h"
#include "m3_ui.h"
#include "m3_board.h"
#include "m3_runtime.h"
#include "m3_web_address.h"
#include "m3_audio.h"
#include "m3_2048.h"
#include "m3_crossing.h"
#include "m3_recorder.h"
#include "m3_calculator.h"
#include "m3_gallery.h"
#include "m3_preferences.h"
#include "m3_feedback.h"
#include "m3_screen_wake.h"
#include "m3_hangul.h"
#include "m3_voice.h"
#include "m3_agent.h"
#include "m3_i18n.h"
#include "m3_tube.h"
#include "m3_flash_usage_view.h"
#include "m3_theme.h"
#include "m3_wallpaper.h"
#include "m3_weather.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_random.h"
#include "lvgl.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>

/* Palette. The theme style (m3_theme.c) sets the neutral roles and the accents CYAN/MAGENTA;
 * ON_WALL inks follow the home wallpaper. They are variables, so static tables cannot use them. */
static uint32_t theme_primary = M3_THEME_PRIMARY, theme_accent = M3_THEME_ACCENT, theme_on_accent = 0x00272b;
static uint32_t theme_on_wall = 0xf7f9fc, theme_on_wall_muted = 0xbfcad8;
static m3_palette_t ui_palette = {0x0a0e14, 0x151b25, 0x202938, 0x2b3647, 0xe9eef5, 0x8c99ab, 0x0e131b,
    0x0d121a, 0x2a3445, 0x18202c, 0xc3cede, 0x3a1d27, 0xff9aa8, 0x111824, 0x1a1426, 0xfffffd};
#define CYAN theme_primary
#define MAGENTA theme_accent
#define INK ui_palette.ink
#define SURFACE ui_palette.surface
#define SURFACE_HI ui_palette.surface_hi
#define LINE ui_palette.line
#define TEXT ui_palette.text
#define MUTED ui_palette.muted
#define KEY_BG ui_palette.key_bg
#define KEY_ITEM ui_palette.key_item
#define KEY_CHECKED ui_palette.key_checked
#define KEY_TEXT ui_palette.key_text
#define DANGER_BG ui_palette.danger_bg
#define DANGER_TEXT ui_palette.danger_text
#define SHADE_TOP ui_palette.shade_top
#define SHADE_BOTTOM ui_palette.shade_bottom
#define GLASS ui_palette.glass
#define ON_WALL theme_on_wall
#define ON_WALL_MUTED theme_on_wall_muted
#define ON_ACCENT theme_on_accent
#define GREEN 0x34d399
#define AMBER 0xfbbf24
#define RED 0xf87171
#define FIELD ui_palette.field
extern const lv_font_t m3_font_korean, m3_font_title, m3_font_clock;
#define FONT_TITLE (&m3_font_title)
#define FONT_ICON (&lv_font_montserrat_28)

typedef enum { APP_HOME, APP_WIFI, APP_FILES, APP_RECORDER, APP_MONITOR, APP_SYSTEM, APP_RADIO, APP_GAMES, APP_TTS, APP_AGENT, APP_TUBE, APP_MUSIC, APP_GALLERY, APP_CALCULATOR, APP_YTMUSIC, APP_READER, APP_WEATHER, APP_COUNT } app_id_t;
_Static_assert(APP_AGENT == 9, "Agent app dispatch IDs must match the UI enum");
_Static_assert(M3_CUE_APP_READER - M3_CUE_APP_HOME + 1 == APP_WEATHER, "Existing spoken app IDs must match navigation");
enum { CONTENT_TOP = 100, HOME_TOP = 756, KEYBOARD_HEIGHT = 270 };
static const m3_text_t app_names[] = {T_APP_HOME, T_APP_WIFI, T_APP_FILES, T_APP_RECORDER, T_APP_MONITOR,
    T_APP_SETTINGS, T_APP_RADIO, T_APP_GAMES, T_APP_TTS, T_APP_AGENT, T_APP_TUBE, T_APP_MUSIC, T_APP_GALLERY, T_APP_CALCULATOR, T_APP_YTMUSIC, T_APP_READER, T_APP_WEATHER};

static lv_obj_t *pages[APP_COUNT], *viewport, *app_title, *back_button, *keyboard;
static lv_obj_t *home_strip, *home_pill, *home_hint;
static int32_t home_progress;
static app_id_t current_app;
static bool form_dirty, swipe_started;
static lv_point_t swipe_origin;
static M3_PSRAM_BSS m3_runtime_state_t snapshot;

/* Status bar, home and activity shade. */
static lv_obj_t *status, *status_clock, *status_audio, *status_strip, *wifi_levels[4];
static lv_obj_t *power_plug, *power_battery, *power_fill, *power_charge, *power_percent;
static lv_obj_t *home_clock, *home_date, *home_net_icon, *home_net, *home_detail;
static lv_obj_t *home_weather;
static void update_weather(void);
enum { HOME_PRIMARY_APPS = 6, HOME_APPS_PER_PAGE = 9,
    HOME_APP_PAGE_COUNT = 1 + (APP_COUNT - 1 - HOME_PRIMARY_APPS + HOME_APPS_PER_PAGE - 1) / HOME_APPS_PER_PAGE,
    HOME_PHOTOS = HOME_APP_PAGE_COUNT + 1, HOME_PAGE_COUNT = HOME_PHOTOS + 1 };
static lv_obj_t *home_tiles, *home_sheets[HOME_PAGE_COUNT], *home_dots[HOME_PAGE_COUNT], *home_page_name;
static unsigned home_index = 1;
static lv_obj_t *calendar_heading, *calendar_weekdays[7], *calendar_cells[42], *calendar_hint;
static unsigned calendar_year, calendar_month, calendar_stamp, calendar_today_stamp;
static bool calendar_follow = true;
static lv_obj_t *photo_canvas, *photo_name, *photo_info, *photo_pause, *photo_empty;
static const uint8_t *photo_frame;
static M3_PSRAM_BSS m3_photo_state_t photo_snapshot;
static void home_page_update(void);
static void update_calendar(void);
static void update_photos(void);
static lv_obj_t *shade, *shade_scrim, *usage_arcs[3], *usage_values[3], *usage_details[3], *job_cards[6], *job_labels[6];
static lv_obj_t *job_icons[6], *cpu_chart, *cpu_values[2], *job_stops[6];
static void service_stop(lv_event_t *event);
static lv_chart_series_t *cpu_series[2];
static unsigned seen_cpu;
static int32_t shade_y = -HOME_TOP, shade_start_y;
static bool shade_drag;
static lv_point_t shade_origin;
static void shade_set(bool open);

/* Apps. */
static lv_obj_t *net_title, *net_dot, *network, *qr, *qr_url;
static lv_obj_t *ssid_field, *pass_field, *ip_field, *gateway_field, *mask_field, *dns_field;
static lv_obj_t *static_switch, *static_fields, *wifi_info, *wifi_actions[3], *scan_buttons[M3_WIFI_RESULTS];
static lv_obj_t *wifi_saved_count, *wifi_saved_rows[M3_WIFI_PROFILE_LIMIT], *wifi_saved_names[M3_WIFI_PROFILE_LIMIT];
static lv_obj_t *wifi_saved_connect[M3_WIFI_PROFILE_LIMIT], *wifi_saved_remove[M3_WIFI_PROFILE_LIMIT];
static unsigned seen_scan, seen_settings, seen_wifi;
static bool wifi_open_selected;
static char wifi_selected_ssid[33];
static lv_obj_t *storage_tabs, *file_path, *file_info, *file_bar, *file_page, *file_buttons[STORAGE_VIEW_ROWS], *file_empty;
static unsigned seen_files;
static bool selected_sd;
static char selected_folder[256];
static unsigned selected_offset;
enum { VOLUME_ROWS = 8 };
static lv_obj_t *recorder_info, *recorder_meter, *recorder_time, *recorder_path, *recorder_actions[4];
static lv_obj_t *volume_sliders[VOLUME_ROWS], *volume_values[VOLUME_ROWS];
static unsigned shown_volume = 1000;
static lv_obj_t *memory, *heap_bar, *chart, *monitor_info, *system_info;
static lv_chart_series_t *heap_series, *psram_series;
static unsigned chart_tick;
static char seen_url[96], seen_audio[96];
static lv_obj_t *radio_url, *radio_codec, *radio_now, *radio_info, *radio_bar, *station_buttons[24], *radio_region, *radio_transport;
static int radio_selected = -1;
static bool radio_pending;
static int64_t radio_pending_until;
static lv_obj_t *tts_provider, *tts_text, *tts_speed, *tts_speed_value, *tts_info;
static lv_obj_t *agent_prompt, *agent_model, *agent_info, *agent_dot, *agent_answer, *agent_output, *agent_trace;
static unsigned seen_agent;
static lv_obj_t *voice_provider, *voice_panel, *voice_info, *voice_text, *voice_target, *voice_key, *voice_key_info, *voice_dot;
static unsigned seen_voice;
static lv_obj_t *language_tabs;
static lv_obj_t *sound_switches[3], *sound_sliders[3], *sound_values[3];
static lv_obj_t *sound_card, *accessibility_panel, *guidance_mode;
static lv_obj_t *agent_thinking, *agent_voice, *voice_think, *tts_wait, *tts_talk, *tube_wait;
enum { METER_BARS = 11, METER_MIN = 4, METER_MAX = 44 };
static lv_obj_t *voice_meter, *voice_meter_bars[METER_BARS], *voice_db;
static unsigned meter_phase;
static lv_obj_t *web_tabs, *web_qr, *web_url, *web_state, *web_dot, *web_requests;
static unsigned web_address_kind = M3_WEB_DOMAIN;
static bool web_qr_dirty = true, web_qr_ok;
static lv_obj_t *web_frame, *web_hint, *web_retry;
static lv_obj_t *web_controls[4], *web_control_info;
static char seen_web_url[96];
static void voice_input(lv_event_t *event);
static void apply_language(void);
static void update_metrics(lv_timer_t *timer);
static void tube_leave(void);
static void reader_leave(void);
static void reader_enter(void);
static void reader_tick(void);
static void power_visible(lv_obj_t *obj, bool visible);
static void reader_open_path(const char *folder, const char *name);
static void sd_apps_close(lv_event_t *event);
static void sd_apps_tick(lv_timer_t *timer);
static lv_obj_t *builtin_games, *sd_launcher, *sd_panel, *sd_list, *sd_stage, *sd_canvas, *sd_status;
static lv_obj_t *sd_rows[M3_SD_APP_COUNT], *sd_faces[M3_SD_APP_COUNT];
static M3_PSRAM_BSS m3_sd_apps_state_t sd_snapshot;
static uint16_t *sd_pixels;
static lv_obj_t *sd_frame_holder;
static lv_obj_t *sd_restart;
static bool sd_launch_failed;

#include "m3_radio_stations.inc"
static lv_obj_t *game_board, *game_tiles[16], *game_info, *game_arrows[4], *game_tabs, *game_undo;
static lv_obj_t *cross_scene, *cross_lanes[M3_CROSS_ROWS], *cross_objects[M3_CROSS_ROWS][3];
static lv_obj_t *cross_windows[M3_CROSS_ROWS][3], *cross_player, *cross_body;
static m3_crossing_t crossing;
static bool crossing_game;
static uint32_t crossing_best;
static lv_obj_t *score_titles[2], *score_values[2];
static m3_2048_t game, undo_game;
static bool undo_available, memory_game, memory_visible[16], memory_solved[16];
static uint8_t memory_cards[16];
static int memory_first = -1, memory_second = -1;
static unsigned memory_turns, best_score;
static uint64_t memory_hide_at;
static lv_point_t game_origin;
static bool game_drag;

/* ---------- Shared styles: one copy in static memory instead of per-object local styles. ---------- */
static lv_style_t st_page, st_card, st_row, st_btn, st_btn_pressed, st_primary, st_primary_pressed, st_danger,
    st_disabled, st_field, st_field_focus, st_seg, st_seg_item, st_seg_checked, st_list, st_list_checked,
    st_caption, st_track, st_indicator, st_knob, st_title, st_bare;
static m3_preferences_t ui_preferences = {1, 100, 30, 1, 0, 40, 40, 0};
static lv_obj_t *brightness_slider, *fps_slider, *brightness_value, *fps_value;
static m3_typography_t ui_typography = {1, 0, 16, 0};
static lv_obj_t *font_family_tabs, *font_size_slider, *font_size_value, *font_preview;
static unsigned applied_font_family = 255, applied_font_pixels = 255;
static void apply_typography(void);
static lv_obj_t *mute_switch, *animation_switch, *focus_switch, *settings_info;
static lv_obj_t *wake_tabs, *screen_shield, *restart_confirm, *restart_info;
static lv_obj_t *shade_note;
static lv_indev_t *screen_input;
static lv_timer_t *screen_timer;
static m3_screen_wake_t screen_wake;
static bool screen_blanked, screen_waking;
static uint32_t screen_release_at;
static unsigned applied_brightness = 255, applied_fps = 255, applied_animations = 255;
static void update_preferences(void);
static void theme_wallpaper_sync(void);
static uint32_t ui_duration(uint32_t ms) { return ui_preferences.animations ? ms : 0; }
static lv_anim_enable_t ui_animation(void) { return ui_preferences.animations ? LV_ANIM_ON : LV_ANIM_OFF; }
static void styles_init(void) {
    lv_style_init(&st_bare);
    lv_style_set_bg_opa(&st_bare, LV_OPA_TRANSP); lv_style_set_border_width(&st_bare, 0);
    lv_style_set_pad_all(&st_bare, 0); lv_style_set_radius(&st_bare, 0);

    lv_style_init(&st_page);
    lv_style_set_bg_color(&st_page, lv_color_hex(INK)); lv_style_set_bg_opa(&st_page, LV_OPA_COVER);
    lv_style_set_text_color(&st_page, lv_color_hex(TEXT));
    lv_style_set_pad_hor(&st_page, 16); lv_style_set_pad_top(&st_page, 6); lv_style_set_pad_bottom(&st_page, 28);
    lv_style_set_pad_row(&st_page, 14); lv_style_set_border_width(&st_page, 0); lv_style_set_radius(&st_page, 0);
    lv_style_set_layout(&st_page, LV_LAYOUT_FLEX); lv_style_set_flex_flow(&st_page, LV_FLEX_FLOW_COLUMN);

    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(SURFACE)); lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_radius(&st_card, 18); lv_style_set_pad_all(&st_card, 16); lv_style_set_pad_row(&st_card, 12);
    lv_style_set_border_width(&st_card, 0); lv_style_set_width(&st_card, LV_PCT(100));
    lv_style_set_height(&st_card, LV_SIZE_CONTENT);
    lv_style_set_layout(&st_card, LV_LAYOUT_FLEX); lv_style_set_flex_flow(&st_card, LV_FLEX_FLOW_COLUMN);

    lv_style_init(&st_row);
    lv_style_set_bg_opa(&st_row, LV_OPA_TRANSP); lv_style_set_border_width(&st_row, 0); lv_style_set_pad_all(&st_row, 0);
    lv_style_set_pad_column(&st_row, 10); lv_style_set_width(&st_row, LV_PCT(100)); lv_style_set_height(&st_row, LV_SIZE_CONTENT);
    lv_style_set_layout(&st_row, LV_LAYOUT_FLEX); lv_style_set_flex_flow(&st_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&st_row, LV_FLEX_ALIGN_CENTER); lv_style_set_flex_track_place(&st_row, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, lv_color_hex(SURFACE_HI)); lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_text_color(&st_btn, lv_color_hex(TEXT)); lv_style_set_radius(&st_btn, 14);
    lv_style_set_height(&st_btn, 52); lv_style_set_width(&st_btn, LV_PCT(100));
    lv_style_set_pad_hor(&st_btn, 12); lv_style_set_border_width(&st_btn, 0); lv_style_set_shadow_width(&st_btn, 0);
    lv_style_init(&st_btn_pressed);
    lv_style_set_bg_color(&st_btn_pressed, lv_color_hex(LINE));
    lv_style_init(&st_primary);
    lv_style_set_bg_color(&st_primary, lv_color_hex(CYAN)); lv_style_set_text_color(&st_primary, lv_color_hex(ON_ACCENT));
    lv_style_init(&st_primary_pressed);
    lv_style_set_bg_color(&st_primary_pressed, lv_color_hex(0x00b4bd));
    lv_style_init(&st_danger);
    lv_style_set_bg_color(&st_danger, lv_color_hex(DANGER_BG)); lv_style_set_text_color(&st_danger, lv_color_hex(DANGER_TEXT));
    lv_style_init(&st_disabled);
    lv_style_set_opa(&st_disabled, LV_OPA_40);

    lv_style_init(&st_field);
    lv_style_set_bg_color(&st_field, lv_color_hex(FIELD)); lv_style_set_bg_opa(&st_field, LV_OPA_COVER);
    lv_style_set_border_color(&st_field, lv_color_hex(LINE)); lv_style_set_border_width(&st_field, 1);
    lv_style_set_radius(&st_field, 12); lv_style_set_pad_all(&st_field, 13); lv_style_set_text_color(&st_field, lv_color_hex(TEXT));
    lv_style_init(&st_field_focus);
    lv_style_set_border_color(&st_field_focus, lv_color_hex(CYAN)); lv_style_set_border_width(&st_field_focus, 2);

    lv_style_init(&st_seg);
    lv_style_set_bg_color(&st_seg, lv_color_hex(FIELD)); lv_style_set_bg_opa(&st_seg, LV_OPA_COVER);
    lv_style_set_radius(&st_seg, 14); lv_style_set_pad_all(&st_seg, 4); lv_style_set_pad_gap(&st_seg, 4);
    lv_style_set_border_width(&st_seg, 0); lv_style_set_height(&st_seg, 52); lv_style_set_width(&st_seg, LV_PCT(100));
    lv_style_init(&st_seg_item);
    lv_style_set_bg_opa(&st_seg_item, LV_OPA_TRANSP); lv_style_set_text_color(&st_seg_item, lv_color_hex(MUTED));
    lv_style_set_radius(&st_seg_item, 10); lv_style_set_border_width(&st_seg_item, 0); lv_style_set_shadow_width(&st_seg_item, 0);
    lv_style_init(&st_seg_checked);
    lv_style_set_bg_opa(&st_seg_checked, LV_OPA_COVER); lv_style_set_bg_color(&st_seg_checked, lv_color_hex(SURFACE_HI));
    lv_style_set_text_color(&st_seg_checked, lv_color_hex(CYAN));

    lv_style_init(&st_list);
    lv_style_set_bg_color(&st_list, lv_color_hex(SURFACE_HI)); lv_style_set_bg_opa(&st_list, LV_OPA_COVER);
    lv_style_set_radius(&st_list, 12); lv_style_set_height(&st_list, 56); lv_style_set_width(&st_list, LV_PCT(100));
    lv_style_set_pad_hor(&st_list, 14); lv_style_set_pad_column(&st_list, 12); lv_style_set_border_width(&st_list, 0);
    lv_style_set_shadow_width(&st_list, 0); lv_style_set_text_color(&st_list, lv_color_hex(TEXT));
    lv_style_set_layout(&st_list, LV_LAYOUT_FLEX); lv_style_set_flex_flow(&st_list, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&st_list, LV_FLEX_ALIGN_CENTER); lv_style_set_flex_track_place(&st_list, LV_FLEX_ALIGN_CENTER);
    lv_style_init(&st_list_checked);
    lv_style_set_border_color(&st_list_checked, lv_color_hex(CYAN)); lv_style_set_border_width(&st_list_checked, 2);
    lv_style_set_bg_color(&st_list_checked, lv_color_hex(0x15303a));

    lv_style_init(&st_caption);
    lv_style_set_text_color(&st_caption, lv_color_hex(MUTED)); lv_style_set_text_line_space(&st_caption, 3);
    lv_style_init(&st_title);
    lv_style_set_text_font(&st_title, m3_system_font(ui_typography.pixels + 4, ui_typography.family));
    lv_style_set_text_color(&st_title, lv_color_hex(TEXT));

    lv_style_init(&st_track);
    lv_style_set_bg_color(&st_track, lv_color_hex(SURFACE_HI)); lv_style_set_bg_opa(&st_track, LV_OPA_COVER);
    lv_style_set_radius(&st_track, LV_RADIUS_CIRCLE); lv_style_set_border_width(&st_track, 0);
    lv_style_init(&st_indicator);
    lv_style_set_bg_color(&st_indicator, lv_color_hex(CYAN)); lv_style_set_bg_opa(&st_indicator, LV_OPA_COVER);
    lv_style_set_radius(&st_indicator, LV_RADIUS_CIRCLE);
    lv_style_init(&st_knob);
    lv_style_set_bg_color(&st_knob, lv_color_hex(0xffffff)); lv_style_set_bg_opa(&st_knob, LV_OPA_COVER);
    lv_style_set_pad_all(&st_knob, 7); lv_style_set_radius(&st_knob, LV_RADIUS_CIRCLE);
    lv_style_set_shadow_width(&st_knob, 10); lv_style_set_shadow_opa(&st_knob, LV_OPA_40);
}

/* ---------- Language binding: every static string remembers its text ID. ---------- */
typedef enum { BIND_TEXT, BIND_PLACEHOLDER } bind_kind_t;
typedef struct { lv_obj_t *obj; const char *prefix; uint16_t id; uint8_t kind; } binding_t;
static M3_PSRAM_BSS binding_t bindings[320];
static unsigned binding_count;
static void bind_apply(unsigned index) {
    /* Help texts exceed any small buffer in Korean (3 bytes per syllable); only
     * icon-prefixed button captions, which are short, are composed here. */
    char composed[128];
    const char *value = tr((m3_text_t)bindings[index].id), *text = value;
    if (bindings[index].prefix) {
        snprintf(composed, sizeof(composed), "%s%s%s", bindings[index].prefix, value[0] ? "  " : "", value);
        text = composed;
    }
    if (bindings[index].kind == BIND_PLACEHOLDER) lv_textarea_set_placeholder_text(bindings[index].obj, text);
    else lv_label_set_text(bindings[index].obj, text);
}
static void bind_text(lv_obj_t *obj, m3_text_t id, const char *prefix, bind_kind_t kind) {
    /* Icon-only labels never change with the language; some (password eye) are swapped at runtime. */
    if (id == T_NONE) { if (kind == BIND_TEXT) lv_label_set_text(obj, prefix ? prefix : ""); return; }
    if (binding_count < sizeof(bindings) / sizeof(bindings[0])) {
        bindings[binding_count] = (binding_t){obj, prefix, (uint16_t)id, (uint8_t)kind};
        bind_apply(binding_count++);
    } else if (kind == BIND_TEXT) lv_label_set_text(obj, tr(id));
}
/* Style setters invalidate even when the value is unchanged; periodic updates go through these. */
static void set_bg(lv_obj_t *obj, uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), lv_color_hex(color))) lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}
static void set_ink(lv_obj_t *obj, uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, 0), lv_color_hex(color))) lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}
/* Avoid redrawing a label every metrics tick when its text did not change. */
static void set_text(lv_obj_t *obj, const char *text) {
    if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}
static const char *status_text(const char *english) {
    static M3_PSRAM_BSS char buffers[4][256]; static unsigned next;
    char *buffer = buffers[next++ % 4];
    return m3_i18n_status(english, buffer, sizeof(buffers[0]));
}

/* ---------- Small widget helpers. ---------- */
static void label_ellipsis(lv_obj_t *obj) {
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    /* DOT needs a bounded line: content height would simply grow and wrap. */
    lv_obj_set_height(obj, lv_obj_get_style_text_font(obj, LV_PART_MAIN)->line_height);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
static lv_obj_t *text(lv_obj_t *parent, m3_text_t id, uint32_t color) {
    lv_obj_t *obj = label(parent, "", color); bind_text(obj, id, NULL, BIND_TEXT); return obj;
}
static lv_obj_t *caption(lv_obj_t *parent, m3_text_t id) {
    lv_obj_t *obj = lv_label_create(parent); lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_add_style(obj, &st_caption, 0); bind_text(obj, id, NULL, BIND_TEXT); return obj;
}
static lv_obj_t *bare(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj); lv_obj_add_style(obj, &st_bare, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
static lv_obj_t *row(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj); lv_obj_add_style(obj, &st_row, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
static lv_obj_t *card(lv_obj_t *parent, m3_text_t title) {
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj); lv_obj_add_style(obj, &st_card, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (title != T_NONE) {
        lv_obj_t *heading = caption(obj, title);
        lv_obj_set_style_text_color(heading, lv_color_hex(CYAN), 0);
    }
    return obj;
}
static void sound_tap(lv_event_t *event) { (void)event; m3_feedback_emit(M3_CUE_TAP, m3_i18n_language()); }
typedef enum { BTN_SECONDARY, BTN_PRIMARY, BTN_DANGER } button_kind_t;
static lv_obj_t *button(lv_obj_t *parent, const char *symbol, m3_text_t id, button_kind_t kind, lv_event_cb_t callback, void *data) {
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &st_btn, 0); lv_obj_add_style(obj, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &st_disabled, LV_STATE_DISABLED);
    if (kind == BTN_PRIMARY) {
        lv_obj_add_style(obj, &st_primary, 0); lv_obj_add_style(obj, &st_primary_pressed, LV_STATE_PRESSED);
    } else if (kind == BTN_DANGER) lv_obj_add_style(obj, &st_danger, 0);
    lv_obj_t *text_obj = lv_label_create(obj); lv_obj_set_width(text_obj, LV_PCT(100));
    label_ellipsis(text_obj);
    lv_obj_set_style_text_align(text_obj, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(text_obj);
    bind_text(text_obj, id, symbol, BIND_TEXT);
    lv_obj_add_event_cb(obj, sound_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    return obj;
}
static lv_obj_t *grow(lv_obj_t *obj) { lv_obj_set_flex_grow(obj, 1); lv_obj_set_width(obj, 0); return obj; }
static lv_obj_t *icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t callback, void *data) {
    lv_obj_t *obj = button(parent, symbol, T_NONE, BTN_SECONDARY, callback, data);
    lv_obj_set_size(obj, 52, 52); lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}
static lv_obj_t *refresh_button(lv_obj_t *parent,m3_text_t action,lv_event_cb_t callback,void *data) {
    lv_obj_t *obj=icon_button(parent,LV_SYMBOL_REFRESH,callback,data);
    /* Keep each app's action description separate from its icon-only face. */
    lv_obj_set_user_data(obj,(void *)(uintptr_t)action);
    return obj;
}
static void style_slider(lv_obj_t *slider) {
    lv_obj_remove_style_all(slider);
    lv_obj_add_style(slider, &st_track, LV_PART_MAIN); lv_obj_add_style(slider, &st_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &st_knob, LV_PART_KNOB);
    lv_obj_set_height(slider, 8);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_ext_click_area(slider, 18);
}
static lv_obj_t *bar(lv_obj_t *parent, uint32_t color, int32_t height) {
    lv_obj_t *obj = lv_bar_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &st_track, LV_PART_MAIN); lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(obj, LV_PCT(100), height);
    if (color != CYAN) lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
    return obj;
}
/* Dot that shows a state color (online, recording, busy). */
static lv_obj_t *dot(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *obj = bare(parent); lv_obj_set_size(obj, 10, 10);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}


/* ---------- Waiting animations ----------
 * Dots: work in progress with no level (AI reply, recognition, voice download).
 * Bars: sound in progress (recording, speaking). Animations run only while shown. */
typedef enum { ACTIVITY_DOTS = 1, ACTIVITY_BARS } activity_kind_t;
static void dot_bounce(void *obj, int32_t v) {
    lv_obj_set_style_translate_y(obj, -v, 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)(110 + v * 18), 0);
}
static void bar_level(void *obj, int32_t v) { lv_obj_set_height(obj, v); }
static lv_obj_t *activity_create(lv_obj_t *parent, activity_kind_t kind, uint32_t color, m3_text_t caption_id) {
    lv_obj_t *box = row(parent);
    lv_obj_set_width(box, LV_SIZE_CONTENT); lv_obj_set_style_pad_column(box, 10, 0);
    lv_obj_set_user_data(box, (void *)(uintptr_t)kind);
    lv_obj_t *indicator = row(box);
    lv_obj_set_size(indicator, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_pad_column(indicator, kind == ACTIVITY_DOTS ? 6 : 4, 0);
    lv_obj_set_style_flex_cross_place(indicator, kind == ACTIVITY_DOTS ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_END, 0);
    for (unsigned i = 0; i < (kind == ACTIVITY_DOTS ? 3u : 5u); ++i) {
        lv_obj_t *part = bare(indicator);
        lv_obj_set_size(part, kind == ACTIVITY_DOTS ? 9 : 5, kind == ACTIVITY_DOTS ? 9 : 6);
        lv_obj_set_style_radius(part, kind == ACTIVITY_DOTS ? LV_RADIUS_CIRCLE : 3, 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
    }
    if (caption_id != T_NONE) { lv_obj_t *text_obj = caption(box, caption_id); lv_obj_set_width(text_obj, LV_SIZE_CONTENT); }
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    return box;
}
static void activity_set(lv_obj_t *box, bool on) {
    if (!box || on != lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN)) return;
    activity_kind_t kind = (activity_kind_t)(uintptr_t)lv_obj_get_user_data(box);
    lv_obj_t *indicator = lv_obj_get_child(box, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(indicator); ++i) {
        lv_obj_t *part = lv_obj_get_child(indicator, (int32_t)i);
        lv_anim_delete(part, NULL);
        if (!on) { if (kind == ACTIVITY_DOTS) dot_bounce(part, 0); else bar_level(part, 6); continue; }
        if (!ui_preferences.animations) { if (kind == ACTIVITY_DOTS) dot_bounce(part, 0); else bar_level(part, 12); continue; }
        lv_anim_t animation; lv_anim_init(&animation);
        lv_anim_set_var(&animation, part);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        if (kind == ACTIVITY_DOTS) {
            /* A wave: each dot starts 140 ms after the previous one and they stay in step. */
            lv_anim_set_exec_cb(&animation, dot_bounce); lv_anim_set_values(&animation, 0, 7);
            lv_anim_set_duration(&animation, ui_duration(280)); lv_anim_set_playback_duration(&animation, 280);
            lv_anim_set_delay(&animation, i * 140); lv_anim_set_repeat_delay(&animation, 280);
        } else {
            /* Bars at unrelated periods read as a live level meter. */
            static const uint16_t periods[] = {230, 310, 270, 350, 290};
            static const uint8_t peaks[] = {18, 26, 22, 26, 16};
            lv_anim_set_exec_cb(&animation, bar_level); lv_anim_set_values(&animation, 6, peaks[i % 5]);
            lv_anim_set_duration(&animation, ui_duration(periods[i % 5])); lv_anim_set_playback_duration(&animation, periods[i % 5]);
        }
        lv_anim_start(&animation);
    }
    if (on) lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
}
/* Speech phase from the audio worker's status: 1 preparing (download or engine start), 2 speaking. */
static unsigned tts_phase(const m3_audio_state_t *audio) {
    static const char *const preparing[] = {"Korean TTS initializing", "Google Korean TTS / downloading", "OpenAI TTS / downloading", "Azure TTS / preparing"};
    static const char *const speaking[] = {"Speaking Korean", "Google Korean TTS / speaking", "OpenAI TTS / AI-generated", "Azure TTS / speaking"};
    if (!audio->playing) return 0;
    for (unsigned i = 0; i < sizeof(preparing) / sizeof(preparing[0]); ++i) {
        if (!strncmp(audio->message, preparing[i], strlen(preparing[i]))) return 1;
        if (!strncmp(audio->message, speaking[i], strlen(speaking[i]))) return 2;
    }
    return 0;
}

/* Segmented control on a button matrix: translated labels, exactly one choice. */
typedef struct { lv_obj_t *obj; const char *const *fixed; const char *map[5]; m3_text_t ids[4]; uint8_t count, selected; } segment_t;
static segment_t segments[28];
static unsigned segment_count;
static segment_t *segment_of(lv_obj_t *obj) {
    for (unsigned i = 0; i < segment_count; ++i) if (segments[i].obj == obj) return &segments[i];
    return NULL;
}
static void segment_refresh(segment_t *segment) {
    /* Product names (models, codecs) use a fixed map that is identical in both languages. */
    for (unsigned i = 0; i < segment->count; ++i) segment->map[i] = segment->fixed ? segment->fixed[i] : tr(segment->ids[i]);
    segment->map[segment->count] = "";
    lv_buttonmatrix_set_map(segment->obj, segment->map);
    lv_buttonmatrix_set_button_ctrl_all(segment->obj, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(segment->obj, segment->selected, LV_BUTTONMATRIX_CTRL_CHECKED);
}
static void segment_select(lv_obj_t *obj, unsigned index) {
    segment_t *segment = segment_of(obj);
    if (!segment || index >= segment->count) return;
    lv_buttonmatrix_clear_button_ctrl_all(obj, LV_BUTTONMATRIX_CTRL_CHECKED);
    segment->selected = (uint8_t)index; lv_buttonmatrix_set_button_ctrl(obj, index, LV_BUTTONMATRIX_CTRL_CHECKED);
}
static unsigned segment_value(lv_obj_t *obj) {
    segment_t *segment = segment_of(obj);
    if (!segment) return 0;
    uint32_t pressed = lv_buttonmatrix_get_selected_button(obj);
    if (pressed != LV_BUTTONMATRIX_BUTTON_NONE && pressed < segment->count) segment->selected = (uint8_t)pressed;
    return segment->selected;
}
static lv_obj_t *segmented(lv_obj_t *parent, const m3_text_t *ids, const char *const *fixed, unsigned count, unsigned selected, lv_event_cb_t callback) {
    lv_obj_t *obj = lv_buttonmatrix_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &st_seg, 0);
    lv_obj_add_style(obj, &st_seg_item, LV_PART_ITEMS);
    lv_obj_add_style(obj, &st_seg_checked, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_buttonmatrix_set_one_checked(obj, true);
    if (segment_count < sizeof(segments) / sizeof(segments[0]) && count <= 4) {
        segment_t *segment = &segments[segment_count++];
        segment->obj = obj; segment->fixed = fixed; segment->count = (uint8_t)count; segment->selected = (uint8_t)selected;
        for (unsigned i = 0; i < count; ++i) segment->ids[i] = ids ? ids[i] : T_NONE;
        segment_refresh(segment);
    }
    if (callback) lv_obj_add_event_cb(obj, callback, LV_EVENT_VALUE_CHANGED, NULL);
    return obj;
}
static void volume_changed(lv_event_t *event);
static lv_obj_t *volume_row(lv_obj_t *parent, unsigned index, unsigned volume) {
    lv_obj_t *line = row(parent);
    lv_obj_t *low = label(line, LV_SYMBOL_MUTE, MUTED); lv_obj_set_width(low, LV_SIZE_CONTENT);
    lv_obj_t *slider = lv_slider_create(line); style_slider(slider); grow(slider);
    lv_slider_set_range(slider, 0, 150); lv_slider_set_value(slider, volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *value = label(line, "", TEXT); lv_obj_set_width(value, 56);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    volume_sliders[index] = slider; volume_values[index] = value;
    return line;
}

static lv_obj_t *music_source, *music_repeat, *music_title, *music_info, *music_time, *music_bar, *music_pause_button;
static lv_obj_t *music_rows[M3_MUSIC_ROWS], *music_labels[M3_MUSIC_ROWS], *music_empty;
static lv_obj_t *music_shuffle, *music_library, *music_eq_panel, *music_eq_sliders[3], *music_eq_values[3];
static lv_obj_t *music_rays[24];
static lv_point_precise_t music_ray_points[24][2];
static float music_display_bands[3];
static M3_PSRAM_BSS m3_music_state_t music_snapshot;
static unsigned seen_music = ~0u;
static void music_refresh_event(lv_event_t *event) {
    (void)event; m3_music_refresh(segment_value(music_source) == 0);
    set_text(music_info, tr(T_MUSIC_SCAN));
}
static void music_page_event(lv_event_t *event) { m3_music_page((intptr_t)lv_event_get_user_data(event)); }
static void music_track_event(lv_event_t *event) {
    unsigned row_index = (uintptr_t)lv_event_get_user_data(event);
    if (row_index < music_snapshot.shown && !m3_music_play(music_snapshot.items[row_index].index, music_snapshot.catalog_revision))
        set_text(music_info, tr(T_MUSIC_FAIL));
}
static void music_step_event(lv_event_t *event) { if (!m3_music_next((intptr_t)lv_event_get_user_data(event))) set_text(music_info, tr(T_MUSIC_FAIL)); }
static void music_pause_event(lv_event_t *event) {
    (void)event;
    if (music_snapshot.active) m3_music_pause();
    else if (!m3_music_play(music_snapshot.selected, music_snapshot.catalog_revision)) set_text(music_info, tr(T_MUSIC_FAIL));
}
static void music_player_event(lv_event_t *event) {
    (void)event;
    lv_obj_add_flag(music_library, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(music_eq_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(pages[APP_MUSIC], 0, LV_ANIM_ON);
}
static void music_stop_event(lv_event_t *event) { (void)event; m3_music_stop(); }
static void music_repeat_event(lv_event_t *event) { (void)event; m3_music_repeat_mode((m3_music_repeat_t)segment_value(music_repeat)); }
static void music_shuffle_event(lv_event_t *event) { (void)event; m3_music_shuffle(segment_value(music_shuffle) == 1); }
static void music_panel_event(lv_event_t *event) {
    lv_obj_t *panel = (uintptr_t)lv_event_get_user_data(event) ? music_eq_panel : music_library;
    if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN); lv_obj_scroll_to_view(panel, LV_ANIM_ON);
    } else lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
static void music_eq_event(lv_event_t *event) {
    unsigned band = (uintptr_t)lv_event_get_user_data(event);
    m3_music_equalizer(band, lv_slider_get_value(music_eq_sliders[band]));
}
static void music_eq_reset(lv_event_t *event) {
    (void)event; for (unsigned b = 0; b < 3; ++b) m3_music_equalizer(b, 0);
}
static void music_swipe_event(lv_event_t *event) {
    lv_indev_t *input = lv_event_get_indev(event); if (!input) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(input);
    if ((dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) && !m3_music_next(dir == LV_DIR_LEFT ? 1 : -1))
        set_text(music_info, tr(T_MUSIC_FAIL));
}
static void music_visual_update(lv_timer_t *timer) {
    (void)timer; if (current_app != APP_MUSIC) return;
    m3_music_state(&music_snapshot);
    for (unsigned b = 0; b < 3; ++b) {
        float target = music_snapshot.active && !music_snapshot.paused ? music_snapshot.bands[b] : 0;
        music_display_bands[b] += (target - music_display_bands[b]) * 0.45f;
    }
    for (unsigned i = 0; i < 24; ++i) {
        float angle = (i * 15.0f - 90.0f) * 0.017453293f;
        float radius = 64 + music_display_bands[i / 8] * 0.28f;
        music_ray_points[i][0] = (lv_point_precise_t){100 + cosf(angle) * 58, 100 + sinf(angle) * 58};
        music_ray_points[i][1] = (lv_point_precise_t){100 + cosf(angle) * radius, 100 + sinf(angle) * radius};
        lv_line_set_points(music_rays[i], music_ray_points[i], 2);
    }
}
static void update_music(void) {
    m3_music_state(&music_snapshot);
    if (seen_music == music_snapshot.revision && current_app != APP_MUSIC) return;
    seen_music = music_snapshot.revision;
    segment_select(music_repeat, music_snapshot.repeat_mode);
    segment_select(music_shuffle, music_snapshot.shuffle ? 1 : 0);
    for (unsigned b = 0; b < 3; ++b) {
        if (!lv_obj_has_state(music_eq_sliders[b], LV_STATE_PRESSED))
            lv_slider_set_value(music_eq_sliders[b], music_snapshot.eq_db[b], LV_ANIM_OFF);
        char value[16]; snprintf(value, sizeof(value), "%+d dB", music_snapshot.eq_db[b]);
        set_text(music_eq_values[b], value);
    }
    set_text(music_title, music_snapshot.title[0] ? music_snapshot.title : tr(T_MUSIC_EMPTY));
    set_text(music_info, music_snapshot.active && music_snapshot.paused ? tr(T_MUSIC_PAUSE) : status_text(music_snapshot.message));
    lv_bar_set_value(music_bar, music_snapshot.progress, LV_ANIM_OFF);
    char time[80]; snprintf(time, sizeof(time), tr(T_MUSIC_STATUS), music_snapshot.seconds / 60, music_snapshot.seconds % 60,
        music_snapshot.progress, music_snapshot.count); set_text(music_time, time);
    lv_label_set_text(lv_obj_get_child(music_pause_button, 0), tr(!music_snapshot.active ? T_RADIO_PLAY : music_snapshot.paused ? T_MUSIC_RESUME : T_MUSIC_PAUSE));
    for (unsigned i = 0; i < M3_MUSIC_ROWS; ++i) {
        if (i < music_snapshot.shown) {
            set_text(music_labels[i], music_snapshot.items[i].name);
            lv_obj_remove_flag(music_rows[i], LV_OBJ_FLAG_HIDDEN);
            set_bg(music_rows[i], music_snapshot.active && music_snapshot.items[i].index == music_snapshot.selected ? 0x164653 : SURFACE_HI);
        } else lv_obj_add_flag(music_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (music_snapshot.count) lv_obj_add_flag(music_empty, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(music_empty, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- Navigation. ---------- */
static void keyboard_motion(void *obj, int32_t y) { lv_obj_set_style_translate_y(obj, y, 0); }
static void home_feedback(void *obj, int32_t progress) {
    home_progress = progress;
    int32_t width = 120 + progress;
    lv_obj_set_size(obj, width, 5 + progress / 20);
    lv_obj_set_pos(obj, (480 - width) / 2, 30 - progress / 20);
    lv_obj_set_style_bg_color(obj, lv_color_mix(lv_color_hex(CYAN), lv_color_hex(MAGENTA), progress * 255 / 100), 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(CYAN), 0);
    lv_obj_set_style_shadow_width(obj, progress / 5, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_50, 0);
}
static void home_settle(void) {
    lv_anim_delete(home_pill, home_feedback);
    lv_anim_t animation; lv_anim_init(&animation);
    lv_anim_set_var(&animation, home_pill); lv_anim_set_exec_cb(&animation, home_feedback);
    lv_anim_set_values(&animation, home_progress, 0); lv_anim_set_duration(&animation, ui_duration(220));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_start(&animation);
}
static void hide_keyboard(void) {
    lv_obj_t *field = lv_keyboard_get_textarea(keyboard);
    if (field) lv_obj_remove_state(field, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(keyboard, NULL);
    lv_anim_delete(keyboard, keyboard_motion);
    keyboard_motion(keyboard, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(viewport, HOME_TOP - (current_app == APP_HOME ? 44 : CONTENT_TOP));
}
/* Translation only: page-wide opacity would force a full-screen layer on every frame. */
static void page_motion(void *obj, int32_t x) { lv_obj_set_style_translate_x(obj, x, 0); }
static void phone_enter(app_id_t app);
static void show_app(app_id_t app) {
    if (app >= APP_COUNT) return;
    if (current_app == APP_TUBE && app != APP_TUBE) tube_leave();
    if (current_app == APP_READER && app != APP_READER) reader_leave();
    if (current_app == APP_GAMES && app != APP_GAMES && sd_panel) sd_apps_close(NULL);
    if (voice_target) { m3_audio_stop(); voice_target = NULL; }
    if (voice_panel) lv_obj_add_flag(voice_panel, LV_OBJ_FLAG_HIDDEN);
    if (shade) shade_set(false);
    hide_keyboard();
    swipe_started = false;
    home_settle();
    for (unsigned i = 0; i < APP_COUNT; ++i) {
        lv_anim_delete(pages[i], page_motion);
        page_motion(pages[i], 0);
        if (i == app) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (current_app == APP_GALLERY && app != APP_GALLERY) m3_gallery_close();
    bool changed_app = current_app != app;
    current_app = app;
    if (changed_app) m3_feedback_emit(app == APP_WEATHER ? M3_CUE_TAP : M3_CUE_APP_HOME + app, m3_i18n_language());
    phone_enter(app);
    if (app == APP_READER) reader_enter();
    if (app == APP_HOME && home_tiles) {
        home_index = 1;
        lv_tileview_set_tile_by_index(home_tiles, 1, 0, LV_ANIM_OFF);
    }
    home_page_update();
    if (app == APP_MUSIC && !music_snapshot.catalog_revision) m3_music_refresh(true);
    bool home = app == APP_HOME;
    if (home) {
        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(home_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(app_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(home_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_y(pages[app], 0, LV_ANIM_OFF);
    }
    lv_obj_set_y(viewport, home ? 44 : CONTENT_TOP);
    lv_obj_set_height(viewport, HOME_TOP - (home ? 44 : CONTENT_TOP));
    lv_label_set_text(app_title, tr(app_names[app]));
    lv_anim_t animation; lv_anim_init(&animation);
    lv_anim_set_var(&animation, pages[app]); lv_anim_set_exec_cb(&animation, page_motion);
    lv_anim_set_values(&animation, home ? 0 : 36, 0); lv_anim_set_duration(&animation, ui_duration(180));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_start(&animation);
}
static void launch_app(lv_event_t *event) { show_app((app_id_t)(uintptr_t)lv_event_get_user_data(event)); }
static void go_home(lv_event_t *event) { (void)event; show_app(APP_HOME); }
/* Only a drag originating in the bottom strip is a home gesture. Scrolling
 * files/forms and dragging the keyboard cannot accidentally exit an app. */
static bool home_swipe(int dx, int dy) { return dy <= -70 && dx >= -90 && dx <= 90; }
static void home_gesture(lv_event_t *event) {
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point; lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        swipe_origin = point; swipe_started = point.y >= HOME_TOP;
        if (swipe_started) {
            lv_anim_delete(home_pill, home_feedback);
            home_feedback(home_pill, 10);
        }
    } else if (code == LV_EVENT_PRESSING && swipe_started) {
        int dx = point.x - swipe_origin.x, dy = point.y - swipe_origin.y;
        int progress = dx < -90 || dx > 90 || dy >= 0 ? 0 : -dy * 100 / 70;
        if (progress > 100) progress = 100;
        home_feedback(home_pill, progress);
        if (home_swipe(dx, dy)) {
            swipe_started = false; show_app(APP_HOME);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (swipe_started) home_settle();
        swipe_started = false;
    }
}

/* ---------- Keyboard. Function keys use LVGL symbols, so keycaps stay language-neutral. ---------- */
#define KEY_SHIFT LV_SYMBOL_UP
#define KEY_DEL LV_SYMBOL_BACKSPACE
#define KEY_HIDE LV_SYMBOL_KEYBOARD
#define KEY_OK LV_SYMBOL_OK
#define KEY_KO "한"
#define KEY_JA "かな"
#define KEY_KATA "カナ"
#define KEY_DAKU "゛"
#define KEY_SEMI "゜"
#define KEY_SMALL "小"
#define KEY_EN "ABC"
#define KEY_SYM "?123"
#define KEY_SPACE " "
static const char *const keys_lower[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    KEY_SHIFT,"z","x","c","v","b","n","m",KEY_DEL,"\n",
    KEY_SYM,KEY_KO,KEY_JA,KEY_SPACE,".",KEY_HIDE,KEY_OK,""
};
static const char *const keys_upper[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    KEY_SHIFT,"Z","X","C","V","B","N","M",KEY_DEL,"\n",
    KEY_SYM,KEY_KO,KEY_JA,KEY_SPACE,".",KEY_HIDE,KEY_OK,""
};
static const char *const keys_special[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "!","@","#","$","%","^","&","*","(",")","\n",
    "_","-","+","=","/","?",":",";","\"","'","\n",
    "[","]","{","}","\\","|","<",">",",","~","`","\n",
    KEY_EN,KEY_KO,KEY_JA,KEY_SPACE,".",KEY_DEL,KEY_HIDE,KEY_OK,""
};
static const char *const keys_number[] = {
    "1","2","3","\n","4","5","6","\n","7","8","9","\n",
    ".","0",KEY_DEL,"\n",KEY_HIDE,KEY_OK,""
};
static const char *const keys_korean[] = {
    "ㅂ","ㅈ","ㄷ","ㄱ","ㅅ","ㅛ","ㅕ","ㅑ","ㅐ","ㅔ","\n",
    "ㅁ","ㄴ","ㅇ","ㄹ","ㅎ","ㅗ","ㅓ","ㅏ","ㅣ","\n",
    KEY_SHIFT,"ㅋ","ㅌ","ㅊ","ㅍ","ㅠ","ㅜ","ㅡ",KEY_DEL,"\n",
    KEY_SYM,KEY_EN,KEY_JA,KEY_SPACE,".",KEY_HIDE,KEY_OK,""
};
static const char *const keys_korean_shift[] = {
    "ㅃ","ㅉ","ㄸ","ㄲ","ㅆ","ㅛ","ㅕ","ㅑ","ㅒ","ㅖ","\n",
    "ㅁ","ㄴ","ㅇ","ㄹ","ㅎ","ㅗ","ㅓ","ㅏ","ㅣ","\n",
    KEY_SHIFT,"ㅋ","ㅌ","ㅊ","ㅍ","ㅠ","ㅜ","ㅡ",KEY_DEL,"\n",
    KEY_SYM,KEY_EN,KEY_JA,KEY_SPACE,".",KEY_HIDE,KEY_OK,""
};
static const char *const keys_hiragana[] = {
    "あ","か","さ","た","な","は","ま","や","ら","わ","\n",
    "い","き","し","ち","に","ひ","み","ゆ","り","を","\n",
    "う","く","す","つ","ぬ","ふ","む","よ","る","ん","\n",
    "え","け","せ","て","ね","へ","め","ゃ","れ","ー","\n",
    "お","こ","そ","と","の","ほ","も","ゅ","ろ","ょ","\n",
    KEY_KATA,KEY_DAKU,KEY_SEMI,KEY_SMALL,KEY_EN,KEY_KO,KEY_SPACE,KEY_DEL,KEY_HIDE,KEY_OK,""
};
static const char *const keys_katakana[] = {
    "ア","カ","サ","タ","ナ","ハ","マ","ヤ","ラ","ワ","\n",
    "イ","キ","シ","チ","ニ","ヒ","ミ","ユ","リ","ヲ","\n",
    "ウ","ク","ス","ツ","ヌ","フ","ム","ヨ","ル","ン","\n",
    "エ","ケ","セ","テ","ネ","ヘ","メ","ャ","レ","ー","\n",
    "オ","コ","ソ","ト","ノ","ホ","モ","ュ","ロ","ョ","\n",
    KEY_JA,KEY_DAKU,KEY_SEMI,KEY_SMALL,KEY_EN,KEY_KO,KEY_SPACE,KEY_DEL,KEY_HIDE,KEY_OK,""
};
static M3_PSRAM_BSS lv_buttonmatrix_ctrl_t key_controls[8][64];
static bool function_key(const char *key) {
    return !strcmp(key, KEY_SHIFT) || !strcmp(key, KEY_DEL) || !strcmp(key, KEY_HIDE) || !strcmp(key, KEY_OK) ||
        !strcmp(key, KEY_KO) || !strcmp(key, KEY_EN) || !strcmp(key, KEY_SYM) || !strcmp(key, KEY_JA) ||
        !strcmp(key, KEY_KATA) || !strcmp(key, KEY_DAKU) || !strcmp(key, KEY_SEMI) || !strcmp(key, KEY_SMALL);
}
static void kana_edit(lv_obj_t *field, unsigned kind) {
    char text[385]; const char *value = lv_textarea_get_text(field);
    if (strlen(value) >= sizeof(text)) return;
    memcpy(text, value, strlen(value) + 1);
    if (m3_kana_edit(text, sizeof(text), kind)) {
        lv_textarea_set_text(field, text); lv_textarea_set_cursor_pos(field, LV_TEXTAREA_CURSOR_LAST);
    }
}
static void keyboard_key(lv_event_t *event) {
    (void)event;
    uint32_t index = lv_buttonmatrix_get_selected_button(keyboard);
    if (index == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *key = lv_buttonmatrix_get_button_text(keyboard, index);
    lv_obj_t *field = lv_keyboard_get_textarea(keyboard);
    if (!key || !field) return;
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
    if (!strcmp(key, KEY_OK)) { hide_keyboard(); lv_obj_send_event(field, LV_EVENT_READY, NULL); }
    else if (!strcmp(key, KEY_HIDE)) hide_keyboard();
    else if (!strcmp(key, KEY_KO)) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
    else if (!strcmp(key, KEY_JA)) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_3);
    else if (!strcmp(key, KEY_KATA)) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_4);
    else if (!strcmp(key, KEY_DAKU)) kana_edit(field, M3_KANA_VOICED);
    else if (!strcmp(key, KEY_SEMI)) kana_edit(field, M3_KANA_SEMIVOICED);
    else if (!strcmp(key, KEY_SMALL)) kana_edit(field, M3_KANA_SMALL);
    else if (!strcmp(key, KEY_EN)) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    else if (!strcmp(key, KEY_SYM)) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_SPECIAL);
    else if (!strcmp(key, KEY_SHIFT)) lv_keyboard_set_mode(keyboard,
        mode == LV_KEYBOARD_MODE_USER_1 ? LV_KEYBOARD_MODE_USER_2 : mode == LV_KEYBOARD_MODE_USER_2 ? LV_KEYBOARD_MODE_USER_1 :
        mode == LV_KEYBOARD_MODE_TEXT_LOWER ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_TEXT_LOWER);
    else if (!strcmp(key, KEY_DEL)) lv_textarea_delete_char(field);
    else if (mode == LV_KEYBOARD_MODE_USER_3 || mode == LV_KEYBOARD_MODE_USER_4) lv_textarea_add_text(field, key);
    else if ((unsigned char)key[0] >= 128) {
        char text[385];
        const char *current = lv_textarea_get_text(field);
        if (strlen(current) >= sizeof(text)) return;
        snprintf(text, sizeof(text), "%s", current);
        if (m3_hangul_append(text, sizeof(text), key)) {
            lv_textarea_set_text(field, text); lv_textarea_set_cursor_pos(field, LV_TEXTAREA_CURSOR_LAST);
        }
    } else lv_textarea_add_text(field, key);
}
static void configure_keyboard(void) {
    static const char *const *maps[] = {keys_lower, keys_upper, keys_special, keys_number, keys_korean, keys_korean_shift, keys_hiragana, keys_katakana};
    static const lv_keyboard_mode_t modes[] = {LV_KEYBOARD_MODE_TEXT_LOWER, LV_KEYBOARD_MODE_TEXT_UPPER,
        LV_KEYBOARD_MODE_SPECIAL, LV_KEYBOARD_MODE_NUMBER, LV_KEYBOARD_MODE_USER_1, LV_KEYBOARD_MODE_USER_2, LV_KEYBOARD_MODE_USER_3, LV_KEYBOARD_MODE_USER_4};
    for (unsigned mode = 0; mode < 8; ++mode) {
        unsigned count = 0;
        for (unsigned key = 0; maps[mode][key][0]; ++key) {
            const char *cap = maps[mode][key];
            if (!strcmp(cap, "\n")) continue;
            unsigned width = mode == 3 ? 1 : !strcmp(cap, KEY_SPACE) ? 6 : function_key(cap) ? 3 : 2;
            if (mode == 2 && width == 2) width = 1; /* Symbol rows hold up to 11 keys. */
            bool upper = mode == 1 && !strcmp(cap, KEY_SHIFT), korean_shift = mode == 5 && !strcmp(cap, KEY_SHIFT);
            key_controls[mode][count++] = width | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                (function_key(cap) && !upper && !korean_shift ? LV_BUTTONMATRIX_CTRL_CHECKED : 0);
        }
        lv_keyboard_set_map(keyboard, modes[mode], maps[mode], key_controls[mode]);
    }
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboard_key, LV_EVENT_VALUE_CHANGED, NULL);
}
static void edit_field(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) { form_dirty = true; return; }
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED && code != LV_EVENT_PRESSED) return;
    lv_obj_t *field = lv_event_get_current_target(event);
    bool opening = lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    uintptr_t input_mode = (uintptr_t)lv_event_get_user_data(event);
    lv_keyboard_set_mode(keyboard, input_mode == 2 ? (m3_i18n_language() == M3_LANG_JA ? LV_KEYBOARD_MODE_USER_3 : LV_KEYBOARD_MODE_USER_1) :
        input_mode == 1 ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, field);
    lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    /* The keyboard constructor uses BOTTOM_MID alignment. Coordinates below
     * must be absolute screen coordinates, not offsets from that alignment. */
    lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 0, HOME_TOP - KEYBOARD_HEIGHT);
    lv_obj_move_to_index(keyboard, -1);
    lv_obj_move_to_index(home_strip, -1);
    lv_obj_set_height(viewport, HOME_TOP - CONTENT_TOP - KEYBOARD_HEIGHT);
    lv_obj_update_layout(viewport);
    lv_obj_scroll_to_view_recursive(field, ui_animation());
    if (opening) {
        lv_anim_delete(keyboard, keyboard_motion);
        lv_anim_t animation; lv_anim_init(&animation);
        lv_anim_set_var(&animation, keyboard); lv_anim_set_exec_cb(&animation, keyboard_motion);
        lv_anim_set_values(&animation, KEYBOARD_HEIGHT, 0); lv_anim_set_duration(&animation, ui_duration(180));
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_start(&animation);
    }
}
static lv_obj_t *icon_part(lv_obj_t *parent, int x, int y, int w, int h, int radius);
/* A drawn glyph on a themed surface: same shape as icon_part, in the text color. */
static lv_obj_t *glyph_part(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *part = icon_part(parent, x, y, w, h, radius); set_bg(part, TEXT); return part;
}
/* Microphone glyph drawn from shapes; the built-in symbol set has no microphone. */
static lv_obj_t *mic_button(lv_obj_t *parent, lv_obj_t *field) {
    lv_obj_t *obj = icon_button(parent, NULL, voice_input, field);
    glyph_part(obj, 21, 11, 10, 19, 5);
    lv_obj_t *cradle = bare(obj); lv_obj_set_pos(cradle, 16, 20); lv_obj_set_size(cradle, 20, 16);
    lv_obj_set_style_border_width(cradle, 2, 0); lv_obj_set_style_border_color(cradle, lv_color_hex(TEXT), 0);
    lv_obj_set_style_border_side(cradle, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(cradle, 9, 0);
    glyph_part(obj, 25, 36, 2, 5, 1); glyph_part(obj, 20, 40, 12, 2, 1);
    return obj;
}
typedef enum { INPUT_TEXT, INPUT_NUMBER, INPUT_KOREAN } input_mode_t;
/* Caption + field. Voice input is offered only where speech is a sensible way to type. */
static lv_obj_t *input_field(lv_obj_t *parent, m3_text_t title, unsigned length, input_mode_t mode, bool voice) {
    if (title != T_NONE) caption(parent, title);
    lv_obj_t *line = row(parent);
    lv_obj_t *field = lv_textarea_create(line);
    lv_obj_add_style(field, &st_field, 0); lv_obj_add_style(field, &st_field_focus, LV_STATE_FOCUSED);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    grow(field); lv_obj_set_height(field, 52);
    lv_textarea_set_one_line(field, true); lv_textarea_set_max_length(field, length);
    if (mode == INPUT_NUMBER) lv_textarea_set_accepted_chars(field, "0123456789.");
    lv_obj_add_event_cb(field, edit_field, LV_EVENT_ALL, (void *)(uintptr_t)mode);
    if (voice) mic_button(line, field);
    return field;
}

#include "m3_weather_ui.inc"

/* ---------- Voice input sheet. ---------- */
static void voice_retry(lv_event_t *event) {
    (void)event;
    static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice); if (voice.busy) return;
    if (!m3_audio_voice()) lv_label_set_text(voice_info, tr(T_VOICE_START_FAIL));
}
static void voice_input(lv_event_t *event) {
    static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice);
    if (voice.busy) return;
    seen_voice = voice.revision;
    hide_keyboard();
    voice_target = lv_event_get_user_data(event);
    lv_label_set_text(voice_text, "");
    lv_label_set_text(voice_info, tr(voice.provider == 3 ? T_VOICE_AZURE : voice.provider ? T_VOICE_GEMINI : T_VOICE_OPENAI));
    lv_obj_remove_flag(voice_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(voice_panel, -1); lv_obj_move_to_index(home_strip, -1);
    voice_retry(NULL);
}
static void voice_finish(lv_event_t *event) { (void)event; m3_voice_finish(); }
static void voice_cancel(lv_event_t *event) {
    (void)event; m3_audio_stop(); voice_target = NULL;
    lv_obj_add_flag(voice_panel, LV_OBJ_FLAG_HIDDEN);
}
static void voice_settings(lv_event_t *event) { (void)event; show_app(APP_SYSTEM); }
static void voice_apply(lv_event_t *event) {
    (void)event;
    static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice);
    if (!voice_target || voice.busy || !voice.text[0]) return;
    const char *accepted = lv_textarea_get_accepted_chars(voice_target);
    unsigned characters = 0;
    for (const unsigned char *p = (const unsigned char *)voice.text; *p; ++p) {
        if ((*p & 0xc0) != 0x80) ++characters;
        if (accepted && !strchr(accepted, *p)) {
            lv_label_set_text(voice_info, tr(T_VOICE_DIGITS)); return;
        }
    }
    unsigned maximum = lv_textarea_get_max_length(voice_target);
    if ((maximum && characters > maximum) || (voice_target == ssid_field && strlen(voice.text) > 32)) {
        lv_label_set_text(voice_info, tr(T_VOICE_TOO_LONG)); return;
    }
    lv_textarea_set_text(voice_target, voice.text);
    voice_cancel(NULL);
}
static void voice_key_status(void) {
    static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice);
    lv_label_set_text(voice_key_info, tr(voice.key_ready ? T_STT_READY : T_STT_NEEDS_KEY));
    lv_obj_set_style_text_color(voice_key_info, lv_color_hex(voice.key_ready ? GREEN : AMBER), 0);
}
static void voice_save_key(lv_event_t *event) {
    (void)event; hide_keyboard();
    if (!m3_voice_set_key(lv_textarea_get_text(voice_key))) {
        lv_label_set_text(voice_key_info, tr(T_STT_KEY_FAIL)); return;
    }
    lv_textarea_set_text(voice_key, "");
    voice_key_status();
}
static void voice_provider_changed(lv_event_t *event) {
    (void)event;
    if (!m3_voice_set_provider(segment_value(voice_provider))) {
        static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice);
        segment_select(voice_provider, voice.provider);
        lv_label_set_text(voice_key_info, tr(T_STT_BUSY)); return;
    }
    voice_key_status();
}
static void language_changed(lv_event_t *event) {
    (void)event;
    m3_lang_t language = (m3_lang_t)segment_value(language_tabs);
    if (language != m3_i18n_language() && m3_i18n_set_language(language)) {
        apply_language();
        m3_feedback_clear(); m3_feedback_emit(M3_CUE_LANGUAGE, language);
    }
}

/* ---------- AI agent. ---------- */
static void agent_send(lv_event_t *event) {
    (void)event; hide_keyboard();
    if (!m3_agent_submit(lv_textarea_get_text(agent_prompt), segment_value(agent_model))) {
        static M3_PSRAM_BSS m3_agent_state_t agent; m3_agent_state(&agent);
        lv_label_set_text(agent_info, tr(!agent.ready ? T_AGENT_UNAVAILABLE : !agent.key_ready ? T_AGENT_NO_KEY :
            agent.busy ? T_AGENT_BUSY : T_AGENT_OFFLINE));
    } else lv_textarea_set_text(agent_prompt, "");
}
static void agent_cancel(lv_event_t *event) { (void)event; m3_agent_cancel(false); }
static void agent_clear(lv_event_t *event) { (void)event; m3_agent_cancel(true); }
static void agent_read(lv_event_t *event) {
    (void)event; static M3_PSRAM_BSS m3_agent_state_t agent; m3_agent_state(&agent);
    static char speech[385]; size_t length = strnlen(agent.answer, sizeof(speech) - 1);
    while (length && ((unsigned char)agent.answer[length] & 0xc0) == 0x80) --length;
    memcpy(speech, agent.answer, length); speech[length] = 0;
    if (!m3_audio_speak(speech, 165)) lv_label_set_text(agent_info, tr(T_AGENT_TTS_FAIL));
}

/* ---------- Wi-Fi. ---------- */
static void static_changed(lv_event_t *event) {
    (void)event; form_dirty = true;
    if (lv_obj_has_state(static_switch, LV_STATE_CHECKED)) lv_obj_remove_flag(static_fields, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(static_fields, LV_OBJ_FLAG_HIDDEN);
}
static void password_visibility(lv_event_t *event) {
    bool hidden = !lv_textarea_get_password_mode(pass_field);
    lv_textarea_set_password_mode(pass_field, hidden);
    lv_obj_t *icon = lv_obj_get_child(lv_event_get_current_target(event), 0);
    lv_label_set_text(icon, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}
static void select_network(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= (unsigned)snapshot.scan_count) return;
    hide_keyboard();
    lv_textarea_set_text(ssid_field, snapshot.networks[index].ssid);
    lv_textarea_set_text(pass_field, "");
    wifi_open_selected = snapshot.networks[index].authmode == WIFI_AUTH_OPEN;
    snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s", snapshot.networks[index].ssid);
    form_dirty = true;
    if (snapshot.networks[index].saved) {
        network_settings_t settings = {.version = 1};
        snprintf(settings.ssid, sizeof(settings.ssid), "%s", snapshot.networks[index].ssid);
        bool accepted = m3_runtime_wifi(M3_WIFI_USE_PROFILE, &settings);
        memset(&settings, 0, sizeof(settings));
        set_text(wifi_info, tr(accepted ? T_WIFI_QUEUED : T_WIFI_BUSY));
        return;
    }
    if (wifi_open_selected) {
        /* Public networks always use an empty password and automatic addressing. */
        network_settings_t settings = {.version = 1};
        snprintf(settings.ssid, sizeof(settings.ssid), "%s", snapshot.networks[index].ssid);
        lv_obj_remove_state(static_switch, LV_STATE_CHECKED); static_changed(NULL);
        bool accepted = m3_runtime_wifi(M3_WIFI_CONNECT, &settings);
        memset(&settings, 0, sizeof(settings));
        set_text(wifi_info, tr(accepted ? T_WIFI_QUEUED : T_WIFI_BUSY));
        set_ink(wifi_info, accepted ? CYAN : AMBER);
    } else lv_obj_send_event(pass_field, LV_EVENT_FOCUSED, NULL);
}
static void wifi_feedback(const char *message, uint32_t color) {
    lv_label_set_text(wifi_info, message);
    lv_obj_set_style_text_color(wifi_info, lv_color_hex(color), 0);
    lv_obj_scroll_to_view_recursive(wifi_info, ui_animation());
}
static void wifi_saved_action(lv_event_t *event) {
    unsigned value = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = value % M3_WIFI_PROFILE_LIMIT;
    if (index >= snapshot.saved_wifi_count) return;
    network_settings_t settings = {.version = 1};
    snprintf(settings.ssid, sizeof(settings.ssid), "%s", snapshot.saved_wifi[index].ssid);
    hide_keyboard();
    bool accepted = m3_runtime_wifi(value >= M3_WIFI_PROFILE_LIMIT ? M3_WIFI_FORGET_PROFILE : M3_WIFI_USE_PROFILE, &settings);
    memset(&settings, 0, sizeof(settings));
    wifi_feedback(tr(accepted ? T_WIFI_QUEUED : T_WIFI_BUSY), accepted ? CYAN : AMBER);
}
static m3_wifi_action_t wifi_connection_action(const network_settings_t *settings) {
    bool explicit_open = wifi_open_selected && !strcmp(settings->ssid, wifi_selected_ssid);
    bool saved = snapshot.wifi_password_saved && !strcmp(settings->ssid, snapshot.settings.ssid);
    for (unsigned i = 0; i < snapshot.saved_wifi_count; ++i)
        if (snapshot.saved_wifi[i].secured && !strcmp(settings->ssid, snapshot.saved_wifi[i].ssid)) saved = true;
    return !settings->pass[0] && !explicit_open && saved ? M3_WIFI_CONNECT_SAVED : M3_WIFI_CONNECT;
}
static void wifi_action(lv_event_t *event) {
    m3_wifi_action_t action = (m3_wifi_action_t)(uintptr_t)lv_event_get_user_data(event);
    network_settings_t settings = {.version = 1};
    hide_keyboard();
    if (action == M3_WIFI_CONNECT) {
        const char *ssid = lv_textarea_get_text(ssid_field), *pass = lv_textarea_get_text(pass_field);
        if (strlen(ssid) > 32 || strlen(pass) > 63) {
            wifi_feedback(tr(T_WIFI_TOO_LONG), RED); return;
        }
        snprintf(settings.ssid, sizeof(settings.ssid), "%s", ssid);
        snprintf(settings.pass, sizeof(settings.pass), "%s", pass);
        settings.use_static = lv_obj_has_state(static_switch, LV_STATE_CHECKED);
        snprintf(settings.ip, sizeof(settings.ip), "%s", lv_textarea_get_text(ip_field));
        snprintf(settings.gateway, sizeof(settings.gateway), "%s", lv_textarea_get_text(gateway_field));
        snprintf(settings.netmask, sizeof(settings.netmask), "%s", lv_textarea_get_text(mask_field));
        snprintf(settings.dns, sizeof(settings.dns), "%s", lv_textarea_get_text(dns_field));
        if (!settings.dns[0]) snprintf(settings.dns, sizeof(settings.dns), "%s", settings.gateway);
        if (!network_settings_valid(&settings)) {
            memset(&settings, 0, sizeof(settings));
            wifi_feedback(tr(T_WIFI_INVALID), RED); return;
        }
    }
    if (action == M3_WIFI_CONNECT) action = wifi_connection_action(&settings);
    bool accepted = m3_runtime_wifi(action,
        action == M3_WIFI_CONNECT || action == M3_WIFI_CONNECT_SAVED ? &settings : NULL);
    memset(&settings, 0, sizeof(settings));
    if (accepted) {
        if (action == M3_WIFI_CONNECT || action == M3_WIFI_CONNECT_SAVED) {
            lv_textarea_set_text(pass_field, ""); lv_textarea_set_password_mode(pass_field, true);
            wifi_open_selected = false;
        }
        wifi_feedback(tr(T_WIFI_QUEUED), CYAN);
    } else wifi_feedback(tr(T_WIFI_BUSY), AMBER);
}
static unsigned wifi_bars(int rssi) {
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}
/* Vector lock: closed for secured networks, open shackle for public networks. */
static void wifi_security(lv_obj_t *icon, bool open) {
    uint32_t color = open ? GREEN : MUTED;
    lv_obj_t *shackle = lv_obj_get_child(icon, 0);
    lv_obj_set_pos(shackle, open ? 10 : 6, 1);
    lv_obj_set_style_border_side(shackle, open ? LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT :
        LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(shackle, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(icon, 1), lv_color_hex(color), 0);
}
static void update_wifi(void) {
    static bool was_busy;
    if (snapshot.wifi_busy || was_busy || seen_wifi != snapshot.wifi_revision) {
        seen_wifi = snapshot.wifi_revision;
        set_text(wifi_info, status_text(snapshot.wifi_message));
        set_ink(wifi_info, snapshot.wifi_busy ? AMBER : CYAN);
    }
    was_busy = snapshot.wifi_busy;
    for (unsigned i = 0; i < 3; ++i) {
        if (!snapshot.initialized || !snapshot.wifi_ready || snapshot.wifi_busy) lv_obj_add_state(wifi_actions[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(wifi_actions[i], LV_STATE_DISABLED);
    }
    if (seen_settings != snapshot.settings_revision) {
        seen_settings = snapshot.settings_revision;
        if (!form_dirty) {
            lv_textarea_set_text(ssid_field, snapshot.settings.ssid);
            lv_textarea_set_text(ip_field, snapshot.settings.ip);
            lv_textarea_set_text(gateway_field, snapshot.settings.gateway);
            lv_textarea_set_text(mask_field, snapshot.settings.netmask[0] ? snapshot.settings.netmask : "255.255.255.0");
            lv_textarea_set_text(dns_field, snapshot.settings.dns);
            if (snapshot.settings.use_static) {
                lv_obj_add_state(static_switch, LV_STATE_CHECKED);
                lv_obj_remove_flag(static_fields, LV_OBJ_FLAG_HIDDEN);
            }
            form_dirty = false;
        }
    }
    if (seen_scan != snapshot.scan_revision) {
        seen_scan = snapshot.scan_revision;
        set_text(wifi_info, status_text(snapshot.wifi_message));
        for (unsigned i = 0; i < M3_WIFI_RESULTS; ++i) {
            if (i < (unsigned)snapshot.scan_count) {
                char text[64];
                unsigned bars = wifi_bars(snapshot.networks[i].rssi);
                lv_obj_set_style_text_color(lv_obj_get_child(scan_buttons[i], 0),
                    lv_color_hex(bars >= 3 ? GREEN : bars == 2 ? AMBER : RED), 0);
                lv_label_set_text(lv_obj_get_child(scan_buttons[i], 1), snapshot.networks[i].ssid);
                snprintf(text, sizeof(text), "%s%s%d dBm", snapshot.networks[i].saved ? tr(T_WIFI_SAVED) : "",
                    snapshot.networks[i].saved ? " · " : "", snapshot.networks[i].rssi);
                lv_label_set_text(lv_obj_get_child(scan_buttons[i], 2), text);
                wifi_security(lv_obj_get_child(scan_buttons[i], 3), snapshot.networks[i].authmode == WIFI_AUTH_OPEN);
                lv_obj_remove_flag(scan_buttons[i], LV_OBJ_FLAG_HIDDEN);
            } else lv_obj_add_flag(scan_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    char count[80]; snprintf(count, sizeof(count), tr(T_WIFI_SAVED_COUNT), snapshot.saved_wifi_count);
    set_text(wifi_saved_count, count);
    for (unsigned i = 0; i < M3_WIFI_PROFILE_LIMIT; ++i) {
        power_visible(wifi_saved_rows[i], i < snapshot.saved_wifi_count);
        if (i >= snapshot.saved_wifi_count) continue;
        set_text(wifi_saved_names[i], snapshot.saved_wifi[i].ssid);
        if (snapshot.wifi_busy || !snapshot.wifi_ready) {
            lv_obj_add_state(wifi_saved_connect[i], LV_STATE_DISABLED); lv_obj_add_state(wifi_saved_remove[i], LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(wifi_saved_connect[i], LV_STATE_DISABLED); lv_obj_remove_state(wifi_saved_remove[i], LV_STATE_DISABLED);
        }
    }
}

/* ---------- Audio, radio and speech. ---------- */
static void recorder_start(lv_event_t *event) {
    (void)event;
    if (!m3_audio_record()) lv_label_set_text(recorder_info, tr(T_AUDIO_BUSY));
}
static void recorder_stop(lv_event_t *event) { (void)event; m3_recorder_finish(); }
static void recorder_play(lv_event_t *event) {
    (void)event; m3_recorder_state_t recorded; m3_recorder_state(&recorded);
    if (recorded.busy) return;
    m3_audio_state_t audio; m3_audio_state(&audio);
    if (audio.playing) { m3_audio_stop(); return; }
    if (!recorded.saved || !m3_audio_recording_play(recorded.filename)) lv_label_set_text(recorder_info, tr(T_AUDIO_BUSY));
}
static void update_recorder(const m3_audio_state_t *audio) {
    m3_recorder_state_t recorded; m3_recorder_state(&recorded);
    char text[160];
    snprintf(text, sizeof(text), "%02u:%02u / 30:00", recorded.seconds / 60, recorded.seconds % 60);
    set_text(recorder_time, text);
    set_text(recorder_info, !audio->ready ? tr(T_AUDIO_UNAVAILABLE) : !snapshot.sd ? status_text("SD unavailable or busy") :
        audio->playing && !recorded.busy ? status_text(audio->message) :
        recorded.message[0] ? status_text(recorded.message) : tr(T_REC_HINT));
    if (!recorded.busy && recorded.error_stage[0]) {
        snprintf(text, sizeof(text), "%.100s / %.23s (%d)", status_text(recorded.message), recorded.error_stage, recorded.error_code);
        set_text(recorder_info, text);
    }
    lv_bar_set_value(recorder_meter, recorded.level, LV_ANIM_OFF);
    snprintf(text, sizeof(text), "%s%s%s", M3_RECORDER_FOLDER, recorded.saved ? "/\n" : "", recorded.saved ? recorded.filename : "");
    set_text(recorder_path, text);
    snprintf(text, sizeof(text), "%s %s", audio->playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY,
        tr(audio->playing ? T_AUDIO_STOP : T_REC_PLAY));
    set_text(lv_obj_get_child(recorder_actions[2], 0), text);
    bool enabled[] = {snapshot.sd && audio->ready && !audio->playing && !recorded.busy,
        recorded.busy, (audio->playing || (snapshot.sd && recorded.saved)) && audio->ready && !recorded.busy,
        snapshot.sd && !recorded.busy};
    for (unsigned i = 0; i < 4; ++i) {
        if (enabled[i]) lv_obj_remove_state(recorder_actions[i], LV_STATE_DISABLED);
        else lv_obj_add_state(recorder_actions[i], LV_STATE_DISABLED);
    }
}
static void stop_audio(lv_event_t *event) { (void)event; radio_pending = false; m3_audio_stop(); }
static void volume_changed(lv_event_t *event) {
    unsigned value = (unsigned)lv_slider_get_value(lv_event_get_target(event));
    m3_audio_volume(value);
    shown_volume = 1000; /* Next metrics tick mirrors the value to every volume row. */
}
static void radio_mark(int index) {
    radio_selected = index;
    for (unsigned i = 0; i < sizeof(radio_stations) / sizeof(radio_stations[0]); ++i) {
        if ((int)i == index) lv_obj_add_state(station_buttons[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(station_buttons[i], LV_STATE_CHECKED);
    }
    lv_label_set_text(radio_now, index >= 0 ? radio_stations[index].name : lv_textarea_get_text(radio_url));
}
static void start_radio(lv_event_t *event) {
    (void)event; hide_keyboard();
    unsigned mode = segment_value(radio_transport);
    bool preset = radio_selected >= 0 && (unsigned)radio_selected < sizeof(radio_stations) / sizeof(radio_stations[0]);
    bool accepted = preset && (mode || radio_stations[radio_selected].relay)
        ? m3_audio_radio_preset(radio_stations[radio_selected].id, mode ? mode - 1 : 1)
        : m3_audio_radio(lv_textarea_get_text(radio_url), segment_value(radio_codec) == 1);
    if (!accepted)
        lv_label_set_text(radio_info, tr(T_RADIO_FAIL));
    else lv_label_set_text(radio_info, tr(T_RADIO_QUEUED));
}
/* Stopping is asynchronous. A new station waits for the old stream to finish. */
static void radio_select(unsigned index);
static void play_radio(void) {
    m3_audio_state_t audio; m3_audio_state(&audio);
    if (!audio.playing) { radio_pending = false; start_radio(NULL); return; }
    m3_audio_stop();
    radio_pending = true; radio_pending_until = esp_timer_get_time() + 6000000;
    lv_label_set_text(radio_info, tr(T_RADIO_SWITCHING));
}
static void radio_preset(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= sizeof(radio_stations) / sizeof(radio_stations[0])) return;
    radio_select(index);
}
static void radio_select(unsigned index) {
    hide_keyboard();
    lv_textarea_set_text(radio_url, radio_stations[index].url);
    segment_select(radio_codec, radio_stations[index].aac ? 1 : 0);
    radio_mark((int)index);
}
static void station_tap(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index < sizeof(radio_stations) / sizeof(radio_stations[0])) { radio_select(index); play_radio(); }
}
static void play_selected(lv_event_t *event) { (void)event; play_radio(); }
static void play_custom(lv_event_t *event) { (void)event; radio_mark(-1); play_radio(); }
static void speak_text(lv_event_t *event) {
    (void)event; hide_keyboard();
    if (!m3_audio_speak(lv_textarea_get_text(tts_text), (unsigned)lv_slider_get_value(tts_speed)))
        lv_label_set_text(tts_info, tr(T_TTS_FAIL));
    else lv_label_set_text(tts_info, tr(T_TTS_QUEUED));
}
static void tts_sample(lv_event_t *event) {
    static const char *samples[] = {"안녕하세요. 한국어 음성 합성 시험입니다.",
        "무선 인터넷에 연결되었습니다.", "하나 둘 셋 넷 다섯. 스피커를 확인합니다."};
    lv_textarea_set_text(tts_text, samples[(uintptr_t)lv_event_get_user_data(event) % 3]);
}
static void tts_speed_label(void) {
    char text[16]; snprintf(text, sizeof(text), "%d", (int)lv_slider_get_value(tts_speed));
    lv_label_set_text(tts_speed_value, text);
}
static void tts_provider_changed(lv_event_t *event) {
    (void)event;
    unsigned provider = segment_value(tts_provider);
    m3_audio_tts_provider(provider);
    if (provider == 2) lv_obj_remove_state(tts_speed, LV_STATE_DISABLED);
    else lv_obj_add_state(tts_speed, LV_STATE_DISABLED);
}
static void tts_speed_changed(lv_event_t *event) { (void)event; tts_speed_label(); }

/* ---------- Games. ---------- */
static uint32_t tile_color(uint8_t power) {
    if (power == 1 || power == 2)
        return lv_color_to_u32(lv_color_mix(lv_color_hex(MUTED), lv_color_hex(SURFACE_HI), power == 1 ? 60 : 105)) & 0xffffff;
    const uint32_t colors[] = {SURFACE_HI, 0, 0, 0xf59e0b, 0xf97316, 0xef4444, 0xdc2626,
        0xeab308, 0xfacc15, 0x22c55e, 0x06b6d4, 0xa855f7};
    return power < sizeof(colors) / sizeof(colors[0]) ? colors[power] : MAGENTA;
}
static void render_crossing(void) {
    char text[48];
    if (crossing.best_row > crossing_best) crossing_best = crossing.best_row;
    set_text(score_titles[0], tr(T_SCORE)); set_text(score_titles[1], tr(T_BEST));
    snprintf(text, sizeof(text), "%lu", (unsigned long)crossing.best_row); set_text(score_values[0], text);
    snprintf(text, sizeof(text), "%lu", (unsigned long)crossing_best); set_text(score_values[1], text);
    set_text(game_info, tr(crossing.dead ? T_GAME_CROSS_OVER : T_GAME_CROSS_HINT));
    for (unsigned i = 0; i < M3_CROSS_ROWS; ++i) {
        uint32_t world = crossing.camera + i;
        m3_cross_lane_t lane = m3_crossing_lane(&crossing, world);
        set_bg(cross_lanes[i], lane == M3_CROSS_GRASS ? (world % 8 ? 0x518d46 : 0x609d50) :
            lane == M3_CROSS_RIVER ? 0x237eaa : 0x364250);
        int offset = m3_crossing_offset(&crossing, world), length = m3_crossing_length(&crossing, world);
        for (unsigned j = 0; j < 3; ++j) {
            lv_obj_t *obstacle = cross_objects[i][j];
            if (lane == M3_CROSS_GRASS) { lv_obj_add_flag(obstacle, LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_remove_flag(obstacle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(obstacle, offset + ((int)j - 1) * M3_CROSS_PERIOD, 8);
            lv_obj_set_width(obstacle, length);
            set_bg(obstacle, lane == M3_CROSS_RIVER ? 0x98602e : (world % 2 ? 0xf0b84a : 0xe46969));
            set_bg(cross_windows[i][j], lane == M3_CROSS_RIVER ? 0xbf8649 : 0xb8e6f2);
            lv_obj_set_width(cross_windows[i][j], lane == M3_CROSS_RIVER ? length - 16 : 20);
        }
    }
    int visible_row = (int)crossing.row - (int)crossing.camera;
    if (visible_row < 0) visible_row = 0;
    lv_obj_set_pos(cross_player, crossing.x - 16, (M3_CROSS_ROWS - 1 - visible_row) * M3_CROSS_CELL + 5);
    set_bg(cross_body, crossing.dead ? RED : 0xfff2cf);
}
static void crossing_tick(lv_timer_t *timer) {
    (void)timer;
    if (!crossing_game || current_app != APP_GAMES || crossing.dead || shade_y > -HOME_TOP) return;
    if (sd_panel && !lv_obj_has_flag(sd_panel, LV_OBJ_FLAG_HIDDEN)) return;
    m3_crossing_tick(&crossing);
    render_crossing();
}
static void render_game(void) {
    char text[96];
    if (crossing_game) {
        lv_obj_add_state(game_undo, LV_STATE_DISABLED);
        render_crossing(); return;
    }
    if (memory_game) {
        unsigned solved = 0;
        for (unsigned i = 0; i < 16; ++i) if (memory_solved[i]) ++solved;
        lv_label_set_text(score_titles[0], tr(T_TURNS)); lv_label_set_text(score_titles[1], tr(T_PAIRS));
        snprintf(text, sizeof(text), "%u", memory_turns); lv_label_set_text(score_values[0], text);
        snprintf(text, sizeof(text), "%u / 8", solved / 2); lv_label_set_text(score_values[1], text);
        lv_label_set_text(game_info, tr(solved == 16 ? T_GAME_CLEARED : T_GAME_MATCH));
    } else {
        if (game.score > best_score) best_score = game.score;
        lv_label_set_text(score_titles[0], tr(T_SCORE)); lv_label_set_text(score_titles[1], tr(T_BEST));
        snprintf(text, sizeof(text), "%lu", (unsigned long)game.score); lv_label_set_text(score_values[0], text);
        snprintf(text, sizeof(text), "%u", best_score); lv_label_set_text(score_values[1], text);
        lv_label_set_text(game_info, tr(!m3_2048_can_move(&game) ? T_GAME_OVER : game.won ? T_GAME_WON : T_GAME_SWIPE));
    }
    if (undo_available && !memory_game) lv_obj_remove_state(game_undo, LV_STATE_DISABLED);
    else lv_obj_add_state(game_undo, LV_STATE_DISABLED);
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t color = SURFACE_HI, ink = TEXT;
        const lv_font_t *font = FONT_ICON;
        if (memory_game) {
            snprintf(text, sizeof(text), "%s", "?"); ink = MUTED;
            if (memory_visible[i] || memory_solved[i]) {
                snprintf(text, sizeof(text), "%u", memory_cards[i] + 1);
                color = memory_solved[i] ? 0x0f5f4a : 0x7c205f; ink = 0xffffff;
            }
        } else {
            text[0] = 0;
            if (game.tiles[i]) {
                unsigned long value = (unsigned long)(UINT32_C(1) << game.tiles[i]);
                snprintf(text, sizeof(text), "%lu", value);
                if (value >= 1024) font = &lv_font_montserrat_20;
                ink = game.tiles[i] <= 2 ? TEXT : 0x1a1205;
            }
            color = tile_color(game.tiles[i]);
        }
        lv_obj_t *face = lv_obj_get_child(game_tiles[i], 0);
        lv_label_set_text(face, text);
        lv_obj_set_style_text_font(face, font, 0); lv_obj_set_style_text_color(face, lv_color_hex(ink), 0);
        lv_obj_set_style_bg_color(game_tiles[i], lv_color_hex(color), 0);
    }
}
static void game_move(m3_game_direction_t direction) {
    if (crossing_game) { m3_crossing_move(&crossing, direction); render_game(); return; }
    if (memory_game) return;
    m3_2048_t previous = game;
    if (m3_2048_move(&game, direction)) { undo_game = previous; undo_available = true; render_game(); }
}
static void game_direction(lv_event_t *event) { game_move((m3_game_direction_t)(uintptr_t)lv_event_get_user_data(event)); }
static void game_touch(lv_event_t *event) {
    if (memory_game) return;
    lv_indev_t *input = lv_indev_active(); if (!input) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) { game_origin = point; game_drag = true; }
    else if (code == LV_EVENT_PRESSING && game_drag) {
        int dx = point.x - game_origin.x, dy = point.y - game_origin.y;
        if (abs(dx) >= 40 || abs(dy) >= 40) {
            game_drag = false;
            game_move(abs(dx) > abs(dy) ? (dx < 0 ? M3_GAME_LEFT : M3_GAME_RIGHT) : (dy < 0 ? M3_GAME_UP : M3_GAME_DOWN));
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (code == LV_EVENT_RELEASED && game_drag && crossing_game) game_move(M3_GAME_UP);
        game_drag = false;
    }
}
static void memory_card(lv_event_t *event) {
    if (!memory_game || crossing_game || memory_second >= 0) return;
    unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (i >= 16 || memory_solved[i] || memory_visible[i]) return;
    memory_visible[i] = true;
    if (memory_first < 0) memory_first = (int)i;
    else {
        ++memory_turns;
        if (memory_cards[memory_first] == memory_cards[i]) {
            memory_solved[memory_first] = memory_solved[i] = true; memory_first = -1;
        } else { memory_second = (int)i; memory_hide_at = (uint64_t)esp_timer_get_time() + 750000; }
    }
    render_game();
}
static void new_game(lv_event_t *event) {
    (void)event;
    game_drag = false;
    if (crossing_game) {
        m3_crossing_reset(&crossing, esp_random()); undo_available = false;
    } else if (memory_game) {
        memset(memory_visible, 0, sizeof(memory_visible)); memset(memory_solved, 0, sizeof(memory_solved));
        memory_first = memory_second = -1; memory_turns = 0;
        for (unsigned i = 0; i < 16; ++i) memory_cards[i] = i / 2;
        for (unsigned i = 15; i; --i) { unsigned other = esp_random() % (i + 1); uint8_t value = memory_cards[i]; memory_cards[i] = memory_cards[other]; memory_cards[other] = value; }
    } else { m3_2048_reset(&game, esp_random()); undo_available = false; }
    render_game();
}
static void choose_game(lv_event_t *event) {
    (void)event;
    memory_game = segment_value(game_tabs) == 1;
    crossing_game = segment_value(game_tabs) == 2;
    for (unsigned i = 0; i < 16; ++i) {
        if (crossing_game) lv_obj_add_flag(game_tiles[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(game_tiles[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (crossing_game) lv_obj_remove_flag(cross_scene, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cross_scene, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < 4; ++i) {
        if (memory_game) lv_obj_add_flag(game_arrows[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(game_arrows[i], LV_OBJ_FLAG_HIDDEN);
    }
    new_game(NULL);
}
static void undo_move(lv_event_t *event) {
    (void)event;
    if (!memory_game && !crossing_game && undo_available) { game = undo_game; undo_available = false; render_game(); }
}

/* SD code runs on its own bounded worker; LVGL only copies completed frames. */
static void sd_apps_release(void) {
    m3_sd_apps_stop();
    if (sd_canvas) { lv_obj_delete(sd_canvas); sd_canvas = NULL; }
    free(sd_pixels); sd_pixels = NULL;
    sd_launch_failed = false;
    lv_obj_add_flag(sd_restart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sd_stage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sd_list, LV_OBJ_FLAG_HIDDEN);
}
static void sd_apps_close(lv_event_t *event) {
    bool was_playing = sd_canvas != NULL;
    sd_apps_release();
    if (!event || !was_playing) {
        lv_obj_add_flag(sd_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(builtin_games, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(sd_launcher, LV_OBJ_FLAG_HIDDEN);
    }
}
static void sd_apps_open(lv_event_t *event) {
    (void)event;
    sd_launch_failed = false;
    lv_obj_add_flag(builtin_games, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sd_launcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sd_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(pages[APP_GAMES], 0, LV_ANIM_OFF);
    sd_apps_tick(NULL);
}
static void sd_apps_launch(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= sd_snapshot.count || sd_canvas) return;
    sd_launch_failed = false;
    sd_pixels = heap_caps_calloc(M3_SD_APP_WIDTH * M3_SD_APP_HEIGHT, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sd_pixels || !m3_sd_apps_start(sd_snapshot.names[index])) {
        free(sd_pixels); sd_pixels = NULL;
        sd_launch_failed = true;
        lv_label_set_text(sd_status, tr(T_SD_APPS_FAILED)); return;
    }
    sd_canvas = lv_canvas_create(sd_frame_holder);
    lv_canvas_set_buffer(sd_canvas, sd_pixels, M3_SD_APP_WIDTH, M3_SD_APP_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(sd_canvas); lv_image_set_scale(sd_canvas, 448);
    lv_obj_remove_flag(sd_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sd_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sd_stage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sd_restart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(pages[APP_GAMES], 0, LV_ANIM_OFF);
}
static void sd_apps_control(lv_event_t *event) {
    m3_sd_apps_input((unsigned)(uintptr_t)lv_event_get_user_data(event));
}
static void sd_apps_tick(lv_timer_t *timer) {
    (void)timer;
    if (current_app != APP_GAMES || !sd_panel || lv_obj_has_flag(sd_panel, LV_OBJ_FLAG_HIDDEN)) return;
    if (!m3_sd_apps_state(&sd_snapshot)) return;
    if (sd_canvas) {
        if (m3_sd_apps_frame(sd_pixels)) lv_obj_invalidate(sd_canvas);
        if (sd_snapshot.error[0]) set_text(sd_status, sd_snapshot.error);
        else {
            char message[64]; snprintf(message, sizeof(message), tr(T_SD_APPS_SCORE), sd_snapshot.score);
            set_text(sd_status, message);
        }
    } else {
        set_text(sd_status, tr(sd_launch_failed ? T_SD_APPS_FAILED : sd_snapshot.count ? T_SD_APPS_HINT : T_SD_APPS_EMPTY));
        for (unsigned i = 0; i < M3_SD_APP_COUNT; ++i) {
            if (i < sd_snapshot.count) {
                lv_obj_remove_flag(sd_rows[i], LV_OBJ_FLAG_HIDDEN);
                set_text(sd_faces[i], sd_snapshot.names[i]);
            } else lv_obj_add_flag(sd_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ---------- Files. ---------- */
static void browse_refresh(void) {
    if (m3_runtime_browse(selected_sd, selected_folder, selected_offset)) {
        for (unsigned i = 0; i < STORAGE_VIEW_ROWS; ++i) lv_obj_add_state(file_buttons[i], LV_STATE_DISABLED);
        lv_label_set_text(file_info, tr(T_FILES_READING));
    }
}
static void choose_storage(lv_event_t *event) {
    (void)event;
    selected_sd = segment_value(storage_tabs) != 0;
    selected_folder[0] = 0; selected_offset = 0; browse_refresh();
}
static void previous_files(lv_event_t *event) {
    (void)event; selected_offset = selected_offset >= STORAGE_VIEW_ROWS ? selected_offset - STORAGE_VIEW_ROWS : 0; browse_refresh();
}
static void next_files(lv_event_t *event) {
    (void)event;
    if (snapshot.files.offset + snapshot.files.shown < snapshot.files.count) {
        selected_offset = snapshot.files.offset + STORAGE_VIEW_ROWS; browse_refresh();
    }
}
static void parent_folder(lv_event_t *event) {
    (void)event; char *slash = strrchr(selected_folder, '/'); if (slash) *slash = 0;
    selected_offset = 0; browse_refresh();
}
static lv_obj_t *list_row(lv_obj_t *parent, lv_event_cb_t callback, void *data);
#include "m3_phone_ui.inc"

static bool wav_name(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && !strcasecmp(ext, ".wav");
}
static void open_file(lv_event_t *event) {
    unsigned row_index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (row_index >= snapshot.files.shown || snapshot.files.error != ESP_OK) return;
    const storage_view_entry_t *entry = &snapshot.files.entries[row_index];
    if (entry->directory && snapshot.browse_sd) {
        char folder[256];
        int length = snprintf(folder, sizeof(folder), "%s/%s", snapshot.folder, entry->name);
        if (length <= 0 || (size_t)length >= sizeof(folder)) return;
        snprintf(selected_folder, sizeof(selected_folder), "%s", folder);
        selected_sd = true; selected_offset = 0; browse_refresh();
    } else if (snapshot.browse_sd && m3_reader_supported(entry->name)) {
        reader_open_path(snapshot.folder, entry->name);
    } else if (snapshot.browse_sd && m3_gallery_supported(entry->name)) {
        show_app(APP_GALLERY); gallery_load(row_index);
    } else if (snapshot.browse_sd && !strcmp(snapshot.folder, "/media")) {
        if (!m3_audio_play(entry->name)) lv_label_set_text(file_info, tr(T_FILES_PLAY_FAIL));
    } else if (snapshot.browse_sd && !strcmp(snapshot.folder, M3_RECORDER_FOLDER)) {
        if (!m3_audio_recording_play(entry->name)) lv_label_set_text(file_info, tr(T_FILES_PLAY_FAIL));
    }
}
static void recorder_folder(lv_event_t *event) {
    (void)event; selected_sd = true; strcpy(selected_folder, M3_RECORDER_FOLDER); selected_offset = 0; browse_refresh();
    segment_select(storage_tabs, 1);
    show_app(APP_FILES);
}
static void format_size(uint64_t bytes, char *text, size_t size) {
    if (bytes >= 1073741824ull) snprintf(text, size, "%.1f GiB", bytes / 1073741824.0);
    else if (bytes >= 1048576) snprintf(text, size, "%.1f MiB", bytes / 1048576.0);
    else if (bytes >= 1024) snprintf(text, size, "%.0f KiB", bytes / 1024.0);
    else snprintf(text, size, "%u B", (unsigned)bytes);
}
static void update_files(void) {
    char text[300], used[24], total[24];
    segment_select(storage_tabs, snapshot.browse_sd ? 1 : 0);
    snprintf(text, sizeof(text), "%s%s", snapshot.browse_sd ? "SD" : "Flash", snapshot.folder[0] ? snapshot.folder : "/");
    lv_label_set_text(file_path, text);
    if (snapshot.files.error != ESP_OK) {
        lv_label_set_text(file_info, tr(T_FILES_UNAVAILABLE)); lv_bar_set_value(file_bar, 0, LV_ANIM_OFF);
    } else {
        format_size(snapshot.files.used, used, sizeof(used)); format_size(snapshot.files.total, total, sizeof(total));
        snprintf(text, sizeof(text), tr(T_FILES_USED), used, total, snapshot.files.count);
        lv_label_set_text(file_info, text);
        lv_bar_set_value(file_bar, snapshot.files.total ? (int32_t)(snapshot.files.used * 100 / snapshot.files.total) : 0, ui_animation());
    }
    unsigned pages_total = (snapshot.files.count + STORAGE_VIEW_ROWS - 1) / STORAGE_VIEW_ROWS;
    snprintf(text, sizeof(text), "%u / %u", snapshot.files.offset / STORAGE_VIEW_ROWS + 1, pages_total ? pages_total : 1);
    lv_label_set_text(file_page, text);
    bool empty = snapshot.files.error == ESP_OK && !snapshot.files.shown;
    if (empty) lv_obj_remove_flag(file_empty, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(file_empty, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < STORAGE_VIEW_ROWS; ++i) {
        if (i < snapshot.files.shown && snapshot.files.error == ESP_OK) {
            const storage_view_entry_t *file = &snapshot.files.entries[i];
            bool playable = !file->directory && snapshot.browse_sd && !strcmp(snapshot.folder, "/media") && wav_name(file->name);
            lv_obj_t *icon = lv_obj_get_child(file_buttons[i], 0);
            lv_label_set_text(icon, file->directory ? LV_SYMBOL_DIRECTORY : playable ? LV_SYMBOL_PLAY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_color(icon, lv_color_hex(file->directory ? AMBER : playable ? CYAN : MUTED), 0);
            set_text(lv_obj_get_child(file_buttons[i], 1), file->name);
            if (file->directory) snprintf(text, sizeof(text), "%s", LV_SYMBOL_RIGHT);
            else format_size(file->bytes, text, sizeof(text));
            lv_label_set_text(lv_obj_get_child(file_buttons[i], 2), text);
            lv_obj_remove_flag(file_buttons[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(file_buttons[i], LV_STATE_DISABLED);
        } else lv_obj_add_flag(file_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- Activity shade. ---------- */
static unsigned usage_percent(uint64_t occupied, uint64_t total) {
    if (!total) return 0;
    if (occupied >= total) return 100;
    return (unsigned)(occupied * 100 / total);
}
static void shade_motion(void *obj, int32_t y) {
    if (y < -HOME_TOP) y = -HOME_TOP;
    if (y > 0) y = 0;
    shade_y = y; lv_obj_set_y(obj, y);
    lv_obj_set_style_bg_opa(shade_scrim, (y + HOME_TOP) * LV_OPA_60 / HOME_TOP, 0);
    if (y == -HOME_TOP) {
        lv_obj_add_flag(shade, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(shade_scrim, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(shade, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(shade_scrim, LV_OBJ_FLAG_HIDDEN);
    }
}
static void shade_set(bool open) {
    shade_drag = false;
    if (open) {
        hide_keyboard();
        lv_obj_move_to_index(shade_scrim, -1); lv_obj_move_to_index(shade, -1); lv_obj_move_to_index(home_strip, -1);
    }
    lv_anim_delete(shade, shade_motion);
    lv_anim_t animation; lv_anim_init(&animation);
    lv_anim_set_var(&animation, shade); lv_anim_set_exec_cb(&animation, shade_motion);
    lv_anim_set_values(&animation, shade_y, open ? 0 : -HOME_TOP); lv_anim_set_duration(&animation, ui_duration(220));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_start(&animation);
}
static bool shade_should_open(int start, int dx, int dy) {
    if (dx < -90 || dx > 90) return start == 0;
    return start == 0 ? dy > -50 : dy >= 70;
}
static void shade_touch(lv_event_t *event) {
    lv_indev_t *input = lv_indev_active(); if (!input) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        shade_drag = point.y <= 44 || (shade_y > -HOME_TOP && point.y < 88);
        if (shade_drag) {
            shade_origin = point; shade_start_y = shade_y;
            lv_anim_delete(shade, shade_motion);
            hide_keyboard();
            lv_obj_move_to_index(shade_scrim, -1); lv_obj_move_to_index(shade, -1); lv_obj_move_to_index(home_strip, -1);
        }
    } else if (code == LV_EVENT_PRESSING && shade_drag) {
        int dx = point.x - shade_origin.x;
        shade_motion(shade, dx >= -90 && dx <= 90 ? shade_start_y + point.y - shade_origin.y : shade_start_y);
    } else if (code == LV_EVENT_RELEASED && shade_drag) {
        shade_set(shade_should_open(shade_start_y, point.x - shade_origin.x, point.y - shade_origin.y));
    } else if (code == LV_EVENT_PRESS_LOST && shade_drag) shade_set(shade_start_y == 0);
}
static void shade_close(lv_event_t *event) { (void)event; shade_set(false); }

/* Backlight only: touch scanning, audio, networking and workers stay active.
 * A top-layer shield consumes all touches, including the wake gesture release. */
static void screen_off(lv_event_t *event) {
    (void)event;
    if (screen_blanked || screen_waking) return;
    screen_input = NULL;
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_display(input) == m3_board_display()) { screen_input = input; break; }
    }
    if (!m3_board_touch_ready() || !screen_input || !screen_timer) {
        set_text(shade_note, tr(T_SET_WAKE_UNAVAILABLE)); return;
    }
    m3_screen_wake_reset(&screen_wake);
    lv_obj_move_to_index(screen_shield, -1);
    lv_obj_remove_flag(screen_shield, LV_OBJ_FLAG_HIDDEN);
    if (m3_board_set_screen_on(false) != ESP_OK) {
        lv_obj_add_flag(screen_shield, LV_OBJ_FLAG_HIDDEN);
        set_text(shade_note, tr(T_SET_BRIGHTNESS_ERROR)); return;
    }
    screen_blanked = true; theme_wallpaper_sync();
}
static void screen_wake_tick(lv_timer_t *timer) {
    (void)timer;
    if ((!screen_blanked && !screen_waking) || !screen_input) return;
    uint32_t now = lv_tick_get();
    bool pressed = lv_indev_get_state(screen_input) == LV_INDEV_STATE_PRESSED;
    lv_point_t point; lv_indev_get_point(screen_input, &point);
    if (screen_blanked && m3_screen_wake_sample(&screen_wake, ui_preferences.wake_mode, now, pressed, point.x, point.y)) {
        esp_err_t err = m3_board_set_screen_on(true);
        /* The board driver fails with backlight on, so always remove the shield. */
        if (err != ESP_OK) set_text(settings_info, tr(T_SET_BRIGHTNESS_ERROR));
        screen_blanked = false; screen_waking = true; screen_release_at = now; theme_wallpaper_sync();
    }
    if (screen_waking) {
        if (pressed) screen_release_at = now;
        else if ((uint32_t)(now - screen_release_at) >= 150) {
            lv_obj_add_flag(screen_shield, LV_OBJ_FLAG_HIDDEN);
            screen_waking = false;
        }
    }
}
static void usage_update(unsigned index, uint64_t occupied, uint64_t total) {
    char text[64]; unsigned percent = usage_percent(occupied, total);
    lv_arc_set_value(usage_arcs[index], percent);
    if (total) snprintf(text, sizeof(text), "%u%%", percent);
    else snprintf(text, sizeof(text), "--");
    set_text(usage_values[index], text);
    if (total >= 1073741824ULL) snprintf(text, sizeof(text), "%.1f / %.1f GiB", occupied / 1073741824.0, total / 1073741824.0);
    else if (total) snprintf(text, sizeof(text), "%.1f / %.1f MiB", occupied / 1048576.0, total / 1048576.0);
    else snprintf(text, sizeof(text), "%s", tr(T_SHADE_UNAVAILABLE));
    set_text(usage_details[index], text);
}
static void job_update(unsigned index, const char *title, const char *detail, bool running) {
    char text[192]; snprintf(text, sizeof(text), "%s\n%s", title, detail);
    set_text(job_labels[index], text);
    set_ink(job_icons[index], running ? CYAN : MUTED);
    if (job_stops[index]) {
        if (running && (index != 4 || !snapshot.web_control_busy)) lv_obj_remove_state(job_stops[index], LV_STATE_DISABLED);
        else lv_obj_add_state(job_stops[index], LV_STATE_DISABLED);
    }
    if (!lv_color_eq(lv_obj_get_style_border_color(job_cards[index], 0), lv_color_hex(running ? CYAN : LINE)))
        lv_obj_set_style_border_color(job_cards[index], lv_color_hex(running ? CYAN : LINE), 0);
}


/* ---------- YouTube (PC relay at tube.audreaming.com) ---------- */
typedef struct { lv_obj_t *button, *thumb, *canvas, *badge, *title, *channel, *meta; } tube_row_t;
static M3_PSRAM_BSS tube_row_t tube_rows[M3_TUBE_ITEMS];
static lv_obj_t *tube_search_view, *tube_player_view, *tube_query, *tube_heading, *tube_back_search, *tube_status;
static lv_obj_t *tube_prev, *tube_next, *tube_page, *tube_pager;
static lv_obj_t *tube_video, *tube_overlay, *tube_seek, *tube_time, *tube_total, *tube_toggle, *tube_title, *tube_channel;
static lv_obj_t *tube_channel_button, *tube_player_status;
static lv_obj_t *tube_server_labels[2], *tube_download_labels[2], *tube_cancel_buttons[2], *tube_quality_tabs[2], *tube_save_button;
static M3_PSRAM_BSS m3_tube_list_t tube_list;
static unsigned seen_tube_list = ~0u, seen_tube_player = ~0u;
static uint16_t seen_tube_thumbs;
static int seen_tube_set = -1;
static bool tube_in_player, tube_pending;
static int64_t tube_pending_until;
static char tube_id[12], tube_channel_id[25], tube_last_query[M3_TUBE_QUERY_BYTES];
static unsigned tube_start_s, tube_duration_s;
static bool tube_live;

static void format_clock(unsigned seconds, char *text, size_t size) {
    if (seconds >= 3600) snprintf(text, size, "%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else snprintf(text, size, "%u:%02u", seconds / 60, seconds % 60);
}
/* Korean counts in 만/억, English in K/M/B. */
static void format_views(uint32_t views, char *text, size_t size) {
    if (m3_i18n_language() == M3_LANG_KO) {
        if (views >= 100000000u) snprintf(text, size, "조회수 %.1f억회", views / 100000000.0);
        else if (views >= 10000u) snprintf(text, size, "조회수 %.*f만회", views >= 100000u ? 0 : 1, views / 10000.0);
        else snprintf(text, size, "조회수 %lu회", (unsigned long)views);
    } else if (m3_i18n_language() == M3_LANG_JA) {
        if (views >= 100000000u) snprintf(text, size, "視聴回数 %.1f億回", views / 100000000.0);
        else if (views >= 10000u) snprintf(text, size, "視聴回数 %.*f万回", views >= 100000u ? 0 : 1, views / 10000.0);
        else snprintf(text, size, "視聴回数 %lu回", (unsigned long)views);
    } else {
        if (views >= 1000000000u) snprintf(text, size, "%.1fB views", views / 1e9);
        else if (views >= 1000000u) snprintf(text, size, "%.1fM views", views / 1e6);
        else if (views >= 1000u) snprintf(text, size, "%.0fK views", views / 1e3);
        else snprintf(text, size, "%lu views", (unsigned long)views);
    }
}
static void tube_show_player(bool player) {
    tube_in_player = player;
    if (player) { lv_obj_add_flag(tube_search_view, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(tube_player_view, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(tube_player_view, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(tube_search_view, LV_OBJ_FLAG_HIDDEN); }
    lv_obj_scroll_to_y(pages[APP_TUBE], 0, LV_ANIM_OFF);
}
/* Starts (or restarts at a position) the selected video. A playing source is
 * stopped first; the new request waits for the audio worker to become free. */
static void tube_start(unsigned seconds) {
    tube_start_s = seconds;
    m3_audio_state_t audio; m3_audio_state(&audio);
    if (audio.playing) m3_audio_stop();
    tube_pending = true; tube_pending_until = esp_timer_get_time() + 8000000;
    lv_label_set_text(tube_overlay, tr(T_TUBE_CONNECTING)); lv_obj_remove_flag(tube_overlay, LV_OBJ_FLAG_HIDDEN);
}
static void tube_stop(void) { tube_pending = false; m3_tube_player_t player; m3_tube_player(&player); if (player.active) m3_audio_stop(); }
static void tube_quality_changed(lv_event_t *event) {
    lv_obj_t *tabs = lv_event_get_user_data(event);
    unsigned selected = segment_value(tabs);
    m3_tube_quality((m3_tube_quality_t)selected);
    for (unsigned i = 0; i < 2; ++i) if (tube_quality_tabs[i]) segment_select(tube_quality_tabs[i], selected);
    m3_tube_player_t player; m3_tube_player(&player);
    if (tube_in_player && player.active && !player.audio_only) tube_start(player.position_ms / 1000);
}
static void tube_saved_folder(lv_event_t *event) {
    (void)event; tube_stop();
    selected_sd = true; strcpy(selected_folder, M3_TUBE_DOWNLOAD_FOLDER); selected_offset = 0;
    segment_select(storage_tabs, 1); browse_refresh(); show_app(APP_FILES);
}
static void tube_save(lv_event_t *event) {
    (void)event;
    if (!tube_id[0] || tube_live) return;
    tube_stop();
    if (!m3_tube_download(tube_id, lv_label_get_text(tube_title))) lv_label_set_text(tube_download_labels[1], tr(T_TUBE_SAVE_BUSY));
}
static void tube_cancel_save(lv_event_t *event) { (void)event; m3_tube_download_cancel(); }
static void tube_leave(void) { if (tube_in_player || tube_pending) tube_stop(); }
static void tube_submit(void) {
    const char *query = lv_textarea_get_text(tube_query);
    if (!query[0]) return;
    if (strlen(query) >= M3_TUBE_QUERY_BYTES || !m3_utf8_valid(query)) { set_text(tube_status, tr(T_TUBE_QUERY_LONG)); return; }
    hide_keyboard();
    snprintf(tube_last_query, sizeof(tube_last_query), "%s", query);
    if (!m3_tube_search(query, 1)) lv_label_set_text(tube_status, tr(T_TUBE_NOT_CONFIGURED));
}
static void tube_search_event(lv_event_t *event) { (void)event; tube_submit(); }
static void tube_chip(lv_event_t *event) {
    lv_textarea_set_text(tube_query, tr((m3_text_t)(uintptr_t)lv_event_get_user_data(event)));
    tube_submit();
}
static void tube_page_turn(lv_event_t *event) {
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    unsigned page = (unsigned)((int)tube_list.page + step);
    if (page < 1 || (step > 0 && !tube_list.more)) return;
    if (tube_list.channel) m3_tube_channel(tube_list.query, page); else m3_tube_search(tube_list.query, page);
    lv_obj_scroll_to_y(pages[APP_TUBE], 0, ui_animation());
}
static void tube_back_to_search(lv_event_t *event) {
    (void)event;
    if (tube_last_query[0]) { lv_textarea_set_text(tube_query, tube_last_query); m3_tube_search(tube_last_query, 1); }
}
static void tube_open(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= tube_list.count) return;
    const m3_tube_item_t *item = &tube_list.items[index];
    snprintf(tube_id, sizeof(tube_id), "%s", item->id);
    snprintf(tube_channel_id, sizeof(tube_channel_id), "%s", item->channel_id);
    tube_duration_s = item->duration;
    tube_live = item->live;
    lv_label_set_text(tube_title, item->title); lv_label_set_text(tube_channel, item->channel);
    if (tube_channel_id[0]) lv_obj_remove_state(tube_channel_button, LV_STATE_DISABLED);
    else lv_obj_add_state(tube_channel_button, LV_STATE_DISABLED);
    /* Show the thumbnail until the first frame arrives. */
    const uint8_t *thumb = m3_tube_thumb(&tube_list, index);
    if (thumb) lv_canvas_set_buffer(tube_video, (void *)thumb, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_style_transform_scale(tube_video, thumb ? 768 : 256, 0);
    lv_slider_set_range(tube_seek, 0, item->duration ? (int32_t)item->duration : 1);
    lv_slider_set_value(tube_seek, 0, LV_ANIM_OFF);
    seen_tube_player = ~0u;
    tube_show_player(true);
    tube_start(0);
}
static void tube_close(lv_event_t *event) { (void)event; tube_stop(); tube_show_player(false); }
static void tube_toggle_event(lv_event_t *event) {
    (void)event;
    m3_tube_player_t player; m3_tube_player(&player);
    if (player.active) m3_tube_pause(!player.paused);
    else if (!tube_pending) tube_start(player.position_ms / 1000 >= tube_duration_s && tube_duration_s ? 0 : player.position_ms / 1000);
}
static void tube_skip(lv_event_t *event) {
    m3_tube_player_t player; m3_tube_player(&player);
    int target = (int)(player.position_ms / 1000) + (int)(intptr_t)lv_event_get_user_data(event);
    if (target < 0) target = 0;
    if (tube_duration_s && target >= (int)tube_duration_s) target = (int)tube_duration_s - 1;
    lv_slider_set_value(tube_seek, target, LV_ANIM_OFF);
    tube_start((unsigned)target);
}
static void tube_seek_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    char text[16]; format_clock((unsigned)lv_slider_get_value(tube_seek), text, sizeof(text));
    if (code == LV_EVENT_VALUE_CHANGED) lv_label_set_text(tube_time, text);
    else if (code == LV_EVENT_RELEASED) tube_start((unsigned)lv_slider_get_value(tube_seek));
}
static void tube_channel_event(lv_event_t *event) {
    (void)event;
    if (!tube_channel_id[0]) return;
    tube_stop(); tube_show_player(false);
    m3_tube_channel(tube_channel_id, 1);
}
static void tube_frame_tick(lv_timer_t *timer) {
    (void)timer;
    if (current_app != APP_TUBE || !tube_in_player) return;
    const uint8_t *frame = m3_tube_frame();
    if (!frame) return;
    lv_obj_set_style_transform_scale(tube_video, 256, 0);
    lv_canvas_set_buffer(tube_video, (void *)frame, M3_TUBE_VIDEO_W, M3_TUBE_VIDEO_H, LV_COLOR_FORMAT_RGB565);
}
static void tube_render_rows(void) {
    char text[64];
    for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) {
        tube_row_t *row = &tube_rows[i];
        if (i >= tube_list.count) { lv_obj_add_flag(row->button, LV_OBJ_FLAG_HIDDEN); continue; }
        const m3_tube_item_t *item = &tube_list.items[i];
        lv_label_set_text(row->title, item->title);
        lv_label_set_text(row->channel, item->channel);
        if (item->live) snprintf(text, sizeof(text), "%s", tr(T_TUBE_LIVE));
        else format_clock(item->duration, text, sizeof(text));
        lv_label_set_text(row->badge, text);
        if (item->live || item->duration) lv_obj_remove_flag(row->badge, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(row->badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(row->badge, lv_color_hex(item->live ? 0xdc2626 : 0x000000), 0);
        if (item->views) { format_views(item->views, text, sizeof(text)); lv_label_set_text(row->meta, text); }
        else lv_label_set_text(row->meta, "");
        lv_obj_remove_flag(row->button, LV_OBJ_FLAG_HIDDEN);
    }
}
static void update_tube(const m3_audio_state_t *audio) {
    if (current_app == APP_TUBE) m3_tube_probe();
    m3_tube_server_t server; m3_tube_server(&server);
    m3_tube_download_t download; m3_tube_download_state(&download);
    const char *server_message = server.age_s > 45 ? tr(T_TUBE_SERVER_UNKNOWN) : status_text(server.message);
    if (server.checking) server_message = tr(T_TUBE_SERVER_CHECKING);
    for (unsigned i = 0; i < 2; ++i) {
        set_text(tube_server_labels[i], server_message);
        set_ink(tube_server_labels[i], server.ready && server.age_s <= 45 ? GREEN : MUTED);
        char progress[160];
        if (download.busy && download.total) snprintf(progress, sizeof(progress), "%s / %u%%", status_text(download.message), download.percent);
        else snprintf(progress, sizeof(progress), "%s", download.message[0] ? status_text(download.message) : tr(T_TUBE_SAVE_HINT));
        set_text(tube_download_labels[i], progress);
        if (download.busy) lv_obj_remove_flag(tube_cancel_buttons[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tube_cancel_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (download.busy || tube_live || !tube_id[0]) lv_obj_add_state(tube_save_button, LV_STATE_DISABLED);
    else lv_obj_remove_state(tube_save_button, LV_STATE_DISABLED);
    m3_tube_list(&tube_list);
    if (tube_list.revision != seen_tube_list) {
        seen_tube_list = tube_list.revision;
        char text[512];
        if (tube_list.thumb_set != seen_tube_set) {
            seen_tube_set = tube_list.thumb_set; seen_tube_thumbs = 0;
            for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) lv_obj_add_flag(tube_rows[i].canvas, LV_OBJ_FLAG_HIDDEN);
        }
        tube_render_rows();
        if (tube_list.channel) snprintf(text, sizeof(text), tr(T_TUBE_CHANNEL_OF), tube_list.title[0] ? tube_list.title : tube_list.query);
        else if (tube_list.count) snprintf(text, sizeof(text), tr(T_TUBE_RESULTS_FOR), tube_list.query);
        else text[0] = 0;
        lv_label_set_text(tube_heading, text);
        if (tube_list.channel && tube_last_query[0]) lv_obj_remove_flag(tube_back_search, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tube_back_search, LV_OBJ_FLAG_HIDDEN);
        const char *status = !m3_tube_configured() ? tr(T_TUBE_NOT_CONFIGURED) :
            tube_list.status == M3_TUBE_LIST_LOADING ? tr(T_TUBE_LOADING) :
            tube_list.status == M3_TUBE_LIST_ERROR ? status_text(tube_list.message) :
            tube_list.status == M3_TUBE_LIST_READY && !tube_list.count ? tr(T_TUBE_NO_RESULTS) :
            tube_list.status == M3_TUBE_LIST_IDLE ? tr(T_TUBE_EMPTY) : "";
        lv_label_set_text(tube_status, status);
        if (status[0]) lv_obj_remove_flag(tube_status, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(tube_status, LV_OBJ_FLAG_HIDDEN);
        set_ink(tube_status, tube_list.status == M3_TUBE_LIST_ERROR ? RED : MUTED);
        snprintf(text, sizeof(text), tr(T_TUBE_PAGE), tube_list.page);
        lv_label_set_text(tube_page, text);
        if (tube_list.count && (tube_list.page > 1 || tube_list.more)) lv_obj_remove_flag(tube_pager, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tube_pager, LV_OBJ_FLAG_HIDDEN);
        if (tube_list.page > 1) lv_obj_remove_state(tube_prev, LV_STATE_DISABLED); else lv_obj_add_state(tube_prev, LV_STATE_DISABLED);
        if (tube_list.more) lv_obj_remove_state(tube_next, LV_STATE_DISABLED); else lv_obj_add_state(tube_next, LV_STATE_DISABLED);
    }
    if (tube_list.thumbs != seen_tube_thumbs) {
        for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) {
            const uint8_t *thumb = m3_tube_thumb(&tube_list, i);
            if (!thumb || (seen_tube_thumbs & (1u << i))) continue;
            lv_canvas_set_buffer(tube_rows[i].canvas, (void *)thumb, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H, LV_COLOR_FORMAT_RGB565);
            lv_obj_remove_flag(tube_rows[i].canvas, LV_OBJ_FLAG_HIDDEN);
        }
        seen_tube_thumbs = tube_list.thumbs;
    }
    m3_tube_player_t player; m3_tube_player(&player);
    /* A YouTube Music song is not this player's video. */
    if (player.audio_only) player.active = player.buffering = player.paused = false;
    if (tube_pending && !audio->playing && !player.active) {
        tube_pending = false;
        if (download.busy || !m3_tube_play(tube_id, tube_start_s)) lv_label_set_text(tube_overlay, tr(download.busy ? T_TUBE_SAVE_BUSY : T_TUBE_NOT_CONFIGURED));
    } else if (tube_pending && esp_timer_get_time() > tube_pending_until) {
        tube_pending = false; lv_label_set_text(tube_overlay, tr(T_TUBE_BUSY));
    }
    if (player.revision != seen_tube_player || player.active) {
        seen_tube_player = player.revision;
        char text[64];
        if (player.active && !strcmp(player.id, tube_id)) {
            if (player.duration_s && player.duration_s != tube_duration_s) {
                tube_duration_s = player.duration_s; lv_slider_set_range(tube_seek, 0, (int32_t)tube_duration_s);
            }
            if (player.channel_id[0] && !tube_channel_id[0]) {
                snprintf(tube_channel_id, sizeof(tube_channel_id), "%s", player.channel_id);
                lv_obj_remove_state(tube_channel_button, LV_STATE_DISABLED);
            }
            if (!lv_obj_has_state(tube_seek, LV_STATE_PRESSED)) {
                lv_slider_set_value(tube_seek, (int32_t)(player.position_ms / 1000), LV_ANIM_OFF);
                format_clock(player.position_ms / 1000, text, sizeof(text)); set_text(tube_time, text);
            }
        }
        format_clock(tube_duration_s, text, sizeof(text)); set_text(tube_total, tube_duration_s ? text : "--:--");
        bool playing = player.active && !player.paused;
        lv_label_set_text(lv_obj_get_child(tube_toggle, 0), playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        const char *overlay = tube_pending ? tr(T_TUBE_CONNECTING) : !player.active ? NULL :
            player.buffering ? tr(T_TUBE_BUFFERING) : player.paused ? tr(T_TUBE_PAUSED) : !player.video ? tr(T_TUBE_AUDIO_ONLY) : NULL;
        if (overlay) {
            if (player.buffering && !tube_pending) snprintf(text, sizeof(text), "%s  %u%%", overlay, player.buffer_percent);
            else snprintf(text, sizeof(text), "%s", overlay);
            set_text(tube_overlay, text); lv_obj_remove_flag(tube_overlay, LV_OBJ_FLAG_HIDDEN);
        } else if (!lv_obj_has_flag(tube_overlay, LV_OBJ_FLAG_HIDDEN) && strcmp(lv_label_get_text(tube_overlay), tr(T_TUBE_BUSY)) &&
                   strcmp(lv_label_get_text(tube_overlay), tr(T_TUBE_NOT_CONFIGURED))) lv_obj_add_flag(tube_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    activity_set(tube_wait, tube_in_player && (tube_pending || (player.active && player.buffering)));
    if (tube_in_player) set_text(tube_player_status, status_text(audio->message));
}
#include "m3_ytmusic_ui.inc"

static void service_stop(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index == 1) {
        radio_pending = tube_pending = ytm_pending = false;
        ytm_queue_count = 0;
        m3_recorder_state_t recorder; m3_recorder_state(&recorder);
        if (recorder.busy) m3_recorder_finish(); /* Preserve a recording by saving it. */
        else { m3_music_stop(); m3_audio_stop(); }
    } else if (index == 2) voice_cancel(NULL);
    else if (index == 4) {
        bool accepted = m3_runtime_web(M3_WEB_STOP_ALL);
        set_text(shade_note, tr(accepted ? T_SERVICE_STOPPING : T_SERVICE_BUSY));
    } else if (index == 5) m3_agent_cancel(false);
}
static void web_control(lv_event_t *event) {
    m3_web_action_t action = (m3_web_action_t)(uintptr_t)lv_event_get_user_data(event);
    bool accepted = m3_runtime_web(action);
    set_text(web_control_info, tr(accepted ? T_SERVICE_STOPPING : T_SERVICE_BUSY));
}


static void update_activity(const m3_agent_state_t *agent, const m3_voice_state_t *voice, const m3_audio_state_t *audio) {
    unsigned phase = tts_phase(audio);
    bool thinking = agent->busy && lv_obj_has_flag(agent_thinking, LV_OBJ_FLAG_HIDDEN);
    bool done = !agent->busy && !lv_obj_has_flag(agent_thinking, LV_OBJ_FLAG_HIDDEN);
    activity_set(agent_thinking, agent->busy);
    /* The previous answer fades while a new one is generated (text opacity needs no layer). */
    if (thinking || done) lv_obj_set_style_text_opa(agent_answer, thinking ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (thinking) lv_obj_scroll_to_y(agent_output, 0, ui_animation());
    activity_set(agent_voice, phase != 0);
    if (voice_target && voice->recording) lv_obj_remove_flag(voice_meter, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(voice_meter, LV_OBJ_FLAG_HIDDEN);
    activity_set(voice_think, voice_target && voice->busy && !voice->recording);
    activity_set(tts_wait, phase == 1);
    activity_set(tts_talk, phase == 2);
}
/* Each tab retains its own meaning, including when its address is unavailable. */
static void web_tab_changed(lv_event_t *event) {
    (void)event; web_address_kind = segment_value(web_tabs); web_qr_dirty = true;
}
static void web_retry_ipv6(lv_event_t *event) {
    (void)event;
    bool accepted = m3_runtime_wifi(M3_WIFI_IPV6, NULL);
    set_text(web_hint, tr(accepted ? T_WEB_RETRY_SENT : T_WIFI_BUSY));
    web_qr_dirty = true;
}
static m3_text_t web_explanation(void) {
    if (snapshot.web_policy == 2) return T_WEB_STOPPED_ALL;
    if (web_address_kind == M3_WEB_DOMAIN) {
        if (snapshot.web_policy == 1 || !strcmp(snapshot.cloud_status, "stopped")) return T_WEB_STOPPED_DOMAIN;
        if (snapshot.cloud) return T_WEB_DOMAIN_READY;
        if (!strcmp(snapshot.cloud_status, "factory_missing")) return T_WEB_PROVISION;
        if (!strcmp(snapshot.cloud_status, "waiting_wifi")) return T_WEB_NO_WIFI;
        if (!strcmp(snapshot.cloud_status, "waiting_time")) return T_WEB_CLOCK;
        if (!strcmp(snapshot.cloud_status, "waiting_memory")) return T_WEB_MEMORY;
        if (!strcmp(snapshot.cloud_status, "paused_ai")) return T_WEB_PAUSED;
        return T_WEB_DOMAIN_WAIT;
    }
    if (web_address_kind == M3_WEB_IPV6) {
        if (snapshot.ipv6_url[0] && snapshot.mode == NET_MODE_STA_CONNECTED) return T_WEB_IPV6_HINT;
        if (!strcmp(snapshot.ipv6_status, "disabled")) return T_WEB_IPV6_DISABLED;
        if (snapshot.mode != NET_MODE_STA_CONNECTED) return T_WEB_NO_WIFI;
        if (!strcmp(snapshot.ipv6_status, "no_global_prefix")) return T_WEB_NO_PREFIX;
        return T_WEB_IPV6_WAIT;
    }
    return web_address_kind == M3_WEB_MDNS ? T_WEB_MDNS_HINT : T_WEB_IPV4_HINT;
}
static void update_web(void) {
    char text[96];
    const char *url = m3_web_address_url(&snapshot, web_address_kind);
    if (web_qr_dirty || strcmp(seen_web_url, url)) {
        web_qr_dirty = false;
        snprintf(seen_web_url, sizeof(seen_web_url), "%s", url);
        web_qr_ok = url[0] && lv_qrcode_update(web_qr, url, strlen(url)) == LV_RESULT_OK;
        if (web_qr_ok) lv_obj_remove_flag(web_frame, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(web_frame, LV_OBJ_FLAG_HIDDEN);
    }
    set_text(web_url, url[0] ? url : tr(T_QR_WAIT));
    bool ready = snapshot.web && url[0] && (web_address_kind != M3_WEB_DOMAIN || snapshot.cloud);
    set_text(web_state, tr(snapshot.web_policy == 2 ? T_WEB_STOPPED_ALL : !snapshot.web ? T_WEB_STARTING : ready ? T_WEB_RUNNING : T_WEB_WAIT));
    set_bg(web_dot, ready && web_qr_ok ? GREEN : AMBER);
    set_text(web_hint, tr(url[0] && !web_qr_ok ? T_WEB_QR_ERROR :
        web_address_kind == M3_WEB_IPV6 && snapshot.wifi_busy ? T_WEB_RETRY_SENT : web_explanation()));
    if (web_address_kind == M3_WEB_IPV6) lv_obj_remove_flag(web_retry, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(web_retry, LV_OBJ_FLAG_HIDDEN);
    if (snapshot.wifi_busy || snapshot.mode != NET_MODE_STA_CONNECTED) lv_obj_add_state(web_retry, LV_STATE_DISABLED);
    else lv_obj_remove_state(web_retry, LV_STATE_DISABLED);
    snprintf(text, sizeof(text), tr(T_JOB_REQUESTS), snapshot.requests);
    set_text(web_requests, text);
    for (unsigned i = 0; i < 4; ++i) {
        bool enabled = !snapshot.web_control_busy;
        if (i == M3_WEB_STOP_DOMAIN) enabled = enabled && snapshot.web && snapshot.web_policy == 0;
        if (i == M3_WEB_STOP_ALL) enabled = enabled && snapshot.web;
        if (i == M3_WEB_START_DOMAIN) enabled = enabled && snapshot.web && snapshot.web_policy == 1;
        if (i == M3_WEB_START_ALL) enabled = enabled && !snapshot.web;
        if (enabled) lv_obj_remove_state(web_controls[i], LV_STATE_DISABLED);
        else lv_obj_add_state(web_controls[i], LV_STATE_DISABLED);
    }
    static unsigned seen_web_control;
    if (seen_web_control != snapshot.web_control_revision) {
        seen_web_control = snapshot.web_control_revision;
        set_text(web_control_info, tr(snapshot.web_control_result == ESP_OK ? T_SERVICE_DONE : T_SERVICE_BUSY));
        set_text(shade_note, tr(snapshot.web_control_result == ESP_OK ? T_SERVICE_DONE : T_SERVICE_BUSY));
    }
}


/* 40 ms: bar heights from the live microphone level. A centre-weighted profile with a
 * slow ripple keeps the shape lively, but silence always draws a flat line. */
static void voice_meter_tick(lv_timer_t *timer) {
    (void)timer;
    if (!voice_target || lv_obj_has_flag(voice_meter, LV_OBJ_FLAG_HIDDEN)) return;
    static const uint8_t profile[METER_BARS] = {34, 48, 62, 76, 90, 100, 90, 76, 62, 48, 34};
    static const uint8_t ripple[8] = {100, 86, 94, 78, 97, 84, 91, 80};
    unsigned level = m3_voice_level();
    ++meter_phase;
    for (unsigned i = 0; i < METER_BARS; ++i) {
        unsigned shape = profile[i] * ripple[(meter_phase + i * 3) % 8] / 100;
        int32_t height = METER_MIN + (int32_t)((METER_MAX - METER_MIN) * level * shape / 10000);
        if (lv_obj_get_height(voice_meter_bars[i]) != height) lv_obj_set_height(voice_meter_bars[i], height);
    }
    /* Quiet input shows grey, speech red; near clipping amber. */
    uint32_t color = level < 12 ? MUTED : level > 92 ? AMBER : RED;
    for (unsigned i = 0; i < METER_BARS; ++i) set_bg(voice_meter_bars[i], color);
    char text[24]; int dbfs = m3_voice_dbfs();
    if (dbfs <= -90) snprintf(text, sizeof(text), "-- dB");
    else snprintf(text, sizeof(text), "%d dB", dbfs);
    set_text(voice_db, text);
}

/* ---------- Periodic refresh (250 ms). ---------- */
static const char *mode_text(void) {
    return tr(snapshot.mode == NET_MODE_STA_CONNECTED ? T_NET_ONLINE : snapshot.mode == NET_MODE_AP_CAPTIVE ? T_NET_SETUP : T_NET_STARTING);
}
static uint32_t mode_color(void) {
    return snapshot.mode == NET_MODE_STA_CONNECTED ? GREEN : snapshot.mode == NET_MODE_AP_CAPTIVE ? MAGENTA : AMBER;
}
static void update_clock(char *text, size_t size) {
    time_t now = time(NULL);
    if (now >= 1735689600) {
        /* Convert to Korea time without changing the process-wide TLS timezone. */
        now += 9 * 3600;
        struct tm local; gmtime_r(&now, &local);
        strftime(text, size, "%H:%M", &local);
        set_text(status_clock, text); set_text(home_clock, text);
        static const char *const days_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char *const days_ko[] = {"일", "월", "화", "수", "목", "금", "토"};
        static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        if (m3_i18n_language() == M3_LANG_KO)
            snprintf(text, size, "%d년 %d월 %d일 %s요일", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, days_ko[local.tm_wday % 7]);
        else if (m3_i18n_language() == M3_LANG_JA) {
            static const char *const days_ja[] = {"日", "月", "火", "水", "木", "金", "土"};
            snprintf(text, size, "%d年%d月%d日 %s曜日", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, days_ja[local.tm_wday % 7]);
        }
        else snprintf(text, size, "%s, %s %d %d", days_en[local.tm_wday % 7], months[local.tm_mon % 12], local.tm_mday, local.tm_year + 1900);
        set_text(home_date, text);
    } else {
        set_text(status_clock, "--:--"); set_text(home_clock, "--:--");
        set_text(home_date, tr(T_HOME_NO_TIME));
    }
}
static void update_home(const m3_audio_state_t *audio) {
    char text[384];
    bool online = snapshot.mode == NET_MODE_STA_CONNECTED;
    snprintf(text, sizeof(text), "%s  " LV_SYMBOL_BULLET "  %s", mode_text(), snapshot.ip[0] ? snapshot.ip : tr(T_NET_PENDING));
    set_text(home_net, text);
    set_ink(home_net_icon, mode_color());
    if (audio->playing) snprintf(text, sizeof(text), "%s  %s", LV_SYMBOL_AUDIO, status_text(audio->message));
    else if (online && snapshot.url[0]) snprintf(text, sizeof(text), tr(T_HOME_WEB), snapshot.url);
    else snprintf(text, sizeof(text), "%s", tr(snapshot.mode == NET_MODE_AP_CAPTIVE ? T_HOME_SETUP : T_HOME_WAITING));
    set_text(home_detail, text);
}
static void power_visible(lv_obj_t *obj, bool visible) {
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void update_power(const m3_power_state_t *p) {
    bool battery = !p->battery_known || p->battery_present;
    bool measured = p->battery_known && p->battery_present && p->percent_known;
    unsigned percent = p->percent > 100 ? 100 : p->percent;
    bool charging = p->battery_known && p->battery_present && p->charge_known && p->charging &&
        (!p->wired_known || p->wired);
    power_visible(power_plug, p->wired_known && p->wired);
    power_visible(power_battery, battery);
    power_visible(power_percent, battery);
    power_visible(power_fill, measured && percent > 0);
    power_visible(power_charge, charging);
    lv_obj_set_width(power_fill, percent ? (int)((22u * percent + 99u) / 100u) : 1);
    uint32_t color = charging ? CYAN : measured && percent <= 20 ? RED : TEXT;
    set_bg(power_fill, color); set_ink(power_percent, measured ? color : MUTED);
    char text[8];
    if (measured) snprintf(text, sizeof(text), "%u%%", percent);
    else snprintf(text, sizeof(text), "--");
    set_text(power_percent, text);
}
static void update_metrics(lv_timer_t *timer) {
    update_preferences();
    (void)timer;
    m3_runtime_state(&snapshot);
    update_weather();
    phone_update();
    update_music();
    static M3_PSRAM_BSS m3_agent_state_t agent; m3_agent_state(&agent);
    if (agent.revision != seen_agent) {
        seen_agent = agent.revision;
        set_text(agent_info, status_text(agent.message));
        lv_label_set_text(agent_answer, agent.answer[0] ? agent.answer : tr(T_AGENT_EMPTY));
        lv_obj_set_style_text_color(agent_answer, lv_color_hex(agent.answer[0] ? TEXT : MUTED), 0);
        lv_label_set_text(agent_trace, agent.tools);
        if (agent.tools[0]) lv_obj_remove_flag(agent_trace, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(agent_trace, LV_OBJ_FLAG_HIDDEN);
        if (agent.answer[0]) lv_obj_scroll_to_y(agent_output, 0, LV_ANIM_OFF);
    }
    set_bg(agent_dot, agent.busy ? AMBER : agent.key_ready ? GREEN : RED);
    static M3_PSRAM_BSS m3_voice_state_t voice; m3_voice_state(&voice);
    segment_t *voice_segment = segment_of(voice_provider), *tts_segment = segment_of(tts_provider);
    if (voice_segment && voice_segment->selected != voice.provider) segment_select(voice_provider, voice.provider);
    unsigned selected_tts = m3_audio_tts_selected();
    if (tts_segment && tts_segment->selected != selected_tts) segment_select(tts_provider, selected_tts);
    if (voice.revision != seen_voice) {
        seen_voice = voice.revision;
        if (voice_target) {
            lv_label_set_text(voice_info, status_text(voice.message));
            lv_label_set_text(voice_text, voice.text);
        }
    }
    set_bg(voice_dot, voice.recording ? RED : voice.busy ? AMBER : MUTED);
    update_wifi();
    m3_audio_state_t audio; m3_audio_state(&audio);
    if (radio_pending && !audio.playing) { radio_pending = false; start_radio(NULL); }
    else if (radio_pending && esp_timer_get_time() > radio_pending_until) { radio_pending = false; lv_label_set_text(radio_info, tr(T_RADIO_FAIL)); }
    if (memory_game && memory_second >= 0 && (uint64_t)esp_timer_get_time() >= memory_hide_at) {
        memory_visible[memory_first] = memory_visible[memory_second] = false;
        memory_first = memory_second = -1; render_game();
    }
    size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = esp_psram_get_size();
    unsigned used = psram_total ? (unsigned)((psram_total - psram) * 100 / psram_total) : 0;
    static char text[768]; /* UI task only; keeps this 250 ms path off the 16 KiB stack */
    set_text(status, mode_text());
    update_power(&snapshot.power);
    reader_tick();
    unsigned levels = snapshot.mode == NET_MODE_STA_CONNECTED ? wifi_bars(snapshot.rssi) : 0;
    bool ap = snapshot.mode == NET_MODE_AP_CAPTIVE;
    for (unsigned i = 0; i < 4; ++i)
        set_bg(wifi_levels[i], ap ? MAGENTA : i < levels ? TEXT : LINE);
    if (audio.playing) lv_obj_remove_flag(status_audio, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(status_audio, LV_OBJ_FLAG_HIDDEN);
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    usage_update(0, internal_total > internal ? internal_total - internal : 0, internal_total);
    usage_update(1, psram_total > psram ? psram_total - psram : 0, psram_total);
    usage_update(2, snapshot.sd_capacity.used, snapshot.sd && snapshot.sd_capacity.error == ESP_OK ? snapshot.sd_capacity.total : 0);
    if (snapshot.sd && snapshot.sd_capacity_stale) {
        if (snapshot.sd_capacity.total && snapshot.sd_capacity.error == ESP_OK) {
            snprintf(text, sizeof(text), "%u%% *", usage_percent(snapshot.sd_capacity.used, snapshot.sd_capacity.total));
            set_text(usage_values[2], text);
        } else set_text(usage_details[2], tr(T_SD_BUSY));
    }
    job_update(0, tr(T_JOB_APP), tr(app_names[current_app]), current_app != APP_HOME);
    static const char *const app_glyphs[APP_COUNT] = {LV_SYMBOL_HOME, LV_SYMBOL_WIFI, LV_SYMBOL_DIRECTORY,
        LV_SYMBOL_CALL, LV_SYMBOL_CHARGE, LV_SYMBOL_SETTINGS, LV_SYMBOL_AUDIO, LV_SYMBOL_LIST,
        LV_SYMBOL_VOLUME_MAX, "AI", LV_SYMBOL_VIDEO, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE, "=", LV_SYMBOL_PLAY, LV_SYMBOL_FILE, "W"};
    set_text(job_icons[0], app_glyphs[current_app]);
    job_update(1, tr(T_JOB_AUDIO), status_text(audio.message), (audio.playing || radio_pending || tube_pending || ytm_pending) && !voice.busy);
    job_update(2, tr(T_JOB_VOICE), voice.busy ? status_text(voice.message) : tr(voice.text[0] ? T_JOB_REVIEW : T_JOB_WAITING), voice.busy);
    job_update(3, tr(T_JOB_WIFI), snapshot.wifi_busy ? status_text(snapshot.wifi_message) : mode_text(),
        snapshot.wifi_busy || snapshot.mode == NET_MODE_STA_CONNECTED || snapshot.mode == NET_MODE_AP_CAPTIVE);
    snprintf(text, sizeof(text), tr(T_JOB_REQUESTS), snapshot.requests);
    job_update(4, tr(T_JOB_WEB), snapshot.web ? text : tr(T_JOB_NOT_STARTED), snapshot.web);
    job_update(5, tr(T_JOB_AGENT), status_text(agent.message), agent.busy);
    if (snapshot.cpu_valid && snapshot.cpu_revision != seen_cpu) {
        seen_cpu = snapshot.cpu_revision;
        for (unsigned i = 0; i < 2; ++i) {
            snprintf(text, sizeof(text), "HP%u  %u%%", i, snapshot.cpu_percent[i]); set_text(cpu_values[i], text);
            lv_chart_set_next_value(cpu_chart, cpu_series[i], snapshot.cpu_percent[i]);
        }
    }
    update_clock(text, sizeof(text));
    update_home(&audio);
    update_calendar();
    update_photos();
    snprintf(text, sizeof(text), "%s\n%.0f KiB\n%s\n%.1f / %.0f MiB", tr(T_MON_INTERNAL), internal / 1024.0,
        tr(T_MON_PSRAM), psram / 1048576.0, psram_total / 1048576.0);
    set_text(memory, text); lv_bar_set_value(heap_bar, used, ui_animation());
    if (++chart_tick % 4 == 0) {
        lv_chart_set_next_value(chart, heap_series, (int)(internal / 1024));
        lv_chart_set_next_value(chart, psram_series, (int)(psram / 65536));
    }
    snprintf(text, sizeof(text), tr(T_MON_STATS), snapshot.requests, snapshot.disconnects, snapshot.rssi,
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024.0,
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024.0);
    set_text(monitor_info, text);
    set_text(net_title, mode_text());
    set_bg(net_dot, mode_color());
    snprintf(text, sizeof(text), "IPv4  %s\n%s  %d dBm\n%s\n%s", snapshot.ip[0] ? snapshot.ip : tr(T_NET_PENDING),
        tr(T_NET_SIGNAL), snapshot.rssi, tr(snapshot.cloud ? T_NET_CLOUD : T_NET_LOCAL), status_text(snapshot.message));
    set_text(network, text);
    if (snapshot.url[0] && strcmp(seen_url, snapshot.url)) {
        snprintf(seen_url, sizeof(seen_url), "%s", snapshot.url);
        lv_qrcode_update(qr, seen_url, strlen(seen_url)); lv_label_set_text(qr_url, seen_url);
        lv_obj_remove_flag(qr, LV_OBJ_FLAG_HIDDEN);
    }
    if (seen_files != snapshot.file_revision) { seen_files = snapshot.file_revision; update_files(); }
    update_recorder(&audio);
    lv_bar_set_value(radio_bar, audio.progress, ui_animation());
    if (strcmp(seen_audio, audio.message)) {
        snprintf(seen_audio, sizeof(seen_audio), "%s", audio.message);
        if (!radio_pending) lv_label_set_text(radio_info, status_text(audio.message));
        lv_label_set_text(tts_info, status_text(audio.message));
    }
    /* Volume can also change from the AI agent; mirror it unless a slider is held. */
    if (audio.volume != shown_volume) {
        bool held = false;
        for (unsigned i = 0; i < VOLUME_ROWS; ++i) held |= lv_obj_has_state(volume_sliders[i], LV_STATE_PRESSED);
        if (!held) shown_volume = audio.volume;
        snprintf(text, sizeof(text), "%u%%", audio.volume);
        for (unsigned i = 0; i < VOLUME_ROWS; ++i) {
            if (!held) lv_slider_set_value(volume_sliders[i], audio.volume, LV_ANIM_OFF);
            lv_label_set_text(volume_values[i], text);
            set_ink(volume_values[i], audio.volume > 100 ? AMBER : TEXT);
        }
    }
    esp_chip_info_t chip; esp_chip_info(&chip);
    snprintf(text, sizeof(text), tr(T_ABOUT),
        chip.cores, chip.revision / 100, chip.revision % 100, esp_get_idf_version(),
        tr(m3_board_touch_ready() ? T_READY : T_NOT_DETECTED), tr(audio.ready ? T_READY : T_NOT_READY),
        tr(snapshot.sd ? T_MOUNTED : T_NOT_MOUNTED), psram_total / 1048576.0);
    set_text(system_info, text);
    update_tube(&audio);
    update_ytm(&audio);
    update_activity(&agent, &voice, &audio);
    update_web();
    int requested_app = m3_agent_take_open_app();
    if (requested_app >= 0 && requested_app < APP_COUNT) show_app((app_id_t)requested_app);
}

/* ---------- App icons: symbol glyphs on colored tiles. ---------- */
static lv_obj_t *icon_part(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *part = bare(parent);
    lv_obj_set_pos(part, x, y); lv_obj_set_size(part, w, h);
    lv_obj_set_style_radius(part, radius, 0); lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(part, lv_color_hex(0xffffff), 0);
    return part;
}
static const struct { app_id_t app; uint32_t top, bottom; const char *glyph; } app_icons[] = {
    {APP_MONITOR, 0x2dd4bf, 0x0f766e, LV_SYMBOL_CHARGE},
    {APP_AGENT, 0x8b5cf6, 0x5b21b6, "AI"},
    {APP_WIFI, 0x38bdf8, 0x1d4ed8, LV_SYMBOL_WIFI},
    {APP_RADIO, 0xfb923c, 0xc2410c, NULL},
    {APP_GAMES, 0x4ade80, 0x15803d, NULL},
    {APP_FILES, 0xfbbf24, 0xb45309, LV_SYMBOL_DIRECTORY},
    {APP_RECORDER, 0xf472b6, 0xbe185d, NULL},
    {APP_TTS, 0x818cf8, 0x4338ca, "가"},
    {APP_SYSTEM, 0x94a3b8, 0x475569, LV_SYMBOL_SETTINGS},
    {APP_TUBE, 0xf87171, 0xb91c1c, LV_SYMBOL_PLAY},
    {APP_YTMUSIC, 0xff0033, 0xff0033, LV_SYMBOL_PLAY},
    {APP_MUSIC, 0x22d3ee, 0x0369a1, LV_SYMBOL_AUDIO},
    {APP_GALLERY, 0x38bdf8, 0x7c3aed, LV_SYMBOL_IMAGE},
    {APP_CALCULATOR, 0x34d399, 0x047857, "="},
    {APP_READER, 0xd6b98d, 0x785e3e, LV_SYMBOL_FILE},
    {APP_WEATHER, 0x38bdf8, 0x0e7490, "W"},
};
static void draw_app_icon(lv_obj_t *icon, unsigned index) {
    lv_obj_set_style_bg_color(icon, lv_color_hex(app_icons[index].top), 0);
    lv_obj_set_style_bg_grad_color(icon, lv_color_hex(app_icons[index].bottom), 0);
    lv_obj_set_style_bg_grad_dir(icon, LV_GRAD_DIR_VER, 0);
    if (app_icons[index].app == APP_YTMUSIC) {
        lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
        lv_obj_t *ring = bare(icon); lv_obj_set_pos(ring, 14, 14); lv_obj_set_size(ring, 48, 48);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 3, 0); lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0xffffff), 0);
        lv_obj_t *play = label(icon, LV_SYMBOL_PLAY, 0xffffff); lv_obj_set_width(play, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(play, FONT_ICON, 0); lv_obj_align(play, LV_ALIGN_CENTER, 2, 0);
        return;
    }
    if (app_icons[index].app == APP_RADIO) {
        icon_part(icon, 17, 28, 42, 30, 6);
        icon_part(icon, 24, 18, 33, 3, 1);
        lv_obj_t *speaker = bare(icon); lv_obj_set_pos(speaker, 23, 35); lv_obj_set_size(speaker, 17, 17);
        lv_obj_set_style_bg_color(speaker, lv_color_hex(0xc2410c), 0); lv_obj_set_style_bg_opa(speaker, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(speaker, LV_RADIUS_CIRCLE, 0);
        for (unsigned i = 0; i < 3; ++i) { lv_obj_t *dial = icon_part(icon, 45, 35 + i * 6, 8, 2, 0); lv_obj_set_style_bg_color(dial, lv_color_hex(0xc2410c), 0); }
        return;
    }
    if (app_icons[index].app == APP_RECORDER) {
        icon_part(icon, 31, 16, 14, 30, 7);
        lv_obj_t *cradle = bare(icon); lv_obj_set_pos(cradle, 23, 29); lv_obj_set_size(cradle, 30, 25);
        lv_obj_set_style_border_width(cradle, 3, 0); lv_obj_set_style_border_color(cradle, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_side(cradle, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_radius(cradle, 13, 0);
        icon_part(icon, 36, 54, 4, 7, 1); icon_part(icon, 28, 61, 20, 3, 1);
        return;
    }
    if (!app_icons[index].glyph) {
        for (int i = 0; i < 4; ++i) {
            lv_obj_t *part = icon_part(icon, 21 + (i % 2) * 20, 21 + (i / 2) * 20, 16, 16, 4);
            if (i == 3) lv_obj_set_style_bg_opa(part, LV_OPA_60, 0);
        }
        return;
    }
    lv_obj_t *glyph = lv_label_create(icon);
    lv_label_set_text(glyph, app_icons[index].glyph);
    lv_obj_set_style_text_color(glyph, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(glyph, app_icons[index].app == APP_TTS ? FONT_TITLE : FONT_ICON, 0);
    if (app_icons[index].app == APP_TTS) {
        /* The title font is 22 px; scale the single glyph around its center to icon size. */
        lv_obj_set_style_transform_scale(glyph, 360, 0);
        lv_obj_set_style_transform_pivot_x(glyph, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(glyph, LV_PCT(50), 0);
    }
    lv_obj_center(glyph);
    if (app_icons[index].app == APP_TUBE) lv_obj_align(glyph, LV_ALIGN_CENTER, 3, 0);
}

/* ---------- Activity shade. ---------- */
static void create_shade(lv_obj_t *screen) {
    shade_scrim = lv_obj_create(screen); lv_obj_remove_style_all(shade_scrim); lv_obj_set_pos(shade_scrim, 0, 0);
    lv_obj_set_size(shade_scrim, 480, HOME_TOP); lv_obj_set_style_bg_color(shade_scrim, lv_color_hex(0), 0);
    lv_obj_remove_flag(shade_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(shade_scrim, shade_close, LV_EVENT_CLICKED, NULL);
    shade = lv_obj_create(screen); lv_obj_remove_style_all(shade); lv_obj_set_size(shade, 480, HOME_TOP);
    lv_obj_set_style_bg_opa(shade, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shade, lv_color_hex(SHADE_TOP), 0);
    lv_obj_set_style_bg_grad_color(shade, lv_color_hex(SHADE_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(shade, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(shade, 24, 0); lv_obj_set_style_text_color(shade, lv_color_hex(TEXT), 0);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(shade, shade_touch, LV_EVENT_ALL, NULL);
    lv_obj_t *handle = icon_part(shade, 205, 12, 70, 5, 3); set_bg(handle, GLASS); lv_obj_set_style_bg_opa(handle, LV_OPA_40, 0);
    lv_obj_t *title = text(shade, T_SHADE_TITLE, TEXT); lv_obj_set_pos(title, 20, 36); lv_obj_add_style(title, &st_title, 0);
    static const m3_text_t names[] = {T_SHADE_RAM, T_SHADE_PSRAM, T_SHADE_SD};
    const uint32_t colors[] = {CYAN, MAGENTA, AMBER};
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *panel = bare(shade); lv_obj_set_pos(panel, 16 + i * 152, 80); lv_obj_set_size(panel, 144, 200);
        lv_obj_set_style_bg_color(panel, lv_color_hex(GLASS), 0); lv_obj_set_style_bg_opa(panel, LV_OPA_10, 0);
        lv_obj_set_style_radius(panel, 18, 0);
        lv_obj_t *name = text(panel, names[i], MUTED);
        lv_obj_set_pos(name, 0, 12); lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        usage_arcs[i] = lv_arc_create(panel); lv_obj_set_pos(usage_arcs[i], 22, 40);
        lv_obj_set_size(usage_arcs[i], 100, 100); lv_arc_set_rotation(usage_arcs[i], 270);
        lv_arc_set_bg_angles(usage_arcs[i], 0, 360); lv_arc_set_range(usage_arcs[i], 0, 100);
        lv_arc_set_value(usage_arcs[i], 0); lv_obj_remove_style(usage_arcs[i], NULL, LV_PART_KNOB);
        lv_obj_remove_flag(usage_arcs[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_arc_width(usage_arcs[i], 9, LV_PART_MAIN);
        lv_obj_set_style_arc_width(usage_arcs[i], 9, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(usage_arcs[i], lv_color_hex(SURFACE_HI), LV_PART_MAIN);
        lv_obj_set_style_arc_color(usage_arcs[i], lv_color_hex(colors[i]), LV_PART_INDICATOR);
        usage_values[i] = label(panel, "--", TEXT); lv_obj_set_pos(usage_values[i], 0, 80);
        lv_obj_set_style_text_font(usage_values[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(usage_values[i], LV_TEXT_ALIGN_CENTER, 0);
        usage_details[i] = label(panel, "", MUTED); lv_obj_set_pos(usage_details[i], 4, 152);
        lv_obj_set_width(usage_details[i], 136); lv_obj_set_style_text_align(usage_details[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    cpu_chart = lv_chart_create(shade); lv_obj_set_pos(cpu_chart, 24, 310); lv_obj_set_size(cpu_chart, 432, 80);
    lv_chart_set_type(cpu_chart, LV_CHART_TYPE_LINE); lv_chart_set_point_count(cpu_chart, 60);
    lv_chart_set_range(cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(cpu_chart, 3, 5);
    lv_obj_set_style_bg_opa(cpu_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_size(cpu_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(cpu_chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (unsigned i = 0; i < 2; ++i) {
        cpu_values[i] = label(shade, i ? "HP1  --" : "HP0  --", i ? MAGENTA : CYAN);
        lv_obj_set_pos(cpu_values[i], 24 + i * 224, 284); lv_obj_set_width(cpu_values[i], 200);
        cpu_series[i] = lv_chart_add_series(cpu_chart, lv_color_hex(i ? MAGENTA : CYAN), LV_CHART_AXIS_PRIMARY_Y);
    }
    title = text(shade, T_SHADE_RUNNING, CYAN); lv_obj_set_pos(title, 20, 400);
    static const char *const job_glyphs[] = {LV_SYMBOL_HOME, LV_SYMBOL_AUDIO, LV_SYMBOL_CALL, LV_SYMBOL_WIFI, LV_SYMBOL_UPLOAD, LV_SYMBOL_SETTINGS};
    for (unsigned i = 0; i < 6; ++i) {
        job_cards[i] = bare(shade); lv_obj_set_pos(job_cards[i], 16 + (i % 2) * 228, 426 + (i / 2) * 78);
        lv_obj_set_size(job_cards[i], 220, 70); lv_obj_set_style_radius(job_cards[i], 14, 0);
        lv_obj_set_style_border_width(job_cards[i], 1, 0);
        lv_obj_set_style_bg_opa(job_cards[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(job_cards[i], lv_color_hex(SURFACE), 0);
        job_icons[i] = label(job_cards[i], job_glyphs[i], MUTED);
        lv_obj_set_pos(job_icons[i], 8, 17); lv_obj_set_size(job_icons[i], 36, 36);
        lv_obj_set_style_text_font(job_icons[i], FONT_ICON, 0);
        job_labels[i] = label(job_cards[i], "", TEXT); lv_obj_set_pos(job_labels[i], 48, 8);
        lv_obj_set_size(job_labels[i], 162, 58); lv_label_set_long_mode(job_labels[i], LV_LABEL_LONG_DOT);
        if (i == 1 || i == 2 || i == 4 || i == 5) {
            lv_obj_set_width(job_labels[i], 120);
            job_stops[i] = icon_button(job_cards[i], LV_SYMBOL_STOP, service_stop, (void *)(uintptr_t)i);
            lv_obj_set_pos(job_stops[i], 176, 17); lv_obj_set_size(job_stops[i], 36, 36);
            lv_obj_set_style_text_font(job_stops[i], &lv_font_montserrat_20, 0);
        }
    }
    shade_note = caption(shade, T_SHADE_NOTE); lv_obj_set_pos(shade_note, 20, 658); lv_obj_set_size(shade_note, 440, 34);
    label_ellipsis(shade_note);
    lv_obj_t *close = button(shade, LV_SYMBOL_UP, T_SHADE_CLOSE, BTN_SECONDARY, shade_close, NULL);
    lv_obj_set_pos(close, 16, 700); lv_obj_set_width(close, 216);
    lv_obj_t *off = button(shade, LV_SYMBOL_POWER, T_SCREEN_OFF, BTN_SECONDARY, screen_off, NULL);
    lv_obj_set_pos(off, 248, 700); lv_obj_set_width(off, 216);
    shade_motion(shade, -HOME_TOP);
}

/* ---------- Screen construction. ---------- */
static lv_obj_t *app_page(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page); lv_obj_add_style(page, &st_page, 0);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    return page;
}
static lv_obj_t *list_row(lv_obj_t *parent, lv_event_cb_t callback, void *data) {
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &st_list, 0); lv_obj_add_style(obj, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &st_list_checked, LV_STATE_CHECKED); lv_obj_add_style(obj, &st_disabled, LV_STATE_DISABLED);
    lv_obj_t *icon = lv_label_create(obj); lv_obj_set_width(icon, 22);
    lv_obj_t *name = lv_label_create(obj); grow(name); label_ellipsis(name);
    lv_obj_t *meta = lv_label_create(obj); lv_obj_add_style(meta, &st_caption, 0);
    (void)icon;
    lv_obj_add_event_cb(obj, sound_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    return obj;
}
#include "m3_theme_ui.inc"
static void home_apps(lv_obj_t *home, unsigned first, unsigned count, int top) {
    _Static_assert(sizeof(app_icons) / sizeof(app_icons[0]) == APP_COUNT - 1, "one icon per app");
    unsigned end = first + count;
    if (end > sizeof(app_icons) / sizeof(app_icons[0])) end = sizeof(app_icons) / sizeof(app_icons[0]);
    for (unsigned i = first; i < end; ++i) {
        int x = 42 + (int)((i - first) % 3) * 148, y = top + (int)((i - first) / 3) * (first ? 180 : 148);
        lv_obj_t *icon = lv_button_create(home); lv_obj_remove_style_all(icon);
        lv_obj_set_pos(icon, x, y); lv_obj_set_size(icon, 76, 76);
        lv_obj_set_style_radius(icon, 22, 0); lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(icon, 18, 0); lv_obj_set_style_shadow_offset_y(icon, 6, 0);
        lv_obj_set_style_shadow_opa(icon, LV_OPA_30, 0);
        lv_obj_set_style_shadow_color(icon, lv_color_hex(app_icons[i].bottom), 0);
        lv_obj_set_style_transform_scale(icon, 236, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_x(icon, 38, 0); lv_obj_set_style_transform_pivot_y(icon, 38, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(icon, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        lv_obj_add_event_cb(icon, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)app_icons[i].app);
        draw_app_icon(icon, i);
        lv_obj_t *name = text(home, app_names[app_icons[i].app], ON_WALL);
        lv_obj_set_pos(name, x - 18, y + 84); lv_obj_set_width(name, 112);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        label_ellipsis(name);
    }
}
static void build_home_main(lv_obj_t *home) {
    /* The main tile keeps the clock, network status and six primary apps. */
    lv_obj_set_style_layout(home, LV_LAYOUT_NONE, 0);
    lv_obj_set_style_pad_all(home, 0, 0);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *foreground = bare(home); lv_obj_set_size(foreground, 480, 660);
    lv_obj_set_pos(foreground, 0, 0); lv_obj_add_flag(foreground, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    home = foreground;
    home_clock = label(home, "--:--", ON_WALL); lv_obj_set_pos(home_clock, 0, 34);
    lv_obj_set_style_text_align(home_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(home_clock, &m3_font_clock, 0);
    home_date = label(home, "", ON_WALL_MUTED); lv_obj_set_pos(home_date, 0, 120);
    lv_obj_set_style_text_align(home_date, LV_TEXT_ALIGN_CENTER, 0);
    label_ellipsis(home_date);
    home_weather = label(home, "", ON_WALL); lv_obj_set_pos(home_weather, 20, 150); lv_obj_set_width(home_weather, 440);
    label_ellipsis(home_weather); lv_obj_set_style_text_align(home_weather, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(home_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_clock, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)APP_WEATHER);
    lv_obj_add_flag(home_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_date, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)APP_WEATHER);
    lv_obj_add_flag(home_weather, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_weather, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)APP_WEATHER);
    /* Glass status card: network state and now playing; tap for Wi-Fi details. */
    lv_obj_t *status_card = lv_button_create(home); lv_obj_remove_style_all(status_card);
    lv_obj_set_pos(status_card, 20, 194); lv_obj_set_size(status_card, 440, 84);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(ON_WALL), 0); lv_obj_set_style_bg_opa(status_card, LV_OPA_10, 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_radius(status_card, 22, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(ON_WALL), 0); lv_obj_set_style_border_opa(status_card, LV_OPA_10, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_add_event_cb(status_card, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)APP_WIFI);
    lv_obj_t *badge = bare(status_card); lv_obj_set_pos(badge, 16, 16); lv_obj_set_size(badge, 52, 52);
    lv_obj_set_style_radius(badge, 16, 0); lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x0b1220), 0); lv_obj_set_style_bg_opa(badge, LV_OPA_60, 0);
    home_net_icon = lv_label_create(badge); lv_label_set_text(home_net_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(home_net_icon, &lv_font_montserrat_20, 0); lv_obj_center(home_net_icon);
    home_net = label(status_card, "", ON_WALL); lv_obj_set_pos(home_net, 84, 18); lv_obj_set_width(home_net, 340);
    label_ellipsis(home_net);
    home_detail = label(status_card, "", ON_WALL_MUTED); lv_obj_set_pos(home_detail, 84, 44); lv_obj_set_width(home_detail, 340);
    label_ellipsis(home_detail);
    lv_obj_t *chevron = label(status_card, LV_SYMBOL_RIGHT, ON_WALL_MUTED); lv_obj_set_width(chevron, LV_SIZE_CONTENT);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -16, 0);
    home_apps(home, 0, HOME_PRIMARY_APPS, 296);
}
static m3_text_t home_page_title(unsigned index) {
    return index == 0 ? T_CAL_TITLE : index == 1 ? T_APP_HOME : index == HOME_PHOTOS ? T_PHOTO_TITLE : T_HOME_MORE;
}
static void home_page_update(void) {
    if (!home_tiles) return;
    for (unsigned i = 0; i < HOME_PAGE_COUNT; ++i) {
        lv_obj_t *mark = lv_obj_get_child(home_dots[i], 0);
        lv_obj_set_width(mark, i == home_index ? 24 : 8);
        lv_obj_set_style_bg_color(mark, lv_color_hex(i == home_index ? CYAN : 0x526175), 0);
        lv_obj_center(mark);
    }
    set_text(home_page_name, home_index >= 2 && home_index < HOME_PHOTOS ? "" : tr(home_page_title(home_index)));
    m3_photos_activate(current_app == APP_HOME && home_index == HOME_PHOTOS);
    theme_wallpaper_sync();
}
static void home_tile_changed(lv_event_t *event) {
    (void)event;
    lv_obj_t *active = lv_tileview_get_tile_active(home_tiles);
    for (unsigned i = 0; i < HOME_PAGE_COUNT; ++i) if (home_sheets[i] == active) home_index = i;
    home_page_update();
}
static void home_page_tap(lv_event_t *event) {
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index >= HOME_PAGE_COUNT) return;
    lv_tileview_set_tile_by_index(home_tiles, index, 0, ui_animation());
}
static void calendar_move(lv_event_t *event) {
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    if (!direction) calendar_follow = true;
    else if (calendar_year) { m3_calendar_shift(&calendar_year, &calendar_month, direction); calendar_follow = false; }
    calendar_stamp = 0; update_calendar();
}
static void update_calendar(void) {
    if (!calendar_heading) return;
    time_t now = time(NULL);
    if (now < 1735689600) {
        set_text(calendar_heading, "-- / --"); set_text(calendar_hint, tr(T_CAL_WAIT)); return;
    }
    now += 9 * 3600;
    struct tm local; gmtime_r(&now, &local);
    unsigned year = local.tm_year + 1900, month = local.tm_mon + 1;
    if (calendar_follow || !calendar_year) { calendar_year = year; calendar_month = month; }
    unsigned stamp = ((calendar_year * 12 + calendar_month) * 32 + local.tm_mday) * 2 + m3_i18n_language() + 1;
    unsigned today_stamp = year * 10000 + month * 100 + local.tm_mday;
    if (stamp == calendar_stamp && today_stamp == calendar_today_stamp) return;
    calendar_stamp = stamp; calendar_today_stamp = today_stamp;
    char text[80]; snprintf(text, sizeof(text), tr(T_CAL_MONTH), calendar_year, calendar_month);
    set_text(calendar_heading, text); set_text(calendar_hint, tr(T_HOME_SLIDE));
    static const char *const ko[] = {"일", "월", "화", "수", "목", "금", "토"};
    static const char *const en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const ja[] = {"日", "月", "火", "水", "木", "金", "土"};
    for (unsigned i = 0; i < 7; ++i) set_text(calendar_weekdays[i], m3_i18n_language() == M3_LANG_KO ? ko[i] : m3_i18n_language() == M3_LANG_JA ? ja[i] : en[i]);
    unsigned start = m3_calendar_first(calendar_year, calendar_month), days = m3_calendar_days(calendar_year, calendar_month);
    for (unsigned i = 0; i < 42; ++i) {
        unsigned day = i >= start && i - start < days ? i - start + 1 : 0;
        bool today = day && calendar_year == year && calendar_month == month && day == (unsigned)local.tm_mday;
        snprintf(text, sizeof(text), "%u", day);
        set_text(lv_obj_get_child(calendar_cells[i], 0), day ? text : "");
        lv_obj_set_style_bg_opa(calendar_cells[i], today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        set_bg(calendar_cells[i], CYAN);
        set_ink(lv_obj_get_child(calendar_cells[i], 0), today ? ON_ACCENT : i % 7 == 0 ? MAGENTA : i % 7 == 6 ? CYAN : ON_WALL);
    }
}
static void build_home_calendar(lv_obj_t *tile) {
    lv_obj_t *heading = text(tile, T_CAL_TITLE, ON_WALL); lv_obj_set_pos(heading, 24, 22);
    lv_obj_set_width(heading, 432);
    label_ellipsis(heading);
    lv_obj_add_style(heading, &st_title, 0);
    lv_obj_t *previous = icon_button(tile, LV_SYMBOL_LEFT, calendar_move, (void *)(intptr_t)-1);
    lv_obj_set_pos(previous, 24, 72); lv_obj_set_size(previous, 44, 44);
    lv_obj_t *next = icon_button(tile, LV_SYMBOL_RIGHT, calendar_move, (void *)(intptr_t)1);
    lv_obj_set_pos(next, 412, 72); lv_obj_set_size(next, 44, 44);
    calendar_heading = label(tile, "-- / --", ON_WALL); lv_obj_set_pos(calendar_heading, 76, 84); lv_obj_set_width(calendar_heading, 328);
    lv_obj_set_style_text_align(calendar_heading, LV_TEXT_ALIGN_CENTER, 0);
    for (unsigned i = 0; i < 7; ++i) {
        calendar_weekdays[i] = label(tile, "", i == 0 ? MAGENTA : i == 6 ? CYAN : ON_WALL_MUTED);
        lv_obj_set_pos(calendar_weekdays[i], 26 + i * 62, 152); lv_obj_set_width(calendar_weekdays[i], 56);
        lv_obj_set_style_text_align(calendar_weekdays[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    for (unsigned i = 0; i < 42; ++i) {
        calendar_cells[i] = bare(tile); lv_obj_set_size(calendar_cells[i], 54, 48);
        lv_obj_set_pos(calendar_cells[i], 27 + (i % 7) * 62, 188 + (i / 7) * 55);
        lv_obj_set_style_radius(calendar_cells[i], 18, 0);
        lv_obj_t *day = label(calendar_cells[i], "", ON_WALL); lv_obj_set_width(day, 54);
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(day);
    }
    lv_obj_t *today = button(tile, NULL, T_CAL_TODAY, BTN_PRIMARY, calendar_move, NULL);
    lv_obj_set_pos(today, 150, 552); lv_obj_set_size(today, 180, 44);
    calendar_hint = label(tile, tr(T_CAL_WAIT), ON_WALL_MUTED); lv_obj_set_pos(calendar_hint, 20, 618); lv_obj_set_width(calendar_hint, 440);
    lv_obj_set_style_text_align(calendar_hint, LV_TEXT_ALIGN_CENTER, 0);
}
static void photo_step(lv_event_t *event) { m3_photos_step((int)(intptr_t)lv_event_get_user_data(event)); }
static void photo_toggle(lv_event_t *event) { (void)event; m3_photos_pause(); }
static void photo_refresh(lv_event_t *event) { (void)event; m3_photos_refresh(); }
static void update_photos(void) {
    if (!photo_canvas || current_app != APP_HOME || home_index != HOME_PHOTOS) return;
    m3_photos_state(&photo_snapshot);
    static const m3_text_t statuses[] = {T_PHOTO_HINT, T_PHOTO_LOADING, T_PHOTO_HINT, T_PHOTO_EMPTY,
        T_PHOTO_BUSY, T_PHOTO_INVALID, T_PHOTO_MEMORY, T_PHOTO_NO_SD};
    unsigned state = photo_snapshot.status;
    if (state >= sizeof(statuses) / sizeof(statuses[0])) state = M3_PHOTO_INVALID;
    set_text(photo_name, photo_snapshot.name);
    char text[128];
    if (state == M3_PHOTO_READY) snprintf(text, sizeof(text), tr(T_PHOTO_COUNT), photo_snapshot.index + 1, photo_snapshot.count,
        tr(photo_snapshot.paused ? T_PHOTO_PAUSED : T_PHOTO_RUNNING));
    else snprintf(text, sizeof(text), "%s", tr(statuses[state]));
    set_text(photo_info, text); set_text(photo_empty, tr(statuses[state]));
    set_text(lv_obj_get_child(photo_pause, 0), tr(photo_snapshot.paused ? T_MUSIC_RESUME : T_MUSIC_PAUSE));
    const uint8_t *frame = m3_photos_frame();
    if (frame && frame != photo_frame) {
        photo_frame = frame;
        lv_canvas_set_buffer(photo_canvas, (void *)frame, M3_PHOTO_W, M3_PHOTO_H, LV_COLOR_FORMAT_RGB565);
    }
    bool visible = frame && (state == M3_PHOTO_READY || state == M3_PHOTO_LOADING || state == M3_PHOTO_BUSY);
    if (visible) { lv_obj_remove_flag(photo_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(photo_empty, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(photo_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(photo_empty, LV_OBJ_FLAG_HIDDEN); }
}
static void build_home_photos(lv_obj_t *tile) {
    lv_obj_t *heading = text(tile, T_PHOTO_TITLE, ON_WALL); lv_obj_set_pos(heading, 24, 14);
    lv_obj_set_width(heading, 432);
    label_ellipsis(heading);
    lv_obj_add_style(heading, &st_title, 0);
    photo_canvas = lv_canvas_create(tile); lv_obj_set_pos(photo_canvas, 24, 68);
    lv_obj_set_size(photo_canvas, M3_PHOTO_W, M3_PHOTO_H);
    lv_obj_remove_flag(photo_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(photo_canvas, LV_OBJ_FLAG_HIDDEN);
    photo_empty = label(tile, tr(T_PHOTO_EMPTY), ON_WALL_MUTED); lv_obj_set_pos(photo_empty, 36, 250); lv_obj_set_width(photo_empty, 408);
    lv_obj_set_style_text_align(photo_empty, LV_TEXT_ALIGN_CENTER, 0);
    photo_name = label(tile, "", ON_WALL); lv_obj_set_pos(photo_name, 24, 554); lv_obj_set_width(photo_name, 432);
    label_ellipsis(photo_name);
    photo_info = label(tile, tr(T_PHOTO_HINT), ON_WALL_MUTED); lv_obj_set_pos(photo_info, 24, 582); lv_obj_set_width(photo_info, 432);
    label_ellipsis(photo_info);
    lv_obj_t *previous = button(tile, LV_SYMBOL_PREV, T_NONE, BTN_SECONDARY, photo_step, (void *)(intptr_t)-1);
    lv_obj_set_pos(previous, 24, 614); lv_obj_set_size(previous, 64, 40);
    photo_pause = button(tile, NULL, T_MUSIC_PAUSE, BTN_SECONDARY, photo_toggle, NULL);
    lv_obj_set_pos(photo_pause, 100, 614); lv_obj_set_size(photo_pause, 150, 40);
    lv_obj_t *next = button(tile, LV_SYMBOL_NEXT, T_NONE, BTN_SECONDARY, photo_step, (void *)(intptr_t)1);
    lv_obj_set_pos(next, 262, 614); lv_obj_set_size(next, 64, 40);
    lv_obj_t *refresh = refresh_button(tile,T_PHOTO_REFRESH,photo_refresh,NULL);
    lv_obj_set_pos(refresh,408,610); lv_obj_set_size(refresh,48,44);
}
static void build_home(lv_obj_t *home) {
    lv_obj_set_style_layout(home, LV_LAYOUT_NONE, 0); lv_obj_set_style_pad_all(home, 0, 0);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    /* One fixed wallpaper below all transparent tiles keeps swipes continuous. */
    home_wallpaper = bare(home); lv_obj_set_size(home_wallpaper, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(home_wallpaper, 0, 0);
    m3_wallpaper_build(home_wallpaper, ui_theme.wallpaper, CYAN, MAGENTA);
    home_tiles = lv_tileview_create(home); lv_obj_remove_style_all(home_tiles);
    lv_obj_set_pos(home_tiles, 0, 0); lv_obj_set_size(home_tiles, 480, 660);
    lv_obj_set_scrollbar_mode(home_tiles, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(home_tiles, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_snap_y(home_tiles, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_anim_duration(home_tiles, 240, 0);
    lv_obj_add_flag(home_tiles, LV_OBJ_FLAG_SCROLL_ONE);
    for (unsigned i = 0; i < HOME_PAGE_COUNT; ++i) {
        lv_dir_t direction = i == 0 ? LV_DIR_RIGHT : i == HOME_PAGE_COUNT - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
        home_sheets[i] = lv_tileview_add_tile(home_tiles, i, 0, direction);
        lv_obj_set_style_pad_all(home_sheets[i], 0, 0); lv_obj_set_style_layout(home_sheets[i], LV_LAYOUT_NONE, 0);
        lv_obj_remove_flag(home_sheets[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(home_sheets[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        lv_obj_set_style_bg_color(home_sheets[i], lv_color_hex(0x0b1424), 0);
        lv_obj_set_style_bg_opa(home_sheets[i], LV_OPA_TRANSP, 0);
    }
    build_home_calendar(home_sheets[0]); build_home_main(home_sheets[1]);
    for (unsigned i = 2; i < HOME_PHOTOS; ++i) {
        home_apps(home_sheets[i], HOME_PRIMARY_APPS + (i - 2) * HOME_APPS_PER_PAGE, HOME_APPS_PER_PAGE, 76);
    }
    build_home_photos(home_sheets[HOME_PHOTOS]);
    for (unsigned i = 0; i < HOME_PAGE_COUNT; ++i) {
        home_dots[i] = lv_button_create(home); lv_obj_remove_style_all(home_dots[i]);
        lv_obj_set_size(home_dots[i], 40, 28); lv_obj_set_pos(home_dots[i], (480 - HOME_PAGE_COUNT * 40) / 2 + i * 40, 664);
        lv_obj_remove_flag(home_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(home_dots[i], home_page_tap, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *mark = bare(home_dots[i]); lv_obj_set_size(mark, 8, 6);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    }
    home_page_name = label(home, "", ON_WALL_MUTED); lv_obj_set_pos(home_page_name, 0, 694);
    lv_obj_set_style_text_align(home_page_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(home_tiles, home_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_tileview_set_tile_by_index(home_tiles, 1, 0, LV_ANIM_OFF);
    home_index = 1; home_page_update();
}
static void build_wifi(lv_obj_t *net) {
    lv_obj_t *box = card(net, T_SEC_STATUS);
    lv_obj_t *line = row(box);
    net_dot = dot(line, AMBER);
    net_title = label(line, "", TEXT); grow(net_title); lv_obj_add_style(net_title, &st_title, 0);
    network = label(box, "", MUTED); lv_obj_set_style_text_line_space(network, 4, 0);
    line = row(box); lv_obj_set_style_pad_column(line, 16, 0);
    qr = lv_qrcode_create(line); lv_qrcode_set_size(qr, 120);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000)); lv_qrcode_set_light_color(qr, lv_color_hex(0xffffff));
    lv_obj_set_style_border_width(qr, 8, 0); lv_obj_set_style_border_color(qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(qr, 8, 0);
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *column = bare(line); grow(column); lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(column, 8, 0);
    caption(column, T_QR_HINT);
    qr_url = text(column, T_QR_WAIT, CYAN);
    box = card(net, T_SEC_NEARBY);
    wifi_actions[0] = button(box, LV_SYMBOL_REFRESH, T_WIFI_SCAN, BTN_SECONDARY, wifi_action, (void *)(uintptr_t)M3_WIFI_SCAN);
    for (unsigned i = 0; i < M3_WIFI_RESULTS; ++i) {
        scan_buttons[i] = list_row(box, select_network, (void *)(uintptr_t)i);
        lv_label_set_text(lv_obj_get_child(scan_buttons[i], 0), LV_SYMBOL_WIFI);
        lv_obj_t *security = bare(scan_buttons[i]); lv_obj_set_size(security, 26, 26);
        lv_obj_t *shackle = bare(security); lv_obj_set_size(shackle, 12, 14);
        lv_obj_set_style_radius(shackle, 6, 0); lv_obj_set_style_border_width(shackle, 2, 0);
        icon_part(security, 3, 12, 18, 13, 3);
        lv_obj_t *keyhole = icon_part(security, 11, 16, 2, 5, 1);
        lv_obj_set_style_bg_color(keyhole, lv_color_hex(SURFACE), 0);
        wifi_security(security, false);
        lv_obj_add_flag(scan_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    box = card(net, T_SEC_CONNECT);
    wifi_info = text(box, T_WIFI_FORM_HINT, CYAN);
    ssid_field = input_field(box, T_WIFI_SSID, 32, INPUT_TEXT, true);
    pass_field = input_field(box, T_WIFI_PASSWORD, 63, INPUT_TEXT, false);
    bind_text(pass_field, T_WIFI_PASSWORD_HINT, NULL, BIND_PLACEHOLDER);
    lv_textarea_set_password_mode(pass_field, true);
    icon_button(lv_obj_get_parent(pass_field), LV_SYMBOL_EYE_CLOSE, password_visibility, NULL);
    line = row(box); lv_obj_set_style_pad_top(line, 6, 0);
    lv_obj_t *texts = bare(line); grow(texts); lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(texts, 2, 0);
    text(texts, T_WIFI_STATIC, TEXT); caption(texts, T_WIFI_STATIC_HINT);
    static_switch = lv_switch_create(line); lv_obj_set_size(static_switch, 60, 32);
    lv_obj_set_style_bg_color(static_switch, lv_color_hex(SURFACE_HI), 0);
    lv_obj_add_event_cb(static_switch, static_changed, LV_EVENT_VALUE_CHANGED, NULL);
    static_fields = bare(box); lv_obj_set_size(static_fields, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(static_fields, LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_column(static_fields, 12, 0);
    lv_obj_set_style_pad_row(static_fields, 8, 0);
    static const m3_text_t ip_titles[] = {T_WIFI_IP, T_WIFI_GATEWAY, T_WIFI_MASK, T_WIFI_DNS};
    lv_obj_t **ip_fields[] = {&ip_field, &gateway_field, &mask_field, &dns_field};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *cell = bare(static_fields); lv_obj_set_size(cell, 202, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(cell, 6, 0);
        *ip_fields[i] = input_field(cell, ip_titles[i], 15, INPUT_NUMBER, false);
    }
    bind_text(dns_field, T_WIFI_DNS_HINT, NULL, BIND_PLACEHOLDER);
    lv_textarea_set_text(mask_field, "255.255.255.0"); form_dirty = false;
    lv_obj_add_flag(static_fields, LV_OBJ_FLAG_HIDDEN);
    wifi_actions[1] = button(box, LV_SYMBOL_OK, T_WIFI_CONNECT, BTN_PRIMARY, wifi_action, (void *)(uintptr_t)M3_WIFI_CONNECT);
    wifi_actions[2] = button(box, LV_SYMBOL_GPS, T_WIFI_HOTSPOT, BTN_SECONDARY, wifi_action, (void *)(uintptr_t)M3_WIFI_SETUP);
    box = card(net, T_WIFI_SAVED_NETWORKS);
    wifi_saved_count = label(box, "", CYAN);
    caption(box, T_WIFI_SAVED_HINT);
    for (unsigned i = 0; i < M3_WIFI_PROFILE_LIMIT; ++i) {
        wifi_saved_rows[i] = row(box);
        wifi_saved_connect[i] = lv_button_create(wifi_saved_rows[i]);
        lv_obj_remove_style_all(wifi_saved_connect[i]);
        lv_obj_add_style(wifi_saved_connect[i], &st_btn, 0);
        lv_obj_add_style(wifi_saved_connect[i], &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_add_style(wifi_saved_connect[i], &st_disabled, LV_STATE_DISABLED);
        grow(wifi_saved_connect[i]); lv_obj_set_height(wifi_saved_connect[i], 48);
        wifi_saved_names[i] = label(wifi_saved_connect[i], "", TEXT);
        lv_obj_center(wifi_saved_names[i]);
        lv_obj_set_width(wifi_saved_names[i], LV_PCT(100));
        label_ellipsis(wifi_saved_names[i]);
        lv_obj_add_event_cb(wifi_saved_connect[i], wifi_saved_action, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        wifi_saved_remove[i] = icon_button(wifi_saved_rows[i], LV_SYMBOL_TRASH, wifi_saved_action,
            (void *)(uintptr_t)(i + M3_WIFI_PROFILE_LIMIT));
        lv_obj_add_flag(wifi_saved_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    box = card(net, T_SEC_HELP);
    caption(box, T_WIFI_HELP);
}
static void build_files(lv_obj_t *files) {
    static const m3_text_t tabs[] = {T_STORAGE_FLASH, T_STORAGE_SD};
    storage_tabs = segmented(files, tabs, NULL, 2, 0, choose_storage);
    lv_obj_t *box = card(files, T_NONE);
    lv_obj_t *line = row(box);
    icon_button(line, LV_SYMBOL_UP, parent_folder, NULL);
    lv_obj_t *texts = bare(line); grow(texts); lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(texts, 4, 0);
    file_path = label(texts, "", TEXT); label_ellipsis(file_path);
    file_info = text(texts, T_FILES_READING, MUTED);
    file_bar = bar(box, AMBER, 6);
    box = card(files, T_NONE);
    lv_obj_set_style_pad_row(box, 8, 0);
    for (unsigned i = 0; i < STORAGE_VIEW_ROWS; ++i) {
        file_buttons[i] = list_row(box, open_file, (void *)(uintptr_t)i);
        lv_obj_remove_event_cb(file_buttons[i], open_file);
        lv_obj_add_event_cb(file_buttons[i], open_file, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(file_buttons[i], file_action_select, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
        lv_obj_add_flag(file_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    file_empty = caption(box, T_FILES_EMPTY); lv_obj_set_style_text_align(file_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(file_empty, LV_OBJ_FLAG_HIDDEN);
    line = row(box);
    grow(button(line, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, previous_files, NULL));
    file_page = label(line, "1 / 1", MUTED); lv_obj_set_width(file_page, 76);
    lv_obj_set_style_text_align(file_page, LV_TEXT_ALIGN_CENTER, 0);
    grow(button(line, NULL, T_FILES_NEXT, BTN_SECONDARY, next_files, NULL));
    caption(files, T_FILES_HINT);
    build_file_actions(files);
}
#include "m3_reader_ui.inc"

static void build_recorder(lv_obj_t *audio, unsigned volume) {
    lv_obj_t *box = card(audio, T_APP_RECORDER);
    recorder_time = label(box, "00:00 / 30:00", TEXT);
    lv_obj_set_style_text_font(recorder_time, FONT_ICON, 0);
    recorder_info = text(box, T_REC_HINT, CYAN);
    recorder_meter = bar(box, MAGENTA, 14);
    recorder_path = label(box, M3_RECORDER_FOLDER, MUTED);
    lv_obj_t *line = row(box);
    recorder_actions[0] = grow(button(line, LV_SYMBOL_SAVE, T_REC_START, BTN_PRIMARY, recorder_start, NULL));
    recorder_actions[1] = grow(button(line, LV_SYMBOL_STOP, T_REC_STOP, BTN_DANGER, recorder_stop, NULL));
    line = row(box);
    recorder_actions[2] = grow(button(line, LV_SYMBOL_PLAY, T_REC_PLAY, BTN_SECONDARY, recorder_play, NULL));
    recorder_actions[3] = grow(button(line, LV_SYMBOL_DIRECTORY, T_REC_BROWSE, BTN_SECONDARY, recorder_folder, NULL));
    caption(audio, T_REC_NOTES);
    box = card(audio, T_SEC_VOLUME);
    volume_row(box, 0, volume);
}
static void radio_filter(lv_event_t *event) {
    (void)event; unsigned region = segment_value(radio_region);
    for (unsigned i = 0; i < sizeof(radio_stations) / sizeof(radio_stations[0]); ++i) {
        if (radio_stations[i].region == region) lv_obj_remove_flag(station_buttons[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(station_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
}
static void build_radio(lv_obj_t *radio, unsigned volume) {
    lv_obj_t *box = card(radio, T_NOW_PLAYING);
    radio_now = label(box, "", TEXT); lv_obj_add_style(radio_now, &st_title, 0);
    label_ellipsis(radio_now);
    radio_info = text(box, T_RADIO_IDLE, MUTED);
    radio_bar = bar(box, MAGENTA, 6);
    lv_obj_t *line = row(box);
    grow(button(line, LV_SYMBOL_PLAY, T_RADIO_PLAY, BTN_PRIMARY, play_selected, NULL));
    grow(button(line, LV_SYMBOL_STOP, T_AUDIO_STOP, BTN_DANGER, stop_audio, NULL));
    box = card(radio, T_SEC_VOLUME);
    volume_row(box, 1, volume);
    box = card(radio, T_RADIO_TRANSPORT);
    static const m3_text_t transport[] = {T_RADIO_DIRECT, T_RADIO_ECO, T_RADIO_BALANCED, T_RADIO_HIGH};
    radio_transport = segmented(box, transport, NULL, 4, 0, NULL);
    caption(box, T_RADIO_RELAY_HINT);
    static const m3_text_t countries[] = {T_RADIO_WORLD, T_RADIO_KR, T_RADIO_JP};
    radio_region = segmented(radio, countries, NULL, 3, 1, radio_filter);
    box = card(radio, T_SEC_STATIONS);
    lv_obj_set_style_pad_row(box, 8, 0);
    for (unsigned i = 0; i < sizeof(radio_stations) / sizeof(radio_stations[0]); ++i) {
        station_buttons[i] = list_row(box, station_tap, (void *)(uintptr_t)i);
        const char *name = radio_stations[i].name, *slash = strstr(name, " / ");
        char title[48]; m3_utf8_copy(title, sizeof(title), name);
        char *separator = strstr(title, " / "); if (separator) *separator = 0;
        lv_label_set_text(lv_obj_get_child(station_buttons[i], 0), LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(lv_obj_get_child(station_buttons[i], 0), lv_color_hex(radio_stations[i].aac ? MAGENTA : CYAN), 0);
        lv_label_set_text(lv_obj_get_child(station_buttons[i], 1), title);
        lv_label_set_text(lv_obj_get_child(station_buttons[i], 2), slash ? slash + 3 : "");
    }
    box = card(radio, T_SEC_CUSTOM);
    radio_url = input_field(box, T_RADIO_URL, 512, INPUT_TEXT, false);
    static const char *const codecs[] = {"MP3", "AAC / ADTS"};
    radio_codec = segmented(box, NULL, codecs, 2, 0, NULL);
    button(box, LV_SYMBOL_PLAY, T_RADIO_PLAY_URL, BTN_SECONDARY, play_custom, NULL);
    caption(radio, T_RADIO_NOTES);
    radio_filter(NULL);
}
static void build_crossing(void) {
    cross_scene = bare(game_board); lv_obj_set_pos(cross_scene, 2, 2);
    lv_obj_set_size(cross_scene, M3_CROSS_WIDTH, M3_CROSS_ROWS * M3_CROSS_CELL);
    for (unsigned i = 0; i < M3_CROSS_ROWS; ++i) {
        cross_lanes[i] = icon_part(cross_scene, 0, (M3_CROSS_ROWS - 1 - i) * M3_CROSS_CELL,
            M3_CROSS_WIDTH, M3_CROSS_CELL, 0);
        for (unsigned j = 0; j < 3; ++j) {
            cross_objects[i][j] = icon_part(cross_lanes[i], 0, 8, 64, 28, 5);
            cross_windows[i][j] = icon_part(cross_objects[i][j], 8, 5, 20, 18, 2);
        }
    }
    cross_player = bare(cross_scene); lv_obj_set_size(cross_player, 32, 34);
    cross_body = icon_part(cross_player, 6, 10, 20, 19, 4);
    icon_part(cross_player, 11, 2, 15, 15, 3);
    lv_obj_t *part = icon_part(cross_player, 24, 10, 7, 5, 1); set_bg(part, 0xffb340);
    part = icon_part(cross_player, 12, 0, 10, 4, 1); set_bg(part, 0xef6868);
    part = icon_part(cross_player, 21, 6, 3, 3, 1); set_bg(part, 0x202630);
    part = icon_part(cross_player, 8, 29, 6, 4, 1); set_bg(part, 0xffb340);
    part = icon_part(cross_player, 20, 29, 6, 4, 1); set_bg(part, 0xffb340);
    lv_obj_add_flag(cross_scene, LV_OBJ_FLAG_HIDDEN);
}
static void build_games(lv_obj_t *games) {
    lv_obj_t *parent = games;
    sd_launcher = button(parent, LV_SYMBOL_SD_CARD, T_SD_APPS, BTN_PRIMARY, sd_apps_open, NULL);
    sd_panel = card(parent, T_SD_APPS);
    lv_obj_t *sd_header = row(sd_panel);
    grow(button(sd_header, LV_SYMBOL_LEFT, T_SD_APPS_STOP, BTN_SECONDARY, sd_apps_close, NULL));
    sd_restart = grow(button(sd_header, LV_SYMBOL_REFRESH, T_GAME_NEW, BTN_SECONDARY, sd_apps_control, (void *)(uintptr_t)16));
    lv_obj_add_flag(sd_restart, LV_OBJ_FLAG_HIDDEN);
    sd_status = label(sd_panel, "", MUTED);
    sd_list = bare(sd_panel); lv_obj_set_size(sd_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sd_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(sd_list, 8, 0);
    for (unsigned i = 0; i < M3_SD_APP_COUNT; ++i) {
        sd_rows[i] = button(sd_list, NULL, T_NONE, BTN_SECONDARY, sd_apps_launch, (void *)(uintptr_t)i);
        sd_faces[i] = lv_obj_get_child(sd_rows[i], 0);
        lv_obj_add_flag(sd_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    sd_stage = bare(sd_panel); lv_obj_set_size(sd_stage, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sd_stage, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(sd_stage, 10, 0);
    lv_obj_t *frame = bare(sd_stage); lv_obj_set_size(frame, 280, 336);
    lv_obj_set_style_align(frame, LV_ALIGN_CENTER, 0);
    sd_frame_holder = frame;
    lv_obj_t *controls = row(sd_stage);
    const char *symbols[] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_REFRESH, LV_SYMBOL_DOWN};
    for (unsigned i = 0; i < 4; ++i)
        grow(button(controls, symbols[i], T_NONE, BTN_SECONDARY, sd_apps_control, (void *)(uintptr_t)(1u << i)));
    caption(sd_stage, T_SD_APPS_RULES);
    lv_obj_add_flag(sd_stage, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(sd_panel, LV_OBJ_FLAG_HIDDEN);
    games = builtin_games = bare(parent);
    lv_obj_set_size(games, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(games, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(games, 12, 0);
    lv_obj_set_style_flex_cross_place(games, LV_FLEX_ALIGN_CENTER, 0);
    static const m3_text_t modes[] = {T_GAME_2048, T_GAME_MEMORY, T_GAME_CROSSING};
    game_tabs = segmented(games, modes, NULL, 3, 0, choose_game);
    lv_obj_t *scores = row(games);
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_t *box = card(scores, T_NONE); grow(box); lv_obj_set_style_pad_row(box, 2, 0);
        lv_obj_set_style_pad_ver(box, 12, 0);
        score_titles[i] = label(box, "", MUTED);
        score_values[i] = label(box, "0", TEXT); lv_obj_set_style_text_font(score_values[i], FONT_ICON, 0);
    }
    game_board = lv_obj_create(games); lv_obj_remove_style_all(game_board);
    lv_obj_set_size(game_board, 400, 400); lv_obj_set_style_align(game_board, LV_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(game_board, lv_color_hex(SURFACE), 0); lv_obj_set_style_bg_opa(game_board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(game_board, 18, 0);
    lv_obj_remove_flag(game_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(game_board, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(game_board, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(game_board, game_touch, LV_EVENT_ALL, NULL);
    for (unsigned i = 0; i < 16; ++i) {
        game_tiles[i] = lv_button_create(game_board); lv_obj_remove_style_all(game_tiles[i]);
        lv_obj_set_size(game_tiles[i], 88, 88); lv_obj_set_pos(game_tiles[i], 10 + (i % 4) * 96, 10 + (i / 4) * 96);
        lv_obj_set_style_radius(game_tiles[i], 12, 0); lv_obj_set_style_bg_opa(game_tiles[i], LV_OPA_COVER, 0);
        lv_obj_t *face = lv_label_create(game_tiles[i]); lv_obj_center(face);
        lv_obj_add_event_cb(game_tiles[i], memory_card, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_flag(game_tiles[i], LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_remove_flag(game_tiles[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    }
    build_crossing();
    game_info = label(games, "", MUTED); lv_obj_set_style_text_align(game_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *arrows = row(games);
    static const char *directions[] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_UP, LV_SYMBOL_DOWN};
    for (unsigned i = 0; i < 4; ++i) {
        game_arrows[i] = grow(button(arrows, directions[i], T_NONE, BTN_SECONDARY, game_direction, (void *)(uintptr_t)i));
    }
    lv_obj_t *line = row(games);
    game_undo = grow(button(line, LV_SYMBOL_LOOP, T_GAME_UNDO, BTN_SECONDARY, undo_move, NULL));
    grow(button(line, LV_SYMBOL_REFRESH, T_GAME_NEW, BTN_PRIMARY, new_game, NULL));
    new_game(NULL);
}
static void build_tts(lv_obj_t *tts, unsigned volume) {
    lv_obj_t *box = card(tts, T_SEC_ENGINE);
    static const m3_text_t engines[] = {T_TTS_OPENAI, T_TTS_GOOGLE, T_TTS_OFFLINE, T_TTS_AZURE};
    tts_provider = segmented(box, engines, NULL, 4, m3_audio_tts_selected(), tts_provider_changed);
    caption(box, T_TTS_ENGINE_HINT);
    box = card(tts, T_SEC_TEXT);
    tts_text = input_field(box, T_NONE, 128, INPUT_KOREAN, true);
    lv_obj_t *field_row = lv_obj_get_parent(tts_text);
    lv_obj_set_style_flex_cross_place(field_row, LV_FLEX_ALIGN_START, 0);
    lv_textarea_set_one_line(tts_text, false); lv_obj_set_height(tts_text, 110);
    bind_text(tts_text, T_TTS_PLACEHOLDER, NULL, BIND_PLACEHOLDER);
    lv_textarea_set_text(tts_text, "안녕하세요. 한국어 음성 합성 시험입니다.");
    lv_obj_t *line = row(box);
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *chip = grow(button(line, NULL, (m3_text_t)(T_TTS_SAMPLE_1 + i), BTN_SECONDARY, tts_sample, (void *)(uintptr_t)i));
        lv_obj_set_height(chip, 40); lv_obj_set_style_radius(chip, 20, 0);
    }
    tts_info = label(box, "", MUTED);
    tts_wait = activity_create(box, ACTIVITY_DOTS, CYAN, T_TTS_PREPARING);
    tts_talk = activity_create(box, ACTIVITY_BARS, MAGENTA, T_TTS_SPEAKING);
    line = row(box);
    grow(button(line, LV_SYMBOL_PLAY, T_TTS_SPEAK, BTN_PRIMARY, speak_text, NULL));
    grow(button(line, LV_SYMBOL_STOP, T_AUDIO_STOP, BTN_DANGER, stop_audio, NULL));
    box = card(tts, T_SEC_SPEED);
    line = row(box);
    tts_speed = lv_slider_create(line); style_slider(tts_speed); grow(tts_speed);
    lv_slider_set_range(tts_speed, 100, 260); lv_slider_set_value(tts_speed, 165, LV_ANIM_OFF);
    lv_obj_add_event_cb(tts_speed, tts_speed_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_style(tts_speed, &st_disabled, LV_STATE_DISABLED); lv_obj_add_state(tts_speed, LV_STATE_DISABLED);
    tts_speed_value = label(line, "", TEXT); lv_obj_set_width(tts_speed_value, 56);
    lv_obj_set_style_text_align(tts_speed_value, LV_TEXT_ALIGN_RIGHT, 0);
    tts_speed_label();
    caption(box, T_TTS_SPEED_HINT);
    box = card(tts, T_SEC_VOLUME);
    volume_row(box, 2, volume);
    caption(tts, T_TTS_NOTES);
}
static void build_agent(lv_obj_t *agent) {
    /* Index order matches m3_agent_model(): 0 Gemini 3.1 Flash-Lite, 1 Gemini 3.5 Flash-Lite. */
    static const char *const models[] = {"Gemini 3.1 Flash-Lite", "Gemini 3.5 Flash-Lite"};
    agent_model = segmented(agent, NULL, models, 2, 1, NULL);
    lv_obj_t *box = card(agent, T_SEC_CONVERSATION);
    lv_obj_t *line = row(box);
    agent_dot = dot(line, MUTED);
    agent_info = label(line, "", MUTED); grow(agent_info);
    agent_voice = activity_create(line, ACTIVITY_BARS, MAGENTA, T_NONE);
    agent_output = lv_obj_create(box); lv_obj_remove_style_all(agent_output);
    lv_obj_set_size(agent_output, LV_PCT(100), 250);
    lv_obj_set_style_bg_color(agent_output, lv_color_hex(FIELD), 0); lv_obj_set_style_bg_opa(agent_output, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(agent_output, 14, 0); lv_obj_set_style_pad_all(agent_output, 14, 0);
    lv_obj_set_flex_flow(agent_output, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(agent_output, 8, 0);
    agent_trace = label(agent_output, "", CYAN); lv_obj_add_flag(agent_trace, LV_OBJ_FLAG_HIDDEN);
    agent_thinking = activity_create(agent_output, ACTIVITY_DOTS, CYAN, T_AGENT_THINKING);
    agent_answer = label(agent_output, "", TEXT); lv_obj_set_style_text_line_space(agent_answer, 5, 0);
    line = row(box);
    grow(button(line, LV_SYMBOL_VOLUME_MAX, T_AGENT_READ, BTN_SECONDARY, agent_read, NULL));
    grow(button(line, LV_SYMBOL_TRASH, T_AGENT_CLEAR, BTN_SECONDARY, agent_clear, NULL));
    icon_button(line, LV_SYMBOL_STOP, stop_audio, NULL);
    box = card(agent, T_NONE);
    agent_prompt = input_field(box, T_NONE, 128, INPUT_KOREAN, true);
    lv_obj_set_style_flex_cross_place(lv_obj_get_parent(agent_prompt), LV_FLEX_ALIGN_START, 0);
    lv_textarea_set_one_line(agent_prompt, false); lv_obj_set_height(agent_prompt, 90);
    bind_text(agent_prompt, T_AGENT_PLACEHOLDER, NULL, BIND_PLACEHOLDER);
    line = row(box);
    grow(button(line, LV_SYMBOL_UPLOAD, T_AGENT_SEND, BTN_PRIMARY, agent_send, NULL));
    grow(button(line, LV_SYMBOL_CLOSE, T_AGENT_CANCEL, BTN_SECONDARY, agent_cancel, NULL));
    caption(agent, T_AGENT_NOTES);
}

static void build_music(lv_obj_t *music, unsigned volume) {
    lv_obj_t *box = card(music, T_NOW_PLAYING);
    music_title = label(box, "", TEXT); lv_obj_add_style(music_title, &st_title, 0);
    label_ellipsis(music_title);
    lv_obj_t *visual_row = bare(box); lv_obj_set_size(visual_row, LV_PCT(100), 200);
    lv_obj_t *visual = bare(visual_row); lv_obj_set_size(visual, 200, 200); lv_obj_center(visual);
    lv_obj_add_flag(visual, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(visual, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(visual, music_swipe_event, LV_EVENT_GESTURE, NULL);
    const uint32_t colors[] = {CYAN, GREEN, MAGENTA};
    for (unsigned i = 0; i < 24; ++i) {
        music_rays[i] = lv_line_create(visual); lv_obj_set_pos(music_rays[i], 0, 0);
        lv_obj_set_style_line_color(music_rays[i], lv_color_hex(colors[i / 8]), 0);
        lv_obj_set_style_line_width(music_rays[i], 5, 0);
        lv_obj_remove_flag(music_rays[i], LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *center = label(visual, "", MUTED); bind_text(center, T_MUSIC_EQ, NULL, BIND_TEXT);
    lv_obj_set_width(center, 112); lv_obj_set_style_text_align(center, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(center);
    caption(box, T_MUSIC_SWIPE);
    music_info = label(box, "", MUTED); music_time = label(box, "", MUTED); music_bar = bar(box, CYAN, 8);
    lv_obj_t *line = row(box);
    grow(button(line, LV_SYMBOL_PREV, T_FILES_PREV, BTN_SECONDARY, music_step_event, (void *)(intptr_t)-1));
    music_pause_button = button(line, LV_SYMBOL_PAUSE, T_MUSIC_PAUSE, BTN_PRIMARY, music_pause_event, NULL); grow(music_pause_button);
    grow(button(line, LV_SYMBOL_NEXT, T_FILES_NEXT, BTN_SECONDARY, music_step_event, (void *)(intptr_t)1));
    line = row(box);
    grow(button(line, NULL, T_MUSIC_LIST, BTN_SECONDARY, music_panel_event, NULL));
    grow(button(line, NULL, T_MUSIC_EQ, BTN_SECONDARY, music_panel_event, (void *)(uintptr_t)1));
    static const m3_text_t repeats[] = {T_MUSIC_REPEAT_OFF, T_MUSIC_REPEAT_ON, T_MUSIC_REPEAT_ONE};
    music_repeat = segmented(box, repeats, NULL, 3, 0, music_repeat_event);
    static const m3_text_t shuffle[] = {T_MUSIC_SHUFFLE_OFF, T_MUSIC_SHUFFLE_ON};
    music_shuffle = segmented(box, shuffle, NULL, 2, 0, music_shuffle_event);
    volume_row(box, 4, volume);
    button(box, LV_SYMBOL_STOP, T_AUDIO_STOP, BTN_DANGER, music_stop_event, NULL);
    music_eq_panel = card(music, T_MUSIC_EQ);
    static const m3_text_t band_names[] = {T_MUSIC_BASS, T_MUSIC_MID, T_MUSIC_TREBLE};
    for (unsigned b = 0; b < 3; ++b) {
        line = row(music_eq_panel);
        lv_obj_t *name = label(line, "", TEXT); bind_text(name, band_names[b], NULL, BIND_TEXT); lv_obj_set_width(name, 62);
        music_eq_sliders[b] = lv_slider_create(line); style_slider(music_eq_sliders[b]); grow(music_eq_sliders[b]);
        lv_slider_set_range(music_eq_sliders[b], -12, 12);
        lv_obj_add_event_cb(music_eq_sliders[b], music_eq_event, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)b);
        music_eq_values[b] = label(line, "0 dB", TEXT); lv_obj_set_width(music_eq_values[b], 65);
    }
    button(music_eq_panel, NULL, T_MUSIC_FLAT, BTN_SECONDARY, music_eq_reset, NULL);
    button(music_eq_panel, NULL, T_NOW_PLAYING, BTN_SECONDARY, music_player_event, NULL);
    lv_obj_add_flag(music_eq_panel, LV_OBJ_FLAG_HIDDEN);
    box = card(music, T_SEC_MEDIA); music_library = box;
    static const m3_text_t sources[] = {T_STORAGE_SD, T_STORAGE_FLASH};
    music_source = segmented(box, sources, NULL, 2, 0, music_refresh_event);
    refresh_button(box,T_MUSIC_REFRESH,music_refresh_event,NULL);

    music_empty = label(box, tr(T_MUSIC_EMPTY), MUTED);
    for (unsigned i = 0; i < M3_MUSIC_ROWS; ++i) {
        music_rows[i] = button(box, LV_SYMBOL_PLAY, T_NONE, BTN_SECONDARY, music_track_event, (void *)(uintptr_t)i);
        music_labels[i] = lv_obj_get_child(music_rows[i], 0);
        lv_obj_set_width(music_labels[i], LV_PCT(94)); label_ellipsis(music_labels[i]);
        lv_obj_set_style_text_align(music_labels[i], LV_TEXT_ALIGN_LEFT, 0); lv_obj_add_flag(music_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    line = row(box);
    grow(button(line, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, music_page_event, (void *)(intptr_t)-1));
    grow(button(line, LV_SYMBOL_RIGHT, T_FILES_NEXT, BTN_SECONDARY, music_page_event, (void *)(intptr_t)1));
    caption(box, T_MUSIC_HINT);
    button(box, NULL, T_NOW_PLAYING, BTN_SECONDARY, music_player_event, NULL);
    lv_obj_add_flag(music_library, LV_OBJ_FLAG_HIDDEN);
}

static void tube_build_controls(lv_obj_t *parent, unsigned index) {
    tube_server_labels[index] = label(parent, tr(T_TUBE_SERVER_UNKNOWN), MUTED);
    caption(parent, T_TUBE_QUALITY);
    static const m3_text_t choices[] = {T_TUBE_LOW, T_TUBE_STANDARD, T_TUBE_HIGH};
    tube_quality_tabs[index] = segmented(parent, choices, NULL, 3, m3_tube_get_quality(), NULL);
    lv_obj_add_event_cb(tube_quality_tabs[index], tube_quality_changed, LV_EVENT_VALUE_CHANGED, tube_quality_tabs[index]);
    tube_download_labels[index] = label(parent, tr(T_TUBE_SAVE_HINT), MUTED);
    lv_obj_t *line = row(parent);
    grow(button(line, LV_SYMBOL_DIRECTORY, T_TUBE_SAVED, BTN_SECONDARY, tube_saved_folder, NULL));
    tube_cancel_buttons[index] = grow(button(line, LV_SYMBOL_STOP, T_TUBE_SAVE_CANCEL, BTN_SECONDARY, tube_cancel_save, NULL));
    lv_obj_add_flag(tube_cancel_buttons[index], LV_OBJ_FLAG_HIDDEN);
}
static void build_tube(lv_obj_t *tube) {
    /* Full-width video; the other content keeps the usual side margins. */
    lv_obj_set_style_pad_hor(tube, 0, 0); lv_obj_set_style_pad_top(tube, 0, 0);
    tube_search_view = bare(tube); lv_obj_set_size(tube_search_view, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tube_search_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(tube_search_view, 16, 0); lv_obj_set_style_pad_top(tube_search_view, 6, 0);
    lv_obj_set_style_pad_row(tube_search_view, 12, 0);
    tube_build_controls(tube_search_view, 0);
    tube_query = input_field(tube_search_view, T_NONE, 100, INPUT_KOREAN, true);
    bind_text(tube_query, T_TUBE_PLACEHOLDER, NULL, BIND_PLACEHOLDER);
    lv_obj_add_event_cb(tube_query, tube_search_event, LV_EVENT_READY, NULL);
    lv_obj_t *go = icon_button(lv_obj_get_parent(tube_query), LV_SYMBOL_RIGHT, tube_search_event, NULL);
    lv_obj_add_style(go, &st_primary, 0); lv_obj_add_style(go, &st_primary_pressed, LV_STATE_PRESSED);
    lv_obj_t *chips = row(tube_search_view);
    lv_obj_add_flag(chips, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(chips, LV_SCROLLBAR_MODE_OFF); lv_obj_set_style_pad_column(chips, 8, 0);
    for (unsigned i = 0; i < 6; ++i) {
        m3_text_t topic = (m3_text_t)(T_TUBE_TOPIC_1 + i);
        lv_obj_t *chip = button(chips, NULL, topic, BTN_SECONDARY, tube_chip, (void *)(uintptr_t)topic);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 40); lv_obj_set_style_radius(chip, 20, 0); lv_obj_set_style_pad_hor(chip, 18, 0);
        /* A 100% wide label inside a content-sized button resolves to zero width (no text). */
        lv_obj_t *chip_text = lv_obj_get_child(chip, 0);
        lv_obj_set_width(chip_text, LV_SIZE_CONTENT); lv_label_set_long_mode(chip_text, LV_LABEL_LONG_CLIP);
        lv_obj_center(chip_text);
    }
    lv_obj_t *line = row(tube_search_view);
    tube_heading = label(line, "", TEXT); grow(tube_heading); label_ellipsis(tube_heading);
    tube_back_search = button(line, LV_SYMBOL_LEFT, T_TUBE_BACK_SEARCH, BTN_SECONDARY, tube_back_to_search, NULL);
    lv_obj_set_size(tube_back_search, LV_SIZE_CONTENT, 40); lv_obj_set_style_pad_hor(tube_back_search, 14, 0);
    lv_obj_add_flag(tube_back_search, LV_OBJ_FLAG_HIDDEN);
    tube_status = caption(tube_search_view, T_TUBE_EMPTY);
    for (unsigned i = 0; i < M3_TUBE_ITEMS; ++i) {
        tube_row_t *item = &tube_rows[i];
        item->button = lv_button_create(tube_search_view); lv_obj_remove_style_all(item->button);
        lv_obj_add_style(item->button, &st_card, 0); lv_obj_add_style(item->button, &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(item->button, 8, 0); lv_obj_set_style_radius(item->button, 14, 0);
        lv_obj_set_flex_flow(item->button, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(item->button, 12, 0);
        lv_obj_add_event_cb(item->button, tube_open, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        item->thumb = bare(item->button); lv_obj_set_size(item->thumb, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H);
        lv_obj_set_style_radius(item->thumb, 10, 0); lv_obj_set_style_clip_corner(item->thumb, true, 0);
        lv_obj_set_style_bg_opa(item->thumb, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(item->thumb, lv_color_hex(SURFACE_HI), 0);
        lv_obj_t *placeholder = label(item->thumb, LV_SYMBOL_PLAY, LINE); lv_obj_set_width(placeholder, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(placeholder, FONT_ICON, 0); lv_obj_center(placeholder);
        item->canvas = lv_canvas_create(item->thumb); lv_obj_add_flag(item->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(item->canvas, LV_OBJ_FLAG_CLICKABLE);
        item->badge = label(item->thumb, "", 0xffffff); lv_obj_set_width(item->badge, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(item->badge, LV_OPA_80, 0); lv_obj_set_style_radius(item->badge, 4, 0);
        lv_obj_set_style_pad_hor(item->badge, 5, 0); lv_obj_set_style_pad_ver(item->badge, 1, 0);
        lv_obj_set_style_text_font(item->badge, &m3_font_korean, 0);
        lv_obj_align(item->badge, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
        lv_obj_t *texts = bare(item->button); grow(texts); lv_obj_set_height(texts, M3_TUBE_THUMB_H);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(texts, 3, 0);
        item->title = label(texts, "", TEXT); lv_obj_set_height(item->title, 42);
        label_ellipsis(item->title); lv_obj_set_style_text_line_space(item->title, 2, 0);
        item->channel = label(texts, "", MUTED); label_ellipsis(item->channel);
        item->meta = label(texts, "", MUTED); label_ellipsis(item->meta);
        lv_obj_add_flag(item->button, LV_OBJ_FLAG_HIDDEN);
    }
    tube_pager = row(tube_search_view);
    tube_prev = grow(button(tube_pager, LV_SYMBOL_LEFT, T_FILES_PREV, BTN_SECONDARY, tube_page_turn, (void *)(intptr_t)-1));
    tube_page = label(tube_pager, "", MUTED); lv_obj_set_width(tube_page, 90); lv_obj_set_style_text_align(tube_page, LV_TEXT_ALIGN_CENTER, 0);
    tube_next = grow(button(tube_pager, NULL, T_FILES_NEXT, BTN_SECONDARY, tube_page_turn, (void *)(intptr_t)1));
    lv_obj_add_flag(tube_pager, LV_OBJ_FLAG_HIDDEN);
    caption(tube_search_view, T_TUBE_NOTES);

    tube_player_view = bare(tube); lv_obj_set_size(tube_player_view, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tube_player_view, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(tube_player_view, 12, 0);
    lv_obj_t *screen = bare(tube_player_view); lv_obj_set_size(screen, M3_TUBE_VIDEO_W, M3_TUBE_VIDEO_H);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(screen, lv_color_hex(0), 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(screen, tube_toggle_event, LV_EVENT_CLICKED, NULL);
    tube_video = lv_canvas_create(screen); lv_obj_center(tube_video); lv_obj_remove_flag(tube_video, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(tube_video, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(tube_video, LV_PCT(50), 0);
    tube_overlay = label(screen, "", 0xffffff); lv_obj_set_width(tube_overlay, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tube_overlay, LV_OPA_70, 0); lv_obj_set_style_bg_color(tube_overlay, lv_color_hex(0), 0);
    lv_obj_set_style_radius(tube_overlay, 16, 0); lv_obj_set_style_pad_hor(tube_overlay, 16, 0); lv_obj_set_style_pad_ver(tube_overlay, 8, 0);
    lv_obj_center(tube_overlay); lv_obj_add_flag(tube_overlay, LV_OBJ_FLAG_HIDDEN);
    tube_wait = activity_create(screen, ACTIVITY_DOTS, 0xffffff, T_NONE); lv_obj_align(tube_wait, LV_ALIGN_CENTER, 0, 40);
    lv_obj_t *body = bare(tube_player_view); lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_hor(body, 16, 0); lv_obj_set_style_pad_row(body, 12, 0);
    line = row(body);
    tube_time = label(line, "0:00", MUTED); lv_obj_set_width(tube_time, 64);
    tube_seek = lv_slider_create(line); style_slider(tube_seek); grow(tube_seek);
    lv_slider_set_range(tube_seek, 0, 1);
    lv_obj_add_event_cb(tube_seek, tube_seek_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(tube_seek, tube_seek_event, LV_EVENT_RELEASED, NULL);
    tube_total = label(line, "--:--", MUTED); lv_obj_set_width(tube_total, 64); lv_obj_set_style_text_align(tube_total, LV_TEXT_ALIGN_RIGHT, 0);
    line = row(body); lv_obj_set_style_flex_main_place(line, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_t *back = icon_button(line, LV_SYMBOL_PREV, tube_skip, (void *)(intptr_t)-10); lv_obj_set_size(back, 64, 56);
    tube_toggle = icon_button(line, LV_SYMBOL_PAUSE, tube_toggle_event, NULL);
    lv_obj_add_style(tube_toggle, &st_primary, 0); lv_obj_add_style(tube_toggle, &st_primary_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(tube_toggle, 72, 72); lv_obj_set_style_radius(tube_toggle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(tube_toggle, 0), FONT_ICON, 0);
    lv_obj_t *forward = icon_button(line, LV_SYMBOL_NEXT, tube_skip, (void *)(intptr_t)10); lv_obj_set_size(forward, 64, 56);
    lv_obj_t *list_button = icon_button(line, LV_SYMBOL_LIST, tube_close, NULL); lv_obj_set_size(list_button, 64, 56);
    tube_title = label(body, "", TEXT); lv_obj_set_style_text_line_space(tube_title, 4, 0);
    line = row(body);
    tube_channel = label(line, "", MUTED); grow(tube_channel); label_ellipsis(tube_channel);
    tube_channel_button = button(line, LV_SYMBOL_LIST, T_TUBE_CHANNEL, BTN_SECONDARY, tube_channel_event, NULL);
    lv_obj_set_size(tube_channel_button, LV_SIZE_CONTENT, 44); lv_obj_set_style_pad_hor(tube_channel_button, 14, 0);
    tube_player_status = label(body, "", MUTED);
    tube_build_controls(body, 1);
    tube_save_button = button(body, LV_SYMBOL_DOWNLOAD, T_TUBE_SAVE, BTN_SECONDARY, tube_save, NULL);
    lv_obj_t *box = card(body, T_SEC_VOLUME);
    volume_row(box, 3, (unsigned)lv_slider_get_value(volume_sliders[0]));
    lv_obj_add_flag(tube_player_view, LV_OBJ_FLAG_HIDDEN);
}
static void build_monitor(lv_obj_t *monitor) {
    lv_obj_t *box = card(monitor, T_SEC_WEB);
    static const m3_text_t web_choices[] = {T_WEB_DOMAIN, T_WEB_IPV6, T_WEB_IPV4, T_WEB_MDNS};
    web_tabs = segmented(box, web_choices, NULL, M3_WEB_COUNT, web_address_kind, web_tab_changed);
    lv_obj_t *state_row = row(box);
    web_dot = dot(state_row, AMBER);
    web_state = label(state_row, "", TEXT); grow(web_state);
    lv_obj_t *controls = row(box);
    web_controls[M3_WEB_STOP_DOMAIN] = grow(button(controls, NULL, T_WEB_STOP_DOMAIN, BTN_SECONDARY, web_control, (void *)M3_WEB_STOP_DOMAIN));
    web_controls[M3_WEB_STOP_ALL] = grow(button(controls, NULL, T_WEB_STOP_ALL, BTN_DANGER, web_control, (void *)M3_WEB_STOP_ALL));
    controls = row(box);
    web_controls[M3_WEB_START_DOMAIN] = grow(button(controls, NULL, T_WEB_START_DOMAIN, BTN_SECONDARY, web_control, (void *)M3_WEB_START_DOMAIN));
    web_controls[M3_WEB_START_ALL] = grow(button(controls, NULL, T_WEB_START_ALL, BTN_PRIMARY, web_control, (void *)M3_WEB_START_ALL));
    caption(box, T_WEB_CONTROL_HINT);
    web_control_info = label(box, "", MUTED);
    /* 32 px white quiet zone around the 216 px modules, independent of dark theme. */
    lv_obj_t *qr_row = row(box);
    lv_obj_set_flex_align(qr_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    web_frame = bare(qr_row); lv_obj_set_size(web_frame, 280, 280);
    lv_obj_set_style_bg_color(web_frame, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(web_frame, LV_OPA_COVER, 0);
    web_qr = lv_qrcode_create(web_frame); lv_qrcode_set_size(web_qr, 216);
    lv_qrcode_set_dark_color(web_qr, lv_color_hex(0x000000)); lv_qrcode_set_light_color(web_qr, lv_color_hex(0xffffff));
    lv_obj_center(web_qr); lv_obj_add_flag(web_frame, LV_OBJ_FLAG_HIDDEN);
    web_url = label(box, "", CYAN);
    label_ellipsis(web_url);
    web_hint = label(box, "", MUTED); lv_label_set_long_mode(web_hint, LV_LABEL_LONG_WRAP);
    web_retry = button(box, NULL, T_WEB_RETRY, BTN_SECONDARY, web_retry_ipv6, NULL);
    lv_obj_add_flag(web_retry, LV_OBJ_FLAG_HIDDEN);
    web_requests = label(box, "", MUTED);
    box = card(monitor, T_SEC_MEMORY);
    memory = label(box, "", TEXT); lv_obj_set_style_text_line_space(memory, 4, 0);
    heap_bar = bar(box, MAGENTA, 10);
    box = card(monitor, T_SEC_HISTORY);
    lv_obj_t *legend = caption(box, T_MON_LEGEND); (void)legend;
    chart = lv_chart_create(box); lv_obj_set_size(chart, LV_PCT(100), 200);
    lv_obj_set_style_bg_color(chart, lv_color_hex(FIELD), 0); lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 12, 0); lv_obj_set_style_line_color(chart, lv_color_hex(SURFACE_HI), 0);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); lv_chart_set_point_count(chart, 60);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 640);
    heap_series = lv_chart_add_series(chart, lv_color_hex(CYAN), LV_CHART_AXIS_PRIMARY_Y);
    psram_series = lv_chart_add_series(chart, lv_color_hex(MAGENTA), LV_CHART_AXIS_PRIMARY_Y);
    box = card(monitor, T_SEC_SERVICES);
    monitor_info = label(box, "", MUTED); lv_obj_set_style_text_line_space(monitor_info, 4, 0);
}
static void sound_changed(lv_event_t *event) {
    unsigned channel = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (channel >= 3) return;
    m3_feedback_settings_t p; m3_feedback_get(&p);
    bool enabled = lv_obj_has_state(sound_switches[channel], LV_STATE_CHECKED);
    unsigned volume = lv_slider_get_value(sound_sliders[channel]);
    if (channel == 0) { p.boot_enabled = enabled; p.boot_volume = volume; }
    else if (channel == 1) { p.effects_enabled = enabled; p.effects_volume = volume; }
    else { p.guidance_enabled = enabled; p.guidance_volume = volume; }
    m3_feedback_update(&p);
    char value[16]; snprintf(value, sizeof(value), "%u%%", volume); set_text(sound_values[channel], value);
}
static void sound_preview(lv_event_t *event) {
    unsigned channel = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    m3_feedback_clear();
    m3_feedback_emit(channel == 0 ? M3_CUE_BOOT : channel == 1 ? M3_CUE_TAP : M3_CUE_READY, m3_i18n_language());
}
static void guidance_changed(lv_event_t *event) {
    (void)event; m3_feedback_settings_t p; m3_feedback_get(&p);
    p.guidance_mode = segment_value(guidance_mode); m3_feedback_update(&p);
    m3_feedback_clear(); m3_feedback_emit(M3_CUE_ACCESSIBILITY, m3_i18n_language());
}
static void accessibility_open(lv_event_t *event) {
    (void)event;
    bool hidden = lv_obj_has_flag(accessibility_panel, LV_OBJ_FLAG_HIDDEN);
    if (hidden) {
        lv_obj_remove_flag(accessibility_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(accessibility_panel, ui_animation());
        m3_feedback_emit(M3_CUE_ACCESSIBILITY, m3_i18n_language());
    } else lv_obj_add_flag(accessibility_panel, LV_OBJ_FLAG_HIDDEN);
}
static void sound_control(lv_obj_t *box, m3_text_t name, unsigned channel, bool enabled, unsigned volume) {
    lv_obj_t *line = row(box); grow(text(line, name, TEXT));
    sound_switches[channel] = lv_switch_create(line); lv_obj_set_size(sound_switches[channel], 60, 32);
    if (enabled) lv_obj_add_state(sound_switches[channel], LV_STATE_CHECKED);
    lv_obj_add_event_cb(sound_switches[channel], sound_changed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)channel);
    sound_values[channel] = label(box, "", CYAN);
    char value[16]; snprintf(value, sizeof(value), "%u%%", volume); set_text(sound_values[channel], value);
    sound_sliders[channel] = lv_slider_create(box); style_slider(sound_sliders[channel]);
    lv_slider_set_range(sound_sliders[channel], 0, 100);
    lv_slider_set_value(sound_sliders[channel], volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(sound_sliders[channel], sound_changed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)channel);
    button(box, LV_SYMBOL_PLAY, T_SOUND_PREVIEW, BTN_SECONDARY, sound_preview, (void *)(uintptr_t)channel);
}
#include "m3_sound_ui.inc"
static void settings_changed(lv_event_t *event) {
    unsigned kind = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    m3_preferences_t p; m3_preferences_get(&p);
    if (kind == 0) p.brightness = lv_slider_get_value(brightness_slider);
    else if (kind == 1) p.max_fps = lv_slider_get_value(fps_slider);
    else if (kind == 2) p.animations = lv_obj_has_state(animation_switch, LV_STATE_CHECKED);
    else if (kind == 3) { m3_audio_volume(lv_obj_has_state(mute_switch, LV_STATE_CHECKED) ? 0 : p.last_volume); return; }
    m3_preferences_update(&p);
}
static void typography_changed(lv_event_t *event) {
    (void)event;
    m3_typography_t p = {1, segment_value(font_family_tabs), lv_slider_get_value(font_size_slider), 0};
    if (m3_typography_update(&p)) apply_typography();
}
static void typography_lines(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_textarea_class) && lv_textarea_get_one_line(obj)) {
        int height = lv_obj_get_style_text_font(obj, LV_PART_MAIN)->line_height + 28;
        lv_obj_set_height(obj, height < 52 ? 52 : height);
    }
    if (lv_obj_check_type(obj, &lv_label_class) && lv_label_get_long_mode(obj) == LV_LABEL_LONG_DOT) {
        bool job = false;
        for (unsigned i = 0; i < 6; ++i) if (obj == job_labels[i]) job = true;
        if (!job) lv_obj_set_height(obj, lv_obj_get_style_text_font(obj, LV_PART_MAIN)->line_height);
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i) typography_lines(lv_obj_get_child(obj, i));
}
static void apply_typography(void) {
    m3_typography_get(&ui_typography);
    const lv_font_t *font = m3_system_font(ui_typography.pixels, ui_typography.family);
    if (applied_font_family != ui_typography.family || applied_font_pixels != ui_typography.pixels) {
        applied_font_family = ui_typography.family; applied_font_pixels = ui_typography.pixels;
        lv_obj_t *screen = lv_display_get_screen_active(m3_board_display());
        lv_obj_set_style_text_font(screen, font, 0);
        lv_style_set_text_font(&st_title, m3_system_font(ui_typography.pixels + 4, ui_typography.family));
        const lv_font_t *compact = m3_system_font(ui_typography.pixels > 20 ? 20 : ui_typography.pixels, ui_typography.family);
        lv_style_set_text_font(&st_caption, compact);
        lv_obj_set_style_text_font(shade, compact, 0);
        lv_obj_report_style_change(&st_title); lv_obj_report_style_change(&st_caption);
        lv_obj_set_style_text_font(font_preview, font, 0);
        typography_lines(screen);
        int status_y = (44 - font->line_height) / 2;
        lv_obj_set_y(status_clock, status_y); lv_obj_set_y(status, status_y);
        lv_obj_invalidate(screen);
    }
    if (segment_value(font_family_tabs) != ui_typography.family) segment_select(font_family_tabs, ui_typography.family);
    if (!lv_obj_has_state(font_size_slider, LV_STATE_PRESSED)) lv_slider_set_value(font_size_slider, ui_typography.pixels, LV_ANIM_OFF);
    char value[24]; snprintf(value, sizeof(value), "%u px", ui_typography.pixels); set_text(font_size_value, value);
}
static void wake_mode_changed(lv_event_t *event) {
    (void)event;
    m3_preferences_t p; m3_preferences_get(&p);
    p.wake_mode = segment_value(wake_tabs);
    m3_preferences_update(&p);
}
static void system_restart(lv_event_t *event) {
    unsigned action = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (action == 0) {
        lv_obj_remove_flag(restart_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(restart_confirm, LV_ANIM_OFF); return;
    }
    lv_obj_add_flag(restart_confirm, LV_OBJ_FLAG_HIDDEN);
    if (action == 1) return;
    m3_preferences_t p; m3_preferences_get(&p);
    m3_mode_result_t result = m3_preferences_restart_with(&p);
    set_text(restart_info, tr(result == M3_MODE_OK ? T_SET_RESTARTING : result == M3_MODE_BUSY ? T_RESTART_BUSY :
        result == M3_MODE_RESTART_FAILED ? T_SET_RESTART_MANUAL : T_SET_SAVE_FAILED));
    lv_obj_scroll_to_view(restart_info, LV_ANIM_OFF);
}
static void apply_operation_mode(lv_event_t *event) {
    (void)event;
    m3_mode_result_t result = m3_preferences_request_mode(lv_obj_has_state(focus_switch, LV_STATE_CHECKED));
    set_text(settings_info, tr(result == M3_MODE_OK ? T_SET_RESTARTING : result == M3_MODE_BUSY ? T_SET_BUSY :
        result == M3_MODE_RESTART_FAILED ? T_SET_RESTART_MANUAL : T_SET_SAVE_FAILED));
}
static void update_preferences(void) {
    m3_preferences_get(&ui_preferences);
    apply_typography();
    if (!screen_blanked && applied_brightness != ui_preferences.brightness) {
        applied_brightness = ui_preferences.brightness;
        if (m3_board_set_brightness(applied_brightness) != ESP_OK) set_text(settings_info, tr(T_SET_BRIGHTNESS_ERROR));
    }
    if (applied_fps != ui_preferences.max_fps) {
        applied_fps = ui_preferences.max_fps;
        lv_display_t *display = m3_board_display();
        if (display) lv_timer_set_period(lv_display_get_refr_timer(display), m3_preferences_frame_ms(applied_fps));
    }
    if (applied_animations != ui_preferences.animations) {
        bool first = applied_animations == 255;
        applied_animations = ui_preferences.animations;
        if (!first) {
            lv_anim_delete(NULL, NULL);
            int32_t scroll = lv_obj_get_scroll_y(pages[current_app]);
            show_app(current_app);
            lv_obj_scroll_to_y(pages[current_app], scroll, LV_ANIM_OFF);
            lv_obj_t *activities[] = {tube_wait, agent_thinking, agent_voice, voice_think, tts_wait, tts_talk};
            for (unsigned i = 0; i < 6; ++i) lv_obj_add_flag(activities[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    char text[32]; snprintf(text, sizeof(text), "%u%%", ui_preferences.brightness); set_text(brightness_value, text);
    snprintf(text, sizeof(text), "%u FPS", ui_preferences.max_fps); set_text(fps_value, text);
    if (!lv_obj_has_state(brightness_slider, LV_STATE_PRESSED)) lv_slider_set_value(brightness_slider, ui_preferences.brightness, LV_ANIM_OFF);
    if (!lv_obj_has_state(fps_slider, LV_STATE_PRESSED)) lv_slider_set_value(fps_slider, ui_preferences.max_fps, LV_ANIM_OFF);
    if (ui_preferences.volume == 0) lv_obj_add_state(mute_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(mute_switch, LV_STATE_CHECKED);
    if (ui_preferences.animations) lv_obj_add_state(animation_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(animation_switch, LV_STATE_CHECKED);
    if (segment_value(wake_tabs) != ui_preferences.wake_mode) segment_select(wake_tabs, ui_preferences.wake_mode);
    theme_wallpaper_sync();
}
static lv_obj_t *settings_switch(lv_obj_t *box, m3_text_t title, bool checked, unsigned kind) {
    lv_obj_t *line = row(box); grow(text(line, title, TEXT));
    lv_obj_t *toggle = lv_switch_create(line); lv_obj_set_size(toggle, 60, 32);
    if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    if (kind < 4) lv_obj_add_event_cb(toggle, settings_changed, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)kind);
    return toggle;
}
enum { SET_TAB_DISPLAY, SET_TAB_THEME, SET_TAB_SOUND, SET_TAB_SYSTEM, SET_TAB_COUNT };
static lv_obj_t *settings_tabs, *settings_pages[SET_TAB_COUNT];
static void settings_tab_changed(lv_event_t *event) {
    (void)event; unsigned tab = segment_value(settings_tabs);
    for (unsigned i = 0; i < SET_TAB_COUNT; ++i) {
        if (i == tab) lv_obj_remove_flag(settings_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(settings_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_scroll_to_y(pages[APP_SYSTEM], 0, LV_ANIM_OFF);
}
/* Settings are grouped into Display, Theme, Sound and System tabs; only one is shown. */
static void settings_tab_pages(lv_obj_t *system) {
    static const m3_text_t names[SET_TAB_COUNT] = {T_SET_SCREEN, T_THEME, T_SET_SOUND, T_SET_TAB_SYSTEM};
    settings_tabs = segmented(system, names, NULL, SET_TAB_COUNT, SET_TAB_DISPLAY, settings_tab_changed);
    for (unsigned i = 0; i < SET_TAB_COUNT; ++i) {
        settings_pages[i] = bare(system); lv_obj_set_size(settings_pages[i], LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(settings_pages[i], LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(settings_pages[i], 14, 0);
        if (i != SET_TAB_DISPLAY) lv_obj_add_flag(settings_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}
static void build_settings(lv_obj_t *system) {
    m3_preferences_t p; m3_preferences_get(&p);
    settings_tab_pages(system);
    lv_obj_t *display = settings_pages[SET_TAB_DISPLAY], *sound_tab = settings_pages[SET_TAB_SOUND], *info = settings_pages[SET_TAB_SYSTEM];
    lv_obj_t *box = card(display, T_SET_SCREEN);
    text(box, T_SET_BRIGHTNESS, TEXT);
    brightness_value = label(box, "", CYAN);
    brightness_slider = lv_slider_create(box); style_slider(brightness_slider);
    lv_slider_set_range(brightness_slider, 5, 100); lv_slider_set_value(brightness_slider, p.brightness, LV_ANIM_OFF);
    lv_obj_set_width(brightness_slider, LV_PCT(100));
    lv_obj_add_event_cb(brightness_slider, settings_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
    caption(box, T_SET_BRIGHTNESS_HINT);
    text(box, T_SET_WAKE, TEXT);
    static const m3_text_t wake_choices[] = {T_WAKE_HOLD, T_WAKE_DOUBLE_TAP};
    wake_tabs = segmented(box, wake_choices, NULL, 2, p.wake_mode, wake_mode_changed);
    caption(box, T_SET_WAKE_HINT);
    theme_card(settings_pages[SET_TAB_THEME]);
    box = card(display, T_SET_FONT);
    static const m3_text_t font_names[] = {T_READER_GOTHIC, T_READER_GOTHIC_BOLD, T_READER_MYEONGJO};
    font_family_tabs = segmented(box, font_names, NULL, M3_READER_FONT_COUNT, ui_typography.family, typography_changed);
    text(box, T_SET_FONT_SIZE, TEXT); font_size_value = label(box, "", CYAN);
    font_size_slider = lv_slider_create(box); style_slider(font_size_slider);
    lv_obj_set_width(font_size_slider, LV_PCT(100));
    lv_slider_set_range(font_size_slider, M3_SYSTEM_FONT_MIN, M3_SYSTEM_FONT_MAX);
    lv_slider_set_value(font_size_slider, ui_typography.pixels, LV_ANIM_OFF);
    lv_obj_add_event_cb(font_size_slider, typography_changed, LV_EVENT_VALUE_CHANGED, NULL);
    caption(box, T_SET_FONT_PREVIEW);
    font_preview = text(box, T_SET_FONT_SAMPLE, TEXT);
    lv_label_set_long_mode(font_preview, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(font_preview, 6, 0);
    caption(box, T_SET_FONT_HINT);
    box = card(sound_tab, T_SET_SOUND);
    volume_row(box, 5, p.volume);
    mute_switch = settings_switch(box, T_SET_MUTE, p.volume == 0, 3);
    sound_card = box;
    m3_feedback_settings_t sound; m3_feedback_get(&sound);
    caption(box, T_SOUND_HINT);
    sound_pack_list(box);
    sound_control(box, T_SOUND_BOOT, 0, sound.boot_enabled, sound.boot_volume);
    sound_control(box, T_SOUND_EFFECTS, 1, sound.effects_enabled, sound.effects_volume);
    sound_control(box, T_SOUND_GUIDANCE, 2, sound.guidance_enabled, sound.guidance_volume);
    button(box, LV_SYMBOL_SETTINGS, T_ACCESSIBILITY, BTN_SECONDARY, accessibility_open, NULL);
    accessibility_panel = card(sound_tab, T_ACCESSIBILITY);
    static const m3_text_t guidance_options[] = {T_GUIDANCE_VOICE, T_GUIDANCE_TONES};
    guidance_mode = segmented(accessibility_panel, guidance_options, NULL, 2, sound.guidance_mode, guidance_changed);
    caption(accessibility_panel, T_GUIDANCE_HINT);
    button(accessibility_panel, LV_SYMBOL_PLAY, T_SOUND_PREVIEW, BTN_SECONDARY, sound_preview, (void *)2);
    lv_obj_add_flag(accessibility_panel, LV_OBJ_FLAG_HIDDEN);
    box = card(display, T_SET_PERFORMANCE);
    text(box, T_SET_FPS, TEXT); fps_value = label(box, "", CYAN);
    fps_slider = lv_slider_create(box); style_slider(fps_slider);
    lv_slider_set_range(fps_slider, 5, 60); lv_slider_set_value(fps_slider, p.max_fps, LV_ANIM_OFF);
    lv_obj_set_width(fps_slider, LV_PCT(100));
    lv_obj_add_event_cb(fps_slider, settings_changed, LV_EVENT_VALUE_CHANGED, (void *)1);
    animation_switch = settings_switch(box, T_SET_ANIMATIONS, p.animations, 2);
    caption(box, T_SET_FPS_HINT);
    box = card(info, T_SET_OPERATION);
    focus_switch = settings_switch(box, T_SET_WEB_FOCUS, p.web_focus, 4);
    caption(box, T_SET_FOCUS_HINT);
    button(box, LV_SYMBOL_REFRESH, T_SET_APPLY_MODE, BTN_PRIMARY, apply_operation_mode, NULL);
    settings_info = text(box, T_SET_AUTO_SAVE, MUTED);
    box = card(info, T_SYSTEM_RESTART);
    caption(box, T_RESTART_HINT);
    button(box, LV_SYMBOL_REFRESH, T_SYSTEM_RESTART, BTN_SECONDARY, system_restart, (void *)0);
    restart_confirm = bare(box);
    lv_obj_set_size(restart_confirm, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(restart_confirm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(restart_confirm, 12, 0);
    caption(restart_confirm, T_RESTART_CONFIRM);
    lv_obj_t *actions = row(restart_confirm);
    grow(button(actions, LV_SYMBOL_CLOSE, T_RESTART_CANCEL, BTN_SECONDARY, system_restart, (void *)1));
    grow(button(actions, LV_SYMBOL_REFRESH, T_RESTART_NOW, BTN_PRIMARY, system_restart, (void *)2));
    lv_obj_add_flag(restart_confirm, LV_OBJ_FLAG_HIDDEN);
    restart_info = label(box, "", MUTED);
    box = card(display, T_SEC_LANGUAGE);
    static const m3_text_t languages[] = {T_LANG_KO, T_LANG_EN, T_LANG_JA};
    language_tabs = segmented(box, languages, NULL, M3_LANG_COUNT, m3_i18n_language(), language_changed);
    box = card(sound_tab, T_SEC_VOICE);
    m3_voice_state_t initial_voice; m3_voice_state(&initial_voice);
    /* Index order matches m3_voice_set_provider(): 0 OpenAI mini-transcribe, 1 Gemini 3.1, 2 Gemini 3.5, 3 Azure. */
    static const char *const recognizers[] = {"OpenAI", "Gemini 3.1", "Gemini 3.5", "Azure"};
    voice_provider = segmented(box, NULL, recognizers, 4, initial_voice.provider, voice_provider_changed);
    caption(box, T_STT_HINT);
    voice_key = input_field(box, T_STT_KEY, 255, INPUT_TEXT, false);
    lv_textarea_set_password_mode(voice_key, true);
    button(box, LV_SYMBOL_SAVE, T_STT_KEY_SAVE, BTN_SECONDARY, voice_save_key, NULL);
    voice_key_info = label(box, "", MUTED);
    voice_key_status();
    box = card(info, T_SET_FLASH_USAGE);
    m3_flash_usage_view_build(box, &(m3_flash_view_style_t){TEXT, MUTED, CYAN, SURFACE_HI, &st_btn, &st_btn_pressed});
    box = card(info, T_SEC_ABOUT);
    system_info = label(box, "", MUTED); lv_obj_set_style_text_line_space(system_info, 4, 0);
}
static void build_voice_panel(lv_obj_t *screen) {
    voice_panel = lv_obj_create(screen); lv_obj_remove_style_all(voice_panel);
    lv_obj_set_pos(voice_panel, 0, 0); lv_obj_set_size(voice_panel, 480, HOME_TOP);
    lv_obj_set_style_bg_color(voice_panel, lv_color_hex(0), 0); lv_obj_set_style_bg_opa(voice_panel, LV_OPA_60, 0);
    lv_obj_remove_flag(voice_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sheet = card(voice_panel, T_NONE);
    lv_obj_set_width(sheet, 456); lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(sheet, 24, 0); lv_obj_set_style_pad_all(sheet, 20, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(LINE), 0); lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_t *line = row(sheet);
    voice_dot = dot(line, RED); lv_obj_set_size(voice_dot, 14, 14);
    lv_obj_t *title = text(line, T_VOICE_TITLE, TEXT); grow(title); lv_obj_add_style(title, &st_title, 0);
    icon_button(line, LV_SYMBOL_SETTINGS, voice_settings, NULL);
    voice_info = label(sheet, "", MUTED);
    /* Live microphone meter: bar heights follow the measured loudness. */
    voice_meter = row(sheet); lv_obj_set_style_pad_column(voice_meter, 14, 0);
    lv_obj_t *bars = row(voice_meter);
    lv_obj_set_size(bars, LV_SIZE_CONTENT, METER_MAX); lv_obj_set_style_pad_column(bars, 4, 0);
    for (unsigned i = 0; i < METER_BARS; ++i) {
        voice_meter_bars[i] = bare(bars); lv_obj_set_size(voice_meter_bars[i], 6, METER_MIN);
        lv_obj_set_style_radius(voice_meter_bars[i], 3, 0); lv_obj_set_style_bg_opa(voice_meter_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(voice_meter_bars[i], lv_color_hex(RED), 0);
    }
    lv_obj_t *meter_text = bare(voice_meter); grow(meter_text); lv_obj_set_height(meter_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(meter_text, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(meter_text, 2, 0);
    text(meter_text, T_VOICE_RECORDING, TEXT);
    voice_db = label(meter_text, "", MUTED);
    lv_obj_add_flag(voice_meter, LV_OBJ_FLAG_HIDDEN);
    voice_think = activity_create(sheet, ACTIVITY_DOTS, AMBER, T_VOICE_RECOGNIZING);
    lv_obj_t *box = bare(sheet); lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(box, 110, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(FIELD), 0); lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 14, 0); lv_obj_set_style_pad_all(box, 14, 0);
    voice_text = label(box, "", TEXT); lv_obj_set_style_text_line_space(voice_text, 5, 0);
    button(sheet, LV_SYMBOL_STOP, T_VOICE_FINISH, BTN_PRIMARY, voice_finish, NULL);
    line = row(sheet);
    grow(button(line, LV_SYMBOL_OK, T_VOICE_USE, BTN_SECONDARY, voice_apply, NULL));
    grow(button(line, LV_SYMBOL_REFRESH, T_VOICE_RETRY, BTN_SECONDARY, voice_retry, NULL));
    button(sheet, LV_SYMBOL_CLOSE, T_VOICE_CANCEL, BTN_DANGER, voice_cancel, NULL);
    lv_obj_add_flag(voice_panel, LV_OBJ_FLAG_HIDDEN);
}
static void style_keyboard(void) {
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(KEY_BG), 0); lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0); lv_obj_set_style_radius(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 6, 0); lv_obj_set_style_pad_gap(keyboard, 6, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(KEY_ITEM), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(TEXT), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS); lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(KEY_CHECKED), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(KEY_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(CYAN), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(ON_ACCENT), LV_PART_ITEMS | LV_STATE_PRESSED);
}
/* Re-render every bound label, control and status after a language switch. */
static void apply_language(void) {
    for (unsigned i = 0; i < binding_count; ++i) bind_apply(i);
    for (unsigned i = 0; i < segment_count; ++i) segment_refresh(&segments[i]);
    m3_flash_usage_view_refresh();
    lv_label_set_text(app_title, tr(app_names[current_app]));
    seen_agent = seen_voice = seen_wifi = seen_scan = seen_files = ~0u;
    web_qr_dirty = true;
    seen_url[0] = seen_audio[0] = seen_web_url[0] = 0; /* Re-apply live QR URL and audio status over the static captions. */
    render_game(); voice_key_status(); update_metrics(NULL);
    calendar_stamp = 0; update_calendar(); home_page_update();
}
esp_err_t m3_ui_start(void) {
    if (!m3_board_display() || !lvgl_port_lock(1000)) return ESP_ERR_INVALID_STATE;
    m3_i18n_load();
    m3_preferences_get(&ui_preferences);
    m3_typography_get(&ui_typography);
    m3_theme_load(&ui_theme); theme_resolve();
    styles_init(); theme_styles();
    lv_obj_t *screen = lv_display_get_screen_active(m3_board_display());
    lv_theme_t *theme = lv_theme_default_init(m3_board_display(), lv_color_hex(CYAN), lv_color_hex(MAGENTA), theme_dark(), &m3_font_korean);
    lv_display_set_theme(m3_board_display(), theme);
    lv_obj_set_style_bg_color(screen, lv_color_hex(INK), 0);
    lv_obj_set_style_text_font(screen, m3_system_font(ui_typography.pixels, ui_typography.family), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(TEXT), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    /* Status bar: time left, network/audio state right. Pull down for the activity shade. */
    status_strip = lv_obj_create(screen); lv_obj_remove_style_all(status_strip);
    lv_obj_set_pos(status_strip, 0, 0); lv_obj_set_size(status_strip, 480, 44);
    lv_obj_remove_flag(status_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(status_strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(status_strip, shade_touch, LV_EVENT_ALL, NULL);
    status_clock = label(status_strip, "--:--", TEXT); lv_obj_set_pos(status_clock, 22, 13); lv_obj_set_width(status_clock, 80);
    status = label(status_strip, "", MUTED); lv_obj_set_pos(status, 108, 13); lv_obj_set_width(status, 150);
    label_ellipsis(status);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
    status_audio = label(status_strip, LV_SYMBOL_AUDIO, CYAN); lv_obj_set_pos(status_audio, 270, 13);
    lv_obj_set_style_text_font(status_audio, &lv_font_montserrat_20, 0);
    lv_obj_set_width(status_audio, 20); lv_obj_add_flag(status_audio, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *signal = bare(status_strip); lv_obj_set_pos(signal, 294, 8); lv_obj_set_size(signal, 44, 28);
    for (unsigned i = 0; i < 4; ++i) wifi_levels[i] = icon_part(signal, 8 + i * 8, 22 - (i + 1) * 5, 5, (i + 1) * 5, 2);
    /* Vector plug and battery work with all language fonts; no emoji glyphs. */
    power_plug = bare(status_strip); lv_obj_set_pos(power_plug, 342, 10); lv_obj_set_size(power_plug, 20, 26);
    glyph_part(power_plug, 4, 1, 3, 7, 1); glyph_part(power_plug, 12, 1, 3, 7, 1);
    glyph_part(power_plug, 2, 8, 15, 9, 3); glyph_part(power_plug, 8, 17, 3, 7, 1);
    power_battery = bare(status_strip); lv_obj_set_pos(power_battery, 366, 13); lv_obj_set_size(power_battery, 33, 18);
    lv_obj_t *outline = bare(power_battery); lv_obj_set_size(outline, 28, 18);
    lv_obj_set_style_border_width(outline, 2, 0); lv_obj_set_style_border_color(outline, lv_color_hex(TEXT), 0);
    lv_obj_set_style_border_opa(outline, LV_OPA_COVER, 0); lv_obj_set_style_radius(outline, 3, 0);
    glyph_part(power_battery, 29, 5, 3, 8, 1);
    power_fill = icon_part(power_battery, 3, 3, 22, 12, 1);
    power_charge = label(power_battery, LV_SYMBOL_CHARGE, CYAN);
    lv_obj_set_style_text_font(power_charge, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(power_charge, 9, 1);
    lv_obj_set_style_text_color(power_charge, lv_color_hex(ON_ACCENT), 0);
    lv_obj_set_style_bg_color(power_charge, lv_color_hex(CYAN), 0);
    lv_obj_set_style_bg_opa(power_charge, LV_OPA_COVER, 0);
    power_percent = label(status_strip, "--", MUTED); lv_obj_set_pos(power_percent, 404, 13);
    lv_obj_set_style_text_font(power_percent, &lv_font_montserrat_14, 0);
    lv_obj_set_width(power_percent, 62); lv_obj_set_style_text_align(power_percent, LV_TEXT_ALIGN_RIGHT, 0);
    update_power(&snapshot.power);
    /* App header: back control and title. */
    back_button = button(screen, LV_SYMBOL_LEFT, T_NONE, BTN_SECONDARY, go_home, NULL);
    lv_obj_set_pos(back_button, 16, 48); lv_obj_set_size(back_button, 44, 44);
    lv_obj_set_style_radius(back_button, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_all(back_button, 0, 0);
    lv_obj_set_ext_click_area(back_button, 10);
    app_title = label(screen, "", TEXT); lv_obj_set_pos(app_title, 74, 58); lv_obj_set_width(app_title, 390);
    label_ellipsis(app_title);
    lv_obj_add_style(app_title, &st_title, 0);
    viewport = lv_obj_create(screen); lv_obj_remove_style_all(viewport); lv_obj_set_pos(viewport, 0, CONTENT_TOP);
    lv_obj_set_size(viewport, 480, HOME_TOP - CONTENT_TOP);
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < APP_COUNT; ++i) {
        pages[i] = app_page(viewport);
        if (i) lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* Root siblings keep the keyboard and home gesture strip outside app scrolling. */
    keyboard = lv_keyboard_create(screen); lv_obj_set_size(keyboard, 480, KEYBOARD_HEIGHT);
    configure_keyboard(); style_keyboard();
    lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 0, HOME_TOP - KEYBOARD_HEIGHT);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    home_strip = lv_obj_create(screen); lv_obj_remove_style_all(home_strip); lv_obj_set_pos(home_strip, 0, HOME_TOP);
    lv_obj_set_size(home_strip, 480, 44);
    lv_obj_set_style_bg_color(home_strip, lv_color_hex(INK), 0); lv_obj_set_style_bg_opa(home_strip, LV_OPA_COVER, 0);
    lv_obj_remove_flag(home_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(home_strip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(home_strip, home_gesture, LV_EVENT_ALL, NULL);
    home_hint = caption(home_strip, T_SWIPE_HOME);
    lv_obj_set_style_text_align(home_hint, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_pos(home_hint, 0, 4);
    home_pill = lv_obj_create(home_strip); lv_obj_remove_style_all(home_pill);
    lv_obj_set_style_bg_opa(home_pill, LV_OPA_COVER, 0); lv_obj_set_style_radius(home_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(home_pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    home_feedback(home_pill, 0);
    build_voice_panel(screen);
    m3_audio_state_t initial_audio; m3_audio_state(&initial_audio);
    build_home(pages[APP_HOME]);
    build_wifi(pages[APP_WIFI]);
    build_files(pages[APP_FILES]);
    build_recorder(pages[APP_RECORDER], initial_audio.volume);
    build_monitor(pages[APP_MONITOR]);
    build_settings(pages[APP_SYSTEM]);
    build_radio(pages[APP_RADIO], initial_audio.volume);
    build_games(pages[APP_GAMES]);
    build_tts(pages[APP_TTS], initial_audio.volume);
    build_agent(pages[APP_AGENT]);
    build_tube(pages[APP_TUBE]);
    build_ytmusic(pages[APP_YTMUSIC], initial_audio.volume);
    build_music(pages[APP_MUSIC], initial_audio.volume);
    build_gallery(pages[APP_GALLERY], initial_audio.volume);
    build_calculator(pages[APP_CALCULATOR]);
    build_reader(pages[APP_READER]);
    build_weather(pages[APP_WEATHER]);
    radio_select(7);
    create_shade(screen);
    screen_shield = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(screen_shield);
    lv_obj_set_pos(screen_shield, 0, 0); lv_obj_set_size(screen_shield, M3_WIDTH, M3_HEIGHT);
    lv_obj_set_style_bg_color(screen_shield, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(screen_shield, LV_OPA_COVER, 0);
    lv_obj_add_flag(screen_shield, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(screen_shield, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    screen_timer = lv_timer_create(screen_wake_tick, 30, NULL);
    show_app(APP_HOME);
    lv_timer_create(update_metrics, 250, NULL);
    lv_timer_create(music_visual_update, 40, NULL);
    lv_timer_create(tube_frame_tick, 30, NULL);
    lv_timer_create(crossing_tick, 50, NULL);
    lv_timer_create(sd_apps_tick, 50, NULL);
    lv_timer_create(voice_meter_tick, 40, NULL);
    update_metrics(NULL);
    lv_timer_create(gallery_tick, 33, NULL);
    lvgl_port_unlock();
    return ESP_OK;
}
