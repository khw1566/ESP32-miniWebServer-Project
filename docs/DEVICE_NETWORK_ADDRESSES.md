# C3·S3·S3 Zero·M1·M3 접속 주소 및 연결 점검

## 2026-10-26 23:09 KST C3 스페어 추가 연결 완료

기존 C3와 다른 스페어 `e83dc19c243c`를 추가했습니다.
전체 4MB 백업 후 앱만 업로드했고 기존 Wi-Fi·홈 파일을 유지했습니다.

- mDNS: http://c3-v1-2-e83dc19c243c.local/
- 외부: https://c3-e83dc19c243c.audreaming.com/
- LAN: http://192.168.0.36/ (DHCP 변경 가능).
- `cloud_status=online`, mDNS와 외부 도메인 모두 HTTP 200. USB 시리얼 입력과 Chrome 표시 확인.
- 전용 Worker: `mini-web-c3-spare`, 전용 중계: `https://relay-c3-spare.audreaming.com`.
- 상세 기록: `cloud-relay/BOARD-e83dc19c243c.md`.
- 별도 전시 PDF: `output/pdf/C3_Spare_Exhibition_QR_A4_Landscape.pdf` (A4 가로 1쪽, QR 2개).
- 기존 C3 주소와 기존 5쪽 전시 PDF는 유지했습니다.

## 2026-10-26 22:51 KST S3 연결 완료

USB COM12의 실제 ESP32-S3 시리얼은 `a4cb8ff82bb8`입니다. 전체 플래시 백업 후
사용자가 승인한 최신 앱 업로드와 전용 서버 등록을 완료했습니다.

- mDNS: http://s3-v1-2-a4cb8ff82bb8.local/
- 외부: https://s3-a4cb8ff82bb8.audreaming.com/
- LAN: http://192.168.0.34/ (DHCP 변경 가능).
- `cloud_status=online`, 실제 공개 페이지 HTTPS 200, Chrome 표시 확인.
- 기존 로컬 홈 페이지의 업데이트 전후 해시 일치. 외부 `/admin`은 403.
- 전용 Worker는 `mini-web-s3`, 중계는 `https://relay-s3.audreaming.com`입니다.
- 상세 기록: `cloud-relay/BOARD-a4cb8ff82bb8.md`.
- 전시 PDF: `output/pdf/Mini_WebServer_Exhibition_QR_A4_Landscape.pdf`.

아래는 **22:27 최초 조사 기록**입니다. 당시 S3 미확인 상태는 위 실물 확인과
연결 결과로 갱신되었으며, 다른 기기의 오프라인 판정은 당시 점검 시각 기준입니다.

점검 시각: 2026-10-26 22:27 KST. 현재 작업 폴더의 소스·배포 설정·기존 보드 기록과 PC 네트워크의 DNS/HTTP GET 결과를 조사했습니다.

**결론: 확인된 기기 도메인 5개는 DNS와 HTTPS 중계 서버에 연결되지만, 모두 HTTP 503을 반환하여 현재 보드의 공개 페이지 접속은 확인되지 않았습니다. 일반 S3는 실제 시리얼을 확인하지 못했습니다.**

## 기기별 주소

| 기기 | 시리얼 / 식별 근거 | mDNS URL | 외부 도메인 | 현재 외부 응답 |
| --- | --- | --- | --- | --- |
| C3 | `90da728a2b18` / 보드 기록·기본 Worker 경로 | http://c3-v1-2-90da728a2b18.local/ | https://c3-90da728a2b18.audreaming.com/ | HTTPS 503, 보드 오프라인 안내 |
| S3 | 실제 시리얼 미확인 | `http://s3-v1-2-<실제MAC12자리>.local/` | `https://s3-<실제MAC12자리>.audreaming.com/` | 실제 주소·등록·접속 미확인 |
| S3 Zero | `90da72475334` / README·실기 직렬 로그·전용 Worker 경로 | http://s3-v1-2-90da72475334.local/ | https://s3-90da72475334.audreaming.com/ | HTTPS 503, 보드 오프라인 안내 |
| M1 1번 | `b0cbd899de80` / 보드 기록·기본 Worker 경로 | http://m1-v1-2-b0cbd899de80.local/ | https://m1-b0cbd899de80.audreaming.com/ | HTTPS 503, 보드 오프라인 안내 |
| M1 2번 | `20500d34b5f0` / 기본 Worker 경로·로컬 등록 ID | http://m1-v1-2-20500d34b5f0.local/ | https://m1-20500d34b5f0.audreaming.com/ | HTTPS 503, 보드 오프라인 안내 |
| M3 | `80f1b2d68499` / 기본 Worker 경로 | http://m3-v1-2-80f1b2d68499.local/ | https://m3-80f1b2d68499.audreaming.com/ | HTTPS 503, 보드 오프라인 안내 |

M1은 설정에 서로 다른 2대가 있어 모두 기재했습니다. M3 시리얼은 배포 경로에서 확인한 값이며 이번에 실물 `/api/system`으로 다시 대조하지 못했습니다. 일반 S3 주소의 꺾쇠 부분은 자리표시자이며 실제 접속 주소가 아닙니다. `docs/MDNS_ACCESS.md`의 `a4cb8ff82bb8`도 형식 설명용 예시이므로 실물 시리얼로 사용하지 않았습니다.

## mDNS 점검과 주소 생성 규칙

- 소스의 기본 주소 규칙은 `http://<제품명>-v1-2-<공장 eFuse MAC 12자리>.local/`이며 HTTP 80 포트입니다.
- S3 Zero는 `mini-WEB (S3-Zero-ESP-IDF_ver)/CMakeLists.txt`에서 일반 S3의 `main`을 공유하므로 제품명은 `s3`입니다. `s3-zero`가 들어간 mDNS 주소를 만들지 않습니다.
- 주소용 버전은 `product_id/addr_version` NVS 값을 보존합니다. 기존 저장값이나 mDNS 이름 충돌 처리에 따라 실제 표시 주소가 달라질 수 있으므로 기기의 QR 또는 `/api/system` 값이 최종 기준입니다.
- 이번 PC의 운영체제 이름 해석에서는 C3·S3 Zero·M1 2번·M3의 `.local` 이름이 모두 공인 IP `218.38.137.28`로 반환되고 HTTP 접속이 시간 초과되었습니다. M1 1번은 이름 해석 자체가 실패했습니다. 이 결과는 보드의 실제 mDNS 응답을 확인한 증거가 아니며, 위 공인 IP를 보드 IP로 사용하면 안 됩니다.
- 전용 UDP 5353 mDNS 패킷 검사나 실물 보드 API 대조는 수행하지 않았습니다. 같은 Wi-Fi/LAN에서 전원·공유기 연결·기기 격리·mDNS 차단 여부를 확인한 뒤 재점검해야 합니다.

## 외부 도메인 점검

확인된 기기 도메인 5개 및 아래 공용 도메인은 모두 `104.21.78.119`, `172.67.220.160`으로 이름이 해석되었습니다. 인증서 검증을 활성화한 HTTPS 요청으로 실제 HTTP 응답을 받았습니다.

| 주소 | 역할 | 이번 응답 | 판정 |
| --- | --- | --- | --- |
| https://audreaming.com/ | 기본 시연 주소 | 503 | 중계까지 도달, 보드 오프라인 |
| https://demo.audreaming.com/ | 시연 별칭 | 503 | 중계까지 도달, 보드 오프라인 |
| https://relay.audreaming.com/ | C3·M1·M3·일반 S3의 기본 중계 API | 404 | 서버 도달 확인. `/` 페이지가 없는 API이므로 이 응답만으로 장애라고 판단하지 않음 |
| https://relay-s3-zero.audreaming.com/ | S3 Zero 전용 중계 API | 404 | 서버 도달 확인. 보드 연결 성공을 의미하지 않음 |

`cloud-relay/worker.mjs`에서 보드 오프라인은 503, 미등록 주소는 404로 처리합니다. 따라서 이번 기기 도메인의 오프라인 응답은 도메인 연결과 실제 보드 연결을 구분해야 함을 보여줍니다. 전원 꺼짐, Wi-Fi 단절, 토큰/등록 문제 중 어느 것이 원인인지는 이 공개 응답만으로 확정할 수 없습니다.

기본 시연 주소는 `cloud-relay/wrangler.toml`의 `DEMO_DEVICE=m1-b0cbd899de80`으로 설정되어 있습니다. S3 Zero는 `wrangler.s3-zero.toml`의 별도 Worker를 사용합니다. 현재 로컬 등록 파일의 ID 목록과 배포 설정이 완전히 같지는 않으므로 로컬 파일을 그대로 서버에 덮어쓰면 안 됩니다. 이번 조사에서는 등록·배포·토큰·펌웨어를 변경하지 않았습니다.

## 일반 S3에 남은 확인

일반 S3의 실제 MAC을 확인할 수 있는 실물 응답·직렬 로그·전용 도메인 경로를 이번 조사 범위에서 찾지 못했습니다. S3 Zero의 MAC을 일반 S3에 재사용하지 않았습니다. 일반 S3 보드의 로컬 `/api/system` 또는 부팅 로그에서 시리얼과 mDNS 주소를 확인한 뒤, 해당 도메인의 Worker 경로·서버 등록·기기 토큰과 HTTPS 페이지 응답을 점검해야 합니다.

## 근거와 재점검 파일

- 주소 생성: 각 프로젝트의 `main/network_mdns.inc`.
- 외부 URL 생성: 각 프로젝트의 `main/cloud_relay.c`, `main/Kconfig.projbuild`, `sdkconfig`의 공개 도메인 설정.
- 배포 경로: `cloud-relay/wrangler.toml`, `cloud-relay/wrangler.s3-zero.toml`.
- 기기 식별 기록: `cloud-relay/BOARD-90da728a2b18.md`, `cloud-relay/BOARD-b0cbd899de80.md`, `mini-WEB (S3-Zero-ESP-IDF_ver)/README.md`, `results/s3-zero-20261026/serial-reboot-verified.log`.
- 기계 판독 결과: `results/network-address-audit-20261026/checks.json`.
- 주소 목록 CSV: `docs/DEVICE_NETWORK_ADDRESSES.csv`.
- 읽기 전용 재점검: 저장소 루트에서 `python results/network-address-audit-20261026/check_addresses.py`. 실행하면 `checks.json`이 새 시각의 결과로 갱신됩니다. 이 문서와 CSV는 자동 갱신되지 않습니다.

기존 S3 Zero 기록의 HTTPS 200은 과거 확인 결과입니다. 이번 점검의 503과 혼동하지 않도록 현재 판정은 이번 네트워크 결과를 기준으로 했습니다.
