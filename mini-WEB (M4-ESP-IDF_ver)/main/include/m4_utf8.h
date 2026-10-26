#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
static inline bool m4_utf8_valid(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t cp = *p++; unsigned count; uint32_t minimum;
        if (cp < 0x80) continue;
        if (cp >= 0xc2 && cp <= 0xdf) { count = 1; minimum = 0x80; cp &= 0x1f; }
        else if (cp >= 0xe0 && cp <= 0xef) { count = 2; minimum = 0x800; cp &= 0xf; }
        else if (cp >= 0xf0 && cp <= 0xf4) { count = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        while (count--) {
            if ((*p & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (*p++ & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
/* Display-only clipping: do not use a clipped name to open/rename a file. */
static inline size_t m4_utf8_copy(char *out, size_t capacity, const char *text) {
    if (!capacity) return 0;
    size_t length = strnlen(text, capacity - 1);
    while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    memcpy(out, text, length); out[length] = 0;
    return length;
}
