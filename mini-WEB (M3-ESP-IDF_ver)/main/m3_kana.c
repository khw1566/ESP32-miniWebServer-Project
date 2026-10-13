#include "m3_kana.h"
#include <string.h>
uint32_t m3_kana_transform(uint32_t cp, unsigned kind) {
    bool katakana = cp >= 0x30a1 && cp <= 0x30f6;
    if (katakana) cp -= 0x60;
    uint32_t result = cp;
    static const uint16_t voiced[] = {0x304b,0x304d,0x304f,0x3051,0x3053,0x3055,0x3057,0x3059,0x305b,0x305d,
        0x305f,0x3061,0x3064,0x3066,0x3068,0x306f,0x3072,0x3075,0x3078,0x307b,0x309d};
    static const uint16_t small[] = {0x3042,0x3044,0x3046,0x3048,0x304a,0x3064,0x3084,0x3086,0x3088,0x308f};
    if (kind == M3_KANA_VOICED) {
        if (cp == 0x3046) result = 0x3094;
        else if (cp == 0x3094) result = 0x3046;
        for (unsigned i = 0; i < sizeof(voiced) / sizeof(voiced[0]); ++i) {
            if (cp == voiced[i]) result = cp + 1;
            else if (cp == (uint32_t)voiced[i] + 1) result = voiced[i];
            else if (voiced[i] >= 0x306f && voiced[i] <= 0x307b && cp == (uint32_t)voiced[i] + 2) result = voiced[i] + 1;
        }
    } else if (kind == M3_KANA_SEMIVOICED) {
        for (unsigned base = 0x306f; base <= 0x307b; base += 3) {
            if (cp == base || cp == base + 1) result = base + 2;
            else if (cp == base + 2) result = base;
        }
    } else if (kind == M3_KANA_SMALL) {
        for (unsigned i = 0; i < sizeof(small) / sizeof(small[0]); ++i) {
            if (cp == small[i]) result = cp - 1;
            else if (cp == (uint32_t)small[i] - 1) result = small[i];
        }
        if (cp == 0x304b) result = 0x3095;
        else if (cp == 0x3095) result = 0x304b;
        else if (cp == 0x3051) result = 0x3096;
        else if (cp == 0x3096) result = 0x3051;
    }
    return katakana ? result + 0x60 : result;
}
bool m3_kana_edit(char *text, size_t capacity, unsigned kind) {
    size_t n = strnlen(text, capacity);
    if (n < 3 || n >= capacity) return false;
    unsigned char *p = (unsigned char *)text + n - 3;
    if ((p[0] & 0xf0) != 0xe0 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80) return false;
    uint32_t before = ((uint32_t)p[0] & 15) << 12 | ((uint32_t)p[1] & 63) << 6 | (p[2] & 63);
    uint32_t after = m3_kana_transform(before, kind);
    if (after == before) return false;
    p[0] = 0xe0 | (after >> 12); p[1] = 0x80 | ((after >> 6) & 63); p[2] = 0x80 | (after & 63);
    return true;
}
