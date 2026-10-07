"""Generates the static WebP scenes for web_examples/12_m1_store.

Only the parts that never move are drawn here. index.html animates the rest
with CSS layers placed in the same coordinate space (scene width x height),
so coordinates in this file and in index.html must stay in sync.
Images are saved at 2x for sharp display on high-density screens.
Run: python tools/make_m1_store_webp.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "web_examples" / "12_m1_store"
S = 2
CLEAR = (0, 0, 0, 0)
BG = (14, 20, 28)
PANEL = (23, 32, 43)
LINE = (48, 64, 78)
TEXT = (236, 241, 245)
MUTED = (140, 156, 168)
BLUE = (39, 122, 199)
SKY = (120, 190, 255)
GREEN = (57, 139, 99)
MINT = (110, 225, 175)
AMBER = (210, 153, 50)
LIGHT = (241, 245, 250)
INK = (36, 50, 68)
PCB = (196, 160, 52)
WHITE = (250, 251, 253)


def font(size, bold=False):
    name = "malgunbd.ttf" if bold else "malgun.ttf"
    try:
        return ImageFont.truetype(f"C:/Windows/Fonts/{name}", int(size * S))
    except OSError:
        return ImageFont.load_default()


def canvas(w, h, bg=CLEAR):
    img = Image.new("RGBA", (w * S, h * S), bg)
    return img, ImageDraw.Draw(img)


def rr(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle([v * S for v in box], radius=r * S, fill=fill, outline=outline, width=width * S)


def text(d, xy, s, f, fill=TEXT, anchor="la"):
    d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)


def board(d, x, y, w, h):
    """Yellow PCB with mounting holes and a black LCD bezel."""
    rr(d, (x + 8, y + 10, x + w + 8, y + h + 12), 18, fill=(8, 12, 18))
    rr(d, (x, y, x + w, y + h), 16, fill=PCB)
    for hx, hy in ((x + 8, y + 8), (x + w - 18, y + 8), (x + 8, y + h - 18), (x + w - 18, y + h - 18)):
        d.ellipse([hx * S, hy * S, (hx + 10) * S, (hy + 10) * S], fill=(120, 96, 30))


def dashboard(d, x, y, w, ram=0.0):
    """The on-board home screen at scale w/240. Animated values are left neutral."""
    k = w / 240
    def sx(v): return x + v * k
    def sy(v): return y + v * k
    rr(d, (x, y, x + w, y + 320 * k), 3, fill=WHITE)
    text(d, (sx(120), sy(10)), "Mini WEB M1", font(11 * k, True), INK, "mm")
    for i, (label, value) in enumerate((("Wi-Fi", "-54 dBm"), ("WEB", "ON"))):
        bx = sx(4 + i * 120)
        rr(d, (bx, sy(24), bx + 112 * k, sy(62)), 4, fill=LIGHT)
        if i == 0:
            for b in range(4):
                bh = (b + 1) * 4
                rr(d, (bx + 6 * k + b * 5, sy(30) + 16 - bh, bx + 6 * k + b * 5 + 3, sy(30) + 16), 1, fill=(200, 210, 220))
        else:
            d.ellipse([(bx + 8 * k) * S, sy(36) * S, (bx + 20 * k) * S, sy(48) * S], fill=GREEN)
        text(d, (bx + 36 * k, sy(30)), label, font(10 * k), MUTED)
        text(d, (bx + 36 * k, sy(44)), value, font(10 * k, True), INK)
    for i, (label, used, color) in enumerate((("RAM", ram, BLUE), ("FLASH", 0.38, BLUE), ("SD", 0.24, AMBER))):
        ry = sy(70 + i * 30)
        rr(d, (sx(4), ry, sx(236), ry + 25 * k), 4, fill=LIGHT)
        text(d, (sx(10), ry + 6 * k), label, font(9 * k, True), INK)
        rr(d, (sx(70), ry + 9 * k, sx(228), ry + 16 * k), 3, fill=(220, 228, 236))
        if used:
            rr(d, (sx(70), ry + 9 * k, sx(70 + 158 * used), ry + 16 * k), 3, fill=color)
    for i in range(6):
        col, row = i % 2, i // 2
        bx, by = sx(4 + col * 120), sy(166 + row * 38)
        rr(d, (bx, by, bx + 112 * k, by + 32 * k), 4, fill=LIGHT)
        rr(d, (bx + 6 * k, by + 8 * k, bx + 22 * k, by + 24 * k), 3, fill=BLUE)
        rr(d, (bx + 30 * k, by + 13 * k, bx + (70 + 20 * (i % 3)) * k, by + 19 * k), 2, fill=(196, 206, 218))


def hero():
    # 480 x 440, transparent so the CSS signal rings can sit behind the board.
    img, d = canvas(480, 440)
    board(d, 110, 40, 260, 360)
    rr(d, (124, 62, 356, 378), 8, fill=(12, 14, 18))
    dashboard(d, 132, 70, 216)
    d.ellipse([346 * S, 384 * S, 356 * S, 394 * S], fill=(70, 24, 24))
    rr(d, (220, 392, 260, 412), 3, fill=(160, 160, 166))
    save("hero.webp", img)


def network():
    # 560 x 260. Packets, the board pulse and phone rows are CSS layers.
    img, d = canvas(560, 260, BG + (255,))
    d.line([80 * S, 130 * S, 460 * S, 130 * S], fill=LINE, width=3 * S)
    rr(d, (55, 82, 105, 178), 10, fill=PANEL, outline=SKY, width=2)
    rr(d, (62, 94, 98, 160), 4, fill=(32, 46, 60))
    for i in range(3):
        rr(d, (66, 100 + i * 18, 94 - i * 6, 110 + i * 18), 2, fill=LINE)
    rr(d, (236, 98, 324, 162), 8, fill=PCB)
    rr(d, (244, 104, 316, 156), 4, fill=WHITE)
    rr(d, (250, 110, 280, 120), 2, fill=BLUE)
    rr(d, (250, 126, 310, 132), 2, fill=(196, 206, 218))
    rr(d, (250, 138, 296, 144), 2, fill=(196, 206, 218))
    for cx, r in ((440, 22), (464, 28), (488, 20)):
        d.ellipse([(cx - r) * S, (134 - r) * S, (cx + r) * S, (134 + r) * S], fill=PANEL, outline=MINT, width=2 * S)
    rr(d, (424, 130, 504, 156), 8, fill=PANEL)
    d.line([424 * S, 156 * S, 504 * S, 156 * S], fill=MINT, width=2 * S)
    for x, label in ((80, "스마트폰"), (280, "M1 보드"), (460, "외부 도메인")):
        text(d, (x, 214), label, font(14, True), TEXT, "mm")
    text(d, (180, 104), "Wi-Fi / SoftAP", font(11), MUTED, "mm")
    text(d, (370, 104), "클라우드 중계", font(11), MUTED, "mm")
    save("network.webp", img)


def qr():
    # 520 x 300. The reveal wipe, scan line and "connected" badge are CSS layers.
    img, d = canvas(520, 300, BG + (255,))
    rr(d, (40, 30, 230, 270), 14, fill=PCB)
    rr(d, (50, 42, 220, 258), 8, fill=WHITE)
    size, cell, ox, oy = 21, 6, 72, 87
    cells = [[((x * 2654435761 ^ y * 40503 ^ (x * y) * 2246822519) >> 7) % 7 < 3 for x in range(size)] for y in range(size)]
    for x0, y0 in ((0, 0), (14, 0), (0, 14)):
        for y in range(7):
            for x in range(7):
                cells[y0 + y][x0 + x] = x in (0, 6) or y in (0, 6) or (2 <= x <= 4 and 2 <= y <= 4)
        for y in range(-1, 8):
            for x in range(-1, 8):
                if (x in (-1, 7) or y in (-1, 7)) and 0 <= x0 + x < size and 0 <= y0 + y < size:
                    cells[y0 + y][x0 + x] = False
    for y in range(size):
        for x in range(size):
            if cells[y][x]:
                d.rectangle([(ox + x * cell) * S, (oy + y * cell) * S, (ox + x * cell + cell) * S - 1, (oy + y * cell + cell) * S - 1], fill=INK)
    text(d, (135, 60), "접속 QR", font(13, True), INK, "mm")
    rr(d, (300, 50, 410, 250), 18, fill=PANEL, outline=SKY, width=2)
    rr(d, (310, 70, 400, 220), 6, fill=(30, 42, 56))
    text(d, (440, 120), "같은 Wi-Fi", font(12), MUTED)
    text(d, (440, 140), "고정 주소", font(12), MUTED)
    text(d, (440, 160), "외부 도메인", font(12), MUTED)
    save("qr.webp", img)


def storage():
    # 520 x 300. Usage arc, percentage and row reveal are CSS layers.
    img, d = canvas(520, 300, BG + (255,))
    cx = 100
    card = [(cx, 70), (cx + 64, 70), (cx + 80, 86), (cx + 80, 170), (cx, 170)]
    d.polygon([(x * S, y * S) for x, y in card], fill=(30, 40, 52), outline=SKY)
    for i in range(5):
        rr(d, (cx + 10 + i * 12, 76, cx + 18 + i * 12, 94), 1, fill=AMBER)
    text(d, (cx + 40, 140), "SD", font(18, True), TEXT, "mm")
    files = (("index.html", "12 KB", BLUE), ("gallery", "폴더", AMBER), ("promo.webp", "84 KB", GREEN),
             ("menu.json", "3 KB", BLUE), ("video.mp4", "1.9 MB", GREEN))
    for i, (name, size, color) in enumerate(files):
        y = 40 + i * 48
        rr(d, (210, y, 490, y + 38), 8, fill=PANEL, outline=LINE)
        rr(d, (222, y + 11, 238, y + 27), 3, fill=color)
        text(d, (250, y + 9), name, font(13, True))
        text(d, (478, y + 10), size, font(12), MUTED, "ra")
    save("storage.webp", img)


def saver():
    # 520 x 300. Screen dimming, RAM bar, state label and tap are CSS layers.
    img, d = canvas(520, 300, BG + (255,))
    rr(d, (40, 40, 220, 270), 14, fill=PCB)
    rr(d, (50, 52, 210, 258), 8, fill=(8, 10, 14))
    dashboard(d, 55, 57, 150, ram=0.5)
    text(d, (270, 50), "내부 RAM 여유", font(15, True))
    rr(d, (270, 82, 480, 110), 8, fill=PANEL, outline=LINE)
    text(d, (270, 172), "화면 끄기: 위젯과 그리기 버퍼 해제", font(12), MUTED)
    text(d, (270, 194), "웹서버와 Wi-Fi는 계속 동작", font(12), MUTED)
    text(d, (270, 236), "화면을 누르면 즉시 복귀", font(12, True), SKY)
    save("saver.webp", img)


def save(name, img):
    path = OUT / name
    img.save(path, quality=82, method=6, alpha_quality=90)
    print(f"{name}: {path.stat().st_size // 1024} KiB")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*-still.webp"):
        old.unlink()
    hero(); network(); qr(); storage(); saver()
