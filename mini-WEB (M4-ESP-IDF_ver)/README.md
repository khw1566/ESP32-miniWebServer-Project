# Mini WebServer M4

GUITION **JC1060P470C**, ESP32-P4 + C6용 M3 기능 이식 프로젝트입니다.
사용자가 실제 동작을 확인한 **JD9165 V2 패널, 세로 사용**을 기본으로 합니다.
이번 결과는 소스 이식 및 호스트 검사 단계입니다. M4 펌웨어 빌드·플래시,
새 네이티브 시뮬레이터 렌더링, M4 실기 동작 확인은 아직 수행하지 않았습니다.

M3의 현재 소스를 독립 복사하고 파일·함수·기기 이름을 M4로 분리했습니다.
기준 커밋과 파일 해시는 [IMPORT_MANIFEST.json](docs/IMPORT_MANIFEST.json)에 있습니다.
M3의 이후 수정은 자동 동기화되지 않습니다.

| 항목 | M4 기본값 |
| --- | --- |
| 패널 | JD9165 V2, 물리 1024×600, 52 MHz, 2 lane 750 Mbps |
| 세로 화면 | 600×1024, 90도 회전; 메뉴에서 270도로 반전 가능 |
| 앱 레이아웃 | 480×800을 1.25배로 확대, 600×1000, 위아래 12px |
| 터치 | GT911, I2C SDA7/SCL8, 0x5D/0x14 탐색, 리셋 핀 미사용 |
| LCD/백라이트 | RESET0, PWM23, 밝기 극성 선택 가능 |
| Flash/PSRAM | 16 MiB / 32 MiB 기준; 실물 용량은 부팅 로그로 확인 |
| P4 칩 | 예제와 같은 v1.x / 360 MHz; v3.x용 별도 설정 제공 |
| C6 | Hosted 2.13.0, SDIO 10 MHz, CLK18/CMD19/D0~3 14~17/RESET54 |
| 스피커/마이크 | M3 ES8311 경로 계승; I2S 13/12/10/9/48, AMP11 |
| SD | slot0 4bit, CLK43/CMD44/D0~3 39~42, LDO4 |
| 전원 상태 | 배터리·충전 측정 회로 미확인: 상태 미상, GPIO53 ADC 비활성 |

모든 앱의 키보드, 홈 제스처, 작업 패널과 부팅 화면에 같은 확대를 적용했습니다.
사진·영상·SD 게임의 원본 버퍼 크기는 유지합니다. 터치 hit-test는 LVGL에 맡기고,
직접 계산하는 스와이프 좌표는 확대 전 좌표로 환산합니다.

이식 범위는 Wi-Fi/SoftAP·고정 IP·mDNS·QR, 웹 관리자·SD 파일/앱,
AI 대화·음성 인식·TTS, 라디오·YouTube/YouTube Music·음악·녹음,
사진·갤러리·MP4·텍스트 리더, 캘린더·계산기·게임, 설정·접근성·시스템 음향입니다.
소스 포함 여부와 보드에서의 동작 검증은 구분합니다.

## 사용자 빌드

ESP-IDF 5.5.5 환경에서 이 폴더를 엽니다.

```powershell
.\m4.ps1 -Action Build                         # V2 패널, 예제의 P4 v1.x
.\m4.ps1 -Action Flash -Port COM9               # 직전 성공한 M4 빌드 업로드
.\m4.ps1 -Action Monitor -Port COM9
```

실제 로그가 **ESP32-P4 v3.x**인 경우에만 다음을 사용합니다.

```powershell
.\m4.ps1 -Action Build -Panel V2 -Silicon v3
```

구형 패널은 `-Panel V1`로 선택합니다. `-Action Menuconfig`에서 터치 미러,
세로 방향, 리셋·백라이트·I2S 핀을 조정할 수 있습니다. 저장된 sdkconfig의
패널·칩 설정이 실행 인수와 다르면 스크립트가 중단하므로 같은 선택을 전달하세요.
패널 V2 표시와 P4 실리콘 v3 표시는 서로 다른 항목입니다.

빌드 스크립트는 공백 없는 새 TEMP 폴더로 공개 소스만 복사하고 병렬 작업을
2개로 제한합니다. 성공한 경우에만 `firmware-output`과 `results/last-build.json`을
갱신합니다. 실패한 빌드로 기존 성공 산출물을 교체하지 않습니다.
Flash는 빌드 단계와 분리되어 있고 자동 monitor는 실행하지 않습니다.

예제 ARD는 Hosted **2.12.2**, M4는 M3와 같은 **2.13.0**입니다.
P4와 C6의 버전을 맞추세요. C6 설치/갱신은 별도이며 이번 작업에서 실행하지 않았습니다.
기존 OTA 슬롯 0x760000 두 개와 NVS 위치는 보존했습니다.

## SD 카드와 외부 서비스

`sd_assets`의 내용을 SD 루트에 복사합니다. 별도 `sd_apps`의 Lua 게임과
`tts_data/espeak-ng-data`는 기존 기능 문서의 설치 절차를 사용할 수 있습니다.
Wi-Fi는 `.private/wifi.json`, API 키는 `.private/api_keys.json`을 사용합니다.
실제 키와 기존 M3 설정은 이번에 복사하지 않았습니다.

M4 릴레이 토큰은 같은 API JSON의 **`tube_token`** 필드로 읽습니다.
기존 PC `tube-relay`의 프로토콜 2, `x-m3-token` 헤더와 `m3-tube-relay` 서비스 이름을
유지하므로 같은 릴레이를 사용할 수 있습니다. M4 토큰을 소스 헤더에 넣지 마세요.
저장소 루트의 `tools/copy_m4_sd_settings.py`로 명시한 JSON을 설치할 수 있습니다.
클라우드 도구에 M4 모델 선택을 추가했으며, 실제 등록·키 발급·배포는 수행하지 않았습니다.

## 검증과 첫 부팅

[PORTING_M4.md](docs/PORTING_M4.md)에 출처, 메모리 계산, 제한과 검증 결과를 기록합니다.
첫 부팅은 V2 패널 표시, 네 모서리 터치, 키보드·스크롤·홈 복귀, SD 마운트,
Wi-Fi/SoftAP, 스피커·마이크, 각 앱 순서로 확인합니다. ES7210이 실제 마이크
입력인 보드 리비전은 별도 ADC 초기화가 필요하며 현재 ES8311 입력 경로의 실기 확인이 필요합니다.
Ethernet·RTC·CSI 카메라는 이번 M3 기능 이식에 포함하지 않습니다.

`docs/m3_reference_hardware`는 M3 전용 과거 자료입니다. M4 회로도나 치수로
사용하지 마세요. 계승 기능 문서보다 이 README와 PORTING_M4.md의 M4 설정이 우선합니다.
