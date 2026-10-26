# M4 firmware rules

This project targets GUITION JC1060P470C (Mini WebServer M4).
Follow `../.agents/rules/project_rules.md`, `embedded-systems` and `espidf-verify`.
Firmware and native simulator compilation and flashing are user-run. Host
Python/Node checks and compiler-free CMake fixtures are allowed. Syntax-only
checks may reuse installed compile-database flags, without creating objects.

Preserve M3 and the ARD reference project. M4 is an independent source snapshot;
do not copy private headers, `.private`, sdkconfig, build outputs or credentials.
Keys remain SD-only in `.private/api_keys.json`, Wi-Fi in `.private/wifi.json`.

The physical panel is 1024x600 JD9165; default use is 600x1024 portrait.
App geometry remains 480x800 with uniform 1.25 scaling. Do not change media
buffer dimensions to the physical panel size. Verify input, scrolling and
overlays after layout changes. Panel V1/V2 and silicon revision are separate.
Never use `--force` to bypass an incompatible chip revision.

M4 battery telemetry is unknown: never enable the inherited M3 GPIO53 ADC
without board-specific circuit evidence. I2S/SD defaults are provisional and
must remain configurable. Report source/host, compilation, renderer and real
hardware evidence separately. See README.md and docs/PORTING_M4.md.

Large data and UI layers use PSRAM; keep DMA/control and sensitive stacks internal.
시뮬레이터 결과와 실기 검증을 구분한다.
