# M3 유튜브 중계 서버 (tube.audreaming.com)

M3 보드(ESP32-P4)가 유튜브를 검색하고 재생할 수 있도록 이 PC가 중계합니다. 보드는 `https://tube.audreaming.com`에만 접속하고, 이 주소는 Cloudflare Tunnel로 PC의 `127.0.0.1:8790`에 연결됩니다. 공유기 포트포워딩이나 공인 IP는 필요 없습니다.

```
M3 보드 --HTTPS--> tube.audreaming.com (Cloudflare) --Tunnel--> PC: server.mjs --> yt-dlp / ffmpeg --> YouTube
```

- **검색·채널 목록**: `yt-dlp`로 페이지당 10개를 가져옵니다. 같은 검색어의 다음 페이지는 앞 페이지에 나온 영상을 건너뛰어 중복을 막습니다(10분 캐시).
- **썸네일**: 160x90 RGB565로 변환해 보드가 디코딩 없이 바로 표시합니다. `cache/thumb`에 저장됩니다.
- **재생**: `ffmpeg`가 영상을 480x272, 15fps MJPEG(보드의 하드웨어 JPEG 디코더용)와 24kHz 스테레오 PCM으로 바꿉니다. 두 스트림을 시간순으로 섞은 패킷(`M3TV`)으로 보냅니다. 보드가 재생 속도로 읽으므로 PC도 그 속도에 맞춰 변환합니다. 대역폭은 약 2.5~3Mbps입니다.
- **탐색(seek)**: `?t=초`로 해당 위치부터 다시 변환합니다. googlevideo는 범위 지정 없는 요청을 약 1.5MB에서 끊기 때문에, ffmpeg는 내부 범위 프록시(`127.0.0.1:8791`, 외부 비공개)를 통해 1MB씩 받습니다.
- **보안**: 모든 `/api` 요청에 `x-m3-token` 헤더가 필요합니다. 토큰은 `config.json`과 보드 헤더 `m3_tube_private.h`에만 있으며 둘 다 Git 제외입니다. 재생은 한 번에 하나만 허용합니다.

## 준비물 (이 PC에 이미 설치됨)

- Node.js 20 이상, ffmpeg (PATH), cloudflared (audreaming.com 계정에 로그인됨)
- yt-dlp: `python -m pip install -U "yt-dlp[default]"`. 유튜브 변경으로 검색·재생이 실패하면 먼저 이 명령으로 업데이트하세요. 기존 `yt-dlp.exe`는 삭제된 Python 3.13을 가리켜 동작하지 않으므로, 서버는 `python -m yt_dlp`를 사용합니다(`config.json`의 `ytdlp`로 변경 가능).

## 처음 한 번

```powershell
cd tube-relay
node setup.mjs            # 토큰 생성, config.json 및 보드 헤더 m3_tube_private.h 작성
.\setup-tunnel.ps1        # Cloudflare Tunnel "m3-tube"와 DNS tube.audreaming.com 생성
```

두 단계는 2026-10-13에 이미 수행했습니다. 터널 ID는 `257b5195-6a52-4076-a4b9-5605616da61d`입니다. 그 다음 M3 펌웨어를 빌드·설치합니다. 토큰이 `m3_tube_private.h`로 펌웨어에 들어갑니다.

## 사용할 때마다: `M3TubeRelay.exe`

이 폴더의 **`M3TubeRelay.exe`를 더블클릭**합니다. 창 없이 작업 표시줄 알림 영역(시계 옆)에 빨간 재생 아이콘이 생기고, 중계 서버와 Cloudflare 터널이 백그라운드에서 실행됩니다.

| 아이콘 점 색 | 의미 |
| --- | --- |
| 초록 | 보드에서 사용 가능 (`tube.audreaming.com` 응답 확인) |
| 노랑 | PC 중계 서버는 정상, 터널 연결 대기 중 |
| 빨강 | 중계 서버 시작 대기 중 또는 오류 |

- 아이콘을 오른쪽 클릭하면 다음 메뉴가 나옵니다: 상태, 공개 주소 확인, 중계 서버·터널 로그 보기(`relay.log`, `cloudflared.log`), 폴더 열기, **Windows 시작 시 자동 실행**, 다시 시작, 종료.
- 중계 서버나 터널이 비정상 종료되면 10초 뒤 자동으로 다시 시작하고 알림을 띄웁니다.
- 실행기가 종료되거나 강제 종료되어도 node·cloudflared가 남지 않습니다(Windows Job Object). 두 번 실행하면 이미 실행 중이라고 알려 줍니다.
- 처음 실행할 때 `config.json`이나 `cloudflared.yml`이 없으면 `setup.mjs`, `setup-tunnel.ps1`을 자동으로 실행합니다.
- `M3TubeRelay.exe`는 반드시 이 폴더(`server.mjs` 옆)에 두세요. 바탕화면에는 바로가기만 만드세요.

실행 파일을 다시 만들려면 `.\build-exe.ps1`을 실행합니다. Windows에 포함된 .NET Framework 4 C# 컴파일러를 사용하며, 소스는 `launcher/M3TubeRelay.cs`입니다. 창을 띄워 로그를 직접 보고 싶다면 기존 `.\start.ps1`도 계속 쓸 수 있습니다.

동작 확인은 `https://tube.audreaming.com/healthz`에서 `{"ok":true}`가 나오면 됩니다.

토큰을 바꾸려면 `node setup.mjs --rotate`를 실행한 뒤 펌웨어를 다시 빌드합니다.

## 확인 결과와 한계

- 호스트 테스트: `node test.mjs`로 문자 정리, JPEG 분리, PCM 분할, 패킷 형식, 섞는 순서와 역압을 검사합니다.
- 실제 측정(2026-10-13, 이 PC): `tube.audreaming.com`을 거친 검색 약 2.3초, 썸네일 약 0.2초, 재생 첫 데이터 약 2.9초가 걸렸습니다. 181초 분량을 받는 동안 프레임 2712개가 모두 정상 JPEG였고, 영상과 음성 패킷의 순서 차이는 최대 107ms였습니다.
- 실제 보드에서의 재생 부드러움, 음성·영상 동기, Wi-Fi(C6) 처리량은 아직 확인하지 않았습니다.
- 보드 화면에 맞게 제목의 이모지와 일본어·중국어 등은 제거하고, 한글·영문·기본 기호만 남깁니다.
- 연령 제한·회원 전용·지역 제한 영상은 재생되지 않을 수 있습니다. 라이브는 시작 위치 이동 없이 재생합니다.
- 유튜브 이용약관은 공식 플레이어 외의 방법으로 영상을 내려받거나 다시 전송하는 것을 제한합니다. 개인 기기에서 개인적으로 볼 때만 사용하고, 외부에 공개하거나 다른 사람에게 서비스하지 마세요.
