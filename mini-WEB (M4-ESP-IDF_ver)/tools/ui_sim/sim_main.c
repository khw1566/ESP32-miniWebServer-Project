/* Host-only LVGL renderer for the M4 touch UI. It links the real m4_ui.c with
 * mocked services and writes RGB565 frames that tools/ui_sim/to_png.py turns
 * into PNG files. It is a layout preview, not a firmware or hardware test. */
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include "nvs.h"
#include "time_compat.h"

#include "../../main/m4_ui.c"

/* Reader fixtures: UTF-8 text in memory; no SD or NVS evidence. */
static m4_reader_settings_t sim_reader_settings = {1,22,1,0,8,24,0,0};
static m4_reader_state_t sim_reader;
static const char sim_reader_text[] =
    "제1장. 작은 화면 속의 책\n\n"
    "창밖으로 아침 햇살이 들어왔다. 책장을 넘길 때마다 새로운 이야기가 시작되었다. "
    "글자 크기와 줄 간격을 바꾸어도 읽던 곳에서 이야기를 이어 갈 수 있다.\n\n"
    "Chapter 1. A book in your hands\n\n"
    "The morning light touched the desk. Every page opened another window. "
    "Swipe left or up to continue, right or down to return. Tap the center to hide the controls.\n\n"
    "제2장. 이어지는 이야기\n\n"
    "천천히 읽는 시간은 작은 휴식이 된다. 화면의 색과 글자의 간격을 고르고 편안하게 읽어 보자. "
    "마지막 문장까지 글이 끊기지 않고 이어진다.\n";
esp_err_t m4_reader_start(void) { return ESP_OK; }
bool m4_reader_supported(const char *name) { const char *p = strrchr(name, '.'); return p && (!strcmp(p,".txt") || !strcmp(p,".md") || !strcmp(p,".log")); }
void m4_reader_settings(m4_reader_settings_t *out) { *out = sim_reader_settings; }
bool m4_reader_configure(const m4_reader_settings_t *p) { sim_reader_settings = *p; return true; }
void m4_reader_state(m4_reader_state_t *out) { *out = sim_reader; out->available = true; }
bool m4_reader_open(const char *path) {
    sim_reader.status = M4_READER_READY; sim_reader.bytes = sizeof(sim_reader_text)-1;
    ++sim_reader.generation; ++sim_reader.revision; snprintf(sim_reader.path,sizeof(sim_reader.path),"%s",path); return true;
}
bool m4_reader_resume(void) { return false; }
void m4_reader_close(void) { sim_reader.status = M4_READER_IDLE; }
size_t m4_reader_copy(unsigned generation, unsigned offset, char *out, size_t capacity) {
    if (generation != sim_reader.generation || offset >= sim_reader.bytes) { out[0]=0; return 0; }
    size_t size = sim_reader.bytes-offset; if (size >= capacity) size=capacity-1;
    while (size && ((unsigned char)sim_reader_text[offset+size]&0xc0)==0x80) --size;
    memcpy(out,sim_reader_text+offset,size); out[size]=0; return size;
}
unsigned m4_reader_align(unsigned generation, unsigned offset) {
    (void)generation; if (offset >= sim_reader.bytes) offset = sim_reader.bytes ? sim_reader.bytes-1 : 0;
    while (offset && ((unsigned char)sim_reader_text[offset]&0xc0)==0x80) --offset; return offset;
}
void m4_reader_progress(unsigned generation, unsigned offset) { (void)generation; sim_reader.resume=offset; }

/* SD app UI fixtures; they do not execute scripts or access an SD card. */
static bool sim_sd_app_running, sim_sd_app_empty, sim_sd_app_error;
void m4_sd_apps_poll(void) {}
bool m4_sd_apps_state(m4_sd_apps_state_t *out) {
    memset(out, 0, sizeof(*out)); out->mounted = true;
    out->count = sim_sd_app_empty ? 0 : 2; out->running = sim_sd_app_running;
    snprintf(out->names[0], sizeof(out->names[0]), "sand_tetris.lua"); out->score = 240;
    snprintf(out->names[1], sizeof(out->names[1]), "watermelon.lua");
    if (sim_sd_app_error) snprintf(out->error, sizeof(out->error), "Execution limit / stopped");
    return true;
}
bool m4_sd_apps_start(const char *name) { (void)name; sim_sd_app_running = true; return true; }
void m4_sd_apps_stop(void) { sim_sd_app_running = false; }
void m4_sd_apps_input(unsigned buttons) { (void)buttons; }
bool m4_sd_apps_frame(uint16_t *out) {
    if (!sim_sd_app_running) return false;
    for (unsigned y = 0; y < M4_SD_APP_HEIGHT; ++y)
        for (unsigned x = 0; x < M4_SD_APP_WIDTH; ++x)
            out[y * M4_SD_APP_WIDTH + x] = y > 150 - (x % 41) ? ((x / 28) % 2 ? 0x459f : 0xfbe4) : 0x0862;
    return true;
}

/* Photo widget mocks: a bounded RGB565 test card, no SD or JPEG hardware. */
static bool sim_photos_active, sim_photos_paused, sim_photos_empty, sim_photos_busy;
static unsigned sim_photo_index;
static uint8_t sim_photo_pixels[M4_PHOTO_BYTES];
void m4_photos_activate(bool enabled) { sim_photos_active = enabled; }
void m4_photos_step(int direction) { sim_photo_index = (sim_photo_index + (direction < 0 ? 2 : 1)) % 3; }
void m4_photos_pause(void) { sim_photos_paused = !sim_photos_paused; }
void m4_photos_refresh(void) { sim_photos_empty = false; sim_photos_busy = false; }
void m4_photos_poll(void) {}
void m4_photos_state(m4_photo_state_t *out) {
    memset(out, 0, sizeof(*out)); out->count = sim_photos_empty ? 0 : 3; out->index = sim_photo_index;
    out->paused = sim_photos_paused; out->status = sim_photos_empty ? M4_PHOTO_EMPTY : sim_photos_busy ? M4_PHOTO_BUSY : M4_PHOTO_READY;
    snprintf(out->name, sizeof(out->name), "春の写真（%u）.jpg", sim_photo_index + 1);
}
const uint8_t *m4_photos_frame(void) {
    if (!sim_photos_active || sim_photos_empty) return NULL;
    for (unsigned y = 0; y < M4_PHOTO_H; ++y) for (unsigned x = 0; x < M4_PHOTO_W; ++x) {
        uint16_t color = ((x / 48 + y / 48) & 1) ? 0x04df : 0xb815;
        unsigned offset = (y * M4_PHOTO_W + x) * 2;
        sim_photo_pixels[offset] = color; sim_photo_pixels[offset + 1] = color >> 8;
    }
    return sim_photo_pixels;
}

/* Music catalog and player mocks; no storage device is accessed. */
static unsigned sim_music_revision = 1, sim_music_page, sim_music_selected;
static m4_music_repeat_t sim_music_mode;
static bool sim_music_shuffle;
static int sim_music_eq[3];
static bool sim_music_sd = true, sim_music_active, sim_music_paused, sim_music_repeat;
void m4_music_state(m4_music_state_t *out) {
    memset(out, 0, sizeof(*out)); out->sd = sim_music_sd; out->count = 7; out->catalog_revision = 1;
    out->page = sim_music_page; out->revision = sim_music_revision; out->selected = sim_music_selected;
    out->active = sim_music_active; out->paused = sim_music_paused; out->repeat = sim_music_repeat; out->repeat_mode = sim_music_mode; out->shuffle = sim_music_shuffle;
    memcpy(out->eq_db, sim_music_eq, sizeof(sim_music_eq));
    for (unsigned b = 0; b < 3; ++b) out->bands[b] = sim_music_active && !sim_music_paused ? 30 + b * 25 : 0;
    out->progress = sim_music_active ? 37 : 0; out->seconds = sim_music_active ? 46 : 0;
    snprintf(out->title, sizeof(out->title), "%s", sim_music_active ? "春の音楽 - ひらがなとカタカナの長いファイル名（特別版）.mp3" : "");
    snprintf(out->message, sizeof(out->message), "%s", sim_music_active ? "Local music playing" : "Music catalog ready");
    for (unsigned i = sim_music_page * M4_MUSIC_ROWS; i < 7 && out->shown < M4_MUSIC_ROWS; ++i) {
        m4_music_item_t *item = &out->items[out->shown++]; item->index = i;
        static const char *titles[] = {"春の音楽 - ひらがなとカタカナの長いファイル名（特別版）.mp3", "カタカナ・テスト.wav", "韓國漢字 - 音樂.aac",
            "特別篇（第①章）.mp3", "Café £10 €5.wav", "𠮟る・夏の歌.mp3", "半角ｶﾀｶﾅ.wav"};
        snprintf(item->name, sizeof(item->name), "%s", titles[i]);
    }
}
void m4_music_refresh(bool sd) { sim_music_sd = sd; sim_music_page = 0; ++sim_music_revision; }
void m4_music_poll(void) {}
void m4_music_page(int direction) { sim_music_page = direction > 0 ? 1 : 0; ++sim_music_revision; }
bool m4_music_play(unsigned index, unsigned revision) { (void)revision; sim_music_selected = index; sim_music_active = true; ++sim_music_revision; return true; }
bool m4_music_next(int direction) { sim_music_selected = (sim_music_selected + (direction > 0 ? 1 : 6)) % 7; sim_music_active = true; ++sim_music_revision; return true; }
void m4_music_pause(void) { sim_music_paused = !sim_music_paused; ++sim_music_revision; }
void m4_music_repeat_mode(m4_music_repeat_t mode) { sim_music_mode = mode; sim_music_repeat = mode != M4_MUSIC_REPEAT_OFF; ++sim_music_revision; }
void m4_music_repeat(bool enabled) { m4_music_repeat_mode(enabled ? M4_MUSIC_REPEAT_ALL : M4_MUSIC_REPEAT_OFF); }
void m4_music_shuffle(bool enabled) { sim_music_shuffle = enabled; ++sim_music_revision; }
void m4_music_equalizer(unsigned band, int db) { if (band < 3) sim_music_eq[band] = db; ++sim_music_revision; }
void m4_music_stop(void) { sim_music_active = false; ++sim_music_revision; }

/* YouTube mocks. tube_sim_data.inc (optional, generated from a real relay listing) supplies items and images. */
#if __has_include("tube_sim_data.inc")
#include "tube_sim_data.inc"
#else
static const m4_tube_item_t sim_items[] = {{"aaaaaaaaaaa", "Sample video", "Channel", "UCaaaaaaaaaaaaaaaaaaaaaa", 201, 123456, false}};
static const unsigned sim_item_count = 1;
static uint8_t sim_thumbs[1][160 * 90 * 2], sim_frame[480 * 272 * 2];
#endif
static m4_tube_list_t sim_tube;
static m4_tube_player_t sim_player;
bool m4_tube_configured(void) { return true; }
static m4_tube_quality_t sim_quality;
static m4_tube_download_t sim_download;
static bool sim_server_offline;
void m4_tube_quality(m4_tube_quality_t quality) { sim_quality = quality; }
m4_tube_quality_t m4_tube_get_quality(void) { return sim_quality; }
void m4_tube_probe(void) {}
void m4_tube_server(m4_tube_server_t *out) {
    memset(out, 0, sizeof(*out)); out->ready = !sim_server_offline;
    snprintf(out->message, sizeof(out->message), "%s", sim_server_offline ? "PC server or tunnel is offline" : "PC server ready");
}
bool m4_tube_download(const char *id, const char *title) {
    (void)id; (void)title; memset(&sim_download, 0, sizeof(sim_download)); sim_download.busy = true;
    sim_download.bytes = 1048576; sim_download.total = 4194304; sim_download.percent = 25;
    snprintf(sim_download.message, sizeof(sim_download.message), "Saving video to SD"); return true;
}
void m4_tube_download_cancel(void) { sim_download.busy = false; snprintf(sim_download.message, sizeof(sim_download.message), "Download cancelled"); }
void m4_tube_download_state(m4_tube_download_t *out) { *out = sim_download; }
static bool sim_music_cache_loading;
static char sim_cached_ids[2][12];
void m4_tube_music_prepare(const char *current, const char *next) {
    snprintf(sim_cached_ids[0],sizeof(sim_cached_ids[0]),"%s",current?current:"");
    snprintf(sim_cached_ids[1],sizeof(sim_cached_ids[1]),"%s",next?next:"");
}
void m4_tube_music_autonext(bool enabled) { (void)enabled; }
void m4_tube_music_cache(const char *id,m4_tube_music_cache_t *out) {
    memset(out,0,sizeof(*out)); out->loading=sim_music_cache_loading && !strcmp(id,sim_cached_ids[1]);
    out->ready=!out->loading; out->total=2400000; out->bytes=out->loading?1600000:2400000; out->percent=out->loading?67:100;
    snprintf(out->message,sizeof(out->message),"%s",out->loading?"Loading music into PSRAM":"Music ready in PSRAM");
}
bool m4_tube_music_download(const char *id,const char *title) {
    if (!m4_tube_download(id,title)) return false; sim_download.music=true;
    snprintf(sim_download.message,sizeof(sim_download.message),"Saving music to SD"); return true;
}
esp_err_t m4_tube_start(void) { return ESP_OK; }
static bool sim_listing(bool channel, const char *text, unsigned page) {
    sim_tube.status = M4_TUBE_LIST_READY; sim_tube.channel = channel; sim_tube.page = page; sim_tube.more = true;
    sim_tube.count = sim_item_count > M4_TUBE_ITEMS ? M4_TUBE_ITEMS : sim_item_count;
    memcpy(sim_tube.items, sim_items, sizeof(m4_tube_item_t) * sim_tube.count);
    snprintf(sim_tube.query, sizeof(sim_tube.query), "%s", text);
    snprintf(sim_tube.title, sizeof(sim_tube.title), "%s", channel ? sim_items[0].channel : "");
    sim_tube.thumbs = (uint16_t)((1u << sim_tube.count) - 1); sim_tube.thumb_set ^= 1; ++sim_tube.revision;
    return true;
}
bool m4_tube_search(const char *query, unsigned page) { return sim_listing(false, query, page); }
bool m4_tube_channel(const char *id, unsigned page) { return sim_listing(true, id, page); }
void m4_tube_list(m4_tube_list_t *out) { *out = sim_tube; }
const uint8_t *m4_tube_thumb(const m4_tube_list_t *list, unsigned index) {
    return index < sim_item_count && (list->thumbs & (1u << index)) ? sim_thumbs[index] : NULL;
}
static m4_tube_list_t sim_music;
bool m4_tube_music_search(const char *query, unsigned page) {
    sim_music.status = M4_TUBE_LIST_READY; sim_music.music = true; sim_music.page = page; sim_music.more = true;
    sim_music.count = sim_item_count > M4_TUBE_ITEMS ? M4_TUBE_ITEMS : sim_item_count;
    memcpy(sim_music.items, sim_items, sizeof(m4_tube_item_t) * sim_music.count);
    snprintf(sim_music.query, sizeof(sim_music.query), "%s", query); ++sim_music.revision;
    sim_music.thumbs = (uint16_t)((1u << sim_music.count) - 1);
    sim_music.thumb_set = 2 + ((sim_music.thumb_set ^ 1) & 1);
    return true;
}
void m4_tube_music_list(m4_tube_list_t *out) { *out = sim_music; }
bool m4_tube_play_audio(const char *id, unsigned start) {
    unsigned revision = sim_player.revision + 1;
    memset(&sim_player, 0, sizeof(sim_player));
    sim_player.active = true; sim_player.audio_only = true; sim_player.position_ms = 63000 + start * 1000; sim_player.duration_s = 201;
    snprintf(sim_player.id, sizeof(sim_player.id), "%s", id); sim_player.revision = revision; return true;
}
bool m4_tube_play(const char *id, unsigned start) {
    sim_player.audio_only = false;
    sim_player.active = true; sim_player.video = true; sim_player.position_ms = 63000 + start * 1000; sim_player.duration_s = 201;
    snprintf(sim_player.id, sizeof(sim_player.id), "%s", id); ++sim_player.revision; return true;
}
void m4_tube_pause(bool paused) { sim_player.paused = paused; ++sim_player.revision; }
void m4_tube_player(m4_tube_player_t *out) { *out = sim_player; }
const uint8_t *m4_tube_frame(void) { return sim_player.active ? sim_frame : NULL; }

static lv_display_t *display;
static uint16_t frame[600 * 1024];
static int sim_lang = -1;
/* System sounds are simulated state only; this renderer does not open a codec. */
static m4_feedback_settings_t sim_feedback = {1,1,45,1,35,1,55,0};
void m4_feedback_get(m4_feedback_settings_t *out) { *out = sim_feedback; }
bool m4_feedback_update(const m4_feedback_settings_t *p) { sim_feedback = *p; return true; }
void m4_feedback_clear(void) {}
void m4_feedback_emit(unsigned cue, unsigned language) { (void)cue; (void)language; }
static m4_preferences_t sim_preferences = {1,100,30,1,0,40,40,0};
bool m4_preferences_restarting(void) { return false; }
void m4_preferences_get(m4_preferences_t *out) { *out = sim_preferences; }
bool m4_preferences_update(const m4_preferences_t *p) { sim_preferences = *p; return true; }
void m4_preferences_volume(unsigned value) { sim_preferences.volume=value; if(value)sim_preferences.last_volume=value; }
unsigned m4_preferences_frame_ms(unsigned fps) { return (1000+fps-1)/fps; }
m4_mode_result_t m4_preferences_request_mode(bool focus) { sim_preferences.web_focus=focus; return M4_MODE_OK; }
esp_err_t m4_board_set_brightness(unsigned value) { (void)value; return ESP_OK; }
lv_display_t *m4_board_display(void) { return display; }
bool m4_board_touch_ready(void) { return true; }
bool lvgl_port_lock(uint32_t timeout_ms) { (void)timeout_ms; return true; }
void lvgl_port_unlock(void) {}
size_t heap_caps_get_free_size(unsigned caps) { return caps & MALLOC_CAP_SPIRAM ? 27u << 20 : 182u << 10; }
size_t heap_caps_get_total_size(unsigned caps) { (void)caps; return 420u << 10; }
size_t heap_caps_get_minimum_free_size(unsigned caps) { (void)caps; return 151u << 10; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 96u << 10; }
size_t esp_psram_get_size(void) { return 32u << 20; }
int64_t esp_timer_get_time(void) { return (int64_t)GetTickCount64() * 1000; }
void esp_chip_info(esp_chip_info_t *info) { info->cores = 2; info->revision = 302; }
const char *esp_get_idf_version(void) { return "v5.5.5"; }
uint32_t esp_random(void) { return (uint32_t)rand() * 7919u + 13u; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) { (void)name; (void)mode; *out = 1; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out) {
    (void)h; (void)key; if (sim_lang < 0) return ESP_FAIL; *out = (uint8_t)sim_lang; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) { (void)h; (void)key; sim_lang = value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
bool network_settings_valid(const network_settings_t *settings) { return settings->ssid[0] != 0; }

/* Service mocks. The scenario selects online/offline and busy states. */
static bool sim_online = true, sim_playing = true, sim_agent_busy, sim_voice_recording = true, sim_voice_busy;
static const char *sim_audio_message;
static unsigned sim_tts_provider = 3, sim_stt_provider;
static m4_recorder_state_t sim_recorder;
static bool sim_sd_ready = true;
static bool sim_ipv6_ready = true;
static m4_power_state_t sim_power;
void m4_runtime_state(m4_runtime_state_t *out) {
    memset(out, 0, sizeof(*out));
    out->initialized = out->web = out->wifi_ready = true;
    out->cpu_valid = true; out->cpu_revision = (unsigned)(esp_timer_get_time() / 1000000) + 1;
    out->cpu_percent[0] = 28 + out->cpu_revision % 30; out->cpu_percent[1] = 42 + out->cpu_revision % 40;
    out->sd = sim_sd_ready;
    out->power = sim_power;
    out->cloud = sim_online;
    out->mode = sim_online ? NET_MODE_STA_CONNECTED : NET_MODE_AP_CAPTIVE;
    out->rssi = -58; out->requests = 42;
    snprintf(out->ip, sizeof(out->ip), "%s", sim_online ? "192.168.0.42" : "192.168.4.1");
    snprintf(out->url, sizeof(out->url), "%s", sim_online ? "http://192.168.0.42/" : "http://192.168.4.1/setup");
    snprintf(out->cloud_url, sizeof(out->cloud_url), "%s", sim_online ? "https://m4-a1b2c3d4e5f6.audreaming.com/" : "");
    snprintf(out->domain_url, sizeof(out->domain_url), "https://m4-a1b2c3d4e5f6.audreaming.com/");
    snprintf(out->ipv6_url, sizeof(out->ipv6_url), "%s", sim_online && sim_ipv6_ready ? "http://[2001:db8:42::1234]/" : "");
    snprintf(out->ipv6_status, sizeof(out->ipv6_status), "%s", sim_ipv6_ready ? "global_ready" : "no_global_prefix");
    snprintf(out->cloud_status, sizeof(out->cloud_status), "%s", sim_online ? "online" : "waiting_wifi");
    out->mdns_ready = true;
    snprintf(out->mdns_url, sizeof(out->mdns_url), "http://m4-v1-2-a1b2c3d4e5f6.local/");
    snprintf(out->message, sizeof(out->message), "Services ready");
    snprintf(out->wifi_message, sizeof(out->wifi_message), "Tap a network, enter its password, then connect.");
    out->scan_revision = 1; out->scan_count = 4; out->settings_revision = 1; out->wifi_revision = 1;
    static const char *names[] = {"KT_GiGA_5G_Home", "iptime-guest", "Cafe Free WiFi", "U+Net3F21"};
    static const int rssi[] = {-48, -63, -71, -82};
    for (int i = 0; i < 4; ++i) {
        snprintf(out->networks[i].ssid, sizeof(out->networks[i].ssid), "%s", names[i]);
        out->networks[i].rssi = (int8_t)rssi[i]; out->networks[i].authmode = i == 2 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    }
    snprintf(out->settings.ssid, sizeof(out->settings.ssid), "KT_GiGA_5G_Home");
    out->wifi_password_saved = true;
    snprintf(out->settings.netmask, sizeof(out->settings.netmask), "255.255.255.0");
    out->browse_sd = true; out->file_revision = 1;
    snprintf(out->folder, sizeof(out->folder), "/media");
    out->files.total = 31914704896ull; out->files.used = 1288490188ull; out->files.count = 12; out->files.shown = 5;
    static const char *files[] = {"音楽アルバム", "春の歌（特別版）.wav", "韓國漢字 - 音樂.wav", "ひらがな・カタカナ.wav", "Café £10 €5.txt"};
    for (int i = 0; i < 5; ++i) {
        snprintf(out->files.entries[i].name, sizeof(out->files.entries[i].name), "%s", files[i]);
        out->files.entries[i].directory = i == 0; out->files.entries[i].bytes = 524288ull * (i + 1) + 1234;
    }
    out->flash.total = 8192000; out->flash.used = 1843200; out->flash.reserved = 0;
}
bool m4_runtime_browse(bool sd, const char *folder, unsigned offset) { (void)sd; (void)folder; (void)offset; return false; }
bool m4_runtime_wifi(m4_wifi_action_t action, const network_settings_t *settings) { (void)action; (void)settings; return true; }
static unsigned sim_volume = 40;
void m4_audio_state(m4_audio_state_t *out) {
    memset(out, 0, sizeof(*out));
    out->ready = true; out->playing = sim_playing; out->volume = sim_volume; out->progress = sim_playing ? 38 : 0;
    snprintf(out->message, sizeof(out->message), "%s", sim_audio_message ? sim_audio_message :
        sim_playing ? "Radio / MP3 / net 412 KiB / PCM 740 ms / gaps 0" : "Ready / PCM16 WAV");
}
bool m4_audio_play(const char *name) { (void)name; return true; }
bool m4_audio_record(void) {
    sim_recorder.busy = sim_recorder.recording = true; sim_recorder.level = 52; return true;
}
bool m4_audio_recording_play(const char *name) { (void)name; return true; }
void m4_recorder_state(m4_recorder_state_t *out) { *out = sim_recorder; }
void m4_recorder_finish(void) {
    sim_recorder.busy = sim_recorder.recording = false; sim_recorder.saved = true;
    sim_recorder.level = 0; snprintf(sim_recorder.filename, sizeof(sim_recorder.filename), "rec_20261014_120000_12345678.wav");
    snprintf(sim_recorder.message, sizeof(sim_recorder.message), "Recording saved");
}
bool m4_audio_radio(const char *url, bool aac) { (void)url; (void)aac; return true; }
bool m4_audio_radio_preset(const char *station, unsigned profile) { (void)station; return profile < 3; }
unsigned m4_audio_tts_selected(void) { return sim_tts_provider; }
void m4_audio_tts_provider(unsigned provider) { sim_tts_provider = provider; }
bool m4_audio_speak(const char *text, unsigned speed) { (void)text; (void)speed; return true; }
bool m4_audio_voice(void) { return true; }
void m4_audio_stop(void) {}
void m4_audio_volume(unsigned percent) { sim_volume = percent; m4_preferences_volume(percent); }
void m4_voice_state(m4_voice_state_t *out) {
    memset(out, 0, sizeof(*out)); out->key_ready = true; out->revision = 3; out->recording = sim_voice_recording; out->busy = sim_voice_busy; out->provider = sim_stt_provider;
    snprintf(out->text, sizeof(out->text), "%s", "내일 날씨 알려줘");
    snprintf(out->message, sizeof(out->message), "Recording 3 / 8 seconds / level 2210");
}
static unsigned sim_mic_level;
unsigned m4_voice_level(void) { return sim_mic_level; }
int m4_voice_dbfs(void) { return sim_mic_level ? (int)sim_mic_level / 2 - 60 : -90; }
bool m4_voice_set_provider(unsigned provider) { sim_stt_provider = provider; return true; }
bool m4_voice_set_key(const char *key) { (void)key; return true; }
void m4_voice_finish(void) {}
void m4_agent_state(m4_agent_state_t *out) {
    memset(out, 0, sizeof(*out)); out->ready = out->key_ready = true; out->revision = sim_agent_busy ? 5 : 2; out->model = 1;
    out->busy = sim_agent_busy;
    snprintf(out->message, sizeof(out->message), "%s", sim_agent_busy ? "Gemini thinking..." : "Ready");
    snprintf(out->answer, sizeof(out->answer), "%s",
        "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94! \xED\x98\x84\xEC\x9E\xAC Wi-Fi\xEB\x8A\x94 \xEC\x97\xB0\xEA\xB2\xB0\xEB\x90\x98\xEC\x96\xB4 \xEC\x9E\x88\xEA\xB3\xA0 "
        "\xEC\x9D\x8C\xEB\x9F\x89\xEC\x9D\x80 40%\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4. \xEB\x9D\xBC\xEB\x94\x94\xEC\x98\xA4\xEB\xA5\xBC \xEC\xBC\x9C \xEB\x93\x9C\xEB\xA6\xB4\xEA\xB9\x8C\xEC\x9A\x94?");
    snprintf(out->tools, sizeof(out->tools), "get_status -> ok");
}
bool m4_agent_submit(const char *prompt, unsigned model) { (void)prompt; (void)model; return true; }
void m4_agent_cancel(bool clear) { (void)clear; }
int m4_agent_take_open_app(void) { return -1; }

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *source = (const uint16_t *)pixels;
    int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y)
        memcpy(&frame[y * 600 + area->x1], &source[(y - area->y1) * width], (size_t)width * 2);
    lv_display_flush_ready(disp);
}
static void settle(unsigned ms) {
    for (unsigned t = 0; t < ms; t += 10) { lv_tick_inc(10); lv_timer_handler(); }
}
static void capture(const char *dir, const char *name) {
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    char path[512]; snprintf(path, sizeof(path), "%s/%s.rgb565", dir, name);
    FILE *file = fopen(path, "wb"); if (!file) { perror(path); exit(1); }
    fwrite(frame, sizeof(frame), 1, file); fclose(file);
    printf("captured %s\n", name);
}
#include "scenarios.inc"

/* Prints the faulting function on a crash (build has /Zi and links dbghelp). */
static LONG WINAPI crash_report(EXCEPTION_POINTERS *info) {
    HANDLE process = GetCurrentProcess(); SymInitialize(process, NULL, TRUE);
    char storage[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 255;
    fprintf(stderr, "crash 0x%08lx\n", info->ExceptionRecord->ExceptionCode);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {0};
    frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 16 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
            NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL); ++depth) {
        IMAGEHLP_LINE64 line = {.SizeOfStruct = sizeof(line)}; DWORD offset = 0;
        const char *name = SymFromAddr(process, frame.AddrPC.Offset, NULL, symbol) ? symbol->Name : "?";
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &offset, &line)) fprintf(stderr, "  %s (%s:%lu)\n", name, line.FileName, line.LineNumber);
        else fprintf(stderr, "  %s\n", name);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
int main(int argc, char **argv) {
    SetUnhandledExceptionFilter(crash_report);
    const char *dir = argc > 1 ? argv[1] : ".";
    if (argc > 2) sim_lang = atoi(argv[2]);
    srand(7);
    lv_init();
    display = lv_display_create(600, 1024);
    static uint16_t buffer[600 * 1024];
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    if (m4_ui_start() != ESP_OK) { puts("UI start failed"); return 1; }
    settle(600);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("LVGL heap used after UI start: %u bytes\n", (unsigned)(memory.total_size - memory.free_size));
    printf("text bindings %u/%u, segments %u/%u\n", binding_count, (unsigned)(sizeof(bindings) / sizeof(bindings[0])),
        segment_count, (unsigned)(sizeof(segments) / sizeof(segments[0])));
    run_scenarios(dir);
    return 0;
}

/* Gallery fixtures do not decode media or use SD hardware. */
static m4_gallery_library_t sim_gallery_library;
static m4_gallery_state_t sim_gallery_state;
static uint16_t sim_gallery_copied;
esp_err_t m4_gallery_start(void) { return ESP_OK; }
bool m4_gallery_supported(const char *name) { const char *ext = strrchr(name, '.'); return ext && (!strcmp(ext, ".png") || !strcmp(ext, ".jpg") || !strcmp(ext, ".avi") || !strcmp(ext, ".mp4")); }
bool m4_gallery_media(const char *name) { return m4_gallery_supported(name); }
bool m4_gallery_video(const char *name) { const char *ext = strrchr(name, '.'); return ext && (!strcmp(ext, ".mp4") || !strcmp(ext, ".avi")); }
bool m4_gallery_path_valid(const char *path) { return path && path[0] == '/' && !strstr(path, ".."); }
bool m4_gallery_open(const char *path) { sim_gallery_state.status=M4_GALLERY_READY; sim_gallery_state.video=m4_gallery_video(path); sim_gallery_state.mp4=sim_gallery_state.video; sim_gallery_state.frames=1; ++sim_gallery_state.revision; return true; }
void m4_gallery_close(void) { sim_gallery_state.status=M4_GALLERY_IDLE; }
void m4_gallery_audio_cancel(void) {}
void m4_gallery_pause(void) { sim_gallery_state.paused=!sim_gallery_state.paused; ++sim_gallery_state.revision; }
bool m4_gallery_active(void) { return false; }
void m4_gallery_state(m4_gallery_state_t *out) { *out=sim_gallery_state; }
const uint8_t *m4_gallery_frame(void) {
    for(unsigned y=0;y<M4_PHOTO_H;++y)for(unsigned x=0;x<M4_PHOTO_W;++x){unsigned p=(y*M4_PHOTO_W+x)*2;uint16_t color=((x/48+y/48)&1)?0x04df:0xb815;sim_photo_pixels[p]=color;sim_photo_pixels[p+1]=color>>8;}
    return sim_photo_pixels;
}
bool m4_gallery_library_request(unsigned page) {
    ++sim_gallery_library.generation; ++sim_gallery_library.revision; sim_gallery_library.status=M4_GALLERY_READY;
    sim_gallery_library.page=page; sim_gallery_library.count=22; sim_gallery_library.shown=page==2?4:9; sim_gallery_copied=0;
    for(unsigned i=0;i<sim_gallery_library.shown;++i){sim_gallery_library.items[i].video=i%3==1;snprintf(sim_gallery_library.items[i].path,256,"/albums/写真_%u.%s",page*9+i,sim_gallery_library.items[i].video?"mp4":"jpg");}
    return true;
}
void m4_gallery_library_state(m4_gallery_library_t *out) { *out=sim_gallery_library; }
bool m4_gallery_thumbnail_copy(unsigned job,unsigned index,uint8_t *out,size_t capacity) {
    if(job!=sim_gallery_library.generation || index>=sim_gallery_library.shown || capacity<M4_GALLERY_THUMB_BYTES || (sim_gallery_copied&(1u<<index)))return false;
    for(unsigned y=0;y<120;++y)for(unsigned x=0;x<120;++x){unsigned p=(y*120+x)*2;uint16_t color=(index%2)?((x/24+y/24)&1?0xfbc0:0x284f):((x/20+y/20)&1?0x04df:0xb815);out[p]=color;out[p+1]=color>>8;}
    sim_gallery_copied|=1u<<index; return true;
}
bool m4_runtime_file_action(bool sd, const char *a, const char *b, bool remove) { (void)sd; (void)a; (void)b; (void)remove; return true; }
