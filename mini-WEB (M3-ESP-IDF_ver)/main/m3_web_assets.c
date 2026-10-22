#include "m3_web_assets.h"
#include "m3_utf8.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#ifndef CONFIG_M3_WEB_ASSET_LIMIT_KB
#define CONFIG_M3_WEB_ASSET_LIMIT_KB 8192
#endif
#ifndef CONFIG_M3_WEB_ASSET_MAX_FILES
#define CONFIG_M3_WEB_ASSET_MAX_FILES 512
#endif
#define WEB_ASSET_ROOT SD_MEDIA_ROOT "/Main WebFlie"
typedef struct asset_entry { struct asset_entry *next; char path[192]; m3_web_asset_t asset; } asset_entry_t;
static asset_entry_t *assets;
static atomic_bool loaded;
static unsigned asset_count;
static size_t asset_bytes;

bool m3_web_assets_path_valid(const char *path) {
    if (!path || path[0] != '/' || !path[1] || strlen(path) >= 192 || !m3_utf8_valid(path)) return false;
    for (const char *p = path; *p; ++p)
        if ((unsigned char)*p < 32 || *p == 127 || strchr("\\:%?#", *p) ||
            (*p == '/' && (!p[1] || p[1] == '.' || p[1] == '/'))) return false;
    return true;
}
static const char *asset_mime(const char *path) {
    static const char *const types[][2] = {
        {".html","text/html; charset=utf-8"},{".htm","text/html; charset=utf-8"},
        {".css","text/css; charset=utf-8"},{".js","application/javascript; charset=utf-8"},
        {".mjs","application/javascript; charset=utf-8"},{".json","application/json"},
        {".webmanifest","application/manifest+json"},{".map","application/json"},{".txt","text/plain; charset=utf-8"},
        {".xml","application/xml"},{".svg","image/svg+xml"},{".png","image/png"},{".jpg","image/jpeg"},
        {".jpeg","image/jpeg"},{".gif","image/gif"},{".webp","image/webp"},{".bmp","image/bmp"},
        {".ico","image/x-icon"},{".woff","font/woff"},{".woff2","font/woff2"},{".ttf","font/ttf"},
        {".otf","font/otf"},{".wasm","application/wasm"},{".mp4","video/mp4"},{".webm","video/webm"},
        {".mp3","audio/mpeg"},{".wav","audio/wav"},{".m4a","audio/mp4"},{".pdf","application/pdf"}
    };
    const char *ext = strrchr(path, '.');
    for (unsigned i = 0; ext && i < sizeof(types) / sizeof(types[0]); ++i) if (!strcasecmp(ext, types[i][0])) return types[i][1];
    return NULL;
}
bool m3_web_assets_ready(void) { return atomic_load(&loaded); }
unsigned m3_web_assets_count(void) { return m3_web_assets_ready() ? asset_count : 0; }
size_t m3_web_assets_bytes(void) { return m3_web_assets_ready() ? asset_bytes : 0; }
const m3_web_asset_t *m3_web_assets_find(const char *path) {
    if (!m3_web_assets_ready() || !path) return NULL;
    if (!strcmp(path, "/")) path = "/index.html";
    if (!m3_web_assets_path_valid(path)) return NULL;
    for (const asset_entry_t *entry = assets; entry; entry = entry->next) if (!strcasecmp(path, entry->path)) return &entry->asset;
    return NULL;
}
static void discard(asset_entry_t *entry) {
    while (entry) { asset_entry_t *next = entry->next; free((void *)entry->asset.data); free(entry); entry = next; }
}
/* Load every supported file or discard the entire attempt. Requests never observe a partial site. */
esp_err_t m3_web_assets_load(void) {
    if (m3_web_assets_ready()) return ESP_OK;
    if (!sd_media_ready()) return ESP_ERR_NOT_FOUND;
    if (!sd_media_admin_lock()) return ESP_ERR_INVALID_STATE;
    const size_t budget = (size_t)CONFIG_M3_WEB_ASSET_LIMIT_KB * 1024;
    asset_entry_t *staged = NULL;
    DIR **dirs = heap_caps_calloc(128, sizeof(*dirs), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned *lengths = heap_caps_calloc(128, sizeof(*lengths), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t result = ESP_ERR_NO_MEM; size_t bytes = 0, allocated = 0; unsigned count = 0, depth = 0;
    if (!dirs || !lengths) goto done;
    dirs[0] = opendir(WEB_ASSET_ROOT);
    if (!dirs[0]) { result = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL; goto done; }
    result = ESP_OK;
    char relative[192] = "";
    for (;;) {
        errno = 0; struct dirent *file = readdir(dirs[depth]);
        if (!file) {
            if (errno) { result = ESP_FAIL; break; }
            closedir(dirs[depth]); dirs[depth] = NULL;
            if (!depth) break;
            relative[lengths[--depth]] = 0; continue;
        }
        if (file->d_name[0] == '.') continue;
        char key[192], path[sizeof(WEB_ASSET_ROOT) + 192];
        int n = snprintf(key, sizeof(key), "%s/%s", relative, file->d_name);
        if (n <= 0 || (size_t)n >= sizeof(key) || !m3_web_assets_path_valid(key)) { result = ESP_ERR_INVALID_SIZE; break; }
        snprintf(path, sizeof(path), "%s%s", WEB_ASSET_ROOT, key);
        struct stat st;
        if (stat(path, &st)) { result = ESP_FAIL; break; }
        if (S_ISDIR(st.st_mode)) {
            if (depth + 1 >= 128) { result = ESP_ERR_INVALID_SIZE; break; }
            DIR *dir = opendir(path); if (!dir) { result = ESP_FAIL; break; }
            dirs[++depth] = dir; lengths[depth] = n; memcpy(relative, key, n + 1); continue;
        }
        const char *mime = asset_mime(key);
        if (!S_ISREG(st.st_mode) || !mime) continue;
        if (st.st_size < 0 || count >= CONFIG_M3_WEB_ASSET_MAX_FILES || (uint64_t)st.st_size + sizeof(asset_entry_t) + 1 > budget - allocated) { result = ESP_ERR_INVALID_SIZE; break; }
        size_t length = st.st_size;
        asset_entry_t *entry = heap_caps_calloc(1, sizeof(*entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t *data = heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        FILE *input = data && entry ? fopen(path, "rb") : NULL;
        if (!input) { bool memory = data && entry; free(entry); free(data); result = memory ? ESP_FAIL : ESP_ERR_NO_MEM; break; }
        size_t used = 0;
        while (used < length) {
            size_t chunk = length - used; if (chunk > 32768) chunk = 32768;
            size_t got = fread(data + used, 1, chunk, input);
            if (got != chunk) break;
            used += got; vTaskDelay(pdMS_TO_TICKS(1));
        }
        bool ok = used == length && !ferror(input); fclose(input);
        if (!ok) { free(entry); free(data); result = ESP_FAIL; break; }
        data[length] = 0; memcpy(entry->path, key, n + 1);
        entry->asset = (m3_web_asset_t){.data = data, .bytes = length, .mime = mime}; entry->next = staged; staged = entry;
        bytes += length; allocated += length + 1 + sizeof(*entry); ++count;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (result == ESP_OK && !count) result = ESP_ERR_NOT_FOUND;
done:
    if (dirs) for (unsigned i = 0; i < 128; ++i) if (dirs[i]) closedir(dirs[i]);
    free(dirs); free(lengths); sd_media_admin_unlock();
    if (result == ESP_OK) {
        assets = staged; asset_count = count; asset_bytes = bytes; atomic_store(&loaded, true);
        ESP_LOGI("WEB_PSRAM", "Main WebFlie: %u files, %u bytes in PSRAM", count, (unsigned)bytes);
    } else {
        discard(staged);
        if (result != ESP_ERR_NOT_FOUND) ESP_LOGW("WEB_PSRAM", "Main WebFlie load failed (%s); using built-in/Flash pages", esp_err_to_name(result));
    }
    return result;
}
bool m3_web_assets_slice(size_t bytes, const char *range, size_t *offset, size_t *length) {
    if (!offset || !length) return false;
    *offset = 0; *length = bytes;
    if (!range || !*range) return true;
    if (!bytes || strncmp(range, "bytes=", 6) || strchr(range, ',')) return false;
    const char *p = range + 6; char *tail; unsigned long first, last = bytes - 1;
    errno = 0;
    if (*p == '-') {
        ++p; if (*p < '0' || *p > '9') return false;
        unsigned long suffix = strtoul(p, &tail, 10);
        if (*tail || errno == ERANGE || !suffix) return false;
        first = suffix >= bytes ? 0 : bytes - suffix;
    } else {
        if (*p < '0' || *p > '9') return false;
        first = strtoul(p, &tail, 10);
        if (*tail != '-' || errno == ERANGE) return false;
        p = tail + 1;
        if (*p) {
            if (*p < '0' || *p > '9') return false;
            last = strtoul(p, &tail, 10); if (*tail || errno == ERANGE) return false;
        }
    }
    if (first >= bytes || last < first) return false;
    if (last >= bytes) last = bytes - 1;
    *offset = first; *length = last - first + 1; return true;
}
