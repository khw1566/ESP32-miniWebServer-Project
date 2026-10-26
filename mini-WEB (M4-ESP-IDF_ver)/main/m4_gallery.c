#include "m4_gallery.h"
#include "m4_photos.h"
#include "m4_audio.h"
#include "m4_mp4.h"
#include "m4_memory.h"
#include "m4_utf8.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/jpeg_decode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

enum { INPUT_LIMIT = 4 * 1024 * 1024, JPEG_LIMIT = 256 * 1024 };
typedef struct { char path[256]; unsigned generation, page; bool library; } request_t;
static QueueHandle_t requests;
static atomic_bool active, paused, library_active;
static atomic_bool mp4_selected;
static atomic_uint generation;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static m4_gallery_state_t state;
/* FREE=0, WRITING=1, READY=2, SHOWN=3. Bounded PSRAM buffers stay pinned for LVGL. */
static struct { uint8_t *data; unsigned owner, generation; } slots[2];
static void status(m4_gallery_status_t value) {
    portENTER_CRITICAL(&lock); state.status = value; ++state.revision; portEXIT_CRITICAL(&lock);
}
bool m4_gallery_supported(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    return ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".bmp") ||
        !strcasecmp(ext, ".png") || !strcasecmp(ext, ".mjpg") || !strcasecmp(ext, ".mjpeg") || !strcasecmp(ext, ".avi") || !strcasecmp(ext, ".mp4"));
}
bool m4_gallery_video(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    static const char *const formats[] = {".mjpg", ".mjpeg", ".avi", ".mp4", ".mov", ".mkv", ".webm", ".m4v", ".3gp", ".mpeg", ".mpg", ".wmv", ".flv", ".ts", ".m2ts", ".mts"};
    for (unsigned i = 0; ext && i < sizeof(formats) / sizeof(formats[0]); ++i) if (!strcasecmp(ext, formats[i])) return true;
    return false;
}
bool m4_gallery_media(const char *name) {
    if (m4_gallery_supported(name) || m4_gallery_video(name)) return true;
    const char *ext = name ? strrchr(name, '.') : NULL;
    static const char *const formats[] = {".gif", ".webp", ".heic", ".heif", ".avif", ".tif", ".tiff"};
    for (unsigned i = 0; ext && i < sizeof(formats) / sizeof(formats[0]); ++i) if (!strcasecmp(ext, formats[i])) return true;
    return false;
}
bool m4_gallery_active(void) { return atomic_load(&active); }
void m4_gallery_close(void) { atomic_store(&active, false); atomic_store(&library_active, false); atomic_fetch_add(&generation, 1); }
void m4_gallery_pause(void) {
    atomic_store(&paused, !atomic_load(&paused));
    portENTER_CRITICAL(&lock); ++state.revision; portEXIT_CRITICAL(&lock);
}
bool m4_gallery_open(const char *relative) {
    if (!requests || !m4_gallery_path_valid(relative) || !m4_gallery_supported(relative)) return false;
    request_t request = {.generation = atomic_fetch_add(&generation, 1) + 1};
    snprintf(request.path, sizeof(request.path), "%s", relative);
    const char *ext = strrchr(relative, '.');
    atomic_store(&mp4_selected, ext && !strcasecmp(ext, ".mp4"));
    atomic_store(&library_active, false);
    atomic_store(&paused, false); atomic_store(&active, true);
    portENTER_CRITICAL(&lock);
    state.status = M4_GALLERY_LOADING; state.mp4 = atomic_load(&mp4_selected); state.video = m4_gallery_video(relative);
    state.frames = state.position_ms = state.duration_ms = 0; ++state.revision;
    portEXIT_CRITICAL(&lock);
    return xQueueOverwrite(requests, &request) == pdPASS;
}
void m4_gallery_state(m4_gallery_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock); out->paused = atomic_load(&paused);
}
const uint8_t *m4_gallery_frame(void) {
    const uint8_t *data = NULL;
    portENTER_CRITICAL(&lock);
    unsigned job = atomic_load(&generation);
    for (unsigned i = 0; i < 2; ++i) if (slots[i].owner == 2 && slots[i].generation != job) slots[i].owner = 0;
    for (unsigned i = 0; i < 2; ++i) if (slots[i].owner == 2) {
        for (unsigned j = 0; j < 2; ++j) if (slots[j].owner == 3) slots[j].owner = 0;
        slots[i].owner = 3; break;
    }
    for (unsigned i = 0; i < 2; ++i) if (slots[i].owner == 3 && slots[i].generation == job) data = slots[i].data;
    portEXIT_CRITICAL(&lock); return data;
}
static int claim(void) {
    int slot = -1; portENTER_CRITICAL(&lock);
    for (unsigned i = 0; i < 2; ++i) if (slots[i].owner == 0 || slots[i].owner == 2) { slots[i].owner = 1; slot = i; break; }
    portEXIT_CRITICAL(&lock); return slot;
}
static bool current(unsigned job) { return atomic_load(&active) && job == atomic_load(&generation); }
void m4_gallery_audio_cancel(void) {
    if (!atomic_load(&mp4_selected)) return;
    m4_gallery_close(); status(M4_GALLERY_STOPPED);
}
static bool mp4_paused(void) { return atomic_load(&paused); }
static uint8_t *mp4_acquire(unsigned job, int *slot) {
    *slot = -1;
    if (!current(job)) return NULL;
    int selected = claim(); if (selected < 0) return NULL;
    if (!slots[selected].data) slots[selected].data = heap_caps_malloc(M4_PHOTO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!slots[selected].data) {
        portENTER_CRITICAL(&lock); slots[selected].owner = 0; portEXIT_CRITICAL(&lock);
        *slot = -2; return NULL;
    }
    *slot = selected; return slots[selected].data;
}
static void mp4_publish(unsigned job, int slot, uint32_t position, uint32_t duration) {
    if (slot < 0 || slot >= 2) return;
    portENTER_CRITICAL(&lock);
    if (current(job) && position != UINT32_MAX) {
        slots[slot].owner = 2; slots[slot].generation = job; state.status = M4_GALLERY_READY; state.mp4 = state.video = true;
        state.position_ms = position; state.duration_ms = duration; ++state.frames; ++state.revision;
    } else slots[slot].owner = 0;
    portEXIT_CRITICAL(&lock);
}
void m4_gallery_mp4_run(const char *relative, unsigned job, const m4_audio_output_t *audio) {
    if (!current(job)) return;
    const m4_mp4_view_t view = {.current = current, .paused = mp4_paused, .acquire = mp4_acquire, .publish = mp4_publish};
    m4_mp4_result_t result = m4_mp4_play(relative, job, audio, &view);
    if (!current(job)) return;
    static const m4_gallery_status_t statuses[] = {M4_GALLERY_ENDED, M4_GALLERY_STOPPED, M4_GALLERY_MP4_FORMAT,
        M4_GALLERY_STORAGE, M4_GALLERY_MEMORY, M4_GALLERY_DECODE_ERROR, M4_GALLERY_AUDIO_ERROR};
    status(statuses[result]); atomic_store(&active, false);
}
/* JPEG SOI/EOI framing also extracts MJPEG from AVI; other AVI codecs are rejected. */
static size_t next_jpeg(FILE *file, uint8_t *buffer, unsigned job, bool (*valid)(unsigned)) {
    unsigned previous = 0, searched = 0; size_t length = 0;
    while (searched++ < 2 * 1024 * 1024 && valid(job)) {
        int byte = fgetc(file); if (byte == EOF) return 0;
        if (!length) { if (previous == 0xff && byte == 0xd8) { buffer[0] = 0xff; buffer[1] = 0xd8; length = 2; } }
        else { if (length >= JPEG_LIMIT) return 0; buffer[length++] = byte; if (previous == 0xff && byte == 0xd9) return length; }
        previous = byte;
    }
    return 0;
}
#include "m4_gallery_index.inc"
static void worker(void *unused) {
    (void)unused;
    for (;;) {
        request_t request; if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
        if (request.library) { gallery_library_run(request.page, request.generation); continue; }
        if (!current(request.generation)) continue;
        status(M4_GALLERY_LOADING);
        char path[272]; snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, request.path);
        const char *ext = strrchr(path, '.');
        bool mp4 = !strcasecmp(ext, ".mp4");
        if (mp4) {
            portENTER_CRITICAL(&lock); state.mp4 = state.video = true; state.frames = 0; state.position_ms = state.duration_ms = 0; portEXIT_CRITICAL(&lock);
            /* The previous MP4 observes its invalidated generation and joins
             * its video task before the sole audio worker accepts this file. */
            bool accepted = false;
            for (unsigned retry = 0; retry < 20 && current(request.generation); ++retry) {
                if (m4_audio_mp4(request.path, request.generation)) { accepted = true; break; }
                vTaskDelay(pdMS_TO_TICKS(25));
            }
            if (!accepted && current(request.generation)) {
                status(M4_GALLERY_AUDIO_BUSY); atomic_store(&active, false);
            }
            continue;
        }
        bool video = !strcasecmp(ext, ".mjpg") || !strcasecmp(ext, ".mjpeg") || !strcasecmp(ext, ".avi");
        portENTER_CRITICAL(&lock); state.mp4 = false; state.video = video; state.frames = 0; portEXIT_CRITICAL(&lock);
        if (!sd_media_admin_lock()) { status(M4_GALLERY_STORAGE); continue; }
        FILE *file = fopen(path, "rb"); long length = 0;
        if (file && !fseek(file, 0, SEEK_END)) { length = ftell(file); if (fseek(file, 0, SEEK_SET)) length = 0; }
        sd_media_admin_unlock();
        if (!file || length <= 0 || (!video && length > INPUT_LIMIT)) {
            if (file) {
                fclose(file);
            }
            status(M4_GALLERY_STORAGE);
            continue;
        }
        jpeg_decode_memory_alloc_cfg_t input_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
        size_t input_capacity;
        uint8_t *input = jpeg_alloc_decoder_mem(video ? JPEG_LIMIT : (size_t)length, &input_cfg, &input_capacity);
        if (!input) { fclose(file); status(M4_GALLERY_MEMORY); continue; }
        /* AVI must identify a MJPG video stream in its bounded header. */
        bool valid = true;
        if (!strcasecmp(ext, ".avi")) {
            uint8_t header[4096]; valid = false;
            if (sd_media_admin_lock()) {
                size_t n = fread(header, 1, sizeof(header), file);
                if (n >= 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "AVI ", 4))
                    for (size_t i = 12; i + 4 <= n; ++i) if (!memcmp(header + i, "MJPG", 4) || !memcmp(header + i, "mjpg", 4)) { valid = true; break; }
                if (fseek(file, 0, SEEK_SET)) valid = false;
                sd_media_admin_unlock();
            }
        }
        if (!valid) status(M4_GALLERY_BAD_FORMAT);
        while (valid && current(request.generation)) {
            if (atomic_load(&paused)) { vTaskDelay(pdMS_TO_TICKS(40)); continue; }
            int64_t frame_started = esp_timer_get_time();
            int slot = claim(); if (slot < 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            if (!slots[slot].data) slots[slot].data = heap_caps_malloc(M4_PHOTO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            size_t size = 0; bool locked = slots[slot].data && sd_media_admin_lock();
            if (locked) { size = video ? next_jpeg(file, input, request.generation, current) : fread(input, 1, length, file); sd_media_admin_unlock(); }
            bool ok = size && (video || size == (size_t)length) && m4_photo_decode(input, size, slots[slot].data);
            portENTER_CRITICAL(&lock);
            slots[slot].owner = ok && current(request.generation) ? 2 : 0;
            if (slots[slot].owner == 2) slots[slot].generation = request.generation;
            if (current(request.generation)) {
                if (ok) { ++state.frames; state.status = M4_GALLERY_READY; }
                else state.status = !slots[slot].data ? M4_GALLERY_MEMORY : !locked ? M4_GALLERY_STORAGE : video && !size && feof(file) && state.frames ? M4_GALLERY_ENDED : M4_GALLERY_BAD_FORMAT;
                ++state.revision;
            }
            portEXIT_CRITICAL(&lock);
            if (!ok || !video) break;
            int64_t remaining = 100000 - (esp_timer_get_time() - frame_started);
            if (remaining > 1000) vTaskDelay(pdMS_TO_TICKS((unsigned)(remaining / 1000)));
        }
        fclose(file); free(input);
        /* A completed still image remains visible; close() or a new request releases activity. */
        if (current(request.generation) && video) atomic_store(&active, false);
    }
}
esp_err_t m4_gallery_start(void) {
    requests = xQueueCreate(1, sizeof(request_t)); if (!requests) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(worker, "m4_gallery", 16384, NULL, 2, NULL, 0) == pdPASS) return ESP_OK;
    vQueueDelete(requests); requests = NULL; return ESP_ERR_NO_MEM;
}
