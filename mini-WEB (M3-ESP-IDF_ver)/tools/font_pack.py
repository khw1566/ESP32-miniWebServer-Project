"""Shared zlib packing for the 1bpp Korean/CJK font bitmaps (m3_font_blob_t).

Generators call c_lines(bitmap) to emit the packed array. Host tests and previews call
read_bitmap(source) to get the raw bitmap back. Re-pack existing generated files without
redrawing glyphs (pixel identical):
    python tools/font_pack.py main/m3_font_korean.c main/m3_font_cjk.c
"""
from pathlib import Path
import re
import sys
import zlib

PACKED = re.compile(r'static const uint8_t bitmap_packed\[\] = \{([\s\S]*?)\};\n'
                    r'static m3_font_blob_t bitmap = \{bitmap_packed, sizeof\(bitmap_packed\), (\d+), NULL\};')
RAW = re.compile(r'static const uint8_t bitmap\[\] = \{([\s\S]*?)\};')


def _hex_lines(data):
    return ['    ' + ','.join(f'0x{c:02x}' for c in data[i:i + 24]) + ',' for i in range(0, len(data), 24)]


def c_lines(bitmap):
    """C declarations of a packed bitmap blob named `bitmap` (use .dsc=&bitmap)."""
    packed = zlib.compress(bytes(bitmap), 9)
    return (['/* zlib level 9; m3_font_bitmap inflates it into PSRAM on first use. */',
             'static const uint8_t bitmap_packed[] = {'] + _hex_lines(packed) +
            ['};', f'static m3_font_blob_t bitmap = {{bitmap_packed, sizeof(bitmap_packed), {len(bitmap)}, NULL}};'])


def _numbers(body):
    return bytes(int(v, 0) for v in (x.strip() for x in body.split(',')) if v)


def read_bitmap(source):
    """Raw glyph bitmap of a generated font source, packed or not."""
    match = PACKED.search(source)
    if match:
        raw = zlib.decompress(_numbers(match.group(1)))
        assert len(raw) == int(match.group(2)), 'packed size mismatch'
        return raw
    return _numbers(RAW.search(source).group(1))


def repack(path):
    source = Path(path).read_text(encoding='utf-8')
    if PACKED.search(source):
        return False
    match = RAW.search(source)
    raw = _numbers(match.group(1))
    source = source[:match.start()] + '\n'.join(c_lines(raw)) + source[match.end():]
    assert source.count('.dsc=bitmap') == 1, path
    source = source.replace('.dsc=bitmap', '.dsc=&bitmap')
    assert read_bitmap(source) == raw
    Path(path).write_text(source, encoding='utf-8', newline='\n')
    return True


if __name__ == '__main__':
    for name in sys.argv[1:]:
        print(name, 'packed' if repack(name) else 'already packed')
