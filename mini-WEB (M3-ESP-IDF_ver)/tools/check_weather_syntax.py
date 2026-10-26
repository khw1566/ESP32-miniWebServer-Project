"""Read existing compile flags and check changed C sources without producing objects.

This is syntax-only evidence, never a firmware build or link check.
"""
from pathlib import Path
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
entries = json.loads((root / 'build/compile_commands.json').read_text(encoding='utf-8'))
base = next(e for e in entries if e['file'].replace('\\', '/').endswith('/main/m3_ui.c'))
flags = re.sub(r' -o .*? -c .*$', '', base['command'])
flags = re.sub(r' -(?:MD|MMD|MP)\b', '', flags)
flags = re.sub(r' -(?:MF|MT|MQ) (?:"[^"]*"|\S+)', '', flags)
failed = False
for source in ('m3_weather.c', 'm3_weather_json.c', 'm3_ui.c', 'm3_runtime.c', 'm3_i18n.c', 'm3_font_title.c'):
    command = flags + ' -fsyntax-only "' + str(root / 'main' / source) + '"'
    result = subprocess.run(command, cwd=base['directory'], capture_output=True, text=True)
    print(f'{source}: syntax exit {result.returncode}', flush=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr)
    failed |= result.returncode != 0
raise SystemExit(1 if failed else 0)
