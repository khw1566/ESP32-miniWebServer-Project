# Mini WebServer M3 — GUITION JC-ESP32P4-M3

개발 지침: [AGENTS.md](AGENTS.md) · [최적화·PSRAM·PC 시뮬레이터 검증 절차](docs/OPTIMIZATION_AND_VALIDATION.md).

홈 배경·3×3 앱·상단 CPU 그래프·공개 Wi-Fi 즉시 연결·라디오 음질 중계:
[변경 내용과 적용 절차](docs/HOME_AND_RADIO.md).

갤러리: [이미지·동영상 격자와 첫 프레임 미리보기](docs/GALLERY_GRID.md).
서비스 웹: [SD `Main WebFlie` 폴더의 부팅 시 PSRAM 적재](docs/MAIN_WEB_PSRAM.md).

소리·접근성: [부팅 멜로디, 개별 음량과 3개 언어 안내음성 설치](docs/SYSTEM_SOUNDS.md).

사용자가 제공한 GUITION 4.3인치 480×800 IPS 정전식 터치 제품 사진과 사양에 맞춰 **JC4880P443C_I_W** 배선 프로파일로 작성한 ESP-IDF 5.5.5 펌웨어입니다. ESP32-P4 + C6, 32MB PSRAM, 16MB Flash를 대상으로 합니다. 사진만으로 PCB revision까지 확정할 수 없으므로 첫 설치 전 뒷면 모델명과 배선을 확인하세요. 다른 화면 모듈에 이 프로파일을 그대로 사용하지 마세요.

소스와 호스트 테스트 단계입니다. 프로젝트 규칙에 따라 에이전트는 ESP-IDF 빌드·업로드를 실행하지 않았으며 실기기 동작·FPS·장시간 안정성은 검증하지 않았습니다.

## 구현 기능

- Gemini AI Agent: 3.1 Flash-Lite / 3.5 Flash-Lite 선택, 한국어 대화, 보드 상태 조회·음량 조절·앱 열기·오디오 정지·선택된 TTS로 음성 출력 도구 호출. [키 설정과 사용법](docs/GEMINI_AGENT.md).
- 상단 스와이프 작업 패널: 앱·서비스 카드와 내부 RAM·PSRAM·Flash 파일 저장공간 원형 그래프. [사용량 기준](docs/ACTIVITY_PANEL.md).
- ES8311 마이크 한국어 음성입력: 최대 8초 녹음, OpenAI mini-transcribe 기본 / Gemini 3.1·3.5 선택, 결과 확인 후 입력창 적용. [음성입력 설명](docs/VOICE_INPUT.md).
- 제조사 치수 PDF와 회로도: [케이스 제작용 치수 자료](docs/hardware/CASE_DIMENSIONS.md).

- 480×800 세로형 LVGL 9 터치 UI: 좌우 스와이프로 캘린더 → 메인 → 추가 앱 → SD 사진 위젯을 탐색합니다. 메인에는 시계·날짜·Wi-Fi 상태 카드와 앱 6개, 추가 앱에는 나머지 앱을 둡니다. 하단 페이지 표시로 바로 이동하며 앱에서 홈으로 돌아오면 메인 페이지를 표시합니다. 달력은 월 이동·오늘 강조, 사진은 SD `widgets/photos`의 JPG/JPEG·BMP를 7초마다 전환하고 이전/다음·일시정지·새로고침을 지원합니다. 배경 원은 별도 뒤쪽 레이어로 묶어 시계·Wi-Fi 카드·아이콘이 항상 앞에 그려지도록 했습니다. 상단 한국 시간·네트워크/오디오 상태와 작업 패널, 하단 70픽셀 위로 밀기 홈 복귀는 유지합니다. [홈 위젯과 SD 사진 준비](docs/HOME_WIDGETS.md).
- 한국어/영어/일본어 화면: 설정 앱의 `언어 / LANGUAGE`에서 `한국어 / English / 日本語`를 선택하여 재부팅 없이 바로 전환합니다. 선택은 NVS(`m3_ui` / `lang`)에 저장되며 기본값은 한국어입니다. 일본어 메뉴·서비스 상태·날짜·달력 요일과 히라가나/가타카나 입력, 탁점·반탁점·작은 글자 변환을 지원합니다. [일본어와 순환 텍스트 안내](docs/JAPANESE_AND_TEXT.md). 화면 문구는 [`main/m3_i18n.c`](main/m3_i18n.c)에 모여 있고, Wi-Fi·오디오·라디오·AI·음성 서비스가 보내는 영문 상태 메시지도 한국어로 바꿔 표시합니다. 번역표에 없는 메시지(WAV 파일 이름 등)는 원문 그대로 나옵니다.
- 앱별 개선: 라디오는 방송국 목록을 누르면 바로 재생하고, 다른 소리가 재생 중이면 정지가 끝난 뒤 자동으로 이어서 시작합니다(최대 6초 대기). 파일은 Flash/SD 탭, 경로·사용량 막대, 크기 표시 목록, 페이지 번호를 제공합니다. Wi-Fi 검색 결과를 누르면 SSID가 채워지고 암호 입력으로 바로 이동하며, 고정 IP 입력은 2열로 배치합니다. 음량 슬라이더는 오디오·라디오·음성 합성 앱과 AI의 음량 변경 사이에서 동기화됩니다. 음성 입력은 하단 시트로 열리며 마이크 버튼은 SSID, 음성 합성 문장, AI 질문 입력란에만 둡니다. 2048은 값별 타일 색상과 점수/최고 점수 카드를 사용합니다.
- 글꼴: 본문은 기존 16px Nanum Gothic 한글(완성형 11,172자)과 Montserrat 16 대체 글꼴입니다. 한글이 영문보다 약 4픽셀 위에 그려지던 기준선 문제를 고쳤습니다. 제목은 22px 반굵은 한글과 기존 한글/CJK 데이터를 재사용하는 22px 확대 대체 글꼴, Montserrat 20을 연결하며 실제 비트맵 기준 줄 높이를 사용합니다. 긴 파일명·제목·상태·버튼 문구는 고정 영역 안에서 순환 스크롤합니다. 홈 시계는 84px 숫자 전용 글꼴, 아이콘은 Montserrat 28 기호입니다. 한글 제목 문구를 바꾸면 `python tools/generate_ui_fonts.py`로 제목·시계 글꼴을 다시 만듭니다.
- Wi-Fi 앱에서 주변 네트워크 검색·선택, SSID·암호 입력, DHCP/서버 고정 IPv4·게이트웨이·마스크·DNS를 설정합니다. 입력란을 누르면 영문 대소문자·숫자·특수문자 가상 키보드가 열리고 IP 입력에는 숫자 키패드가 열립니다. 입력란은 키보드 위로 스크롤되며 확인(체크) 키나 키보드 아이콘 키로 키보드를 닫습니다. 암호는 기본 마스킹하고 연결 요청 후 입력란에서 지웁니다. 저장된 암호를 화면에 읽어오지 않으므로 다시 연결할 때 입력해야 합니다.
- `저장 후 연결`(Save and connect)은 NVS 저장 후 재부팅 없이 연결을 시도합니다. 실패 시 설정 SoftAP로 돌아오고, `설정 핫스팟 켜기`(Start setup hotspot)로도 전환할 수 있습니다. 검색·저장·연결은 코어 0 서비스 큐에서 처리하며 진행 중 중복 요청은 받지 않습니다. 잘못된 주소나 너무 짧은 암호는 저장 전에 거부합니다.
- ST7701S MIPI-DSI 2-lane 500Mbps, RGB565, 34MHz DPI. PSRAM 프레임버퍼 2개(합계 1,536,000 bytes)를 LVGL과 LCD가 공유하고 공식 포트의 프레임 완료 동기화를 사용합니다. DMA2D 복사 기능도 활성화합니다. 전체 프레임을 복제하는 별도 UI 버퍼는 만들지 않습니다.
- 코어 1의 LVGL 작업과 코어 0의 HTTP·서비스·오디오 작업 분리. 1초 메모리 이력 그래프와 250ms 상태 표시. 회전 큐브 시험 그래픽은 제거했습니다. 이 주기는 측정 FPS가 아닙니다.
- 실제 내부 힙·PSRAM 여유량, 최소 내부 힙, 최대 연속 블록, 홈 요청 수, Wi-Fi 끊김 횟수, RSSI, SD/터치/오디오 상태 표시.
- 홈 첫 번째 **웹 서버** 앱: **도메인 / IPv6 / IPv4 / .local** QR 탭을 항상 표시합니다. 외부 도메인은 릴레이 오프라인에도 주소를 보여 주며 연결 상태를 안내합니다. 공인 IPv6는 `http://[주소]/` QR로 자동 갱신하고, 미발급 이유와 재요청 버튼을 제공합니다. 흑백 QR은 216px와 흰 여백 32px를 사용합니다. 메모리·이력 그래프도 유지됩니다. [주소별 사용 조건과 검증](docs/WEB_ADDRESS_QR.md).
- 대기 애니메이션: 기다리는 동안 움직이는 표시를 보여 줍니다.
  - 점 3개 물결(진행 중): AI 답변 생성(이전 답변은 흐리게 표시), 음성 인식 전송, 음성 합성 준비(음성 다운로드·엔진 시작), 유튜브 연결·버퍼링.
  - 막대 5개 레벨(소리 진행 중): 마이크 녹음 중, 음성 합성으로 말하는 중. AI 화면에서도 답변을 읽는 동안 상태 옆에 표시됩니다.
  - 애니메이션은 해당 상태일 때만 실행되고 끝나면 멈춥니다.
- 서버 접속 QR. 첫 부팅·Wi-Fi 연결 실패 시 SoftAP 설정, DHCP 또는 서버 고정 IPv4·게이트웨이·마스크·DNS 저장, mDNS.
- Flash·SD 읽기 전용 터치 파일 탐색기: 폴더 진입/상위 폴더/페이지 이동. 업로드·삭제·이름 변경은 기존 인증된 웹 관리자에서 처리합니다.
- ES8311 + I2S 스피커: 테스트음, 볼륨 0–150% (101–150은 소프트웨어 증폭), NVS 볼륨 저장, 재생 중 정지, 진행률. **SD `/media`의 PCM16 RIFF WAV**를 터치해 재생합니다. 모노/스테레오, 16/22.05/24/32/44.1/48kHz, 최대 8MiB. SD 파일 탐색기의 직접 재생은 WAV이며, 음악 앱에서 WAV/MP3/ADTS AAC를 재생합니다. 부팅 시 자동으로 소리를 내지 않습니다.
- 음악: SD `/media` 및 하위 폴더와 Flash 파일을 목록에서 선택해 재생합니다. 이전/다음 곡, 일시정지/재개, 정지, 목록 반복, 진행률과 음량 0–150을 제공합니다. 최대 64곡·파일당 256MiB, PSRAM 파일 버퍼와 8MiB 이하 사전 읽기를 사용합니다. [음악 앱 설명](docs/MUSIC.md).
- 파일명: 기존 한글에 일본 상용한자·KS 한자·히라가나·가타카나·추가 문장부호/통화/수학 기호 9,341개 글리프를 더했습니다. SD UTF-8 API, 문자 경계 잘림 처리와 폰트의 Flash 메타데이터 축소를 함께 적용합니다. [지원 범위와 검증](docs/UNICODE_FILENAMES.md).
- Radio: 방송국 6개·프리셋 7개 선택, 직접 HTTP(S) MP3/AAC 주소 입력. 512KiB 압축 수신 버퍼와 256KiB PCM 버퍼로 수신·디코딩·스피커 출력을 분리합니다. 방송 비트레이트 기준 약 6초의 시작 여유와 PCM 750ms를 확보하며, PCM이 부족할 때만 재버퍼링합니다. 화면에 수신 버퍼·PCM 잔량·끊김 횟수를 표시합니다. HTTPS 리다이렉트는 최대 3회 처리합니다. [라디오 설명](docs/RADIO.md).
- 유튜브: PC 중계 서버([`../tube-relay`](../tube-relay/README.md), `https://tube.audreaming.com`)를 거쳐 검색하고 재생합니다.
  - 검색: 검색어 입력(한글 자판·음성 입력, 키보드 확인 키로 바로 검색)과 주제 버튼(인기 음악·뉴스·게임·요리·여행·동물)을 제공합니다.
  - 목록: 한 페이지에 10개를 보여 줍니다. 각 항목에는 썸네일(160x90), 재생 시간 또는 라이브 표시, 채널, 조회수(만·억 단위)가 나옵니다. 이전/다음 페이지로 넘길 수 있습니다.
  - 채널: 재생 화면의 `채널 영상`으로 그 채널의 영상 목록을 열고, `검색 결과`로 다시 돌아옵니다.
  - 재생: 480x272 영상, 진행 막대(끌어서 이동), 10초 뒤로/앞으로, 일시정지(영상을 눌러도 됨), 목록 버튼, 음량을 제공합니다. 영상이 오기 전에는 썸네일을 크게 보여 줍니다.
  - 동작 방식: 영상은 P4 하드웨어 JPEG 디코더(`esp_driver_jpeg`)로 RGB565로 풀고, 소리 출력 시각에 맞춰 프레임을 고릅니다. 프레임 버퍼는 PSRAM에 5개(약 1.3MB)를 두며, 수신 링버퍼는 2MB, 썸네일은 이중 버퍼 576KB입니다.
  - 제한: 소리는 라디오처럼 기존 오디오 작업을 사용하므로 다른 소리를 정지한 뒤 시작합니다. 앱을 나가면 재생을 멈춥니다. 재생 중에는 HTTPS 임대를 계속 쓰므로 AI 요청과 클라우드 중계는 기다립니다.
  - 빌드 준비: PC에서 `node tube-relay/setup.mjs`가 만든 `main/include/m3_tube_private.h`(Git 제외)가 있어야 연결 주소와 토큰이 펌웨어에 들어갑니다. 없으면 앱에 설정 안내가 표시됩니다.
- Games: 2048과 숫자 카드 짝 맞추기. 2048은 보드 스와이프/방향 버튼, 한 번 되돌리기, 점수·현재 부팅 세션 최고 점수, 2048 도달/계속 진행/게임 종료를 지원합니다. 카드 게임은 8쌍, 시도 횟수, 일치하지 않는 카드 자동 뒤집기를 제공합니다. 네트워크와 SD 없이 동작합니다.
- Korean TTS: OpenAI `gpt-4o-mini-tts` / `marin`을 기본으로, Google Chirp 3 HD와 오프라인 eSpeak NG를 선택합니다. 클라우드 음성은 PSRAM에 최대 2MiB 버퍼링한 뒤 재생하며, AI 답변 읽기도 이 선택을 따릅니다. 클라우드 모드는 Wi-Fi와 해당 API 키가 필요합니다. 오프라인 모드는 한국어를 지원하는 eSpeak NG를 보드에서 실행합니다. 내장 음성 데이터 875,195 bytes와 한글 폰트(완성형 11,172자 및 호환 자모)를 사용하므로 오프라인 선택 시 서버·SD·API 키 없이 문장을 합성합니다. 한글 자판의 자모 조합, 영문 전환, 예제 문장, 속도 100–260, 음량/정지를 지원합니다. 최대 384 UTF-8 bytes, 기계음 성격의 포먼트 합성으로 자연스러운 신경망 음성은 아닙니다. 한글 입력은 텍스트 끝의 음절을 조합하며 중간 커서 위치에서의 고급 IME 편집은 지원하지 않습니다.

음악·Radio·TTS·WAV·유튜브·녹음은 하나의 오디오 작업에서 ES8311을 사용합니다. 음악 앱은 현재 오디오를 정지한 뒤 선택 곡을 시작합니다. 다른 앱에서 소리를 시작하려면 현재 재생을 Stop으로 먼저 정지하세요. 홈 복귀와 게임 진입은 재생을 중단하지 않습니다. 라디오 연결 제한은 3초, 재생 중 본문 읽기는 250ms이며 Stop은 두 생산 작업이 종료된 뒤 버퍼를 해제합니다. 연결 중 Stop 반영은 즉시가 아닐 수 있습니다. HTTPS 라디오는 종료까지 TLS 메모리 임대를 유지하므로 AI 요청과 cloud relay가 대기할 수 있습니다. 오디오 작업 스택은 eSpeak 호출을 위해 64KiB로 늘렸습니다. 실제 스택 여유와 OTA 4MiB 이내 바이너리 크기는 사용자 빌드와 보드에서 확인해야 하며 기존 파티션을 확장하거나 지우지 않았습니다.

시험용 Audio 앱과 440 Hz 테스트음은 제거하고 **녹음** 앱으로 교체했습니다. 16 kHz,
PCM16 모노 WAV를 SD 카드의 `/recordings` 폴더에 저장합니다. 폴더는 첫 녹음 시작 시
자동으로 만듭니다. `녹음 시작` → `정지 후 저장`으로 저장하고, `최근 녹음 재생` 또는
`녹음 폴더`에서 파일을 확인합니다. 재생 중에는 재생 버튼이 `정지`로 바뀝니다.
시간과 입력 레벨을 표시하며, 최대 30분(약 57.6 MB)의 오디오를 작은 PSRAM 버퍼로
연속 기록합니다. 앱을 나가도 녹음은 계속됩니다. 음악·라디오·TTS·음성입력과
동시에 마이크를 열지 않으며, 다른 오디오를 시작하려면 현재 작업을 정지하세요.

파일명은 `rec_날짜_시간_임의값.wav`이며 시간이 동기화되지 않았으면 부팅 이후
시간을 사용합니다. 기존 파일을 덮어쓰지 않습니다. 녹음 중에는 `.wav.part`로
저장하고 1초마다 WAV 헤더를 갱신합니다. 정지 시 파일 동기화·닫기·이름 변경이
완료되어야 저장 성공으로 표시합니다. SD 쓰기 실패나 전원 중단 시 `.part`가
남을 수 있으며, 자동으로 완성 파일로 처리하지 않습니다. 카드를 빼기 전에
저장 완료를 확인하세요. 이전 부팅의 녹음은 `녹음 폴더`에서 재생할 수 있습니다.

호스트 검사: `node tools/test_recorder.cjs`, `node tools/test_audio_output.cjs`.
실제 마이크 음질, SD 기록 지연, 저장 공간 부족, 녹음 중 전원 중단과 재부팅,
정지 후 재생 및 STT 전환은 사용자 빌드·업로드 후 실물에서 확인해야 합니다.

무음이 지속되면 WAV 재생 직후의 `M3_AUDIO` 로그를 확인하세요. 샘플레이트·MCLK·음량·앰프 GPIO11 상태, ES8311 레지스터 0x00/01/09/0D/12/31/32, 실패 코드가 출력됩니다. PCM 제출 성공은 소리가 물리적으로 나왔다는 증거가 아닙니다. 이 값으로 제어/I2S 문제와 출력단 문제를 구분할 수 있습니다.
- 웹 관리자, AI Assistant, Flash/SD 파일 관리, OTA는 P4 웹서버 기반으로 유지합니다. AI는 사용자 API 키·인터넷 설정이 필요합니다.
- M3 전용 반응형 기본 홈페이지: 흑백·시안·자홍 UI, CSS 3D 애니메이션, 오프라인 동작 및 reduced-motion 지원. [원본 HTML](web/index.html)은 `node tools/embed_home.cjs`로 C 소스에 포함합니다.

카메라·Bluetooth·USB Host·RS485 기능은 이번 펌웨어에서 활성화하지 않습니다. 스피커와 외부 카메라 연결 지원이라는 판매 설명이 카메라 센서의 기본 장착을 뜻하지는 않습니다.

## 기기 설정

설정 앱에 **화면 밝기 / 음량·무음 / 최대 FPS / 애니메이션 효과 / 웹서버 집중 모드**를
분류해 추가했습니다. 화면·소리·효과는 자동 저장하며, 집중 모드는 적용 후 재부팅합니다.
집중 모드에서는 디스플레이와 다른 앱 작업을 시작하지 않고 웹서버·Wi-Fi·SD·중계를
유지합니다. 복귀는 로컬 `/device-settings` 또는 BOOT를 누른 채 재시작으로 가능합니다.
[범위·저장·복귀·검증 절차](docs/DEVICE_SETTINGS.md)를 참고하세요.

## 첫 사용

게임 앱에는 **2048 / 짝 맞추기 / 길 건너기**가 있습니다. 길 건너기는 화면 탭으로
한 칸 전진하고, 스와이프나 방향 버튼으로 상하좌우 이동합니다. 차를 피하고
강에서는 통나무를 타세요. 앞으로 도달한 최대 거리가 점수이며, 제자리에 오래
머물면 화면이 따라와 게임이 끝납니다. 다른 앱이나 상단 작업 패널을 열면
일시정지되고, 돌아오면 이어집니다. 충돌·물에 빠짐·화면 밖 이탈 후 `새 게임`으로
다시 시작합니다. 최고 점수는 실행 중에 유지되며 재부팅하면 초기화됩니다.
별도 다운로드나 네트워크 없이 보드에서 그린 캐릭터·차량·통나무를 사용합니다.

길 건너기 호스트 검사: `node tools/test_crossing.cjs`. 게임 규칙과 UI 타이머의
중단 조건을 검사하며 C 컴파일·실기 검증을 대신하지 않습니다. PC 미리보기에는
`65_games_crossing`, `66_games_crossing_river`, `67_games_crossing_over` 시나리오를
추가했습니다. 사용자 빌드·업로드 후 터치 조작, 게임 종료와 재시작, 앱 전환·복귀,
한국어·영어 화면을 확인하세요.

1. 홈에서 Wi-Fi 앱을 열고 `네트워크 검색`(Search networks)으로 검색하거나 SSID를 직접 입력합니다.
2. 암호와 DHCP/고정 IP를 입력하고 `저장 후 연결`을 누릅니다. 화면 입력 대신 스마트폰을 `Mini-WebServer-M3-Setup`에 연결해 `http://192.168.4.1/setup`에서 설정해도 됩니다. 웹 설정 저장 방식은 재부팅 후 적용합니다.
3. 공유기에 접속한 뒤 터치 화면 Wi-Fi 앱의 IP/QR로 접속합니다. `/admin`의 초기 관리자 암호는 `00000000`이며 변경 후 사용하세요.
4. 저장된 Wi-Fi를 무시하고 설정 AP로 부팅하려면 **BOOT를 누른 상태에서 전원을 켜거나 리셋**합니다. 자격증명을 삭제하지 않습니다. C6 펌웨어가 호환되지 않으면 SoftAP도 생성되지 않습니다.
5. FAT32 SD 카드를 넣고 재부팅합니다. 웹 관리자로 `/media`에 PCM16 WAV를 업로드하고 음악 앱에서 SD 목록을 새로고침한 뒤 파일을 선택합니다. 스피커는 실제 장착/연결되어 있어야 합니다.

Wi-Fi 목록의 닫힌 자물쇠는 보안 네트워크, 열린 자물쇠는 공개 네트워크입니다. 공개 네트워크를 선택하면 암호 입력 없이 연결할 수 있습니다. 저장된 보안 Wi-Fi와 같은 SSID에 다시 연결할 때 비밀번호를 비워 두면 기존 비밀번호를 재사용합니다. 새 비밀번호를 입력하면 저장 값을 변경합니다. 저장된 비밀번호는 화면이나 상태 정보로 내보내지 않습니다.

부팅 시 저장된 Wi-Fi에 연결하지 못하면 설정 AP로 전환하고 60초 간격으로 다시 연결을 시도합니다. 자동 재시도는 NVS를 다시 쓰지 않습니다. 휴대폰이 설정 AP에 접속해 있으면 재시도를 미룹니다. BOOT로 설정 모드에 들어왔거나 화면에서 설정 핫스팟을 직접 선택한 경우에는 자동 전환하지 않습니다. 사용자 빌드·업로드 후 저장 → 재부팅 → 자동 연결, 공유기 지연 시작 후 복구, 공개 Wi-Fi 선택을 실물에서 확인하세요.

재생 중 파일 삭제와 충돌하지 않도록 SD 관리자 잠금을 유지합니다. 이때 SD 탐색/관리 요청은 busy가 될 수 있고 정지 또는 재생 완료 후 풀립니다. Flash 파일 제공은 별도로 계속됩니다. 터치 화면은 한국어/영어를 지원하며 웹 설정은 한국어입니다.

제품 주소는 `http://m3-v1-2-<MAC12자리>.local/`입니다. 클라우드 식별자 `m3-<MAC12자리>`는 별도입니다. 도메인 접속에는 M3용 서버 등록·도메인 경로 배포·보드 토큰 저장이 모두 필요합니다. `cloud-relay`에서 `node provision.mjs 실제MAC12자리 m3`로 등록 파일을 만들고, 서버 배포 후 `./Register-Board.ps1 -BoardUrl http://보드IP -Serial 실제MAC12자리 -Product m3`로 키를 저장하고 재부팅합니다. 시리얼은 로컬 관리자 `/api/system` 값을 사용하며, M1 등록을 재사용하지 않습니다. 자세한 절차와 상태별 점검은 [M3 도메인 연결](../cloud-relay/README.md#m3-도메인-연결)을 참고하세요. QR 주소 표시는 연결 완료를 뜻하지 않습니다.

## 배선 프로파일

| 기능 | 설정 |
| --- | --- |
| LCD ST7701S | MIPI-DSI, 480×800, RESET GPIO5, 백라이트 GPIO23 |
| DSI PHY 전원 | 내부 LDO3 / 2.5V |
| GT911 / ES8311 공용 I2C | SDA7 / SCL8 / 400kHz, GT911 0x5D 또는 0x14 자동 탐색 |
| 터치 | RESET 기본 GPIO3, INT 미사용·폴링. menuconfig의 M3 메뉴에서 reset/mirror 조정 |
| ES8311 I2S 입출력 | MCLK13 / BCLK12 / LRCK10 / DOUT9 / DIN48 / AMP GPIO11 |
| SDMMC slot0 | CLK43 / CMD44 / D0=39 / D1=40 / D2=41 / D3=42, 4-bit 20MHz |
| SD 전원 | LDO4, SD power controller 단일 소유. GPIO45는 이 프로파일에서 연결되지 않아 구동하지 않음 |
| C6 SDIO slot1 | CLK18 / CMD19 / D0=14 / D1=15 / D2=16 / D3=17 / RESET54, 4-bit 20MHz |
| BOOT | GPIO35, active low |

백라이트는 우선 GPIO on/off로 구동합니다. 화면 방향은 세로형으로 고정합니다. GT911 reset 배선이 다른 revision이면 `CONFIG_M3_TOUCH_RESET`를 확인하세요. Flash는 DIO 80MHz, PSRAM은 HEX 200MHz입니다. 사용자의 esptool 로그에서 **P4 v3.2**가 확인되어 v3.1 이상 전용 설정을 사용합니다. v1.x와 v3.x 펌웨어는 서로 호환되지 않습니다.

16MB 파티션에는 4MiB OTA 앱 슬롯 두 개와 7.8125MiB SPIFFS를 사용합니다. 파티션 라벨 `littlefs`는 기존 웹서버와 호환하기 위한 이름이며 실제 파일시스템은 SPIFFS입니다. SD 마운트 실패 시 카드를 자동 포맷하지 않습니다.

## 사용자가 실행하는 빌드·설치

이전에 v1.x 설정으로 빌드했다면 기본값 파일 변경만으로 기존 `sdkconfig`가 바뀌지는 않습니다. 현재 작업 폴더의 `sdkconfig`도 v3.1 이상으로 수정했습니다. 이 폴더에서 직접 빌드·설치할 때는 다음 명령으로 기존 부트로더를 포함해 다시 빌드하세요. COM8은 사용자의 로그에 표시된 P4 USB-Serial/JTAG 포트입니다.

```powershell
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py -p COM8 flash monitor
```

기존 v1.x 바이너리를 `--force`로 설치하지 마세요. 이 오류는 USB 슬롯 문제가 아니라 바이너리의 지원 칩 리비전 불일치입니다. 다른 사본에서 빌드한다면 `menuconfig`의 Chip revision에서 `Select ESP32-P4 revisions <3.0`을 끄고 최소 리비전을 v3.1로 선택하세요.

부팅 후 `_mipi_dsi_ll_set_phy_pllref_clock_source`에서 abort가 발생했다면 구형 DSI 클록 지정이 포함된 앱입니다. `m3_board.c`는 `phy_clk_src = 0`으로 ESP-IDF의 리비전별 기본값 선택을 사용합니다. v3.x에서는 XTAL을 선택하며, 구형 `MIPI_DSI_PHY_CLK_SRC_DEFAULT` 별칭의 PLL_F20M을 강제하지 않습니다. 수정 후 앱을 다시 빌드·설치하세요. 이 변경에는 전체 Flash 삭제가 필요하지 않습니다.

SoftAP·HTTP 시작 직후 `assert failed: netif_add ... (netif already added)`로 재부팅되는 경로에는 `m3_netif_guard.c`의 M3 전용 보호 처리를 적용합니다. 링크 시 `esp_netif_action_start`를 감싸고 Wi-Fi STA/AP START의 실제 lwIP 목록 등록 여부를 TCP/IP 작업 안에서 확인합니다. 등록된 인터페이스는 재등록하지 않으며, STOP으로 제거된 후에는 다시 시작할 수 있습니다. 연결 전 STA는 down 상태일 수 있어 단순 `is_up` 검사로 대체하지 않습니다. SDK나 관리 컴포넌트 원본은 수정하지 않습니다. 이 처리는 로그에서 확인된 중복 등록 중단을 방지하지만, 반복 이벤트의 C6 측 발생 원인이나 전체 무선 호환성을 실기기로 검증한 것은 아닙니다.

같은 로그에 나타난 SD 카드 `ESP_ERR_TIMEOUT`은 별도로 확인해야 합니다. SD 마운트 실패 시에도 웹 서버는 계속 시작합니다. `main_task: Returned from app_main()`은 초기화 작업을 별도 태스크로 넘긴 뒤 정상적으로 반환했다는 뜻입니다.

ESP-IDF **5.5.5** PowerShell 터미널에서 이 폴더를 열어 실행합니다. 첫 의존성 다운로드에는 인터넷이 필요합니다.

```powershell
.\m3.ps1 -Action Build
.\prepare_c6.ps1
```

`m3.ps1`은 한글·공백 경로 문제를 피하도록 영문 TEMP 경로에 소스를 복사하고, 빌드 결과를 `firmware-output`에 복사합니다. 에이전트는 이 스크립트를 실행하지 않았습니다.

의존성: esp_wifi_remote 1.6.5 / esp_hosted 2.13.0 / esp_lvgl_port 2.7.2 / LVGL 9.2.2 / GT911 1.1.3 / esp_codec_dev 1.2.0 / mDNS 1.13.1. C6는 **동일 esp_hosted 2.13.0의 공식 slave 예제에서 만든 이미지**를 사용하세요. 공장 C6 데모·AT·다른 Hosted 버전과의 호환을 가정하지 않습니다.

Hosted 1.4.1은 SDIO 리셋 극성 설정을 무시하고 RESET54를 항상 HIGH로 유지했습니다. 2.x는 설정을 그대로 따르므로 `sdkconfig.defaults`를 `ACTIVE_HIGH`로 바꿨고, 기존 `sdkconfig`는 `cmake/hosted_v2_migration.cmake`가 빌드 전에 같은 값으로 옮깁니다. 핀의 실제 동작은 이전과 같습니다. 1.4.1 전용이던 `hosted_idf55_compat.cmake`와 `hosted_dma_guard.cmake`는 더 이상 쓰지 않습니다. 2.13은 IDF 5.5 필드를 직접 지원하고, SDIO 수신 DMA가 부족하면 assert 대신 패킷을 버립니다.

### C6 펌웨어를 P4로 업데이트

C6는 UART 없이 P4를 통해 업데이트할 수 있습니다. 1.4.1 C6에서 처음 넘어갈 때도 같은 방법을 씁니다.

1. `.\m3.ps1 -Action Build`로 P4를 빌드하고 설치합니다.
2. `.\prepare_c6.ps1`이 출력하는 명령으로 C6 이미지를 빌드합니다(설치는 하지 않음).
3. C6 빌드 폴더의 `build\network_adapter.bin`을 SD 카드 루트에 `c6_update.bin`이라는 이름으로 복사하고 M3를 재부팅합니다.
4. P4가 Wi-Fi를 켜기 전에 이미지를 확인하고 C6에 기록합니다. 성공하면 파일 이름이 `c6_update.done`, 실패하면 `c6_update.failed`로 바뀌고, 성공 시 P4가 한 번 재시작합니다. C6 버전이 이미 같으면 기록하지 않습니다.

C6의 파티션 표는 처음 UART로 설치할 때 정해지고 OTA로는 바뀌지 않습니다. 그래서 C6 이미지는 기존과 같은 `m3_c6_partitions.csv`(앱 슬롯 1536 KiB 두 개)로 빌드하며, 이미지가 슬롯보다 크면 C6 빌드가 실패합니다. 기록 중 실패하면 이전 C6 펌웨어가 그대로 부팅됩니다. P4 경로가 동작하지 않을 때만 아래 UART 설치를 사용하세요.

파티션의 `littlefs,data,spiffs` 경고는 기존 저장소 라벨과 다른 파일시스템 subtype 이름이 같아서 발생합니다. 실제 subtype은 SPIFFS이며, 이번 RPC 컴파일 오류의 원인이 아닙니다. 저장된 웹 파일과 호환되는 라벨 및 파티션 배치는 유지합니다.

`prepare_c6.ps1`은 해당 버전의 slave 프로젝트를 별도의 TEMP 폴더에 준비하고 C6 빌드 명령, SD 업데이트 방법, UART 대체 설치 명령을 출력합니다. 직접 P4 프로젝트를 빌드한 경우 `-HostedComponentPath '.\managed_components\espressif__esp_hosted'`를 지정합니다. C6 설치 중 P4가 C6 RESET54를 구동하지 않도록 P4를 다운로드 모드로 정지해 두세요. C6 UART의 연결·BOOT/RESET은 JP1 라벨과 회로도로 확인하고 3.3V UART를 사용합니다. P4 USB와 C6 UART 포트를 구분하세요.

C6 설치 후 원래 폴더로 돌아와 P4를 설치합니다.

```powershell
$env:IDF_TARGET = 'esp32p4'
.\m3.ps1 -Action Flash -Port COM9
```

`COM9`는 예시입니다. 실제 P4 포트를 지정하세요. 이전 보드 펌웨어에서 옮겨올 때 전체 파티션 레이아웃과 기존 웹 파일 보존 여부를 확인하세요. 이미 `index.html`이 저장되어 있으면 새 기본 페이지가 덮어쓰지 않으므로 `web/index.html`을 관리자에서 업로드합니다.

## 검증

호스트에서 실행 가능한 검사는 아래와 같습니다.

`test_touch_apps.cjs`는 실제 C 소스의 제스처·키보드·Wi-Fi 작업 분기를 호스트 모의 객체로 실행합니다. 하단에서 시작한 스와이프의 거리/취소, 입력 문자와 IP 소수점, 저장 실패·연결 실패·SoftAP 복귀를 검사하고 설치된 LVGL 헤더에 사용 API가 있는지도 확인합니다. LCD 렌더링이나 C6 무선 연결 검증은 아닙니다.

`test_media_apps.cjs`는 실제 C의 2048 이동·병합·점수·종료·난수 생성, 한글 음절/겹받침/복합 모음 조합과 ICY 메타데이터 분리를 실행합니다. 음성 데이터 해시·형식, 한국어 엔진 선택과 오프라인 경로도 검사합니다. TTS 합성 품질이나 라이브 방송 재생을 검증하는 테스트는 아닙니다. `tools/generate_tts_assets.py`는 포함된 데이터 및 OFL 폰트로 생성 C 자산을 재생성하는 도구이며 빌드에 실행할 필요는 없습니다.

```powershell
Get-ChildItem .\tools\test_*.cjs | ForEach-Object { node $_.FullName }
node ..\tools\test_network_setup.cjs
```

WAV 파서는 플랫폼 독립 C입니다. Visual Studio x64 Native Tools 터미널 또는 GCC 환경에서 `main/m3_wav.c`와 `tools/test_wav.c`만 함께 컴파일하여 검사할 수 있습니다. ESP-IDF 펌웨어 컴파일이 아닙니다. 현재 네이티브 파서 34개 케이스 및 웹·설정·파티션 호스트 검사를 통과했습니다.

실기기에서는 LCD 색상·깜박임·GT911 좌표, SoftAP 설정·고정 IP·재연결, PSRAM 인식, SD 탐색·재생·업로드 충돌, 스피커 음량·정지, OTA 양쪽 슬롯, 장시간 Wi-Fi+화면+오디오 동시 동작을 확인해야 합니다. 호스트 검사는 이를 대체하지 않습니다.

`test_i18n.cjs`는 모든 문구의 한국어/영어 쌍, 두 언어의 서식 지정자(`%u`, `%s` 등) 일치, 글꼴에 없는 문자(가운뎃점 등) 사용 여부, 제목 글꼴의 한글 포함 여부, 상태 메시지 번역표가 실제 서비스 소스의 문자열과 일치하는지 검사합니다.

`test_tube.cjs`는 `m3_tube.c`의 실제 프레임 선택·버퍼 재사용 코드를 실행합니다. 소리 시각보다 늦지 않은 가장 새 프레임을 고르는지, 표시 중인 버퍼를 덮어쓰지 않는지, 한글 검색어를 올바르게 URL 인코딩하는지 확인합니다. 또 `tube-relay`와 패킷 번호·해상도·샘플레이트·출력 지연(6x240 프레임 = 60ms)이 일치하는지도 검사합니다. 실제 하드웨어 JPEG 디코딩과 Wi-Fi 처리량은 보드에서 확인해야 합니다.

`tools/ui_sim`은 실제 `m3_ui.c`를 PC에서 LVGL로 그려 PNG로 저장하는 화면 미리보기입니다. Visual Studio 2022 C 도구와 Pillow가 필요하며 `.\tools\ui_sim\build.ps1 -Lang 0`(한국어) 또는 `-Lang 1`(영어)로 실행하면 `%TEMP%\m3_ui_sim\frames`에 화면이 저장됩니다. 서비스는 모의 데이터이며 펌웨어 빌드, 실제 LCD 색상, 터치, 속도를 검증하지 않습니다.

터치 UI는 각 앱을 열고 하단에서 위로 밀어 홈 복귀, 키보드가 열린 상태의 홈 복귀, 긴 Wi-Fi 폼에서 입력란 노출을 확인하세요. 잘못된 암호로 연결 실패 후 SoftAP가 다시 열리는지와, 올바른 암호·DHCP/고정 IP로 연결 후 전원을 다시 켜도 설정이 유지되는지 확인하세요. 가상 키보드는 한글 자모 조합·영문·숫자·ASCII 특수문자를 지원합니다. `한`/`ABC` 키로 한글과 영문을, `?123` 키로 기호를 전환하고 위쪽 화살표는 대문자 또는 쌍자음, 키보드 아이콘은 키보드 닫기입니다. 설정에서 언어를 바꾼 뒤 각 앱 문구, 날짜 형식, 상태 메시지가 즉시 바뀌는지와 재부팅 후 선택이 유지되는지 확인하세요. 새 글꼴(Montserrat 28, 제목·시계 글꼴)과 번역표로 앱 바이너리가 약 100KB 늘어나므로 빌드 후 OTA 슬롯(4MiB) 여유를 확인하세요.

## 참조와 라이선스

- [한국어 eSpeak NG](https://github.com/espeak-ng/espeak-ng): GPL-3.0-or-later 엔진·음성 데이터. `components/espeak_ng/COPYING`과 Unicode 데이터 라이선스를 보존합니다. 이 엔진을 링크한 펌웨어의 배포에는 GPL 소스 제공 조건이 적용됩니다. [P4 포팅 기반](https://github.com/inlanger/esp32-p4-inflect-tts), commit `6d18e17bc6691ba66b7ce04b9a121f25c9c2ee2b`; [음성 데이터 출처](https://github.com/pschatzmann/arduino-espeak-ng), commit `93b2651d324874e1e00077af008d8ab83ab32e73`. `tts_data/manifest.json`에 파일별 해시를 기록합니다.
- [Nanum Gothic](https://github.com/google/fonts/tree/main/ofl/nanumgothic): SIL OFL 1.1, `third_party/nanum-gothic/OFL.txt` 보존. 생성 폰트 이름은 `m3_font_korean`입니다.
- [Espressif Audio Codec 2.3.0](https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.3.0): MP3/AAC 스트림 디코더, ESP-IDF component manager로 설치합니다.
- [SomaFM 직접 스트림 안내](https://somafm.com/groovesalad/directstreamlinks.html): 기본 MP3 프리셋 출처. 방송 URL·접속 가능 여부는 변경될 수 있습니다.
- [LVGL 9.2 가상 키보드 API](https://docs.lvgl.io/9.2/widgets/keyboard.html): 입력란 연결·사용자 키맵·완료/닫기 이벤트를 대조했습니다.
- [GUITION 제조사 보드 패키지](https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip)
- [제조사 예제 미러](https://github.com/DRubioG/JC-ESP32P4-M3-DEV), 확인한 commit `0d64f526d8f22ff21d973752b9bfab96878015a7`. 이 미러의 display BSP에는 800×1280 설정이 포함되어 있어 480×800 패널 설정으로 복사하지 않았습니다. 공용 I2C·오디오·SD 배선과 예제 API를 대조했습니다.
- [GUITION 480×800 BSP](https://github.com/ultramcu/guition-jc4880p4-bsp), 확인한 commit `324970bade0d1f4e52880fe8016580368bc1e06e`. MIT의 ST7701 초기화 테이블을 `main/include/m3_panel_init.h`에 사용하고 [라이선스](third_party/guition-bsp-LICENSE)를 보존했습니다. 해당 프로젝트의 실기기 결과는 이 펌웨어를 실기기 검증했다는 뜻이 아닙니다.
- [Espressif LVGL port](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.7.2), [GT911](https://components.espressif.com/components/espressif/esp_lcd_touch_gt911/versions/1.1.3), [esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.2.0)

음성 서비스 호스트 검사: `node tools/test_cloud_speech.cjs`. OpenAI/Gemini/Google 키는 Git 제외 로컬 헤더에 하드코딩되어 있으며, 실제 인증·과금·보드 음성 출력은 아직 검증하지 않았습니다. 자세한 설정은 [AI 및 음성 서비스 설명](docs/GEMINI_AGENT.md)을 참고하세요.

SDIO `pkt_rxbuff` assert 재부팅 수정: 일반 할당은 PSRAM 우선, 내부 RAM 예약 128KiB, I2S DMA 축소, 수신 패킷 DMA 할당 실패 보호 처리를 적용했습니다. 기존 sdkconfig도 재구성 시 자동 이관합니다. [원인·검증·재빌드 안내](docs/DMA_MEMORY.md).

PSRAM 추가 활용: 일반 할당 threshold 0, TLS 외부 힙, eSpeak BSS와 UI·영상 목록·대화 캐시의 외부 배치를 적용합니다. `python tools/audit_psram.py build/mini-webserver-m3.map --report results/psram-map.json`으로 사용자 재빌드 후 배치를 확인합니다. 기존 map 추정과 새 펌웨어 측정은 구분합니다.

## SD 앱 실행 실험

게임 → SD 앱에서 `/apps/*.lua`를 자동 탐색하고 실행합니다. 모래 테트리스와 수박 만들기는
`sd_apps/apps/sand_tetris.lua`, `sd_apps/apps/watermelon.lua`를 SD 카드 최상위의
`apps` 폴더로 복사하세요. `sd_apps/sd_games.zip`을 카드 최상위에 풀어도 됩니다.
설치·앱 API·제한·검증 범위는 [SD 앱 안내](docs/SD_APPS.md)를 참고하세요.

[스마트폰형 앱 사용·지원 포맷·녹음 복구](docs/PHONE_APPS.md)

## 텍스트 리더

홈의 텍스트 리더 또는 파일 앱에서 SD카드의 UTF-8 TXT·MD·LOG를 열 수 있습니다.
스와이프/가장자리 탭, 읽던 위치 복원, 글자 크기·간격·여백·화면 테마·배경 설정을
지원합니다. 사용법과 지원 범위는 [텍스트 리더 안내](docs/TEXT_READER.md)를 참고하세요.

유튜브 뮤직의 현재·다음 곡 PSRAM 캐시, SD 저장, 자동 다음 곡과 앱별 새로고침 아이콘: [설명](docs/YOUTUBE_MUSIC_CACHE.md).
