> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# M4 외부 도메인 다운로드 성능 개선 계획

첫 구현의 적용 내용·검증 결과·배포와 실측 절차는
[외부 다운로드 첫 개선 패키지](EXTERNAL_DOWNLOAD_IMPLEMENTATION.md)에 기록했다.
아래의 현재 구조 표는 계획 작성 시점의 기준값이다.

작성일: 2026-10-15. 사용자 목표: 외부 도메인에서 다운로드할 때 최소 5 MB/s.
이 문서는 현재 소스와 설치된 SDK/컴포넌트를 조사한 계획이며, 구현이나 실기 성능 결과가 아니다.
펌웨어·중계 코드는 변경하지 않았다. 빌드·업로드는 저장소 지침에 따라 사용자가 수행한다.

## 목표와 판단 기준

- MB/s는 1,000,000 bytes/s 기준으로 계산한다. 5 MB/s = 파일 본문 40 Mbit/s.
- 주 목표는 브라우저 캐시를 비운 외부 클라이언트가 도메인으로 받는 실제 파일 전송 속도다.
- 보드에서 매번 직접 읽어 중계하는 전송과 클라우드에 게시된 정적 파일 전송을 각각 기록한다.
  CDN 다운로드 5 MB/s를 보드 Wi-Fi 5 MB/s로 보고하지 않는다.
- 초기 직접 중계의 하위 계층 측정 목표는 P4 TCP 송신 50 Mbit/s 이상이다.
  이는 TLS/HTTP 등에 여유를 두기 위한 설계 목표이며, 보장치가 아니다.
- 공식 ESP-Hosted P4+C6 참고 결과는 4-bit SDIO 40 MHz에서 TCP 송신 32 Mbit/s,
  수신 30 Mbit/s다. 이 측정은 MCU/무선 설정에 따라 달라지며 절대 상한은 아니다.
  그래도 현 C6 경로에서 외부 HTTPS 40 Mbit/s를 보장할 근거는 없다.

## 현재 확인한 구조

전송 경로:

```text
SD / Flash → P4 파일 읽기 → P4 TLS/lwIP → SDIO → C6 Wi-Fi
          → 공유기 인터넷 업로드 → Cloudflare Worker / Durable Object
          → 방문자 인터넷 다운로드
```

| 항목 | 현재 소스/설정 | 개선 검토 |
| --- | --- | --- |
| 중계 데이터 | 2048바이트 POST 후 204 응답을 기다림 | 파일당 연속 스트림 |
| Worker 제한 | chunk 2048바이트, 전송 8 MiB, 작업 60초 | v2 경로의 크기·유휴 제한 설계 |
| 동시 전송 | 기기당 활성 작업 1개 | 우선 단일 파일 스트림 처리량 확보 |
| 공개 응답 | Cache-Control: no-store | 선택한 공개 정적 파일만 버전 캐시 |
| P4 TCP 송신 버퍼 | 5760바이트 | 32 KiB → 약 64 KiB 단계 평가 |
| P4 TCP 수신 윈도 | 32768바이트 | 약 64 KiB 단계 평가 |
| P4 TLS 송신 레코드 버퍼 | 2048바이트 | 전송 전용 8/16 KiB 후보 |
| P4↔C6 | SDIO 4-bit 20 MHz | 양쪽 설정을 맞춰 40 MHz 평가 |
| C6 slave 설정 | ESP_SDIO_DEFAULT_SPEED | HIGH_SPEED 설정과 호환성 확인 |
| Wi-Fi remote | RX 10/32, TX 32, BA window 6 | 공식 성능 설정을 단계 적용 |
| 웹 파일 PSRAM 선적재 | 없음 | 작은 정적 파일 캐시와 SD 읽기 선행 |

P4의 Wi-Fi 설정은 esp_wifi_remote를 거친다. P4의 CONFIG_ESP_WIFI_* 복제값만
바꾸고 끝내지 말고 CONFIG_WIFI_RMT_*와 실제 C6 부팅 설정을 확인한다.
P4 IDF 5.5.5, Hosted 2.13.0, wifi_remote 1.6.5를 기준으로 시작한다.
실제 보드에 올라간 P4/C6 버전은 별도 로그로 확인해야 한다.

현재 중계는 애플리케이션 수준에서 매 2 KiB마다 응답을 기다린다.
왕복 시간이 20/50/100 ms라고 가정하면 이 대기만으로 계산한 속도는 각각
약 0.102/0.041/0.020 MB/s다. 이는 실제 측정치가 아닌 구조적 지연 예시다.
같은 방식에서 chunk만 64 KiB로 늘려도 RTT 50 ms이면 약 1.31 MB/s이므로,
5 MB/s 목표에는 chunk 확대만으로 충분하지 않다.

## 1단계: 구간별 기준 측정

1. PC를 공유기 유선 LAN에 연결하여 M4 Wi-Fi와 PC Wi-Fi의 중복 영향을 줄인다.
   외부 측정은 별도 인터넷 회선의 유선 PC를 우선 사용한다.
2. C6 및 P4 버전, 실제 SDIO 클럭, RSSI, 채널/대역폭/프로토콜,
   전원 절약 상태, BLE/APSTA 사용 여부, 내부 힙/최대 DMA 블록을 기록한다.
3. P4↔PC 단일 스트림 TCP iperf를 양방향 60초씩 5회 측정한다.
   UDP는 손실/링크 진단용이고 HTTP 목표 판정에는 사용하지 않는다.
   기존 예제가 iperf2인지 확인해 호환되는 PC 도구를 사용한다.
4. 같은 데이터의 SD 연속 읽기, PSRAM 소스 로컬 HTTP, SD 소스 로컬 HTTP,
   PSRAM 소스 외부 HTTPS, SD 소스 외부 HTTPS를 나누어 측정한다.
5. 최초에는 현 8 MiB/60초 제한 안에서 256 KiB → 1 MiB → 4 MiB → 8 MiB로
   시험한다. 완료 실패/타임아웃을 성공 속도로 평균하지 않는다.
6. 공유기 인터넷 업로드와 외부 PC 다운로드를 독립 측정한다.
   파일 40 Mbit/s를 위해 각 구간에 실효 50 Mbit/s 이상의 여유를 권장한다.

측정 항목: 실제 본문 bytes/전체 경과시간, 첫 바이트 시간, 본문 전송 구간 속도,
파일 해시, SD 읽기 대기, TLS 쓰기 대기, CPU 사용률, SDIO 오류/패킷 드롭,
작업별 스택 여유, 내부 힙/PSRAM 최소값. 캐시 적중과 미적중, 일반/집중 모드는 구분한다.
현재 관리자 화면의 작은 상태 요청으로 추정한 속도는 처리량 판정에 사용하지 않는다.

## 2단계: 중계 v2 연속 스트리밍

가장 먼저 구현할 개선이다.

- 기존 poll 제어 경로를 유지하고, 다운로드 데이터는 파일당 한 번의 HTTPS POST로
  연속 전송한다. 정확한 길이를 알면 Content-Length를 사용하고 16 KiB 단위로 쓴다.
- 보드는 여러 번 write하더라도 매 write마다 HTTP 응답을 기다리지 않는다.
  전송 완료 시 최종 수신 확인을 검사한다.
- Worker는 Request.body를 TransformStream으로 방문자 Response에 전달한다.
  파일 전체를 text()/arrayBuffer()로 읽지 않는다.
- 느린 방문자에 대한 역압력, 방문자 취소, 보드 연결 끊김, 조기 EOF,
  초과 데이터, Range/206/416, 요청 ID·기기 인증 검사를 유지한다.
- 전체 60초 고정 제한을 전송 길이/진행 상태와 유휴 시간 제한으로 재설계한다.
  사용하지 않는 작업이 무한히 남지 않도록 상한을 별도로 둔다.
- v2 시험 다운로드만 최대 64 MiB로 확대한다. 기존 업로드 크기 제한과 v1
  다른 보드의 동작은 유지한다. 기존 사용자는 capability 협상으로 v1에 남긴다.
- AI/음성의 TLS 독점 요청 시 현재 중계 취소·정리 경로를 보존한다.
  취소된 다운로드와 집중 모드 성능을 정상 지속 전송과 구분해 보고한다.

산출물: M4 cloud_relay.c와 web_public_stream.inc의 v2 송신, Worker v2 수신,
버전 협상·취소·불완전 응답·Range·느린 방문자에 대한 호스트 검사.
Cloudflare 설정/요금제의 실제 제한은 구현 시 확인한다.

## 3단계: Wi-Fi/SDIO/TCP/TLS 조정

- 전송 중 WIFI_PS_NONE을 적용한다. 라디오·영상에서도 절전 설정을 바꾸므로
  중앙 소유권/참조 수 방식으로 관리하고 마지막 사용자가 끝날 때 원래 값으로 복구한다.
- 먼저 2.4 GHz의 가까운 AP에서 시험하고, HT20/HT40을 같은 조건에서 비교한다.
  40 MHz 채널은 주변 간섭과 AP 협상에 따라 실제로 더 느릴 수 있다.
- P4 SDIO 20→40 MHz 변경은 C6 HIGH_SPEED 설정과 함께 적용한다.
  기존 핀/회로에서 CRC·시간 초과·드롭이 증가하면 20 MHz로 되돌린다.
  50 MHz는 40 MHz 안정성 확보 후 별도 평가 대상으로 둔다.
- P4 TCP 송신 버퍼를 32 KiB, 약 64 KiB 순서로 비교한다. 수신 윈도와 mailbox,
  SACK도 측정에 맞춰 조정한다. 연결 수가 늘면 총 사용량도 계산한다.
- 공식 C6 remote 성능 후보는 static RX 16, dynamic RX/TX 64, BA window 32다.
  P4와 C6의 실제 협상값/힙 사용을 확인하고 한 그룹씩 바꾼다.
- TLS 출력 2→8→16 KiB를 비교한다. 전역 증가의 다른 TLS 세션 영향을 계산하고,
  가능하면 중계 전용 설정으로 분리한다. TLS/일반 데이터는 PSRAM 정책을 따른다.
- WAN 왕복 시간이 50 ms이면 5 MB/s에는 약 250 KB의 비행 중 데이터가 필요하다.
  64 KiB에서 막히면 window scaling/송신 버퍼를 128/256 KiB까지 별도로 평가한다.
  IDF 5.5.5 Kconfig의 LWIP_WND_SCALE은 SPIRAM_TRY_ALLOCATE_WIFI_LWIP에 의존한다.
  이 옵션은 현재 꺼져 있으므로 메모리 정책 검토 없이 숫자만 키우지 않는다.
- sdkconfig.defaults와 기존 sdkconfig의 마이그레이션을 함께 반영한다.
  특히 cmake/memory_policy.cmake가 현재 수신 윈도를 32768로 덮어쓴다.
- SDIO/I2S 디스크립터·제어 상태와 NVS/파일시스템/TLS 호출 스택은 내부 RAM에 둔다.

판정: 단일 TCP 송신이 40 Mbit/s에 못 미치면 보드 직접 외부 5 MB/s 달성의
하위 조건을 충족하지 못한 것이다. 40 Mbit/s를 넘어도 TLS/HTTP를 포함한 실측이 필요하다.
적절한 설정에서도 정체되면 동일 설정의 반복 조정을 중단하고 5단계로 판단한다.

## 4단계: PSRAM 캐시와 파일 읽기

- 전송 버퍼는 16 KiB×2부터 시작해 32 KiB×2와 비교한다.
  SD 읽기와 TLS 전송의 생산자/소비자 큐를 제한된 크기로 운용한다.
- HTML/CSS/JS와 작은 이미지에 대해 PSRAM 2 MiB 상한, 파일당 256 KiB 상한을
  초기 후보로 사용한다. 실제 여유량과 앱 동시 사용을 보고 조정한다.
- 캐시 키는 저장소·경로·파일 버전이다. 업로드/삭제/교체 시 무효화하고,
  전송 중인 항목은 참조가 끝나기 전 해제하지 않는다.
- 캐시 할당 실패 시 원래 파일 스트리밍으로 복귀한다. 큰 영상/음원은 전체 선적재하지 않는다.
- 관리자·API 키·인증 응답과 SD .private 파일을 공개 파일 캐시에 넣지 않는다.
- PSRAM은 파일 읽기 병목을 줄인다. 무선/인터넷 대역폭이나 중계 왕복 지연의 대체 수단은 아니다.

## 5단계: 외부 도메인 5 MB/s의 제공 방식 결정

### 공개 정적 파일에 권장: 클라우드 게시 및 캐시

SD/Flash를 원본으로 유지하고 공개할 정적 파일만 변경 시 클라우드 저장소에
동기화한다. 콘텐츠 해시가 들어간 URL/manifest와 ETag로 버전을 관리하고,
완전한 업로드가 끝난 후 manifest를 교체한다. 도메인은 저장소/CDN에서 파일을 제공한다.
공개 HTML/CSS/JS/이미지/다운로드 파일만 대상이며 인증·기기 제어는 기존 경로에 둔다.

Cache API만 사용하면 지역별 미적중이 발생할 수 있으므로 항상 빠른 최초 다운로드가
필요한 경우 R2 같은 원본 저장소와 CDN을 검토한다. 클라우드 캐시 전에는 현재
no-store 정책을 경로별로 분리하고 삭제/공개 취소도 무효화한다.
비용·저장 범위·오프라인 공개 유지 여부를 구체화한 뒤 배포 단계에서 확정한다.

이 경로는 방문자 다운로드 5 MB/s 목표의 달성 가능성을 높이지만,
보드 Wi-Fi가 5 MB/s로 동작한다는 의미는 아니다. 보드→클라우드 동기화 속도도 따로 보고한다.

### 모든 요청을 보드가 직접 보내야 하는 경우

클라우드에 파일을 보관할 수 없으면 2~4단계 실기 결과를 기준으로 판단한다.
P4+C6가 목표에 못 미치면 Ethernet 또는 더 높은 처리량의 무선 모듈/보드가 대안이다.
Ethernet은 Wi-Fi 목표와 구분하며, 모듈 변경은 핀·회로·드라이버·전원 검토 후 별도 작업으로 잡는다.

## 완료 기준과 실행 순서

1. 측정 도구/로그 준비 → 기준 측정.
2. v2 연속 스트리밍 → 동일 파일/환경에서 비교.
3. SDIO·TCP·TLS·절전 설정 단계 조정 → 각 단계 회귀 점검.
4. PSRAM 캐시/파일 읽기 선행 → SD와 PSRAM 소스 비교.
5. 직접 중계의 목표 달성 여부 판단 → 필요한 경우 공개 정적 파일의 클라우드 게시.

새 v2 경로에서 압축이 잘 안 되는 64 MiB 바이너리를 외부 PC에 5회 다운로드한다.
각 회차 전체 본문 bytes/경과시간이 5,000,000 bytes/s 이상이고 해시가 같아야
해당 환경에서 목표를 달성한 것으로 기록한다. DNS/TLS/첫 바이트를 포함한 시간과
본문 구간 시간을 함께 남긴다. 브라우저 캐시는 비우고 Cloudflare 적중/미적중은 분리한다.
64 MiB 파일은 약 13.42초 이내에 받아야 이 기준을 통과한다.

30분 반복 전송에서 오류·재부팅·누적 메모리 감소를 확인하고,
화면/라디오 동시 사용 및 AI로 인한 중단·복구를 별도로 시험한다.
한 요청의 속도와 여러 방문자의 합산 처리량은 구분한다.
호스트 검사, 사용자 펌웨어 빌드, 클라우드 배포, 실기·외부 인터넷 결과를 각각 보고한다.

## 근거

- 현재 프로젝트: main/cloud_relay.c, main/web_public_stream.inc, main/web_server.c,
  sdkconfig, c6_sdkconfig.defaults, cmake/memory_policy.cmake.
- 현재 중계: 저장소 루트 cloud-relay/worker.mjs.
- 설치된 Hosted 2.13.0: managed_components/espressif__esp_hosted/docs/performance_optimization.md,
  docs/sdio.md 및 slave/main/Kconfig.projbuild.
- [Espressif Hosted 성능 가이드](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/performance_optimization.md)
- [ESP-IDF 5.5.5 C6 Wi-Fi 가이드](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-guides/wifi.html)
- [Cloudflare Streams](https://developers.cloudflare.com/workers/runtime-apis/streams/)
- [Cloudflare Workers 제한](https://developers.cloudflare.com/workers/platform/limits/)
- [Cloudflare Cache 동작](https://developers.cloudflare.com/workers/reference/how-the-cache-works/)

Context7은 정확한 5.5.5 인덱스를 제공하지 않아 5.5.3 문서를 보조로 확인했으며,
구체적인 설정 가능 범위는 로컬 ESP-IDF 5.5.5 Kconfig와 설치된 Hosted 소스를 우선했다.
