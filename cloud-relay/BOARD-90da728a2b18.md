# C3 도메인 연결: 90da728a2b18

- 제품: `c3`, 주소용 버전: `1-2`
- 로컬 주소: `http://c3-v1-2-90da728a2b18.local/`
- 확인한 IP: `http://100.105.2.67/` (DHCP 변경 가능)
- 연결할 외부 주소: `https://c3-90da728a2b18.audreaming.com/`
- Cloudflare Worker: `mini-web-relay`
- Cloudflare 계정 ID: `60bbc5ec2b671498c5f12537cd46e65a`

## 2026-10-23 준비 상태

실제 `/api/system`에서 제품과 시리얼을 확인했습니다. 당시 상태는
`cloud_status: factory_missing`으로, 보드의 연결 키 등록이 필요합니다.

`provisioned/`에 C3 기기별 연결 키와 서버 등록용 SHA-256 해시를 생성했고,
`wrangler.toml`에 외부 주소를 추가했습니다. 다시 provision을 실행해서 키를
교체하지 마세요. 중계 서버의 병합 충돌 표시를 제거하고 기존 스트리밍 기능을
유지했습니다. `node --test test/*.test.mjs`는 30개 모두 통과했습니다.

서버 `DEVICE_REGISTRY` 갱신 시 자동 승인 심사에서 명시적 승인이 필요하다고
차단했습니다. 서버 인증정보 갱신, 도메인 배포, 보드 키 저장은 아직 완료하지
않았습니다. 펌웨어 빌드와 플래시는 실행하지 않았습니다.

## 승인 후 연결 순서

아래 명령은 이 폴더에서 실행합니다. 첫 명령은 기존 기기 등록과 C3 인증 해시를
기존 Cloudflare Worker의 비밀값으로 등록합니다. 원본 연결 키는 서버에 보내지
않습니다.

```powershell
Get-Content -Raw -LiteralPath provisioned/registry.json | .\node_modules\.bin\wrangler.cmd secret put DEVICE_REGISTRY --name mini-web-relay
.\node_modules\.bin\wrangler.cmd deploy
```

그 다음 로컬 보드에 키를 저장합니다. 관리자 비밀번호는 스크립트의 보안 입력창에
입력합니다. 기존 Wi-Fi와 관리자 설정을 유지합니다.

```powershell
./Register-Board.ps1 -BoardUrl http://100.105.2.67 -Serial 90da728a2b18 -Product c3
```

보드를 재부팅한 뒤 `/api/system`의 `cloud_status`가 `online`인지 확인합니다.
외부 주소에서 실제 보드의 웹 파일을 열어 연결 완료를 확인합니다.
