> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# M4 SDIO DMA allocation reboot

The supplied boot log reports `esp_dma_capable_malloc: Not enough heap memory`
followed by `assert failed: sdio_push_data_to_queue ... (pkt_rxbuff)`. The
allocator fails while ESP-Hosted copies a C6 SDIO receive packet into its
aligned DMA pool. The observed reset is a software assertion, not a PSRAM
memory-test failure. The log alone does not identify every allocation that
contributed to the shortage.

The current feature set also reduces the initial internal heap to about
373 KiB in the main region. UI objects, internal task stacks, SDIO buffers
and I2S DMA compete for that RAM even with 32 MiB of PSRAM present.

Changes:

- Generic `malloc`/`realloc` allocations prefer PSRAM for every nonzero size
  (`ALWAYSINTERNAL=0`). This is
  set before NVS/display/UI initialization as well as in SDK defaults.
  Drivers that explicitly request internal or DMA capabilities keep them.
- SDK's internal allocation reserve is 128 KiB instead of 32 KiB. This is
  protection from generic malloc, not extra RAM or a private SDIO-only pool.
  Internal task-stack and driver allocations may also consume that reserve.
- TLS allocations explicitly use external memory. eSpeak BSS and selected
  UI/list/conversation caches are mapped to external BSS. The previous map
  contains 89,855 bytes of internal eSpeak BSS; this is a baseline, not a measured
  saving from a new binary. Use `tools/audit_psram.py` after rebuilding.
- `cmake/memory_policy.cmake` updates these memory settings in an existing
  `sdkconfig` before `project()` reads it. Other user settings are preserved.
  Fresh projects use matching `sdkconfig.defaults` values; staged builds copy
  the helper with the existing `m4.ps1` CMake sync.
- I2S TX/RX use six 240-frame descriptors again. Sample storage totals
  11,520 bytes instead of 30,720 bytes, saving 18.75 KiB before descriptor and
  alignment overhead. Radio's 512 KiB compressed and 256 KiB PCM rings remain
  in PSRAM. YouTube's 24kHz output-latency estimate is adjusted to 60ms.
- The downloaded ESP-Hosted component stays unchanged. A version-scoped
  generated `sdio_drv.c` retries the observed packet-allocation failure up to
  three times with 10ms sleeps, then logs/drops the remaining burst and returns
  `ESP_ERR_NO_MEM` instead of asserting or copying through a null pointer.
  TCP can retransmit lost Wi-Fi packets. An RPC/control packet lost under
  sustained pressure may still fail or time out; this guard is not additional
  capacity or a guarantee of successful network initialization.
- Boot logs internal free bytes, DMA free bytes, largest DMA block and PSRAM
  immediately before starting network services.

The version/source guards require ESP-Hosted 1.4.1 and reject an unexpected
allocation site, rather than silently generating a partial vendor patch.
The packet-allocation guard addresses the assertion in the supplied log; it
does not replace every allocation/assertion in the transport library.

User-side rebuild from an ESP-IDF terminal in this project folder:

```powershell
idf.py -p COM8 reconfigure build flash monitor
```

Look for `M4: migrated saved configuration...` when an old configuration is
present, `M4: Hosted SDIO packet DMA allocation guard enabled`, and the runtime
`Before Wi-Fi: internal=..., DMA=..., largest DMA=..., PSRAM=...` line.
Then test Wi-Fi, Groove Salad MP3 128k, STT/TTS and video playback. If pressure
persists, retain the complete boot log and any `M4 SDIO RX DMA exhausted` or
`M4_RADIO` messages. Flash erasure is not required for these memory changes.

Host validation: `node tools/test_dma_memory.cjs` runs CMake fixtures with
`LANGUAGES NONE` (no compiler), validates saved-config migration/idempotence,
unchanged registry sources and other settings, and executes the generated
allocation guard with immediate success, transient failure and permanent
failure mocks. Other audio/radio/voice/video host checks also pass. They do
not measure physical DMA heap margins or demonstrate reboot-free board use.
Repository rules prohibit agent-run firmware compilation and flash.

Current optimization rules: [AGENTS.md](../AGENTS.md). Detailed host, simulator
and board procedures: [OPTIMIZATION_AND_VALIDATION.md](OPTIMIZATION_AND_VALIDATION.md).
