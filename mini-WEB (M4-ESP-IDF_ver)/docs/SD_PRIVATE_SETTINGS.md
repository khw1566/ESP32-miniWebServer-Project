> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# M4 시제품 JSON 설정

M4는 SD카드의 평문 JSON 두 개를 설정 원본으로 사용합니다.
Wi-Fi 자격 증명은 NVS로 복사하지 않으며, 예전 NVS Wi-Fi가 SD 설정을 덮어쓰지
않습니다. API 키도 내장 헤더나 별도 웹 AI NVS 저장값으로 우회하지 않습니다.
관리자 비밀번호·세션 인증 등 일반 설정은 기존 NVS를 유지합니다.

## SD 파일 배치

SD카드 루트에 `.private` 폴더를 만들고 다음 두 파일을 UTF-8로 저장하세요.
Windows에서 파일 확장자가 `.json.txt`가 되지 않도록 확인합니다.

`.private/api_keys.json`:

```json
{
  "gemini": "YOUR_GEMINI_API_KEY",
  "google_tts": "YOUR_GOOGLE_TTS_API_KEY",
  "openai": "YOUR_OPENAI_API_KEY",
  "azure_speech": "YOUR_AZURE_SPEECH_KEY",
  "azure_region": "koreacentral"
}
```

사용하는 제공자 항목만 넣어도 됩니다. 빈 문자열은 미설정이며, `version`은
생략할 수 있습니다. 넣는 경우 숫자 1을 사용합니다. 키는 공백 없는 16~255자
문자열로 읽고 실제 제공자의 키 형식·접근 권한 검사는 요청 시 해당 제공자가 합니다.
키를 펌웨어 코드나 Git에 넣지 않습니다.

`.private/wifi.json`:

```json
{"ssid":"YOUR_WIFI_SSID","password":"YOUR_WIFI_PASSWORD"}
```

`static` 생략 또는 false는 DHCP입니다. 공개 Wi-Fi는 password를 빈 문자열로
둡니다. SSID는 UTF-8 최대 32바이트, 비밀번호는 8~63바이트입니다.
고정 IP는 아래처럼 모든 주소 필드를 지정합니다.

```json
{"ssid":"YOUR_WIFI_SSID","password":"YOUR_WIFI_PASSWORD","static":true,
 "ip":"192.168.1.50","gateway":"192.168.1.1","netmask":"255.255.255.0","dns":"1.1.1.1"}
```

예전 `version:1, networks:[...], last_ssid` 형식도 읽습니다. 화면 또는 웹 Wi-Fi
설정에서 저장하면 최신 선택 하나를 위 단일 프로필 JSON으로 기록합니다. 이전
파일은 `.bak`에 남깁니다. 저장 실패 시 성공이라고 표시하지 않고, 기존 실행 중
프로필을 유지합니다. 부팅 때 JSON을 읽은 뒤 RAM의 설정 스냅샷으로 자동 연결·
고정 IP·재시도·저장 비밀번호 재사용을 처리합니다. SD 파일을 PC에서 바꾼 경우
카드를 넣고 재부팅하세요. 설정 AP 동작과 명시적 설정 모드 선택은 유지합니다.

## 웹 관리자와 AI 어시스턴트

- 관리자에서 연결되는 AI 어시스턴트(`/ai`, `/ai_studio.html`)의 키 상태는
  `.private/api_keys.json`의 `gemini`를 확인합니다. 키 원문은 반환하지 않습니다.
- 웹의 **키 저장·교체**와 **저장 키 삭제**는 같은 JSON의 `gemini`만 갱신하며
  다른 제공자 키는 보존합니다. 별도의 AES/NVS API 키 저장을 사용하지 않습니다.
- 실제 `/api/ai/chat`의 Gemini HTTPS 요청도 요청마다 같은 JSON에서 키 사본을
  읽습니다. 따라서 웹에서 바꾼 키는 다음 요청부터 사용합니다.
- 기기의 Gemini 에이전트·Google STT/TTS·OpenAI·Azure 음성은 부팅 때 읽은 같은
  JSON의 제공자 항목을 사용합니다. 웹에서 키를 바꾼 뒤 이 기능들에도 반영하려면
  재부팅합니다. 진행 중 요청이 참조하는 키 메모리를 변경하지 않습니다.
- 관리자 로그인을 유지하고 `.private`를 공개 웹·파일 관리 경로에서 숨깁니다.
  관리자 기본값 복원은 SD의 키와 Wi-Fi JSON을 삭제하지 않습니다.

`Main WebFlie`에 예전 웹 AI 화면을 별도로 올려 사용한다면 새 내장 화면이
그 파일에 가려질 수 있습니다. 업데이트된 내장 화면으로 확인하거나 기존
`tools/export_main_web.py`로 최신 웹 패키지를 다시 내보낸 뒤 SD를 갱신합니다.
백엔드 API는 두 화면 모두 같은 JSON 키를 사용합니다.

## PC에서 복사

기존 Git 제외 로컬 키 원본 `sd_private/.private/api_keys.json`을 실제 SD에
복사하려면 저장소 루트에서 다음 명령을 실행합니다. E:/는 실제 카드입니다.
기존 SD 키 교체를 명시한 경우에만 `--replace-keys`를 사용합니다.

```powershell
python -X utf8 tools/copy_m4_sd_settings.py E:/ --replace-keys
```

Wi-Fi도 복사하려면 예제 `docs/examples/wifi.example.json`을 개인 폴더에 복사하여
실제 값으로 수정하고 `--wifi-source`로 지정합니다. 키 파일은 기존 설치용 원본을
사용하며 Wi-Fi 원문도 출력하지 않습니다. UTF-16 또는 BOM JSON 원본은 UTF-8로
변환하여 SD에 설치합니다. Wi-Fi 옵션이 없으면 SD Wi-Fi 파일을 건드리지 않습니다.

```powershell
python -X utf8 tools/copy_m4_sd_settings.py E:/ --replace-keys --wifi-source "개인폴더/wifi.json" --replace-wifi
```

예제 파일에는 실제 키·비밀번호가 없습니다. 로컬 설치 원본에 Wi-Fi JSON이 없어도
카드의 기존 Wi-Fi JSON과는 별개이며, 새 펌웨어의 화면·웹 Wi-Fi 설정 저장으로
카드에 생성할 수 있습니다.

## 진단과 검증

`SD_CONFIG`는 키 내용 대신 제공자별 로드 여부와 Wi-Fi 준비 여부만 기록합니다.
`API JSON loaded: gemini=1 ...`, `Wi-Fi JSON boot profile: ready`,
`Wi-Fi JSON committed`로 단계별 결과를 확인합니다. SD 마운트 실패는 앞선
`SD_MEDIA` 로그로 확인합니다. JSON을 못 읽으면 NVS·내장 키로 우회하지 않습니다.
`.tmp`에 flush/fsync 후 교체하고 `.bak` 복원을 지원합니다.

저장소 루트의 `node tools/test_m4_json_runtime.cjs`, `python -X utf8 tools/test_m4_sd_settings.py`,
`node tools/test_network_setup.cjs` 및 M4의 `test_sd_wifi_recovery.cjs`,
`test_touch_apps.cjs`, `test_ai_key.cjs`, `test_ai_studio.cjs`, `test_azure_speech.cjs`로
호스트 코드·모의 동작을 검사합니다. 빌드·플래시는 사용자가 실행하며 실제 카드,
Wi-Fi 접속, 제공자 인증 성공은 별도로 확인해야 합니다.
