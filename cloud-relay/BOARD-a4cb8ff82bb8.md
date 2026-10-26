# S3 도메인 연결: a4cb8ff82bb8

- 확인일: 2026-10-26 KST.
- 실물: COM12의 ESP32-S3(QFN56), 리비전 v0.2, Flash 8MB, 내장 PSRAM 8MB.
- eFuse MAC: `a4:cb:8f:f8:2b:b8`. USB 변환기의 시리얼과 구분합니다.
- 제품 주소: http://s3-v1-2-a4cb8ff82bb8.local/
- 확인한 LAN 주소: http://192.168.0.34/ (DHCP로 변경될 수 있음).
- 공개 주소: https://s3-a4cb8ff82bb8.audreaming.com/
- 전용 중계: https://relay-s3.audreaming.com/
- Cloudflare Worker: `mini-web-s3`.
- 배포 설정: `wrangler.s3.toml`.
- 비공개 등록 파일: `provisioned/registry.s3.json`. 이 S3 한 대의 해시만 포함합니다.

## 준비 및 보존

기존 보드는 2026-09-28에 빌드된 펌웨어로 부팅했고, `/api/system`에서
위 제품·시리얼·mDNS 주소를 확인했습니다. 당시에는 ngrok URL이 표시되고
`cloud_status`가 없어 최신 중계 펌웨어가 필요했습니다.

사용자가 이번 S3의 빌드·업로드를 명시적으로 승인했습니다. 전체 8MB 플래시는
Git 제외 경로 `results/network-address-audit-20261026/s3-a4cb8ff82bb8-before.bin`에
백업했습니다. 백업에는 개인 설정이 들어갈 수 있으므로 공개·업로드하지 않습니다.

기존 M1/M3/C3 Worker와 S3 Zero Worker의 기기 등록을 보존하기 위해 별도
Worker를 사용합니다. S3 연결 토큰의 SHA-256 해시를 이 Worker의
`DEVICE_REGISTRY` secret으로 등록하는 작업은 사용자의 별도 승인 후 완료했습니다.
기기용 원본 토큰은 로컬 비공개 CSV에서만 읽어 해당 보드에 저장합니다.

## 유지보수

일반 S3 프로젝트의 중계 설정은 `https://relay-s3.audreaming.com`입니다.
S3 Zero는 공유 `main`을 사용하지만 자기 `sdkconfig`의
`https://relay-s3-zero.audreaming.com` 설정을 계속 사용합니다.

이미 발급된 토큰을 재발급하지 마세요. 기존 Wi-Fi·관리자·웹 파일을 유지하려면
NVS 전체를 새 CSV로 덮어쓰거나 전체 플래시를 삭제하지 마세요.

추가 S3를 연결할 때는 해당 보드의 실제 MAC, 도메인 경로, 서버 등록,
기기 토큰 저장을 함께 확인해야 합니다. 한 보드의 등록을 다른 보드에 재사용하지 않습니다.

## 검증 기록

- ESP-IDF 5.5.5 실제 빌드 성공. 앱 크기 `0x149f90`(1,351,568바이트), 기존 `0x1e0000` 슬롯의 약 31% 여유.
- COM12의 MAC을 재확인한 뒤 앱 `0x20000`만 업로드했고 기록 데이터의 해시 검증에 성공했습니다. NVS·파티션·부트로더·파일 영역은 기록하지 않았습니다.
- 로컬 관리자 API에서 기존 공장 기본 인증을 한 번 확인하여 토큰을 저장하고 정상 재부팅했습니다. 사용자 비밀번호 변경이나 설정 초기화는 하지 않았습니다.
- 2026-10-26 22:51 KST 전후 실제 `/api/system`에서 `cloud_status=online`, 위 공개 도메인과 LAN 주소를 확인했습니다.
- 로컬 홈 HTTP 200. 업데이트 전후 14,825바이트 페이지의 SHA-256 해시가 일치했습니다.
- 공개 도메인 HTTPS 200 및 Chrome에서 기존 시제품 시연 쇼케이스 허브 표시를 확인했습니다. 외부 `/admin`은 중계의 HTTP 403으로 차단됐습니다.
- mDNS는 기기에서 ready이며 초기 실제 HTTP 200을 확인했습니다. 후속 PC 재검사에서 한 번 시간 초과가 발생하여 이름 해석·연결의 지속 안정성은 보장하지 않습니다.
- 기본 Python User-Agent 요청은 Cloudflare 1010으로 차단됐습니다. 식별용 점검 User-Agent와 실제 Chrome에서는 공개 페이지를 확인했고, Cloudflare 보안 설정은 변경하지 않았습니다.
- 중계 서버 Node 테스트 31개, 네트워크 설정 호스트 검사 통과. 장시간 부하·모바일 데이터 회선 검사는 수행하지 않았습니다.
- 원시 검증 기록: `results/network-address-audit-20261026/s3-verification.json`, `s3-build.log`, `s3-flash.log`, `s3-serial-online.log`.

## 전시 QR

`output/pdf/Mini_WebServer_Exhibition_QR_A4_Landscape.pdf`에 이 S3를 포함했습니다.
C3·S3·S3 Zero·M1 1번·M3의 A4 가로 5쪽이며, 각 쪽은 mDNS와 공개 도메인
QR 2개입니다. M1 2번은 제외했습니다. PDF를 렌더링한 이미지에서 QR 10개를
모두 다시 해독하여 원래 URL과 일치함을 확인했습니다.
