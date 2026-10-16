# ESP32 mini WebServer

ESP-IDF 기반 ESP32 웹 서버와 온보드 UI 프로젝트입니다.

## 폴더 안내

| 위치 | 내용 |
| --- | --- |
| `mini-WEB (...-ESP-IDF_ver)` | 보드별 펌웨어 프로젝트 |
| [docs](docs/README.md) | Wi-Fi·IP 설정, 관리자, AI, 파일 관리, 보안 문서 |
| [scripts](scripts/README.md) | Windows 웹 서버·터널 실행 파일 |
| `tools` | 공통 소스 생성, 동기화, 호스트 검사 도구 |
| `web_examples` | 보드에 업로드하는 웹 예제와 미디어 |
| `sites` | 별도 호스팅 사이트의 원본·배포 프로젝트 |
| `cloud-relay` | 클라우드 중계 서버 |
| `기획서` | 기획·보고서·신청서 등 로컬 자료 |
| `모델링` | 케이스·부품 CAD 자료와 참고 원본 |
| `releases` | 배포용 압축 파일 |
| `firmware_upload` | 보드 업로드용 펌웨어 산출물 |
| `results`, `test`, `patch_backups` | 검증 결과·테스트 자료·수정 전 백업 |

최근 P4 소개 페이지는 [web_examples/13_p4_webserver/index.html](web_examples/13_p4_webserver/index.html)에 있습니다. 폴더 정리 시 파일을 삭제하지 않았으며 이동 기록은 `results/folder_cleanup/2026-10-10-moves.json`에 보관했습니다.

## 펌웨어

- `mini-WEB (S3-ESP-IDF_ver)`: ESP32-S3용 웹 서버, AI Assistant 및 파일 관리 기능.
- `mini-WEB (S3-N16R8-SD-ESP-IDF_ver)`: 16MB 플래시·8MB PSRAM 및 SDMMC 슬롯용 S3 펌웨어. M1의 SD 관리자와 클라우드 기능을 이식했으며 카메라는 사용하지 않습니다.
- `mini-WEB (C3-ESP-IDF_ver)`: ESP32-C3용 펌웨어.
- `mini-WEB (P4-ESP-IDF_ver)`: Waveshare ESP32-P4-Module-DEV-KIT용 C6 무선 웹 서버. ESP32-P4NRW32, 16MB 플래시·32MB PSRAM, Wi-Fi SoftAP 설정 및 SD 파일 관리. 호환되는 C6 펌웨어 설치가 필요합니다.
- [mini-WEB (M3-ESP-IDF_ver)](mini-WEB%20(M3-ESP-IDF_ver)/README.md): GUITION JC-ESP32P4-M3 / JC4880P443C_I_W용 480×800 스마트폰식 앱 런처, 홈 복귀 제스처, Wi-Fi·서버 IP 설정, 인터넷 라디오(MP3/AAC), 2048·카드 짝 맞추기, 한국어 로컬 TTS·한글 키보드, ES8311 스피커·SD WAV 재생. 16MB Flash·32MB PSRAM. 소스 및 호스트 검사 단계이며 펌웨어 빌드·실기기 검증은 별도입니다.
- `mini-WEB (M1-ESP-IDF_ver)`: ESP32-2432S028용 LCD·터치 UI, 가상 키보드, Wi-Fi 및 SoftAP 접속과 QR 안내. 자세한 사용법은 폴더 내부 README를 참고하세요.

ESP-IDF 개발 환경에서 해당 펌웨어 폴더를 열어 사용합니다. 타깃, 파티션 및 보드 배선을 확인한 뒤 빌드·업로드하세요. 빌드 산출물과 다운로드된 의존성은 저장소에서 제외합니다.

## 웹 예제

PC에서 이미지를 P4용 JPG/BMP 또는 웹용 WebP로 줄이려면 `scripts/ESP32_Image_Converter.bat`을 실행하세요. 파일·폴더 일괄 변환과 크기·품질 설정을 지원합니다. [이미지 변환기 사용법](tools/ESP32_IMAGE_CONVERTER.md)을 참고하세요.

`web_examples`에 보드에서 제공할 HTML 및 미디어 예제가 있습니다. `scripts/Serve_Bianca_ngrok.bat`을 실행하면 Bianca/Fernando 보드용 웹 파일을 로컬 서버와 기존 ngrok으로 제공합니다. Python과 ngrok 설치 및 ngrok 계정 인증이 필요합니다. 세부 사용법은 `web_examples/09_bianca_fernando/README.md`를 참고하세요.

## S3 AI 키 저장 수정

키 저장 실패 시 입력값 유지, 중복 저장 방지, 저장 상태 조회 경쟁 방지 및 형식·암호화·NVS 오류 구분을 반영했습니다. `node "mini-WEB (S3-ESP-IDF_ver)/tools/test_ai_key.cjs"`로 관련 호스트 테스트를 실행할 수 있습니다. 실제 보드 검증은 별도로 필요합니다.

## AI 미디어와 진단

AI Assistant의 일반 대화와 보드 작업 분기는 [AI_ROUTER.md](docs/AI_ROUTER.md)에 설명되어 있습니다. 인사·질문에는 자연어로 답하고, 실제 작업에 필요한 도구만 활성화합니다. M1·S3·C3·S3-N16R8-SD에 적용했습니다.

S3·C3·M1에서 보드의 JPG 이미지와 MP4 화면 표본을 AI로 분석하고, 하드웨어·메모리·네트워크·최근 로그를 조회할 수 있습니다. MP4는 최대 3개 시점의 화면을 분석하며 음성은 포함하지 않습니다. 사용법과 제한은 [AI_MEDIA_DIAGNOSTICS.md](docs/AI_MEDIA_DIAGNOSTICS.md)를 참고하세요.

개인 신청서, 설계 파일, 백업 및 로컬 인증정보는 업로드 범위에서 제외합니다.

모든 펌웨어에서 SoftAP로 Wi-Fi와 서버 고정 IP를 설정할 수 있습니다. [Wi-Fi·IP 설정 사용법](docs/WIFI_SETUP.md)을 참고하세요.
