"""Reuse installed M3 flags for M4 syntax only; does not configure/build/link."""
import argparse
import ctypes
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(flags_project: Path, jd_driver: Path, sources=None):
    records = json.loads((flags_project / 'build/compile_commands.json').read_text(encoding='utf-8'))
    entry = next(e for e in records if e['file'].replace('\\', '/').endswith('/m3_board.c'))
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    pointer = parse(entry['command'], ctypes.byref(count))
    if not pointer:
        raise ValueError('Cannot parse the installed compile command')
    args = [pointer[i] for i in range(count.value)]
    ctypes.windll.kernel32.LocalFree(pointer)
    output = ROOT / 'results/syntax-config'
    output.mkdir(parents=True, exist_ok=True)
    config = (flags_project / 'build/config/sdkconfig.h').read_text(encoding='utf-8').replace('M3_', 'M4_')
    config = re.sub(r'^#define CONFIG_M4_TOUCH_RESET .*$', '#define CONFIG_M4_TOUCH_RESET -1', config, flags=re.M)
    for key, value in {'M4_LCD_RESET':0,'M4_LCD_BACKLIGHT':23,'M4_PANEL_V2':1,'M4_I2S_MCLK':13,
                       'M4_I2S_BCLK':12,'M4_I2S_LRCK':10,'M4_I2S_DOUT':9,'M4_I2S_DIN':48}.items():
        config += f'\n#define CONFIG_{key} {value}\n'
    (output / 'sdkconfig.h').write_text(config, encoding='utf-8')
    base = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if arg in ('-c', '-MD', '-MMD', '-MP') or arg.replace('\\','/').endswith('/main/m3_board.c'):
            continue
        if arg.startswith('-I') and any(str(flags_project / name).replace('\\','/') in arg.replace('\\','/')
                                       for name in ('build/config', 'main/include')):
            continue
        base.append(arg)
    base[1:1] = ['-I' + str(output), '-I' + str(ROOT / 'main/include'), '-I' + str(jd_driver)]
    results = []
    selected = sources or ('m4_board.c', 'm4_panel_init.c', 'm4_layout.c', 'm4_ui.c', 'm4_boot_splash.c',
                           'm4_power.c', 'm4_sd_settings.c', 'm4_audio.c', 'main.c')
    for name in selected:
        if Path(name).name != name or not name.endswith('.c'):
            raise ValueError('Select a C source filename in main/')
        command = base + ['-fsyntax-only', str(ROOT / 'main' / name)]
        try:
            proc = subprocess.run(command, cwd=entry['directory'], capture_output=True,
                                  text=True, encoding='utf-8', errors='replace')
            code, diagnostic = proc.returncode, proc.stdout + proc.stderr
        except OSError as error:
            code, diagnostic = -1, str(error)
        results.append({'source': name, 'exit': code, 'command': command, 'diagnostics': diagnostic})
        print(name, 'PASS' if code == 0 else 'FAIL')
        if code:
            print(diagnostic[-3000:])
    record_path = ROOT / 'results/syntax-checks.json'
    if sources and record_path.exists():
        previous = json.loads(record_path.read_text(encoding='utf-8'))['checks']
        results = [item for item in previous if item['source'] not in sources] + results
    record = {'evidence': 'borrowed M3 flags/SDK configuration with M4 board defines; syntax only, no M4 configuration or link validation',
              'checks': results}
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    return int(any(item['exit'] for item in results))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flags-project', required=True, type=Path)
    parser.add_argument('--jd-driver', required=True, type=Path, help='Installed JD9165 include directory')
    parser.add_argument('--source', action='append', help='Check only this main/ C source; may repeat')
    options = parser.parse_args()
    raise SystemExit(check(options.flags_project.resolve(), options.jd_driver.resolve(), options.source))
