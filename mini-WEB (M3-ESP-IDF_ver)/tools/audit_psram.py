"""Inspect an existing ESP32-P4 GNU map; never invokes a compiler or device."""
import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path

RECORD = re.compile(
    r"^[ \t]+(?P<section>\.(?:bss|sbss|ext_ram\.bss)(?:\.[^\s]+)?|COMMON)"
    r"[ \t]*(?:\r?\n[ \t]*)?(?P<address>0x[\da-fA-F]+)[ \t]+"
    r"(?P<size>0x[\da-fA-F]+)[ \t]+(?P<archive>[^\r\n]*?\.a)\((?P<object>[^)\r\n]+)\)",
    re.MULTILINE,
)


def inspect_map(text):
    rows = []
    totals = {}
    for match in RECORD.finditer(text):
        address, size = int(match['address'], 16), int(match['size'], 16)
        if not address or not size:  # Discarded input sections are not allocations.
            continue
        region = ('psram' if 0x48000000 <= address < 0x4C000000 else
                  'internal' if (0x4FF00000 <= address < 0x50000000 or
                                 0x30100000 <= address < 0x30200000 or
                                 0x50100000 <= address < 0x50200000) else 'other')
        archive = match['archive'].replace('\\', '/').rsplit('/', 1)[-1]
        row = dict(section=match['section'], address=hex(address), bytes=size,
                   archive=archive, object=match['object'], region=region)
        rows.append(row)
        bucket = totals.setdefault(archive, dict(internal=0, psram=0, other=0))
        bucket[region] += size
    engine = totals.get('libespeak_ng.a', dict(internal=0, psram=0, other=0))
    ok = engine['psram'] > 0 and not engine['internal'] and not engine['other']
    return dict(espeak_bss=engine, espeak_bss_in_psram=ok, libraries=totals, sections=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--allow-internal', action='store_true', help='Inspect an old baseline without a failing exit code')
    args = parser.parse_args()
    data = args.map.read_bytes()
    report = inspect_map(data.decode('utf-8', errors='replace'))
    report['map'] = dict(path=str(args.map.resolve()), sha256=hashlib.sha256(data).hexdigest(),
                         modified_utc=datetime.fromtimestamp(args.map.stat().st_mtime, timezone.utc).isoformat())
    report['scope'] = 'Existing map placement only; not a new build, runtime heap measurement or ELF/device identity check'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    engine = report['espeak_bss']
    print(f"eSpeak BSS: internal={engine['internal']} bytes, PSRAM={engine['psram']} bytes, other={engine['other']} bytes")
    print('PASS: eSpeak BSS is mapped to PSRAM' if report['espeak_bss_in_psram'] else
          'CHECK FAILED: eSpeak BSS is internal or missing; verify a fresh reconfigured map')
    print(report['scope'])
    return 0 if args.allow_internal or report['espeak_bss_in_psram'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
