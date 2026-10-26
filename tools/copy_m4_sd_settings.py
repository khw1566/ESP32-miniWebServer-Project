"""Install plain JSON settings on SD; Wi-Fi is copied only when explicitly selected."""
import argparse
import json
import os
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'mini-WEB (M4-ESP-IDF_ver)/sd_private/.private/api_keys.json'


def install(source: Path, sd_root: Path, replace: bool = False) -> Path:
    if not sd_root.is_dir():
        raise ValueError('SD root must be an existing mounted directory')
    raw = source.read_bytes()
    if len(raw) > 4096:
        raise ValueError('API settings file is too large')
    data = json.loads(raw)
    if not isinstance(data, dict) or data.get('version', 1) != 1:
        raise ValueError('Invalid API settings version')
    if not any(name in data for name in ('gemini', 'google_tts', 'openai', 'azure_speech', 'tube_token')):
        raise ValueError('No API settings fields')
    for name in ('gemini', 'google_tts', 'openai', 'tube_token'):
        if name not in data:
            continue
        key = data.get(name)
        if not isinstance(key, str) or not (len(key) == 0 or 16 <= len(key) <= 255) or any(not 33 <= ord(c) <= 126 for c in key):
            raise ValueError('Invalid API settings field: ' + name)
    azure = data.get('azure_speech')
    if azure is not None and (not isinstance(azure, str) or not (len(azure) == 0 or 16 <= len(azure) <= 255) or any(not 33 <= ord(c) <= 126 for c in azure)):
        raise ValueError('Invalid Azure key field')
    region = data.get('azure_region')
    if region is not None and (not isinstance(region, str) or not 1 <= len(region) <= 31 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789' for c in region)):
        raise ValueError('Invalid Azure region field')
    # Windows editors may save UTF-16/BOM JSON: firmware consumes plain UTF-8.
    raw = (json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
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


def install_wifi(source: Path, sd_root: Path, replace: bool = False) -> Path:
    if not sd_root.is_dir():
        raise ValueError('SD root must exist')
    raw = source.read_bytes()
    if len(raw) > 65536:
        raise ValueError('Wi-Fi JSON too large')
    data = json.loads(raw)
    if not isinstance(data, dict) or data.get('version', 1) != 1:
        raise ValueError('Invalid Wi-Fi JSON version')
    profiles = data.get('networks', [data])
    if not isinstance(profiles, list) or not profiles:
        raise ValueError('No Wi-Fi profiles')
    for profile in profiles:
        if not isinstance(profile, dict):
            raise ValueError('Invalid Wi-Fi profile')
        ssid, password = profile.get('ssid'), profile.get('password')
        if not isinstance(ssid, str) or not 1 <= len(ssid.encode('utf-8')) <= 32 or '\0' in ssid:
            raise ValueError('Invalid SSID')
        if not isinstance(password, str) or '\0' in password or not (len(password) == 0 or 8 <= len(password.encode('utf-8')) <= 63):
            raise ValueError('Invalid Wi-Fi password')
        if not isinstance(profile.get('static', False), bool):
            raise ValueError('Invalid address mode')
        if profile.get('static'):
            import ipaddress
            values = [ipaddress.IPv4Address(profile.get(key, '')) for key in ('ip', 'gateway', 'netmask', 'dns')]
            network = ipaddress.IPv4Network((str(values[0]), str(values[2])), strict=False)
            if network.prefixlen > 30 or network.prefixlen == 0 or values[1] not in network or values[0] == values[1]:
                raise ValueError('Invalid static network')
            if ipaddress.IPv4Address('192.168.4.1') in network:
                raise ValueError('Static network overlaps setup AP')
    folder = sd_root / '.private'; folder.mkdir(exist_ok=True)
    destination = folder / 'wifi.json'
    if destination.exists() and not replace:
        raise ValueError('Wi-Fi JSON exists; explicit replacement required')
    temporary = folder / 'wifi.copy.tmp'
    with temporary.open('xb') as handle:
        handle.write((json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
        handle.flush(); os.fsync(handle.fileno())
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
    parser.add_argument('--wifi-source', type=Path, help='Optional Wi-Fi JSON; omitted means existing Wi-Fi is untouched')
    parser.add_argument('--replace-wifi', action='store_true', help='Allow replacement of the explicitly selected Wi-Fi JSON')
    args = parser.parse_args()
    try:
        target = install(args.source, args.sd_root, args.replace_keys)
        if args.wifi_source:
            install_wifi(args.wifi_source, args.sd_root, args.replace_wifi)
    except (OSError, ValueError):
        # JSON decoder errors can include input; do not print exception details.
        parser.exit(1, 'Copy failed: check source format, SD path, existing settings and permissions. No key values printed.\n')
    print('UTF-8 API JSON installed at ' + str(target) + '; ' +
          ('selected Wi-Fi JSON installed' if args.wifi_source else 'existing Wi-Fi unchanged') + '. Reboot M4.')


if __name__ == '__main__':
    main()
