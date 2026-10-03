# C3 AI 웹 관리 에이전트

관리자 메뉴의 AI Studio(/ai)에서 Gemini API 키와 보드 관리자 비밀번호를 입력하고 '보드 도구 연결 확인'을 누릅니다.

## 지원 도구

- list_files: SPIFFS 웹파일 목록, 크기, 편집 가능 여부
- read_file: 텍스트 파일 내용과 현재 버전 조회
- write_file: 새 파일 추가 또는 읽은 버전과 일치하는 기존 파일 수정
- set_home: 존재하는 HTML 파일을 루트 첫 화면으로 지정
- get_settings: 첫 화면 설정과 파일 크기 제한 조회

예시: 'cafe_menu.html을 읽고 제목을 오늘의 메뉴로 수정해줘', 'welcome.html을 만들어 저장하고 메인 화면으로 지정해줘', '파일별 크기를 표와 막대 차트로 보여줘'.

## 동작과 범위

Gemini 함수 호출을 브라우저가 보드의 POST /api/agent로 전달하고 결과를 모델에 돌려줍니다.
MCP와 유사한 도구 브리지이며 표준 MCP 서버/JSON-RPC 전송 계층을 구현한 것은 아닙니다.
관리자 인증은 각 도구 요청에서 검사하며 비밀번호는 보드 도구 요청 JSON 본문에만 보냅니다. HTTP 헤더에는 넣지 않습니다.
Gemini API 키는 브라우저 메모리에만 두고 Gemini 요청 헤더에 사용합니다.
도구로 읽은 파일 내용은 AI 제공자에 전달됩니다. 인증 정보는 모델 입력에 포함하지 않습니다.

편집 대상은 업로드된 SPIFFS 텍스트 파일입니다. 펌웨어에 내장된 관리자·AI 화면 C 코드를 보드에서 수정하지는 않습니다.
파일명은 영문/숫자/밑줄/하이픈/점으로 구성된 최대 24자, 폴더 없는 이름을 사용합니다.
지원 확장자는 html, htm, css, js, json, txt, md이며 파일당 24 KiB, JSON 요청은 48 KiB까지입니다.
기존 파일은 read_file의 version을 expected_version으로 전달해야 합니다.
새 파일은 expected_version을 new로 지정합니다. 파일이 이미 있으면 충돌로 거부합니다.
새 내용을 임시 파일에 기록한 뒤 기존 파일을 .aibak으로 옮겨 교체합니다.
교체 실패 시 원본 복원을 시도하고, 재부팅 시 남은 백업을 확인해 복구합니다.
SPIFFS에는 원본과 새 파일을 함께 보관할 여유 공간이 필요합니다.
한 요청은 최대 8단계 모델 호출과 16회 도구 실행으로 제한합니다.
전송 실패 시 이미 완료된 변경은 취소되지 않으므로 실행 기록과 파일 조회로 확인하세요.
메인 화면 설정은 재부팅 후 유지됩니다. SoftAP의 루트는 Wi-Fi 설정 페이지입니다.

## 마크다운과 차트

제목, 굵게, 기울임, 취소선, 목록, 인용, 표, 링크, 코드블록을 표시합니다.
원시 HTML은 채팅에서 실행하지 않습니다. HTML 코드 미리보기는 sandbox iframe을 사용합니다.
전체 CommonMark 확장을 구현한 것은 아니며 Mermaid 대신 chart JSON을 지원합니다.

```chart
{"type":"bar","title":"파일 크기 (KB)","labels":["index.html","cafe_menu.html"],"values":[12,9]}
```

type은 bar, line, pie이며 labels/values 길이가 같고 최대 24개입니다.
값은 0 이상이어야 하며 원형 차트는 합계가 0보다 커야 합니다.
외부 CDN 없이 SVG와 데이터 표로 표시합니다.

## 소스 관리와 검증

- main/ai_studio.html: 화면·스타일
- main/ai_studio.js: 모델 호출·도구 루프·마크다운 및 차트 렌더러
- main/web_agent_tools.inc: web_server.c에서 포함하는 보드 도구 구현
- main/web_page_ai.c: HTML/JS에서 생성하는 펌웨어용 내장 문자열

소스 수정 후 C3 프로젝트 폴더에서 내장 문자열을 갱신합니다. 아래 명령은 펌웨어를 컴파일하지 않습니다.

```text
node tools/embed_ai_studio.cjs
```


펌웨어 빌드·업로드 및 HTML 응답의 보드 바로 적용은 파일 이름을 입력하면 SPIFFS에 저장하고 메인 화면 지정 여부를 묻습니다. 실제 보드 저장/복구·Gemini API 연동은 별도로 확인해야 합니다.

