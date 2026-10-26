"""Generate a compact LVGL CJK fallback from pinned Unifont bitmaps; no compiler.

Retain all KS X 1001 Hanja, JIS X 0208 Kanji and Unihan kJoyoKanji entries,
including U+20B9F. Latin extended, kana and non-pictographic symbols are added.
Font data/derivative: SIL OFL 1.1, see third_party/unifont.
"""
from pathlib import Path
import gzip
import hashlib
import json
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'third_party/unifont'
SOURCE = ASSETS / 'unifont_jp-17.0.05.hex.gz'
SOURCE_SHA256 = 'ecf9455c8a44b09e3f645e6b1bab4ac22a57399bf19b1f8e950e11af9523df8c'


def character_set():
    points = set(int(c, 16) for c in json.loads((ASSETS / 'joyo-17.0.json').read_text())['codepoints'])
    for first in range(0xA1, 0xFF):
        for second in range(0xA1, 0xFF):
            for codec in ('euc_jp', 'euc_kr'):
                try:
                    text = bytes((first, second)).decode(codec)
                except UnicodeDecodeError:
                    continue
                if len(text) == 1 and (0x3400 <= ord(text) <= 0x9FFF or 0xF900 <= ord(text) <= 0xFAFF):
                    points.add(ord(text))
    # Kana, CJK punctuation, full/half width forms, Latin extended, Greek/Cyrillic,
    # punctuation, currency, math, enclosed letters/digits, box drawing. No emoji.
    for start, end in ((0xA0, 0x24F), (0x370, 0x52F), (0x2000, 0x206F),
                       (0x2070, 0x209F), (0x20A0, 0x20CF), (0x2100, 0x214F),
                       (0x2200, 0x22FF), (0x2460, 0x259F), (0x3000, 0x303F),
                       (0x3040, 0x30FF), (0x31F0, 0x31FF), (0xFF01, 0xFFEF)):
        for cp in range(start, end + 1):
            if unicodedata.category(chr(cp)) not in ('Cn', 'Cc', 'Cf', 'Cs'):
                points.add(cp)
    # Preserve previously supported bullet and a visible replacement glyph.
    points.add(0xFFFD)
    return sorted(points)


def source_glyphs():
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_SHA256, 'Pinned font changed'
    return {int(cp, 16): bytes.fromhex(bits)
            for cp, bits in (line.split(':') for line in gzip.decompress(SOURCE.read_bytes()).decode().splitlines())}


def main():
    glyphs = source_glyphs()
    points = character_set()
    assert all(cp in glyphs for cp in points), 'Missing glyph in pinned font'
    offsets, bitmap = [], bytearray()
    for cp in points:
        assert len(glyphs[cp]) in (16, 32)
        offsets.append(len(bitmap) // 16)
        bitmap.extend(glyphs[cp])
    offsets.append(len(bitmap) // 16)
    assert offsets[-1] < 65536
    lines = ['/* Generated M4 CJK subset from GNU Unifont JP 17.0.05 (SIL OFL 1.1).',
             ' * Copyright notices and license: third_party/unifont/NOTICE.md and OFL-1.1.txt. */',
             '#include "m4_font_bitmap.h"', 'static const uint8_t bitmap[] = {']
    lines += ['    ' + ','.join(f'0x{x:02x}' for x in bitmap[i:i+24]) + ',' for i in range(0, len(bitmap), 24)]
    bmp = [cp for cp in points if cp <= 0xFFFF]
    upper = [cp for cp in points if cp > 0xFFFF]
    lines += ['};', 'static const uint16_t codepoints[] = {']
    lines += ['    ' + ','.join(f'0x{x:x}' for x in bmp[i:i+12]) + ',' for i in range(0, len(bmp), 12)]
    lines += ['};', 'static const uint32_t upper_codepoints[] = {',
              '    ' + ','.join(f'0x{x:x}' for x in upper), '};']
    lines += ['static const uint16_t offsets[] = {']
    lines += ['    ' + ','.join(str(x) for x in offsets[i:i+16]) + ',' for i in range(0, len(offsets), 16)]
    lines += ['};', '''static bool describe(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next) {
    (void)font; (void)next;
    unsigned lo = 0, count = sizeof(codepoints) / sizeof(codepoints[0]), hi = count;
    if (cp > 0xffff) {
        hi = sizeof(upper_codepoints) / sizeof(upper_codepoints[0]);
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (upper_codepoints[mid] < cp) lo = mid + 1; else hi = mid;
        }
        if (lo == sizeof(upper_codepoints) / sizeof(upper_codepoints[0]) || upper_codepoints[lo] != cp) return false;
        lo += count;
    } else {
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (codepoints[mid] < cp) lo = mid + 1; else hi = mid;
        }
        if (lo == sizeof(codepoints) / sizeof(codepoints[0]) || codepoints[lo] != cp) return false;
    }
    unsigned width = (offsets[lo + 1] - offsets[lo]) * 8;
    out->adv_w = width; out->box_w = width; out->box_h = 16;
    out->ofs_x = 0; out->ofs_y = -2;
    if (cp == 0x3099 || cp == 0x309a) { out->adv_w = 0; out->ofs_x = -(int)width; }
    out->format = LV_FONT_GLYPH_FORMAT_A1; out->is_placeholder = false;
    out->gid.index = offsets[lo] * 16;
    return true;
}
const lv_font_t m4_font_cjk = {
    .get_glyph_dsc=describe, .get_glyph_bitmap=m4_font_bitmap,
    .line_height=20, .base_line=4, .dsc=bitmap, .fallback=&lv_font_montserrat_16
};''', '']
    (ROOT / 'main/m4_font_cjk.c').write_text('\n'.join(lines), encoding='utf-8')
    manifest = {'source': 'https://unifoundry.com/pub/unifont/unifont-17.0.05/font-builds/' + SOURCE.name,
                'sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(), 'license': 'SIL-OFL-1.1',
                'glyph_count': len(points), 'bitmap_bytes': len(bitmap),
                'table_bytes': 2 * len(bmp) + 4 * len(upper) + 2 * len(offsets),
                'codepoints': [f'{cp:04X}' for cp in points]}
    (ASSETS / 'subset-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'CJK: {len(points):,} glyphs, {len(bitmap):,} bitmap + {manifest["table_bytes"]:,} table bytes.')


if __name__ == '__main__':
    main()
