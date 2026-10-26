#include "m4_reader_font.h"
#include <string.h>
extern const lv_font_t m4_font_korean;
static unsigned pixels = 22;
static int scaled(int value) {
    return value >= 0 ? (value * (int)pixels + 8) / 16 : -((-value * (int)pixels + 8) / 16);
}
static bool base_glyph(uint32_t cp, uint32_t next, lv_font_glyph_dsc_t *out) {
    return lv_font_get_glyph_dsc(&m4_font_korean, out, cp, next) && !out->is_placeholder;
}
static bool describe(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next) {
    (void)font; lv_font_glyph_dsc_t base;
    if (!base_glyph(cp, next, &base)) return false;
    memset(out, 0, sizeof(*out));
    out->adv_w = scaled(base.adv_w); out->box_w = scaled(base.box_w); out->box_h = scaled(base.box_h);
    out->ofs_x = scaled(base.ofs_x); out->ofs_y = scaled(base.ofs_y);
    out->format = LV_FONT_GLYPH_FORMAT_A8; out->gid.index = cp;
    return true;
}
static const void *bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer) {
    if (!glyph || !buffer || !buffer->data || !glyph->box_w || !glyph->box_h) return NULL;
    lv_font_glyph_dsc_t base;
    if (!base_glyph(glyph->gid.index, 0, &base) || !base.box_w || !base.box_h) return NULL;
    /* Base glyphs are 16px. Bounded aligned scratch also handles ASCII A4. */
    _Alignas(64) uint8_t scratch[2048];
    uint32_t source_stride = lv_draw_buf_width_to_stride(base.box_w, LV_COLOR_FORMAT_A8);
    if (source_stride * base.box_h > sizeof(scratch)) return NULL;
    lv_draw_buf_t source;
    if (lv_draw_buf_init(&source, base.box_w, base.box_h, LV_COLOR_FORMAT_A8,
            source_stride, scratch, sizeof(scratch)) != LV_RESULT_OK) return NULL;
    if (!lv_font_get_glyph_bitmap(&base, &source)) return NULL;
    uint32_t stride = lv_draw_buf_width_to_stride(glyph->box_w, LV_COLOR_FORMAT_A8);
    for (unsigned y = 0; y < glyph->box_h; ++y) {
        uint8_t *target = buffer->data + y * stride;
        const uint8_t *row = source.data + (y * base.box_h / glyph->box_h) * source_stride;
        for (unsigned x = 0; x < glyph->box_w; ++x) target[x] = row[x * base.box_w / glyph->box_w];
    }
    lv_font_glyph_release_draw_data(&base);
    return buffer;
}
static lv_font_t font = {.get_glyph_dsc = describe, .get_glyph_bitmap = bitmap, .line_height = 28, .base_line = 6};
const lv_font_t *m4_reader_font(unsigned value) {
    pixels = value < 16 ? 16 : value > 36 ? 36 : value;
    font.line_height = (20 * pixels + 15) / 16;
    font.base_line = (4 * pixels + 15) / 16;
    return &font;
}
