# M3 Azure Speech 연결

2026-10-15에 Azure for Students 구독에 `audreaming-m3-speech`를 생성했다.
리소스 그룹은 `m3-speech-rg`, 지역은 `koreacentral`, 등급은 무료 F0다.
TTS와 STT는 같은 Speech 리소스 키를 사용한다. 유료 S0로 변경하지 않았다.

## 키 보관

- `sd_private/.private/api_keys.json`에 `azure_speech`와 `azure_region`을 추가했다. 기존 Gemini·Google·OpenAI 키는 보존했다.
- 키·지역은 SD의 `.private/api_keys.json`에서 읽습니다. 이전 로컬 헤더의 키 fallback은 사용하지 않습니다. JSON 형식과 복사는 [SD_PRIVATE_SETTINGS.md](SD_PRIVATE_SETTINGS.md)를 따릅니다.
- 기존 SD 파일에 Azure 필드가 없으면 새 펌웨어의 로컬 헤더 fallback을 사용한다. 따라서 이번 PC에서 빌드한 펌웨어는 Azure 키를 쓰기 위해 SD 파일을 반드시 교체할 필요는 없다.
- 다른 PC에는 private 헤더가 Git으로 전달되지 않는다. SD의 비공개 설정을 사용하거나 개인 백업에서 복구한다.
- 키를 채팅·로그·스크린샷·테스트 fixture에 출력하지 않았다. 복사에 사용한 클립보드는 비웠다.
- Azure 키는 지역과 함께 사용한다. 지역에는 소문자 영문·숫자만 허용하고, URL은 고정된 Microsoft Speech 도메인으로 구성한다. API 요청의 리디렉션을 허용하지 않는다.

형식 예시(실제 키 아님):

```json
{"version":1,"gemini":"EXISTING_GEMINI_KEY","google_tts":"EXISTING_GOOGLE_KEY","openai":"EXISTING_OPENAI_KEY","azure_speech":"YOUR_AZURE_SPEECH_KEY","azure_region":"koreacentral"}
```

SD 설정을 갱신할 때는 저장소 루트에서 다음을 실행한다. E:/는 실제 SD 경로로 바꾼다. 이 명령은 기존 키 파일을 교체하므로 명시적으로 --replace-keys를 사용한다. Wi-Fi 기록은 보존한다.

```powershell
python -X utf8 tools/copy_m3_sd_settings.py E:/ --replace-keys
```

## 펌웨어 동작

- 기존 제공자 번호를 유지했다. TTS 0=OpenAI, 1=Google, 2=오프라인, 3=Azure. STT 0=OpenAI, 1=Gemini 3.1, 2=Gemini 3.5, 3=Azure.
- 유효한 Azure 키/지역이 있으면 부팅 시 TTS·STT 기본값으로 Azure를 선택한다. 화면에서 기존 제공자로 다시 바꿀 수 있다. SD 로드 뒤 기본값이 바뀌는 경우도 화면의 선택 상태에 반영한다.
- TTS는 `ko-KR-SunHiNeural`, raw PCM16·16kHz·모노를 요청한다. HTTP에서 받은 조각부터 기존 오디오 소유 작업을 통해 stereo I2S로 재생한다. 전체 음성을 메모리에 보관하지 않으며 홀수 바이트 경계를 다음 수신으로 넘긴다.
- TTS payload/수신 버퍼는 PSRAM 약 5.8KiB이며 TLS·코덱의 별도 메모리는 이 계산에 포함하지 않는다. 출력 변환은 기존 내부 오디오 작업 스택의 2KiB 블록이다. 수신 상한은 2MiB다.
- STT는 기존 16kHz 모노 WAV 녹음(최대 8초)을 Azure 단문 REST API에 그대로 전송한다. Base64 변환은 하지 않는다. **녹음 중 실시간 인식 방식은 아니다.** 중간 결과 없이 최종 결과를 받으며, 384 UTF-8 바이트 제한과 사용자 결과 확인 절차를 유지한다.
- TLS 독점 보호, Wi-Fi 확인, 요청 시간 제한, 중지·취소, 오디오 코덱 단독 소유와 기존 제공자를 보존했다. 키·녹음 내용 없이 TTS 첫 PCM 시간과 STT 요청 시간을 로그에 남긴다.
- 네트워크가 끊기거나 수신이 늦으면 스트리밍 출력이 끊길 수 있다. 실제 Wi-Fi/I2S에서의 연속 재생과 취소는 추가 확인이 필요하다.
- 한국어·영어·일본어 제공자/상태 문자열과 미리보기 시나리오 82/83을 추가했다. 기존 PC mock에 잘못 복사된 런타임 전용 멤버도 해당 상태 타입에 맞게 정리했다.

## 검증 결과

### 실제 Azure API, PC에서 실행

사용자 마이크나 사적 음성 대신 짧은 합성 한국어 문장을 사용했다.

- TTS: PCM 146,800바이트, 16kHz·16bit·모노.
- TTS 요청 시작부터 첫 1,024바이트 수신: 0.265초.
- 합성 음성을 WAV로 감싸 STT 호출: 1.0초, RecognitionStatus=Success, 테스트 문장의 핵심 단어 일치.
- 이는 한 번의 PC 측정이다. ESP32의 TLS/I2S 시간, 녹음 시간, 실제 보드 STT 정확도·재생 품질을 증명하지 않는다.
- 결과: `results/azure-speech/azure-speech-verification.json`, `azure-korean-test.wav`.

재실행은 명시적인 --live 옵션이 필요하다:

```powershell
python -X utf8 tools/verify_azure_speech.py --live
```

### 호스트 검사

`test_azure_speech.cjs`는 실제 C 본문을 추출해 키/지역 검증, XML 이스케이프, PCM 부호·모노→스테레오·홀수/짝수 HTTP 경계·쓰기 실패, 취소/시간 만료, Azure 대기/재생 표시를 검사한다. 펌웨어 C 컴파일은 아니다.
기존 `test_voice_input.cjs`, `test_cloud_speech.cjs`, `test_audio_output.cjs`, `test_i18n.cjs`, `test_touch_apps.cjs`, `test_memory_budget.cjs`, `test_psram_policy.cjs`, `test_gemini_agent.cjs`도 해당 경로를 검사했다. SD 설정의 구형 파일 호환, 잘못된 Azure 필드/지역의 카드 보존 검사도 통과했다.

### 사용자가 실행할 빌드·실기 확인

저장소 지침에 따라 에이전트는 펌웨어 또는 PC 네이티브 렌더러를 컴파일하지 않았다. 기존 ELF/EXE를 새 결과로 사용하지 않았다. 현재 소스의 링크 성공과 한국어·영어·일본어 실제 레이아웃은 미확인이다.

ESP-IDF 5.5.5 환경에서 M3 폴더의 기존 절차를 사용한다:

```powershell
.\m3.ps1 -Action Build
.\m3.ps1 -Action Flash -Port COM8
.\m3.ps1 -Action Monitor -Port COM8
```

COM8은 실제 보드 포트로 바꾼다. 빌드 스크립트는 main을 staging 폴더에 복사하므로 새 C 모듈과 private 헤더도 포함된다.
화면에서 Azure 기본 선택, 한국어 읽기, 짧은/8초 녹음, 인식 결과 확인, 재생 중 취소, Wi-Fi 끊김/한도 초과 후 복구를 확인한다. PC UI를 렌더링하려면 사용자 측에서 `tools/ui_sim/build.ps1`로 새 EXE를 빌드한다.

## 공식 API 근거

- [Azure TTS REST](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/rest-text-to-speech)
- [Azure 단문 STT REST](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/rest-speech-to-text-short)
- [무료 F0 제공량](https://azure.microsoft.com/en-us/pricing/details/speech/)
