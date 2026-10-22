"""Measure app Flash usage by function from the link map and patch it into the ELF.

Run automatically after linking (cmake/flash_usage.cmake), before the .bin is made:
    python tools/flash_usage.py build/app.map --elf build/app.elf
Report only:
    python tools/flash_usage.py build/app.map
Categories and their order come from main/include/m3_flash_usage.h (M3_FLASH_PARTS).
"""
from pathlib import Path
import argparse
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'main/include/m3_flash_usage.h'
MAGIC = b'M3-FLASH-USAGE1\0'
BLOB_PARTS = 32
# Output sections stored in the app image (the rest is bss/noload/debug).
IMAGE_SECTIONS = {'.rtc.text', '.rtc.data', '.spm.text', '.spm.data', '.iram0.text', '.iram0.data',
                  '.dram0.data', '.flash.text', '.flash.appdesc', '.flash.rodata', '.flash.tdata'}

APP_OBJECTS = {
    'APP_UI': ['m3_ui'],
    'APP_WEB': ['web_server', 'web_page_static', 'web_page_admin', 'web_page_ai', 'web_page_factory',
                'cloud_relay', 'dns_server', 'qr_printer', 'qrcode', 'storage_view', 'sd_media', 'https_memory'],
    'APP_AI': ['m3_agent', 'm3_voice', 'm3_google_audio', 'm3_azure_audio', 'm3_speech_text'],
    'APP_TTS': ['m3_tts'],
    'APP_AUDIO': ['m3_music', 'm3_radio', 'm3_pcm', 'm3_wav', 'm3_audio'],
    'APP_VIDEO': ['m3_tube', 'm3_mp4', 'm3_mp4_demux', 'm3_mp4_pixels'],
    'APP_PHOTO': ['m3_photos', 'm3_photo_pixels', 'm3_gallery'],
    'APP_READER': ['m3_reader', 'm3_font_reader'],
    'APP_TOOLS': ['m3_2048', 'm3_crossing', 'm3_calculator', 'm3_calendar', 'm3_recorder'],
    'APP_LUA': ['m3_sd_apps'],
    'AST_VOICE': ['m3_tts_data'],
    'AST_FONT_KO': ['m3_font_korean'],
    'AST_FONT_CJK': ['m3_font_cjk'],
    'AST_FONT_UI': ['m3_font_title', 'm3_font_clock', 'm3_font_large', 'm3_font_splash', 'm3_font_splash_small'],
    'AST_WEB': ['web_pages_gzip', 'm3_web_assets'],
    'AST_BOOT': ['m3_boot_splash'],
    'AST_TEXT': ['m3_i18n'],
}
OBJECT_PART = {name: part for part, names in APP_OBJECTS.items() for name in names}
ARCHIVE_PART = [
    ('APP_TTS', ['libespeak_ng.a']),
    ('APP_AUDIO', ['libesp_audio_codec.a', 'libesp_audio_simple_dec.a', 'libespressif__esp_audio_codec.a',
                   'libespressif__esp_codec_dev.a', 'libesp_driver_i2s.a']),
    ('APP_VIDEO', ['libtinyh264.a', 'libespressif__esp_h264.a', 'libesp_h264.a']),
    ('APP_PHOTO', ['libesp_driver_jpeg.a']),
    ('APP_LUA', ['libm3_lua.a']),
    ('LIB_LVGL', ['liblvgl__lvgl.a', 'liblvgl_port_lib.a', 'libespressif__esp_lvgl_port.a', 'libesp_lcd.a',
                  'libespressif__esp_lcd_touch.a', 'libespressif__esp_lcd_touch_gt911.a']),
    ('LIB_NET', ['liblwip.a', 'libesp_netif.a', 'libespressif__esp_hosted.a', 'libespressif__esp_wifi_remote.a',
                 'libwpa_supplicant.a', 'libespressif__mdns.a', 'libesp_http_server.a', 'libesp_http_client.a',
                 'libhttp_parser.a', 'libtcp_transport.a', 'libesp_wifi.a', 'libnet80211.a', 'libpp.a',
                 'libesp_event.a', 'libespressif__eppp_link.a']),
    ('LIB_TLS', ['libmbedtls.a', 'libmbedcrypto.a', 'libmbedx509.a', 'libesp-tls.a', 'libp256m.a',
                 'libeverest.a', 'libesp_security.a']),
    ('LIB_LIBC', ['libc.a', 'libm.a', 'libgcc.a', 'libstdc++.a', 'libnewlib.a', 'libg.a', 'libc_nano.a']),
]
ARCHIVE_LOOKUP = {archive: part for part, archives in ARCHIVE_PART for archive in archives}

OUTPUT_SECTION = re.compile(r'^(\.[\w.]+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+))?\s*$')
INPUT_ENTRY = re.compile(r'^ (\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
INPUT_NAME = re.compile(r'^ ([.\w][^\s]*)\s*$')
FILL = re.compile(r'^ \*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
RELAX = re.compile(r'^\s+0x([0-9a-f]+) \(size before relaxing\)')


def part_ids():
    text = HEADER.read_text(encoding='utf-8')
    body = text[text.index('#define M3_FLASH_PARTS(X)'):]
    ids = re.findall(r'^\s+X\((\w+),', body[:body.index('\n\n')], re.M)
    assert ids and len(ids) <= BLOB_PARTS and len(set(ids)) == len(ids), 'bad M3_FLASH_PARTS'
    return ids


def classify(source, section):
    match = re.search(r'([^/\\]+\.a)\(([^)]+)\)$', source)
    archive, obj = (match.group(1), match.group(2)) if match else ('', re.split(r'[/\\]', source)[-1])
    stem = re.sub(r'\.(c|cpp|S)?\.?(obj|o)$', '', obj)
    if archive == 'libmain.a':
        return OBJECT_PART.get(stem, 'CORE_MAIN')
    if archive == 'libmbedtls.a' and stem == 'x509_crt_bundle':
        return 'AST_CERTS'
    if archive == 'liblvgl__lvgl.a':
        if stem.startswith('lv_font_montserrat') or stem.startswith('lv_font_unscii'):
            return 'AST_FONT_UI'
        if stem == 'lodepng':
            return 'APP_PHOTO'
    if archive in ARCHIVE_LOOKUP:
        return ARCHIVE_LOOKUP[archive]
    if archive.startswith('libesp_wifi') or archive.startswith('libespressif__esp_wifi'):
        return 'LIB_NET'
    if archive.startswith('libmbed'):
        return 'LIB_TLS'
    return 'LIB_IDF'


def measure(map_path):
    lines = Path(map_path).read_text(encoding='utf-8', errors='replace').split('\n')
    start = next(i for i, line in enumerate(lines) if line.startswith('Linker script and memory map'))
    sizes, image, ends = {}, 0, {}
    out, pending, last = None, None, None

    def add(part, size):
        sizes[part] = sizes.get(part, 0) + size

    def claim(addr, size):
        """Bytes of [addr, addr+size) not already counted (merged strings repeat addresses)."""
        prev = ends.get(out, 0)
        ends[out] = max(prev, addr + size)
        return max(0, addr + size - max(prev, addr))

    for line in lines[start:]:
        match = OUTPUT_SECTION.match(line)
        if match:
            out, pending, last = match.group(1), None, None
            if out in IMAGE_SECTIONS and match.group(3):
                image += int(match.group(3), 16)
            continue
        if out not in IMAGE_SECTIONS:
            continue
        match = RELAX.match(line)
        if match and last:
            part, size, section = last
            own = int(match.group(1), 16)
            # One .str1 entry holds every merged string literal of the program.
            if '.str1.' in section and size > own + 4096:
                add(part, -size + own)
                add('AST_STRINGS', size - own)
            last = None
            continue
        match = FILL.match(line)
        if match:
            add('OTHER', claim(int(match.group(1), 16), int(match.group(2), 16)))
            last = None
            continue
        match = INPUT_ENTRY.match(line)
        if match:
            section = match.group(1) or pending or ''
            source = match.group(4).strip()
            pending, last = None, None
            if source.startswith('0x') or source.startswith('(') or ' = ' in source:
                continue
            size = claim(int(match.group(2), 16), int(match.group(3), 16))
            if size:
                part = classify(source, section)
                add(part, size)
                last = (part, size, section)
            continue
        match = INPUT_NAME.match(line)
        if match and not line.startswith(' *'):
            pending = match.group(1)
    counted = sum(sizes.values())
    if image > counted:
        add('OTHER', image - counted)
    return image, sizes


def blob(ids, image, sizes):
    unknown = set(sizes) - set(ids)
    assert not unknown, f'categories missing from header: {unknown}'
    values = [max(0, sizes.get(name, 0)) for name in ids] + [0] * (BLOB_PARTS - len(ids))
    return MAGIC + struct.pack('<3I', 1, image, len(ids)) + struct.pack(f'<{BLOB_PARTS}I', *values)


def patch(elf_path, data):
    raw = bytearray(Path(elf_path).read_bytes())
    first = raw.find(MAGIC)
    if first < 0 or raw.find(MAGIC, first + 1) >= 0:
        raise SystemExit(f'flash_usage: expected exactly one usage table in {elf_path}')
    if raw[first:first + len(data)] != data:
        raw[first:first + len(data)] = data
        Path(elf_path).write_bytes(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('map')
    parser.add_argument('--elf', help='patch the measured table into this ELF')
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args()
    ids = part_ids()
    image, sizes = measure(args.map)
    if args.elf:
        patch(args.elf, blob(ids, image, sizes))
    if not args.quiet:
        print(f'App image {image:,} bytes')
        for name in sorted(ids, key=lambda n: -sizes.get(n, 0)):
            size = sizes.get(name, 0)
            print(f'  {name:14s} {size:>10,}  {100 * size / max(image, 1):5.1f}%')
    return 0


if __name__ == '__main__':
    sys.exit(main())
