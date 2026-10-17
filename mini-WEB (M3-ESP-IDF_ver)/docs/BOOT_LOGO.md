# UC 도트와 청록색 제품 로고

상단의 기존 UC 겹침 도트 문양과 물결 애니메이션은 그대로 유지합니다.
최근 제작한 M-arrow는 `MiniWEB 시제품` 문구 오른쪽에 별도의 선명한 이미지로
배치합니다. 도트화하거나 확대 애니메이션을 적용하지 않습니다. 학교명·행사명,
부팅 단계, 터치 건너뛰기와 종료·메모리 해제 조건도 유지합니다.

M-arrow 원본은 `assets/branding/logo-mark.svg`이며 제품 웹 패키지의
`releases/MiniWeb-Product-Lineup-20261016/assets/logo-mark.svg`에서 가져왔습니다.
사각 배경을 제외한 M·상승 화살표의 벡터 윤곽을 56×56으로 안티앨리어싱하여
기기 UI의 기본 청록색 `#00E5EF`로 적용합니다. 투명 ARGB8888 정적 이미지로,
실행 중 크기 변환 없이 표시합니다. 제품 문구도 같은 청록색입니다.

상단 도트는 원래 33×33 UC 테이블 1,089바이트를 사용합니다. 오른쪽 로고는
12,544바이트의 정적 이미지 데이터를 펌웨어에 추가합니다. 기존 400×420
RGB565 PSRAM 캔버스 336,000바이트는 그대로이며 SD 파일이나 추가 작업은
필요하지 않습니다. 문구 등장 애니메이션이 끝난 뒤 제품 로고를 페이드인하여
확대 중인 글자와 겹치지 않도록 합니다.

두 로고 재생성·검사(컴파일하지 않음):

```powershell
python -X utf8 tools/generate_boot_splash.py --logo-only
node tools/test_boot_splash.cjs
python -X utf8 tools/preview_boot_splash.py
```

인자 없는 생성도 기본적으로 UC 도트와 청록색 M-arrow를 만듭니다.
`--logo-only`는 기존 글꼴을 유지하며 두 로고 데이터만 갱신합니다.

`results/boot_splash/uc-product-ready.png`와 `uc-product-reveal.png`는 실제
C의 도트 테이블·캔버스 계산과 제품 이미지 데이터를 사용한 Pillow 참고
이미지입니다. 글자는 같은 Nanum Gothic 원본으로 배치를 표현하며 LVGL의
실제 렌더링이나 보드 캡처는 아닙니다. 호스트 검사에서 UC 도트 일치,
제품 이미지 데이터와 투명도·안티앨리어싱, 캔버스 경계, 글리프, 부팅 단계와
버퍼 해제를 확인했습니다. 펌웨어 컴파일·실기 부팅은 사용자 확인이 필요합니다.
