# M3 SD 설정 보존

SD카드 루트의 `.private/api_keys.json`을 부팅 시 읽습니다. Gemini 에이전트·음성인식,
Google TTS, OpenAI 음성인식·음성합성에서 사용합니다. 키 파일이 없으면 기존
Git 제외 `m3_gemini_private.h` 설정으로 동작합니다. 키 파일 변경 후 재부팅하세요.

현재 PC용 키 원본은 `sd_private/.private/api_keys.json`입니다. `.private` 폴더는
모든 경로에서 Git 제외 대상이며, 실제 키를 예제·소스·커밋에 넣지 않습니다.
Git pull/push는 이 파일이나 SD카드 설정을 변경하지 않습니다. 다른 PC에 clone하면
개인 키 파일은 내려오지 않으므로 SD카드나 개인 백업에서 가져와야 합니다.

빌드·업로드는 프로젝트 규칙에 따라 사용자가 실행합니다. 새 펌웨어를 설치한 뒤,
PC에 SD카드를 연결하고 저장소 루트에서 실행하세요. `E:/`는 실제 SD 드라이브로 바꿉니다.

```powershell
python tools/copy_m3_sd_settings.py E:/
```

기존 SD 키를 교체할 때만 `--replace-keys`를 추가합니다. 이 도구는 Wi-Fi 기록을
덮어쓰거나 삭제하지 않습니다. 카드에 `.private/api_keys.json`을 직접 복사해도 됩니다.

파일 형식 예시(실제 키는 이 문서에 넣지 않음):

```json
{"version":1,"google_tts":"YOUR_GOOGLE_TTS_KEY","gemini":"YOUR_GEMINI_KEY","openai":"YOUR_OPENAI_KEY"}
```

## Wi-Fi 기록

기기 화면 또는 웹 설정에서 유효한 Wi-Fi 설정을 저장하면 기존 내부 NVS 저장에
추가하여 SD의 `.private/wifi.json`에 SSID·비밀번호·DHCP/고정 IP 정보를 기록합니다.
연결 시도에 실패해도 입력한 유효한 설정은 기록됩니다. 같은 SSID는 최신 설정으로
갱신하며 다른 SSID 기록은 유지합니다. NVS가 비어 있으면 마지막 저장 SSID의 설정을
SD에서 NVS로 복원한 뒤 기존 자동 연결·실패 시 설정 AP·재시도 절차를 사용합니다.
기존 NVS 설정이 있으면 그것을 우선하고 SD에 백업합니다. 다른 SSID의 기록도
보관하지만 부팅 시 주변의 모든 저장 네트워크를 순회하는 기능은 아닙니다.

기존 기록은 삭제하지 않고 최대 64 KiB까지 보존합니다. SD가 없거나 업로드 중 잠김,
손상·용량 초과·쓰기 실패가 발생하면 NVS 설정은 유지하고 키 값 없이 `SD_CONFIG`
경고를 기록합니다. 실패한 SD 백업은 카드를 확인한 뒤 다시 설정을 저장하세요.
`.tmp`에 쓰고 flush/fsync 후 교체하며 이전 파일은 `.bak`에 남깁니다.
FAT 전원 차단의 모든 상황에 대한 복구를 보장하지 않으므로 중요한 카드는 별도 백업하세요.

이 폴더는 기기 파일 목록·이름 변경·삭제·미디어 업로드 경로에서 접근하지 않도록
분리합니다. SD의 파일은 평문이므로 카드를 가진 사람은 읽을 수 있습니다.
웹 AI 페이지의 기존 암호화 키 저장은 별도 기능이며 이 SD 키로 자동 덮어쓰지 않습니다.
음성인식 화면의 임시 키 입력은 기존 세션 동작을 유지하며 재부팅하면 SD 키를 사용합니다.

## 확인

호스트 검사: `python tools/test_m3_sd_settings.py`, `node tools/test_network_setup.cjs`.
펌웨어 빌드 후 SD 키 로드, Wi-Fi 두 개 저장, 재부팅, NVS 초기화 후 마지막 설정 복원,
카드 없음/쓰기 실패, 실제 제공자 인증을 보드에서 확인해야 합니다.

## Azure Speech

Azure TTS·STT는 `azure_speech` 공용 키와 `azure_region`을 사용합니다. 두 필드는 선택 사항이며 기존 키 파일 형식과 호환됩니다. 키가 없으면 Git 제외 `m3_azure_private.h`를 사용하고, 지역 기본값은 `koreacentral`입니다. 키가 준비되면 부팅 기본 제공자로 Azure를 선택하며 기존 제공자도 유지합니다. [연결 및 검증 절차](AZURE_SPEECH.md)를 참고하세요.
