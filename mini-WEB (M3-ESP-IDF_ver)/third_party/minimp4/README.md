# minimp4

`minimp4.h` is an unmodified copy from
https://github.com/lieff/minimp4/blob/2a3014a7753cfcba4442adeb469431967238fe6a/minimp4.h
at commit `2a3014a7753cfcba4442adeb469431967238fe6a`.

Upstream distributes the header under CC0; the license notice is included at
the beginning of the header. M3 only calls its demuxer. The bounded PSRAM allocator,
SD reader, metadata validation and supported-codec policy live in
`main/m3_mp4_demux.c`.
