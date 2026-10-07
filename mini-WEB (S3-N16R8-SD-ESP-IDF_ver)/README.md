# ESP32-S3 N16R8 SD 웹 서버

M1의 관리자 대시보드, QR 탭, 플래시·SD 파일 관리, SD 미디어 전송, AI Assistant와 클라우드 중계를 이식한 별도 펌웨어입니다. 기존 S3와 M1 프로젝트는 유지합니다. LCD·터치·카메라 드라이버는 포함하지 않습니다.

## 대상 보드와 배선

ESP32-S3-WROOM-1 N16R8: 16MB Quad SPI 플래시, 8MB Octal PSRAM. GPIO35·36·37은 PSRAM에서 사용하므로 다른 기능에 할당하지 않습니다.

| SDMMC 신호 | GPIO |
| --- | --- |
| CMD | 38 |
| CLK | 39 |
| D0 | 40 |

SD는 1-bit, 10MHz로 시작합니다. 사용자가 제공한 핀 그룹과 [Freenove 제조사 예제](https://docs.freenove.com/projects/fnk0086/en/latest/fnk0086/codes/tutorial/4_Read_and_Write_the_SDcard.html)의 역할 배치가 일치합니다. 구매 링크의 실제 회로도는 확인하지 못했으므로 첫 실행에서 이 역할 배치를 확인해야 합니다. 모듈에 N16R8V/WROOM-2가 표시된 경우 플래시 전압·모드가 다르므로 이 설정을 그대로 사용하지 마세요.

FAT32로 준비한 SD를 전원을 끈 상태에서 넣습니다. 자동 포맷은 하지 않습니다. SD 인식 실패 시에도 웹 서버와 플래시 파일 관리가 동작하며, 로그에 실패 단계가 표시됩니다. SD 관리자에서 파일 업로드·다운로드·삭제와 폴더 탐색을 할 수 있습니다. 전송당 최대 8MiB이며, 큰 파일은 스트리밍으로 처리합니다. SD 루트에 `index.html`을 두면 공개 웹 페이지로 제공합니다. 카드 재삽입 후에는 재부팅하세요.

## 메모리와 파티션

PSRAM 초기화와 시작 메모리 검사를 활성화했습니다. 큰 JSON 및 TLS 버퍼는 일반 malloc을 통해 PSRAM을 활용하고, 내부 RAM 32KiB를 예약합니다. AI 요청 시 중계 연결을 잠시 양보하는 M1의 안정화 정책을 유지합니다.

OTA 앱 슬롯 두 개는 각각 4MiB, SPIFFS 파일 영역은 7.8125MiB입니다. 나머지는 NVS·OTA 정보·PHY·코어 덤프입니다. 기존 펌웨어의 파티션과 다르므로 최초 설치는 전체 이미지로 수행하고 필요한 파일은 먼저 백업하세요. 이후 같은 파티션 구성에서 웹 OTA를 사용할 수 있습니다.

## 사용자 빌드와 첫 연결

ESP-IDF 5.5 환경에서 이 폴더를 프로젝트로 열고 사용자가 빌드·설치합니다.

```powershell
$env:IDF_TARGET = 'esp32s3'
idf.py set-target esp32s3
idf.py build
idf.py -p COM포트 flash monitor
```

`GPIO_NUM_40 undeclared`와 `Building ESP-IDF components for target esp32`가 함께 나오면 S3 프로젝트에 ESP32 타깃·캐시가 적용된 상태입니다. VS Code에서 이 프로젝트 폴더 자체를 열고 ESP-IDF 터미널에서 위 타깃 지정부터 다시 실행하세요. `set-target`은 기존 빌드 캐시를 초기화하고 `sdkconfig`를 새 타깃으로 재생성하므로, GPIO를 30 등으로 바꾸거나 기존 `sdkconfig`의 타깃 한 줄만 고치지 않습니다. 이후 로그에 `target esp32s3`가 표시되는지 확인하세요. 생성된 설정은 `CONFIG_IDF_TARGET="esp32s3"`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`여야 합니다.

최초 부팅 시 `Mini-WebServer-Setup` Wi-Fi에 연결하고 `http://192.168.4.1/setup`에서 Wi-Fi를 설정합니다. 저장된 Wi-Fi는 다음 부팅부터 자동 연결합니다. 시리얼 설정도 지원합니다. 로컬 주소는 `http://s3-v1-2-<MAC12자리>.local/`이며 관리자는 `/admin`입니다.

클라우드 주소는 기본 `https://s3-<MAC12자리>.audreaming.com/`입니다. 펌웨어만 설치해서 DNS·등록 정보가 생성되지는 않습니다. 보드 MAC이 확인되면 중계 등록과 해당 호스트 연결이 필요합니다. 기존 M1 등록을 덮어쓰지 않습니다.

```powershell
cd ..\cloud-relay
node provision.mjs MAC12자리 s3
# 생성된 registry.json을 기존 절차에 따라 Worker 등록 정보에 반영하고,
# s3-MAC12자리.audreaming.com을 Worker 사용자 지정 도메인으로 연결합니다.
.\Register-Board.ps1 -Product s3 -Serial MAC12자리 -BoardUrl http://보드IP
```

생성된 `provisioned/` 키는 비공개로 보관합니다. 등록 후 보드를 재부팅합니다. 제조 시 등록해 두면 사용자는 전원과 Wi-Fi 설정만으로 서비스를 제공할 수 있습니다.

## 검증 범위

호스트 Node 테스트로 관리자·AI UI 동작, 메모리 정책, 등록 키 분리를 검증합니다. 프로젝트 규칙상 에이전트가 펌웨어를 컴파일하거나 설치하지 않습니다. 실제 PSRAM 검사, SD 인식·전송, OTA와 클라우드 연결은 보드 설치 후 확인해야 합니다.
