"""Validate embedded reader atlases and preview decoded glyphs; no native build."""
from pathlib import Path
import hashlib
import json
import re
import zlib
from PIL import Image, ImageDraw, ImageFont
from generate_reader_fonts import POINTS, FONTS, supported_points

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / 'main/m3_reader_fonts.inc').read_text(encoding='utf-8')
    manifest = json.loads((ROOT / 'third_party/reader-fonts.json').read_text())
    preview = Image.new('RGB', (1440, 1280), '#faf9f6')
    draw = ImageDraw.Draw(preview)
    faces = []
    for face, filename in enumerate(FONTS):
        path = ROOT / 'third_party' / filename
        assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest[face]['sha256']
        packed = re.search(rf'reader_packed_{face}\[\] = \{{(.*?)\}};', source, re.S)[1]
        packed = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]+)', packed))
        blob = zlib.decompress(packed)
        assert len(blob) == manifest[face]['raw_bytes']
        table = re.search(rf'reader_glyphs_{face}\[\] = \{{(.*?)\}};', source, re.S)[1]
        glyphs = [tuple(map(int, m.split(','))) for m in re.findall(r'\{([\d,\s-]+)\}', table)]
        assert len(glyphs) == len(POINTS) == 11457
        font = ImageFont.truetype(str(path), 24)
        supported = supported_points(path)
        end = 0
        # Check every glyph against the original TTF raster, not just UI strings.
        for cp, (offset, advance, w, h, ox, oy) in zip(POINTS, glyphs):
            assert offset == end
            size = (w*h+3)//4
            assert offset+size <= len(blob)
            assert w <= 32 and h <= 32 and h+oy <= 26 and oy >= -8
            if cp not in supported:
                assert (advance, w, h, ox, oy) == (0, 0, 0, 0, 0), 'missing glyph must fall back'
                continue
            mask, (left, top) = font.getmask2(chr(cp), mode='L', anchor='ls')
            assert mask.size == (w, h) and (left, -top-h) == (ox, oy)
            assert advance == round(font.getlength(chr(cp)))
            expected = [(p*3+127)//255 for p in bytes(mask)]
            actual = [(blob[offset+i//4] >> (6-2*(i%4))) & 3 for i in range(w*h)]
            assert actual == expected, hex(cp)
            end = offset+size
        assert end == len(blob)
        faces.append((blob, dict(zip(POINTS, glyphs))))
        draw.text((face*480+20, 16), ['Nanum Gothic', 'Nanum Gothic Bold', 'Nanum Myeongjo'][face], font=font, fill='#20242a')
    assert len({hashlib.sha256(b).hexdigest() for b, _ in faces}) == 3
    # Use the exact embedded rasters and the C renderer's fixed-point interpolation.
    for face, (blob, glyphs) in enumerate(faces):
        for row, px in enumerate([16, 24, 36]):
            x, y = face*480+22, [90, 310, 600][row]
            font = ImageFont.truetype(str(ROOT/'third_party'/FONTS[0]), 16)
            draw.text((x, y-26), f'{px}px / embedded 2bpp / bilinear', font=font, fill='#60656a')
            for line in ['한글과 English를 함께 읽습니다.', '가나다라마바사 아자차카타파하', 'Hello, world! ABC abc 0123456789', '얇은 획과 굵은 획, 꽃 값 읽기.', 'Quick brown fox jumps over the dog.']:
                origin = face*480+22
                for ch in line:
                    offset, advance, w, h, ox, oy = glyphs[ord(ch)]
                    scale = lambda v: (v*px+12)//24 if v >= 0 else -((-v*px+12)//24)
                    sw, sh = scale(w), scale(h)
                    if x+scale(advance) > face*480+458:
                        x = origin; y += (34*px+23)//24+4
                    if sw and sh:
                        pixels = [(blob[offset+i//4] >> (6-2*(i%4)) & 3)*85 for i in range(w*h)]
                        image = Image.new('L', (sw, sh))
                        for ty in range(sh):
                            for tx in range(sw):
                                sx = max(0, ((2*tx+1)*w*128)//sw-128)
                                sy = max(0, ((2*ty+1)*h*128)//sh-128)
                                x0, y0 = sx >> 8, sy >> 8
                                x1, y1 = min(x0+1, w-1), min(y0+1, h-1)
                                fx, fy = sx & 255, sy & 255
                                a = pixels[y0*w+x0]*(256-fx)+pixels[y0*w+x1]*fx
                                b = pixels[y1*w+x0]*(256-fx)+pixels[y1*w+x1]*fx
                                image.putpixel((tx, ty), (a*(256-fy)+b*fy+32768) >> 16)
                        preview.paste('#20242a', (x+scale(ox), y+(26*px)//24-sh-scale(oy)), image)
                    x += scale(advance)
                x = origin; y += (34*px+23)//24+4
    draw.text((20, 1252), 'Embedded glyph QA only; not an LVGL UI or hardware capture.', font=font, fill='#60656a')
    output = ROOT / 'tools/ui_sim/reader_fonts_preview.png'
    preview.save(output)
    print(f'PASS: all {len(POINTS)*3:,} embedded glyphs match bundled TTFs; preview: {output}')


if __name__ == '__main__':
    main()
