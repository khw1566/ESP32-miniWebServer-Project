# 배포 파일과 보조 도구

- `tube-relay/`: 중계 프로그램의 버전별 배포 폴더입니다. `M3TubeRelay.exe`, 실행 스크립트, 서버 소스와 `manifest.json`을 함께 제공합니다. 폴더 전체를 내려받아 사용하세요. 최신 소스와 실행파일은 `../tube-relay/`에 있습니다.
- 이미지 변환기: `../scripts/ESP32_Image_Converter.bat`, `../tools/esp32_image_converter.py`와 `../tools/ESP32_IMAGE_CONVERTER.md`를 사용하세요. `../scripts/a/`는 입력 이미지, `../scripts/esp32_images/`는 변환 결과입니다.
- 오디오 변환기: `../scripts/ESP32_Audio_Converter.bat`, `../tools/esp32_audio_converter.py`와 `../tools/ESP32_AUDIO_CONVERTER.md`를 사용하세요.
- CAD 원본과 출력물은 `../CAD Models/`에 있습니다. M1_v2와 M3_v3는 Inventor 부품·조립품 및 기존 버전 파일까지 포함합니다. M3_v3의 제공된 ZIP 원본도 함께 보관합니다.
- SD 설정 형식과 비밀값 없는 예제는 `../mini-WEB (M3-ESP-IDF_ver)/docs/SD_PRIVATE_SETTINGS.md` 및 `docs/examples/`를 참고하세요. 실제 API 키·Wi-Fi 정보가 있는 `.private/`는 Git에 포함하지 않습니다.

일반 ZIP, 캐시, 개인 설정, 빌드 임시 파일은 Git 제외 대상입니다. CAD ZIP은 명시적인 예외로 포함합니다. 기존 배포 ZIP 대신 필요한 파일이 풀려 있는 배포 폴더를 제공합니다. 실행파일을 추가로 컴파일하거나 펌웨어를 빌드·플래시하지 않았습니다.
