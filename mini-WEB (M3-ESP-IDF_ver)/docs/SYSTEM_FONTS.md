# 시스템 글꼴과 텍스트 표시

설정 앱의 **시스템 글꼴**에서 고딕, 굵은 고딕, 명조를 선택한다.
글자 크기는 16~24 px이며 선택 즉시 적용한다. **실제 출력 미리보기**는
이미지가 아니라 시스템과 같은 `m3_system_font()` 객체로 그리는 LVGL 텍스트다.
한글·영문·숫자를 선택한 크기로 표시하며 긴 샘플은 줄바꿈한다.

본문은 선택한 크기를 사용한다. 제목은 본문보다 4 px 크게 표시하며,
도움말과 작업 패널 같은 작은 영역은 최대 20 px로 제한한다.
입력란 높이도 글자 크기에 맞춰 조절한다. 시계·게임 숫자·기호 아이콘은
기존 전용 글꼴을 유지한다. 리더기 본문의 글꼴·크기는 리더 설정을 따른다.

선택은 기존 기기 설정을 변경하지 않는 별도 NVS 키 `m3_device/typography`에
저장한다. 저장은 서비스 작업에서 약 2초 간격으로 처리하며 실패하면 재시도한다.
기존 키가 없거나 형식이 잘못되면 고딕 16 px를 사용한다.

파일명·제목·버튼·상태 문구는 넘칠 때 `...`로 축약한다. 순환 스크롤은
사용하지 않는다. 도움말·대화·리더 본문은 줄바꿈하고 페이지 또는 세로
스크롤로 읽는다. 상단 작업 패널의 중지 버튼은 사각형 중지 아이콘을 사용하며
기존 중지 동작과 사용 가능 조건을 유지한다.

호스트 검사:

```powershell
node tools/test_system_fonts.cjs
node tools/test_preferences.cjs
node tools/test_device_settings.cjs
node tools/test_reader.cjs
node tools/test_service_controls.cjs
node tools/test_japanese.cjs
node tools/test_i18n.cjs
```

호스트 검사는 실제 C 함수 본문과 모의 NVS/LVGL을 사용한다. 새 네이티브
렌더링이나 보드 검증을 대신하지 않는다. 사용자 PC 시뮬레이터 빌드에는
한국어·영어의 글꼴 3종, 16/20/24 px와 중지 아이콘 시나리오 100~103이 포함된다.

```powershell
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m3_font_ko" -Lang 0
.\tools\ui_sim\build.ps1 -Out "$env:TEMP\m3_font_en" -Lang 1
.\m3.ps1 -Action Build
```

실물에서 설정 미리보기, 최대 크기의 입력란·상태바·작업 패널, 글꼴 변경 후
리더기 진입, 재부팅 후 복원과 긴 한글·영문 파일명의 축약을 확인한다.
