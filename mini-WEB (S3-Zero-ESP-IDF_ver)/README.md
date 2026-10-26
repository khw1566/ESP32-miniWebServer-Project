# Mini WebServer for ESP32-S3 Zero

ESP32-S3FH4R2용 독립 ESP-IDF 5.5.5 프로젝트입니다. `../mini-WEB (S3-ESP-IDF_ver)/main`을 공통 애플리케이션으로 사용하고 설정·파티션·빌드 출력은 이 폴더에 둡니다. 기존 8MB S3 바이너리를 이 보드에 기록하지 마세요.

- 대상 실기기: ESP32-S3 rev 0.2, 내장 Flash 4MB, Quad PSRAM 2MB, USB Serial/JTAG.
- USB 콘솔: 보드 USB-C 포트, 115200 baud. UART 변환기는 필요하지 않습니다.
- Flash: DIO 80MHz. PSRAM: Quad 40MHz, 부팅 메모리 검사 활성화.
- CPU: 160MHz. DMA·제어 데이터용 내부 RAM 32KiB를 예약합니다.
- 이 보드의 STA 송신 출력은 15dBm으로 제한합니다. 기본 출력에서 강한 WPA2 AP 신호에도 인증 시간 초과가 발생했고, 15dBm 비교 시험 및 이후 재부팅 자동 연결에 성공했습니다. 이 설정은 이 프로젝트의 main 컴포넌트에만 적용합니다.
- OTA: 1536KiB 슬롯 2개. SPIFFS 파티션: 832KiB, 파일시스템 포맷 후 실제 사용 가능 용량은 더 작습니다.
- 기존 보드의 NVS 위치 `0x9000/0x5000`를 유지합니다. 파티션 변경 전 전체 백업이 필요합니다. 이전 SPIFFS 위치와는 호환되지 않으므로 기존 웹 파일은 백업에서 별도로 복구해야 합니다.
- Wi-Fi가 없으면 `Mini-WebServer-Setup`에 연결하고 `http://192.168.4.1/setup`에서 설정합니다.
- 제품 ID와 중계 주소는 공통 S3 규칙을 사용합니다: `s3-<MAC>.audreaming.com`. 기기별 토큰과 Cloudflare 등록이 모두 필요합니다.
- 중계 API는 `https://relay-s3-zero.audreaming.com`입니다. 기존 M1/M3/C3 중계의 기기 등록을 덮어쓰지 않도록 `../cloud-relay/wrangler.s3-zero.toml`로 별도 Worker를 배포합니다. 비공개 `provisioned/registry.s3-zero.json`에는 이 보드 등록만 들어 있습니다.

ESP-IDF 5.5.5 환경에서 이 폴더를 작업 디렉터리로 사용합니다.

```powershell
idf.py build
idf.py -p COM19 -b 921600 flash
idf.py -p COM19 monitor
```

COM 번호는 연결 환경에 따라 달라집니다. 펌웨어 flash는 기존 NVS를 지우지 않습니다. `erase-flash`는 Wi-Fi·관리자·기기 토큰을 제거하므로 일반 업데이트에는 사용하지 마세요.

## 2026-10-26 실기 검증

MAC `90da72475334`, COM19에서 빌드·링크·업로드 및 기록 해시 검증에 성공했습니다. 최종 앱 크기 `0x148ea0` (1,347,232 bytes), OTA 슬롯 여유 14%. USB 부팅 로그에서 PSRAM 2MB 인식 및 메모리 검사 통과, SPIFFS 마운트, SoftAP·mDNS 시작을 확인했습니다. 기존 QR 코드의 pragma/type-limits 및 사용하지 않는 함수 경고는 남아 있으며 빌드 오류는 없습니다.

로컬 주소: `http://s3-v1-2-90da72475334.local/` (같은 네트워크에서 사용).

공개 주소: `https://s3-90da72475334.audreaming.com/`. 전용 Worker와 도메인 배포, 로컬 관리자 인증을 통한 기기 토큰 저장, 재부팅 후 Wi-Fi 자동 연결(IP `192.168.0.33`), `cloud_status=online`, 실제 공개 페이지 HTTPS 200을 확인했습니다. 외부 `/admin`은 403으로 차단됩니다. Chrome에서도 기본 페이지가 표시됐습니다. 기본 중계는 동시 요청 4개로 제한되며 검사 중 한 차례 요청 제한 안내가 나온 뒤 재요청으로 정상 표시됐습니다. 장시간 부하 시험은 수행하지 않았습니다.

최종 바이너리 SHA-256: `775b81fbaddba5bff8a5d49560d3e8481ecef419395a18bfd88a79d0ed9d0a5a`.

빌드·업로드·시리얼 기록은 저장소 제외 경로 `../results/s3-zero-20261026/`, 기존 전체 Flash 백업은 그 아래 `.private/before-flash.bin`에 있습니다. 최종 상태와 HTTP 검사 결과는 `verification.json`, 재부팅 로그는 `serial-reboot-verified.log`를 참고하세요. 시리얼의 `/scan` 명령으로 AP 신호와 인증 방식을 조회하고, `/tx-low`로 실행 중 송신 출력을 15dBm으로 설정할 수 있습니다.
