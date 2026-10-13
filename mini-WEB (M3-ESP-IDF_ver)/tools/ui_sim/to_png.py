"""Convert sim_main RGB565 frames to PNG."""
import sys
from pathlib import Path
from PIL import Image
for path in sorted(Path(sys.argv[1]).glob('*.rgb565')):
    data = path.read_bytes()
    image = Image.new('RGB', (480, 800))
    pixels = []
    for i in range(0, len(data), 2):
        v = data[i] | data[i + 1] << 8
        pixels.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    image.putdata(pixels)
    image.save(path.with_suffix('.png'))
    path.unlink()
