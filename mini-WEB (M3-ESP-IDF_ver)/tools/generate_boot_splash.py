"""Generate the boot splash assets: dot-matrix logo table and splash fonts.

main/m3_boot_logo.inc      : original UC palette-indexed dots (0 = empty)
main/m3_boot_product_mark.inc: sharp transparent cyan M-arrow beside the product text
main/m3_font_splash.c      : 40 px semibold glyphs for the headline strings
main/m3_font_splash_small.c: 22 px glyphs for the caption and progress status

Usage:
  python tools/generate_boot_splash.py                  # UC overlap dot emblem plus a sharp cyan M-arrow
  python tools/generate_boot_splash.py --logo logo.png  # official logo (PNG/JPG, any size)
  python tools/generate_boot_splash.py --placeholder    # explicitly select the default UC emblem

With --logo, near-white or transparent pixels become empty dots and the remaining
colours are reduced to at most six. Dark colours are lifted so they stay visible
on the dark splash background. Run again after changing the splash text below.
"""
from pathlib import Path
import argparse
import re
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_ui_fonts import ROOT, TTF, render, emit  # noqa: E402

GRID = 33
PRODUCT_MARK_SIZE = 56
PRODUCT_MARK_COLOUR = 0x00E5EF
BRAND_LOGO = ROOT / 'assets/branding/logo-mark.svg'
# Keep in sync with the strings in main/m3_boot_splash.c.
HEADLINE = ['창업경진대회', 'MiniWEB 시제품']
CAPTION = ['울산과학대학교', 'ULSAN COLLEGE']
# Progress status lines under the bar, plus the percentage digits.
STATUS = ['화면 준비 중', '음성·미디어 엔진 시작', '홈 화면 구성 중', '저장소 확인 중', 'Wi-Fi 모듈 시작',
          'Wi-Fi 연결 중', '웹 서버 시작', '준비 완료', '0123456789%']


def placeholder():
    """The original ring emblem with overlapping UC letters."""
    size = GRID * 16
    ring = Image.new('L', (size, size))
    draw = ImageDraw.Draw(ring)
    draw.ellipse((8, 8, size - 8, size - 8), fill=255)
    draw.ellipse((size * 0.12, size * 0.12, size * 0.88, size * 0.88), fill=0)
    letters = Image.new('L', (size, size))
    font = ImageFont.truetype(str(TTF), int(size * 0.40))
    for letter, centre in (('U', 0.35), ('C', 0.66)):
        ImageDraw.Draw(letters).text((size * centre, size / 2), letter, font=font, fill=255, anchor='mm',
                                     stroke_width=size // 36, stroke_fill=255)
    ring, letters = ring.resize((GRID, GRID), Image.BOX), letters.resize((GRID, GRID), Image.BOX)
    dots = []
    for y in range(GRID):
        for x in range(GRID):
            dots.append(2 if letters.getpixel((x, y)) > 96 else 1 if ring.getpixel((x, y)) > 110 else 0)
    return dots, [(0x38, 0xBD, 0xF8), (0xF8, 0xFA, 0xFC)]


def brand_masks(size):
    """Rasterize the repository's line-only SVG geometry into coverage masks.

    Keep the exact tile/ink colours, including the dark ink on the light tile.
    Fail on changed geometry rather than silently restoring a placeholder.
    """
    ns = {'svg': 'http://www.w3.org/2000/svg'}
    svg = ET.parse(BRAND_LOGO).getroot()
    if svg.get('viewBox') != '0 0 192 192':
        raise ValueError('M-arrow SVG viewBox changed; update the boot rasterizer')
    rect, group = svg.find('svg:rect', ns), svg.find('svg:g', ns)
    if rect is None or group is None:
        raise ValueError('M-arrow SVG must contain its background tile and path group')
    transform = re.fullmatch(r'translate\(([-\d.]+) ([-\d.]+)\) scale\(([-\d.]+)\)', group.get('transform', ''))
    if not transform:
        raise ValueError('Unsupported M-arrow SVG transform')
    tx, ty, scale = map(float, transform.groups())
    factor = size / 192
    tile, ink = Image.new('L', (size, size)), Image.new('L', (size, size))
    ImageDraw.Draw(tile).rounded_rectangle((0, 0, size - 1, size - 1),
                                          radius=float(rect.get('rx')) * factor, fill=255)
    draw = ImageDraw.Draw(ink)
    paths = group.findall('svg:path', ns)
    if len(paths) != 4:
        raise ValueError('M-arrow SVG path count changed')
    for path in paths:
        data = path.get('d', '')
        tokens = re.findall(r'[MLHVZ]|-?\d+(?:\.\d+)?', data)
        if ''.join(tokens) != re.sub(r'[\s,]', '', data):
            raise ValueError('Boot mark supports only absolute SVG M/L/H/V/Z commands')
        points, i, x, y = [], 0, 0.0, 0.0
        while i < len(tokens):
            command = tokens[i]; i += 1
            if command in ('M', 'L'):
                x, y = float(tokens[i]), float(tokens[i + 1]); i += 2
            elif command == 'H':
                x = float(tokens[i]); i += 1
            elif command == 'V':
                y = float(tokens[i]); i += 1
            elif command == 'Z':
                continue
            else:
                raise ValueError('Unsupported M-arrow path command')
            points.append(((tx + x * scale) * factor, (ty + y * scale) * factor))
        draw.polygon(points, fill=255)
    def colour(value):
        if not re.fullmatch(r'#[0-9a-fA-F]{6}', value or ''):
            raise ValueError('M-arrow colours must be RGB hex')
        return tuple(int(value[i:i+2], 16) for i in (1, 3, 5))
    return tile, ink, [colour(rect.get('fill')), colour(group.get('fill'))]


def product_mark():
    """Sharp alpha-covered M-arrow, with no tile, in the device's cyan accent."""
    _, ink, _ = brand_masks(PRODUCT_MARK_SIZE * 8)
    box = ink.getbbox()
    if not box:
        raise ValueError('M-arrow has no visible paths')
    mask = ink.crop(box)
    side = max(mask.size)
    square = Image.new('L', (side, side))
    square.paste(mask, ((side-mask.width)//2, (side-mask.height)//2))
    mask = square.resize((PRODUCT_MARK_SIZE, PRODUCT_MARK_SIZE), Image.Resampling.LANCZOS)
    colour = PRODUCT_MARK_COLOUR
    # LVGL's little-endian ARGB8888 memory layout is B,G,R,A.
    data = bytes(v for alpha in mask.tobytes()
                 for v in (colour & 255, (colour >> 8) & 255, colour >> 16, alpha))
    return mask, data


def write_product_mark():
    _, data = product_mark()
    lines = ['/* Sharp cyan M-arrow. Generated from assets/branding/logo-mark.svg. */',
             '_Alignas(4) static const uint8_t m3_boot_product_mark_data[] = {']
    lines.extend('    ' + ','.join(f'0x{v:02x}' for v in data[i:i+24]) + ',' for i in range(0,len(data),24))
    lines += ['};', 'static const lv_image_dsc_t m3_boot_product_mark = {',
              '    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,',
              f'               .w = {PRODUCT_MARK_SIZE}, .h = {PRODUCT_MARK_SIZE}, .stride = {PRODUCT_MARK_SIZE*4}' + '},',
              '    .data_size = sizeof(m3_boot_product_mark_data), .data = m3_boot_product_mark_data,',
              '};', '']
    (ROOT / 'main/m3_boot_product_mark.inc').write_text('\n'.join(lines),encoding='utf-8',newline='\n')


def lift(rgb):
    r, g, b = rgb
    if 0.2126 * r + 0.7152 * g + 0.0722 * b >= 110:
        return rgb
    return tuple(round(c + (255 - c) * 0.45) for c in rgb)


def from_logo(path):
    source = Image.open(path).convert('RGBA')
    flat = Image.new('RGBA', source.size, (255, 255, 255, 255))
    flat.alpha_composite(source)
    flat = flat.convert('RGB')
    ink = Image.eval(flat.convert('L'), lambda v: 255 if v < 235 else 0)
    box = ink.getbbox()
    if not box:
        raise SystemExit('logo has no visible pixels')
    flat = flat.crop(box)
    side = max(flat.size)
    square = Image.new('RGB', (side, side), (255, 255, 255))
    square.paste(flat, ((side - flat.width) // 2, (side - flat.height) // 2))
    small = square.resize((GRID, GRID), Image.BOX)
    quant = small.quantize(colors=7, method=Image.Quantize.MEDIANCUT)
    raw = quant.getpalette()[:21]
    colours = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    index, palette, dots = {}, [], []
    for y in range(GRID):
        for x in range(GRID):
            rgb = colours[quant.getpixel((x, y))]
            if min(rgb) > 225:
                dots.append(0)
                continue
            if rgb not in index:
                palette.append(lift(rgb))
                index[rgb] = len(palette)
            dots.append(index[rgb])
    return dots, palette


def write_logo(dots, palette, origin):
    lines = [f'/* Boot splash dot logo ({origin}). Generated by tools/generate_boot_splash.py. */',
             f'#define M3_BOOT_LOGO_GRID {GRID}',
             'static const uint32_t m3_boot_logo_palette[] = {0x000000, ' +
             ', '.join(f'0x{r:02X}{g:02X}{b:02X}' for r, g, b in palette) + '};',
             'static const uint8_t m3_boot_logo_dots[M3_BOOT_LOGO_GRID * M3_BOOT_LOGO_GRID] = {']
    for y in range(GRID):
        lines.append('    ' + ','.join(str(v) for v in dots[y * GRID:(y + 1) * GRID]) + ',')
    lines += ['};', '']
    (ROOT / 'main/m3_boot_logo.inc').write_text('\n'.join(lines), encoding='utf-8', newline='\n')


def write_font(name, strings, size, embolden, comment):
    chars = sorted({c for text in strings for c in text})
    glyphs, _, _ = render(chars, size, 4, embolden)
    inked = [g for g in glyphs if g[3]]
    ascent = max(g[3] + g[5] for g in inked) + 1
    descent = max(0, -min(g[5] for g in inked)) + 1
    return len(chars), emit(name, glyphs, ascent, descent, 'NULL', comment)


def main():
    parser = argparse.ArgumentParser()
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument('--logo', help='custom dot logo; default keeps the UC overlap emblem')
    choice.add_argument('--placeholder', action='store_true', help='explicitly select the default UC overlap emblem')
    parser.add_argument('--logo-only', action='store_true', help='regenerate both logo assets, preserving font assets')
    args = parser.parse_args()
    dots, palette = from_logo(args.logo) if args.logo else placeholder()
    origin = Path(args.logo).name if args.logo else 'UC overlap emblem'
    write_logo(dots, palette, origin)
    write_product_mark()
    if args.logo_only:
        print(f'logo: {sum(1 for d in dots if d)} dots, {len(palette)} colours ({origin})')
        return
    big = write_font('m3_font_splash', HEADLINE, 40, 3, 'Semibold 40 px boot splash headline.')
    small = write_font('m3_font_splash_small', CAPTION + STATUS, 22, 1, '22 px boot splash caption and progress status.')
    print(f'logo: {sum(1 for d in dots if d)} dots, {len(palette)} colours; '
          f'headline {big[0]} glyphs / {big[1]:,} B; caption {small[0]} glyphs / {small[1]:,} B')


if __name__ == '__main__':
    main()
