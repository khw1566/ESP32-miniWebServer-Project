#include "m3_audio_apps.h"
#include <string.h>

bool m3_speech_text_valid(const char *text) {
    if (!text || !*text || strlen(text) > 384) return false;
    bool content = false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p < 0x80) {
            if ((*p < 32 && *p != '\n' && *p != '\t') || *p == 127) return false;
            if (*p > 32) content = true;
            ++p; continue;
        }
        /* Korean/ASCII/Unicode BMP text; reject malformed and overlong UTF-8. */
        unsigned first = *p++, value, count;
        if (first >= 0xc2 && first <= 0xdf) { value = first & 31; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 15; count = 2; }
        else return false;
        for (unsigned i = 0; i < count; ++i) {
            if ((*p & 0xc0) != 0x80) return false;
            value = (value << 6) | (*p++ & 63);
        }
        if ((count == 2 && value < 0x800) || (value >= 0xd800 && value <= 0xdfff)) return false;
        content = true;
    }
    return content;
}
