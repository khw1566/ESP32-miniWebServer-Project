# C3 스페어: e83dc19c243c

## 기기와 주소

- 확인일: 2026-10-26 KST.
- 실제 칩: ESP32-C3(QFN32), 리비전 v0.4, 내장 Flash 4MB(XMC).
- USB: native USB-Serial/JTAG, 확인한 포트 COM14.
- 공장 eFuse MAC: `e8:3d:c1:9c:24:3c`.
- 기존 C3 `90da728a2b18`과 별개인 스페어 기기입니다.
- mDNS: http://c3-v1-2-e83dc19c243c.local/
- 공개 도메인: https://c3-e83dc19c243c.audreaming.com/
- 확인한 LAN 주소: http://192.168.0.36/ (DHCP로 바뀔 수 있음).
- 전용 Worker: `mini-web-c3-spare`.
- 전용 중계 API: https://relay-c3-spare.audreaming.com/
- 서버 설정: `wrangler.c3-spare.toml`.

## 보존과 서버 등록

사용자가 이 스페어 C3의 업로드·시리얼·연결 작업을 요청했습니다.
기존 전체 Flash 4MB를 Git 제외 경로
`results/c3-spare-20261026/flash-before.bin`에 백업했습니다.
개인 설정이 들어갈 수 있으므로 이 파일을 공개하거나 업로드하지 않습니다.

기존 파티션을 읽어 앱 `0x20000`/`0x1a0000`, NVS `0x9000`/`0x6000`,
파일 영역 `0x1c0000`/`0x1c0000`이 현재 C3 파티션과 일치함을 확인했습니다.
앱 갱신은 NVS·파일·부트로더·파티션 테이블을 덮어쓰지 않는 방식입니다.

기존 C3와 다른 보드의 인증 등록을 유지하기 위해 별도 Worker를 배포했습니다.
사용자가 별도 승인한 뒤 이 기기의 토큰 SHA-256 해시만 해당 Worker의
`DEVICE_REGISTRY` secret으로 등록했습니다. 원본 토큰은 서버로 보내지 않았습니다.
로컬 비공개 등록 파일은 `provisioned/registry.c3-spare.json`과
`provisioned/c3-e83dc19c243c.csv`입니다. 토큰을 재발급하지 마세요.

## 독립 빌드와 USB 시리얼

일반 C3의 `sdkconfig`와 `sdkconfig.defaults`는 유지합니다.
이 스페어에는 별도 `sdkconfig.spare`와 `build.spare-domain`을 사용하며,
재현용 설정은 `mini-WEB (C3-ESP-IDF_ver)/sdkconfig.spare.defaults`입니다.
USB-Serial/JTAG를 주 콘솔로 선택해 USB 케이블에서 시리얼 Wi-Fi 입력을
사용할 수 있도록 했습니다.

ESP-IDF는 기존 설치본 5.5.5입니다. 첫 구성 실패는 RISC-V 컴파일러가
PATH에 없어서 발생했습니다. SDK의 `tools.json`과 일치하는 기존 설치본
`C:/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin`을
빌드 프로세스의 PATH에 추가해 진행했습니다. SDK나 컴파일러를 새로 설치하거나
기존 빌드 디렉터리를 삭제하지 않았습니다.

## 별도 전시 QR PDF

`output/pdf/C3_Spare_Exhibition_QR_A4_Landscape.pdf`는 A4 가로 1쪽,
흑백 인쇄용 벡터 QR 2개(mDNS·공개 도메인)입니다.
렌더링 이미지를 직접 확인하고 두 QR을 다시 해독하여 위 URL과 일치함을
검증했습니다. 기존 5쪽 전시 PDF는 유지했습니다.

## 실기 검증

업데이트 전에는 저장된 Wi-Fi 자동 연결과 로컬 홈 HTTP 200을 확인했습니다.
로컬 `/api/system`의 실제 제품은 `c3`, 시리얼은 위 값이었습니다.
기존 펌웨어에는 `cloud_status`가 없어 앱 업데이트가 필요했습니다.

2026-10-26 23:09 KST 최종 확인:

- ESP-IDF 5.5.5 실제 컴파일·링크 성공. 앱 크기 `0x15c6b0`(1,427,120바이트), 기존 앱 슬롯의 16% 여유.
- MAC을 재확인한 뒤 `0x20000`에 앱만 기록했고 esptool 데이터 해시 검증 성공.
- 보드 관리자 인증으로 토큰 저장 후 재부팅. 저장된 Wi-Fi에 자동 재연결.
- `/api/system`: `product=c3`, `serial=e83dc19c243c`, `cloud_status=online`.
- IP·mDNS·공개 도메인의 홈 페이지 모두 HTTP 200. Chrome에서도 기존 쇼케이스 허브 표시 확인.
- 기존 로컬 홈은 14,122바이트이며 업데이트 전후 SHA-256이 일치했습니다.
- 외부 `/admin`은 중계의 HTTP 403으로 차단됐습니다.
- USB 시리얼에서 DTR 연결 신호를 활성화한 상태로 빈 줄을 보내 `> ` 프롬프트 응답을 확인했습니다. 비밀번호·Wi-Fi 변경 명령은 보내지 않았습니다.
- 점검 당시 가용 힙 130,664바이트, 최소 힙 114,308바이트. 장시간 부하·모바일 데이터 회선 검사는 수행하지 않았습니다.
- 원시 결과는 `results/c3-spare-20261026/verification.json`, `build.log`, `flash.log`, `serial-reboot.log`, `serial-input-verification.json`에 보관했습니다.
