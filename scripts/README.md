# Windows 실행 파일

이 폴더의 BAT 파일은 더블클릭하거나 터미널에서 실행합니다. 다른 폴더에서 실행해도 프로젝트 파일을 찾도록 스크립트 위치를 기준으로 경로를 지정합니다.

- `Serve_Bianca_ngrok.bat`: 보드용 Bianca/Fernando 웹 예제를 Python 서버와 ngrok으로 제공합니다. 사용법은 [예제 안내](../web_examples/09_bianca_fernando/README.md)에 있습니다.
- `Serve_Opera_2021_ngrok.bat`: 로컬 `opera_2021/start_ngrok.bat`을 실행합니다.
- `Setup_nrok.bat`: 기존 파일 이름을 유지한 zrok 설치 도구입니다. 설치가 필요할 때 사용자가 실행합니다.
- `ESP32_Audio_Converter.bat`: 음악 폴더를 저용량 MP3 또는 PCM16 WAV로 일괄 변환합니다. [변환기 사용법](../tools/ESP32_AUDIO_CONVERTER.md)을 참고하세요.
- `ESP32_Image_Converter.bat`: 이미지 파일·폴더를 P4용 JPG/BMP 또는 웹용 WebP로 변환합니다. [변환기 사용법](../tools/ESP32_IMAGE_CONVERTER.md)을 참고하세요.

이번 파일 정리에서는 서버·터널이나 설치 도구를 실행하지 않았습니다. 로컬 전용 실행 파일은 기존 Git 제외 상태를 유지합니다.
