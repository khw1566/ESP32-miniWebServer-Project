/* zlib inflate of Flash assets into PSRAM; ROM miniz on the board, lodepng in host previews. */
#include "m3_inflate.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "miniz.h"
#else
#include "lvgl.h"
#include "src/libs/lodepng/lodepng.h"
#endif

#ifdef ESP_PLATFORM
uint8_t *m3_inflate(const uint8_t *packed, size_t packed_size, size_t size) {
    uint8_t *out = heap_caps_malloc(size ? size : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = malloc(size ? size : 1);
    /* About 11 KiB of Huffman tables: keep them off the caller's stack. */
    tinfl_decompressor *state = heap_caps_malloc(sizeof(*state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!state) state = malloc(sizeof(*state));
    if (!out || !state) { free(out); free(state); return NULL; }
    tinfl_init(state);
    size_t in_size = packed_size, out_size = size;
    tinfl_status status = tinfl_decompress(state, packed, &in_size, out, out, &out_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);
    if (status != TINFL_STATUS_DONE || out_size != size) { free(out); return NULL; }
    return out;
}
#else
uint8_t *m3_inflate(const uint8_t *packed, size_t packed_size, size_t size) {
    unsigned char *raw = NULL; size_t raw_size = 0;
    unsigned error = lodepng_zlib_decompress(&raw, &raw_size, packed, packed_size, &lodepng_default_decompress_settings);
    uint8_t *out = !error && raw_size == size ? malloc(size ? size : 1) : NULL;
    if (out) memcpy(out, raw, size);
    lv_free(raw);
    return out;
}
#endif
