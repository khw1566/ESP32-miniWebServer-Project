"""Bounded USB serial capture. Keep raw logs and credentials in ignored results/."""
import argparse
import json
import re
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM19')
parser.add_argument('--seconds', type=int, default=35)
parser.add_argument('--wifi', type=Path)
parser.add_argument('--scan', action='store_true')
parser.add_argument('--tx-low', action='store_true')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
password = ''
command = None
if args.wifi:
    settings = json.loads(args.wifi.read_text(encoding='utf-8'))
    password = settings['password']
    command = (settings['ssid'] + ':' + password + '\n').encode()
args.output.parent.mkdir(parents=True, exist_ok=True)
commands = ([b'/scan\n'] if args.scan else [])
if args.tx_low:
    commands.append(b'/tx-low\n')
if command:
    commands.append(command)
with serial.Serial(args.port, 115200, timeout=0.2) as port:
    port.dtr = False
    port.rts = False
    end = time.monotonic() + args.seconds
    captured = bytearray()
    prompt_start = 0
    while time.monotonic() < end:
        captured.extend(port.read(max(1, port.in_waiting)))
        fresh = captured[prompt_start:]
        ready = b'> ' in fresh or b'[RADIO] TX limit 15 dBm: ESP_OK' in fresh
        if commands and ready:
            port.write(commands.pop(0))
            port.flush()
            prompt_start = len(captured)
text = captured.decode('utf-8', errors='replace')
text = re.sub(r'\x1b\[[0-9;]*m', '', text)
if password:
    text = text.replace(password, '[REDACTED]')
args.output.write_text(text, encoding='utf-8')
for line in text.splitlines():
    if re.search(r'error|assert|panic|rst:|reset=|PSRAM|psram|IP:|IPv4|Product|Cloud|cloud|SoftAP|MAIN_APP|http|SPIFFS|Failed|SCAN|RADIO|STA_DISCONNECTED|시리얼', line):
        print(line)
