#include "m3_memory.h"
#include "m3_wifi_performance.h"
#include "m3_tube.h"
#include "m3_relay.h"
#include "network_manager.h"
#include "https_memory.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_vfs_fat.h"
#include "sd_media.h"
#include "m3_preferences.h"
#include "m3_utf8.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include <stdatomic.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static const char *TAG = "M3_TUBE";
enum {
    THUMB_BYTES = M3_TUBE_THUMB_W * M3_TUBE_THUMB_H * 2, FRAME_BYTES = M3_TUBE_VIDEO_W * M3_TUBE_VIDEO_H * 2,
    JSON_LIMIT = 32 * 1024, FRAME_SLOTS = 5, RING_BYTES = 4 * 1024 * 1024, READ_BYTES = 8192,
    /* Start soon, but refill more after a stall so playback does not stutter on and off.
     * Video is about 2 Mbps (~250 KB/s); music is 32 kHz mono PCM (64 KB/s). */
    VIDEO_PREBUFFER_BYTES = 384 * 1024, VIDEO_REBUFFER_BYTES = 1024 * 1024,
    LOW_PREBUFFER_BYTES = 128 * 1024, LOW_REBUFFER_BYTES = 384 * 1024,
    STANDARD_PREBUFFER_BYTES = 256 * 1024, STANDARD_REBUFFER_BYTES = 768 * 1024,
    MUSIC_PREBUFFER_BYTES = 96 * 1024, MUSIC_REBUFFER_BYTES = 256 * 1024,
    JPEG_LIMIT = 192 * 1024, AUDIO_LIMIT = 8192, INFO_LIMIT = 2048,
    /* 6 DMA descriptors x 240 frames: queued output, converted to ms at the stream rate. */
    OUTPUT_FRAMES = 6 * 240,
};
/* M3TV packet header: u8 type, u8 flags, u16 reserved, u32 pts_ms, u32 length (little endian). */
enum { PACKET_INFO = 1, PACKET_VIDEO, PACKET_AUDIO, PACKET_END, PACKET_WAIT };

bool m3_tube_configured(void) {
    return m3_relay_count() != 0;
}
static void url_encode(const char *in, char *out, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *in && n + 4 < size; ++in) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[n++] = (char)c;
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}
static bool video_id_valid(const char *id) {
    if (!id || strlen(id) != 11) return false;
    for (const char *p = id; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return false;
    return true;
}
static void copy_text(char *out, size_t size, const cJSON *value) {
    snprintf(out, size, "%s", cJSON_IsString(value) ? value->valuestring : "");
    /* snprintf may cut the last UTF-8 sequence; drop it only when it is incomplete. */
    size_t length = strlen(out), lead = length;
    while (lead && ((unsigned char)out[lead - 1] & 0xc0) == 0x80) --lead;
    if (lead) {
        unsigned char first = (unsigned char)out[lead - 1];
        size_t need = first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : first >= 0xc0 ? 2 : 1;
        if (length - (lead - 1) < need) out[lead - 1] = 0;
    }
}
/* Asks the persistent cloud relay to yield its TLS session. The relay only notices
 * between requests and its long poll lasts up to 20 s (right after boot it is also
 * handshaking), so waiting for it made YouTube report "HTTPS busy". TLS buffers are
 * in PSRAM (MBEDTLS_EXTERNAL_MEM_ALLOC), so a concurrent session is allowed when
 * internal RAM still has headroom. *owned tells the caller whether to end the lease. */
static bool lease_tls(uint32_t timeout_ms, bool *owned) {
    *owned = https_memory_exclusive_begin(timeout_ms);
    if (*owned) return true;
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 48 * 1024 &&
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 16 * 1024;
}

/* ---------- Listings and thumbnails ---------- */
typedef struct { bool channel, music; unsigned page; char text[M3_TUBE_QUERY_BYTES]; } list_request_t;
typedef struct { uint8_t *data; size_t capacity, length; bool overflow; } sink_t;
static QueueHandle_t list_requests;
static SemaphoreHandle_t list_lock;
static M3_PSRAM_BSS m3_tube_list_t list, music_list;
static uint8_t *thumb_sets[4];
static uint8_t *json_buffer;
static atomic_int selected_quality = ATOMIC_VAR_INIT(M3_TUBE_LOW);
static const char *quality_name(unsigned quality) {
    return quality == M3_TUBE_HIGH ? "high" : quality == M3_TUBE_STANDARD ? "standard" : "low";
}
void m3_tube_quality(m3_tube_quality_t quality) { if ((unsigned)quality <= M3_TUBE_HIGH) atomic_store(&selected_quality, quality); }
m3_tube_quality_t m3_tube_get_quality(void) { return (m3_tube_quality_t)atomic_load(&selected_quality); }
static portMUX_TYPE service_lock = portMUX_INITIALIZER_UNLOCKED;
static m3_tube_server_t server_state;
static int64_t server_checked, server_requested;
static atomic_bool probe_requested;
void m3_tube_probe(void) {
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&service_lock);
    bool due = !server_requested || now - server_requested >= 15000000;
    if (due) server_requested = now;
    portEXIT_CRITICAL(&service_lock);
    if (due) atomic_store(&probe_requested, true);
}
void m3_tube_server(m3_tube_server_t *out) {
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&service_lock);
    *out = server_state;
    out->age_s = server_checked ? (uint32_t)((now - server_checked) / 1000000) : UINT32_MAX;
    portEXIT_CRITICAL(&service_lock);
}

static esp_err_t sink_event(esp_http_client_event_t *event) {
    sink_t *sink = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && sink && sink->data) {
        if (sink->length + event->data_len > sink->capacity) sink->overflow = true;
        else { memcpy(sink->data + sink->length, event->data, event->data_len); sink->length += event->data_len; }
    }
    return ESP_OK;
}
static int http_get(esp_http_client_handle_t client, sink_t *sink, uint8_t *data, size_t capacity, const char *url) {
    sink->data = data; sink->capacity = capacity; sink->length = 0; sink->overflow = false;
    esp_http_client_set_url(client, url);
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s, status=%d", esp_err_to_name(result), status);
        /* IDF can return ESP_ERR_NOT_SUPPORTED for a token-based 401 without
         * WWW-Authenticate. Preserve that response for the token error UI. */
        return status == 401 ? 401 : -1;
    }
    return sink->overflow ? -2 : esp_http_client_get_status_code(client);
}
static void server_probe_execute(void) {
    bool owned = false, ready = false;
    int status = 0;
    const char *message = "Checking PC server";
    portENTER_CRITICAL(&service_lock); server_state.checking = true; portEXIT_CRITICAL(&service_lock);
    if (!m3_tube_configured()) { message = "YouTube relay is not configured"; goto done; }
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { message = "Connect Wi-Fi first"; goto done; }
    if (!lease_tls(100, &owned)) { message = "Server check waiting for HTTPS"; goto done; }
    m3_relay_profile_t relay;
    if (!m3_relay_select(&relay)) { message = "Relay connection failed"; goto done; }
    char url[M3_RELAY_URL_SIZE + 16]; snprintf(url, sizeof(url), "%s/api/status", relay.url);
    uint8_t data[512]; sink_t sink = {0};
    esp_http_client_config_t config = {.url = url, .timeout_ms = 5000,
        .buffer_size = 1024, .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true, .event_handler = sink_event, .user_data = &sink};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { message = "Relay connection failed"; goto done; }
    esp_http_client_set_header(client, "x-m3-token", relay.token);
    status = http_get(client, &sink, data, sizeof(data) - 1, url);
    esp_http_client_cleanup(client);
    message = status == 401 ? "Relay token rejected; check SD relay settings" :
        status == 404 ? "Update PC relay for quality and downloads" :
        status == 530 || status == 502 || status == 503 ? "PC server or tunnel is offline" : "Relay connection failed";
    if (status == 200) {
        data[sink.length] = 0;
        cJSON *root = cJSON_Parse((char *)data);
        const cJSON *protocol = root ? cJSON_GetObjectItem(root, "protocol") : NULL;
        const cJSON *service = root ? cJSON_GetObjectItem(root, "service") : NULL;
        bool compatible = cJSON_IsString(service) && !strcmp(service->valuestring, "m3-tube-relay") &&
            cJSON_IsNumber(protocol) && protocol->valueint >= 2;
        ready = compatible && cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
        message = ready ? "PC server ready" : compatible ? "PC yt-dlp or ffmpeg unavailable" : "Update PC relay for quality and downloads";
        cJSON_Delete(root);
    }
done:
    if (owned) https_memory_exclusive_end();
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&service_lock);
    server_state.checking = false; server_state.ready = ready; server_state.http_status = status; server_checked = now;
    snprintf(server_state.message, sizeof(server_state.message), "%s", message);
    portEXIT_CRITICAL(&service_lock);
}
#include "m3_tube_download.inc"
static void list_status(m3_tube_list_t *target, m3_tube_list_status_t status, const char *message) {
    xSemaphoreTake(list_lock, portMAX_DELAY);
    target->status = status; snprintf(target->message, sizeof(target->message), "%s", message); ++target->revision;
    xSemaphoreGive(list_lock);
}
static bool parse_listing(const char *text, m3_tube_item_t *items, unsigned *count, bool *more, char *title, size_t title_size) {
    cJSON *root = cJSON_Parse(text);
    const cJSON *array = root ? cJSON_GetObjectItem(root, "items") : NULL;
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); return false; }
    *count = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, array) {
        if (*count >= M3_TUBE_ITEMS) break;
        m3_tube_item_t *item = &items[*count];
        copy_text(item->id, sizeof(item->id), cJSON_GetObjectItem(entry, "id"));
        if (!video_id_valid(item->id)) continue;
        copy_text(item->title, sizeof(item->title), cJSON_GetObjectItem(entry, "title"));
        copy_text(item->channel, sizeof(item->channel), cJSON_GetObjectItem(entry, "channel"));
        copy_text(item->channel_id, sizeof(item->channel_id), cJSON_GetObjectItem(entry, "channel_id"));
        const cJSON *duration = cJSON_GetObjectItem(entry, "duration"), *views = cJSON_GetObjectItem(entry, "views");
        item->duration = cJSON_IsNumber(duration) && duration->valuedouble > 0 ? (uint32_t)duration->valuedouble : 0;
        item->views = cJSON_IsNumber(views) && views->valuedouble > 0 ? (uint32_t)(views->valuedouble > 4e9 ? 4e9 : views->valuedouble) : 0;
        item->live = cJSON_IsTrue(cJSON_GetObjectItem(entry, "live"));
        ++*count;
    }
    *more = cJSON_IsTrue(cJSON_GetObjectItem(root, "more"));
    copy_text(title, title_size, cJSON_GetObjectItem(root, "title"));
    cJSON_Delete(root);
    return true;
}
static void list_execute(const list_request_t *request) {
    static M3_PSRAM_BSS m3_tube_item_t items[M3_TUBE_ITEMS];
    static sink_t sink;
    static M3_PSRAM_BSS char encoded[3 * M3_TUBE_QUERY_BYTES + 1], url[1600];
    char title[64] = "";
    unsigned count = 0; bool more = false, owned = false;
    m3_tube_list_t *target = request->music ? &music_list : &list;
    if (!m3_tube_configured()) { list_status(target, M3_TUBE_LIST_ERROR, "YouTube relay is not configured"); return; }
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { list_status(target, M3_TUBE_LIST_ERROR, "Connect Wi-Fi first"); return; }
    if (!lease_tls(1500, &owned)) { list_status(target, M3_TUBE_LIST_ERROR, "HTTPS busy; stop radio or video and retry"); return; }
    m3_relay_profile_t relay;
    if (!m3_relay_select(&relay)) {
        if (owned) https_memory_exclusive_end();
        list_status(target, M3_TUBE_LIST_ERROR, "Relay connection failed"); return;
    }
    snprintf(url, sizeof(url), "%s/healthz", relay.url);
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 30000, .buffer_size = 4096, .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = sink_event, .user_data = &sink,
        .user_agent = "Mini-WebServer-M3/1.2", .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { list_status(target, M3_TUBE_LIST_ERROR, "Relay connection failed"); goto done; }
    esp_http_client_set_header(client, "x-m3-token", relay.token);
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    url_encode(request->text, encoded, sizeof(encoded));
    int url_size = snprintf(url, sizeof(url), "%s/api/%s?%s=%s&page=%u%s", relay.url, request->channel ? "channel" : "search",
        request->channel ? "id" : "q", encoded, request->page, request->music ? "&music=1" : "");
    if (url_size < 0 || (size_t)url_size >= sizeof(url)) { list_status(target, M3_TUBE_LIST_ERROR, "Relay search failed; try again"); goto done; }
    int status = http_get(client, &sink, json_buffer, JSON_LIMIT - 1, url);
    if (status != 200) {
        ESP_LOGW(TAG, "listing HTTP %d", status);
        list_status(target, M3_TUBE_LIST_ERROR, status == 401 ? "Relay token rejected; check SD relay settings" :
            status < 0 ? "Relay connection failed" : "Relay search failed; try again");
        goto done;
    }
    json_buffer[sink.length] = 0;
    if (!parse_listing((const char *)json_buffer, items, &count, &more, title, sizeof(title))) {
        list_status(target, M3_TUBE_LIST_ERROR, "Relay returned an invalid listing"); goto done;
    }
    xSemaphoreTake(list_lock, portMAX_DELAY);
    memcpy(target->items, items, sizeof(items));
    target->count = count; target->more = more; target->page = request->page; target->channel = request->channel;
    target->music = request->music;
    snprintf(target->query, sizeof(target->query), "%s", request->text);
    snprintf(target->title, sizeof(target->title), "%s", title);
    snprintf(target->message, sizeof(target->message), "%s", count ? "Ready" : "No results");
    target->status = M3_TUBE_LIST_READY; target->thumbs = 0; ++target->revision;
    /* Independent double buffers keep a music search from replacing a video
     * thumbnail still displayed by the other app. */
    target->thumb_set = (request->music ? 2 : 0) + ((target->thumb_set ^ 1) & 1);
    uint8_t *set = thumb_sets[target->thumb_set];
    xSemaphoreGive(list_lock);
    for (unsigned i = 0; i < count; ++i) {
        if (uxQueueMessagesWaiting(list_requests)) break; /* A newer request replaces this listing. */
        int url_length = snprintf(url, sizeof(url), "%s/api/thumb?id=%.11s&w=%d&h=%d",
            relay.url, items[i].id, M3_TUBE_THUMB_W, M3_TUBE_THUMB_H);
        if (url_length < 0 || (size_t)url_length >= sizeof(url)) continue;
        if (http_get(client, &sink, set + i * THUMB_BYTES, THUMB_BYTES, url) != 200 || sink.length != THUMB_BYTES) continue;
        xSemaphoreTake(list_lock, portMAX_DELAY); target->thumbs |= 1u << i; xSemaphoreGive(list_lock);
    }
done:
    if (client) esp_http_client_cleanup(client);
    if (owned) https_memory_exclusive_end();
}
static void list_worker(void *unused) {
    (void)unused;
    list_request_t request;
    for (;;) {
        if (xQueueReceive(list_requests, &request, pdMS_TO_TICKS(500)) == pdTRUE) list_execute(&request);
        if (atomic_exchange(&probe_requested, false)) server_probe_execute();
    }
}
static bool list_enqueue(bool channel, bool music, const char *text, unsigned page) {
    if (!list_requests || !text || !text[0] || strlen(text) >= M3_TUBE_QUERY_BYTES || !m3_utf8_valid(text) || page < 1 || page > 20) return false;
    list_request_t request = {.channel = channel, .music = music, .page = page};
    snprintf(request.text, sizeof(request.text), "%s", text);
    m3_tube_list_t *target = music ? &music_list : &list;
    xSemaphoreTake(list_lock, portMAX_DELAY);
    target->status = M3_TUBE_LIST_LOADING; snprintf(target->message, sizeof(target->message), "Loading..."); ++target->revision;
    xSemaphoreGive(list_lock);
    return xQueueOverwrite(list_requests, &request) == pdTRUE;
}
bool m3_tube_search(const char *query, unsigned page) { return list_enqueue(false, false, query, page); }
bool m3_tube_music_search(const char *query, unsigned page) { return list_enqueue(false, true, query, page); }
bool m3_tube_channel(const char *channel_id, unsigned page) {
    return channel_id && strlen(channel_id) == 24 && !strncmp(channel_id, "UC", 2) && list_enqueue(true, false, channel_id, page);
}
static void list_copy(m3_tube_list_t *out, const m3_tube_list_t *source) {
    if (!list_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(list_lock, portMAX_DELAY); *out = *source; xSemaphoreGive(list_lock);
}
void m3_tube_list(m3_tube_list_t *out) { list_copy(out, &list); }
void m3_tube_music_list(m3_tube_list_t *out) { list_copy(out, &music_list); }
const uint8_t *m3_tube_thumb(const m3_tube_list_t *copy, unsigned index) {
    if (!copy || index >= M3_TUBE_ITEMS || copy->thumb_set >= 4 || !(copy->thumbs & (1u << index)) || !thumb_sets[copy->thumb_set]) return NULL;
    return thumb_sets[copy->thumb_set] + index * THUMB_BYTES;
}

/* ---------- Video frames: decoded by the audio worker, shown by the UI ---------- */
typedef enum { SLOT_FREE, SLOT_DECODING, SLOT_READY, SLOT_SHOWN } slot_state_t;
typedef struct { uint8_t *data; size_t size; uint32_t pts; slot_state_t state; } frame_slot_t;
static frame_slot_t slots[FRAME_SLOTS];
static portMUX_TYPE frame_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_int clock_ms = ATOMIC_VAR_INIT(-1); /* stream time at the speaker; -1 when idle */
static jpeg_decoder_handle_t jpeg;
static uint8_t *jpeg_input;
static size_t jpeg_input_size;

static bool frames_ready(void) {
    if (jpeg && jpeg_input) return true;
    jpeg_decode_engine_cfg_t engine = {.intr_priority = 0, .timeout_ms = 200};
    if (!jpeg && jpeg_new_decoder_engine(&engine, &jpeg) != ESP_OK) { jpeg = NULL; return false; }
    jpeg_decode_memory_alloc_cfg_t input = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t output = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    for (unsigned i = 0; i < FRAME_SLOTS; ++i) {
        if (slots[i].data) continue;
        slots[i].data = jpeg_alloc_decoder_mem(FRAME_BYTES, &output, &slots[i].size);
        if (!slots[i].data || slots[i].size < FRAME_BYTES) return false;
    }
    jpeg_input = jpeg_alloc_decoder_mem(JPEG_LIMIT, &input, &jpeg_input_size);
    return jpeg_input != NULL;
}
const uint8_t *m3_tube_frame(void) {
    int now = atomic_load(&clock_ms);
    if (now < 0) return NULL;
    const uint8_t *shown = NULL;
    portENTER_CRITICAL(&frame_lock);
    int best = -1;
    for (int i = 0; i < FRAME_SLOTS; ++i)
        if (slots[i].state == SLOT_READY && slots[i].pts <= (uint32_t)now + 15 && (best < 0 || slots[i].pts > slots[best].pts)) best = i;
    if (best >= 0) {
        for (int i = 0; i < FRAME_SLOTS; ++i)
            if (i != best && (slots[i].state == SLOT_SHOWN || (slots[i].state == SLOT_READY && slots[i].pts < slots[best].pts)))
                slots[i].state = SLOT_FREE;
        slots[best].state = SLOT_SHOWN; shown = slots[best].data;
    }
    portEXIT_CRITICAL(&frame_lock);
    return shown;
}
/* Returns a slot to decode into, recycling frames the clock has already passed. */
static int frame_claim(int now) {
    int slot = -1;
    portENTER_CRITICAL(&frame_lock);
    for (int i = 0; i < FRAME_SLOTS && slot < 0; ++i) if (slots[i].state == SLOT_FREE) slot = i;
    for (int i = 0; i < FRAME_SLOTS && slot < 0; ++i)
        if (slots[i].state == SLOT_READY && (int)slots[i].pts + 200 < now) slot = i;
    if (slot >= 0) slots[slot].state = SLOT_DECODING;
    portEXIT_CRITICAL(&frame_lock);
    return slot;
}
static void frames_reset(void) {
    portENTER_CRITICAL(&frame_lock);
    for (int i = 0; i < FRAME_SLOTS; ++i) if (slots[i].state != SLOT_SHOWN) slots[i].state = SLOT_FREE;
    portEXIT_CRITICAL(&frame_lock);
}

/* ---------- Player state ---------- */
static portMUX_TYPE player_lock = portMUX_INITIALIZER_UNLOCKED;
static m3_tube_player_t player;
static atomic_bool paused;
void m3_tube_player(m3_tube_player_t *out) {
    portENTER_CRITICAL(&player_lock); *out = player; portEXIT_CRITICAL(&player_lock);
    out->paused = atomic_load(&paused);
}
void m3_tube_pause(bool value) { atomic_store(&paused, value); }
bool m3_tube_play(const char *id, unsigned start_seconds) {
    return m3_tube_configured() && video_id_valid(id) && start_seconds < 86400 && m3_audio_tube(id, start_seconds, false);
}
bool m3_tube_play_audio(const char *id, unsigned start_seconds) {
    return m3_tube_configured() && video_id_valid(id) && start_seconds < 86400 && m3_audio_tube(id, start_seconds, true);
}
static void player_update(bool buffering, unsigned percent, unsigned position_ms) {
    portENTER_CRITICAL(&player_lock);
    player.buffering = buffering; player.buffer_percent = percent; player.position_ms = position_ms; ++player.revision;
    portEXIT_CRITICAL(&player_lock);
}

/* ---------- Stream receiver (sole HTTP reader, like the radio) ---------- */
typedef struct {
    StreamBufferHandle_t stream;
    StaticStreamBuffer_t control;
    esp_http_client_handle_t http;
    uint8_t *storage, *chunk;
    atomic_bool stop, done;
    const char *error;
} tube_rx_t;
static void tube_receive(void *arg) {
    tube_rx_t *rx = arg;
    int64_t last_data = esp_timer_get_time();
    while (!atomic_load(&rx->stop)) {
        int bytes = esp_http_client_read(rx->http, (char *)rx->chunk, READ_BYTES);
        if (bytes <= 0) {
            if (esp_http_client_is_complete_data_received(rx->http)) break;
            if (network_get_mode() != NET_MODE_STA_CONNECTED) { rx->error = "Video Wi-Fi disconnected"; break; }
            if (bytes < 0 && bytes != -ESP_ERR_HTTP_EAGAIN) { rx->error = "Video connection lost"; break; }
            if (esp_timer_get_time() - last_data > 60000000) { rx->error = "Video stream stalled"; break; }
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        unsigned offset = 0;
        while (offset < (unsigned)bytes && !atomic_load(&rx->stop))
            offset += xStreamBufferSend(rx->stream, rx->chunk + offset, (unsigned)bytes - offset, pdMS_TO_TICKS(50));
        last_data = esp_timer_get_time(); /* A full ring is backpressure, not a stall. */
    }
    atomic_store(&rx->done, true);
    vTaskDelete(NULL);
}

/* ---------- Consumer on the audio worker ---------- */
typedef struct {
    tube_rx_t *rx;
    const m3_audio_output_t *out;
    unsigned start_ms;
    size_t prebuffer, rebuffer;
    bool music; /* YouTube Music session: status messages say "Music" */
} tube_play_t;
/* Waits for buffered data; while waiting the clock stands still (I2S plays silence). */
static bool wait_buffer(tube_play_t *play, size_t target, unsigned position_ms) {
    int64_t last_message = 0;
    while (!play->out->cancelled()) {
        size_t available = xStreamBufferBytesAvailable(play->rx->stream);
        bool done = atomic_load(&play->rx->done);
        if (available >= target || (done && available)) { player_update(false, 100, position_ms); return true; }
        if (done) return false;
        unsigned percent = (unsigned)(available * 100 / target);
        if (esp_timer_get_time() - last_message > 250000) {
            char text[64]; snprintf(text, sizeof(text), play->music ? "Music buffering / %u%%" : "Video buffering / %u%%", percent);
            play->out->message(text, true, percent); player_update(true, percent, position_ms);
            last_message = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
static bool read_exact(tube_play_t *play, void *buffer, size_t length, unsigned position_ms) {
    size_t got = 0;
    while (got < length) {
        if (play->out->cancelled()) return false;
        size_t received = xStreamBufferReceive(play->rx->stream, (uint8_t *)buffer + got, length - got, pdMS_TO_TICKS(20));
        got += received;
        if (!received && !xStreamBufferBytesAvailable(play->rx->stream) &&
            !wait_buffer(play, atomic_load(&play->rx->done) ? 1 : play->rebuffer, position_ms)) return false;
    }
    return true;
}
static bool skip_bytes(tube_play_t *play, size_t length, unsigned position_ms) {
    uint8_t scratch[256];
    while (length) {
        size_t step = length < sizeof(scratch) ? length : sizeof(scratch);
        if (!read_exact(play, scratch, step, position_ms)) return false;
        length -= step;
    }
    return true;
}
static void apply_info(const char *text, unsigned *rate, unsigned *channels, bool *video, bool *audio, unsigned *start_s) {
    cJSON *root = cJSON_Parse(text);
    if (!root) return;
    const cJSON *value;
    if (cJSON_IsNumber(value = cJSON_GetObjectItem(root, "rate"))) *rate = (unsigned)value->valuedouble;
    if (cJSON_IsNumber(value = cJSON_GetObjectItem(root, "ch"))) *channels = (unsigned)value->valuedouble;
    if ((value = cJSON_GetObjectItem(root, "video"))) *video = cJSON_IsTrue(value);
    if ((value = cJSON_GetObjectItem(root, "audio"))) *audio = cJSON_IsTrue(value);
    if (cJSON_IsNumber(value = cJSON_GetObjectItem(root, "start"))) *start_s = (unsigned)value->valuedouble;
    int w = cJSON_IsNumber(value = cJSON_GetObjectItem(root, "w")) ? (int)value->valuedouble : 0;
    int h = cJSON_IsNumber(value = cJSON_GetObjectItem(root, "h")) ? (int)value->valuedouble : 0;
    if (*video && (w != M3_TUBE_VIDEO_W || h != M3_TUBE_VIDEO_H)) { ESP_LOGW(TAG, "video %dx%d ignored", w, h); *video = false; }
    portENTER_CRITICAL(&player_lock);
    copy_text(player.title, sizeof(player.title), cJSON_GetObjectItem(root, "title"));
    copy_text(player.channel, sizeof(player.channel), cJSON_GetObjectItem(root, "channel"));
    copy_text(player.channel_id, sizeof(player.channel_id), cJSON_GetObjectItem(root, "channel_id"));
    value = cJSON_GetObjectItem(root, "duration");
    player.duration_s = cJSON_IsNumber(value) && value->valuedouble > 0 ? (unsigned)value->valuedouble : 0;
    player.video = *video; ++player.revision;
    portEXIT_CRITICAL(&player_lock);
    cJSON_Delete(root);
}
/* Mono 16-bit PCM -> interleaved stereo in place; the buffer holds twice the input. */
static void mono_to_stereo(int16_t *samples, unsigned frames) {
    for (unsigned i = frames; i-- > 0;) samples[2 * i] = samples[2 * i + 1] = samples[i];
}
#include "m3_tube_music.inc"
void m3_tube_run(const char *id, unsigned start_seconds, bool audio_only, const m3_audio_output_t *out) {
    const char *error = "Video connection failed";
    char music_error[128] = "Music cache unavailable";
    bool leased = false, lease_owned = false, ps_changed = false, receiver_started = false, codec_open = false, ended = false;
    esp_http_client_handle_t http = NULL;
    uint8_t *pcm = NULL;
    char *info = NULL;
    unsigned quality = atomic_load(&selected_quality);
    tube_rx_t rx = {.stop = ATOMIC_VAR_INIT(false), .done = ATOMIC_VAR_INIT(false)};
    tube_play_t play = {.rx = &rx, .out = out, .start_ms = start_seconds * 1000,
        .prebuffer = audio_only ? MUSIC_PREBUFFER_BYTES : quality == M3_TUBE_LOW ? LOW_PREBUFFER_BYTES :
            quality == M3_TUBE_STANDARD ? STANDARD_PREBUFFER_BYTES : VIDEO_PREBUFFER_BYTES,
        .rebuffer = audio_only ? MUSIC_REBUFFER_BYTES : quality == M3_TUBE_LOW ? LOW_REBUFFER_BYTES :
            quality == M3_TUBE_STANDARD ? STANDARD_REBUFFER_BYTES : VIDEO_REBUFFER_BYTES, .music = audio_only};
    portENTER_CRITICAL(&player_lock);
    unsigned revision = player.revision + 1;
    memset(&player, 0, sizeof(player)); player.active = player.buffering = true; player.position_ms = play.start_ms;
    player.audio_only = audio_only;
    snprintf(player.id, sizeof(player.id), "%s", id); player.revision = revision;
    portEXIT_CRITICAL(&player_lock);
    atomic_store(&paused, false); atomic_store(&clock_ms, -1); frames_reset();
    out->message(audio_only ? "Music connecting..." : "Video connecting...", true, 0);
    if (audio_only && music_cached_run(id, start_seconds, out, &ended, music_error, sizeof(music_error))) {
        error = music_error; goto done;
    }
    if (network_get_mode() != NET_MODE_STA_CONNECTED) { error = "Connect Wi-Fi first"; goto done; }
    if (!audio_only && !frames_ready()) { error = "Video decoder memory unavailable"; goto done; }
    leased = lease_tls(1500, &lease_owned);
    if (!leased) { error = "HTTPS busy; stop radio or video and retry"; goto done; }
    m3_relay_profile_t relay;
    if (!m3_relay_select(&relay)) { error = "Relay connection failed"; goto done; }
    ps_changed = m3_wifi_performance_acquire();
    rx.storage = heap_caps_malloc(RING_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rx.chunk = heap_caps_malloc(READ_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pcm = heap_caps_malloc(AUDIO_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    info = heap_caps_malloc(INFO_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx.storage || !rx.chunk || !pcm || !info) { error = "Video buffer allocation failed"; goto done; }
    rx.stream = xStreamBufferCreateStatic(RING_BYTES + 1, 1, rx.storage, &rx.control);
    char url[320]; snprintf(url, sizeof(url), "%s/api/play?id=%s&t=%u%s&quality=%s", relay.url, id, start_seconds,
        audio_only ? "&audio=1" : "", quality_name(quality));
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 15000, .buffer_size = 4096, .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .user_agent = "Mini-WebServer-M3/1.2",
    };
    http = esp_http_client_init(&config);
    if (!http) goto done;
    esp_http_client_set_header(http, "x-m3-token", relay.token);
    esp_http_client_set_header(http, "Accept-Encoding", "identity");
    if (out->cancelled() || esp_http_client_open(http, 0) != ESP_OK || esp_http_client_fetch_headers(http) < 0) goto done;
    int status = esp_http_client_get_status_code(http);
    if (status != 200) { error = status == 401 ? "Relay token rejected; check SD relay settings" : "Relay could not play this video"; goto done; }
    esp_http_client_set_timeout_ms(http, 250);
    rx.http = http;
    if (xTaskCreatePinnedToCore(tube_receive, "m3_tube_rx", 8192, &rx, 5, NULL, 0) != pdPASS) { error = "Video task allocation failed"; goto done; }
    receiver_started = true;

    unsigned rate = 24000, channels = 2, latency_ms = 60, position_ms = play.start_ms;
    bool video = !audio_only, audio = true, started = false;
    uint64_t written_us = 0;   /* stream time written to I2S */
    int64_t video_epoch = 0;   /* video-only clock origin */
    int64_t last_status = 0;
    for (;;) {
        uint8_t header[12];
        if (!started && !wait_buffer(&play, play.prebuffer, position_ms)) { if (!out->cancelled()) error = rx.error ? rx.error : "Video ended before buffering"; break; }
        if (!read_exact(&play, header, sizeof(header), position_ms)) { error = rx.error ? rx.error : "Video stream ended"; break; }
        unsigned type = header[0];
        uint32_t pts = header[4] | header[5] << 8 | header[6] << 16 | (uint32_t)header[7] << 24;
        uint32_t length = header[8] | header[9] << 8 | header[10] << 16 | (uint32_t)header[11] << 24;
        if (type == PACKET_WAIT) { if (length && !skip_bytes(&play, length, position_ms)) break; continue; }
        if (type == PACKET_INFO || type == PACKET_END) {
            if (length > INFO_LIMIT) { error = "Video stream format error"; break; }
            if (!read_exact(&play, info, length, position_ms)) break;
            info[length] = 0;
            if (type == PACKET_END) {
                cJSON *root = cJSON_Parse(info);
                const cJSON *reason = root ? cJSON_GetObjectItem(root, "reason") : NULL;
                ended = cJSON_IsString(reason) && !strcmp(reason->valuestring, "eof");
                if (!ended) {
                    const cJSON *detail = root ? cJSON_GetObjectItem(root, "error") : NULL;
                    copy_text(music_error, sizeof(music_error), detail);
                    error = music_error[0] ? music_error : "Relay could not play this video";
                }
                cJSON_Delete(root);
                break;
            }
            apply_info(info, &rate, &channels, &video, &audio, &start_seconds);
            if (audio_only || !jpeg) video = false;
            play.start_ms = start_seconds * 1000;
            if (rate < 8000 || rate > 48000 || (channels != 1 && channels != 2)) { error = "Video stream format error"; break; }
            latency_ms = OUTPUT_FRAMES * 1000 / rate;
            if (audio) { if (!out->begin(rate)) { error = "Codec open failed"; break; } codec_open = true; }
            started = true; video_epoch = esp_timer_get_time();
            continue;
        }
        if (!started) { error = "Video stream format error"; break; }
        while (atomic_load(&paused) && !out->cancelled()) {
            int64_t paused_at = esp_timer_get_time();
            if (esp_timer_get_time() - last_status > 500000) { out->message(audio_only ? "Music paused" : "Video paused", true, 0); last_status = esp_timer_get_time(); }
            vTaskDelay(pdMS_TO_TICKS(50));
            video_epoch += esp_timer_get_time() - paused_at;
        }
        if (type == PACKET_AUDIO) {
            /* Mono packets (half the bandwidth) are widened to the stereo I2S format here. */
            if (length > AUDIO_LIMIT / (3 - channels) || length % (2 * channels)) { error = "Video stream format error"; break; }
            if (!read_exact(&play, pcm, length, position_ms)) break;
            unsigned frames = length / (2 * channels);
            if (channels == 1) mono_to_stereo((int16_t *)pcm, frames);
            if (!audio) continue;
            if (!out->write(pcm, frames * 4)) { error = "Music I2S write failed"; break; }
            written_us = (uint64_t)pts * 1000 + (uint64_t)frames * 1000000 / rate;
            int now = (int)(written_us / 1000) - (int)latency_ms;
            atomic_store(&clock_ms, now < 0 ? 0 : now);
        } else if (type == PACKET_VIDEO) {
            if (!video || length > jpeg_input_size) { if (!skip_bytes(&play, length, position_ms)) break; continue; }
            if (!read_exact(&play, jpeg_input, length, position_ms)) break;
            if (!audio) { /* Pace video-only streams with the wall clock. */
                int64_t due = video_epoch + (int64_t)pts * 1000;
                while (esp_timer_get_time() < due && !out->cancelled()) vTaskDelay(pdMS_TO_TICKS(5));
                atomic_store(&clock_ms, (int)pts);
            }
            int slot = frame_claim(atomic_load(&clock_ms));
            if (slot < 0) { portENTER_CRITICAL(&player_lock); ++player.dropped; portEXIT_CRITICAL(&player_lock); continue; }
            uint32_t size = 0;
            bool ok = jpeg_decoder_process(jpeg, &(jpeg_decode_cfg_t){.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601},
                jpeg_input, length, slots[slot].data, slots[slot].size, &size) == ESP_OK && size >= FRAME_BYTES;
            portENTER_CRITICAL(&frame_lock);
            slots[slot].pts = pts; slots[slot].state = ok ? SLOT_READY : SLOT_FREE;
            portEXIT_CRITICAL(&frame_lock);
        } else if (!skip_bytes(&play, length, position_ms)) break;
        int now = atomic_load(&clock_ms);
        position_ms = play.start_ms + (now > 0 ? (unsigned)now : 0);
        if (esp_timer_get_time() - last_status > 500000) {
            m3_tube_player_t snapshot; m3_tube_player(&snapshot);
            out->message(audio_only ? "Music playing" : "Video playing", true, snapshot.duration_s ? position_ms / 10 / snapshot.duration_s : 0);
            player_update(false, 100, position_ms);
            last_status = esp_timer_get_time();
        }
    }
    if (ended && codec_open && !out->cancelled() && !out->drain()) { ended = false; error = "Music output drain failed"; }
done:
    atomic_store(&clock_ms, -1);
    if (receiver_started) {
        atomic_store(&rx.stop, true);
        while (!atomic_load(&rx.done)) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (http) { esp_http_client_close(http); esp_http_client_cleanup(http); }
    free(rx.storage); free(rx.chunk); free(pcm); free(info);
    if (ps_changed) m3_wifi_performance_release();
    if (lease_owned) https_memory_exclusive_end();
    frames_reset();
    portENTER_CRITICAL(&player_lock);
    player.active = false; player.buffering = false; player.ended = ended && !out->cancelled(); ++player.revision;
    portEXIT_CRITICAL(&player_lock);
    out->message(out->cancelled() ? (audio_only ? "Music stopped" : "Video stopped") :
        ended ? (audio_only ? "Music ended" : "Video ended") : error, false, ended ? 100 : 0);
    if (audio_only && ended && !out->cancelled()) music_follow(id);
}

esp_err_t m3_tube_start(void) {
    list_lock = xSemaphoreCreateMutex();
    list_requests = xQueueCreate(1, sizeof(list_request_t));
    json_buffer = heap_caps_malloc(JSON_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (unsigned i = 0; i < 4; ++i) thumb_sets[i] = heap_caps_calloc(M3_TUBE_ITEMS, THUMB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!list_lock || !list_requests || !json_buffer || !thumb_sets[0] || !thumb_sets[1] || !thumb_sets[2] || !thumb_sets[3]) goto failed;
    /* TLS handshakes need an internal-RAM stack. */
    if (xTaskCreatePinnedToCore(list_worker, "m3_tube", 10240, NULL, 3, NULL, 0) != pdPASS) goto failed;
    if (music_cache_start() != ESP_OK) ESP_LOGW(TAG, "Music cache worker unavailable; using relay playback");
    return ESP_OK;
failed:
    for (unsigned i = 0; i < 4; ++i) { free(thumb_sets[i]); thumb_sets[i] = NULL; }
    free(json_buffer); json_buffer = NULL;
    if (list_requests) { vQueueDelete(list_requests); list_requests = NULL; }
    if (list_lock) { vSemaphoreDelete(list_lock); list_lock = NULL; }
    return ESP_ERR_NO_MEM;
}
