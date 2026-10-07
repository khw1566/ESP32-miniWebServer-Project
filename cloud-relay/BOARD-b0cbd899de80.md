# M1 b0cbd899de80 도메인 연결

이 보드의 MAC은 `b0:cb:d8:99:de:80`, 서버 등록 ID는 `m1-b0cbd899de80`입니다.

- 보드 주소: `https://m1-b0cbd899de80.audreaming.com/`
- 시연 주소: `https://audreaming.com/`, `https://demo.audreaming.com/`
- 보드 연결 서버: `https://relay.audreaming.com`

2026-10-06 Cloudflare `mini-web-relay` 배포, 위 네 주소의 Custom Domain 연결, `DEVICE_REGISTRY` 등록을 완료했습니다. 공개 주소의 HTTPS 응답도 확인했습니다. 현재 보드는 `cloud_status: factory_missing`으로 연결 키가 없으며, 사용자가 새 펌웨어를 아직 설치하지 않았습니다. 따라서 공개 페이지는 보드 오프라인 안내를 반환합니다. 아래 펌웨어 설치와 키 등록을 마쳐야 보드 웹페이지가 열립니다. `provisioned/` 파일은 공개하거나 저장소에 올리지 않습니다.

## Cloudflare

최초 배포는 완료했습니다. 이후 중계 서버를 변경할 때만 이 폴더에서 실행합니다.

```powershell
npx wrangler login
Get-Content -Raw -LiteralPath provisioned/registry.json | npx wrangler secret put DEVICE_REGISTRY --name mini-web-relay
npm run deploy
```

기존 Worker가 있으면 등록 파일에 다른 기기가 있는지 확인한 뒤 병합해야 합니다. 기존 도메인의 DNS와 서비스 충돌도 확인합니다.

## 현재 보드 설정 유지하며 등록

프로젝트 지침에 따라 사용자가 M1의 변경된 펌웨어를 빌드·업로드합니다. 공장 NVS 바이너리로 기존 NVS를 덮어쓰는 대신, 다음 관리자 등록을 사용하면 Wi-Fi와 관리자 설정을 유지합니다.

프로젝트 루트(`Mini WebServer`)에서 다음 명령을 실행합니다.

```powershell
& '.\cloud-relay\Register-Board.ps1'
```

현재 로컬 관리자 비밀번호를 입력합니다. 스크립트는 실제 보드 MAC을 확인하고, 로컬 `/api/settings`에 인증하여 연결 키를 저장합니다. 기존 펌웨어는 이 명령을 지원하지 않으므로 새 펌웨어를 먼저 설치해야 합니다. 로컬 HTTP 연결은 신뢰하는 Wi-Fi에서 제작자가 한 번 수행합니다.

보드 전원을 다시 켜면 자동으로 중계 서버에 연결합니다. `/api/system`에서 `cloud_status`가 `online`이고 `tunnel_url`이 보드 도메인인지 확인합니다. 휴대폰 모바일 데이터로 외부 주소 접속을 확인합니다.

SD 미디어 업로드에 쓰는 소유자 키는 `provisioned/m1-b0cbd899de80-upload-key.txt`에 있습니다. 연결 키와 별개이며 공개 페이지에 넣지 않습니다.

## TLS 메모리 부족 로그

`mbedtls_ssl_setup returned -0x7F00`은 TLS 메모리 할당 실패입니다. 2026-10-06 수정에서는 LCD 전송 버퍼를 20행에서 8행으로, LVGL 풀을 64 KiB에서 48 KiB로, Wi-Fi 고정 수신 버퍼를 10개에서 6개로 줄였습니다. LCD 버퍼 두 개에서 11,520바이트와 LVGL에서 16,384바이트를 절약하며 Wi-Fi도 추가 여유를 확보합니다. TLS 동적 버퍼를 켰고, 서버의 큰 TLS 레코드 수신을 위해 수신 한도 16 KiB는 유지합니다. 인증서 검증도 유지합니다.

추가 로그에서 소켓 부족과 TLS 메모리 부족이 함께 확인되어 후속 수정했습니다. LVGL은 48 KiB 고정 풀 대신 표준 malloc/free/realloc으로 화면에 필요한 메모리를 할당합니다. 전체 소켓 한도는 10개에서 16개로 늘리고 웹 서버의 동시 접속은 4개로 제한해 중계·mDNS·DNS·SNTP 여유를 둡니다. 관리자 상태 조회는 2.5초, 로그는 7초 간격이며 이전 요청이 끝나기 전 새 요청을 보내지 않습니다. 로컬 파일 전송이 실패해도 클라우드가 취소 처리를 확인하면 기존 TLS 연결을 유지합니다.

송신 시간 초과(`send: 11`) 후 연결 초기화(`send: 104`) 로그에 대응해 중계 파일 읽기도 변경했습니다. 중계 태스크는 `web_public_stream.inc`를 통해 SD/SPIFFS 파일을 직접 2 KiB씩 읽습니다. 자기 웹 서버에 HTTP 요청을 보내는 구조를 제거해 중계의 느린 전송이 관리자 웹 서버의 송신을 막지 않도록 했습니다. 공개 경로 제한, SD 우선 제공, 초기 HTML 선택, 미디어 목록 및 Range 응답은 유지합니다. 끊어진 연결에는 종료 청크와 추가 500 응답을 보내지 않고 즉시 정리하며, 마지막 청크 실패도 호출자에 전달합니다. 실제 하드웨어에서 동시 접속 재검증이 필요합니다.

TLS 연결 시작 전 내부 RAM 여유 64 KiB와 최대 연속 블록 24 KiB를 확인합니다. 부족하면 `waiting_memory`로 대기하며 실제 값을 로그에 출력합니다. 이는 여유 확보용 기준이며 모든 TLS 핸드셰이크의 성공을 보장하는 수치는 아닙니다. 수정 후 펌웨어를 다시 빌드·설치하고 홈, 전체 키보드, 저장소 화면, 외부 페이지 및 SD 전송을 확인해야 합니다. 하드웨어 검증과 펌웨어 빌드는 아직 수행하지 않았습니다. 연결 실패 로그의 `free`, `largest` 값이 다음 진단에 필요합니다.
