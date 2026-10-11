# 비앙카와 페르난도: 보드 음악 + R2 공연 영상

## PC에서 ngrok으로 공개하기

작업 폴더 `scripts` 안의 `Serve_Bianca_ngrok.bat`를 더블클릭합니다. Python 로컬 서버와 설치된 ngrok을 함께 실행합니다. ngrok 화면의 `Forwarding` HTTPS 주소를 열면 `board/bianca_fernando.html`이 기본 화면으로 표시됩니다. `board` 폴더의 파일만 제공합니다. MP3의 구간 요청과 HEAD 응답을 지원합니다.

- 로컬 주소: `http://127.0.0.1:8080/`.
- 포트 변경: 터미널에서 `scripts\Serve_Bianca_ngrok.bat 8081`.
- 실행 파일 지정: `scripts\Serve_Bianca_ngrok.bat 8080 --ngrok "C:\경로\ngrok.exe"` 또는 `NGROK_EXE` 환경 변수 사용.
- 현재 설치된 Python312의 ngrok 실행 파일을 우선 탐색하고, 없으면 PATH의 ngrok을 사용합니다. 기존 ngrok 인증 설정을 유지합니다. ngrok 인증 오류가 나오면 현재 ngrok 계정 설정을 확인합니다.
- 종료: 실행 창에서 `Ctrl+C`. ngrok 터널과 로컬 서버를 함께 종료합니다.
- BAT와 같은 기능을 담당하는 `serve_board_ngrok.py`를 함께 유지합니다. 이 파일은 공개되는 `board` 폴더 밖에 있습니다.

PC 정적 서버에는 보드 관리자 API가 없습니다. 페이지 안의 `/admin` 등 펌웨어 전용 링크는 지원하지 않습니다. 영상은 `bianca-media.json`에 저장된 R2 주소에서 불러옵니다.

## 제공 파일

- board/bianca_fernando.html: 반응형 작품 설명, 커튼·별빛·스크롤 애니메이션, 실제 MP3/MP4 플레이어.
- board/bianca-audio.mp3: 전체 길이 유지, 32kHz / 48kbps 모노. 7,125,620 → 1,069,029 bytes (약 85% 감소). 음질은 원본보다 낮습니다.
- board/bianca-media.json: 모든 방문자에게 적용할 영상 공개 주소. 최초에는 비어 있습니다.
- cloud/bianca-performance.mp4: H.264 480p / AAC 64kbps, 전체 길이 유지. 74,208,587 → 25,392,843 bytes (약 66% 감소). faststart 적용.

## 보드에 적용

1. ZIP을 보드에 그대로 올리지 말고 압축을 풉니다. board 폴더의 파일 3개만 관리자 파일관리에서 업로드합니다. 영상은 R2에 올립니다.
2. C3의 1.75MiB 파티션에는 다른 파일 및 SPIFFS 여유 공간도 필요합니다. 필요 없는 예제를 삭제하고 약 1.08MB인 이 패키지를 업로드하세요. 전체 예제 패키지와 함께 넣으면 공간이 부족할 수 있습니다.
3. 관리자 파일관리에서 bianca_fernando.html을 메인화면으로 지정합니다.
4. S3와 C3 소스에는 음악 MIME 및 단일 HTTP Range 응답을 추가했습니다. 수정한 펌웨어의 빌드·업로드는 사용자가 진행합니다.
5. PC에서는 board/bianca_fernando.html을 열면 로컬 압축 음원과 ../cloud의 영상을 참조합니다. 브라우저가 로컬 영상을 제한하면 페이지의 파일 선택 기능으로 MP4를 선택하세요.

## Cloudflare R2 설정

1. Cloudflare 대시보드에서 R2 Object Storage를 열고, 처음 사용한다면 R2 활성화 절차를 완료합니다.
2. Create bucket에서 전용 버킷(예: mini-web-opera)을 생성합니다.
3. 버킷의 Objects에서 cloud/bianca-performance.mp4를 업로드합니다. 이름은 bianca-performance.mp4로 유지합니다. 객체의 Content-Type은 video/mp4인지 확인합니다.
4. Settings → Public Development URL → Enable을 선택하고 공개 확인을 완료합니다. 이 버킷의 객체가 인터넷에 공개됩니다. 시험용 영상만 이 버킷에 넣으세요.
5. 표시된 Public Bucket URL 뒤에 /bianca-performance.mp4를 붙입니다. 예: https://pub-실제식별자.r2.dev/bianca-performance.mp4
6. 이 주소를 새 브라우저 창에서 열어 재생을 확인합니다. S3 API 주소인 r2.cloudflarestorage.com 주소는 공개 재생 주소가 아닙니다.
7. 오페라 페이지의 “영상 연결 및 로컬 재생 시험”에 이 HTTPS 주소를 넣고 “영상 연결”을 누르면 즉시 현재 브라우저에 적용됩니다.
8. 모든 휴대전화에 적용하려면 bianca-media.json을 아래와 같이 수정한 뒤 관리자에서 같은 이름으로 업로드합니다.

```json
{"videoUrl":"https://pub-실제식별자.r2.dev/bianca-performance.mp4"}
```

브라우저에 저장된 시험 주소가 있으면 JSON보다 우선합니다. 새 설정을 확인할 때는 새 브라우저 또는 사이트 데이터 삭제 후 접속하세요. API 키와 비밀키는 이 파일이나 HTML에 넣지 않습니다.

r2.dev는 개발용이며 요청 제한이 있습니다. 지속 운영할 때는 버킷 Settings → Custom Domains에서 Cloudflare에 등록된 도메인의 서브도메인을 연결하고 해당 주소를 사용하세요.

기본 video 태그로 직접 재생하므로 별도 CORS 설정은 사용하지 않습니다. 영상은 휴대전화가 R2에서 직접 받습니다. 보드의 인터넷 연결과 별개로 휴대전화에 인터넷 연결이 필요합니다. 인터넷 없는 보드 AP에서는 로컬 음악만 재생됩니다.

## 확인할 항목

- 모바일에서 음악 재생·일시정지·앞뒤 탐색, 영상 재생 및 탐색.
- 음악 재생 중에는 원판/파형 애니메이션이 움직이며 영상 재생 시 음악은 정지합니다.
- 모션 줄이기 설정과 모바일 세로 화면.
- R2 공개 URL이 없는 상태에서도 작품 설명과 음악은 이용 가능합니다.
- 실제 보드 재생, 펌웨어 빌드 및 R2 업로드는 아직 확인하지 않았습니다.

## 자료

작품 설명: https://en.wikipedia.org/wiki/Bianca_e_Fernando
영상·음원은 사용자가 제공한 원본을 압축했습니다. 영상 표기는 파일명 기준이며 작품 판본을 단정하지 않습니다.
R2 공개 설정: https://developers.cloudflare.com/r2/buckets/public-buckets/
R2 업로드: https://developers.cloudflare.com/r2/objects/upload-objects/
