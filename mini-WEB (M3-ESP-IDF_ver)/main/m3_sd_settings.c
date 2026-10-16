#include "m3_sd_settings.h"
#include "m3_gemini_config.h"
#include "m3_azure_config.h"
#include "network_manager.h"
#include "sd_media.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define PRIVATE_ROOT SD_MEDIA_ROOT "/.private"
#define KEYS_PATH PRIVATE_ROOT "/api_keys.json"
#define WIFI_PATH PRIVATE_ROOT "/wifi.json"
enum { JSON_LIMIT = 65536, KEY_LIMIT = 256 };
/* Published once. Readers keep immutable pointers for the entire HTTP request. */
static char gemini[KEY_LIMIT], tts[KEY_LIMIT], openai[KEY_LIMIT];
static char azure[KEY_LIMIT], azure_region[32];
static atomic_bool keys_loaded;
const char *m3_sd_gemini_key(void) { return atomic_load(&keys_loaded) ? gemini : M3_GEMINI_API_KEY; }
const char *m3_sd_google_tts_key(void) { return atomic_load(&keys_loaded) ? tts : M3_GOOGLE_TTS_API_KEY; }
const char *m3_sd_openai_key(void) { return atomic_load(&keys_loaded) ? openai : M3_OPENAI_API_KEY; }

const char *m3_sd_azure_key(void) { return atomic_load(&keys_loaded) ? azure : M3_AZURE_SPEECH_KEY; }
const char *m3_sd_azure_region(void) { return atomic_load(&keys_loaded) ? azure_region : M3_AZURE_SPEECH_REGION; }

static void destroy_json(cJSON *value) {
    for (cJSON *item = value; item; item = item->next) {
        if (item->valuestring) mbedtls_platform_zeroize(item->valuestring, strlen(item->valuestring));
        if (item->child) destroy_json(item->child);
    }
}
static void release_json(cJSON *value) { destroy_json(value); cJSON_Delete(value); }
/* Caller owns the SD management lock. Distinguish absent from unreadable files:
 * a malformed or oversized history must never be overwritten with an empty one. */
static cJSON *read_json(const char *path, bool *absent) {
    *absent = false;
    FILE *file = fopen(path, "rb");
    if (!file) { *absent = errno == ENOENT; return NULL; }
    cJSON *json = NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size < 2 || size > JSON_LIMIT || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char *data = heap_caps_malloc((size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data) {
        if (fread(data, 1, size, file) == (size_t)size) {
            data[size] = 0;
            json = cJSON_ParseWithLengthOpts(data, (size_t)size + 1, NULL, true);
            if (!cJSON_IsObject(json)) { release_json(json); json = NULL; }
        }
        mbedtls_platform_zeroize(data, (size_t)size + 1); free(data);
    }
    fclose(file); return json;
}
static bool history_valid(const cJSON *json);
static cJSON *read_history(bool *absent) {
    cJSON *json = read_json(WIFI_PATH, absent);
    if (json && history_valid(json)) return json;
    release_json(json);
    bool backup_absent;
    json = read_json(WIFI_PATH ".bak", &backup_absent);
    *absent = *absent && backup_absent;
    return json;
}
static bool field(const cJSON *obj, const char *name, char *out, size_t capacity) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= capacity) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1); return true;
}
static bool profile(const cJSON *obj, network_settings_t *s) {
    memset(s, 0, sizeof(*s)); s->version = 1;
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(obj, "static");
    if (!cJSON_IsBool(mode)) return false;
    s->use_static = cJSON_IsTrue(mode);
    return field(obj, "ssid", s->ssid, sizeof(s->ssid)) && field(obj, "password", s->pass, sizeof(s->pass)) &&
        (!s->use_static || (field(obj, "ip", s->ip, sizeof(s->ip)) &&
         field(obj, "gateway", s->gateway, sizeof(s->gateway)) &&
         field(obj, "netmask", s->netmask, sizeof(s->netmask)) && field(obj, "dns", s->dns, sizeof(s->dns)))) &&
        network_settings_valid(s);
}
static bool history_valid(const cJSON *json) {
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *networks = cJSON_GetObjectItemCaseSensitive(json, "networks");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsArray(networks)) return false;
    network_settings_t s;
    bool valid = true;
    const cJSON *item;
    cJSON_ArrayForEach(item, networks) if (!profile(item, &s)) { valid = false; break; }
    mbedtls_platform_zeroize(&s, sizeof(s)); return valid;
}
static bool write_history(cJSON *json) {
    char *data = cJSON_PrintUnformatted(json);
    if (!data) return false;
    size_t length = strlen(data);
    bool ok = length <= JSON_LIMIT;
    FILE *file = ok ? fopen(WIFI_PATH ".tmp", "wb") : NULL;
    if (!file) ok = false;
    else {
        ok = fwrite(data, 1, length, file) == length && !fflush(file) && !fsync(fileno(file));
        if (fclose(file)) ok = false;
    }
    mbedtls_platform_zeroize(data, length); cJSON_free(data);
    if (!ok) { unlink(WIFI_PATH ".tmp"); return false; }
    /* Keep the last committed version through interruption of FAT renames. */
    struct stat st;
    if (!stat(WIFI_PATH, &st)) {
        bool absent;
        cJSON *previous = read_json(WIFI_PATH, &absent);
        bool valid = history_valid(previous);
        release_json(previous);
        if (valid) {
            if (unlink(WIFI_PATH ".bak") && errno != ENOENT) return false;
            if (rename(WIFI_PATH, WIFI_PATH ".bak")) return false;
        } else if (unlink(WIFI_PATH)) return false; /* Keep the good recovery backup. */
    }
    if (rename(WIFI_PATH ".tmp", WIFI_PATH)) {
        rename(WIFI_PATH ".bak", WIFI_PATH); return false;
    }
    return true;
}
void m3_sd_wifi_save(const network_settings_t *s) {
    if (!network_settings_valid(s) || !sd_media_ready()) return;
    if (!sd_media_admin_lock()) { ESP_LOGW("SD_CONFIG", "Wi-Fi SD backup busy; NVS retained"); return; }
    bool absent;
    cJSON *json = read_history(&absent);
    bool ok = true;
    if (!json && absent) {
        json = cJSON_CreateObject();
        ok = json && cJSON_AddNumberToObject(json, "version", 1) && cJSON_AddArrayToObject(json, "networks");
    }
    ok = ok && history_valid(json);
    cJSON *networks = cJSON_GetObjectItemCaseSensitive(json, "networks");
    cJSON *item = ok ? cJSON_CreateObject() : NULL;
    ok = ok && item && cJSON_AddStringToObject(item, "ssid", s->ssid) &&
        cJSON_AddStringToObject(item, "password", s->pass) && cJSON_AddBoolToObject(item, "static", s->use_static) &&
        cJSON_AddStringToObject(item, "ip", s->ip) && cJSON_AddStringToObject(item, "gateway", s->gateway) &&
        cJSON_AddStringToObject(item, "netmask", s->netmask) && cJSON_AddStringToObject(item, "dns", s->dns);
    if (ok) {
        int index = 0; cJSON *old;
        cJSON_ArrayForEach(old, networks) {
            const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(old, "ssid");
            if (!strcmp(ssid->valuestring, s->ssid)) break;
            ++index;
        }
        ok = old ? cJSON_ReplaceItemInArray(networks, index, item) : cJSON_AddItemToArray(networks, item);
        if (ok) item = NULL;
        cJSON_DeleteItemFromObjectCaseSensitive(json, "last_ssid");
        ok = ok && cJSON_AddStringToObject(json, "last_ssid", s->ssid);
        ok = ok && (mkdir(PRIVATE_ROOT, 0700) == 0 || errno == EEXIST) && write_history(json);
    }
    release_json(item); release_json(json); sd_media_admin_unlock();
    if (!ok) ESP_LOGW("SD_CONFIG", "Wi-Fi SD backup failed; existing history and NVS retained");
}
bool m3_sd_wifi_find(const char *ssid, network_settings_t *out) {
    if (!ssid || !out || !sd_media_ready() || !sd_media_admin_lock()) return false;
    bool absent, found = false;
    cJSON *json = read_history(&absent);
    const cJSON *last = cJSON_GetObjectItemCaseSensitive(json, "last_ssid");
    const char *wanted = *ssid ? ssid : cJSON_IsString(last) ? last->valuestring : "";
    if (history_valid(json)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(json, "networks")) {
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "ssid");
            if (!strcmp(name->valuestring, wanted)) { found = profile(item, out); break; }
        }
    }
    release_json(json); sd_media_admin_unlock(); return found;
}
static bool key_field(const cJSON *json, const char *name, char *out) {
    char candidate[KEY_LIMIT] = {0};
    bool ok = field(json, name, candidate, sizeof(candidate));
    size_t length = strlen(candidate);
    ok = ok && (length == 0 || length >= 20);
    for (size_t i = 0; i < length; ++i) if ((unsigned char)candidate[i] <= 32 || (unsigned char)candidate[i] >= 127) ok = false;
    if (ok) memcpy(out, candidate, sizeof(candidate));
    mbedtls_platform_zeroize(candidate, sizeof(candidate)); return ok;
}
void m3_sd_settings_init(void) {
    if (!sd_media_ready()) return;
    snprintf(gemini, sizeof(gemini), "%s", M3_GEMINI_API_KEY);
    snprintf(tts, sizeof(tts), "%s", M3_GOOGLE_TTS_API_KEY);
    snprintf(openai, sizeof(openai), "%s", M3_OPENAI_API_KEY);
    snprintf(azure, sizeof(azure), "%s", M3_AZURE_SPEECH_KEY);
    snprintf(azure_region, sizeof(azure_region), "%s", M3_AZURE_SPEECH_REGION);
    if (sd_media_admin_lock()) {
        bool absent;
        cJSON *json = read_json(KEYS_PATH, &absent);
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
        if (json && cJSON_IsNumber(version) && version->valuedouble == 1) {
            bool valid = key_field(json, "gemini", gemini);
            valid = key_field(json, "google_tts", tts) && valid;
            valid = key_field(json, "openai", openai) && valid;
            /* Optional Azure fields preserve compatibility with older SD files. */
            if (cJSON_GetObjectItemCaseSensitive(json, "azure_speech")) valid = key_field(json, "azure_speech", azure) && valid;
            const cJSON *region = cJSON_GetObjectItemCaseSensitive(json, "azure_region");
            if (cJSON_IsString(region)) {
                size_t length = strlen(region->valuestring); bool region_ok = length > 0 && length < sizeof(azure_region);
                for (size_t i = 0; i < length; ++i) {
                    char c = region->valuestring[i];
                    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) region_ok = false;
                }
                if (region_ok) snprintf(azure_region, sizeof(azure_region), "%s", region->valuestring);
                else valid = false;
            } else if (region) valid = false;
            if (!valid) ESP_LOGW("SD_CONFIG", "Invalid API key field; retained header fallback for that field");
        } else if (!absent) ESP_LOGW("SD_CONFIG", "API settings unreadable/version invalid; header fallback retained");
        release_json(json); sd_media_admin_unlock();
    }
    atomic_store(&keys_loaded, true);
    network_settings_t settings;
    if (!network_settings_load(&settings) && m3_sd_wifi_find("", &settings)) {
        esp_err_t err = network_save_configuration(&settings);
        if (err != ESP_OK) ESP_LOGW("SD_CONFIG", "Wi-Fi NVS restore failed");
    } else if (network_settings_valid(&settings)) m3_sd_wifi_save(&settings);
    mbedtls_platform_zeroize(&settings, sizeof(settings));
}
