#include "m3_photos.h"
#include <string.h>
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static bool dimensions(unsigned width, unsigned height) {
    return width && height && width <= 2048 && height <= 2048 && (uint64_t)width * height <= 2 * 1024 * 1024;
}
static void fit(unsigned width, unsigned height, unsigned *w, unsigned *h) {
    if ((uint64_t)width * M3_PHOTO_H > (uint64_t)height * M3_PHOTO_W) {
        *w = M3_PHOTO_W; *h = (uint64_t)height * M3_PHOTO_W / width;
    } else { *h = M3_PHOTO_H; *w = (uint64_t)width * M3_PHOTO_H / height; }
    if (!*w) *w = 1;
    if (!*h) *h = 1;
}
bool m3_photo_rgb565_fit(const uint8_t *data, unsigned width, unsigned height, unsigned stride, uint8_t *canvas) {
    if (!data || !canvas || !dimensions(width, height) || stride < width * 2) return false;
    unsigned w, h; fit(width, height, &w, &h);
    memset(canvas, 0, M3_PHOTO_BYTES);
    unsigned left = (M3_PHOTO_W - w) / 2, top = (M3_PHOTO_H - h) / 2;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        unsigned src = (y * height / h) * stride + (x * width / w) * 2;
        unsigned dest = ((top + y) * M3_PHOTO_W + left + x) * 2;
        canvas[dest] = data[src]; canvas[dest + 1] = data[src + 1];
    }
    return true;
}
bool m3_photo_bmp_fit(const uint8_t *data, size_t size, uint8_t *canvas) {
    if (!data || !canvas || size < 54 || data[0] != 'B' || data[1] != 'M') return false;
    uint32_t offset = le32(data + 10), dib = le32(data + 14), width = le32(data + 18);
    int32_t signed_height = (int32_t)le32(data + 22);
    unsigned height = signed_height < 0 ? -(int64_t)signed_height : signed_height, bits = le16(data + 28);
    if (dib < 40 || (uint64_t)14 + dib > size || offset < 14 + dib || le16(data + 26) != 1 ||
        le32(data + 30) || (bits != 24 && bits != 32) || !dimensions(width, height)) return false;
    unsigned pixel = bits / 8, stride = (width * pixel + 3) & ~3u;
    if ((uint64_t)offset + (uint64_t)stride * height > size) return false;
    unsigned w, h; fit(width, height, &w, &h);
    memset(canvas, 0, M3_PHOTO_BYTES);
    unsigned left = (M3_PHOTO_W - w) / 2, top = (M3_PHOTO_H - h) / 2;
    for (unsigned y = 0; y < h; ++y) {
        unsigned sy = y * height / h;
        if (signed_height > 0) sy = height - 1 - sy;
        for (unsigned x = 0; x < w; ++x) {
            const uint8_t *p = data + offset + sy * stride + (x * width / w) * pixel;
            uint16_t color = (uint16_t)(p[2] >> 3) << 11 | (uint16_t)(p[1] >> 2) << 5 | p[0] >> 3;
            unsigned dest = ((top + y) * M3_PHOTO_W + left + x) * 2;
            canvas[dest] = color; canvas[dest + 1] = color >> 8;
        }
    }
    return true;
}
