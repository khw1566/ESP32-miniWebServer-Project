/* Scaled 22px fallback over the existing OFL Nanum/Unifont 1bpp glyphs.
 * No extra glyph atlas or persistent scratch allocation. */
#include "lvgl.h"
extern const lv_font_t m3_font_korean, m3_font_cjk;
static bool base_glyph(uint32_t cp, uint32_t next, lv_font_glyph_dsc_t *out) {
    return lv_font_get_glyph_dsc(&m3_font_korean, out, cp, next) && !out->is_placeholder &&
        (out->resolved_font == &m3_font_korean || out->resolved_font == &m3_font_cjk) && out->format == LV_FONT_GLYPH_FORMAT_A1;
}
static int scale(int value) { return value >= 0 ? (value * 22 + 15) / 16 : -((-value * 22 + 15) / 16); }
static bool describe(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next) {
    (void)font;
    lv_font_glyph_dsc_t base;
    if (!base_glyph(cp, next, &base)) return false;
    out->adv_w = scale(base.adv_w); out->box_w = scale(base.box_w); out->box_h = scale(base.box_h);
    out->ofs_x = scale(base.ofs_x); out->ofs_y = scale(base.ofs_y);
    out->format = LV_FONT_GLYPH_FORMAT_A1; out->is_placeholder = false; out->gid.index = cp;
    return true;
}
static const void *bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer) {
    if (!glyph || !buffer || !buffer->data) return NULL;
    lv_font_glyph_dsc_t base;
    if (!base_glyph(glyph->gid.index, 0, &base) || !base.box_w || !base.box_h) return NULL;
    const uint8_t *source = (const uint8_t *)base.resolved_font->dsc + base.gid.index;
    uint8_t *target = buffer->data;
    uint32_t stride = lv_draw_buf_width_to_stride(glyph->box_w, LV_COLOR_FORMAT_A8);
    for (unsigned y = 0; y < glyph->box_h; ++y) {
        for (unsigned x = 0; x < glyph->box_w; ++x) {
            unsigned bit = (y * base.box_h / glyph->box_h) * base.box_w + x * base.box_w / glyph->box_w;
            target[x] = source[bit >> 3] & (0x80u >> (bit & 7)) ? 255 : 0;
        }
        target += stride;
    }
    return buffer;
}
const lv_font_t m3_font_large = {
    .get_glyph_dsc=describe, .get_glyph_bitmap=bitmap,
    .line_height=32, .base_line=7, .fallback=&lv_font_montserrat_20
};
