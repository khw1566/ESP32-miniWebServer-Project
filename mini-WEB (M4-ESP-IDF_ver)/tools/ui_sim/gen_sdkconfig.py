"""Turn the project sdkconfig into a header for the host LVGL build."""
import sys
from pathlib import Path
lines = ['#pragma once']
for raw in Path(sys.argv[1]).read_text(encoding='utf-8').splitlines():
    if not raw.startswith('CONFIG_') or '=' not in raw:
        continue
    key, value = raw.split('=', 1)
    if not key.startswith('CONFIG_LV_') or key == 'CONFIG_LV_USE_CLIB_MALLOC':
        continue
    lines.append(f'#define {key} {1 if value == "y" else value}')
# The builtin pool lets the preview report how much LVGL heap the UI uses.
lines += ['#define CONFIG_LV_USE_BUILTIN_MALLOC 1', '#define CONFIG_LV_MEM_SIZE_KILOBYTES 8192',
          '#define CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES 0']
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n', encoding='utf-8')
