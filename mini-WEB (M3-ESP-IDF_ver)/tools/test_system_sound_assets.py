"""Execute asset generation/installation checks with no API calls or real keys."""
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import wave
from generate_system_sounds import ROOT, TONES, tone_wav, valid_wav
from install_system_sounds import install


class SoundTests(unittest.TestCase):
    def test_original_tones_are_bounded_and_fade_at_note_boundaries(self):
        for name, notes in TONES.items():
            data = tone_wav(notes)
            self.assertTrue(valid_wav(data), name)
            with wave.open(io.BytesIO(data)) as wav:
                samples = struct.unpack('<' + 'h' * wav.getnframes(), wav.readframes(wav.getnframes()))
            self.assertEqual(len(samples), sum(24000 * ms // 1000 for _, ms in notes))
            self.assertGreater(max(samples), 3000)
            self.assertLessEqual(max(abs(v) for v in samples), 9000)
            offset = 0
            for _, ms in notes:
                self.assertEqual(samples[offset], 0)
                offset += 24000 * ms // 1000
                self.assertLess(abs(samples[offset - 1]), 40)

    def test_real_google_pack_has_all_languages_and_verified_pcm(self):
        base = ROOT / 'sd_assets/sounds/m3'
        manifest = json.loads((base / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(len(manifest), 69)
        for lang in ('ko', 'en', 'ja'):
            for cue in range(2, 25):
                name = f'{lang}/{cue}.wav'
                data = (base / name).read_bytes()
                self.assertTrue(valid_wav(data), name)
                self.assertEqual(hashlib.sha256(data).hexdigest(), manifest[name]['audio_sha256'])
                self.assertIn('Aoede', manifest[name]['voice'])

    def test_sd_install_preserves_wifi_and_keys(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            sd = Path(folder)
            private = sd / '.private'; private.mkdir()
            (private / 'api_keys.json').write_bytes(b'fake private key fixture')
            (private / 'wifi.json').write_bytes(b'fake Wi-Fi fixture')
            self.assertEqual(install(sd), 74)
            self.assertEqual((private / 'api_keys.json').read_bytes(), b'fake private key fixture')
            self.assertEqual((private / 'wifi.json').read_bytes(), b'fake Wi-Fi fixture')
            self.assertEqual(len(list((sd / 'sounds/m3').rglob('*.wav'))), 74)

    def test_invalid_wavs_are_rejected(self):
        self.assertFalse(valid_wav(b'not a WAV'))
        self.assertFalse(valid_wav(tone_wav([(400, 11000)])))
        self.assertFalse(valid_wav(tone_wav([(400, 0)])))


if __name__ == '__main__':
    unittest.main()
