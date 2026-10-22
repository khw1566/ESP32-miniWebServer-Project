# 외부 도메인 다운로드: 첫 개선 패키지

## 적용한 코드

- 새 Worker와 M3는 poll의 `X-Relay-Capabilities: stream-v2`로 기능을 확인한다.
  새 Worker가 작업에 `transport: stream-v2`를 붙인 경우에만 새 전송을 사용한다.
  기존 Worker에 새 M3를 연결하거나, 새 Worker에 기존 M1/M3를 연결하면 v1을 사용한다.
- v2는 파일 하나를 `/v2/devices/<id>/result/<job>/stream`에 한 번의 HTTPS POST로
  전송한다. 파일/PSRAM에서 16 KiB씩 읽고 부분 write를 처리하며 매 블록 HTTP 응답을 기다리지 않는다.
- 전송 버퍼 16 KiB는 PSRAM에 할당하고 성공·실패 후 해제한다. 기존 `Main WebFlie`
  선적재 자산을 그대로 읽는다. 파일 전체를 추가 복사하거나 두 번째 사이트 캐시를 만들지 않는다.
- Worker는 본문을 `ReadableStream`으로 전달하며 최대 한 입력 블록만 선행 처리한다.
  파일 전체 `arrayBuffer`/문자열 버퍼링은 하지 않는다. 방문자 취소, 입력 끊김,
  조기 EOF, 초과 바이트, 중복 전송, 잘못된 Range, 재접속을 처리한다.
- v2 다운로드 상한은 64 MiB, 진행 없는 유휴 시간은 15초, 전체 상한은 10분이다.
  v1의 2 KiB chunk/8 MiB 상한 및 원격 업로드 8 MiB 제한은 유지한다.
- v2는 미디어 Range를 요청한 범위대로 처리하며 EOF에서 자른다.
  기존 v1 미디어의 256 KiB 구간 제한은 유지한다.
- AI/음성 TLS 독점 요청, 서버 정지, Wi-Fi 끊김을 다음 bounded write에서 확인한다.
  실패한 POST는 연결을 닫고, 부분 파일을 성공으로 확인하지 않는다.
- `CLOUD_RELAY` 로그에 전송 모드, 성공 여부, 바이트 수, 경과·읽기·쓰기 시간,
  KB/s를 남긴다. 이 수치는 보드 측 전송 시도이며 최종 방문자 속도는 별도로 측정한다.
- 라디오·영상·중계가 공유하는 Wi-Fi 절전 lease를 추가했다. 마지막 사용자가 끝날 때
  원래 절전 상태로 복구한다. Hosted 절전 RPC 실패는 서비스 실패로 처리하지 않는다.

## 기본 통신 설정

| 설정 | 이전 | 현재 |
| --- | ---: | ---: |
| TCP 송신 버퍼 | 5760 B | 65534 B |
| TCP 수신 윈도 | 32768 B | 65534 B |
| TCP 수신 mailbox | 32 | 64 |
| TCP/IP 수신 mailbox | 32 | 64 |
| TCP SACK | 꺼짐 | 켜짐 |
| TLS 출력 버퍼 | 2048 B | 8192 B |
| P4 SDIO 클럭 | 20 MHz | 20 MHz 유지, 40 MHz 전환 도구 추가 |

sdkconfig.defaults, 로컬 sdkconfig 및 cmake/memory_policy.cmake에 같은 값을 적용했다.
일반 데이터/네트워크 힙은 기존 PSRAM 정책을 따르며, DMA 제어 구조와 작업 스택을
외부 메모리로 옮기지 않았다. 연결당 TCP 버퍼 상한 및 TLS 송신 버퍼가 증가하므로
실물의 최소 내부 힙·최대 DMA 블록·PSRAM 잔량을 확인해야 한다.

C6 준비 설정에는 HIGH_SPEED를 추가했다. P4 40 MHz는 호환 C6 이미지 설치를 먼저
확인해야 하므로 자동으로 켜지 않았다. prepare_c6.ps1은 현재 패키지의 `slave`와
이전 `examples/slave` 배치를 모두 지원하도록 수정했다.

## 호스트 검증 결과

- Cloudflare 중계 검사 30개 통과: 기존 보드·원격 업로드와 새 v2 전송을 함께 검사.
  10 MiB 바이너리 스트림의 해시 일치, Range, 0바이트 응답, 취소, 지연/전체 제한,
  입력 오류, 재접속, 인증, 크기·길이 제한을 포함한다.
- test_relay_stream.cjs: 실제 C 함수 본문을 SDK 모의 객체로 실행해 부분 write,
  전체/유휴 시간 제한, 취소, PSRAM 할당 실패·해제, 읽기/쓰기/응답 오류를 검사.
- test_wifi_performance.cjs: 세 서비스의 lease가 겹쳐도 마지막 사용자 전까지 절전을
  복원하지 않는지, RPC 실패·기존 무절전 상태를 처리하는지 검사.
- test_relay_yield.cjs, test_web_psram.cjs, test_memory_budget.cjs,
  test_dma_memory.cjs, test_psram_policy.cjs 통과.
- test_touch_apps.cjs, 저장소 tools/test_network_setup.cjs, test_audio_output.cjs,
  test_tube.cjs, test_hosted_compat.cjs 통과. Hosted fixture의 CRLF 비교만 정규화했다.
- Python 다운로드 측정 도구 검사 3개와 SDIO/설정 일치 검사 2개 통과.
  Python 검사는 127.0.0.1의 임시 서버만 사용했으며 실제 M3·인터넷 속도 측정이 아니다.
- Wrangler 4.147.0 deploy --dry-run 통과. 배포 묶음 약 20.14 KiB, gzip 약 5.74 KiB.
  결과물: 저장소 results/m3-relay-stream-v2. 실제 클라우드 배포는 수행하지 않았다.
- preflight: 현재 ESP32-P4 / IDF 5.5.5 / LVGL 9.2.2 및 기존 cloud_relay.c 빌드
  메타데이터 일치. 새 소스의 컴파일·링크 결과는 사용자 빌드로 확인해야 한다.

이 결과는 펌웨어 빌드, 실제 Cloudflare 런타임 통합, Wi-Fi/TLS 처리량 또는
30분 실물 안정성 검증을 대신하지 않는다. 5 MB/s 달성 수치는 아직 없다.

## 실서비스 반영 순서

1. 인증된 Cloudflare 터미널에서 Worker를 배포한다. 필요 시 `wrangler login`으로
   사용자가 로그인한다. 현재 설정된 routes/bindings와 DEVICE_REGISTRY secret을 유지한다.

   ```powershell
   Set-Location 'C:\Users\work\Desktop\Mini WebServer\cloud-relay'
   .\node_modules\.bin\wrangler.cmd deploy
   ```

2. 사용자가 ESP-IDF 5.5.5 터미널에서 M3를 빌드·업로드한다.

   ```powershell
   Set-Location 'C:\Users\work\Desktop\Mini WebServer\mini-WEB (M3-ESP-IDF_ver)'
   idf.py build
   idf.py -p COM번호 flash monitor
   ```

3. 로그에 `stream-v2`가 나오는지 확인한다. 새 서버를 배포하기 전에는 새 펌웨어도
   v1이므로 속도 측정에서 전송 모드를 반드시 확인한다.

## 외부 다운로드 측정

가장 먼저 기존 실제 파일로 측정한다. HTTP 200의 전체 본문을 사용하고,
압축된 응답·해시 불일치·미완료·실패를 성공 속도로 평균하지 않는다.
보고서는 기존 파일을 덮어쓰지 않는다. 요청 query/자격증명은 보고서에 기록하지 않는다.

```powershell
Set-Location 'C:\Users\work\Desktop\Mini WebServer\mini-WEB (M3-ESP-IDF_ver)'
python -X utf8 tools/benchmark_external_download.py --url 'https://기기도메인/media/movie.mp4' --repeat 5 --output download-report.json
```

처음에는 기존 1~8 MiB 파일로 동작을 확인하고, v2 확인 후 64 MiB로 확대한다.
독립된 외부 인터넷 회선의 PC에서 실행한다. MB/s는 1,000,000 bytes/s다.
첫 바이트 시간, 전체 평균 속도, 본문 구간 속도, SHA-256, 전송 모드,
Cloudflare 캐시 상태가 각각 기록된다. 다섯 회 모두 5 MB/s를 넘어야 도구가 PASS한다.
브라우저 캐시는 쓰지 않으며 Cloudflare 캐시 적중/미적중은 출력 헤더로 구분한다.

저작물 없는 테스트 파일을 직접 만들 수도 있다. 다음 파일은 다운로드 측정용
바이너리이며 MP4 재생용 파일이 아니다. E: 및 media 폴더는 실제 SD 경로에 맞춘다.
64 MiB 파일을 Main WebFlie에 두면 선적재 예산을 초과하므로 SD의 media 폴더에 둔다.

```powershell
python -X utf8 tools/benchmark_external_download.py --make-fixture 'E:\media\download-benchmark.mp4' --size-mib 64
```

출력된 SHA-256을 측정 명령의 `--expected-sha256`에 전달한다. 모든 회차의 해시도
서로 같아야 PASS한다. 기준 미달이어도 JSON 보고서는 저장된다.

## 선택: SDIO 40 MHz 실기 평가

먼저 prepare_c6.ps1을 실행하고 출력된 절차에 따라 사용자가 HIGH_SPEED C6 이미지를
빌드한다. SD c6_update.bin 경로로 설치가 성공한 뒤 P4 설정을 바꾼다.

```powershell
Set-Location 'C:\Users\work\Desktop\Mini WebServer\mini-WEB (M3-ESP-IDF_ver)'
.\prepare_c6.ps1
# 출력된 별도 C6 프로젝트의 빌드/설치를 사용자가 완료한 다음:
python -X utf8 tools/configure_sdio_clock.py --clock-mhz 40 --c6-high-speed-installed
# 이후 사용자가 P4를 다시 빌드·업로드하고 오류/속도를 비교한다.
```

CRC/시간 초과/재접속이 늘면 다음 명령으로 20 MHz를 복원하고 사용자가 재빌드한다.

```powershell
python -X utf8 tools/configure_sdio_clock.py --clock-mhz 20
```

## 다음 판단

이번 패키지는 보드 직접 중계의 왕복 대기를 제거하는 첫 단계다. HT20/HT40,
Wi-Fi remote 버퍼·BA window, 더 큰 TCP window scaling은 기준 실측에 따라 조정한다.
CDN/R2 게시 기능은 이번에 추가하지 않았고 공개 파일을 클라우드 저장소로 복제하지 않았다.
실측 후 직접 중계가 5 MB/s에 못 미치면 원래 계획의 공개 정적 파일 게시·캐시 경로를
구체화한다. CDN 방문자 속도는 보드 Wi-Fi 속도와 분리하여 보고한다.
