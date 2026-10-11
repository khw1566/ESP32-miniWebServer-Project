# ESP32-P4 Mini WebServer — C6 무선 연결 버전

Waveshare **ESP32-P4-Module-DEV-KIT / ESP32-P4NRW32**용 ESP-IDF 5.5.5 프로젝트입니다. ESP32-C6 무선 칩을 ESP-Hosted/SDIO로 연결하여 Wi-Fi STA와 SoftAP를 제공합니다. 관리자·AI Assistant·플래시/SD 파일 관리·웹 OTA·mDNS를 포함합니다.

## Wi-Fi와 서버 IP

기본 소개 화면은 시안·자홍 포인트와 흑백 UI, 픽셀 3D 보드 애니메이션을 사용합니다. [업로드용 단일 HTML](../web_examples/13_p4_webserver/index.html)과 [페이지 편집 안내](../web_examples/13_p4_webserver/README.md)를 제공합니다. 이미 웹 파일이 저장된 보드는 관리자에서 `index.html`을 업로드하면 새 화면으로 바뀝니다. 최초 부팅용 기본 파일은 `node tools/embed_p4_home.cjs`로 갱신합니다.

처음 켜거나 저장된 Wi-Fi 연결에 실패하면 `Mini-WebServer-Setup`에 연결하고 `http://192.168.4.1/setup`에서 Wi-Fi와 DHCP/고정 IP를 설정합니다. 고정 IP는 서버 IPv4·게이트웨이·마스크·DNS를 입력합니다. 전체 사용법은 [공통 Wi-Fi 설정 안내](../docs/WIFI_SETUP.md)를 참고하세요.

P4 제품 주소는 `http://p4-v1-2-<MAC12자리>.local/`, 관리자는 `/admin`, 초기 관리자 비밀번호는 `00000000`입니다. 클라우드 식별자는 `p4-<MAC12자리>`이며 중계의 제품 허용 목록·기기 등록·도메인 설정은 별도입니다.

## P4와 C6 설치

무선 연결에는 **P4 이미지와 호환되는 C6 이미지 두 개**가 필요합니다. 제조사의 ESP-IDF 5.x 안내를 따라 `esp_wifi_remote 0.14.0`과 `esp_hosted 1.4.1`을 고정했습니다. C6에 AT 펌웨어나 다른 Hosted 릴리스가 들어 있다면 P4만 설치해서는 동작하지 않습니다.

ESP-IDF 5.5.5 PowerShell 터미널에서 이 폴더를 열고 사용자가 실행합니다.

```powershell
.\p4.ps1 -Action Build
.\prepare_c6.ps1
```

`prepare_c6.ps1`은 빌드 때 받은 **동일한 esp_hosted 1.4.1의 공식 slave 예제**를 별도의 영문 TEMP 폴더에 준비하고 C6 설정·빌드·업로드 명령을 출력합니다. 이 스크립트 자체는 컴파일·업로드하지 않습니다. 일반 `idf.py`로 P4를 빌드했다면 `-HostedComponentPath '.\managed_components\espressif__esp_hosted'`를 지정합니다.

출력된 C6 프로젝트에서 안내 명령으로 C6를 빌드·설치한 후, 원래 P4 프로젝트로 돌아와 실행합니다.

```powershell
$env:IDF_TARGET = 'esp32p4'
.\p4.ps1 -Action Flash -Port COM9
```

COM9는 P4 포트입니다. C6 설치 명령의 포트는 **C6 UART에 연결한 포트**로 지정해야 합니다. P4의 USB-UART 포트와 혼동하지 마세요. 보드의 C6 UART 헤더·BOOT/RESET 연결은 제조사 회로도와 실제 보드 revision으로 확인하고, 3.3V UART 레벨을 사용하세요. C6 다운로드 모드 진입은 C6의 BOOT/RESET으로 수행합니다. 펌웨어는 eFuse를 변경하지 않습니다.

기존 sdkconfig에서 무선 관련 설정을 끈 경우 menuconfig에서 Remote Wi-Fi/Hosted SDIO와 SoftAP 설정을 확인하세요. 첫 빌드는 Component Manager 의존성을 내려받기 위한 인터넷 연결이 필요합니다.

## 하드웨어 설정

- 16MB DIO 플래시, 32MB HEX PSRAM 200MHz. 제공된 카메라 예제의 초기 P4 rev 1.x 설정을 유지합니다. 실제 칩이 rev 3 이상이면 칩 revision 설정을 바꿔 빌드하세요.
- C6 SDIO: ESP-Hosted 1.4.1의 P4 기본 배선 CLK18, CMD19, D0=14, D1=15, D2=16, D3=17, RESET54. 4-bit/20MHz로 시작하며 P4 SDMMC **slot1**을 사용합니다. 실제 SDIO 열거 로그로 보드와의 동작을 확인해야 합니다.
- TF 카드: 제공된 회로도의 CMD44, CLK43, D0=39, GPIO45(low) 전원 스위치, LDO4를 사용합니다. **slot0**, 1-bit/10MHz로 분리해 무선 통신과 함께 사용합니다. FAT32 카드를 넣고 재부팅합니다. 자동 포맷하지 않습니다.
- 4MiB OTA 슬롯 두 개와 7.8125MiB SPIFFS. 최초 설치는 전체 flash를 사용합니다.
- 이 버전의 기본 네트워크는 Wi-Fi입니다. 이전 유선 전용 초기화는 무선 초기화로 교체했습니다. RJ45는 이번 무선 펌웨어에서 활성화하지 않습니다. 카메라·LCD·오디오도 포함하지 않습니다.

## 확인할 로그와 검증 범위

C6 Hosted 버전/SDIO 열거, `SoftAP 시작`, 공유기 연결 후 IPv4 URL과 mDNS 주소, PSRAM 용량, SD 인식 결과를 확인하세요. Hosted 응답 대기/리셋 반복이 나오면 C6 이미지·전원·보드 배선을 먼저 확인합니다. C6가 동작하지 않으면 무선 SoftAP도 생성할 수 없습니다.

에이전트는 프로젝트 규칙에 따라 펌웨어 컴파일·업로드를 실행하지 않았습니다. 호스트 화면/설정/파티션 검사 결과만으로 실제 C6·Wi-Fi·고정 IP·SD 동시 동작을 보장하지 않습니다. 사용자 빌드 후 공통 안내의 실물 확인 항목을 수행하세요.

참조: [Waveshare 보드 문서](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT), [제조사 P4/C6 버전 안내](https://github.com/waveshareteam/ESP32-P4-Platform/blob/main/docs/P4_C6_HOSTED_WIFI.md), [ESP-Hosted 1.4.1](https://components.espressif.com/components/espressif/esp_hosted/versions/1.4.1), [Wi-Fi Remote 0.14.0](https://components.espressif.com/components/espressif/esp_wifi_remote/versions/0.14.0).
