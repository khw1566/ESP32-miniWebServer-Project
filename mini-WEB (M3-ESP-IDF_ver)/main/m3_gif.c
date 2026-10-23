#include "m3_gif.h"
#include "m3_photos.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Streaming GIF87a/89a decoder. The file is read in 4 KiB blocks under the SD
 * lock; only the composited logical screen (and a lazily allocated copy for
 * "restore previous" disposal) is kept, so frame count and file size are unbounded. */
enum { READ_BYTES = 4096, CODES = 4096 };
struct m3_gif {
    FILE *file;
    unsigned job;
    bool (*current)(unsigned);
    long start;
    unsigned width, height, global_count, frames, pass_frames;
    m3_gif_result_t error;
    bool eof;
    uint16_t *screen, *backup;
    struct { unsigned disposal, x, y, w, h; } previous;
    struct { unsigned delay_cs, disposal, transparent; bool has_transparent; } control;
    unsigned position, length;
    uint8_t buffer[READ_BYTES];
    uint16_t global[256], local[256], prefix[CODES];
    uint8_t suffix[CODES], stack[CODES + 4];
};
static int byte(m3_gif_t *g) {
    if (g->position == g->length) {
        if (g->error) return -1;
        if (!g->current(g->job)) { g->error = M3_GIF_STOPPED; return -1; }
        if (!sd_media_admin_lock()) { g->error = M3_GIF_STORAGE; return -1; }
        size_t n = fread(g->buffer, 1, READ_BYTES, g->file);
        bool failed = ferror(g->file) != 0;
        sd_media_admin_unlock();
        if (failed) { g->error = M3_GIF_STORAGE; return -1; }
        if (!n) { g->eof = true; return -1; } /* Truncated files end like a trailer. */
        g->position = 0; g->length = n;
    }
    return g->buffer[g->position++];
}
static bool read_bytes(m3_gif_t *g, uint8_t *out, unsigned count) {
    for (unsigned i = 0; i < count; ++i) { int b = byte(g); if (b < 0) return false; out[i] = b; }
    return true;
}
static bool skip_blocks(m3_gif_t *g) {
    for (;;) {
        int size = byte(g); if (size < 0) return false;
        if (!size) return true;
        while (size--) if (byte(g) < 0) return false;
    }
}
static bool palette(m3_gif_t *g, uint16_t *out, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        uint8_t rgb[3]; if (!read_bytes(g, rgb, 3)) return false;
        out[i] = (uint16_t)(rgb[0] >> 3) << 11 | (uint16_t)(rgb[1] >> 2) << 5 | rgb[2] >> 3;
    }
    return true;
}
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void dispose_previous(m3_gif_t *g) {
    unsigned x = g->previous.x, y = g->previous.y, w = g->previous.w, h = g->previous.h;
    for (unsigned row = y; row < y + h; ++row) {
        uint16_t *line = g->screen + (size_t)row * g->width + x;
        if (g->previous.disposal == 2) memset(line, 0, w * 2);
        else if (g->previous.disposal == 3 && g->backup) memcpy(line, g->backup + (size_t)row * g->width + x, w * 2);
    }
    g->previous.disposal = 0;
}
typedef struct { unsigned left, top, w, h, count, total, x, row, pass, colors; bool interlace; const uint16_t *palette; } cursor_t;
static void emit(m3_gif_t *g, cursor_t *c, unsigned index) {
    static const uint8_t starts[4] = {0, 4, 2, 1}, steps[4] = {8, 8, 4, 2};
    if (c->count >= c->total) return;
    unsigned ox = c->left + c->x, oy = c->top + c->row;
    if (ox < g->width && oy < g->height && index < c->colors && !(g->control.has_transparent && index == g->control.transparent))
        g->screen[(size_t)oy * g->width + ox] = c->palette[index];
    ++c->count;
    if (++c->x < c->w) return;
    c->x = 0;
    if (!c->interlace) { ++c->row; return; }
    c->row += steps[c->pass];
    while (c->row >= c->h && c->pass < 3) c->row = starts[++c->pass];
}
/* LZW over GIF sub-blocks. Corrupt codes end the frame early instead of failing. */
static bool image_data(m3_gif_t *g, cursor_t *c) {
    int minimum = byte(g);
    if (minimum < 2 || minimum > 8) return false;
    unsigned clear = 1u << minimum, end = clear + 1, avail = clear + 2, size = minimum + 1;
    unsigned block = 0, bits = 0; uint32_t accumulator = 0;
    int old = -1; uint8_t first = 0; bool terminated = false;
    for (;;) {
        while (bits < size) {
            if (!block) {
                int next = byte(g); if (next < 0) return false;
                if (!next) { terminated = true; break; }
                block = next;
            }
            int value = byte(g); if (value < 0) return false;
            accumulator |= (uint32_t)value << bits; bits += 8; --block;
        }
        if (terminated) break;
        unsigned code = accumulator & ((1u << size) - 1); accumulator >>= size; bits -= size;
        if (code == clear) { size = minimum + 1; avail = clear + 2; old = -1; continue; }
        if (code == end) break;
        if (old < 0) {
            if (code >= clear) break;
            emit(g, c, code); first = code; old = code; continue;
        }
        if (code > avail) break;
        unsigned in = code, top = 0;
        if (code == avail) { g->stack[top++] = first; code = old; }
        while (code >= clear && top < CODES) { g->stack[top++] = g->suffix[code]; code = g->prefix[code]; }
        if (code >= clear) break;
        first = code; g->stack[top++] = first;
        if (avail < CODES) {
            g->prefix[avail] = old; g->suffix[avail] = first;
            if (++avail == (1u << size) && size < 12) ++size;
        }
        old = in;
        while (top) emit(g, c, g->stack[--top]);
    }
    if (terminated) return true;
    while (block--) if (byte(g) < 0) return false;
    return skip_blocks(g);
}
static m3_gif_result_t image(m3_gif_t *g) {
    uint8_t d[9];
    if (!read_bytes(g, d, sizeof(d))) return g->error ? g->error : M3_GIF_FORMAT;
    cursor_t c = {.left = le16(d), .top = le16(d + 2), .w = le16(d + 4), .h = le16(d + 6),
        .interlace = d[8] & 0x40, .palette = g->global, .colors = g->global_count};
    if (d[8] & 0x80) {
        c.colors = 2u << (d[8] & 7);
        if (!palette(g, g->local, c.colors)) return g->error ? g->error : M3_GIF_FORMAT;
        c.palette = g->local;
    }
    if (!c.colors) return M3_GIF_FORMAT;
    c.total = c.w * c.h;
    dispose_previous(g);
    unsigned x = c.left < g->width ? c.left : g->width, y = c.top < g->height ? c.top : g->height;
    unsigned w = c.left + c.w > g->width ? g->width - x : c.w, h = c.top + c.h > g->height ? g->height - y : c.h;
    if (g->control.disposal == 3 && w && h) {
        if (!g->backup) g->backup = heap_caps_malloc((size_t)g->width * g->height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g->backup) return M3_GIF_MEMORY;
        for (unsigned row = y; row < y + h; ++row)
            memcpy(g->backup + (size_t)row * g->width + x, g->screen + (size_t)row * g->width + x, w * 2);
    }
    g->previous.disposal = g->control.disposal; g->previous.x = x; g->previous.y = y; g->previous.w = w; g->previous.h = h;
    if (!image_data(g, &c) && g->error) return g->error;
    return M3_GIF_OK;
}
static bool restart(m3_gif_t *g) {
    if (!sd_media_admin_lock()) { g->error = M3_GIF_STORAGE; return false; }
    bool ok = !fseek(g->file, g->start, SEEK_SET);
    sd_media_admin_unlock();
    if (!ok) { g->error = M3_GIF_STORAGE; return false; }
    g->position = g->length = 0; g->pass_frames = 0; g->eof = false;
    memset(g->screen, 0, (size_t)g->width * g->height * 2);
    memset(&g->previous, 0, sizeof(g->previous)); memset(&g->control, 0, sizeof(g->control));
    return true;
}
m3_gif_result_t m3_gif_frame(m3_gif_t *g, uint8_t *canvas, unsigned *delay_ms) {
    if (!g || !canvas) return M3_GIF_FORMAT;
    for (;;) {
        int block = byte(g);
        if (g->error) return g->error;
        if (block < 0 || block == 0x3b) {
trailer:
            if (!g->pass_frames) return M3_GIF_FORMAT;
            if (g->frames == 1) return m3_photo_rgb565_fit((const uint8_t *)g->screen, g->width, g->height, g->width * 2, canvas) ? M3_GIF_END : M3_GIF_FORMAT;
            if (!restart(g)) return g->error;
            continue;
        }
        if (!block) continue; /* Some encoders pad between blocks. */
        if (block == 0x21) {
            int label = byte(g);
            bool ok = label >= 0;
            if (ok && label == 0xf9) {
                uint8_t gce[5] = {0};
                ok = read_bytes(g, gce, sizeof(gce)) && gce[0] == 4;
                g->control.disposal = (gce[1] >> 2) & 7; g->control.has_transparent = gce[1] & 1;
                g->control.delay_cs = le16(gce + 2); g->control.transparent = gce[4];
            }
            if (ok && skip_blocks(g)) continue;
            if (g->error) return g->error;
            if (g->eof) goto trailer;
            return M3_GIF_FORMAT;
        }
        if (block != 0x2c) return M3_GIF_FORMAT;
        m3_gif_result_t result = image(g);
        if (result == M3_GIF_FORMAT && g->eof && !g->error) goto trailer;
        if (result != M3_GIF_OK) return result;
        ++g->pass_frames; if (g->frames < 2) ++g->frames;
        unsigned delay = g->control.delay_cs * 10;
        if (delay_ms) *delay_ms = delay < 20 ? 100 : delay; /* Browsers treat 0-10 ms as 100 ms. */
        memset(&g->control, 0, sizeof(g->control));
        return m3_photo_rgb565_fit((const uint8_t *)g->screen, g->width, g->height, g->width * 2, canvas) ? M3_GIF_OK : M3_GIF_FORMAT;
    }
}
m3_gif_result_t m3_gif_open(const char *relative, unsigned job, bool (*current)(unsigned), m3_gif_t **out) {
    if (!out || !current || !relative || relative[0] != '/' || strlen(relative) > 255 || strstr(relative, "..")) return M3_GIF_FORMAT;
    *out = NULL;
    m3_gif_t *g = heap_caps_calloc(1, sizeof(*g), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g) return M3_GIF_MEMORY;
    g->job = job; g->current = current;
    m3_gif_result_t result = M3_GIF_FORMAT;
    char path[272]; snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, relative);
    if (!sd_media_admin_lock()) { result = M3_GIF_STORAGE; goto done; }
    g->file = fopen(path, "rb");
    sd_media_admin_unlock();
    if (!g->file) { result = M3_GIF_STORAGE; goto done; }
    uint8_t header[13];
    if (!read_bytes(g, header, sizeof(header))) { result = g->error ? g->error : M3_GIF_FORMAT; goto done; }
    if (memcmp(header, "GIF87a", 6) && memcmp(header, "GIF89a", 6)) goto done;
    g->width = le16(header + 6); g->height = le16(header + 8);
    if (!g->width || !g->height || g->width > 2048 || g->height > 2048 || (uint64_t)g->width * g->height > 2 * 1024 * 1024) goto done;
    if (header[10] & 0x80) {
        g->global_count = 2u << (header[10] & 7);
        if (!palette(g, g->global, g->global_count)) { result = g->error ? g->error : M3_GIF_FORMAT; goto done; }
    }
    g->start = sizeof(header) + g->global_count * 3;
    g->screen = heap_caps_calloc((size_t)g->width * g->height, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g->screen) { result = M3_GIF_MEMORY; goto done; }
    *out = g; return M3_GIF_OK;
done:
    if (!current(job)) result = M3_GIF_STOPPED;
    m3_gif_close(g); return result;
}
void m3_gif_close(m3_gif_t *g) {
    if (!g) return;
    if (g->file) fclose(g->file);
    free(g->screen); free(g->backup); free(g);
}
