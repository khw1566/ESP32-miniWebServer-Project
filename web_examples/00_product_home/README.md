# Mini WebServer 제품 기본 페이지

보드를 처음 켜고 접속했을 때 보이는 제품 소개 페이지입니다. 모든 펌웨어(M1, S3-N16R8, S3, C3)에 들어가며, 첫 부팅 때 저장소의 `index.html`로 복사됩니다.

## 구성

| 영역 | 내용 | 미디어 |
| --- | --- | --- |
| 첫 화면 | 제품 소개와 분해 애니메이션 영상 | R2 `hero.webm`, `hero.mp4`, `hero-poster.webp` |
| 내부 구조 | 스크롤하면 커버, 기판, 케이스가 분리되는 3D 모형 | 없음 (CSS 3D) |
| 시작하기 | 보드 화면과 휴대전화로 보여주는 설정 3단계 | 없음 (CSS 애니메이션) |
| 기능, 구조, 라인업 | 기능 6개, 시스템 구성도, 모델 4종 | R2 `architecture.webp`, `lineup-*.webp` |
| 마무리 | 같은 Wi-Fi에서만 관리자, AI, Wi-Fi 설정 버튼 표시 | 없음 |

- 페이지 파일은 약 31 KB이며, 큰 미디어는 Cloudflare R2(`https://media.audreaming.com/product/v2/`)에서 불러옵니다.
- 인터넷이 없는 환경(SoftAP 등)에서는 미디어 영역만 숨기고, CSS 애니메이션과 본문은 그대로 동작합니다.
- 기기에서 동작 줄이기를 켜면 영상 자동 재생과 애니메이션을 멈춥니다.
- 관리자, AI, Wi-Fi 설정 버튼은 `/api/system`에 응답이 있을 때(같은 Wi-Fi)만 보입니다. 외부 도메인 접속에는 표시하지 않습니다.

## 보드에 들어가는 방식

1. `node tools/embed_factory_page.cjs`가 이 파일을 네 펌웨어의 `main/web_page_factory.c`로 넣습니다.
2. 보드는 첫 부팅 때 저장소에 `index.html`이 없으면 이 페이지를 복사하고, NVS에 `factory_seed`를 기록합니다.
3. 사용자가 파일을 지우거나 바꿔도 다시 덮어쓰지 않습니다. 관리자 페이지의 저장소 초기화를 하면 다시 복사합니다.
4. 이미 `index.html`이 있는 보드는 그 파일을 유지합니다.

페이지를 고친 뒤에는 다시 생성하고 펌웨어를 빌드합니다.

생성 도구는 이 폴더의 `index.html`을 사용하며, 해당 파일이 없으면 기존 `index1.html`을 사용합니다.

```powershell
node tools/embed_factory_page.cjs
```

## 미디어 다시 만들기와 업로드

미디어는 `tools/make_product_media.py`가 만듭니다. 영상은 S3v2 도면 치수(68 x 31.8 x 9 mm)를 바탕으로 그린 분해 애니메이션이고, 라인업 그림은 경진대회 도면에서 분해도 부분을 잘라낸 것입니다. ffmpeg와 Pillow가 필요합니다.

```powershell
python tools/make_product_media.py
```

업로드한 파일은 1년 캐시(`immutable`)로 제공됩니다. 내용을 바꾸면 같은 경로에 덮어쓰지 말고 새 버전 폴더(`product/v3/` 등)에 올린 뒤 `index.html`의 `MEDIA` 주소를 바꿉니다.

```powershell
cd cloud-relay
npx wrangler r2 object put mini-web-media/product/v3/hero.mp4 --file ../web_examples/00_product_home/media/hero.mp4 --content-type video/mp4 --cache-control "public, max-age=31536000, immutable" --remote
```

버킷: `mini-web-media`, 공개 주소: `media.audreaming.com` (R2 사용자 지정 도메인).
