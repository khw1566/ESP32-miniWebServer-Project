#include "m3_relay.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mbedtls/platform_util.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Immutable after boot; selection never modifies credentials used by a worker. */
static m3_relay_profile_t profiles[M3_RELAY_MAX];
static atomic_uint profile_count, selected;
static bool valid_profile(const cJSON *item, m3_relay_profile_t *out) {
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(item, "url");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(item, "token");
    if (!cJSON_IsString(url) || !cJSON_IsString(token)) return false;
    size_t n = strlen(url->valuestring), t = strlen(token->valuestring);
    if (n >= sizeof(out->url) || t < 24 || t >= sizeof(out->token)) return false;
    const size_t scheme = 8;
    if (strncmp(url->valuestring, "https://", scheme) || n <= scheme) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = url->valuestring[i];
        if (c <= 32 || c >= 127 || c == '@' || c == '?' || c == '#' || c == '\\') return false;
        if (i >= scheme && c == '/' && i != n - 1) return false;
    }
    for (size_t i = 0; i < t; ++i) if ((unsigned char)token->valuestring[i] <= 32 || (unsigned char)token->valuestring[i] >= 127) return false;
    snprintf(out->url, sizeof(out->url), "%s", url->valuestring);
    if (out->url[n - 1] == '/') out->url[n - 1] = 0;
    snprintf(out->token, sizeof(out->token), "%s", token->valuestring);
    return strlen(out->url) > scheme;
}
bool m3_relay_load(const cJSON *json) {
    atomic_store(&profile_count, 0);
    mbedtls_platform_zeroize(profiles, sizeof(profiles));
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(json, "servers");
    int count = cJSON_GetArraySize(servers);
    if (!cJSON_IsObject(json) || !cJSON_IsNumber(version) || version->valuedouble != 1 ||
        !cJSON_IsArray(servers) || count < 1 || count > M3_RELAY_MAX) return false;
    for (int i = 0; i < count; ++i) {
        if (!valid_profile(cJSON_GetArrayItem(servers, i), &profiles[i])) {
            mbedtls_platform_zeroize(profiles, sizeof(profiles)); return false;
        }
    }
    atomic_store(&selected, 0);
    atomic_store(&profile_count, (unsigned)count);
    return true;
}
unsigned m3_relay_count(void) { return atomic_load(&profile_count); }
bool m3_relay_current(m3_relay_profile_t *out) {
    unsigned count = m3_relay_count(), index = atomic_load(&selected);
    if (!out || !count) return false;
    *out = profiles[index < count ? index : 0]; return true;
}
typedef struct { char data[1024]; size_t length; bool overflow; } probe_t;
static esp_err_t probe_event(esp_http_client_event_t *event) {
    probe_t *probe = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (probe->length + event->data_len >= sizeof(probe->data)) probe->overflow = true;
        else { memcpy(probe->data + probe->length, event->data, event->data_len); probe->length += event->data_len; }
    }
    return ESP_OK;
}
static bool probe(const m3_relay_profile_t *profile) {
    char url[M3_RELAY_URL_SIZE + 16];
    snprintf(url, sizeof(url), "%s/api/status", profile->url);
    probe_t data = {0};
    esp_http_client_config_t config = {.url=url, .timeout_ms=5000, .buffer_size=1024,
        .crt_bundle_attach=esp_crt_bundle_attach, .disable_auto_redirect=true,
        .event_handler=probe_event, .user_data=&data};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "x-m3-token", profile->token);
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || data.overflow) {
        ESP_LOGW("M3_RELAY", "Candidate unavailable: HTTP %d, %s", status, esp_err_to_name(err)); return false;
    }
    data.data[data.length] = 0;
    cJSON *json = cJSON_Parse(data.data);
    const cJSON *service = cJSON_GetObjectItemCaseSensitive(json, "service");
    const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(json, "protocol");
    bool ready = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "ok")) &&
        cJSON_IsString(service) && !strcmp(service->valuestring, "m3-tube-relay") &&
        cJSON_IsNumber(protocol) && protocol->valuedouble >= 2;
    cJSON_Delete(json); return ready;
}
bool m3_relay_select(m3_relay_profile_t *out) {
    unsigned count = m3_relay_count(), start = atomic_load(&selected);
    if (!out || !count) return false;
    if (start >= count) start = 0;
    for (unsigned n = 0; n < count; ++n) {
        unsigned index = (start + n) % count;
        if (probe(&profiles[index])) {
            *out = profiles[index]; atomic_store(&selected, index); return true;
        }
    }
    memset(out, 0, sizeof(*out)); return false;
}
bool m3_relay_radio_route(const char *url, const char **suffix) {
    if (!url || !suffix) return false;
    for (unsigned i = 0; i < m3_relay_count(); ++i) {
        size_t n = strlen(profiles[i].url);
        if (!strncmp(url, profiles[i].url, n) && !strncmp(url + n, "/api/radio?", 11)) {
            *suffix = url + n; return true;
        }
    }
    return false;
}
