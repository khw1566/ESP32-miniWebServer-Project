from pathlib import Path
import hashlib,json,sys,subprocess
repo=Path(__file__).resolve().parents[2]
for model in ('m1','m3'):
 directory=repo/f'results/product-ui-capture-20261015/{model}-current'
 manifest=json.loads((directory/'source-manifest.json').read_text())
 changed=[name for name,expected in manifest['source_files'].items() if hashlib.sha256((repo/name).read_bytes()).hexdigest()!=expected]
 def renderer_dependency(name):
  file=Path(name).name
  if model=='m1': return file in ('onboard_ui.c','m1_dashboard.c','m1_keyboard.c','font_m1.c','qrcode.c','sdkconfig') or file.endswith('.h')
  return file.startswith('m3_') and (file.endswith('.h') or file.endswith('.inc') or file in ('m3_ui.c','m3_i18n.c','m3_2048.c','m3_crossing.c','m3_hangul.c','m3_calendar.c','m3_kana.c','m3_calculator.c') or file.startswith('m3_font_'))
 configuration_changed=False
 if model=='m3':
  project=repo/'mini-WEB (M3-ESP-IDF_ver)'
  current=directory/'verify-sdkconfig.h'
  subprocess.run([sys.executable,'-X','utf8',str(project/'tools/ui_sim/gen_sdkconfig.py'),str(project/'sdkconfig'),str(current)],check=True)
  configuration_changed=current.read_bytes()!=(directory/'sim_sdkconfig.h').read_bytes()
 changed_renderer=[name for name in changed if renderer_dependency(name)]
 report={'model':model,'native_png_count':len(list((directory/'frames').glob('*.png'))),'changed_renderer_dependencies':changed_renderer,'ui_configuration_changed':configuration_changed,'changed_outside_renderer': [name for name in changed if name not in changed_renderer], 'firmware_build_or_flash_executed':False}
 (directory/'verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(json.dumps(report))
 if changed_renderer or configuration_changed: sys.exit(1)
