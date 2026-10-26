#include "m4_mp4.h"
#include "m4_photos.h"
#include <string.h>

bool m4_mp4_avcc_sample(uint8_t *data, size_t bytes, unsigned length_bytes) {
    /* Four-byte length prefixes can be replaced in place with Annex-B markers. */
    if (!data || length_bytes != 4 || !bytes) return false;
    size_t position = 0;
    while (position < bytes) {
        if (bytes - position < 4) return false;
        uint32_t length = ((uint32_t)data[position] << 24) | ((uint32_t)data[position + 1] << 16) |
            ((uint32_t)data[position + 2] << 8) | data[position + 3];
        if (!length || length > bytes - position - 4) return false;
        if (data[position + 4] & 0x80) return false;
        unsigned type = data[position + 4] & 31;
        /* Parameter changes inside samples could defeat the pre-decode size limit. */
        if (type != 1 && type != 5 && type != 6 && type != 9 && type != 12) return false;
        data[position] = data[position + 1] = data[position + 2] = 0; data[position + 3] = 1;
        position += 4 + length;
    }
    return true;
}
typedef struct { const uint8_t *data; size_t bytes, bit; bool failed; } bits_t;
static uint32_t bits(bits_t *b, unsigned count) {
    if (count > 32 || b->bit + count > b->bytes * 8) { b->failed = true; return 0; }
    uint32_t value = 0;
    while (count--) { value = (value << 1) | ((b->data[b->bit / 8] >> (7 - b->bit % 8)) & 1); ++b->bit; }
    return value;
}
static uint32_t ue(bits_t *b) {
    unsigned zeroes = 0;
    while (!b->failed && !bits(b, 1)) { if (++zeroes > 24) { b->failed = true; return 0; } }
    return zeroes ? ((1U << zeroes) - 1) + bits(b, zeroes) : 0;
}
bool m4_mp4_sps_valid(const uint8_t *data, size_t bytes, unsigned *width, unsigned *height) {
    if (!data || !width || !height || bytes < 5 || bytes > 256 || (data[0] & 0x9f) != 7) return false;
    uint8_t rbsp[256]; unsigned length = 0, zeroes = 0;
    for (size_t i = 1; i < bytes; ++i) {
        if (zeroes >= 2 && data[i] == 3) { zeroes = 0; continue; }
        rbsp[length++] = data[i]; zeroes = data[i] == 0 ? zeroes + 1 : 0;
    }
    bits_t b = {.data = rbsp, .bytes = length};
    if (bits(&b, 8) != 66 || !(bits(&b, 8) & 0x40)) return false;
    if (bits(&b, 8) > 31 || ue(&b) > 31 || ue(&b) > 12) return false;
    unsigned order = ue(&b);
    if (order == 0) { if (ue(&b) > 12) return false; }
    else if (order == 1) {
        bits(&b, 1); ue(&b); ue(&b);
        unsigned cycle = ue(&b); if (cycle > 32) return false;
        for (unsigned i = 0; i < cycle; ++i) ue(&b);
    } else if (order != 2) return false;
    if (ue(&b) > 4) return false;
    bits(&b, 1);
    unsigned mbw = ue(&b), mbh = ue(&b);
    if (mbw >= 40 || mbh >= 30 || !bits(&b, 1)) return false; /* <=640x480, progressive */
    bits(&b, 1);
    unsigned left = 0, right = 0, top = 0, bottom = 0;
    if (bits(&b, 1)) { left = ue(&b); right = ue(&b); top = ue(&b); bottom = ue(&b); }
    unsigned w = (mbw + 1) * 16, h = (mbh + 1) * 16;
    if (b.failed || left + right >= w / 2 || top + bottom >= h / 2) return false;
    *width = w - 2 * (left + right); *height = h - 2 * (top + bottom);
    return *width >= 16 && *height >= 16;
}
static unsigned clamp(int value) { return value < 0 ? 0 : value > 255 ? 255 : (unsigned)value; }
bool m4_mp4_i420_fit(const uint8_t *data, size_t bytes, unsigned width, unsigned height, uint8_t *canvas) {
    if (!data || !canvas || width < 16 || height < 16 || width > 640 || height > 480 || (width & 1) || (height & 1) ||
        bytes < (size_t)width * height * 3 / 2) return false;
    const uint8_t *u = data + width * height, *v = u + width * height / 4;
    unsigned w = M4_PHOTO_W, h = (uint64_t)height * w / width;
    if (h > M4_PHOTO_H) { h = M4_PHOTO_H; w = (uint64_t)width * h / height; }
    if (!w || !h) return false;
    memset(canvas, 0, M4_PHOTO_BYTES);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        unsigned sx = x * width / w, sy = y * height / h;
        int yy = (int)data[sy * width + sx] - 16;
        int uu = (int)u[(sy / 2) * (width / 2) + sx / 2] - 128;
        int vv = (int)v[(sy / 2) * (width / 2) + sx / 2] - 128;
        unsigned r = clamp((298 * yy + 409 * vv + 128) >> 8);
        unsigned g = clamp((298 * yy - 100 * uu - 208 * vv + 128) >> 8);
        unsigned blue = clamp((298 * yy + 516 * uu + 128) >> 8);
        uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (blue >> 3);
        size_t offset = ((y + (M4_PHOTO_H - h) / 2) * M4_PHOTO_W + x + (M4_PHOTO_W - w) / 2) * 2;
        canvas[offset] = pixel; canvas[offset + 1] = pixel >> 8;
    }
    return true;
}
