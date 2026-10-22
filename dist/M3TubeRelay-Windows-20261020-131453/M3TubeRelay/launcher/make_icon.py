"""Draws launcher/icon.ico (red rounded tile with a white play triangle)."""
from pathlib import Path
from PIL import Image, ImageDraw

size = 256
image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((8, 36, 248, 220), radius=56, fill=(220, 38, 38, 255))
draw.polygon([(100, 84), (100, 172), (176, 128)], fill=(255, 255, 255, 255))
image.save(Path(__file__).with_name('icon.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
