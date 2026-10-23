"""Verify/preview actual embedded glyph arrays with Pillow, without a UI build."""
from pathlib import Path
import importlib.util
import re
from PIL import Image, ImageDraw, ImageFont
from font_pack import read_bitmap

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cjk', ROOT / 'tools/generate_cjk_font.py')
cjk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cjk)


def array(source, name):
    text = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', source, re.S)[1]
    return [int(x.strip(), 0) for x in text.split(',') if x.strip()]


def main():
    source = (ROOT / 'main/m3_font_cjk.c').read_text(encoding='utf-8')
    points = array(source, 'codepoints') + array(source, 'upper_codepoints')
    offsets = array(source, 'offsets')
    bitmap = read_bitmap(source)
    original = cjk.source_glyphs()
    for i, cp in enumerate(points):
        assert bitmap[offsets[i]*16:offsets[i+1]*16] == original[cp], hex(cp)
    glyphs = {cp: bitmap[offsets[i]*16:offsets[i+1]*16] for i, cp in enumerate(points)}
    kb = read_bitmap((ROOT / 'main/m3_font_korean.c').read_text(encoding='utf-8'))
    nanum = ImageFont.truetype(str(ROOT / 'third_party/nanum-gothic/NanumGothic-Regular.ttf'), 16)
    image = Image.new('RGB', (480, 440), '#0a0e14')
    draw = ImageDraw.Draw(image)
    draw.text((20, 14), 'M3 embedded filename glyph check', fill='#00e5ef', font=nanum)
    lines = ['파일명 - 한국어 완성형 유지.wav', '春の音楽（特別版）①.mp3', 'ひらがな・カタカナ.wav',
             '韓國漢字 音樂.aac', '半角ｶﾀｶﾅ・テスト.wav', 'Café £10 €5 ±×÷.mp3', '𠮟る・夏の歌.mp3',
             'が・が・ぱ・ぱ.wav']
    for line_index, line in enumerate(lines):
        x, y = 22, 64 + line_index*42
        for ch in line:
            cp, height, baseline_offset = ord(ch), 16, 2
            if 0xAC00 <= cp <= 0xD7A3:
                index = cp - 0xAC00 + 94
                data = kb[index*36:(index+1)*36]
                height, baseline_offset = 18, 4
            elif cp in glyphs:
                data = glyphs[cp]
            else:
                assert cp < 128, f'Missing glyph {cp:x}'
                draw.text((x, y+14), ch, font=nanum, fill='#e9eef5', anchor='ls')
                x += round(nanum.getlength(ch))
                continue
            width = len(data)*8//height
            left = x - width if cp in (0x3099, 0x309A) else x
            for py in range(height):
                for px in range(width):
                    bit = py*width+px
                    if data[bit//8] & (0x80 >> (bit % 8)):
                        image.putpixel((left+px, y+18-baseline_offset-height+py), (233,238,245))
            if cp not in (0x3099, 0x309A):
                x += width
    draw.text((20, 410), 'Bitmap QA only - not a PC UI/hardware capture', font=nanum, fill='#8c99ab')
    output = ROOT / 'tools/ui_sim/font_preview.png'
    image.save(output)
    print(f'PASS: {len(points):,} embedded glyphs match pinned source; preview: {output}')


if __name__ == '__main__':
    main()
