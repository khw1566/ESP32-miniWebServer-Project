# M1 자동 연결: 무료 대회 시연

사용자는 전원과 Wi-Fi만 연결합니다. M1이 `relay.audreaming.com`으로 HTTPS 요청을 보내고 기다리면, Cloudflare Worker가 방문자의 요청을 보드로 전달합니다. 보드의 정적 웹 파일을 2 KiB씩 전달하며 집의 PC, 포트포워딩, 공인 IP가 필요 없습니다. 초기 제품 등록과 Cloudflare 배포는 제작자가 한 번 수행합니다.

2026-10-06 Cloudflare 중계 서버와 `audreaming.com`, `demo.audreaming.com`, `relay.audreaming.com`, `m1-b0cbd899de80.audreaming.com`의 Custom Domain 배포 및 기기 인증 해시 등록을 완료했습니다. HTTPS 응답은 확인했으며, 실제 보드의 연결 키 등록과 새 펌웨어 설치가 남아 있습니다. 현재 공개 페이지는 보드 오프라인 안내를 반환합니다. 펌웨어 빌드·업로드는 프로젝트 지침에 따라 사용자가 수행합니다. 현재 보드 절차는 [MAC별 연결 안내](BOARD-b0cbd899de80.md)를 참고하세요.

## 1. 제작자: 보드 등록

M1의 로컬 관리자 시스템 정보에서 시리얼(MAC 12자리)을 확인합니다. 이 폴더에서 실행합니다.

```powershell
node provision.mjs aabbccddeeff
```

예시 MAC 대신 실제 보드 값을 사용합니다. `provisioned/`에 기기별 NVS CSV와 `registry.json`이 만들어집니다. 중복 등록은 거부합니다. CSV에는 기기 비밀키가 있으며 서버 등록 파일에는 그 SHA-256 해시만 있습니다. 이 폴더를 저장소에 올리지 마세요.

`wrangler.toml`의 `DEMO_DEVICE`에 생성된 ID(`m1-실제MAC`)를 넣습니다. 현재 시연 보드는 `m1-b0cbd899de80`이며 `audreaming.com`과 `demo.audreaming.com`도 이 보드로 연결됩니다. 보드의 기본 QR은 MAC별 주소 `m1-실제MAC.audreaming.com`을 표시합니다. 여러 보드에서는 각 주소별 Custom Domain을 추가합니다. 주소와 토큰은 Wi-Fi가 바뀌어도 같습니다. 실제 Cloudflare 계정에서 구매 도메인을 확인하여 기존 문서의 `audeaming.com` 오기를 수정했습니다.

## 2. 제작자: Cloudflare에 무료 배포

Cloudflare 계정에서 Workers Free를 사용합니다. Durable Objects는 무료 플랜의 SQLite 형태로 구성했습니다. 기존 도메인에 웹사이트가 있다면 apex 도메인의 경로를 덮어쓰지 말고 `wrangler.toml`의 `audreaming.com` route를 제거한 뒤 demo 주소만 사용합니다.

```powershell
npm install
npx wrangler login
Get-Content -Raw -LiteralPath provisioned/registry.json | npx wrangler secret put DEVICE_REGISTRY --name mini-web-relay
```

마지막 명령이 등록 파일을 서버 설정으로 전달합니다. 이후 실행합니다.

```powershell
npm run deploy
```

최초 secret 설정에서 Worker 생성 여부를 물으면 생성합니다. 도메인은 같은 Cloudflare 계정에서 활성 상태여야 합니다. Custom Domains가 DNS와 인증서를 연결하므로 사용자는 DNS를 다룰 필요가 없습니다. 기존 충돌 DNS 레코드가 있으면 제작자가 배포 전에 확인합니다. 이 구현에서는 Cloudflare Tunnel이나 VPS를 따로 설치하지 않습니다.

무료 사용량: [Cloudflare 공식 요금표](https://developers.cloudflare.com/durable-objects/platform/pricing/) 기준 Durable Objects 하루 100,000요청, 13,000 GB-s입니다. 연결 대기 중에도 실행 시간이 계산됩니다. 보드 한 대를 하루 내내 켜두는 경우 0.128 × 86,400 = 약 11,059 GB-s로 추산됩니다. 다른 Worker/DO 사용량도 함께 계산되므로 대회 시연용 한 대를 기준으로 사용합니다. 초과하면 무료 플랜에서는 작업이 실패하며 서비스가 중단됩니다. 큰 파일을 2 KiB씩 전송하므로 요청 수도 확인하세요. 여러 보드의 상시 운영용 무료 구조로 보장하지 않습니다.

## 3. 제작자: 출고용 NVS 및 펌웨어

이미 사용 중인 보드는 Wi-Fi·관리자 설정을 유지하는 로컬 관리자 등록을 사용할 수 있습니다. 변경된 펌웨어를 먼저 설치한 뒤 `./Register-Board.ps1 -BoardUrl http://보드주소 -Serial 실제MAC12자리`를 실행하고, 로컬 관리자 비밀번호를 입력합니다. 이 방식은 연결 키만 NVS에 저장하며 보드 재부팅 후 적용됩니다. 실제 시연 보드 절차는 [MAC별 연결 안내](BOARD-b0cbd899de80.md)를 참고하세요. 다음 공장 NVS 생성 절차는 새 보드 또는 설정 초기화를 허용하는 경우에만 사용합니다.

ESP-IDF 5.5 환경에서 NVS 생성 도구로 CSV를 바이너리로 변환합니다. M1 파티션 테이블의 NVS는 `0x9000`, 크기 `0x6000`입니다.

```powershell
python "$env:IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" generate provisioned/m1-aabbccddeeff.csv provisioned/m1-aabbccddeeff.bin 0x6000
```

NVS 바이너리는 공장 초기 상태 보드에 `0x9000` 위치로 기록합니다. **기존 NVS 전체를 대체하므로 Wi-Fi·관리자·AI 설정이 초기화됩니다. 기존 보드의 설정을 유지해야 하면 먼저 백업하거나 기존 공장 CSV와 합쳐 생성하세요.** 모든 보드에 같은 토큰을 넣지 않습니다. firmware flash만으로 NVS 토큰은 생성되지 않습니다. 등록 후 전체 flash erase를 하면 출고 NVS도 다시 기록해야 합니다.

M1에서 새 설정 `Mini WEB automatic cloud connection`의 URL과 도메인을 확인합니다. 기본값은 이 배포에 맞춰져 있습니다. 기존 sdkconfig가 있으면 `menuconfig`에서 인증서 번들(`MBEDTLS_CERTIFICATE_BUNDLE`)을 활성화하고 public hostname을 `demo.audreaming.com`으로 지정합니다. 새 설정 생성 시 sdkconfig.defaults에 번들 설정을 적용합니다. 펌웨어를 사용자가 빌드·업로드합니다. 앱 파티션 3008 KiB 이내인지와 TLS 연결 중 여유 RAM을 확인해야 합니다.

## 4. 사용자: 전원과 Wi-Fi

1. 전원을 켜고 기존 온보드/SoftAP 방식으로 Wi-Fi를 연결합니다.
2. 보드가 시간을 동기화하고 중계 서버에 자동으로 접속합니다. 첫 연결은 약 20초 이상 걸릴 수 있습니다.
3. 홈의 `외부: 연결`을 확인하고 `접속 QR`을 누릅니다. 화면 제목이 `외부 접속 QR`이면 인터넷 접속용 주소입니다.
4. 휴대폰 Wi-Fi를 끄고 모바일 데이터로 접속합니다.
5. Wi-Fi 연결 해제·재연결, 전원 재부팅 후 같은 주소로 복구되는지 확인합니다.

미등록이면 `외부: 등록 필요`가 표시됩니다. 클라우드에 연결되지 않았으면 QR은 기존 로컬 IP를 표시하고 화면에 `로컬 접속 QR`로 구분합니다. 관리자 `/api/system`의 `cloud_status`와 `tunnel_url`로도 확인합니다. 저장된 Wi-Fi는 다음 부팅에 자동 연결합니다.

## 제공 범위와 검증

SD 미디어 업로드를 추가했습니다. 새 공장 등록은 연결 토큰과 다른 업로드 키도 발급합니다. 기존 등록은 `node issue-upload-key.mjs 실제MAC12자리`로 키를 추가한 뒤 registry secret과 Worker를 업데이트합니다. `/__upload/`는 소유자 인증 후 보드에 begin/chunk/commit/abort 명령을 전달하며 보드 저장 확인 후에만 성공을 응답합니다. 준비 절차는 [SD·태평양 전쟁 웹페이지 안내](../web_examples/10_pacific_war/README.md)를 참고하세요.

- 보드에 업로드된 첫 페이지 또는 `/index.html`, CSS·JS·이미지·지원 미디어 파일의 GET 요청.
- 외부 관리자/AI/Wi-Fi 설정은 전달하지 않습니다. 외부 POST는 소유자 인증을 받은 SD 미디어 업로드만 지원합니다. 사용자 페이지의 임의 동적 API는 아직 지원하지 않습니다.
- 파일 이름은 ASCII 문자·숫자·`_-.` 및 폴더 구분자 `/`만 지원합니다. 한글 파일명이나 percent encoding은 아직 지원하지 않습니다. 쿼리는 파일 선택에 사용하지 않습니다.
- HTTP Range는 기존 M1의 MP3/MP4 처리에 전달됩니다. 파일 크기 최대 8 MiB, 보드당 동시 대기 4개, 한 요청 최대 60초입니다. 보드의 저장 공간과 속도가 실제 제한입니다. 큰 영상은 시연 범위에서 제외하는 것이 좋습니다.
- 보드가 꺼지면 오프라인 안내를 반환합니다. Durable Object가 재시작되면 진행 중 요청은 실패할 수 있으며 보드의 다음 poll로 복구합니다.
- 인증서 검증과 시간 동기화를 사용합니다. 비밀 토큰은 URL, UI, 로그에 표시하지 않습니다.

```powershell
npm test
```

테스트는 인증, HTML/바이너리 전달, Range, 중복 chunk, 시간 초과·복구, 오프라인, 관리자 차단, 큐 제한을 검사합니다. Node에서 프로토콜을 검증하며 실제 Cloudflare 런타임이나 M1 동작을 대체하지 않습니다.

Cloudflare 로컬 런타임 검증은 `npx wrangler dev --local`을 사용합니다. 테스트용 `.dev.vars`에 `DEVICE_REGISTRY`를 설정하고 host를 `relay.audreaming.com`, `demo.audreaming.com`으로 지정해 요청합니다. `.dev.vars`와 공장 파일은 git에 포함하지 않습니다.
