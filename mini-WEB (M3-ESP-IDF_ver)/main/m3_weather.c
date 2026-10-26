#include "m3_weather.h"
#include "m3_memory.h"
#include "m3_preferences.h"
#include "network_manager.h"
#include "sd_media.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define SETTINGS_PATH SD_MEDIA_ROOT "/weather.json"
#define REFRESH_US (15LL * 60 * 1000000)
#define RETRY_US (60LL * 1000000)
enum { RESPONSE_LIMIT = 12288, WORKER_STACK = 8192 };
#include "m3_weather_presets.inc"
typedef struct { unsigned kind; char query[M3_WEATHER_QUERY_BYTES], language[3]; m3_weather_place_t place; } request_t;
static QueueHandle_t requests;
static atomic_bool started;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static M3_PSRAM_BSS m3_weather_state_t state;
static bool dirty;
static int64_t next_fetch, next_save, next_manual;
unsigned m3_weather_preset_count(void) { return sizeof(weather_places) / sizeof(weather_places[0]); }
const m3_weather_place_t *m3_weather_preset(unsigned index) { return index < m3_weather_preset_count() ? &weather_places[index] : NULL; }
void m3_weather_state(m3_weather_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock);
    if (!out->place.name[0]) out->place = weather_places[0];
    out->stale = out->valid && (out->status == M3_WEATHER_OFFLINE || out->status == M3_WEATHER_NETWORK ||
        out->status == M3_WEATHER_INVALID || out->status == M3_WEATHER_MEMORY ||
        esp_timer_get_time() - out->updated_us >= REFRESH_US || network_get_mode() != NET_MODE_STA_CONNECTED);
}
static void finish(m3_weather_status_t status) {
    portENTER_CRITICAL(&lock); state.status = status; state.busy = false; ++state.revision; portEXIT_CRITICAL(&lock);
}
static bool submit(const request_t *request) {
    if (!atomic_load(&started) || m3_preferences_restarting() || m3_preferences_web_active()) return false;
    portENTER_CRITICAL(&lock);
    bool accept = !state.busy && (request->kind == 1 || esp_timer_get_time() >= next_manual);
    if (accept) { state.busy = true; state.status = M3_WEATHER_LOADING; ++state.revision; }
    portEXIT_CRITICAL(&lock);
    if (!accept) return false;
    if (xQueueSend(requests, request, 0) == pdPASS) return true;
    finish(M3_WEATHER_WAIT); return false;
}
bool m3_weather_refresh(void) { const request_t request = {0}; return submit(&request); }
bool m3_weather_select(const m3_weather_place_t *place) {
    if (!m3_weather_place_valid(place)) return false;
    request_t request = {.kind = 1, .place = *place}; return submit(&request);
}
bool m3_weather_search(const char *query, const char *language) {
    if (!query || strlen(query) < 2 || strlen(query) >= M3_WEATHER_QUERY_BYTES) return false;
    request_t request = {.kind = 2};
    snprintf(request.query, sizeof(request.query), "%s", query);
    snprintf(request.language, sizeof(request.language), "%s", language && (!strcmp(language, "ko") || !strcmp(language, "ja")) ? language : "en");
    return submit(&request);
}
/* Strict HTTPS, a fixed host, no redirects, and a bounded PSRAM response. */
static char *get_json(const char *url) {
    char *body = heap_caps_malloc(RESPONSE_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) return NULL;
    esp_http_client_config_t config = {.url = url, .timeout_ms = 8000, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .buffer_size = 1024};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool ok = client && esp_http_client_open(client, 0) == ESP_OK;
    int64_t length = ok ? esp_http_client_fetch_headers(client) : -1;
    ok = ok && length >= 0 && length <= RESPONSE_LIMIT && esp_http_client_get_status_code(client) == 200;
    int used = 0;
    int64_t deadline = esp_timer_get_time() + 12000000LL;
    while (ok && !esp_http_client_is_complete_data_received(client) && used < RESPONSE_LIMIT && esp_timer_get_time() < deadline &&
        !m3_preferences_web_active() && !m3_preferences_restarting()) {
        int n = esp_http_client_read(client, body + used, RESPONSE_LIMIT - used);
        if (n <= 0) { ok = false; break; }
        used += n;
    }
    ok = ok && used > 0 && esp_http_client_is_complete_data_received(client);
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    if (!ok) { free(body); return NULL; }
    body[used] = 0; return body;
}
/* Caller owns the SD lock. A backup survives interrupted FAT rename sequences. */
static bool read_setting(const char *path, m3_weather_place_t *place) {
    bool valid = false;
    FILE *file = fopen(path, "rb");
    if (file) {
        char json[768]; size_t n = fread(json, 1, sizeof(json) - 1, file);
        bool complete = !ferror(file) && fgetc(file) == EOF;
        fclose(file); json[n] = 0;
        valid = complete && m3_weather_parse_setting(json, place);
    }
    return valid;
}
static void load_setting(void) {
    m3_weather_place_t place = weather_places[0]; bool saved = false;
    if (sd_media_ready() && sd_media_admin_lock()) {
        saved = read_setting(SETTINGS_PATH, &place) || read_setting(SETTINGS_PATH ".bak", &place);
        sd_media_admin_unlock();
    }
    portENTER_CRITICAL(&lock); state.place = place; state.saved = saved; ++state.revision; portEXIT_CRITICAL(&lock);
}
static bool save_setting(const m3_weather_place_t *place) {
    if (!sd_media_ready() || !sd_media_admin_lock()) return false;
    cJSON *root = cJSON_CreateObject();
    bool ok = root && cJSON_AddNumberToObject(root, "version", 1) && cJSON_AddStringToObject(root, "name", place->name) &&
        cJSON_AddStringToObject(root, "area", place->area) && cJSON_AddNumberToObject(root, "latitude", place->latitude) &&
        cJSON_AddNumberToObject(root, "longitude", place->longitude);
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    FILE *file = json ? fopen(SETTINGS_PATH ".tmp", "wb") : NULL;
    ok = file && fwrite(json, 1, strlen(json), file) == strlen(json);
    if (file) {
        if (fflush(file) || fsync(fileno(file))) ok = false;
        if (fclose(file)) ok = false;
    }
    if (ok) {
        struct stat st;
        if (!stat(SETTINGS_PATH, &st)) {
            m3_weather_place_t previous;
            if (read_setting(SETTINGS_PATH, &previous)) {
                if (unlink(SETTINGS_PATH ".bak") && errno != ENOENT) ok = false;
                if (ok && rename(SETTINGS_PATH, SETTINGS_PATH ".bak")) ok = false;
            } else if (unlink(SETTINGS_PATH)) ok = false;
        }
        if (ok && rename(SETTINGS_PATH ".tmp", SETTINGS_PATH)) {
            rename(SETTINGS_PATH ".bak", SETTINGS_PATH); ok = false;
        }
    }
    if (!ok) unlink(SETTINGS_PATH ".tmp");
    cJSON_free(json); cJSON_Delete(root); sd_media_admin_unlock(); return ok;
}
static void fetch_weather(const m3_weather_place_t *place) {
    char url[640];
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&forecast_days=3&timezone=auto&wind_speed_unit=ms", place->latitude, place->longitude);
    char *json = get_json(url);
    m3_weather_data_t data;
    bool ok = json && m3_weather_parse(json, &data);
    free(json);
    if (ok) {
        portENTER_CRITICAL(&lock); state.data = data; state.valid = true; state.updated_us = esp_timer_get_time(); portEXIT_CRITICAL(&lock);
    }
    next_fetch = esp_timer_get_time() + (ok ? REFRESH_US : RETRY_US);
    finish(ok ? M3_WEATHER_READY : M3_WEATHER_NETWORK);
}
static void search_places(const request_t *request) {
    char encoded[M3_WEATHER_QUERY_BYTES * 3], url[M3_WEATHER_QUERY_BYTES * 3 + 160];
    if (!m3_weather_encode(request->query, encoded, sizeof(encoded))) { finish(M3_WEATHER_INVALID); return; }
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=5&language=%s&format=json", encoded, request->language);
    portENTER_CRITICAL(&lock); state.count = 0; portEXIT_CRITICAL(&lock);
    char *json = get_json(url);
    m3_weather_place_t results[M3_WEATHER_RESULTS]; unsigned count = 0;
    bool ok = json && m3_weather_parse_places(json, results, &count);
    free(json);
    if (ok) {
        portENTER_CRITICAL(&lock); memcpy(state.results, results, count * sizeof(results[0])); state.count = count; portEXIT_CRITICAL(&lock);
    }
    finish(!ok ? M3_WEATHER_NETWORK : count ? M3_WEATHER_READY : M3_WEATHER_EMPTY);
}
static void worker(void *unused) {
    (void)unused;
    request_t request;
    bool was_online = false;
    for (;;) {
        bool received = xQueueReceive(requests, &request, pdMS_TO_TICKS(1000)) == pdTRUE;
        if (m3_preferences_web_active() || m3_preferences_restarting()) { if (received) finish(M3_WEATHER_WAIT); continue; }
        if (received && request.kind == 1) {
            portENTER_CRITICAL(&lock);
            state.place = request.place; state.valid = false; state.saved = false; state.count = 0; ++state.revision;
            portEXIT_CRITICAL(&lock); dirty = true;
        }
        m3_weather_place_t place;
        portENTER_CRITICAL(&lock); place = state.place; portEXIT_CRITICAL(&lock);
        if (dirty && esp_timer_get_time() >= next_save) {
            bool saved = save_setting(&place); dirty = !saved; next_save = esp_timer_get_time() + 10000000;
            portENTER_CRITICAL(&lock); state.saved = saved; ++state.revision; portEXIT_CRITICAL(&lock);
        }
        bool online = network_get_mode() == NET_MODE_STA_CONNECTED;
        if (!online) {
            was_online = false;
            if (received) finish(M3_WEATHER_OFFLINE);
            else {
                portENTER_CRITICAL(&lock);
                if (!state.busy && state.status != M3_WEATHER_OFFLINE) { state.status = M3_WEATHER_OFFLINE; ++state.revision; }
                portEXIT_CRITICAL(&lock);
            }
            continue;
        }
        if (!was_online) next_fetch = 0;
        was_online = true;
        bool automatic = !received && esp_timer_get_time() >= next_fetch;
        if (automatic) {
            portENTER_CRITICAL(&lock);
            automatic = !state.busy;
            if (automatic) { state.busy = true; state.status = M3_WEATHER_LOADING; ++state.revision; }
            portEXIT_CRITICAL(&lock);
        }
        if (!received && !automatic) continue;
        if (received && request.kind == 2) search_places(&request);
        else fetch_weather(&place);
        portENTER_CRITICAL(&lock); next_manual = esp_timer_get_time() + 2000000; portEXIT_CRITICAL(&lock);
    }
}
void m3_weather_init(void) {
    if (atomic_load(&started)) return;
    /* Service startup after SD mount; no UI task touches the filesystem. */
    load_setting();
    requests = xQueueCreate(1, sizeof(request_t));
    if (!requests) { finish(M3_WEATHER_MEMORY); return; }
    if (xTaskCreate(worker, "m3_weather", WORKER_STACK, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(requests); requests = NULL; finish(M3_WEATHER_MEMORY); return;
    }
    atomic_store(&started, true);
}
