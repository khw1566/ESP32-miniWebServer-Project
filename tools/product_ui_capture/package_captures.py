from pathlib import Path
import zipfile
repo=Path(__file__).resolve().parents[2]
dest=repo/'releases/MiniWeb-PC-UI-Captures-20261016.zip'
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED) as archive:
 for model in ('m1','m3'):
  folder=repo/f'results/product-ui-capture-20261015/{model}-current'
  for file in (folder/'frames').glob('*.png'):archive.write(file,f'{model}/{file.name}')
  for name in ('source-manifest.json','verification.json'):archive.write(folder/name,f'{model}/{name}')
 archive.writestr('README.txt','Native PC LVGL captures. M1 240x320: real onboard_ui.c + LVGL 8.4. M3 480x800: real m3_ui.c + LVGL 9.2.2. Network, media, memory and storage are mocked fixtures. These are not physical LCD or hardware performance evidence. User authorized PC simulator compilation only. No ESP32 firmware compilation or flashing. Source hashes and verification reports included.\n')
print(dest,dest.stat().st_size,'bytes')
