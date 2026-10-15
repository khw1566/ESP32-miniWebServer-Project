#include "m3_photos.h"
#include "m3_memory.h"
#include "m3_utf8.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/jpeg_decode.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include "src/libs/lodepng/lodepng.h"

#define PHOTO_ROOT SD_MEDIA_ROOT "/widgets/photos"
enum { PHOTO_COUNT = 64, PHOTO_FILE_LIMIT = 4 * 1024 * 1024 };
static M3_PSRAM_BSS char names[PHOTO_COUNT][256];
static M3_PSRAM_BSS m3_photo_state_t state;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool active, paused, rescan;
static atomic_int steps;
static atomic_uint epoch;
static int64_t due;
static unsigned count, photo_index;
typedef enum { FREE, WRITING, READY, SHOWN } slot_state_t;
static struct { uint8_t *data; slot_state_t state; } slots[2];

static void status(m3_photo_status_t value) {
    portENTER_CRITICAL(&lock); state.status = value; state.count = count;
    state.index = photo_index; ++state.revision; portEXIT_CRITICAL(&lock);
}
void m3_photos_activate(bool enabled) {
    if (atomic_exchange(&active, enabled) != enabled) {
        atomic_fetch_add(&epoch, 1);
        if (enabled) atomic_store(&rescan, true);
    }
}
void m3_photos_step(int direction) { atomic_fetch_add(&steps, direction < 0 ? -1 : 1); atomic_fetch_add(&epoch, 1); }
void m3_photos_pause(void) { bool was = atomic_load(&paused); atomic_store(&paused, !was); }
void m3_photos_refresh(void) { atomic_store(&rescan, true); atomic_fetch_add(&epoch, 1); }
void m3_photos_state(m3_photo_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock);
    out->paused = atomic_load(&paused);
}
const uint8_t *m3_photos_frame(void) {
    const uint8_t *frame = NULL;
    portENTER_CRITICAL(&lock);
    for (unsigned i = 0; i < 2; ++i) if (slots[i].state == READY) {
        for (unsigned j = 0; j < 2; ++j) if (slots[j].state == SHOWN) slots[j].state = FREE;
        slots[i].state = SHOWN; break;
    }
    for (unsigned i = 0; i < 2; ++i) if (slots[i].state == SHOWN) frame = slots[i].data;
    portEXIT_CRITICAL(&lock); return frame;
}
static int claim(void) {
    int chosen = -1;
    portENTER_CRITICAL(&lock);
    for (unsigned i = 0; i < 2; ++i) if (slots[i].state == FREE || slots[i].state == READY) {
        chosen = i; slots[i].state = WRITING; break;
    }
    portEXIT_CRITICAL(&lock); return chosen;
}
static bool cancelled(unsigned generation) { return !atomic_load(&active) || generation != atomic_load(&epoch); }
static bool supported(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".bmp"));
}
static int compare(const void *a, const void *b) { return strcmp(a, b); }
static void scan(void) {
    count = 0; photo_index = 0;
    mkdir(SD_MEDIA_ROOT "/widgets", 0775); mkdir(PHOTO_ROOT, 0775);
    DIR *dir = opendir(PHOTO_ROOT); if (!dir) return;
    struct dirent *entry; unsigned visited = 0;
    while (count < PHOTO_COUNT && visited++ < 1024 && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !supported(entry->d_name) || !m3_utf8_valid(entry->d_name) || strlen(entry->d_name) >= sizeof(names[0])) continue;
        char path[320]; int n = snprintf(path, sizeof(path), PHOTO_ROOT "/%s", entry->d_name);
        struct stat st;
        if (n <= 0 || (size_t)n >= sizeof(path) || stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > PHOTO_FILE_LIMIT) continue;
        memcpy(names[count++], entry->d_name, strlen(entry->d_name) + 1);
    }
    closedir(dir); qsort(names, count, sizeof(names[0]), compare);
}
static bool jpeg_fit(const uint8_t *input, size_t size, uint8_t *canvas, unsigned generation) {
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(input, size, &info) != ESP_OK || !info.width || !info.height ||
        info.width > 2048 || info.height > 2048 || (uint64_t)info.width * info.height > 2 * 1024 * 1024) return false;
    unsigned mx = info.sample_method == JPEG_DOWN_SAMPLING_YUV420 || info.sample_method == JPEG_DOWN_SAMPLING_YUV422 ? 16 : 8;
    unsigned my = info.sample_method == JPEG_DOWN_SAMPLING_YUV420 ? 16 : 8;
    unsigned width = (info.width + mx - 1) / mx * mx, height = (info.height + my - 1) / my * my;
    jpeg_decode_memory_alloc_cfg_t cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t capacity = 0;
    uint8_t *pixels = jpeg_alloc_decoder_mem((size_t)width * height * 2, &cfg, &capacity);
    if (!pixels) { if (generation != UINT_MAX) status(M3_PHOTO_NO_MEMORY); return false; }
    jpeg_decoder_handle_t decoder = NULL;
    jpeg_decode_engine_cfg_t engine = {.intr_priority = 0, .timeout_ms = 500};
    uint32_t written = 0;
    esp_err_t opened = generation != UINT_MAX && cancelled(generation) ? ESP_ERR_INVALID_STATE : jpeg_new_decoder_engine(&engine, &decoder);
    if (opened == ESP_ERR_NO_MEM && generation != UINT_MAX) status(M3_PHOTO_NO_MEMORY);
    bool ok = opened == ESP_OK;
    if (ok) ok = jpeg_decoder_process(decoder, &(jpeg_decode_cfg_t){.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601},
        input, size, pixels, capacity, &written) == ESP_OK && written >= (size_t)width * height * 2;
    if (ok && (generation == UINT_MAX || !cancelled(generation))) ok = m3_photo_rgb565_fit(pixels, info.width, info.height, width * 2, canvas);
    if (decoder) jpeg_del_decoder_engine(decoder);
    free(pixels); return ok;
}
bool m3_photo_decode(const uint8_t *data, size_t size, uint8_t *canvas) {
    if (!data || !canvas || size < 8 || size > PHOTO_FILE_LIMIT) return false;
    if (data[0] == 'B' && data[1] == 'M') return m3_photo_bmp_fit(data, size, canvas);
    if (data[0] == 0xff && data[1] == 0xd8) return jpeg_fit(data, size, canvas, UINT_MAX);
    if (memcmp(data, "\x89PNG\r\n\x1a\n", 8)) return false;
    LodePNGState png; lodepng_state_init(&png);
    unsigned width = 0, height = 0;
    unsigned error = lodepng_inspect(&width, &height, &png, data, size);
    if (error || !width || !height || width > 2048 || height > 2048 || (uint64_t)width * height > 2 * 1024 * 1024) {
        lodepng_state_cleanup(&png); return false;
    }
    unsigned char *rgba = NULL;
    png.info_raw.colortype = LCT_RGBA; png.info_raw.bitdepth = 8;
    png.decoder.zlibsettings.max_output_size = (size_t)(width * 4 + 8) * height + 65536;
    error = lodepng_decode(&rgba, &width, &height, &png, data, size);
    lodepng_state_cleanup(&png);
    if (error || !rgba) { lv_free(rgba); return false; }
    memset(canvas, 0, M3_PHOTO_BYTES);
    unsigned w = M3_PHOTO_W, h = (uint64_t)height * w / width;
    if (h > M3_PHOTO_H) { h = M3_PHOTO_H; w = (uint64_t)width * h / height; }
    if (!w) w = 1;
    if (!h) h = 1;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const uint8_t *p = rgba + ((size_t)(y * height / h) * width + x * width / w) * 4;
        unsigned r = p[0] * p[3] / 255, g = p[1] * p[3] / 255, b = p[2] * p[3] / 255;
        uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        size_t offset = ((y + (M3_PHOTO_H - h) / 2) * M3_PHOTO_W + x + (M3_PHOTO_W - w) / 2) * 2;
        canvas[offset] = color; canvas[offset + 1] = color >> 8;
    }
    lv_free(rgba); return true;
}
void m3_photos_poll(void) {
    if (!atomic_load(&active)) return;
    int64_t now = esp_timer_get_time();
    bool refresh = atomic_load(&rescan); int step = atomic_load(&steps);
    if (!refresh && !step && (atomic_load(&paused) || now < due)) return;
    if (!sd_media_ready()) { count = 0; atomic_store(&rescan, true); status(M3_PHOTO_NO_SD); due = now + 2000000; return; }
    if (!sd_media_admin_lock()) { status(M3_PHOTO_BUSY); due = now + 2000000; return; }
    if (atomic_exchange(&rescan, false)) { scan(); refresh = true; }
    step = atomic_exchange(&steps, 0);
    unsigned generation = atomic_load(&epoch);
    if (!count) { sd_media_admin_unlock(); status(M3_PHOTO_EMPTY); due = now + 7000000; return; }
    if (step) photo_index = (unsigned)((int)photo_index + step % (int)count + (int)count) % count;
    else if (!refresh) photo_index = (photo_index + 1) % count;
    int slot = claim();
    if (slot < 0) { sd_media_admin_unlock(); return; }
    if (!slots[slot].data) slots[slot].data = heap_caps_malloc(M3_PHOTO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    status(slots[slot].data ? M3_PHOTO_LOADING : M3_PHOTO_NO_MEMORY);
    char path[320]; snprintf(path, sizeof(path), PHOTO_ROOT "/%s", names[photo_index]);
    FILE *file = slots[slot].data ? fopen(path, "rb") : NULL;
    uint8_t *input = NULL; size_t size = 0; bool ok = false;
    if (file && !fseek(file, 0, SEEK_END)) {
        long length = ftell(file);
        if (length > 0 && length <= PHOTO_FILE_LIMIT && !fseek(file, 0, SEEK_SET)) {
            size = length;
            jpeg_decode_memory_alloc_cfg_t cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
            size_t capacity;
            input = jpeg_alloc_decoder_mem(size, &cfg, &capacity);
            if (!input) status(M3_PHOTO_NO_MEMORY);
            if (input) {
                size_t read = 0;
                while (read < size && !cancelled(generation)) {
                    size_t block = size - read; if (block > 16384) block = 16384;
                    if (fread(input + read, 1, block, file) != block) break;
                    read += block;
                }
                ok = read == size;
            }
        }
    }
    if (file) fclose(file);
    sd_media_admin_unlock();
    if (ok && !cancelled(generation)) ok = size >= 2 && (input[0] == 'B' && input[1] == 'M' ?
        m3_photo_bmp_fit(input, size, slots[slot].data) : jpeg_fit(input, size, slots[slot].data, generation));
    free(input);
    portENTER_CRITICAL(&lock);
    if (ok && !cancelled(generation)) {
        slots[slot].state = READY; state.status = M3_PHOTO_READY;
        m3_utf8_copy(state.name, sizeof(state.name), names[photo_index]);
    } else {
        slots[slot].state = FREE;
        if (state.status != M3_PHOTO_NO_MEMORY) state.status = M3_PHOTO_INVALID;
    }
    state.count = count; state.index = photo_index; ++state.revision;
    portEXIT_CRITICAL(&lock);
    due = esp_timer_get_time() + 7000000;
}
