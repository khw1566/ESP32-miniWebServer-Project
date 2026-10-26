"""Host checks for output compatibility and non-destructive batch conversion."""
import hashlib
from pathlib import Path
import struct
import tempfile
import threading
import unittest

from PIL import Image, JpegImagePlugin
from esp32_image_converter import Options, convert, encode


class ConverterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / '원본'
        self.source.mkdir()
        self.photo = self.source / '사진.png'
        Image.new('RGBA', (1200, 800), (255, 0, 0, 255)).save(self.photo)

    def test_jpeg_contract_and_bmp_header(self):
        data, size = encode(self.photo, Options())
        self.assertEqual(size, (432, 288))
        import io
        with Image.open(io.BytesIO(data)) as jpg:
            jpg.load()
            self.assertEqual(jpg.mode, 'RGB')
            self.assertFalse(jpg.info.get('progressive'))
            self.assertEqual(JpegImagePlugin.get_sampling(jpg), 2)
            self.assertFalse(jpg.getexif())
        bmp, size = encode(self.photo, Options(format='bmp', mode='pad'))
        self.assertEqual(size, (432, 480))
        self.assertEqual(bmp[:2], b'BM')
        self.assertEqual(struct.unpack_from('<H', bmp, 28)[0], 24)
        self.assertEqual(struct.unpack_from('<I', bmp, 30)[0], 0)
        self.assertLess(len(bmp), 4 * 1024 * 1024)

    def test_orientation_alpha_frames_and_resize(self):
        rotated = self.source / 'rotate.jpg'
        exif = Image.Exif(); exif[274] = 6
        Image.new('RGB', (100, 50), 'red').save(rotated, exif=exif)
        self.assertEqual(encode(rotated, Options())[1], (50, 100))
        transparent = self.source / 'alpha.png'
        Image.new('RGBA', (20, 10), (255, 0, 0, 0)).save(transparent)
        data, _ = encode(transparent, Options(format='bmp', mode='pad'))
        import io
        with Image.open(io.BytesIO(data)) as image:
            self.assertEqual(image.getpixel((216, 240)), (0, 0, 0))
        self.assertEqual(encode(self.photo, Options(mode='crop'))[1], (432, 480))
        animated = self.source / 'animated.gif'
        Image.new('RGB', (32, 32), 'red').save(animated, save_all=True,
                                              append_images=[Image.new('RGB', (32, 32), 'blue')])
        data, _ = encode(animated, Options(format='webp'))
        with Image.open(io.BytesIO(data)) as image:
            self.assertFalse(getattr(image, 'is_animated', False))
            self.assertGreater(image.getpixel((0, 0))[0], 200)

    def test_batch_collision_errors_repeated_run_and_cancel(self):
        Image.new('RGB', (10, 10)).save(self.source / '사진.bmp')
        nested = self.source / '하위'; nested.mkdir()
        Image.new('RGB', (10, 10)).save(nested / 'other.jpg')
        (self.source / 'broken.png').write_bytes(b'broken')
        before = hashlib.sha256(self.photo.read_bytes()).digest()
        output = self.source / 'esp32_images'
        result = convert(self.source, output, Options())
        self.assertEqual((result['converted'], result['failed']), (3, 1))
        self.assertTrue((output / '사진.jpg').exists())
        self.assertTrue((output / '사진_2.jpg').exists())
        self.assertTrue((output / '하위' / 'other.jpg').exists())
        saved = {p: p.read_bytes() for p in output.rglob('*.jpg')}
        again = convert(self.source, output, Options(quality=20))
        self.assertEqual((again['converted'], again['skipped']), (0, 3))
        self.assertEqual(saved, {p: p.read_bytes() for p in output.rglob('*.jpg')})
        self.assertEqual(before, hashlib.sha256(self.photo.read_bytes()).digest())
        cancel = threading.Event(); cancel.set()
        self.assertTrue(convert(self.source, self.root / 'cancelled', Options(), cancel=cancel)['cancelled'])
        single = convert(self.photo, self.root / 'single', Options())
        self.assertEqual(single['converted'], 1)
        flat = convert(self.source, self.root / 'flat', Options(), recursive=False)
        self.assertEqual(flat['converted'], 2)

    def test_limits_and_source_protection(self):
        for options in [Options(width=0), Options(width=2048, height=2048), Options(quality=96)]:
            with self.assertRaises(ValueError):
                options.validate()
        for target in [self.source, self.root]:
            with self.assertRaises(ValueError):
                convert(self.source, target, Options())


if __name__ == '__main__':
    unittest.main()
