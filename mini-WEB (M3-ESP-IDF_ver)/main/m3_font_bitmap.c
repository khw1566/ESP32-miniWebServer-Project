#include "m3_font_bitmap.h"
const void *m3_font_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer) {
    if (!glyph || !glyph->resolved_font || !buffer || !buffer->data) return NULL;
    const uint8_t *source = (const uint8_t *)glyph->resolved_font->dsc + glyph->gid.index;
    uint8_t *target = buffer->data;
    uint32_t stride = lv_draw_buf_width_to_stride(glyph->box_w, LV_COLOR_FORMAT_A8);
    unsigned bit = 0;
    for (unsigned y = 0; y < glyph->box_h; ++y) {
        for (unsigned x = 0; x < glyph->box_w; ++x, ++bit)
            target[x] = source[bit >> 3] & (0x80u >> (bit & 7)) ? 255 : 0;
        target += stride;
    }
    return buffer;
}
