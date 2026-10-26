> M4 이식 참고 문서: M3 기능 문서를 계승했습니다. 보드 배선, 화면, 메모리와 전원 관련 값은 M4 README 및 docs/PORTING_M4.md를 우선합니다. 실기 검증은 별도입니다.

# Korean microphone input

The board's analog MSM481 microphone feeds the ES8311 ADC. The official
schematic connects its SDOUT net to ESP32-P4 GPIO48. MCLK13, BCLK12, LRCK10,
DAC data9 and PA11 are shared with the existing speaker path. Only the audio
worker opens/reads/closes the codec. The configuration now includes I2S RX
and ADC/DAC; the speaker amplifier is muted during recording.

1. Open Settings and choose the STT provider. OpenAI mini-transcribe is the
   default and uses the key compiled into the ignored private header. Gemini
   3.1/3.5 uses the separately configured Gemini key. The masked OpenAI STT
   override field changes the current session only; empty disables OpenAI STT
   until another key is entered or the board reboots. It does not alter TTS.
2. Connect Wi-Fi and stop radio, TTS, or WAV playback.
3. Tap the microphone button beside the SSID, Korean TTS text, or AI Agent
   message field. A bottom sheet opens and recording begins
   immediately and sends audio to the selected provider after up to 8 seconds.
4. **Done speaking** (Korean UI: 말하기 끝) ends recording early. **Cancel** or
   navigating Home discards the pending result. Cancellation during upload
   stops subsequent work; audio already transmitted cannot be recalled.
5. Review the result, then select **Use text** (텍스트 사용) to replace that
   field. Numeric fields accept only their allowed characters; oversized
   results are rejected rather than silently truncated. The keyboard remains
   available for correction and passwords, SSIDs, URLs, or IP addresses that
   speech recognition does not reproduce exactly.

The capture is 16kHz mono PCM16 WAV, at most 256,044 bytes. The populated
ES8311 slot is selected by block energy. Silence and clips shorter than
0.5 seconds are rejected. Cloud requests use the fixed HTTPS endpoint
`https://api.openai.com/v1/audio/transcriptions`, `model=gpt-4o-mini-transcribe`,
`language=ko`, and `response_format=json`, with multipart file upload.
Gemini instead sends the same WAV as inline base64 audio to the selected
Flash-Lite `generateContent` endpoint with a transcription-only prompt, no
device tools. The API account needs transcription access and available quota; ChatGPT app
subscription credentials are not used. No paid request was executed by the
agent. The existing HTTPS admission gate excludes concurrent radio/AI TLS.

Only explicit recording sends microphone audio. There is no background or
wake-word listener. Recorded audio and response buffers are cleared after use.
Request headers and response bodies are not logged. Keys supplied for this firmware are stored in the ignored local private
header; the public template contains empty macros. OpenAI STT can also be
overridden from the physical display for the current session.

Official references, checked 2026-10-13:

- [Create transcription](https://developers.openai.com/api/reference/resources/audio/subresources/transcriptions/methods/create)
- [Speech to text](https://developers.openai.com/api/docs/guides/speech-to-text)
- [GUITION schematic](https://github.com/guitionofficial/P4-series/tree/master/JC4880P443C_I_W/JC4880P443C_I_W/5-Schematic)

Physical microphone level, ADC channel alignment, speaker reopening, API
account access, live recognition, and long-run playback after capture still
need board testing. Repository rules prohibit agent-run build/flash.
