"""Preview actual title glyph data plus the shared scaled CJK fallback, no build."""
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont
from font_pack import read_bitmap

ROOT = Path(__file__).resolve().parents[1]


def array(source, name):
    text = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', source, re.S)[1]
    return [int(x.strip(), 0) for x in text.split(',') if x.strip()]


def main():
    korean = (ROOT / 'main/m3_font_korean.c').read_text(encoding='utf-8')
    cjk = (ROOT / 'main/m3_font_cjk.c').read_text(encoding='utf-8')
    title = (ROOT / 'main/m3_font_title.c').read_text(encoding='utf-8')
    kb, cb, tb = read_bitmap(korean), read_bitmap(cjk), bytes(array(title, 'bitmap'))
    points = array(cjk, 'codepoints') + array(cjk, 'upper_codepoints')
    offsets = array(cjk, 'offsets')
    fallback = {cp: cb[offsets[i]*16:offsets[i+1]*16] for i, cp in enumerate(points)}
    selected = {}
    pattern = r'\.bitmap_index=(\d+), \.adv_w=(\d+), \.box_w=(\d+), \.box_h=(\d+), \.ofs_x=(-?\d+), \.ofs_y=(-?\d+)\}, /\* U\+([0-9A-F]+)'
    for match in re.finditer(pattern, title):
        start, advance, w, h, ox, oy = map(int, match.groups()[:6])
        cp = int(match[7], 16)
        mask = Image.new('L', (w, h))
        mask.putdata([((tb[start+i//2] >> (4 if i % 2 == 0 else 0)) & 15)*17 for i in range(w*h)])
        selected[cp] = (mask, round(advance/16), ox, oy)
    line_height = int(re.search(r'\.line_height=(\d+)', title)[1])
    base_line = int(re.search(r'\.base_line=(\d+)', title)[1])
    text = ['큰 제목 한글 대한민국', '캘린더 사진 위젯', '日本語・ひらがな・カタカナ', 'カレンダー 写真ウィジェット', '가나다・한글・𠮟る']
    image = Image.new('RGB', (480, 350), '#0a0e14')
    draw = ImageDraw.Draw(image)
    latin = ImageFont.truetype(str(ROOT/'third_party/nanum-gothic/NanumGothic-Regular.ttf'), 20)
    draw.text((18, 12), 'M3 large text - embedded glyph check', font=latin, fill='#00e5ef')
    scale = lambda x: (x*22+15)//16 if x >= 0 else -((-x*22+15)//16)
    for row, line in enumerate(text):
        x, y = 22, 66 + row*48
        for ch in line:
            cp = ord(ch)
            if cp < 128:
                draw.text((x, y+line_height-base_line), ch, font=latin, fill='#e9eef5', anchor='ls')
                x += round(latin.getlength(ch)); continue
            if cp in selected:
                mask, advance, ox, oy = selected[cp]
            else:
                if 0xAC00 <= cp <= 0xD7A3:
                    index = cp - 0xAC00 + 94
                    data, h, oy = kb[index*36:(index+1)*36], 18, -4
                else:
                    data, h, oy = fallback[cp], 16, -2
                w = len(data)*8//h
                mask = Image.new('L', (w, h))
                mask.putdata([255 if data[i//8] & (0x80 >> (i % 8)) else 0 for i in range(w*h)])
                mask = mask.resize((scale(w), scale(h)), Image.Resampling.NEAREST)
                advance, ox, oy = scale(w), 0, scale(oy)
            top = y+line_height-base_line-mask.height-oy
            assert top >= y and top+mask.height <= y+line_height
            image.paste('#e9eef5', (x+ox, top), mask)
            x += advance
    draw.text((18, 318), 'Glyph QA only - not a PC UI/device capture', font=latin, fill='#8c99ab')
    output = ROOT/'tools/ui_sim/large_font_preview.png'
    image.save(output)
    print(f'PASS: title line {line_height}px / baseline {base_line}px; preview: {output}')


if __name__ == '__main__':
    main()
