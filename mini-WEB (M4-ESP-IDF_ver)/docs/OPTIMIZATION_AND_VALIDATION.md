> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# M4 최적화 및 검증 절차

자동 적용되는 작업 지침은 프로젝트 루트의 [AGENTS.md](../AGENTS.md)다.
이 문서는 사용자가 실행할 명령과 증거의 범위를 설명한다.

## 메모리 배치

| 대상 | 배치 | 확인 방법 |
| --- | --- | --- |
| 일반 malloc, UI/LVGL 동적 객체 | PSRAM 우선, SDK fallback 허용 | 런타임 threshold 0 및 sdkconfig 확인 |
| TLS 힙 | PSRAM | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` |
| eSpeak 합성 BSS | PSRAM, 정적 0 초기화 | 컴포넌트 linker fragment 및 새 링크 맵 |
| UI 스냅샷·대화 기록·영상 목록 캐시 | PSRAM BSS | `M4_PSRAM_BSS`, 새 링크 맵 |
| 라디오 압축/PCM·녹음·음성·영상 데이터 | PSRAM | 각 heap_caps 할당과 실패 경로 |
| I2S/SDIO 내부 DMA·드라이버 필수 버퍼 | 내부 RAM | free/largest DMA 및 드라이버 요구사항 |
| RTOS TCB·lock·제어용 atomic | 내부 RAM | 선언·RTOS/드라이버 API |
| NVS/Flash/TLS 호출 작업 스택 | 내부 RAM | 호출 경로·실기 stack high-water mark |

일반 malloc의 PSRAM 우선은 PSRAM-only 보장이 아니다. PSRAM이 부족하면 SDK가
일반 내부 힙으로 fallback할 수 있다. 반드시 외부에 두어야 하는 대형 데이터는
명시적인 SPIRAM capability를 사용한다. 내부 예약 128KiB도 DMA만의 독점 풀이
아니며 내부 전용 스택·드라이버가 사용할 수 있다.

기존 sdkconfig는 CMake 재구성 전에 메모리 정책만 자동 이관한다. 다른 사용자
설정과 키를 초기화하지 않는다. BSS 외부 배치를 사용하면 PSRAM 부팅 초기화가 필수다.
아래 명령은 사용자가 ESP-IDF 터미널에서 실행한다. 에이전트는 기본 규칙상 펌웨어
컴파일/플래시를 실행하지 않는다.

```powershell
idf.py -p COM8 reconfigure build flash monitor
python tools/audit_psram.py build/mini-webserver-m4.map --report results/psram-map.json
```

`m4.ps1 -Action Build`의 map은 별도 TEMP staging 폴더에 생성된다. 그 map 경로를
감사 도구에 직접 전달한다. 다른 빌드의 map을 현재 펌웨어 증거로 섞지 않는다.
감사 결과의 경로·SHA256·수정 시각과 업로드 ELF/빌드 로그를 함께 보존한다.
도구는 map을 읽을 뿐 새 펌웨어를 빌드하거나 ELF와의 일치를 보증하지 않는다.

## 호스트 검사

다음은 Node 소스 정책/모의 transport 검사이며 실제 ESP32 디코더·DMA는 실행하지 않는다.

```powershell
node tools/test_psram_policy.cjs
node tools/test_dma_memory.cjs
node tools/test_memory_budget.cjs
node tools/test_radio_buffer.cjs
node tools/test_audio_output.cjs
node tools/test_recorder.cjs
node tools/test_voice_input.cjs
node tools/test_cloud_speech.cjs
node tools/test_tube.cjs
node tools/test_m4_integration.cjs
```

전부 매번 실행하기보다 수정 범위에 맞춰 선택한다. 메모리 배치 수정에는 앞의
세 검사와 영향을 받은 기능 검사가 필요하다. CMake fixture는 `LANGUAGES NONE`으로
구성하며 타깃 컴파일러를 호출하지 않는다.

## PC 화면 시뮬레이터

Visual Studio 2022 C 도구와 Pillow가 설치된 PC에서 사용자가 실행한다.

```powershell
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m4_ui_review_ko" -Lang 0
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m4_ui_review_en" -Lang 1
```

각 `frames` 폴더에 저장된 PNG를 직접 확인한다. 새 출력 폴더를 사용하면 이전
EXE/PNG와 혼동할 가능성이 줄어든다. 컴파일 종료 코드가 실패이면 EXE가 남아
있더라도 해당 결과를 폐기한다. 현재 소스의 렌더링을 확인하지 못했으면
“PC 시뮬레이터 검증 완료”라고 보고하지 않는다.

화면 검증 항목:

- 홈의 모든 페이지와 앱 아이콘, 시간·Wi-Fi 상태, 위로 홈 복귀와 하단 바 피드백.
- 상단 작업 패널의 작업 상태 및 메모리 원형 그래프. 실제 힙 측정이 아닌 모의 표시임을 명시.
- Wi-Fi 비밀번호·고정 IP 입력, 영문/한글/숫자 자판, 언어 전환 후 잘림과 스크롤.
- 음성입력의 녹음·인식 중·결과 적용·실패·취소, AI 전송·응답·오류 상태.
- 라디오 연결·버퍼링·재생·리버퍼링·종료, TTS 합성·재생·실패 표시.
- 파일·게임·설정·영상 앱의 진입·복귀·스크롤 및 사용자 입력 접근성.

코드/속성/메모리 배치만 바뀌고 UI 동작·레이아웃이 같으면 관련 호스트 검사와
링크 맵 확인을 우선하며 전체 화면 캡처를 반복할 필요는 없다. PC에는 C6 SDIO,
ES8311, MIPI 패널, PSRAM 캐시가 없으므로 무선·오디오 품질이나 메모리 안정성은
PC 이미지로 판정할 수 없다.

## 실기 기록

비교할 로그는 같은 보드·방송국·음량·앱 동작 조건에서 수집한다. 부팅 직전/후
internal free, largest DMA block, minimum heap와 작업 스택 여유를 기록한다.
Groove Salad MP3 128k, Wi-Fi 재연결, SoftAP 전환, STT 후 출력 재개, TTS,
영상 재생, 앱 전환을 각각 확인한다. 시간에 따라 내부 힙이나 최대 DMA 블록이
계속 감소하면 해제 누락·단편화를 점검한다. SDIO 할당 실패를 피하기 위해 앱의
DMA 버퍼를 무조건 확대하지 않는다.

보고 형식은 “변경 / 호스트 검사 / PC 화면 확인 / 사용자 펌웨어 빌드 /
실기 결과 / 남은 확인”으로 구분한다. 미실행·모의 결과·기존 map 추정치를
실기 성공으로 표현하지 않는다.

## MP4 추가 후 앱 크기 초과 (2026-10-15)

사용자 로그에서 MP4 포함 이미지가 `0x40ac00` (4,238,336바이트)로 생성됐으나
4 MiB OTA 앱 슬롯을 44,032바이트 초과해 크기 검사에서 실패했다. 앞부분의 빌드·플래시
성공 기록은 이전 이미지이며 MP4 포함 이미지의 설치 성공을 뜻하지 않는다.

관리자와 AI 스튜디오 페이지를 gzip 정적 데이터로 제공한다. 내용·인증·API는 유지하며
브라우저가 압축을 해제한다. 원본 C 문자열은 편집과 기존 검사에 남기고 서버에서는
참조하지 않아 링크 시 제거된다. 원본 데이터 179,309바이트가 53,007바이트가 되어
126,302바이트 줄었다. 이는 데이터 계산이며 최종 이미지 감소량은 링크·정렬에 따라 달라진다.
파티션과 NVS/SD 데이터는 변경하지 않는다. 실시간 디코더의 최적화 설정도 유지한다.

`tools/generate_web_gzip.py`는 Python 표준 라이브러리만 사용한다. CMake 구성 시 실행하며
원본 페이지와 SD 관리자 매크로가 바뀌면 자동으로 다시 생성한다. 바이너리 데이터는
문자열 길이 대신 명시적 길이를 사용해 2 KiB 단위로 전송한다.

`node tools/test_web_gzip.cjs`는 원본과 압축 해제 결과의 바이트 일치, CRC 오류,
분할 전송·실패, 인증 유지와 컴파일러 없는 CMake 재생성 fixture를 검사한다.
이 검사에는 CMake와 Python 경로가 필요하다. 기존 대시보드·AI 스튜디오 검사도 통과했다.
사용자는 다시 `idf.py build`를 실행해 앱 슬롯 크기 검사 통과를 확인한 뒤 설치하고,
Chrome에서 관리자·AI 로그인과 MP4 재생을 확인한다. 새 펌웨어 빌드·브라우저·실기 결과는 아직 없다.


## 유튜브 서버 상태·화질·SD 저장 (2026-10-15)

앱 진입 시 별도 서버 상태를 조회한다. 목록 작업이 없을 때도 작업 큐를 500ms마다
확인하므로 첫 검색 전 상태 확인이 가능하다. 인증 거절, 구버전 중계 서버,
PC 도구 누락과 터널 연결 실패를 구분한다. 요청은 15초 간격으로 제한하며
45초 넘게 갱신하지 못하면 화면에서 오래된 연결됨 표시를 제거한다.

화질은 저화질(기본, 8fps)·표준(12fps)·고화질(15fps)이다. 고정 480x272
RGB565 화면과 JPEG 디코더는 유지하고 PC가 세부 묘사와 JPEG 압축을 조절한다.
선택 화질은 이번 실행 동안 유지하며 NVS 설정 형식은 변경하지 않는다.
합성 3초 영상의 전송량은 270183/397251/508817바이트였다. 실제 YouTube 영상의
대역폭이나 보드에서의 버퍼링 개선 비율을 뜻하지 않는다.

SD 저장 작업은 내부 스택 10240바이트, PSRAM 전송 버퍼 8192바이트를 사용한다.
PC 준비 중에는 SD 잠금을 잡지 않고, 전송과 파일 commit 동안에만 잠금을 유지한다.
HTTP 헤더의 길이와 실제 전송량, 여유 공간, flush/fsync/close/rename 결과를 확인한다.
취소와 오류는 해당 작업의 부분 파일만 제거한다. SD에는 H.264 Baseline과 AAC-LC
MP4를 저장하고 기존 파일 앱·갤러리로 재생한다. PC 전용 변환 작업은 단일 실행이며
30분 이하 영상·파일 512MiB·캐시 12개와 약 1GiB 상한을 둔다.

호스트 검사:
- `node tools/test_tube.cjs`
- `node tools/test_tube_status.cjs`
- `node tools/test_tube_download.cjs`
- `node tools/test_preferences.cjs` 및 `node tools/test_device_settings.cjs`
- `node tools/test_file_actions.cjs`
- `node tools/test_touch_apps.cjs`, `node tools/test_i18n.cjs`, `node tools/test_japanese.cjs`
- `tube-relay`에서 `npm test`, FFmpeg/FFprobe가 있는 환경에서 `npm run test:media`

위 검사는 펌웨어 컴파일이나 SD카드 실기 검증이 아니다. PC 시뮬레이터에는
화질·저장 진행·서버 오프라인 시나리오를 추가했지만 네이티브 컴파일과
새 한국어·영어 화면 렌더링은 실행하지 않았다. 프로젝트 규칙상 사용자 빌드가 필요하다.
M4 폴더에서 `.\m4.ps1 -Action Build`로 빌드한 뒤 사용 중인 COM 포트로 업로드한다.

실기에서는 PC 종료/터널 단절/토큰 불일치와 복구 표시, 세 화질 재연결, SD 용량 부족,
준비·전송 중 취소, 완료 후 재부팅, Wi-Fi와 PC를 끈 상태의 저장 MP4 음성·영상 재생,
SD 파일 관리와 녹음·클라우드 업로드 간의 잠금, 설정 재시작 거절을 확인한다.
최소 내부 힙·최대 DMA 블록·각 작업의 스택 여유도 함께 기록한다.
