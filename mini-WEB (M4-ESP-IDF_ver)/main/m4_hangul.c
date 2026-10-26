#include "m4_hangul.h"
#include <stdint.h>
#include <string.h>
static const uint16_t initials[19] = {0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,0x3146,0x3147,0x3148,0x3149,0x314a,0x314b,0x314c,0x314d,0x314e};
static const uint16_t finals[28] = {0,0x3131,0x3132,0x3133,0x3134,0x3135,0x3136,0x3137,0x3139,0x313a,0x313b,0x313c,0x313d,0x313e,0x313f,0x3140,0x3141,0x3142,0x3144,0x3145,0x3146,0x3147,0x3148,0x314a,0x314b,0x314c,0x314d,0x314e};
static const uint8_t vowel_pairs[][3] = {{8,0,9},{8,1,10},{8,20,11},{13,4,14},{13,5,15},{13,20,16},{18,20,19}};
static const uint8_t final_pairs[][3] = {{1,19,3},{4,22,5},{4,27,6},{8,1,9},{8,16,10},{8,17,11},{8,19,12},{8,25,13},{8,26,14},{8,27,15},{17,19,18}};
static uint32_t decode(const unsigned char *p) {
    if (*p < 128) return *p;
    if ((*p & 0xe0) == 0xc0 && p[1] && (p[1] & 0xc0) == 0x80) return ((*p & 31) << 6) | (p[1] & 63);
    if ((*p & 0xf0) == 0xe0 && p[1] && p[2] && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80)
        return ((*p & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63);
    return 0;
}
static unsigned encode(uint32_t c, char *out) {
    if (c < 128) { out[0] = (char)c; return 1; }
    if (c < 2048) { out[0] = (char)(0xc0 | (c >> 6)); out[1] = (char)(0x80 | (c & 63)); return 2; }
    out[0] = (char)(0xe0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3;
}
static int find(const uint16_t *values, unsigned size, uint32_t key) {
    for (unsigned i = 0; i < size; ++i) if (values[i] == key) return (int)i;
    return -1;
}
bool m4_hangul_append(char *text, size_t capacity, const char *key) {
    if (!text || !key || !capacity || !memchr(text, 0, capacity)) return false;
    size_t length = strlen(text), start = length;
    uint32_t next = decode((const unsigned char *)key), last = 0;
    if (!next) return false;
    if (length) {
        start = length - 1;
        while (start && ((unsigned char)text[start] & 0xc0) == 0x80) --start;
        last = decode((const unsigned char *)text + start);
    }
    int l = find(initials, 19, last), vowel = next >= 0x314f && next <= 0x3163 ? (int)(next - 0x314f) : -1;
    int final = find(finals, 28, next);
    uint32_t replacement[2] = {next, 0}; unsigned count = 1;
    bool replace = false;
    if (l >= 0 && vowel >= 0) { replacement[0] = 0xac00 + ((unsigned)l * 21 + vowel) * 28; replace = true; }
    else if (last >= 0xac00 && last <= 0xd7a3) {
        unsigned syllable = last - 0xac00, lead = syllable / 588, v = syllable / 28 % 21, tail = syllable % 28;
        if (!tail && final > 0) { replacement[0] = last + final; replace = true; }
        else if (!tail && vowel >= 0) {
            for (unsigned i = 0; i < sizeof(vowel_pairs)/sizeof(vowel_pairs[0]); ++i)
                if (vowel_pairs[i][0] == v && vowel_pairs[i][1] == vowel) {
                    replacement[0] = 0xac00 + (lead * 21 + vowel_pairs[i][2]) * 28; replace = true;
                }
        } else if (tail && final > 0) {
            for (unsigned i = 0; i < sizeof(final_pairs)/sizeof(final_pairs[0]); ++i)
                if (final_pairs[i][0] == tail && final_pairs[i][1] == final) {
                    replacement[0] = last - tail + final_pairs[i][2]; replace = true;
                }
        } else if (tail && vowel >= 0) {
            unsigned keep = 0, carry = tail;
            for (unsigned i = 0; i < sizeof(final_pairs)/sizeof(final_pairs[0]); ++i)
                if (final_pairs[i][2] == tail) { keep = final_pairs[i][0]; carry = final_pairs[i][1]; break; }
            int onset = find(initials, 19, finals[carry]);
            if (onset >= 0) {
                replacement[0] = last - tail + keep;
                replacement[1] = 0xac00 + ((unsigned)onset * 21 + vowel) * 28;
                count = 2; replace = true;
            }
        }
    }
    if (!replace) start = length;
    char suffix[7]; unsigned bytes = 0;
    for (unsigned i = 0; i < count; ++i) bytes += encode(replacement[i], suffix + bytes);
    if (start + bytes >= capacity) return false;
    memcpy(text + start, suffix, bytes); text[start + bytes] = 0; return true;
}
