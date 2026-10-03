# ESP32 mini WebServer

ESP-IDF 기반 ESP32 웹 서버와 온보드 UI 프로젝트입니다.

## 펌웨어

- `mini-WEB (S3-ESP-IDF_ver)`: ESP32-S3용 웹 서버, AI Assistant 및 파일 관리 기능.
- `mini-WEB (C3-ESP-IDF_ver)`: ESP32-C3용 펌웨어.
- `mini-WEB (M1-ESP-IDF_ver)`: ESP32-2432S028용 LCD·터치 UI, 가상 키보드, Wi-Fi 및 SoftAP 접속과 QR 안내. 자세한 사용법은 폴더 내부 README를 참고하세요.

ESP-IDF 개발 환경에서 해당 펌웨어 폴더를 열어 사용합니다. 타깃, 파티션 및 보드 배선을 확인한 뒤 빌드·업로드하세요. 빌드 산출물과 다운로드된 의존성은 저장소에서 제외합니다.

## 웹 예제

`web_examples`에 보드에서 제공할 HTML 및 미디어 예제가 있습니다. `Serve_Bianca_ngrok.bat`을 실행하면 Bianca/Fernando 보드용 웹 파일을 로컬 서버와 기존 ngrok으로 제공합니다. Python과 ngrok 설치 및 ngrok 계정 인증이 필요합니다. 세부 사용법은 `web_examples/09_bianca_fernando/README.md`를 참고하세요.

## S3 AI 키 저장 수정

키 저장 실패 시 입력값 유지, 중복 저장 방지, 저장 상태 조회 경쟁 방지 및 형식·암호화·NVS 오류 구분을 반영했습니다. `node "mini-WEB (S3-ESP-IDF_ver)/tools/test_ai_key.cjs"`로 관련 호스트 테스트를 실행할 수 있습니다. 실제 보드 검증은 별도로 필요합니다.

개인 신청서, 설계 파일, 백업 및 로컬 인증정보는 업로드 범위에서 제외합니다.
