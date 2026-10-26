> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# M4 internet radio

The pipeline now has separate HTTP receive, decoder and I2S stages:

- HTTP receiver: 512 KiB compressed-audio ring in PSRAM, priority 5 / core 0.
- Decoder: MP3/AAC to stereo PCM16, priority 3 / core 0, a 256 KiB PCM ring.
- Audio worker: sole ES8311/I2S owner, priority 4 / core 0, 480-frame stereo
  output blocks. Its existing role in WAV, voice capture and TTS is preserved.

At startup, the compressed reserve is six seconds using the `icy-br` header,
clamped to 32–256 KiB. Without that header it uses 96 KiB (about 6.1 seconds
at 128 kbit/s). Groove Salad MP3 128k needs 96,000 bytes when the header is
present. The PCM stage then prepares 750ms before opening the speaker.
These are buffer durations, not measured startup times. PCM capacity is
about 1.49 seconds at 44.1kHz stereo, independently of stream bitrate.
Initial compressed buffering fails after 30 seconds rather than waiting
indefinitely for a stream that cannot supply the reserve.

An empty compressed ring no longer triggers rebuffering. Playback continues
from PCM while receiving/decoding catches up. A PCM read allows up to 20ms for
a ready decoder before reporting an underrun. Only PCM starvation triggers
silence and refilling, targeting 1500ms capped at 75% of the PCM ring. It does
not require another complete compressed prebuffer. EOF drains remaining PCM.

HTTP connection/headers keep a 3-second timeout. Body reads use 250ms so
partial data is delivered promptly; transient read stalls are retried up to
20 seconds. Fatal connection errors and Wi-Fi loss have distinct messages.
Radio attempts to disable modem power-save for the session and restores the
previous setting after stopping. An unsupported ESP-Hosted power-save RPC
is non-fatal. I2S uses six 240-frame DMA descriptors (about 33ms at 44.1kHz), preserving
internal RAM for C6 SDIO. Radio jitter stays in the PSRAM rings. See
[DMA memory correction](DMA_MEMORY.md).

Stop sets both producer stop flags, joins both tasks, and then releases rings,
decoder, HTTP and the TLS lease. Partial sends retain their offset; no audio
bytes are deliberately dropped. Main PSRAM allocation is about 872 KiB plus
codec/HTTP allocations; the extra decoder stack is 24 KiB of internal RAM.

The screen shows `net ... KiB / PCM ... ms / gaps ...`. Every five seconds,
`M4_RADIO` logs received compressed B/s, both fill levels, and PCM underruns.
An underrun logs the compressed fill immediately. For a 128 kbit/s stream,
16,000 audio B/s is the consumption rate. Persistently lower receive rate
while the compressed ring is empty indicates an input bottleneck; a full ring
can intentionally lower receive rate through backpressure. Empty PCM with
compressed data available points toward decoder/scheduling delay. These
counters provide diagnostics; they are not a measured hardware diagnosis.

Choose a station, then Play. Stop current playback before changing stations.
MP3/AAC codec selection follows the chosen preset; manual URLs remain editable.
HTTPS redirects are followed at most three times. HTTP URLs must be direct.
Playlists, HLS, DRM, and arbitrary web pages are not supported.

Seven presets / six stations, verified against official listings on 2026-10-13:

- [SomaFM Groove Salad](https://somafm.com/groovesalad/directstreamlinks.html): MP3 128k and AAC 64k
- [SomaFM Drone Zone](https://somafm.com/dronezone/directstreamlinks.html): MP3 128k
- [SomaFM Secret Agent](https://somafm.com/secretagent/directstreamlinks.html): MP3 128k
- [Radio Swiss Jazz](https://www.radioswissjazz.ch/de/empfang/internet): MP3 128k
- [Radio Swiss Classic](https://www.radioswissclassic.ch/en/reception/internet): MP3 128k
- [Radio Swiss Pop](https://www.radioswisspop.ch/en/reception/internet): MP3 128k

SomaFM lists its links for personal listening. Direct server addresses can
change. Official listing verification does not establish live stream availability
or audible playback on this board.

Host validation: `node tools/test_radio_buffer.cjs` checks buffer admission,
adaptive compressed reserves, PCM refill/EOF policies, a two-ring 5-second
receive-gap / 300ms decoder-delay simulation, and resumption after a longer
outage. It executes the C receiver/decoder task bodies with mocks, including
partial writes, cached-frame draining, mono-to-stereo conversion and stop under
backpressure, and checks cleanup ordering and station selection. It does not
test the physical SDIO link, scheduler deadlines, decoder throughput, or I2S.
The agent has not compiled or flashed this firmware under the repository rules.
