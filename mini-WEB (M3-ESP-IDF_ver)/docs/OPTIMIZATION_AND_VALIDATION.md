# M3 최적화 및 검증 절차

자동 적용되는 작업 지침은 프로젝트 루트의 [AGENTS.md](../AGENTS.md)다.
이 문서는 사용자가 실행할 명령과 증거의 범위를 설명한다.

## 메모리 배치

| 대상 | 배치 | 확인 방법 |
| --- | --- | --- |
| 일반 malloc, UI/LVGL 동적 객체 | PSRAM 우선, SDK fallback 허용 | 런타임 threshold 0 및 sdkconfig 확인 |
| TLS 힙 | PSRAM | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` |
| eSpeak 합성 BSS | PSRAM, 정적 0 초기화 | 컴포넌트 linker fragment 및 새 링크 맵 |
| UI 스냅샷·대화 기록·영상 목록 캐시 | PSRAM BSS | `M3_PSRAM_BSS`, 새 링크 맵 |
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
python tools/audit_psram.py build/mini-webserver-m3.map --report results/psram-map.json
```

`m3.ps1 -Action Build`의 map은 별도 TEMP staging 폴더에 생성된다. 그 map 경로를
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
node tools/test_voice_input.cjs
node tools/test_cloud_speech.cjs
node tools/test_tube.cjs
node tools/test_m3_integration.cjs
```

전부 매번 실행하기보다 수정 범위에 맞춰 선택한다. 메모리 배치 수정에는 앞의
세 검사와 영향을 받은 기능 검사가 필요하다. CMake fixture는 `LANGUAGES NONE`으로
구성하며 타깃 컴파일러를 호출하지 않는다.

## PC 화면 시뮬레이터

Visual Studio 2022 C 도구와 Pillow가 설치된 PC에서 사용자가 실행한다.

```powershell
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m3_ui_review_ko" -Lang 0
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m3_ui_review_en" -Lang 1
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
