# M4 이식 기록

2026-10-24. 사용자 결정: M4 명칭, JC1060P470C, 세로 사용, 실물에서 동작한 V2 패널.

## 기준과 출처

- M3: 현재 저장소 커밋 `1fb6d9b8`의 기능 소스. 실제 전체 해시는 IMPORT_MANIFEST.json 참조.
- 보드 참조: `C:/Users/work/Desktop/ARD_v1/ARD_ESP-IDF/JC1060P470C`의
  `main/board.h`, `bsp_display.cpp`, `panel_init.c/h`, `Kconfig.projbuild`, sdkconfig/defaults.
  참조 프로젝트의 기존 빌드 결과를 M4 빌드 증거로 사용하지 않는다.
- [ESPHome JC1060 V1/V2 패널 정의](https://github.com/esphome/esphome/blob/dev/esphome/components/mipi_dsi/models/guition.py):
  참조 초기화 테이블과 54/52MHz 타이밍의 출처. RGB565 COLMOD 0x55와 INVOFF를 명시한다.
- [Espressif esp-claw GUITION 보드 추가 제안](https://github.com/espressif/esp-claw/pull/51/files):
  SD slot0/LDO4, ES8311 full duplex/I2S13·12·10·9·48 보조 근거.
  병합되지 않은 보드 제안이며 실물 검증을 대신하지 않는다.
- ESP-IDF 5.5.5 및 로컬 esp_lvgl_port 2.7.2/LVGL9.2.2 헤더·구현을 직접 확인했다.
  Context7의 가장 가까운 공개 문서는 5.5.3이었으며 5.5.5 API 판정은 설치본을 우선했다.

## 변경과 메모리

JD9165 전용 드라이버 2.0.2, GT911 1.2.1을 선택했다. V2가 기본이고 V1도 선택 가능하다.
패널을 1024×600으로 구동하고 LVGL partial rendering + software/PPA rotation으로
600×1024를 만든다. direct mode/full refresh/avoid_tearing과 회전을 혼합하지 않는다.
DSI 복사 완료 시 LVGL 포트의 on_color_trans_done이 draw buffer를 반환한다.

고정적인 RGB565 버퍼 계산은 다음과 같다. 실제 링크 맵·힙 측정 결과는 아니다.

| 용도 | 계산 | 바이트 |
| --- | --- | ---: |
| 패널 scanout 한 장 | 1024×600×2 | 1,228,800 |
| LVGL partial 두 장 | 1024×64×2×2 | 262,144 |
| 회전 출력 한 장 | 1024×64×2 | 131,072 |
| 위 세 항목 합 | | 1,622,016 |

PPA 정렬, LVGL 변환 합성 레이어, 글꼴·사진·영상·음성·웹 캐시와 드라이버 제어 데이터는
위 합계에 포함되지 않는다. 1.25배 변환 레이어의 피크 메모리와 실기 FPS는 아직 미측정이다.
PSRAM 우선 할당, TLS 외부 힙, 내부 DMA/TCB/민감 경로 스택과 내부 예약 128KiB를 유지했다.
칩 v1.x 기본값에 맞춰 메모리 정책의 무조건 400MHz 강제를 제거했다.

서비스 작업과 LVGL의 기존 소유권, 중지·취소·오류 해제 흐름은 계승했다.
화면·수동 제스처·부팅 화면·PC 시뮬레이터 소스를 세로 화면에 맞췄다.
M3 배터리 ADC는 M4 회로 증거가 없어 사용하지 않는다.

## 서비스와 보안

기기 제품 이름·AP·mDNS·클라우드 ID는 M4로 분리한다. PC 릴레이의 외부 프로토콜 이름과
인증 헤더는 M3와 공유한다. 사용자 비밀 파일은 복사하지 않았고, M4 API/릴레이 토큰은
SD JSON에서만 읽는다. public build staging은 private header를 거부한다.
cloud-relay의 M4 허용은 로컬 도구 변경이며 등록·키 발급·배포 증거가 아니다.

## 검증 경계

에이전트는 저장소 `.agents/rules/project_rules.md`의
“에이전트는 코드 작성 및 수정까지만 수행하며, idf.py build, flash 등 컴파일 및 업로드 명령을 직접 실행하지 않는다”를 따른다.
M4 전체 펌웨어 컴파일·링크·플래시와 새 PC 네이티브 UI 렌더링은 실행하지 않았다.
기존 compile database를 재사용한 `-fsyntax-only`는 문법 확인일 뿐 새 M4 SDK 구성이나 링크 성공 증거가 아니다.

검사 명령과 실제 결과는 `results/host-checks.json`, `results/syntax-checks.json`,
`results/validation-summary.json`에 저장한다. SDK/관리 컴포넌트는 M4에 아직 다운로드하지 않았다.
의존성 API 검사는 M3/ARD에 설치된 같은 버전 헤더를 읽었으며, M4 사용자 빌드에서 새로 해결해야 한다.

첫 실기 단계에서 확인할 항목은 P4 리비전·메모리 용량, 패널 색/방향, 네 모서리 터치,
키보드·홈 제스처·작업 패널·스크롤, SD 설정·파일, C6 버전·연결·재연결,
마이크 입력 부품/채널·스피커 복귀, 라디오·YouTube·MP4·게임과 장시간 자원 여유이다.

## 이번 실행 결과

선택한 Node/Python 호스트 검사 19개가 통과했다. 빌드 준비/SD 설치 검사는
4개 사례를 실행했고, 추가 cloud-relay M4 등록 fixture와 PowerShell 구문 검사도 통과했다.
보드·패널·레이아웃·UI·부팅·전원·SD·오디오·앱 진입·YouTube·라디오의
C 소스 11개가 설치된 플래그를 재사용한 `-fsyntax-only`를 경고 없이 통과했다.
M4 전체 펌웨어 구성·컴파일·링크나 보드 실행 성공을 의미하지 않는다.

작업 중 M3의 `m3_tube.c`와 `test_tube.cjs`, PC 릴레이에 별도 변경이 관찰됐다.
이 작업은 M3와 ARD 참조 파일을 쓰지 않았다. M3의 HTTP 401 진단 수정은 내용을
확인한 뒤 M4 코드와 동작 fixture에 별도로 반영했다. 최초 snapshot 해시는
IMPORT_MANIFEST.json에 보존했고 현재 차이는 validation-summary.json에 기록했다.

## 부팅 중 SDIO mempool 재부팅 수정

실물 로그(ELF SHA256 접두사 `22889bbe6`)에서 P4 v1.3, 16MB Flash,
32MB PSRAM 및 PSRAM 메모리 검사 통과를 확인했다. 화면 초기화와 app_main 진입 전에
Hosted 전역 생성자 → esp_hosted_init → sdio_mempool_create의 `assert(buf_mp_g)`로 중단됐다.
이 로그만으로 mempool 내부의 어느 할당이 실패했는지까지 확정할 수는 없다.

설치된 IDF 5.5.5는 전역 생성자 이후 main_task에서 시작용 스택 힙을 회수하고
내부 DMA 예약 풀을 만든다. M4는 링크 wrapper로 Hosted 생성자의 초기화 요청을
유예하고 app_main에서 NVS 준비 후, 디스플레이·앱 작업을 만들기 전에 시작한다.
SDK 관리 소스, 내부 DMA 버퍼 정책, mempool 크기와 C6 버전은 변경하지 않았다.
초기화 로그 `M4_HOSTED: Starting after startup heap reclamation`에 내부/DMA 힙과
가장 큰 DMA 블록 크기를 남긴다. 이후 SDK 초기화 호출은 원래 구현으로 전달한다.

`test_hosted_boot.cjs`의 생성자 유예·실행 순서·오류 전달 검사와 M4 통합 검사,
실제 M4 compile database를 사용한 변경 C 소스 2개의 문법 검사가 통과했다.
기존 Hosted 아카이브에서 생성자의 외부 init 참조와 API 정의도 확인했다.
결과는 `results/hosted-boot-fix.json`에 있다. 새 링크·업로드·재부팅 해소·화면 출력은
사용자 재빌드와 실기 로그로 확인해야 한다.
