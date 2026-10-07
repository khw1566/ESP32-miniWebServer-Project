# 태평양 전쟁: 바다와 기억

`index.html` 하나로 구성된 한국어 디지털 전시입니다. 외부 폰트·이미지·라이브러리 없이 열리며 연표, 배경 설명, 미디어 자료실, 자료 출처를 포함합니다. 사진·영상·음악은 실제 업로드 파일만 표시합니다. 기본 지도는 직접 만든 개념 지도이며 실제 사료나 작전 경로가 아닙니다.

## 지금 확인하기

PC에서 `index.html`을 직접 열어 연표와 반응형 화면을 확인합니다. `자료실 관리 · 파일 전송`에서 파일을 선택하고 `이 브라우저에서 미리보기`를 누르면 선택한 이미지·영상·음악을 볼 수 있습니다. 이 미리보기는 SD카드 저장이나 업로드 성공을 의미하지 않습니다. 브라우저의 코덱 지원에 따라 재생 가능 여부가 다릅니다.

## 나중에 보드에서 사용하기

1. FAT32로 준비한 SD카드의 최상위에 이 `index.html`을 복사합니다. 펌웨어는 마운트 실패 시 카드를 포맷하지 않습니다.
2. 카드의 `media` 폴더에 이미지·영상·음악을 복사할 수 있습니다. 폴더는 없으면 펌웨어가 자동 생성합니다. 예: `media/midway-photo.jpg`, `media/pacific-video.mp4`, `media/narration.mp3`.
3. 카드와 수정된 펌웨어를 준비한 뒤 보드를 켜고 Wi-Fi를 연결합니다. 보드 IP 또는 설정한 외부 HTTPS 주소에서 페이지를 엽니다. SD 루트의 index.html이 기존 SPIFFS 첫 페이지보다 우선합니다.
4. `목록 새로고침`으로 `/media/catalog.json`의 등록 목록을 확인합니다. 원격으로는 아래의 업로드 키를 입력하고 전송합니다.

SD 슬롯 핀은 현재 ESP32-2432S028R 기준 MOSI 23 / MISO 19 / CLK 18 / CS 5, SPI3입니다. LCD는 SPI2, 터치는 기존 소프트웨어 SPI를 유지합니다. 카드 초기 연결은 10 MHz로 설정했습니다. 보드 변형에서는 실제 배선 확인이 필요합니다. 카드 탈착 시 전원을 끄고, 카드를 넣은 뒤 재부팅합니다. 실행 중 핫플러그·자동 재마운트는 아직 지원하지 않습니다.

## 원격 전송 준비

Cloudflare의 M1 중계 Worker 배포와 기기 NVS 등록은 [중계 서버 안내](../../cloud-relay/README.md)에 따릅니다. 새 `provision.mjs`는 보드 연결 토큰과 별도로 `provisioned/m1-실제MAC-upload-key.txt`에 업로드 키를 생성하고 해시를 registry.json에 추가합니다. 이 키를 사이트 자료실 관리에 입력합니다. 키는 브라우저 저장소에 저장하지 않으며 보드 연결 토큰을 사용자에게 공유하지 않습니다.

이전 버전에서 등록한 기기는 다음 명령으로 업로드 키만 추가합니다. 기존 NVS 연결 토큰은 유지합니다.

```powershell
node issue-upload-key.mjs 실제MAC12자리
Get-Content -Raw -LiteralPath provisioned/registry.json | npx wrangler secret put DEVICE_REGISTRY --name mini-web-relay
npm run deploy
```

위 명령은 `cloud-relay` 폴더에서 제작자가 실행합니다. 실제 기기 시리얼과 배포가 준비되지 않았다면 지금 실행할 필요가 없습니다.

## 저장과 공개 범위

- JPEG·PNG·WEBP·GIF, MP4·WEBM, MP3·WAV·M4A. 한 파일 최대 8 MiB, 이름 최대 64자. 영문·숫자·`_-.`를 사용합니다. 한글 이름을 선택하면 웹페이지가 영문 저장 이름을 제안합니다.
- 업로드는 2 KiB씩 SD카드에 기록하고 매 조각의 보드 응답을 확인합니다. 마지막에 크기와 SHA-256을 확인한 뒤 `media/파일이름`으로 공개합니다. 실패·취소된 임시 파일은 자료 목록에 나타나지 않습니다.
- 같은 파일명은 덮어쓰지 않습니다. 다른 이름으로 새 파일을 보내세요. 삭제·덮어쓰기와 전송 중단 후 이어받기는 이번 범위에 포함하지 않았습니다.
- 한 번에 업로드 한 개. 유휴 세션은 다음 명령에서 5분 기준으로 만료 처리합니다. 재부팅하면 남은 임시 파일을 정리합니다.
- 목록은 최대 64개입니다. 목록과 미디어는 공개 웹 자료이며 업로드만 소유자 키가 필요합니다.
- 영상과 음악은 자동 재생하지 않습니다. 원격 Range 읽기는 256 KiB 창으로 나눕니다. 큰 파일의 전송·재생 속도는 Wi-Fi, 보드, 클라우드 한도에 영향을 받습니다. 첫 시연은 작은 이미지와 짧은 영상·음성으로 진행하세요. MP4는 브라우저 호환 H.264/AAC와 Fast Start 구성을 권장합니다.
- 원격 업로드는 HTTPS 주소에서 사용합니다. 로컬 파일 및 HTTP에서는 브라우저 미리보기만 제공합니다. 클라우드 미연결 상태에서도 SD에 직접 복사한 파일은 같은 Wi-Fi의 보드 웹페이지에서 확인할 수 있습니다.

## 검증 상태

중계 프로토콜의 인증, 바이너리 전달, 저장 확인 응답, 세션·순서 검사, 취소, SD 미연결 오류 전달을 호스트 테스트로 확인했습니다. 웹페이지의 연표와 모바일 레이아웃도 브라우저에서 확인했습니다. 펌웨어는 프로젝트 지침에 따라 에이전트가 빌드·업로드하지 않았습니다. 실제 SD 마운트·메모리·저장·재생·속도·장애 복구 및 Cloudflare 배포는 나중에 실물로 확인해야 합니다.

## 역사 자료

전쟁 설명은 아래 자료를 참고한 요약입니다. 사진·영상·음악은 포함하지 않았으며, 등록자가 제공하는 파일의 출처와 공개 권한은 별도로 확인해야 합니다.

- [진주만 공격: 미국 해군 역사유산사령부](https://www.history.navy.mil/browse-by-topic/wars-conflicts-and-operations/world-war-ii/1941/pearl-harbor.html)
- [미드웨이 해전: 국립 제2차 세계대전 박물관](https://www.nationalww2museum.org/students-teachers/student-resources/research-starters/research-starters-battle-midway)
- [과달카날: 국립 제2차 세계대전 박물관](https://www.nationalww2museum.org/war/articles/battle-guadalcanal)
- [종전과 항복](https://www.nationalww2museum.org/war/topics/v-j-day-surrender-japan)
