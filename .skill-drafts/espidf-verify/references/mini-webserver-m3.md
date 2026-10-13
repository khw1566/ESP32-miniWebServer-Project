# Mini WebServer M3 context

Use only when the workspace is `C:/Users/work/Desktop/Mini WebServer` or the
user explicitly selects its M3 firmware. Recheck versions and board profile.

- Firmware: `mini-WEB (M3-ESP-IDF_ver)`.
- Board: GUITION JC4880P443C_I_W / JC-ESP32P4-M3, ESP32-P4 revision 3.2,
  C6 radio, 480x800 display, 32 MiB PSRAM and 16 MiB flash.
- At installation: ESP-IDF 5.5.5 at `C:/esp/v5.5.5/esp-idf`, LVGL 9.2.2 in
  `managed_components/lvgl__lvgl`. Check actual headers before each new API.
- Read `.agents/rules/project_rules.md` and firmware `AGENTS.md`.
- Those rules prohibit agent-run firmware and native simulator compilation.
  A request to install this skill does not remove the prohibition.
- Credentials may exist in ignored local headers. Do not read them for general
  preflight, copy them into fixtures, publish them, or send them to doc servers.
- Preserve `main/m3_netif_guard.c` and wrapper extraction linker flags. A missing
  `__wrap_esp_netif_action_start` is an implementation/link integration defect.
- Preserve generated codec library path quoting. This project path contains
  spaces and parentheses; a split `-L` path can fail only at the link step.
- Preserve chip revision range compatible with 3.2. Do not bypass esptool's
  incompatible revision warning with `--force` as the normal solution.
- Internal DMA descriptors, RTOS control and sensitive-path stacks stay internal;
  display/media/voice/JSON data use bounded PSRAM allocation or the established
  `M3_PSRAM_BSS` policy. Flash OTA slots remain 4 MiB unless the user changes scope.

Read `docs/OPTIMIZATION_AND_VALIDATION.md` and the relevant feature document.
Choose existing checks for the change, such as `test_m3_integration.cjs`,
`test_touch_apps.cjs`, `test_i18n.cjs`, `test_japanese.cjs`,
`test_web_qr.cjs`, `test_psram_policy.cjs` and `audit_psram.py`.
Do not run every media test for an unrelated change. An old map/ELF is useful
as historical evidence only; it does not validate the current sources.

User-side firmware compilation from an ESP-IDF 5.5.5 environment:

```powershell
Set-Location -LiteralPath 'C:/Users/work/Desktop/Mini WebServer/mini-WEB (M3-ESP-IDF_ver)'
idf.py build
```

Do not append flash, monitor or fullclean without a need and authorization.
