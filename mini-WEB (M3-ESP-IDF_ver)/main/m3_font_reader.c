#include "m3_reader_font.h"
#include "m3_font_bitmap.h"
#include <stdlib.h>
#include <string.h>
extern const lv_font_t m3_font_korean;
typedef struct {
    uint32_t offset;
    uint8_t advance, width, height;
    int8_t x, y;
} reader_glyph_t;
typedef struct {
    m3_font_blob_t *blob;
    const reader_glyph_t *glyphs;
} reader_face_t;
#include "m3_reader_fonts.inc"
typedef struct { unsigned pixels, family; } reader_style_t;
/* Stable identities and metrics for each size, including LVGL's glyph cache. */
static lv_font_t fonts[M3_READER_FONT_COUNT][21];
static reader_style_t styles[M3_READER_FONT_COUNT][21];
static unsigned active_family, system_family;
static int scaled(int value, unsigned pixels, unsigned source_px) {
    return value >= 0 ? (value * (int)pixels + (int)source_px / 2) / (int)source_px :
        -((-value * (int)pixels + (int)source_px / 2) / (int)source_px);
}
static const reader_glyph_t *lookup(unsigned family, uint32_t cp) {
    unsigned index;
    if (cp >= 32 && cp <= 126) index = cp - 32;
    else if (cp >= 160 && cp <= 255) index = cp - 160 + 95;
    else if (cp >= 0x3131 && cp <= 0x318e) index = cp - 0x3131 + 191;
    else if (cp >= 0xac00 && cp <= 0xd7a3) index = cp - 0xac00 + 285;
    else return NULL;
    const reader_glyph_t *g = &reader_faces[family].glyphs[index];
    return g->advance || g->width ? g : NULL;
}
static bool base_glyph(unsigned family, uint32_t cp, uint32_t next, lv_font_glyph_dsc_t *out, unsigned *source_px) {
    memset(out, 0, sizeof(*out));
    const reader_glyph_t *g = lookup(family, cp);
    if (g) {
        out->adv_w = g->advance; out->box_w = g->width; out->box_h = g->height;
        out->ofs_x = g->x; out->ofs_y = g->y;
        *source_px = 24;
        return true;
    }
    /* Preserve the existing CJK and symbol fallback for other scripts. */
    *source_px = 16;
    return lv_font_get_glyph_dsc(&m3_font_korean, out, cp, next) && !out->is_placeholder;
}
static bool describe(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next) {
    const reader_style_t *style = font->dsc;
    lv_font_glyph_dsc_t base; unsigned source_px;
    if (!base_glyph(style->family, cp, next, &base, &source_px)) return false;
    memset(out, 0, sizeof(*out));
    out->adv_w = scaled(base.adv_w, style->pixels, source_px);
    out->box_w = scaled(base.box_w, style->pixels, source_px);
    out->box_h = scaled(base.box_h, style->pixels, source_px);
    out->ofs_x = scaled(base.ofs_x, style->pixels, source_px);
    out->ofs_y = scaled(base.ofs_y, style->pixels, source_px);
    out->format = LV_FONT_GLYPH_FORMAT_A8; out->gid.index = cp;
    return true;
}
/* Center-aligned bilinear resampling retains antialiasing at 16..36px. */
static unsigned interpolate(const uint8_t *data, unsigned stride, unsigned width, unsigned height,
        unsigned x, unsigned y, unsigned target_w, unsigned target_h) {
    int sx = ((2 * x + 1) * width * 128) / target_w - 128;
    int sy = ((2 * y + 1) * height * 128) / target_h - 128;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    unsigned x0 = (unsigned)sx >> 8, y0 = (unsigned)sy >> 8;
    unsigned x1 = x0 + 1 < width ? x0 + 1 : x0;
    unsigned y1 = y0 + 1 < height ? y0 + 1 : y0;
    unsigned fx = sx & 255, fy = sy & 255;
    unsigned top = data[y0 * stride + x0] * (256 - fx) + data[y0 * stride + x1] * fx;
    unsigned bottom = data[y1 * stride + x0] * (256 - fx) + data[y1 * stride + x1] * fx;
    return (top * (256 - fy) + bottom * fy + 32768) >> 16;
}
static const void *bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer) {
    if (!glyph || !glyph->resolved_font || !buffer || !buffer->data || !glyph->box_w || !glyph->box_h) return NULL;
    const reader_style_t *style = glyph->resolved_font->dsc;
    lv_font_glyph_dsc_t base; unsigned source_px;
    if (!base_glyph(style->family, glyph->gid.index, 0, &base, &source_px) || !base.box_w || !base.box_h) return NULL;
    _Alignas(64) uint8_t scratch[2048];
    uint32_t source_stride = lv_draw_buf_width_to_stride(base.box_w, LV_COLOR_FORMAT_A8);
    if (source_stride * base.box_h > sizeof(scratch)) return NULL;
    lv_draw_buf_t source;
    if (lv_draw_buf_init(&source, base.box_w, base.box_h, LV_COLOR_FORMAT_A8,
            source_stride, scratch, sizeof(scratch)) != LV_RESULT_OK) return NULL;
    const reader_glyph_t *g = lookup(style->family, glyph->gid.index);
    if (g) {
        const lv_font_t storage = {.dsc = reader_faces[style->family].blob};
        const uint8_t *data = m3_font_blob_data(&storage);
        if (!data) return NULL;
        data += g->offset;
        unsigned bit = 0;
        for (unsigned y = 0; y < base.box_h; ++y)
            for (unsigned x = 0; x < base.box_w; ++x, ++bit)
                source.data[y * source_stride + x] = ((data[bit >> 2] >> (6 - 2 * (bit & 3))) & 3) * 85;
    } else if (!lv_font_get_glyph_bitmap(&base, &source)) return NULL;
    uint32_t stride = lv_draw_buf_width_to_stride(glyph->box_w, LV_COLOR_FORMAT_A8);
    for (unsigned y = 0; y < glyph->box_h; ++y)
        for (unsigned x = 0; x < glyph->box_w; ++x)
            buffer->data[y * stride + x] = interpolate(source.data, source_stride, base.box_w, base.box_h,
                x, y, glyph->box_w, glyph->box_h);
    if (!g) lv_font_glyph_release_draw_data(&base);
    return buffer;
}
static const lv_font_t *font_at(unsigned value, unsigned family) {
    unsigned pixels = value < 16 ? 16 : value > 36 ? 36 : value;
    if (family >= M3_READER_FONT_COUNT) family = 0;
    lv_font_t *font = &fonts[family][pixels - 16];
    if (!font->get_glyph_dsc) {
        reader_style_t *style = &styles[family][pixels - 16];
        *style = (reader_style_t){pixels, family};
        *font = (lv_font_t){.get_glyph_dsc = describe, .get_glyph_bitmap = bitmap,
            .line_height = (34 * pixels + 23) / 24, .base_line = (8 * pixels + 23) / 24, .dsc = style};
    }
    return font;
}
static void trim_atlases(void) {
    /* At most two inflated faces: system and reader. Font getters never evict either. */
    for (unsigned i = 0; i < M3_READER_FONT_COUNT; ++i) {
        if (i == active_family || i == system_family) continue;
        free(reader_faces[i].blob->data); reader_faces[i].blob->data = NULL;
    }
}
const lv_font_t *m3_reader_font(unsigned pixels, unsigned family) {
    active_family = family < M3_READER_FONT_COUNT ? family : 0;
    trim_atlases(); return font_at(pixels, active_family);
}
const lv_font_t *m3_system_font(unsigned pixels, unsigned family) {
    system_family = family < M3_READER_FONT_COUNT ? family : 0;
    trim_atlases(); return font_at(pixels, system_family);
}
