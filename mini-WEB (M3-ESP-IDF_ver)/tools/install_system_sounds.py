"""Install the generated M3 sound pack without modifying personal settings."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from generate_system_sounds import ROOT, valid_wav


def install(sd_root: Path, source: Path = ROOT / 'sd_assets/sounds/m3'):
    if not sd_root.is_dir():
        raise ValueError('SD root must exist')
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    expected = {f'{lang}/{cue}.wav' for lang in ('en', 'ko', 'ja') for cue in range(2, 25)}
    if set(manifest) != expected:
        raise ValueError('Incomplete voice pack')
    files = list(expected) + ['tones/' + name + '.wav' for name in ('boot', 'tap', 'confirm', 'error', 'wifi_setup')]
    # Validate the entire source before changing the SD card.
    for name in files:
        data = (source / name).read_bytes()
        if not valid_wav(data):
            raise ValueError('Invalid WAV')
        if name in manifest and hashlib.sha256(data).hexdigest() != manifest[name]['audio_sha256']:
            raise ValueError('Audio checksum mismatch')
    target = sd_root / 'sounds/m3'
    if target.resolve() == source.resolve():
        raise ValueError('Source and destination must differ')
    # Never traverse symlinked directories into another location.
    for directory in (sd_root / 'sounds', target, *(target / lang for lang in ('en', 'ko', 'ja', 'tones'))):
        if directory.is_symlink():
            raise ValueError('Symlinked destination rejected')
    for name in files + ['manifest.json']:
        path = target / name
        if path.is_symlink():
            raise ValueError('Symlinked destination rejected')
    for name in files + ['manifest.json']:
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, path)
    return len(files)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    args = parser.parse_args()
    try:
        count = install(args.sd_root)
    except (OSError, ValueError, KeyError):
        parser.exit(1, 'Install failed: check sound pack, SD path and permissions. Private settings were not modified.\n')
    print(f'Installed {count} WAV clips. Private API keys and Wi-Fi history retained. Reboot M3.')
