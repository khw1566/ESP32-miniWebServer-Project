#include "m3_font_bitmap.h"
#include "m3_inflate.h"
const uint8_t *m3_font_blob_data(const lv_font_t *font) {
    m3_font_blob_t *blob = font ? (m3_font_blob_t *)font->dsc : NULL;
    if (!blob) return NULL;
    /* LVGL renders on one task, so the first glyph draw inflates without a lock. */
    if (!blob->data) blob->data = m3_inflate(blob->packed, blob->packed_size, blob->size);
    return blob->data;
}
const void *m3_font_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer) {
    if (!glyph || !glyph->resolved_font || !buffer || !buffer->data) return NULL;
    const uint8_t *data = m3_font_blob_data(glyph->resolved_font);
    if (!data) return NULL;
    const uint8_t *source = data + glyph->gid.index;
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
