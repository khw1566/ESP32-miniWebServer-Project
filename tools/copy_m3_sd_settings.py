"""Copy the local ignored API settings to a mounted SD card without touching Wi-Fi."""
import argparse
import json
import os
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'mini-WEB (M3-ESP-IDF_ver)/sd_private/.private/api_keys.json'


def install(source: Path, sd_root: Path, replace: bool = False) -> Path:
    if not sd_root.is_dir():
        raise ValueError('SD root must be an existing mounted directory')
    raw = source.read_bytes()
    if len(raw) > 4096:
        raise ValueError('API settings file is too large')
    data = json.loads(raw)
    if not isinstance(data, dict) or data.get('version') != 1:
        raise ValueError('Invalid API settings version')
    for name in ('gemini', 'google_tts', 'openai'):
        key = data.get(name)
        if not isinstance(key, str) or not 20 <= len(key) <= 255 or any(not 33 <= ord(c) <= 126 for c in key):
            raise ValueError('Invalid API settings field: ' + name)
    azure = data.get('azure_speech')
    if azure is not None and (not isinstance(azure, str) or not 20 <= len(azure) <= 255 or any(not 33 <= ord(c) <= 126 for c in azure)):
        raise ValueError('Invalid Azure key field')
    region = data.get('azure_region')
    if region is not None and (not isinstance(region, str) or not 1 <= len(region) <= 31 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789' for c in region)):
        raise ValueError('Invalid Azure region field')
    folder = sd_root / '.private'
    folder.mkdir(exist_ok=True)
    destination = folder / 'api_keys.json'
    if destination.exists() and not replace:
        raise ValueError('API settings already exist; use --replace-keys to update them')
    temporary = folder / 'api_keys.copy.tmp'
    # Exclusive creation avoids overwriting a pre-existing interrupted copy.
    with temporary.open('xb') as handle:
        handle.write(raw)
        handle.flush()
        os.fsync(handle.fileno())
    try:
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path, help='Mounted SD card root, e.g. E:/')
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--replace-keys', action='store_true')
    args = parser.parse_args()
    try:
        target = install(args.source, args.sd_root, args.replace_keys)
    except (OSError, ValueError):
        # JSON decoder errors can include input; do not print exception details.
        parser.exit(1, 'Copy failed: check source format, SD path, existing settings and permissions. No key values printed.\n')
    print('API settings copied to ' + str(target) + '; Wi-Fi history retained. Reboot M3.')


if __name__ == '__main__':
    main()
