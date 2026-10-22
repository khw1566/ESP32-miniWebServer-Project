#include "m3_sd_settings.h"
#include "network_manager.h"
#include "sd_media.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
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
static portMUX_TYPE wifi_lock = portMUX_INITIALIZER_UNLOCKED;
static network_settings_t wifi_settings;
static bool wifi_loaded;
const char *m3_sd_gemini_key(void) { return atomic_load(&keys_loaded) ? gemini : ""; }
const char *m3_sd_google_tts_key(void) { return atomic_load(&keys_loaded) ? tts : ""; }
const char *m3_sd_openai_key(void) { return atomic_load(&keys_loaded) ? openai : ""; }

const char *m3_sd_azure_key(void) { return atomic_load(&keys_loaded) ? azure : ""; }
const char *m3_sd_azure_region(void) { return atomic_load(&keys_loaded) ? azure_region : "koreacentral"; }

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
    if (!file) {
        *absent = errno == ENOENT;
        if (!*absent) ESP_LOGW("SD_CONFIG", "Settings open failed: %s (errno=%d)", path, errno);
        return NULL;
    }
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
    fclose(file);
    if (!json) ESP_LOGW("SD_CONFIG", "Settings JSON unreadable or invalid: %s", path);
    return json;
}
static bool history_valid(const cJSON *json);
static bool keys_version_valid(const cJSON *json);
static cJSON *read_history(bool *absent) {
    cJSON *json = read_json(WIFI_PATH, absent);
    if (json && history_valid(json)) return json;
    release_json(json);
    bool backup_absent;
    json = read_json(WIFI_PATH ".bak", &backup_absent);
    *absent = *absent && backup_absent;
    if (json && history_valid(json)) {
        ESP_LOGW("SD_CONFIG", "Using Wi-Fi recovery backup");
        return json;
    }
    release_json(json);
    if (!*absent) ESP_LOGW("SD_CONFIG", "No valid Wi-Fi history; existing SD files retained");
    return NULL;
}
static bool field(const cJSON *obj, const char *name, char *out, size_t capacity) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= capacity) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1); return true;
}
static bool profile(const cJSON *obj, network_settings_t *s) {
    memset(s, 0, sizeof(*s)); s->version = 1;
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(obj, "static");
    if (mode && !cJSON_IsBool(mode)) return false;
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
    if (version && (!cJSON_IsNumber(version) || version->valuedouble != 1)) return false;
    if (!networks) { network_settings_t single; bool valid = profile(json, &single); mbedtls_platform_zeroize(&single,sizeof(single)); return valid; }
    if (!cJSON_IsArray(networks)) return false;
    network_settings_t s;
    bool valid = true;
    const cJSON *item;
    cJSON_ArrayForEach(item, networks) if (!profile(item, &s)) { valid = false; break; }
    mbedtls_platform_zeroize(&s, sizeof(s)); return valid;
}
static bool write_json(const char *path, cJSON *json) {
    char temporary[128], backup[128];
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    snprintf(backup,sizeof(backup),"%s.bak",path);
    char *data = cJSON_PrintUnformatted(json);
    if (!data) return false;
    size_t length = strlen(data);
    bool ok = length <= JSON_LIMIT;
    FILE *file = ok ? fopen(temporary, "wb") : NULL;
    if (!file) ok = false;
    else {
        ok = fwrite(data, 1, length, file) == length && !fflush(file) && !fsync(fileno(file));
        if (fclose(file)) ok = false;
    }
    mbedtls_platform_zeroize(data, length); cJSON_free(data);
    if (!ok) { unlink(temporary); return false; }
    /* Keep the last committed version through interruption of FAT renames. */
    struct stat st;
    if (!stat(path, &st)) {
        bool absent;
        cJSON *previous = read_json(path, &absent);
        bool valid = !strcmp(path,WIFI_PATH) ? history_valid(previous) : keys_version_valid(previous);
        release_json(previous);
        if (valid) {
            if (unlink(backup) && errno != ENOENT) return false;
            if (rename(path, backup)) return false;
        } else if (unlink(path)) return false; /* Keep the good recovery backup. */
    }
    if (rename(temporary, path)) {
        rename(backup, path); return false;
    }
    return true;
}
esp_err_t m3_sd_wifi_store(const network_settings_t *s) {
    if (!network_settings_valid(s)) return ESP_ERR_INVALID_ARG;
    if (!sd_media_ready()) return ESP_ERR_INVALID_STATE;
    if (!sd_media_admin_lock()) return ESP_ERR_TIMEOUT;
    cJSON *json=cJSON_CreateObject();
    bool ok=json && cJSON_AddNumberToObject(json,"version",1) &&
        cJSON_AddStringToObject(json,"ssid",s->ssid) && cJSON_AddStringToObject(json,"password",s->pass) &&
        cJSON_AddBoolToObject(json,"static",s->use_static);
    if (ok && s->use_static) ok=cJSON_AddStringToObject(json,"ip",s->ip) && cJSON_AddStringToObject(json,"gateway",s->gateway) &&
        cJSON_AddStringToObject(json,"netmask",s->netmask) && cJSON_AddStringToObject(json,"dns",s->dns);
    ok=ok && (mkdir(PRIVATE_ROOT,0700)==0 || errno==EEXIST) && write_json(WIFI_PATH,json);
    release_json(json);
    if (ok) { portENTER_CRITICAL(&wifi_lock);wifi_settings=*s;wifi_loaded=true;portEXIT_CRITICAL(&wifi_lock); }
    sd_media_admin_unlock();
    if (ok) ESP_LOGI("SD_CONFIG","Wi-Fi JSON committed");
    else ESP_LOGW("SD_CONFIG","Wi-Fi JSON save failed; settings not reported as saved");
    return ok ? ESP_OK : ESP_FAIL;
}
void m3_sd_wifi_save(const network_settings_t *s) { (void)m3_sd_wifi_store(s); }
bool m3_sd_wifi_load(network_settings_t *out) {
    memset(out,0,sizeof(*out));out->version=1;
    portENTER_CRITICAL(&wifi_lock);
    bool found=wifi_loaded;
    if(found) *out=wifi_settings;
    portEXIT_CRITICAL(&wifi_lock);
    return found;
}
bool m3_sd_wifi_find(const char *ssid, network_settings_t *out) {
    if (!ssid || !out || !sd_media_ready() || !sd_media_admin_lock()) return false;
    bool absent, found = false;
    cJSON *json = read_history(&absent);
    const cJSON *last = cJSON_GetObjectItemCaseSensitive(json, "last_ssid");
    const char *wanted = *ssid ? ssid : cJSON_IsString(last) ? last->valuestring : "";
    if (history_valid(json) && !cJSON_GetObjectItemCaseSensitive(json,"networks")) {
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(json,"ssid");
        if (!*ssid || !strcmp(name->valuestring,ssid)) found=profile(json,out);
    } else if (history_valid(json)) {
        const cJSON *item, *fallback = NULL;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(json, "networks")) {
            fallback = item;
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "ssid");
            if (!strcmp(name->valuestring, wanted)) { found = profile(item, out); break; }
        }
        /* PC-created/recovered histories may lack a usable last_ssid. Only
         * boot selection may fall back; an explicit SSID must match exactly. */
        if (!found && !*ssid && fallback) {
            found = profile(fallback, out);
            ESP_LOGW("SD_CONFIG", "Last Wi-Fi selection missing; using final saved profile");
        }
    }
    release_json(json); sd_media_admin_unlock(); return found;
}
static bool key_field(const cJSON *json, const char *name, char *out) {
    char candidate[KEY_LIMIT] = {0};
    bool ok = field(json, name, candidate, sizeof(candidate));
    size_t length = strlen(candidate);
    ok = ok && (length == 0 || length >= 16);
    for (size_t i = 0; i < length; ++i) if ((unsigned char)candidate[i] <= 32 || (unsigned char)candidate[i] >= 127) ok = false;
    if (ok) memcpy(out, candidate, sizeof(candidate));
    mbedtls_platform_zeroize(candidate, sizeof(candidate)); return ok;
}
/* Web requests own copies; boot clients keep immutable snapshots until reboot. */
static bool keys_version_valid(const cJSON *json) {
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(json,"version");
    return cJSON_IsObject(json) && (!version || (cJSON_IsNumber(version) && version->valuedouble==1));
}
static cJSON *read_keys(bool *absent) {
    cJSON *json=read_json(KEYS_PATH,absent);
    if (keys_version_valid(json)) return json;
    release_json(json);
    bool backup_absent;
    json=read_json(KEYS_PATH ".bak",&backup_absent);
    *absent=*absent && backup_absent;
    if (keys_version_valid(json)) { ESP_LOGW("SD_CONFIG","Using API JSON recovery backup");return json; }
    release_json(json);return NULL;
}
bool m3_sd_key_copy(const char *name, char out[256]) {
    memset(out,0,KEY_LIMIT);
    if (!name || !sd_media_ready() || !sd_media_admin_lock()) return false;
    bool absent; cJSON *json=read_keys(&absent);
    bool ok=json && key_field(json,name,out) && out[0];
    release_json(json);sd_media_admin_unlock();return ok;
}
esp_err_t m3_sd_gemini_save(const char *key) {
    if (!key || strlen(key)>=KEY_LIMIT) return ESP_ERR_INVALID_ARG;
    if (!sd_media_ready()) return ESP_ERR_INVALID_STATE;
    if (!sd_media_admin_lock()) return ESP_ERR_TIMEOUT;
    bool absent; cJSON *json=read_keys(&absent);
    if (!json && absent) json=cJSON_CreateObject();
    bool ok=json!=NULL;
    if (ok) {
        cJSON_DeleteItemFromObjectCaseSensitive(json,"gemini");
        ok=cJSON_AddStringToObject(json,"gemini",key)!=NULL;
        char check[KEY_LIMIT];ok=ok && key_field(json,"gemini",check);
        mbedtls_platform_zeroize(check,sizeof(check));
        ok=ok && (mkdir(PRIVATE_ROOT,0700)==0 || errno==EEXIST) && write_json(KEYS_PATH,json);
    }
    release_json(json);sd_media_admin_unlock();
    return ok ? ESP_OK : ESP_FAIL;
}
void m3_sd_settings_init(void) {
    snprintf(azure_region,sizeof(azure_region),"koreacentral");
    if (!sd_media_ready()) {
        ESP_LOGW("SD_CONFIG","SD unavailable; API keys and Wi-Fi JSON not loaded");
        atomic_store(&keys_loaded,true);return;
    }
    if (sd_media_admin_lock()) {
        bool absent; cJSON *json=read_keys(&absent);
        const cJSON *version=cJSON_GetObjectItemCaseSensitive(json,"version");
        if (json && (!version || (cJSON_IsNumber(version) && version->valuedouble==1))) {
            const char *names[]={"gemini","google_tts","openai","azure_speech"};
            char *destinations[]={gemini,tts,openai,azure};
            for (unsigned i=0;i<4;++i) {
                if (cJSON_GetObjectItemCaseSensitive(json,names[i]) && !key_field(json,names[i],destinations[i]))
                    ESP_LOGW("SD_CONFIG","Invalid API JSON field: %s",names[i]);
            }
            const cJSON *region=cJSON_GetObjectItemCaseSensitive(json,"azure_region");
            if (cJSON_IsString(region)) {
                size_t n=strlen(region->valuestring);bool valid=n>0 && n<sizeof(azure_region);
                for(size_t i=0;i<n;++i) if(!((region->valuestring[i]>='a' && region->valuestring[i]<='z') ||
                    (region->valuestring[i]>='0' && region->valuestring[i]<='9'))) valid=false;
                if(valid) snprintf(azure_region,sizeof(azure_region),"%s",region->valuestring);
            }
            ESP_LOGI("SD_CONFIG","API JSON loaded: gemini=%d google_tts=%d openai=%d azure=%d",
                gemini[0]!=0,tts[0]!=0,openai[0]!=0,azure[0]!=0);
        } else ESP_LOGW("SD_CONFIG","API JSON missing or invalid; no embedded key fallback");
        release_json(json);sd_media_admin_unlock();
    } else ESP_LOGW("SD_CONFIG","API JSON load busy; boot key snapshot unavailable");
    atomic_store(&keys_loaded,true);
    network_settings_t settings;
    bool wifi=m3_sd_wifi_find("",&settings);
    portENTER_CRITICAL(&wifi_lock);
    if(wifi) wifi_settings=settings;
    wifi_loaded=wifi;
    portEXIT_CRITICAL(&wifi_lock);
    ESP_LOGI("SD_CONFIG","Wi-Fi JSON boot profile: %s",wifi ? "ready" : "missing/invalid");
    mbedtls_platform_zeroize(&settings,sizeof(settings));
}
