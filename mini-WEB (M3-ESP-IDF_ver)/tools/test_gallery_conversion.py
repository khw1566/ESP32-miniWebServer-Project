"""Verify conversion arguments without running FFmpeg or a compiler."""
import unittest
from prepare_gallery import command


class ConversionTests(unittest.TestCase):
    def test_video(self):
        args = command('holiday clip.mp4', 'holiday clip.mjpg')
        self.assertIn('-n', args)
        self.assertIn('-an', args)
        self.assertIn('fps=10,scale=432:480:force_original_aspect_ratio=decrease:force_divisible_by=2', args)
        self.assertEqual(args[-1], 'holiday clip.mjpg')
        self.assertEqual(args[args.index('-i') + 1], 'holiday clip.mp4')
        self.assertEqual(args[args.index('-f') + 1], 'mjpeg')

    def test_still_formats(self):
        for ext in ['heic', 'webp', 'tiff', 'png', 'jpeg', 'avif']:
            args = command('photo.' + ext, 'result.png')
            self.assertIn('-frames:v', args)
            self.assertNotIn('-c:v', args)

    def test_native_mp4(self):
        args = command('phone.mov', 'compatible.mp4', native_mp4=True)
        for option, value in [('-profile:v', 'baseline'), ('-pix_fmt', 'yuv420p'), ('-bf', '0'),
                              ('-refs', '1'), ('-c:a', 'aac'), ('-profile:a', 'aac_low'),
                              ('-ar', '44100'), ('-ac', '2'), ('-use_editlist', '0')]:
            self.assertEqual(args[args.index(option) + 1], value)
        self.assertIn('-n', args)
        self.assertNotIn('-an', args)
        self.assertIn('0:a:0?', args)
        self.assertIn('fps=15,scale=480:272:force_original_aspect_ratio=decrease:force_divisible_by=16', args)
        self.assertEqual(args[-1], 'compatible.mp4')
        for source, destination in [('a.mp4', 'a.mp4'), ('a.png', 'b.mp4'), ('a.mov', 'b.mjpg')]:
            with self.assertRaises(ValueError):
                command(source, destination, native_mp4=True)

    def test_gif_animation(self):
        self.assertIn('mjpeg', command('animation.gif', 'animation.mjpg'))

    def test_invalid(self):
        for source, destination in [('a.mp4', 'a.mp4'), ('a.exe', 'a.png'), ('a.mp4', 'a.png'), ('a.heic', 'a.mjpg')]:
            with self.assertRaises(ValueError):
                command(source, destination)

    def test_no_shell_splitting(self):
        args = command('photo $(literal).webp', 'result image.png', 'C:/Program Files/FFmpeg/ffmpeg.exe')
        self.assertEqual(args[0], 'C:/Program Files/FFmpeg/ffmpeg.exe')
        self.assertIn('photo $(literal).webp', args)


if __name__ == '__main__':
    unittest.main()
