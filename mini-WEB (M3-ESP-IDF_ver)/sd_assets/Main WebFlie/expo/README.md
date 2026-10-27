# 전시용 웹 예제 (expo)

SD 카드의 `Main WebFlie/expo/` 폴더에 이 폴더의 파일을 그대로 복사하고 보드를 재부팅한다.
부팅 시 `Main WebFlie` 전체가 PSRAM으로 올라가므로 Flash(littlefs) 용량은 쓰지 않는다. 전체 약 60 KB.

- 시작 주소: `http://<보드 IP>/expo/index.html` (안내 화면, 누르면 `hub.html`)
- 예제: `monitor.html`(실제 `/api/system` 값), `kiosk.html`, `farm.html`, `reflex.html`
- 예제 페이지는 120초 동안 입력이 없으면 안내 화면으로 돌아간다. 주소 뒤에 `?idle=60`처럼 붙이면
  그 탭에서 시간이 바뀌고 `?idle=0`이면 꺼진다.
- 외부 CDN, 웹 폰트를 쓰지 않으므로 인터넷이 없는 전시장에서도 동작한다.
- 보드 밖(PC 미리보기)에서는 `/api/system`이 없으므로 예시 값을 쓰고 화면에 미리보기라고 표시한다.
  미리보기: 이 폴더의 상위(`Main WebFlie`)에서 `python -m http.server` 후 `http://localhost:8000/expo/index.html`
