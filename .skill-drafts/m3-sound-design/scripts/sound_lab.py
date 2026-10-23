"""Render, measure and audition M3 sound sets (tools/sound_packs.json) on a PC.

The synthesis mirrors note() in main/m3_feedback.c: 24 kHz, 10 ms edge fades,
sine/triangle/softened square/bell (octave partial and exponential decay),
amplitude 90 * gain. Standard library only; writes 16-bit mono WAV files.

  python sound_lab.py --project <firmware-dir> --out <preview-dir> [--pack retro]
"""
from pathlib import Path
import argparse
import json
import math
import struct
import wave

RATE = 24000
CUES = ['boot', 'tap', 'confirm', 'error', 'setup']
LIMITS = {'boot': (600, 1600), 'tap': (10, 40), 'confirm': (60, 400), 'error': (150, 500), 'setup': (150, 450)}


def sample(kind, phase):
    if kind == 'triangle':
        return 4.0 * abs(phase - 0.5) - 1.0
    if kind == 'square':
        return 0.6 if phase < 0.5 else -0.6
    base = math.sin(2 * math.pi * phase)
    return 0.75 * base + 0.25 * math.sin(4 * math.pi * phase) if kind == 'bell' else base


def render_note(hz, ms, kind, gain):
    total = RATE * ms // 1000
    edge = min(total // 4, 240)
    out, phase, step = [], 0.0, hz / RATE
    for n in range(total):
        distance = min(n, total - n)
        env = distance / edge if edge and distance < edge else 1.0
        if kind == 'bell':
            env *= math.exp(-3.0 * n / total)
        out.append(int(90.0 * gain * env * sample(kind, phase)) if hz else 0)
        phase += step
        if phase >= 1.0:
            phase -= 1.0
    return out


def render(notes):
    pcm = []
    for note in notes:
        pcm += render_note(*note)
    return pcm


def app_note(app, index):
    return [app['base'] + app['step'] * index, app['ms'], app['wave'], app['gain']]


def write_wav(path, pcm):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(struct.pack(f'<{len(pcm)}h', *pcm))


def measure(name, notes, pcm):
    ms = len(pcm) * 1000 // RATE
    peak = max((abs(v) for v in pcm), default=0)
    rms = math.sqrt(sum(v * v for v in pcm) / max(len(pcm), 1))
    tones = [n[0] for n in notes if n[0]]
    warnings = []
    low, high = LIMITS.get(name, (0, 800))
    if not low <= ms <= high:
        warnings.append(f'{ms} ms outside the suggested {low}-{high} ms')
    if tones and min(tones) < 300:
        warnings.append(f'{min(tones)} Hz is weak on the board speaker')
    if peak > 32000:
        warnings.append('near clipping')
    for a, b in zip(notes, notes[1:]):
        if a[0] and a[0] == b[0] and a[2] != 'bell':
            warnings.append(f'repeated {a[0]} Hz without a rest blurs into one note')
    db = 20 * math.log10(peak / 32767) if peak else -120
    return f'{name:8s} {ms:5d} ms  peak {db:6.1f} dBFS  rms {rms:6.0f}  {min(tones, default=0)}-{max(tones, default=0)} Hz' + \
        (('  WARN: ' + '; '.join(warnings)) if warnings else '')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--project', type=Path, default=Path.cwd(), help='firmware directory with tools/sound_packs.json')
    parser.add_argument('--out', type=Path, required=True, help='preview output directory (keep it outside the repo or in build/)')
    parser.add_argument('--pack', help='render one set only')
    args = parser.parse_args()
    spec = json.loads((args.project / 'tools/sound_packs.json').read_text(encoding='utf-8'))
    for pack in spec['packs']:
        if args.pack and pack['id'] != args.pack:
            continue
        print(f'[{pack["id"]}] {pack["name"]["en"]} / {pack["name"]["ko"]}')
        demo = []
        for cue in CUES:
            pcm = render(pack[cue])
            write_wav(args.out / pack['id'] / f'{cue}.wav', pcm)
            print('  ' + measure(cue, pack[cue], pcm))
            demo += pcm + [0] * (RATE * 45 // 100)
        apps = [app_note(pack['app'], i) for i in (0, 5, 10, 15)]
        app_pcm = []
        for note in apps:
            app_pcm += render_note(*note) + [0] * (RATE // 10)
        write_wav(args.out / pack['id'] / 'apps.wav', app_pcm)
        print('  ' + measure('apps', apps, app_pcm))
        write_wav(args.out / f'{pack["id"]}_demo.wav', demo + app_pcm)
    print(f'Previews: {args.out}')


if __name__ == '__main__':
    main()
