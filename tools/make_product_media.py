"""Media for the product home page (web_examples/00_product_home), hosted on R2.

    python tools/make_product_media.py        -> web_examples/00_product_home/media/

hero.mp4 / hero.webm / hero-poster.webp
    Isometric exploded view of the S3v2 enclosure (68 x 31.8 x 9 mm, from the
    competition drawing): case, board, module, USB-C and cover separate, the
    antenna radiates, then everything closes again. Drawn at 2x and downscaled.
lineup-*.webp, architecture.webp
    The existing product drawings and architecture diagram, recompressed.
Upload: see web_examples/00_product_home/README.md (wrangler r2 object put).
"""
import math
import shutil
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'web_examples' / '00_product_home' / 'media'
DRAWINGS = ROOT / '2026 지산학 창업창직 경진대회_김형우'
W, H, S = 1280, 720, 2          # output size and supersampling factor
FPS, SECONDS = 30, 7
FONT = 'C:/Windows/Fonts/malgunbd.ttf'


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def shade(color, f):
    return tuple(max(0, min(255, int(c * f))) for c in color[:3]) + tuple(color[3:])


class Iso:
    """Isometric projection, millimetres -> pixels (supersampled)."""
    def __init__(self, scale, cx, cy):
        self.k, self.cx, self.cy = scale * S, cx * S, cy * S

    def p(self, x, y, z):
        return (self.cx + (x - y) * math.cos(math.pi / 6) * self.k,
                self.cy + (x + y) * math.sin(math.pi / 6) * self.k - z * self.k)

    def box(self, d, x, y, z, w, dep, h, color, outline=None, top_color=None):
        P = self.p
        x1, y1, z1 = x + w, y + dep, z + h
        side_y = [P(x, y1, z), P(x1, y1, z), P(x1, y1, z1), P(x, y1, z1)]
        side_x = [P(x1, y, z), P(x1, y1, z), P(x1, y1, z1), P(x1, y, z1)]
        top = [P(x, y, z1), P(x1, y, z1), P(x1, y1, z1), P(x, y1, z1)]
        line = outline or shade(color, 0.55)
        d.polygon(side_y, fill=shade(color, 0.78), outline=line)
        d.polygon(side_x, fill=shade(color, 0.62), outline=line)
        d.polygon(top, fill=top_color or color, outline=line)
        return top

    def quad(self, d, pts3, fill, outline=None):
        d.polygon([self.p(*q) for q in pts3], fill=fill, outline=outline)

    def ellipse(self, d, x, y, z, r, fill):
        cx, cy = self.p(x, y, z)
        rx, ry = r * self.k * 1.0, r * self.k * 0.58
        d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=fill)


CASE = (226, 230, 236)
COVER = (244, 246, 249)
PCB = (30, 36, 46)
MODULE = (196, 203, 213)
GOLD = (214, 170, 72)
USB = (128, 138, 152)
ACCENT = (96, 165, 250)
MINT = (94, 234, 212)
L, Wd = 68.0, 31.8             # enclosure length (x) and width (y)


def device(d, iso, t_open, glow):
    """Draw the enclosure exploded by t_open (0 closed, 1 fully open)."""
    lift = ease(t_open)
    z_case, z_pcb, z_cover, z_screw = 0, 3.0 + 16 * lift, 7.4 + 40 * lift, 9.0 + 58 * lift
    # Bottom case: tray with an inner cavity.
    top = iso.box(d, 0, 0, z_case, L, Wd, 7, CASE)
    iso.quad(d, [(1.6, 1.6, 7), (L - 1.6, 1.6, 7), (L - 1.6, Wd - 1.6, 7), (1.6, Wd - 1.6, 7)], shade(CASE, 0.86))
    iso.quad(d, [(1.6, 1.6, 7), (1.6, Wd - 1.6, 7), (1.6, Wd - 1.6, 1.4), (1.6, 1.6, 1.4)], shade(CASE, 0.7))
    for cx, cy in ((3, 3), (L - 3, 3), (3, Wd - 3), (L - 3, Wd - 3)):
        iso.ellipse(d, cx, cy, 7.01, 1.6, shade(CASE, 0.55))
    # Board with module, pin header, USB-C ports and antenna strip.
    iso.box(d, 5, 3.4, z_pcb, 58, 25, 1.6, PCB)
    for i in range(22):
        iso.box(d, 9 + i * 2.4, 25.4, z_pcb + 1.6, 1.3, 1.3, 0.7, GOLD)
    module_top = iso.box(d, 26, 7, z_pcb + 1.6, 22, 16, 3.2, MODULE)
    iso.quad(d, [(27.5, 8.5, z_pcb + 4.81), (46.5, 8.5, z_pcb + 4.81), (46.5, 21.5, z_pcb + 4.81), (27.5, 21.5, z_pcb + 4.81)], shade(MODULE, 1.08))
    iso.box(d, 48, 9, z_pcb + 1.6, 7, 12, 1.0, (40, 52, 68))           # antenna area
    for i in range(2):
        iso.box(d, 56.5, 6 + i * 11, z_pcb + 1.6, 6.5, 8.8, 3.2, USB)
    if glow > 0:
        # Wi-Fi waves from the PCB antenna.
        cx, cy = iso.p(52, 15, z_pcb + 3)
        for i in range(3):
            phase = (glow * 3 + i / 3) % 1
            r = (8 + phase * 46) * S
            a = int(200 * (1 - phase) * min(1, glow * 4))
            d.arc([cx - r, cy - r * 0.62, cx + r, cy + r * 0.62], 200, 340, fill=MINT + (a,), width=3 * S)
        mx, my = iso.p(37, 15, z_pcb + 4.8)
        d.ellipse([mx - 46 * S, my - 26 * S, mx + 46 * S, my + 26 * S], outline=ACCENT + (int(160 * glow),), width=2 * S)
    # Cover with screw holes, then the screws above it.
    iso.box(d, 0, 0, z_cover, L, Wd, 1.6, COVER, top_color=COVER)
    for cx, cy in ((3, 3), (L - 3, 3), (3, Wd - 3), (L - 3, Wd - 3)):
        iso.ellipse(d, cx, cy, z_cover + 1.61, 1.3, shade(COVER, 0.7))
    for cx, cy in ((3, 3), (L - 3, 3), (3, Wd - 3), (L - 3, Wd - 3)):
        sx, sy = iso.p(cx, cy, z_screw)
        bx, by = iso.p(cx, cy, z_screw - 5)
        d.line([sx, sy, bx, by], fill=(150, 156, 166), width=3 * S)
        iso.ellipse(d, cx, cy, z_screw, 1.7, (120, 128, 140))
        iso.ellipse(d, cx, cy, z_screw + 0.05, 1.2, (170, 176, 186))
    return z_pcb, z_cover


def label(d, font, anchor_xy, text, alpha, side=1):
    if alpha <= 0:
        return
    ax, ay = anchor_xy
    ex, ey = ax + side * 90 * S, ay - 34 * S
    col = (226, 232, 240, int(255 * alpha))
    d.line([ax, ay, ex, ey, ex + side * 40 * S, ey], fill=(148, 163, 184, int(220 * alpha)), width=2 * S)
    d.ellipse([ax - 5 * S, ay - 5 * S, ax + 5 * S, ay + 5 * S], fill=MINT + (int(255 * alpha),))
    tx = ex + side * 48 * S
    d.text((tx, ey), text, font=font, fill=col, anchor='lm' if side > 0 else 'rm')


def background():
    bg = Image.new('RGBA', (W * S, H * S), (6, 9, 16, 255))
    glow = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    g.ellipse([(W / 2 - 520) * S, (H * 0.5 - 300) * S, (W / 2 + 520) * S, (H * 0.5 + 300) * S], fill=(37, 99, 235, 70))
    bg.alpha_composite(glow.filter(ImageFilter.GaussianBlur(120 * S)))
    grid = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    g = ImageDraw.Draw(grid)
    iso = Iso(5.2, W / 2, H * 0.62)
    for i in range(-14, 15):
        g.line([iso.p(i * 8, -112, -3), iso.p(i * 8, 112, -3)], fill=(100, 116, 139, 30), width=S)
        g.line([iso.p(-112, i * 8, -3), iso.p(112, i * 8, -3)], fill=(100, 116, 139, 30), width=S)
    bg.alpha_composite(grid)
    return bg


def render_hero(tmp):
    font = ImageFont.truetype(FONT, 18 * S)
    base = background()
    iso = Iso(5.2, W / 2 + 10, H * 0.62)
    frames = FPS * SECONDS
    for f in range(frames):
        t = f / frames
        # 0-0.14 closed, 0.14-0.36 open, 0.36-0.7 open + glow, 0.7-0.9 close, 0.9-1 closed
        if t < 0.14: o = 0
        elif t < 0.36: o = (t - 0.14) / 0.22
        elif t < 0.70: o = 1
        elif t < 0.90: o = 1 - (t - 0.70) / 0.20
        else: o = 0
        glow = ease((t - 0.30) / 0.08) * (1 - ease((t - 0.66) / 0.08))
        img = base.copy()
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer, 'RGBA')
        # Soft contact shadow under the case.
        sx, sy = iso.p(L / 2, Wd / 2, 0)
        d.ellipse([sx - 230 * S, sy - 40 * S, sx + 230 * S, sy + 90 * S], fill=(0, 0, 0, 110))
        layer = layer.filter(ImageFilter.GaussianBlur(26 * S))
        d = ImageDraw.Draw(layer, 'RGBA')
        z_pcb, z_cover = device(d, iso, o, glow)
        a = ease((o - 0.6) / 0.4)
        label(d, font, iso.p(L, Wd / 2, z_cover + 1.6), '커버 (나사 4개)', a)
        label(d, font, iso.p(26, 15, z_pcb + 4.8), 'ESP32 Wi-Fi 모듈', a, side=-1)
        label(d, font, iso.p(52, 15, z_pcb + 2.6), 'PCB 안테나', a)
        label(d, font, iso.p(63, 20, z_pcb + 3.2), 'USB-C 전원', a)
        label(d, font, iso.p(18, Wd, 3.5), '케이스 68 x 31.8 x 9 mm', a, side=-1)
        img.alpha_composite(layer)
        img.convert('RGB').resize((W, H), Image.LANCZOS).save(tmp / f'f{f:04d}.png')
        if f == int(frames * 0.5):
            img.convert('RGB').resize((W, H), Image.LANCZOS).save(OUT / 'hero-poster.webp', quality=82, method=6)
    ff = shutil.which('ffmpeg')
    common = [ff, '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', str(tmp / 'f%04d.png')]
    subprocess.run(common + ['-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '23', '-preset', 'slow',
                             '-movflags', '+faststart', str(OUT / 'hero.mp4')], check=True)
    subprocess.run(common + ['-c:v', 'libvpx-vp9', '-b:v', '0', '-crf', '36', '-row-mt', '1', '-pix_fmt', 'yuv420p',
                             str(OUT / 'hero.webm')], check=True)


def drawings():
    # Crop each competition sheet to its exploded stack (fractions of the sheet).
    crops = {'lineup-s3': ('S3v2-1.png', (0.665, 0.20, 0.91, 0.70)),
             'lineup-c3': ('C3v1-1.png', (0.755, 0.17, 0.958, 0.62)),
             'lineup-d1': ('D1-V1-1.png', (0.755, 0.17, 0.955, 0.665))}
    for name, (src, (l, t, r, b)) in crops.items():
        img = Image.open(DRAWINGS / src).convert('RGB')
        w, h = img.size
        img = img.crop((int(l * w), int(t * h), int(r * w), int(b * h)))
        # Centre on a white 4:3 card so all models line up.
        cw = max(img.width, int(img.height * 4 / 3)); ch = int(cw * 3 / 4)
        card = Image.new('RGB', (int(cw * 1.08), int(ch * 1.08)), 'white')
        card.paste(img, ((card.width - img.width) // 2, (card.height - img.height) // 2))
        card.thumbnail((1200, 900), Image.LANCZOS)
        card.save(OUT / f'{name}.webp', quality=82, method=6)
    arch = Image.open(ROOT / 'results' / 'architecture_v2_ko.png').convert('RGB')
    arch.thumbnail((3200, 3200), Image.LANCZOS)
    arch.save(OUT / 'architecture.webp', quality=82, method=6)


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    drawings()
    if '--drawings-only' not in __import__('sys').argv:
        with tempfile.TemporaryDirectory() as tmp:
            render_hero(Path(tmp))
    for p in sorted(OUT.iterdir()):
        print(f'{p.name:22s} {p.stat().st_size // 1024:6d} KiB')
