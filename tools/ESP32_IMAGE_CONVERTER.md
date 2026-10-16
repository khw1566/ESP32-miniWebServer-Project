# ESP32-P4 이미지 변환기

`scripts/ESP32_Image_Converter.bat`을 더블클릭 → 파일 또는 폴더 선택 → **변환 시작**. Python 3.10 이상, Tcl/Tk, Pillow가 필요합니다. 이 PC에서는 Python/Pillow/Tk를 확인했습니다. 다른 PC에서는 `python -m pip install Pillow`로 의존성을 설치하세요.

기본 저장 위치는 입력 폴더의 `esp32_images`입니다. 원본과 기존 결과를 덮어쓰지 않습니다. 같은 이름의 다른 입력 형식에는 `_2` 등의 접미사를 붙입니다. 다른 설정으로 재변환하려면 새 저장 폴더를 지정하세요. 하위 폴더 구조를 유지하고 출력 폴더와 심볼릭 링크는 검색에서 제외합니다. 손상 파일은 로그에 실패로 남기고 계속합니다. 중단은 현재 이미지 처리 후 적용하며 완료된 결과를 보존합니다.

| 빠른 설정 | 최대 크기 | 저장 형식 | 용도 |
|---|---|---|---|
| P4 / M3 사진 앱 (기본) | 432×480 | JPG, RGB, baseline, YUV 4:2:0 | 현재 M3 사진 영역에 맞춤 |
| P4 세로 화면 | 480×800 | 동일 JPG | 세로 LCD용 이미지 준비 |
| P4 / M3 BMP | 432×480 | 무압축 24비트 BMP | 현재 M3 BMP 디코더 대응, JPG보다 큰 용량 |
| 웹페이지 | 800×800 | WebP | 브라우저 표시용, 현재 M3 사진 앱에서는 지원하지 않음 |

기본 **비율 유지**는 원본을 확대하지 않고 최대 크기 안으로 축소합니다. **여백 추가**는 비율을 유지하면서 지정 크기의 검정 캔버스에 배치합니다. **중앙 자르기**는 지정 크기에 맞춰 확대/축소하고 가장자리를 자릅니다. 투명 영역은 검정 배경으로 합성합니다. 품질은 JPG/WebP에만 적용합니다. EXIF 회전을 적용한 후 위치 정보 등 메타데이터를 제거합니다. 색상은 RGB로 변환하며 ICC 색상 프로필에 따른 색 보정은 하지 않습니다.

입력: JPG/JPEG, PNG, BMP, WebP, GIF, TIF/TIFF, ICO. 애니메이션과 다중 페이지는 첫 화면만 변환합니다. HEIC/AVIF는 이 도구의 입력 목록에 포함하지 않습니다. 파일별 순차 처리이며 아주 큰 원본은 Pillow의 안전 제한으로 거부될 수 있습니다.

현재 소스의 M3 사진 앱은 SD 카드의 `/media/widgets/photos/`에서 JPG/JPEG/BMP를 최대 64개 읽습니다. 변환 결과의 이미지들을 해당 폴더에 직접 복사하세요. 하위 폴더는 사진 앱이 탐색하지 않습니다. 현재 펌웨어 제한은 파일당 4 MiB, 가로/세로 2048 이하, 전체 2,097,152 픽셀 이하이며 변환기도 이를 적용합니다. 기본 JPG의 디코딩 버퍼는 432×480×2 = 414,720바이트(약 405 KiB)입니다. 보드 전체 메모리 사용량과는 다릅니다.

Waveshare P4 웹 서버 버전은 LCD 사진 앱을 포함하지 않습니다. WebP는 보드가 파일을 전송하고 브라우저가 표시하는 용도입니다. PC 출력 검증과 실제 보드 재생 검증은 별개입니다.

명령줄 (PowerShell):

```powershell
python tools/esp32_image_converter.py 'D:\Pictures'
python tools/esp32_image_converter.py 'D:\Pictures\photo.png' --mode pad
python tools/esp32_image_converter.py 'D:\Pictures' -o 'D:\P4_BMP' --profile p4-bmp --no-recursive
python tools/esp32_image_converter.py 'D:\Pictures' --profile p4-screen --quality 85 --mode crop
python tools/esp32_image_converter.py 'D:\Pictures' --profile web-small --width 640 --height 640
```
