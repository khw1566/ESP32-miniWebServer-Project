#include "m4_wav.h"
#include <string.h>
static uint16_t u16(const unsigned char *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
bool m4_wav_parse(FILE *f, m4_wav_info_t *out) {
    return m4_wav_parse_limit(f, out, 8 * 1024 * 1024);
}
bool m4_wav_parse_limit(FILE *f, m4_wav_info_t *out, unsigned max_bytes) {
    if (!f || !out) return false;
    memset(out, 0, sizeof(*out));
    unsigned char h[16];
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long actual = ftell(f);
    if (actual < 44 || (unsigned long)actual > max_bytes || fseek(f, 0, SEEK_SET) != 0) return false;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    uint64_t end = (uint64_t)u32(h + 4) + 8;
    if (end > (uint64_t)actual || end < 44) return false;
    bool format = false, data = false;
    for (unsigned chunks = 0; chunks < 128; ++chunks) {
        long pos = ftell(f);
        if (pos < 0 || (uint64_t)pos + 8 > end) break;
        if (fread(h, 1, 8, f) != 8) return false;
        uint32_t bytes = u32(h + 4);
        uint64_t next = (uint64_t)pos + 8 + bytes + (bytes & 1);
        if (next > end) return false;
        if (!memcmp(h, "fmt ", 4)) {
            if (format || bytes < 16 || fread(h, 1, 16, f) != 16 || u16(h) != 1 || u16(h + 14) != 16) return false;
            out->channels = u16(h + 2);
            out->sample_rate = u32(h + 4);
            uint32_t rate = out->sample_rate;
            if ((out->channels != 1 && out->channels != 2) ||
                (rate != 16000 && rate != 22050 && rate != 24000 && rate != 32000 && rate != 44100 && rate != 48000) ||
                u16(h + 12) != out->channels * 2 || u32(h + 8) != rate * out->channels * 2) return false;
            format = true;
        } else if (!memcmp(h, "data", 4)) {
            if (data || !bytes) return false;
            out->data_offset = pos + 8;
            out->data_bytes = bytes;
            data = true;
        }
        if (fseek(f, (long)next, SEEK_SET) != 0) return false;
    }
    long finish = ftell(f);
    if (!format || !data || finish < 0 || (uint64_t)finish != end || out->data_bytes % (out->channels * 2)) return false;
    return fseek(f, out->data_offset, SEEK_SET) == 0;
}
