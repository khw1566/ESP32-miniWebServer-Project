# S3 / C3 IPv6 자동 구성 수정

## 확인된 문제

- C3 sdkconfig에서 CONFIG_LWIP_IPV6_AUTOCONFIG가 꺼져 있었고 sdkconfig.defaults에도 설정이 없었습니다.
- STA_CONNECTED 처리에서 링크로컬 생성을 기본 ESP-NETIF 연결 처리보다 먼저 요청할 수 있었습니다.
- 재시도는 링크로컬만 다시 만들고 Router Solicitation을 재시작하지 않았습니다.

## 수정

- 두 보드의 현재 sdkconfig와 defaults에 IPv6, SLAAC, Neighbor Discovery를 활성화했습니다. IPv6 주소 슬롯은 5개입니다.
- STA 연결 이벤트 후 별도 이벤트로 IPv6 시작을 처리해 기본 인터페이스 설정을 먼저 완료합니다.
- IPv6 처리는 esp_netif_tcpip_exec를 통해 TCP/IP 태스크에서 수행합니다. SLAAC를 켜고 필요한 경우에만 링크로컬을 생성하며 nd6_restart_netif로 공유기 응답을 다시 요청합니다.
- 글로벌/링크로컬/사설 주소를 로그에서 구분합니다. 연결이 끊긴 동안 이전 주소를 노출하지 않습니다.
- 관리자 QR 탭에 연결 없음, 자동 구성 비활성화, 링크로컬 대기, 글로벌 프리픽스 대기, 글로벌 주소 준비 상태를 표시합니다.
- Wi-Fi 성공 후에도 재연결 요청 상태를 유지합니다.

## 사용자 적용 및 확인

각 보드의 프로젝트에서 펌웨어를 빌드하고 업로드하세요. 에이전트는 빌드·플래시 및 실기기 시험을 실행하지 않았습니다.

정상 시작 로그: [IPv6] SLAAC enabled; Router Solicitation scheduled on STA
재시도 로그: [IPv6] Router Solicitation restarted; waiting for router advertisement and global prefix
실제 글로벌 발급 로그: [EVT] STA IPv6 assigned: [주소], type=global

FE80 주소만 나오면 외부 인터넷용 주소가 아닙니다. 공유기의 LAN에서 글로벌 /64 프리픽스를 SLAAC Router Advertisement로 제공해야 합니다. 통신사 IPv6 지원과 공유기 IPv6 설정을 확인하세요. 이 펌웨어의 lwIP는 DHCPv6 stateful 주소 할당을 지원하지 않으므로 DHCPv6 전용 LAN에서는 자동 글로벌 주소를 받을 수 없습니다.

주소 발급과 외부 HTTP 접속 허용은 별도입니다. 외부 접속이 필요하면 해당 보드의 TCP 80에 대한 공유기 IPv6 방화벽 규칙을 확인하세요.

공식 자료: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/lwip.html
