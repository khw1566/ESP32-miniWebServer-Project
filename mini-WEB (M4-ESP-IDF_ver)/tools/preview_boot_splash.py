"""Source-based Pillow preview, not an LVGL render or a board capture.

Reads the firmware's dot table, geometry, palette and label positions.
The dot canvas follows the RGB565 render_frame calculation; Pillow text is
only a layout reference for the compiled LVGL splash fonts.
"""
from pathlib import Path
import argparse
import math
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'results/boot_splash')
    args = parser.parse_args()
    source = (ROOT / 'main/m4_boot_splash.c').read_text(encoding='utf-8')
    table = (ROOT / 'main/m4_boot_logo.inc').read_text(encoding='utf-8')
    def constant(name):
        value = re.search(r'^#define ' + name + r' (\S+)', source, re.M)[1]
        return int(value, 16) if value.startswith('0x') else float(value.rstrip('f'))
    width, height, canvas_y = (int(constant(n)) for n in ('CANVAS_W', 'CANVAS_H', 'CANVAS_Y'))
    pitch, radius, wave_px = (constant(n) for n in ('PITCH', 'DOT_RADIUS', 'WAVE_PX'))
    bg, accent = int(constant('BG')), int(constant('ACCENT'))
    grid = int(re.search(r'#define M4_BOOT_LOGO_GRID (\d+)', table)[1])
    palette = [int(v, 16) for v in re.search(r'palette\[\] = \{([^}]+)', table)[1].split(',')]
    dots = [int(v) for v in re.search(r'm4_boot_logo_dots[^=]+=[^{]*\{([^}]+)', table)[1].split(',') if v.strip()]
    def rgb565(r, g, b):
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    background = rgb565(bg >> 16, (bg >> 8) & 255, bg & 255)
    origin_x, origin_y = (width-(grid-1)*pitch)/2, (height-(grid-1)*pitch)/2
    centre = (grid-1)/2
    args.output.mkdir(parents=True, exist_ok=True)
    for label, t in [('reveal', 0.65), ('ready', 2.4)]:
        pixels = [background] * (width * height)
        for gy in range(grid):
            for gx in range(grid):
                index = dots[gy*grid+gx]
                if not index:
                    continue
                reveal = min(1, (t*30-math.hypot(gx-centre, gy-centre))/4)
                if reveal <= 0:
                    continue
                grow = 1-(1-reveal)**2
                phase = .42*gx+.24*gy-t*4.2
                wave = math.sin(phase)
                cx, cy = origin_x+gx*pitch, origin_y+gy*pitch-wave_px*wave*(1+1.5*(1-reveal))
                r = radius*grow*(.86+.14*wave)
                shine = .62+.38*(.5+.5*math.sin(phase+.6))
                colour = palette[index]
                red, green, blue = (int(((colour >> shift) & 255)*shine) for shift in (16, 8, 0))
                for y in range(max(0, math.floor(cy-r-1)), min(height-1, math.ceil(cy+r+1))+1):
                    for x in range(max(0, math.floor(cx-r-1)), min(width-1, math.ceil(cx+r+1))+1):
                        edge = r+.5-math.hypot(x+.5-cx, y+.5-cy)
                        if edge <= 0:
                            continue
                        cover = 255 if edge >= 1 else int(edge*255)
                        old = pixels[y*width+x]
                        pr, pg, pb = (old >> 8) & 248, (old >> 3) & 252, (old << 3) & 248
                        pixels[y*width+x] = rgb565(pr+int((red-pr)*cover/255), pg+int((green-pg)*cover/255), pb+int((blue-pb)*cover/255))
        canvas = Image.new('RGB', (width, height))
        canvas.putdata([(((v >> 11) & 31)*255//31, ((v >> 5) & 63)*255//63, (v & 31)*255//31) for v in pixels])
        screen = Image.new('RGB', (480, 800), f'#{bg:06x}')
        screen.paste(canvas, ((480-width)//2, canvas_y))
        draw = ImageDraw.Draw(screen)
        font = ROOT / 'third_party/nanum-gothic/NanumGothic-Regular.ttf'
        for text, size, colour, y in [('울산과학대학교',22,0xE2E8F0,canvas_y+height+6),
                                    ('ULSAN COLLEGE',22,0x94A3B8,canvas_y+height+36),
                                    ('창업경진대회',40,0xF8FAFC,594),('MiniWEB 시제품',40,accent,650)]:
            centre_x = int(constant('PRODUCT_TEXT_X')+constant('PRODUCT_TEXT_W')/2) if text == 'MiniWEB 시제품' else 240
            draw.text((centre_x,y),text,font=ImageFont.truetype(str(font),size),fill=f'#{colour:06x}',anchor='mt')
        sharp = (ROOT / 'main/m4_boot_product_mark.inc').read_text(encoding='utf-8')
        raw = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',sharp.split('};',1)[0]))
        mark = Image.frombytes('RGBA',(56,56),raw,'raw','BGRA')
        screen.paste(mark,(int(constant('PRODUCT_MARK_X')),int(constant('PRODUCT_MARK_Y'))),mark)
        bar_width, bar_y = int(constant('BAR_W')), int(constant('BAR_Y'))
        left = (480-bar_width)//2
        draw.rounded_rectangle((left,bar_y,left+bar_width,bar_y+8),radius=4,fill='#1e293b')
        draw.rounded_rectangle((left,bar_y,left+int(bar_width*.94),bar_y+8),radius=4,fill=f'#{accent:06x}')
        draw.text((240,bar_y+18),'웹 서버 시작  94%',font=ImageFont.truetype(str(font),22),fill='#94a3b8',anchor='mt')
        screen.save(args.output / f'uc-product-{label}.png')
    print(f'Source preview only (Pillow text, not LVGL): {args.output}')


if __name__ == '__main__':
    main()
