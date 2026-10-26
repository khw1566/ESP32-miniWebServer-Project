# Mini WebServer 제품 소개 패키지

메인 + C3 / S3 / M1 / M3 + 사진·사양 출처, 총 6페이지입니다.
반응형 HTML/CSS/JS이며 프레임워크·빌드 의존성이 없습니다.

## 디자인과 상호작용

- 메인: 차콜 / 라임, 프로젝트 영상용 무대와 라인업·비교표.
- C3: 민트 / RISC-V와 가벼운 연결, Wi-Fi 파동 애니메이션.
- S3: 퍼플 / 개발 워크스페이스, WEB·FILES·AI 탭 전환.
- M1: 크림·테라코타 / 실제 노란 케이스 보정 사진과 PC UI 캡처 6종 전환.
- M3: 네이비·블루 / 실제 회색 케이스 보정 사진과 PC UI 캡처 6종 전환.
- 스크롤 등장·사진 부유 애니메이션, 공통 정지 버튼, 시스템 동작 줄이기 지원.
- C3·S3 애니메이션은 설명용입니다. M1·M3 화면은 실제 UI C 소스를 사용하는 네이티브 PC LVGL 렌더링입니다. 네트워크·저장소·재생·갤러리 콘텐츠는 모의 데이터이며 실물 LCD 캡처와 구분합니다.

## 파일 사용

`releases/MiniWeb-Product-Lineup-20261016.zip`을 풀어 내용물을 웹 루트에 복사합니다.
`index.html`, 네 모델 HTML, `sources.html`, `assets/`, `media/` 경로를 함께 유지하세요.
로컬 사진을 포함하므로 인터넷이 없어도 본문·사진·인터페이스 예시를 볼 수 있습니다.
웹 폰트는 온라인 Google Fonts를 사용하며 오프라인에서는 시스템 한글 폰트로 표시합니다.
보드 관리자에 올릴 때에는 폴더 구조를 유지하는 업로드 방식 또는 SD카드를 사용합니다.
기존 보드 웹 파일이나 펌웨어를 자동으로 교체하지 않습니다.

## R2

호스팅 버전은 Sites가 연결한 별도 R2 `BUCKET`을 사용합니다.
빌드가 WebP 사진·PC 화면 18장과 해시 키를 Worker에 포함하고, 각 사진의 첫 요청에서 R2에 한 번 저장합니다.
`/media/*-product-retouched.webp`, `/media/*-screen-*.webp` 등을 R2에서 읽습니다.
R2 오류 시 성공으로 가장하거나 다른 사진으로 바꾸지 않고 503을 반환합니다.
호스팅 버전의 접근 권한은 소유자 비공개입니다. 보드 배포 패키지는 사진을 로컬에 포함합니다.
기존 사용자 버킷 `mini-web-media`와 공개 도메인 `media.audreaming.com`은 변경하지 않았습니다.
기존 버킷으로 옮기려면 로그인된 Wrangler에서 아래 스크립트를 실행한 뒤
`assets/config.js`의 `mediaBase`를 `https://media.audreaming.com/product/lineup-20261016/`로 바꾸세요.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Upload-Existing-R2.ps1
```

## 나중에 영상 넣기

영상 제작은 포함하지 않았습니다. `public/assets/config.js`에서 `projectVideo`에
실제 R2 MP4 URL을 넣으면 메인에 `controls`, `playsinline` 영상이 나타납니다.
자동 재생하지 않습니다. 주소가 비어 있으면 준비 중 화면, 재생 실패 시 준비 중 화면으로 돌아갑니다.
`projectPoster`에는 선택적으로 포스터 URL을 지정합니다.
보드 ZIP을 수정한다면 압축 안의 `assets/config.js`에 같은 값을 적용하면 됩니다.

## 수정·빌드·검사

문구는 `scripts/build_pages.py`, 스타일과 상호작용은 `public/assets/`에서 수정합니다.

```powershell
python -X utf8 scripts/build_pages.py
node scripts/build.mjs
node --test scripts/worker.test.mjs
node scripts/preview.mjs
python -X utf8 scripts/package.py
```

사진 원본 URL·출처·SHA256은 `media/sources.json`, 방문자용 출처는 `sources.html`에 있습니다.
C3/S3는 Espressif 참고 개발 보드입니다. M1·M3 대표 사진은 사용자 실물 사진을 built-in ImageGen으로 보정한 것입니다. 투명 PNG 원본도 `media/*-product-retouched.png`에 포함합니다.
Flash 용량은 현재 프로젝트 설정 기준으로 표기했습니다. 실제 보드 성능 검증은 포함하지 않습니다.

## 사진 보정과 PC 캡처

사진 보정 요청: 사용자가 제공한 제품의 케이스 색·형태·화면 구성을 유지하면서 책상·모니터·케이블 배경 제거, 노출·화이트밸런스·선명도·기울기 정리, 투명 배경의 단일 제품 사진으로 제작. M1은 노란 3D 프린트 케이스, M3는 회색 케이스와 검은 전면 유리를 유지합니다. AI로 보정된 화면의 픽셀·문자·아이콘은 정밀 UI 증거가 아니며 PC 원본 캡처와 구분합니다.

PC 렌더러는 저장소 `tools/product_ui_capture/build_host.ps1`입니다. 사용자가 이 대화에서 PC 시뮬레이터 빌드·실행을 허용했습니다. ESP32 펌웨어 빌드·플래시는 실행하지 않았습니다.

- M1: 실제 `onboard_ui.c`, `m1_dashboard.c`, `m1_keyboard.c`, `font_m1.c`, LVGL 8.4; PNG 8장.
- M3: Claude가 만든 `tools/ui_sim`의 실제 `m3_ui.c`/LVGL 9.2.2 렌더러; 최신 소스로 PNG 133장.
- PC 호환: 디버그 PDB 제외, M1 QR 라이브러리 가변 길이 배열의 PC 복사본 `_alloca` 이식, M3에서 제거된 부팅 오버레이 시나리오 제외 및 PC calloc mock 보충. 펌웨어 원본은 수정하지 않았습니다.
- 원본 PNG, 소스 SHA256 및 검증 결과: `results/product-ui-capture-20261015/`.
- 전체 캡처 묶음: `releases/MiniWeb-PC-UI-Captures-20261016.zip`.
- 미디어 출처: `media/capture-sources.json`.

## M 화살표 로고

공통 헤더·푸터와 파비콘에 대문자 M의 대각선을 우상향 화살표로 확장한 SVG를 적용했습니다. `assets/logo-mark.svg`는 라임 아이콘, `assets/logo.svg`는 투명 배경 전체 로고, `assets/logo-monochrome.svg`는 검정 심벌입니다. 모든 글자는 벡터 경로이며 별도 폰트가 필요하지 않습니다. ZIP에는 이 SVG 파일을 함께 포함합니다.
