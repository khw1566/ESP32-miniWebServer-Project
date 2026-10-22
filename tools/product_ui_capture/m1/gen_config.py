from pathlib import Path
import sys
lines=['#pragma once']
for line in Path(sys.argv[1]).read_text(encoding='utf-8').splitlines():
 if not line.startswith('CONFIG_') or '=' not in line: continue
 key,value=line.split('=',1)
 if not (key.startswith('CONFIG_LV_') or key.startswith('CONFIG_M1_')): continue
 lines.append(f'#define {key} {1 if value == "y" else value}')
lines += ['#define LV_MEM_CUSTOM 0','#define LV_MEM_SIZE (4 * 1024 * 1024)']
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n',encoding='utf-8')
