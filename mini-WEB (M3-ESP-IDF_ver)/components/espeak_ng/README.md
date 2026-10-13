# eSpeak-NG local Korean TTS component

This component vendors the eSpeak-NG 1.52.0 C sources from upstream commit
`4870adfa25b1a32b4361592f1be8a40337c58d6c`. Optional audio, async, MBROLA,
Klatt, Sonic, and SpeechPlayer backends are disabled. The M3 firmware uses
synchronous PCM synthesis with the `ko` voice, streamed to its existing ES8311
audio worker. No operating-system audio backend is needed for callback output.

The port foundation is from inlanger/esp32-p4-inflect-tts commit
`6d18e17bc6691ba66b7ce04b9a121f25c9c2ee2b`. M3 adds the compatibility include
directory for Unicode character classification and changes the data path to
`/tts/espeak-ng-data`. The matching phondata format is `0x014801`, 22050 Hz.
The Korean/shared runtime data is from pschatzmann/arduino-espeak-ng commit
`93b2651d324874e1e00077af008d8ab83ab32e73`. Original files and SHA-256 hashes
are retained under `tts_data`; generated flash arrays are exposed through a
read-only VFS in `main/m3_tts_data.c`. This is a new integration; host policy
tests do not verify firmware compilation, audible Korean quality, or stack use.
Upstream GPL-3.0 and Unicode data licenses are retained as `COPYING` and
`COPYING.UCD`.


ESP-IDF 5.5 / GCC 14 compatibility: byte classification arguments are cast to
unsigned char before newlib ctype macros. Reported unbounded number/path
formatting uses snprintf, and number lookup keys have room for signed integer
text and ordinal prefixes. Only format-truncation warnings are nonfatal for
this vendored component; warnings remain enabled and application policy is
unchanged. Dictionary/phoneme compiler APIs are not called by the M3 TTS app.
