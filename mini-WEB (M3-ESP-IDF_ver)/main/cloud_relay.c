#include "m3_cores.h"
#include "cloud_relay.h"
#include "https_memory.h"
#include "sd_media.h"
#include "network_manager.h"
#include "web_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "cJSON.h"
#include "cloud_relay_config.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum { WAIT_WIFI, FACTORY_MISSING, CONNECTING, ONLINE, RETRY, WAIT_TIME, WAIT_MEMORY };
static atomic_int state = WAIT_WIFI;
static atomic_uint last_ok_seconds;
static char public_url[96], device_id[32];
static TaskHandle_t relay_task_handle;
static atomic_bool relay_enabled = true;
bool cloud_relay_enabled(void) { return atomic_load(&relay_enabled); }
void cloud_relay_set_enabled(bool enabled) {
    atomic_store(&relay_enabled, enabled);
    atomic_store(&last_ok_seconds, 0);
    atomic_store(&state, WAIT_WIFI);
}

bool cloud_relay_public_path(const char *path) {
    if (!path || path[0] != '/' || strlen(path) > 180) return false;
    if (!strcmp(path, "/")) return true;
    if (strstr(path, "..") || strstr(path, "//")) return false;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' && p[1] == '.') return false;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || strchr("/_-.", *p))) return false;
    }
    const char *reserved[] = {"api", "admin", "setup", "ai", "__relay"};
    for (unsigned i = 0; i < sizeof(reserved)/sizeof(reserved[0]); ++i) {
        size_t n = strlen(reserved[i]);
        if (!strncasecmp(path + 1, reserved[i], n) &&
            (path[n + 1] == '/' || path[n + 1] == '.' || path[n + 1] == 0)) return false;
    }
    const char *ext = strrchr(path, '.');
    const char *allowed[] = {".html", ".htm", ".css", ".js", ".json", ".txt", ".svg", ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".mp4", ".webm", ".mp3", ".wav", ".m4a", ".pdf", ".woff", ".woff2", ".ttf"};
    for (unsigned i = 0; ext && i < sizeof(allowed)/sizeof(allowed[0]); ++i)
        if (!strcasecmp(ext, allowed[i])) return true;
    return false;
}
bool cloud_relay_is_online(void) {
    return cloud_relay_enabled() && !https_memory_exclusive_pending() && atomic_load(&state) == ONLINE && web_server_is_running() &&
        network_get_mode() == NET_MODE_STA_CONNECTED &&
        (uint32_t)(esp_timer_get_time()/1000000) - atomic_load(&last_ok_seconds) < 45;
}
const char *cloud_relay_status(void) {
    if (!cloud_relay_enabled()) return "stopped";
    if (atomic_load(&state) == FACTORY_MISSING) return "factory_missing";
    if (!web_server_is_running() || network_get_mode() != NET_MODE_STA_CONNECTED) return "waiting_wifi";
    if (https_memory_exclusive_pending()) return "paused_ai";
    if (cloud_relay_is_online()) return "online";
    if (atomic_load(&state) == WAIT_MEMORY) return "waiting_memory";
    return atomic_load(&state) == WAIT_TIME ? "waiting_time" : "connecting";
}
esp_err_t cloud_relay_get_url(char *out, size_t length) {
    if (!out || !length) return ESP_ERR_INVALID_ARG;
    out[0] = 0;
    if (!cloud_relay_is_online()) return ESP_ERR_INVALID_STATE;
    return cloud_relay_get_public_url(out, length);
}
esp_err_t cloud_relay_get_public_url(char *out, size_t length) {
    if (!out || !length) return ESP_ERR_INVALID_ARG;
    out[0] = 0;
    if (!public_url[0]) return ESP_ERR_INVALID_STATE;
    int n = snprintf(out, length, "%s", public_url);
    if (n < 0 || (size_t)n >= length) { out[0] = 0; return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
typedef struct { char body[4096]; size_t used; bool overflow; } reply_t;
static esp_err_t cloud_event(esp_http_client_event_t *event) {
    reply_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (r->used + event->data_len >= sizeof(r->body)) { r->overflow = true; return ESP_FAIL; }
        memcpy(r->body + r->used, event->data, event->data_len);
        r->used += event->data_len; r->body[r->used] = 0;
    }
    return ESP_OK;
}
static int exchange(esp_http_client_handle_t client, reply_t *reply, const char *suffix,
                    const void *data, size_t length, int sequence) {
    if (!cloud_relay_enabled() || !web_server_is_running()) return -1;
    char url[256];
    int n = snprintf(url, sizeof(url), "%s/v1/devices/%s/%s", CONFIG_MINI_WEB_RELAY_URL, device_id, suffix);
    if (n < 0 || (size_t)n >= sizeof(url)) return -1;
    memset(reply, 0, sizeof(*reply));
    esp_http_client_set_url(client, url);
    esp_http_client_set_method(client, data ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    esp_http_client_set_post_field(client, data, length);
    if (data) esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    else esp_http_client_delete_header(client, "Content-Type");
    esp_http_client_delete_header(client, "X-Sequence");
    if (sequence >= 0) { char seq[16]; snprintf(seq, sizeof(seq), "%d", sequence); esp_http_client_set_header(client, "X-Sequence", seq); }
    if (esp_http_client_perform(client) != ESP_OK || reply->overflow) return -1;
    return esp_http_client_get_status_code(client);
}
static bool result_post(esp_http_client_handle_t cloud, reply_t *reply, const char *id,
                        const char *action, const void *data, size_t len, int seq) {
    char suffix[96]; snprintf(suffix, sizeof(suffix), "result/%s/%s", id, action);
    return exchange(cloud, reply, suffix, data, len, seq) == 204;
}
static bool serve_job(esp_http_client_handle_t cloud, reply_t *reply) {
    if (!cloud_relay_enabled() || !web_server_is_running()) return false;
    cJSON *job = cJSON_Parse(reply->body);
    const cJSON *j_id = cJSON_GetObjectItemCaseSensitive(job, "id");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(job, "kind");
    if(cJSON_IsString(kind) && !strcmp(kind->valuestring,"upload")) {
        if(!cJSON_IsString(j_id)||strlen(j_id->valuestring)!=36) {cJSON_Delete(job);return false;}
        char id[40];snprintf(id,sizeof(id),"%s",j_id->valuestring);
        for(const char *p=id;*p;++p) if(!strchr("0123456789abcdef-",*p)) {cJSON_Delete(job);return false;}
        cJSON *result=sd_media_command(job);cJSON_Delete(job);
        char *json=result?cJSON_PrintUnformatted(result):NULL;cJSON_Delete(result);
        bool sent=json&&result_post(cloud,reply,id,"ack",json,strlen(json),-1);
        free(json);return sent;
    }
    const cJSON *j_path = cJSON_GetObjectItemCaseSensitive(job, "path");
    const cJSON *j_range = cJSON_GetObjectItemCaseSensitive(job, "range");
    char id[40] = {0}, path[192], range[80] = {0};
    bool valid = cJSON_IsString(j_id) && strlen(j_id->valuestring) == 36 &&
        cJSON_IsString(j_path) && cloud_relay_public_path(j_path->valuestring);
    if (valid) {
        snprintf(id, sizeof(id), "%s", j_id->valuestring);
        for (const char *p = id; *p; ++p) if (!strchr("0123456789abcdef-", *p)) valid = false;
        snprintf(path, sizeof(path), "%s", j_path->valuestring);
    }
    if (cJSON_IsString(j_range) && j_range->valuestring[0]) {
        if (strlen(j_range->valuestring) >= sizeof(range) || strncmp(j_range->valuestring, "bytes=", 6)) valid = false;
        else {
            snprintf(range, sizeof(range), "%s", j_range->valuestring);
            for (const char *p = range + 6; *p; ++p) if (!strchr("0123456789-", *p)) valid = false;
        }
    }
    cJSON_Delete(job);
    if (!valid) return false;
    web_public_stream_t stream;
    if (!web_server_public_open(path, range, &stream)) return false;
    bool ok = false; uint32_t bytes = 0; int chunks = 0;
    cJSON *start = cJSON_CreateObject(), *headers = cJSON_CreateObject();
    if (!start || !headers) { cJSON_Delete(start); cJSON_Delete(headers); goto done; }
    cJSON_AddNumberToObject(start, "status", stream.status); cJSON_AddItemToObject(start, "headers", headers);
    cJSON_AddStringToObject(headers, "content-type", stream.mime);
    if (stream.content_range[0]) cJSON_AddStringToObject(headers, "content-range", stream.content_range);
    if (stream.file) cJSON_AddStringToObject(headers, "accept-ranges", "bytes");
    char *json = cJSON_PrintUnformatted(start); cJSON_Delete(start);
    bool started = json && result_post(cloud, reply, id, "start", json, strlen(json), -1);
    free(json); if (!started) goto done;
    char buffer[2048]; size_t count;
    while ((count = web_server_public_read(&stream, buffer, sizeof(buffer))) > 0) {
        if (!cloud_relay_enabled() || https_memory_exclusive_pending() || network_get_mode() != NET_MODE_STA_CONNECTED || !web_server_is_running() ||
            bytes + count > 8 * 1024 * 1024 || !result_post(cloud, reply, id, "chunk", buffer, count, chunks)) goto done;
        bytes += count; chunks++;
        atomic_store(&last_ok_seconds, (uint32_t)(esp_timer_get_time()/1000000));
    }
    if (stream.offset != stream.length) goto done;
    char end[80]; snprintf(end, sizeof(end), "{\"bytes\":%lu,\"chunks\":%d}", (unsigned long)bytes, chunks);
    ok = result_post(cloud, reply, id, "end", end, strlen(end), -1);
done:
    web_server_public_close(&stream);
    /* A local transfer failure does not require another TLS handshake if the
     * relay acknowledged its abort and released the active job. */
    if (!ok) {
        ESP_LOGW("CLOUD_RELAY", "Local web transfer failed; notifying relay");
        /* Do not keep an AI request waiting behind an unreachable relay. */
        if (https_memory_exclusive_pending()) esp_http_client_set_timeout_ms(cloud, 3000);
        bool acknowledged = result_post(cloud, reply, id, "abort", "{}", 2, -1);
        esp_http_client_set_timeout_ms(cloud, 30000);
        return acknowledged;
    }
    return ok;
}
static void relay_task(void *arg) {
    /* SDK debug logging can print Authorization headers. */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    esp_log_level_set("HTTP_HEADER", ESP_LOG_WARN);
    char token[65] = {0}; nvs_handle_t nvs; size_t length = sizeof(token);
    bool provisioned = false;
    if (nvs_open("cloud_relay", NVS_READONLY, &nvs) == ESP_OK) {
        provisioned = nvs_get_str(nvs, "token", token, &length) == ESP_OK && strlen(token) == 64;
        nvs_close(nvs);
    }
    for (const char *p = token; provisioned && *p; ++p) if (!strchr("0123456789abcdef", *p)) provisioned = false;
    if (!provisioned || strncmp(CONFIG_MINI_WEB_RELAY_URL, "https://", 8)) {
        memset(token, 0, sizeof(token)); atomic_store(&state, FACTORY_MISSING); vTaskDelete(NULL); return;
    }
    char authorization[72]; snprintf(authorization, sizeof(authorization), "Bearer %s", token);
    memset(token, 0, sizeof(token));
    reply_t *reply = NULL;
    esp_http_client_config_t cfg = {.url = CONFIG_MINI_WEB_RELAY_URL, .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .event_handler = cloud_event, .buffer_size = 1024, .buffer_size_tx = 512, .keep_alive_enable = true};
    esp_http_client_handle_t cloud = NULL; unsigned retry = 1;
    bool memory_warning = false;
    while (1) {
        if (!cloud_relay_enabled()) {
            if (cloud) { esp_http_client_cleanup(cloud); cloud = NULL; }
            free(reply); reply = NULL; https_memory_relay_leave();
            atomic_store(&state, WAIT_WIFI);
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (https_memory_exclusive_pending()) {
            if (cloud) { esp_http_client_cleanup(cloud); cloud = NULL; }
            free(reply); reply = NULL;
            /* Signal only after TLS and the response buffer have been freed. */
            https_memory_relay_leave();
            atomic_store(&state, WAIT_MEMORY);
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (network_get_mode() != NET_MODE_STA_CONNECTED || !web_server_is_running()) {
            atomic_store(&state, WAIT_WIFI);
            if (cloud) { esp_http_client_cleanup(cloud); cloud = NULL; }
            free(reply); reply = NULL; https_memory_relay_leave();
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        if (time(NULL) < 1735689600) {
            atomic_store(&state, WAIT_TIME);
            /* The M3 service task starts SNTP independently of cloud setup. */
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        if (!cloud) {
            /* TLS handshake needs both total free heap and a contiguous record
             * buffer. Leave room for Wi-Fi, mDNS and the local HTTP server. */
            size_t available = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            if (available < 64 * 1024 || largest < 24 * 1024 ||
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32 * 1024 ||
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 8 * 1024) {
                atomic_store(&state, WAIT_MEMORY);
                if (!memory_warning) {
                    ESP_LOGW("CLOUD_RELAY", "Waiting for TLS memory: free=%u, largest=%u",
                        (unsigned)available, (unsigned)largest);
                    memory_warning = true;
                }
                vTaskDelay(pdMS_TO_TICKS(5000)); continue;
            }
            memory_warning = false;
            if (!https_memory_relay_enter()) continue;
            reply = calloc(1, sizeof(*reply));
            if (!reply) { https_memory_relay_leave(); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            cfg.user_data = reply;
            cloud = esp_http_client_init(&cfg);
            if (!cloud) {
                free(reply); reply = NULL; https_memory_relay_leave();
                vTaskDelay(pdMS_TO_TICKS(5000)); continue;
            }
            esp_http_client_set_header(cloud, "Authorization", authorization);
        }
        if (!cloud_relay_is_online()) atomic_store(&state, CONNECTING);
        int code = exchange(cloud, reply, "poll", NULL, 0, -1);
        if (cloud_relay_enabled() && web_server_is_running() && (code == 200 || code == 204)) {
            atomic_store(&last_ok_seconds, (uint32_t)(esp_timer_get_time()/1000000)); atomic_store(&state, ONLINE); retry = 1;
            if (code == 204 || serve_job(cloud, reply)) continue;
        }
        atomic_store(&state, RETRY);
        esp_http_client_cleanup(cloud); cloud = NULL;
        free(reply); reply = NULL; https_memory_relay_leave();
        ESP_LOGW("CLOUD_RELAY", "Connection retry: HTTP=%d, free=%u, largest=%u", code,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        unsigned delay = retry * 1000 + esp_random()%1000;
        for (unsigned waited = 0; waited < delay && cloud_relay_enabled(); waited += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (retry < 30) retry *= 2;
    }
}
esp_err_t cloud_relay_provision(const char *token) {
    if (!token || strlen(token) != 64) return ESP_ERR_INVALID_ARG;
    for (const char *p = token; *p; ++p)
        if (!strchr("0123456789abcdef", *p)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("cloud_relay", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "token", token);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
esp_err_t cloud_relay_start(void) {
    if (relay_task_handle) return ESP_OK;
    snprintf(device_id, sizeof(device_id), "%s-%s", network_get_product_name(), network_get_product_serial());
    int n;
    if (CONFIG_MINI_WEB_PUBLIC_HOST[0]) n = snprintf(public_url, sizeof(public_url), "https://%s/", CONFIG_MINI_WEB_PUBLIC_HOST);
    else n = snprintf(public_url, sizeof(public_url), "https://%s.%s/", device_id, CONFIG_MINI_WEB_PUBLIC_DOMAIN);
    if (n < 0 || (size_t)n >= sizeof(public_url)) { public_url[0] = 0; return ESP_ERR_INVALID_SIZE; }
    return xTaskCreatePinnedToCore(relay_task, "cloud_relay", 10240, NULL, 3, &relay_task_handle, M3_CORE_NET) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
