#include "m3_reader.h"
#include "m3_memory.h"
#include "m3_utf8.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct { unsigned generation; char path[256]; } request_t;
typedef struct { uint32_t version, offset, bytes; char path[256]; } bookmark_t;
static QueueHandle_t requests;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_uint epoch;
static M3_PSRAM_BSS m3_reader_state_t state;
static M3_PSRAM_BSS bookmark_t bookmark;
static m3_reader_settings_t settings = {1, 22, 1, 0, 8, 24, 0, 0};
static char *document;
static bool dirty;
static unsigned settings_revision;

bool m3_reader_supported(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    return ext && (!strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md") || !strcasecmp(ext, ".log"));
}
bool m3_reader_path_valid(const char *path) {
    if (!path || path[0] != '/' || strlen(path) >= 256 || !m3_utf8_valid(path) || !m3_reader_supported(path)) return false;
    for (const char *p = path; *p; ++p) {
        if ((unsigned char)*p < 32 || *p == 127 || *p == '\\' || *p == ':') return false;
        if (*p == '/' && (!p[1] || p[1] == '/' || p[1] == '.')) return false;
    }
    return true;
}
bool m3_reader_settings_valid(const m3_reader_settings_t *s) {
    return s && s->version == 1 && s->font_px >= 16 && s->font_px <= 36 && s->theme <= 3 &&
        s->letter_space <= 6 && s->line_space <= 24 && s->margin >= 12 && s->margin <= 40 &&
        s->background <= 2 && s->reserved == 0;
}
bool m3_reader_normalize(char *text, size_t bytes, size_t *length) {
    if (!text || !bytes || memchr(text, 0, bytes)) return false;
    text[bytes] = 0;
    if (!m3_utf8_valid(text)) return false;
    size_t start = bytes >= 3 && (unsigned char)text[0] == 0xef &&
        (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf ? 3 : 0;
    size_t used = 0;
    for (size_t i = start; i < bytes; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r') { if (i + 1 < bytes && text[i + 1] == '\n') ++i; text[used++] = '\n'; }
        else if (c == '\t') text[used++] = ' ';
        else if (c < 32 && c != '\n') return false;
        else if (c == 127) return false;
        else text[used++] = (char)c;
    }
    text[used] = 0; *length = used; return used != 0;
}
void m3_reader_settings(m3_reader_settings_t *out) {
    portENTER_CRITICAL(&lock); *out = settings; portEXIT_CRITICAL(&lock);
}
bool m3_reader_configure(const m3_reader_settings_t *next) {
    if (!m3_reader_settings_valid(next)) return false;
    portENTER_CRITICAL(&lock);
    if (memcmp(&settings, next, sizeof(settings))) { settings = *next; dirty = true; ++settings_revision; }
    portEXIT_CRITICAL(&lock); return true;
}
void m3_reader_state(m3_reader_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock);
}
size_t m3_reader_copy(unsigned generation, unsigned offset, char *out, size_t capacity) {
    if (!out || capacity < 2 || capacity > M3_READER_WINDOW) return 0;
    size_t count = 0;
    portENTER_CRITICAL(&lock);
    if (state.status == M3_READER_READY && state.generation == generation && document && offset < state.bytes &&
        ((unsigned char)document[offset] & 0xc0) != 0x80) {
        count = state.bytes - offset;
        if (count >= capacity) count = capacity - 1;
        while (count && ((unsigned char)document[offset + count] & 0xc0) == 0x80) --count;
        memcpy(out, document + offset, count);
    }
    out[count] = 0;
    portEXIT_CRITICAL(&lock); return count;
}
void m3_reader_progress(unsigned generation, unsigned offset) {
    portENTER_CRITICAL(&lock);
    if (state.status == M3_READER_READY && state.generation == generation && offset < state.bytes && document &&
        ((unsigned char)document[offset] & 0xc0) != 0x80 && bookmark.offset != offset) {
        bookmark.offset = offset; dirty = true; ++settings_revision;
    }
    portEXIT_CRITICAL(&lock);
}
unsigned m3_reader_align(unsigned generation, unsigned offset) {
    portENTER_CRITICAL(&lock);
    if (!document || state.status != M3_READER_READY || generation != state.generation || !state.bytes) offset = 0;
    else {
        if (offset >= state.bytes) offset = state.bytes - 1;
        while (offset && ((unsigned char)document[offset] & 0xc0) == 0x80) --offset;
    }
    portEXIT_CRITICAL(&lock); return offset;
}
static bool request(const char *path) {
    if (!requests) return false;
    request_t r = {.generation = atomic_fetch_add(&epoch, 1) + 1};
    if (path) snprintf(r.path, sizeof(r.path), "%s", path);
    portENTER_CRITICAL(&lock);
    state.status = path ? M3_READER_LOADING : M3_READER_IDLE; state.generation = r.generation;
    state.bytes = state.resume = 0; ++state.revision;
    snprintf(state.path, sizeof(state.path), "%s", r.path);
    portEXIT_CRITICAL(&lock);
    return xQueueOverwrite(requests, &r) == pdPASS;
}
bool m3_reader_open(const char *path) { return m3_reader_path_valid(path) && request(path); }
bool m3_reader_resume(void) {
    char path[256];
    portENTER_CRITICAL(&lock); memcpy(path, bookmark.path, sizeof(path)); portEXIT_CRITICAL(&lock);
    return m3_reader_open(path);
}
void m3_reader_close(void) { (void)request(NULL); }
static void publish(const request_t *r, char *text, size_t bytes, m3_reader_status_t status) {
    char *old = NULL; bool accepted = false;
    portENTER_CRITICAL(&lock);
    if (r->generation == atomic_load(&epoch)) {
        old = document; document = text; accepted = true;
        state.status = status; state.bytes = bytes; state.resume = 0; ++state.revision;
        if (status == M3_READER_READY) {
            if (bookmark.version == 1 && bookmark.bytes == bytes && !strcmp(bookmark.path, r->path) && bookmark.offset < bytes) {
                state.resume = bookmark.offset;
                while (state.resume && ((unsigned char)text[state.resume] & 0xc0) == 0x80) --state.resume;
            }
            bookmark = (bookmark_t){.version = 1, .offset = state.resume, .bytes = bytes};
            snprintf(bookmark.path, sizeof(bookmark.path), "%s", r->path);
            dirty = true; ++settings_revision;
        }
    }
    portEXIT_CRITICAL(&lock);
    free(old); if (!accepted) free(text);
}
static void load(const request_t *r) {
    if (r->generation != atomic_load(&epoch)) return;
    if (!r->path[0]) { publish(r, NULL, 0, M3_READER_IDLE); return; }
    /* LOADING rejects UI copies; retire the old document before allocating a
     * replacement so the book heap never peaks at two complete books. */
    publish(r, NULL, 0, M3_READER_LOADING);
    if (r->generation != atomic_load(&epoch)) return;
    if (!sd_media_ready()) { publish(r, NULL, 0, M3_READER_NO_SD); return; }
    if (!sd_media_admin_lock()) { publish(r, NULL, 0, M3_READER_BUSY); return; }
    char path[272]; snprintf(path, sizeof(path), SD_MEDIA_ROOT "%s", r->path);
    struct stat st; FILE *file = NULL; char *text = NULL; size_t bytes = 0;
    m3_reader_status_t result = M3_READER_IO;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) goto done;
    if (st.st_size > M3_READER_MAX_BYTES) { result = M3_READER_TOO_LARGE; goto done; }
    text = heap_caps_malloc((size_t)st.st_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!text) { result = M3_READER_MEMORY; goto done; }
    file = fopen(path, "rb"); if (!file) goto done;
    int64_t deadline = esp_timer_get_time() + 15000000;
    while (bytes < (size_t)st.st_size) {
        if (r->generation != atomic_load(&epoch) || esp_timer_get_time() >= deadline) goto done;
        size_t count = (size_t)st.st_size - bytes; if (count > 32768) count = 32768;
        if (fread(text + bytes, 1, count, file) != count) goto done;
        bytes += count; vTaskDelay(1);
    }
    if (fgetc(file) != EOF || ferror(file)) goto done;
    if (r->generation != atomic_load(&epoch)) goto done;
    if (!m3_reader_normalize(text, bytes, &bytes)) { result = M3_READER_ENCODING; goto done; }
    result = M3_READER_READY;
done:
    if (file) fclose(file);
    sd_media_admin_unlock();
    if (result != M3_READER_READY) { free(text); text = NULL; bytes = 0; }
    publish(r, text, bytes, result);
}
static void save(void) {
    static M3_PSRAM_BSS bookmark_t copy;
    m3_reader_settings_t p; bool pending; unsigned captured;
    portENTER_CRITICAL(&lock); p = settings; copy = bookmark; pending = dirty; captured = settings_revision; portEXIT_CRITICAL(&lock);
    if (!pending) return;
    nvs_handle_t nvs; esp_err_t err = nvs_open("m3_reader", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "settings", &p, sizeof(p));
        if (err == ESP_OK) err = nvs_set_blob(nvs, "bookmark", &copy, sizeof(copy));
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    portENTER_CRITICAL(&lock);
    state.save_failed = err != ESP_OK;
    if (err == ESP_OK && captured == settings_revision) dirty = false;
    portEXIT_CRITICAL(&lock);
}
static void worker(void *arg) {
    (void)arg; request_t r;
    for (;;) {
        if (xQueueReceive(requests, &r, pdMS_TO_TICKS(2000)) == pdTRUE) load(&r);
        save();
    }
}
esp_err_t m3_reader_start(void) {
    if (requests) return ESP_OK;
    nvs_handle_t nvs;
    if (nvs_open("m3_reader", NVS_READONLY, &nvs) == ESP_OK) {
        m3_reader_settings_t p; size_t size = sizeof(p);
        if (nvs_get_blob(nvs, "settings", &p, &size) == ESP_OK && size == sizeof(p) && m3_reader_settings_valid(&p)) settings = p;
        size = sizeof(bookmark);
        if (nvs_get_blob(nvs, "bookmark", &bookmark, &size) != ESP_OK || size != sizeof(bookmark) ||
            bookmark.version != 1 || !memchr(bookmark.path, 0, sizeof(bookmark.path)) || !m3_reader_path_valid(bookmark.path))
            memset(&bookmark, 0, sizeof(bookmark));
        nvs_close(nvs);
    }
    requests = xQueueCreate(1, sizeof(request_t));
    if (!requests) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(worker, "m3_reader", 4096, NULL, 2, NULL, 0) != pdPASS) {
        vQueueDelete(requests); requests = NULL; return ESP_ERR_NO_MEM;
    }
    state.available = true; return ESP_OK;
}
