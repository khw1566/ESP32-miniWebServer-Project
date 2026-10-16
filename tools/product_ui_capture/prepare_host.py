from pathlib import Path
import sys,re,json,hashlib
repo=Path(__file__).resolve().parents[2]
model=sys.argv[1]
out=Path(sys.argv[2]);project=repo/f'mini-WEB ({model}-ESP-IDF_ver)'
if model=='M3':
 sim=project/'tools/ui_sim'
 source=(sim/'sim_main.c').read_text(encoding='utf-8')
 source=source.replace('#include "../../main/m3_ui.c"', 'void *heap_caps_calloc(size_t count, size_t size, unsigned caps);\n#include "../../main/m3_ui.c"')
 source+='\nvoid *heap_caps_calloc(size_t count, size_t size, unsigned caps) { (void)caps; return calloc(count, size); }\n'
 source=source.replace('#include "../../main/m3_ui.c"',f'#include "{(project/"main/m3_ui.c").as_posix()}"')
 (out/'sim_main.c').write_text(source,encoding='utf-8')
 scenarios=(sim/'scenarios.inc').read_text(encoding='utf-8')
 # Latest UI removed the old boot overlay; skip only that obsolete scenario.
 scenarios=scenarios.replace('    if (boot_overlay) { lv_obj_delete(boot_overlay); boot_overlay = NULL; }','')
 (out/'scenarios.inc').write_text(scenarios,encoding='utf-8')
else:
 source=(project/'main/qrcode.c').read_text(encoding='utf-8')
 source='#include <malloc.h>\n'+source
 source=re.sub(r'\bmax\(', 'qr_host_max(',source)
 arrays={'alignPosition':'alignCount','result':'data->capacityBytes','coeff':'blockEccLen','codewordBytes':'bb_getBufferSizeBytes(moduleCount)','isFunctionGridBytes':'bb_getGridSizeBytes(size)'}
 for name,size in arrays.items():
  original=f'uint8_t {name}[{size}];'
  assert original in source,original
  source=source.replace(original,f'uint8_t *{name} = (uint8_t *)_alloca({size});')
  source=source.replace(f'sizeof({name})',f'({size})')
 (out/'qrcode_host.c').write_text(source,encoding='utf-8')
files=list((project/'main').glob('*.c'))+list((project/'main').glob('*.inc'))+list((project/'main/include').glob('*.h'))+[project/'sdkconfig']
files=[p for p in files if 'private' not in p.name and 'secret' not in p.name]
manifest={'model':model,'evidence':'PC LVGL renderer; real UI source with mocked services, no ESP32 firmware compilation','source_files':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
