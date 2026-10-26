"""Create original tones and Google female voice guidance for the M4 SD card.

Credentials are read only from the ignored SD settings. Never print API replies.
Existing matching WAVs are reused; --tones-only makes no network requests.
"""
import argparse
import base64
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import urllib.error
import urllib.request
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 24000
MELODY = [(523.251, 140), (659.255, 140), (783.991, 180), (1046.502, 240), (987.767, 120), (1046.502, 400)]
TONES = {'boot': MELODY, 'tap': [(880, 35)], 'confirm': [(659.255, 90), (987.767, 150)],
         'error': [(330, 110), (220, 180)], 'wifi_setup': [(440, 110), (660, 110), (440, 160)]}


def tone_wav(notes):
    pcm = bytearray()
    for frequency, ms in notes:
        total = RATE * ms // 1000
        for n in range(total):
            envelope = min(1.0, min(n, total - n) / 240.0)
            value = int(9000 * envelope * math.sin(2 * math.pi * frequency * n / RATE))
            pcm += struct.pack('<h', value)
    output = io.BytesIO()
    with wave.open(output, 'wb') as audio:
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(RATE)
        audio.writeframes(pcm)
    return output.getvalue()


def valid_wav(data):
    if len(data) > 512 * 1024:
        return False
    try:
        with wave.open(io.BytesIO(data)) as audio:
            frames = audio.getnframes()
            return (audio.getnchannels() == 1 and audio.getsampwidth() == 2 and
                    audio.getframerate() == RATE and 0 < frames <= RATE * 10 and
                    len(audio.readframes(frames)) == frames * 2)
    except (wave.Error, EOFError):
        return False


def generate(output, keys_path, tones_only=False):
    base = output / 'sounds/m4'
    (base / 'tones').mkdir(parents=True, exist_ok=True)
    for name, notes in TONES.items():
        (base / 'tones' / (name + '.wav')).write_bytes(tone_wav(notes))
    print('Created 5 original PCM16 system tones.', flush=True)
    if tones_only:
        return
    catalog = json.loads((ROOT / 'tools/sound_catalog.json').read_text(encoding='utf-8'))
    key = json.loads(keys_path.read_text(encoding='utf-8'))['google_tts']
    if not isinstance(key, str) or not 20 <= len(key) <= 255:
        raise ValueError('Invalid Google key')
    manifest_path = base / 'manifest.json'
    try:
        old = json.loads(manifest_path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        old = {}
    manifest = dict(old)
    for lang, voice in catalog['voices'].items():
        (base / lang).mkdir(exist_ok=True)
        for cue in catalog['cues']:
            payload = {'input': {'text': cue['text'][lang]}, 'voice': voice,
                       'audioConfig': {'audioEncoding': 'LINEAR16', 'sampleRateHertz': RATE}}
            encoded = json.dumps(payload, ensure_ascii=False).encode('utf-8')
            digest = hashlib.sha256(encoded).hexdigest()
            relative = lang + '/' + str(cue['id']) + '.wav'
            path = base / relative
            if manifest.get(relative, {}).get('request_sha256') == digest and path.is_file() and valid_wav(path.read_bytes()):
                continue
            request = urllib.request.Request('https://texttospeech.googleapis.com/v1/text:synthesize',
                data=encoded, headers={'Content-Type': 'application/json; charset=utf-8', 'x-goog-api-key': key}, method='POST')
            with urllib.request.urlopen(request, timeout=45) as response:
                raw = response.read(1024 * 1024)
            data = base64.b64decode(json.loads(raw)['audioContent'], validate=True)
            if not valid_wav(data):
                raise ValueError('Unexpected audio format')
            path.write_bytes(data)
            manifest[relative] = {'voice': voice['name'], 'request_sha256': digest,
                                  'audio_sha256': hashlib.sha256(data).hexdigest(), 'text': cue['text'][lang]}
            manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('Generated ' + relative + ' (' + cue['name'] + ')', flush=True)
    print('Voice pack complete: ' + str(len(manifest)) + ' clips. Copy sounds/ to the SD root.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'sd_assets')
    parser.add_argument('--keys', type=Path, default=ROOT / 'sd_private/.private/api_keys.json')
    parser.add_argument('--tones-only', action='store_true')
    args = parser.parse_args()
    try:
        generate(args.output, args.keys, args.tones_only)
    except urllib.error.HTTPError as exc:
        parser.exit(1, f'Google TTS HTTP {exc.code}; check key, API enablement, restrictions and billing. Response hidden.\n')
    except (OSError, ValueError, KeyError):
        parser.exit(1, 'Sound generation failed; check local settings, network and output path. Credentials/replies hidden.\n')


if __name__ == '__main__':
    main()
