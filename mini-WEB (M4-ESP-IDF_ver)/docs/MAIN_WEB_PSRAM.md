> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# SD 웹 파일의 부팅 시 PSRAM 적재

SD카드 루트의 폴더명은 사용자가 지정한 철자 그대로 **`Main WebFlie`**다.
부팅 서비스 작업에서 SD 초기화 직후, Wi-Fi·HTTP·클라우드 중계를 시작하기
전에 이 폴더와 하위 폴더의 웹 파일을 PSRAM으로 읽는다. 적재한 파일의
HTTP 요청과 공개 중계 요청은 PSRAM 데이터를 사용하며 SD에 다시 접근하지 않는다.
캐시는 재부팅까지 유지된다. SD 파일을 편집한 뒤에는 보드를 재부팅한다.

## 기본 파일 준비

프로젝트의 `sd_assets/Main WebFlie`에 현재 기본 서비스 화면 6개를 내보냈다.
기본 합계는 205,872바이트(약 201 KiB)다. 프로젝트 폴더에서 아래 명령으로
SD카드에 준비할 수 있다. `E:/`는 실제 SD 드라이브로 바꾼다.

```powershell
python -X utf8 tools/export_main_web.py --sd E:/
```

이 명령은 이미 존재하는 사용자 파일을 덮어쓰지 않는다. 소스 기본값으로
명시적으로 교체할 때만 `--replace`를 붙인다. 별도 로컬 폴더로 내보낼 때는
`--output "경로/Main WebFlie"`를 사용한다. 사용자가 편집한 파일은 SD의
`Main WebFlie` 안에서 관리하며 CSS·JS·이미지·글꼴을 하위 폴더에 둘 수 있다.
경로의 숨김 요소·상위 경로 이동·제어문자·URL 예약 문자는 허용하지 않는다.
상대 URL 기준은 폴더 자체가 아닌 웹 루트 `/`다.

| SD 폴더 안의 파일 | 화면 주소 |
| --- | --- |
| `index.html` | `/`, `/index.html` |
| `admin.html` | `/admin` |
| `help.html` | `/help` |
| `setup.html` | `/setup`, 설정 AP 첫 화면 |
| `ai.html` | `/ai` |
| `ai_studio.html` | `/api/ai/studio`, 기존 인증을 통과한 뒤 제공 |
| `assets/style.css` 등 | `/assets/style.css` 등 |

API 응답은 캐시하지 않는다. AI 스튜디오 인증을 유지하며 공개 중계는
관리·설정·AI 화면을 전달하지 않는다. API 키는 기존 SD `.private/api_keys.json`
정책을 사용하며 웹 폴더에 복사하지 않는다. 기존 `/media/`의 동영상·음성은
별도의 SD 스트리밍 경로를 유지한다.

## 용량·오류 처리

기본 PSRAM 예산은 메타데이터를 포함해 8 MiB, 파일 개수는 최대 512개다.
`menuconfig → Mini WebServer M4`에서 `M4_WEB_ASSET_LIMIT_KB`(256~16384 KiB),
`M4_WEB_ASSET_MAX_FILES`(8~4096개)를 조절할 수 있다. HTML/CSS/JS/JSON/SVG,
이미지·글꼴·WASM·일반 웹 미디어 형식을 지원하며 알 수 없는 확장자와 숨김
파일은 제외한다. 경로는 최대 191바이트, 공개 중계 경로는 기존 180바이트·
ASCII·확장자 정책을 유지한다.

일부 파일만 보이는 상태를 막기 위해 적재가 전부 성공한 뒤 한 번에 공개한다.
용량 초과·메모리 부족·읽기 오류가 나면 부분 적재 메모리를 모두 해제하고
기존 내장/Flash 화면으로 복귀한다. 폴더가 없거나 SD를 읽지 못해도 기존
설정·관리 화면은 사용할 수 있다. 성공 시 로그에 다음 형식이 나타난다.

```text
WEB_PSRAM: Main WebFlie: 6 files, 205872 bytes in PSRAM
```

단일 HTTP byte range를 지원한다. 공개 중계의 기존 8 MiB 전송 제한도
그대로 적용한다. 캐시 적재 뒤 SD를 제거해도 적재된 웹 파일 데이터는 PSRAM에
남지만 다른 SD 기능은 사용할 수 없다. 실행 중 캐시를 교체하거나 해제하지 않는다.

## 검증과 적용

`node tools/test_web_psram.cjs`는 실제 C 적재·조회·범위 처리·응답 함수의
바이너리/UTF-8 보존, 메모리/SD 실패 복구, SD 없는 조회, 인증 경계와 부팅 순서를
호스트 모의 객체로 검사한다. 기존 gzip·PSRAM·메모리 검사를 함께 실행한다.
호스트 검사는 펌웨어 컴파일·실기 부팅 성공의 증거가 아니다.

프로젝트 지침에 따라 사용자가 펌웨어를 빌드·플래시한다.

```powershell
.\m4.ps1 -Action Build
.\m4.ps1 -Action Flash -Port COM8
```

`COM8`은 예시다. 실물에서는 성공 로그, PSRAM 여유, 로컬/공개 웹 접속,
SD 편집 후 재부팅 반영, 인증, 미디어 range 요청과 동시 재생을 확인한다.
