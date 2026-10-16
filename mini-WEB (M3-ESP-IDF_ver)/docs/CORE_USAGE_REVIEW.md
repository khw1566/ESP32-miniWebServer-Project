# M3 HP/LP 코어 사용 및 최적화 검토

검토일: 2026-10-15. 현재 작업 폴더의 소스와 sdkconfig 기준이다. 보드에 설치된 바이너리의 상태나 실측 CPU 부하를 뜻하지 않는다. 이번 작업은 감사 도구와 검토 문서만 추가했으며 펌웨어 동작·설정·핀·사용자 변경 사항을 수정하지 않았다.

## 결론

- HP0와 HP1을 모두 사용한다. HP0에는 주로 네트워크·서비스, HP1에는 LVGL·오디오를 배치했다.
- LP 코어용 사용자 프로그램은 사용하지 않는다. `CONFIG_ULP_COPROC_ENABLED`가 꺼져 있고 프로젝트 소스에 LP 프로그램 임베드·로드·실행 경로가 없다.
- `CONFIG_SOC_LP_CORE_SUPPORTED=y`는 칩의 지원 능력이며 LP 프로그램 활성화 증거가 아니다.
- 코어 0과 1은 모두 HP 코어다. LP 코어는 `xTaskCreatePinnedToCore(..., 2)`로 사용하는 세 번째 FreeRTOS 코어가 아니다.
- TTS/STT 지연 개선은 LP 이관보다 녹음·전송·수신·재생의 스트리밍화가 우선이다. 현재 CPU 병목은 실측하지 않았으므로 특정 코어가 포화됐다고 단정할 수 없다.

## 환경과 현재 설정

ESP-IDF preflight: 타깃 esp32p4, 설치 SDK 5.5.5, LVGL 9.2.2, CMake 프로젝트 일치, m3_runtime.c의 기존 compile database 포함을 확인했다. 이는 메타데이터 검사이며 컴파일 검증이 아니다. Context7은 5.5.3 문서를 참조했고, 핵심 API와 드라이버 제약은 설치된 5.5.5 소스로 재확인했다.

| 항목 | 현재 값 | 근거 |
| --- | --- | --- |
| HP 코어 수 | 2 | sdkconfig:1830 |
| HP 주파수 | 400 MHz | sdkconfig:1559-1560 |
| RTOS tick | 1000 Hz | sdkconfig:1768 |
| 실행시간 통계 | 켜짐, esp_timer 시간원, U32 | sdkconfig:1793-1813 |
| ULP 사용자 프로그램 | 꺼짐 | sdkconfig:2462 |
| 전력 관리 PM | 꺼짐 | sdkconfig:1497 |
| lwIP | 우선순위 18, 코어 고정 없음 | sdkconfig:1920,2024-2027 |
| H.264 병렬 디코더 옵션 | 꺼짐 | sdkconfig:2626 |

`main/include/m3_cores.h`의 정책은 NET=0, MEDIA=1이다. 그러나 실제 모든 네트워크 처리가 HP0로 한정되는 것은 아니다. 오디오 작업에서 수행하는 클라우드 TTS/STT HTTP/TLS 호출은 HP1에서 실행되며, lwIP와 ESP-Hosted 작업은 코어 고정이 없다.

## 앱 태스크 생성 지점

IDF 태스크 스택 단위는 바이트다. 다음은 생성 지점 목록이며 모든 작업이 항상 동시에 존재한다는 의미가 아니다. 라디오·영상·SD 저장·재시작 등은 필요할 때 생성된다. 동작 중이 아닌 태스크는 큐·버퍼·I/O를 기다릴 수 있어 할당된 스택 크기나 태스크 수를 CPU 부하로 환산할 수 없다.

| 작업 | HP 코어 | 우선순위 | 요청 스택 | 소스 |
| --- | --- | --- | --- | --- |
| `cloud_relay` | 0 | 3 | 10 KiB | `main/cloud_relay.c:285` |
| `dns_task` | 0 | 5 | 4 KiB | `main/dns_server.c:124` |
| `m3_agent` | 0 | 3 | 24 KiB | `main/m3_agent.c:327` |
| `m3_audio` | 1 | 4 | 64 KiB | `main/m3_audio.c:411` |
| `m3_gallery` | 0 | 2 | 12 KiB | `main/m3_gallery.c:197` |
| `m3_mp4_video` | 0 | 2 | 16 KiB | `main/m3_mp4.c:184` |
| `mode_restart` | 고정 없음 | 5 | 2 KiB | `main/m3_preferences.c:131` |
| `m3_radio_rx` | 0 | 5 | 12 KiB | `main/m3_radio.c:290` |
| `m3_radio_dec` | 1 | 4 | 24 KiB | `main/m3_radio.c:300` |
| `m3_services` | 0 | 3 | 12 KiB | `main/m3_runtime.c:323` |
| `sd_lua` | 고정 없음 | 3 | 24 KiB | `main/m3_sd_apps.c:214` |
| `m3_tube_rx` | 0 | 5 | 8 KiB | `main/m3_tube.c:526` |
| `m3_tube` | 0 | 3 | 10 KiB | `main/m3_tube.c:633` |
| `m3_tube_save` | 0 | 3 | 10 KiB | `main/m3_tube_download.inc:163` |
| `ai_https` | 고정 없음 | 4 | 16 KiB | `main/web_ai_proxy.inc:168` |
| `wifi_restart` | 고정 없음 | 5 | 2 KiB | `main/web_network_setup.inc:96` |
| `ota_reboot` | 고정 없음 | 5 | 2 KiB | `main/web_server.c:952` |
| `sd_index` | 1 | 1 | 4 KiB | `main/web_server.c:1543` |

추가 라이브러리·시스템 작업:

| 작업 | 배치 | 우선순위 / 스택 | 근거 |
| --- | --- | --- | --- |
| taskLVGL | HP1 고정 | 4 / 16 KiB | main/m3_board.c:120-125, managed_components/espressif__esp_lvgl_port/include/esp_lvgl_port.h:65 |
| HTTP 서버 | HP0 고정 | 5 / 8 KiB | main/web_server.c:1362-1365, 설치 SDK esp_http_server.h의 HTTPD_DEFAULT_CONFIG |
| C6 SDIO / RPC | 고정 없음 | 기본 23, SDIO 기본 스택 3 KiB | ESP-Hosted hosted_thread_create의 xTaskCreate, DFLT_TASK_PRIO/RPC_TASK_PRIO |
| lwIP tcpip | 고정 없음 | 18 / 3 KiB | sdkconfig |
| esp_timer | HP0 고정 | SDK 시스템 작업 | sdkconfig:1649-1651 |
| mDNS | HP0 고정 | 1 / 4 KiB | sdkconfig:2895-2901 |
| app_main | HP0 고정 | 초기화 후 반환 | main/main.c 및 sdkconfig:1597 |

C6는 Wi-Fi 보조 칩이며 P4의 LP 코어가 아니다. 이 표는 P4 호스트 작업만 다룬다. C6 바이너리의 내부 스케줄링은 별도 분석 대상이다.

## 확인된 처리 구조와 개선 우선순위

### 1. 클라우드 TTS/STT 스트리밍화: 가장 직접적인 지연 개선 후보

`m3_audio` 하나가 출력·마이크·로컬 TTS·클라우드 TTS/STT·음악·영상 오디오를 직렬 소유한다. 이 방식은 코덱 충돌과 취소/해제를 단순하게 만들지만 전체 네트워크 응답을 기다리는 동안 해당 작업의 다음 처리는 진행되지 않는다.

- STT는 16 kHz 모노를 최대 8초 녹음하고, 사용자의 종료 또는 상한 도달 뒤 전송한다. 무음 자동 종료는 없다. RMS/dbFS 미터는 존재하지만 발화 종료 검출기는 아니다 (`main/m3_voice.c:22,146-183`).
- Google/Gemini 경로는 녹음 WAV를 Base64 JSON으로 보내므로 원본 대비 약 4/3 전송량과 인코딩/JSON 복사 비용이 추가된다.
- 클라우드 TTS의 공통 request는 HTTP 본문 전체를 받은 뒤 반환한다. Google은 Base64/WAV 디코딩 이후, OpenAI PCM도 전체 수신 이후 출력 코덱을 연다 (`main/m3_google_audio.c:77,139-155,237-242`).
- TLS 독점권 획득은 최대 5초 대기하며 다른 작업의 독점권이 이미 있으면 바로 실패한다 (`main/https_memory.c`, m3_google_audio.c:35, m3_voice.c:183). 이 보호를 단순 제거하면 내부 힙/DMA 부족이 재발할 수 있다.

권장: 오디오 작업의 코덱 단독 소유권은 유지하고, HP0의 전송 생산자와 HP1의 재생/녹음 소비자를 작은 제어 큐와 제한된 PSRAM 링으로 연결한다. STT는 녹음 중 업로드, TTS는 첫 PCM 조각부터 재생한다. 취소 시 네트워크 작업 종료 확인 뒤 버퍼를 해제하고, 재생 역압·홀수 PCM 바이트·오류 응답을 처리한다. 서버가 실제 스트리밍을 지원해야 한다. 먼저 발화 종료 검출을 추가하면 최대 8초 또는 수동 종료 대기 구간도 줄일 수 있다.

속도 향상 수치는 아직 없다. 먼저 TLS 대기, 연결/헤더, 첫 오디오, 전체 수신, 첫 I2S write를 별도로 측정해야 한다. API 추론/네트워크 대기는 HP 코어 배치만으로 없어지지 않는다.

### 2. HP1의 LVGL·오디오·라디오 디코더 경쟁: 측정 후 조정

LVGL, m3_audio, m3_radio_dec는 모두 HP1 우선순위 4다. 같은 코어에서 번갈아 실행되는 구조이며 반드시 문제라는 뜻은 아니다. 라디오는 HP0 수신 → PSRAM 압축 링 → HP1 디코드 → PCM 링 → HP1 출력으로 이미 분리되어 있다. 기존 압축 512 KiB·PCM 256 KiB와 취소 join을 보존한다.

확인할 것: UI를 빠르게 조작하는 동안 PCM 잔량/underrun, UI 갱신 소요, I2S write 지연, 코어별 부하. 압축 링은 충분한데 PCM만 마르면 디코드/스케줄링, 둘 다 마르면 네트워크/수신을 우선 의심한다. 실제 로그와 비교해야 확정할 수 있다.

권장 후보: UI의 변경 없는 재계산·레이아웃 갱신을 줄이고, 측정상 필요하면 단순 출력 소비자만 한 단계 높은 우선순위로 분리한다. 현재 오디오 작업 전체의 우선순위를 높이면 동기 eSpeak 합성까지 함께 높아지므로 그대로 적용하지 않는다. `taskYIELD()`는 낮은 우선순위 작업의 실행을 보장하지 않는다.

### 3. 네트워크 코어 고정: 실험 후보, 일괄 적용하지 않음

m3_cores.h 주석과 달리 lwIP와 ESP-Hosted의 높은 우선순위 작업은 HP1도 선점할 수 있다. lwIP를 HP0에 고정하는 A/B 비교는 가능하지만 HP0에 서비스·수신·H.264 디코드가 이미 모여 있어 처리량 저하 가능성도 있다. 먼저 현 상태를 측정한 뒤 한 항목씩 바꾼다. 관리 컴포넌트 전체를 직접 패치하거나 네트워크 태스크 우선순위를 임의로 낮추지 않는다.

### 4. 로컬 TTS와 상주 스택

eSpeak는 매 발화마다 초기화·음성 로드·종료한다 (`main/m3_tts.c:53-73`). 초기화 재사용은 검토할 수 있지만 지속되는 엔진 메모리, 실패 복구, 앱 전환, 취소, 음성 데이터 변경을 함께 관리해야 한다.

m3_audio는 내부 스택 64 KiB를 상주시키고 LVGL은 16 KiB다. 스택은 예전에 LVGL overflow가 있었고 NVS/Flash 경로도 있으므로 추측으로 축소하거나 PSRAM으로 옮기지 않는다. 최악의 eSpeak·SD·TLS·오류/취소 경로의 stack high-water mark를 먼저 수집한다. 필요하면 eSpeak만 필요 시 생성하는 별도 합성 작업으로 분리하되 총 피크 메모리와 생성 비용까지 비교한다.

### 5. 영상 디코딩과 색 변환

YouTube MJPEG는 이미 JPEG 하드웨어 디코더를 사용하고 화면은 DMA2D 및 LVGL direct mode를 사용한다. 반면 SD MP4는 HP0에서 tinyH264 소프트웨어 디코딩과 I420→RGB565 크기 변환을 수행한다 (`main/m3_mp4.c:168,184`, `main/m3_mp4_pixels.c:68`).

P4의 H.264 하드웨어 인코더를 H.264 디코더로 대체할 수는 없다. 설치된 esp_h264의 decoder는 SW이고 dual-task 옵션은 현재 꺼져 있다. dual-task는 HP1의 UI·오디오 부하 및 추가 내부 스택을 확인한 뒤 별도 실험한다. 색 변환은 소프트웨어 루프 비용을 먼저 측정하고, PPA가 요구하는 실제 입력 형식·정렬·메모리 일관성 조건을 만족하는지 확인한 뒤 오프로딩을 검토한다. 무조건 PPA로 바꾸면 빨라진다고 주장하지 않는다.

### 6. 폴링과 서비스 응답 지연

- m3_audio는 유휴 때 요청 큐를 20ms마다 확인한다. 새 시스템 효과음 큐도 확인하므로 단순 portMAX_DELAY 변경은 효과음 반응을 깨뜨릴 수 있다. 두 입력을 하나의 큐/알림으로 깨우는 구조가 후보이다.
- m3_services는 browse_queue에서 최대 1초 대기 후 Wi-Fi/파일 변경/설정/효과음 poll을 처리한다. 별도 큐에 들어간 요청을 대기로부터 깨우는 통합 알림이 서비스 반응 개선 후보다.
- LVGL 앱 타이머는 30/33/40/50/250ms 등으로 존재하고 LVGL tick은 기본 5ms다. 비활성 화면의 타이머를 일시정지하는 것이 후보지만 기존 callback 조기 반환과 활성 전환을 먼저 확인한다. port.task_max_sleep_ms=20을 모든 루프의 20ms 상한으로 해석하면 안 된다. 설치된 포트는 LV_NO_TIMER_READY일 때 이 값을 사용한다.

### 7. LP 코어: 대기 전력 개선용 후속 과제

LP 사용자 프로그램은 별도 바이너리·LP SRAM·공유 데이터 동기화가 필요하다. LP에서 현재 LVGL·TLS·SD/오디오 작업을 그대로 실행할 수 없다. 가벼운 GPIO/센서 관찰과 조건부 HP 깨우기가 적합하다.

현재 배선의 LP 마이크 이관에는 구체적 제약이 있다:

- 마이크는 HP I2S0 master, ES8311, BCLK=GPIO12, WS=GPIO10, DIN=GPIO48, MCLK=GPIO13이다 (`main/include/m3_board.h`, `main/m3_audio.c:187-200`).
- IDF 5.5.5의 P4 LP I2S는 slave RX, DMA 없음이며 LP/RTC GPIO를 사용한다 (`components/esp_driver_i2s/lp_i2s_std.c`).
- P4 RTC IO map은 GPIO0-15를 지원하고 GPIO48은 -1이다 (`components/soc/esp32p4/rtc_io_periph.c`). 따라서 현재 DIN 배선을 그대로 LP I2S 입력으로 직접 사용하는 방식은 지원되지 않는다.
- HP I2S를 자게 하려면 외부 코덱의 BCLK/WS/MCLK 공급도 별도로 해결해야 한다. 현 master 경로를 유지한 채 LP만 추가해도 HP 전체 수면을 달성한 것이 아니다.

저전력 상시 청취가 필요하면 LP 가능한 핀 배선·외부 클록/마이크·VAD를 별도 설계한다. 깨어 있는 HP가 PCM을 복사해 LP로 넘겨 RMS만 계산하는 방식은 복사·동기화 비용이 있어 현재 목적의 우선순위가 낮다. LP I2S/VAD 하드웨어의 존재와 LP CPU 사용자 프로그램 활성화는 서로 다른 개념이다.

## 보드 비교 절차

현재 이미 약 30초 간격의 `CPU load: core0 ..., core1 ...` 로그가 있다 (`main/m3_runtime.c:49`). 서비스 작업의 실행 지연에 따라 간격이 늘 수 있고 짧은 순간의 음성/UI 지연은 평균에 가려진다. 이번 폴더에서는 해당 수치가 담긴 실측 로그를 확보하지 못했다.

사용자 빌드/실기에서 동일한 Wi-Fi와 콘텐츠로 다음을 비교한다:

1. 홈 유휴 60초, TTS 짧은/긴 한국어, STT 1초/8초 및 조기 종료, 오류·취소.
2. 라디오 단독 및 UI 조작 동시 실행: PCM/압축 잔량, underrun, 수신 B/s.
3. YouTube와 SD MP4: 프레임 누락·A/V 지연·색 변환 시간·화면 반응.
4. 태스크별 CPU 실행시간, core affinity, 내부 스택 high-water mark, free/largest DMA와 최소 내부 힙.
5. 최소 10분 재생 후 stop/start 반복: 누수, 취소 join, I2S/마이크 재개.

CPU 표는 전체 태스크를 한 번에 snapshot한 뒤 출력하고, 관측 자체의 긴 scheduler/IRQ 정지를 피한다. 실측 전에는 배치를 일괄 바꾸지 않는다. 제안한 구조 변경을 구현할 때 관련 test_voice_input.cjs, test_audio_output.cjs, test_radio_buffer.cjs, test_tube.cjs, test_mp4.cjs 및 메모리 검사를 선택한다. 이러한 호스트 검사는 펌웨어 컴파일·실기 검증을 대신하지 않는다.

## 재실행 가능한 소스 감사

```powershell
python -X utf8 tools/audit_core_usage.py --output results/core-usage-20261015.json
```

main의 C/inc 태스크 생성 18개 지점과 일부 설정, 해당 파일 SHA256을 기록한다. 관리 컴포넌트·SDK 작업은 위의 수동 검토 표를 함께 본다. private 헤더·SD 비밀 설정·실제 토큰은 읽거나 기록하지 않는다. 파일 해시는 관련 소스가 바뀌었는지 확인하는 용도이며 현재 보드 바이너리와 일치를 증명하지 않는다.

검증 완료: preflight 메타데이터, 현재 소스 inventory 18개, 주요 affinity/스택 값 교차 확인, 인자 parser의 중첩 호출·문자열 쉼표 fixture. 미실행: 펌웨어 컴파일/링크/플래시, PC 네이티브 컴파일, 실기 CPU/전력/지연 측정.

## 공식 근거

- [ESP-IDF 5.5.5 P4 FreeRTOS: affinity와 스택 단위](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/system/freertos_idf.html)
- [ESP-IDF 5.5.5 P4 ULP LP core](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/system/ulp-lp-core.html)
- [설치 버전의 LP I2S 문서 소스](https://github.com/espressif/esp-idf/blob/v5.5.5/docs/en/api-reference/peripherals/lp_i2s.rst)

로컬 API 근거는 C:/esp/v5.5.5/esp-idf 아래의 동일 버전 헤더·드라이버·FreeRTOS 소스다. 현재 설정과 결론은 이전 메모리 수치를 그대로 사용하지 않고 재확인했다.
