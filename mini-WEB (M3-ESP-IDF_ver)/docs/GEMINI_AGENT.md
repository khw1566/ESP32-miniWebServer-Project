# Gemini device agent

The AI Agent home icon opens a Korean assistant with an exact two-model selector:

- `gemini-3.1-flash-lite`
- `gemini-3.5-flash-lite` (initial UI selection)

Model identifiers and function-call support were checked against official Google
documentation on 2026-10-13. A request uses exactly the selected model; there is
no silent fallback to a different model. Access still depends on the API key,
project permissions and quota. No live or paid request was made by the agent.

## Hardcoded key setup

`gen-lang-client-0210670991` is a Google Cloud project ID, not an API key. It is
recorded only as project metadata in `main/include/m3_gemini_config.h`. It is
never used as the authentication header.

In `main/include/m3_gemini_private.h`, set:

```c
#define M3_GEMINI_API_KEY "YOUR_GEMINI_KEY"
#define M3_OPENAI_API_KEY "YOUR_OPENAI_KEY"
#define M3_GOOGLE_TTS_API_KEY "YOUR_GOOGLE_CLOUD_TTS_KEY"
```

The user-supplied keys are present in the local private header. Their values
are not included in this document or the public example. The private header is
excluded from Git; `m3_gemini_private.example.h` is the empty public template.
Keys are compiled into the firmware as requested. Source staging copies this
header with `main`, so the setup also works with `m3.ps1`. A fresh clone needs
its own private header. Missing keys disable their cloud features.

Use the usual user-run build and flash command after editing the header:

```powershell
idf.py -p COM8 reconfigure build flash monitor
```

## Using the app

Connect Wi-Fi, open AI Agent, select a model, enter Korean text and select Send.
Voice input beneath the prompt uses the existing microphone/STT setup; review
the recognized text before sending it to Gemini. Default STT uses the hardcoded OpenAI key and `gpt-4o-mini-transcribe`.
Settings also offers Gemini 3.1/3.5 transcription with the Gemini key.

Examples:

- “현재 메모리와 와이파이 상태를 알려줘.”
- “음량을 30으로 바꿔줘.”
- “파일 앱을 열어줘.”
- “오디오를 정지해줘.”
- “안녕하세요라고 말해줘.”

The model can call five bounded device functions: status snapshot, volume
0–150, app navigation (including Music), audio cancellation, and short Korean TTS using the provider selected in the TTS app. Unknown
tools and invalid arguments do not execute actions. Function results identify
queued operations as requested/queued rather than claiming completion.
UI navigation is passed to the LVGL task; the network task does not call LVGL.

The task panel shows agent activity while another app is open. Home navigation
does not cancel the agent. Cancel stops subsequent network work and tool
dispatch; an action already started is not undone. Clear conversation also
discards text history. Changing models starts a fresh history. History contains
the previous two successful text exchanges; opaque thought signatures and
function-call IDs are preserved within the current tool round trip.

A request is bounded to three model round trips and six device tool calls, a
32KiB response per round, and a 90-second processing deadline. The UI displays
tool names and the final answer. HTTPS uses the existing exclusive memory
gate. Stop HTTPS radio or an active voice upload before querying Gemini; the
AI app includes a direct Stop audio button for this purpose.

Read reply uses a UTF-8-safe prefix of at most 384 bytes with the selected
TTS provider. `speak_text` stages one validated sentence per agent request.
It is queued only after a successful final model answer and TLS lease release;
cancellation or a failed model round discards staged speech. The displayed tool
result reports staged speech, not completed playback. Actual TTS errors appear
in the audio status/task card.

## Speech providers

- Default STT: OpenAI `gpt-4o-mini-transcribe`, Korean multipart WAV upload.
- Optional STT: Gemini 3.1 / 3.5 Flash-Lite audio-input transcription, using the
  Gemini key. The result is reviewed before it is applied or sent as a prompt.
- Default TTS: OpenAI `gpt-4o-mini-tts`, voice `marin`, Korean instructions,
  raw 24kHz signed PCM16. Audio is downloaded to PSRAM before playback,
  converted from mono to the existing stereo codec path in 480-frame blocks.
- Optional TTS: Google Cloud `ko-KR-Chirp3-HD-Aoede`, 24kHz LINEAR16 WAV.
  Requires Cloud TTS API enablement, an accepted key and billing/quota.
- Offline TTS: existing Korean eSpeak NG. Its speed slider applies only to this
  engine. Cloud-provider failure is shown explicitly; there is no silent switch.

TTS provider selection also controls agent speech/read reply. The settings
are session choices. Responses are bounded to 2MiB for TTS and 16KiB for Gemini
transcription, with a 90-second cloud-audio deadline, certificate verification,
no redirects and cancellation checks. Large buffers use PSRAM; the configured
SPIRAM malloc policy also serves large cJSON allocations. A long synthesized
clip over the cap is rejected. This is record-then-recognize and
buffer-then-play, not a full-duplex streaming voice conversation.

No live API request has been made, and the supplied credentials' permissions,
quota and billing have not been verified. Actual board playback and recognition
remain user-side checks. Voices are disclosed in the UI as AI-generated.

Official sources:

- [Gemini 3.1 Flash-Lite](https://ai.google.dev/gemini-api/docs/models/gemini-3.1-flash-lite)
- [Gemini 3.5 Flash-Lite](https://ai.google.dev/gemini-api/docs/models/gemini-3.5-flash-lite)
- [API keys and projects](https://ai.google.dev/gemini-api/docs/api-key)
- [Function calling](https://ai.google.dev/gemini-api/docs/function-calling)
- [Audio understanding and transcription](https://ai.google.dev/gemini-api/docs/generate-content/audio)
- [OpenAI text to speech](https://developers.openai.com/api/docs/guides/text-to-speech)
- [OpenAI create transcription](https://developers.openai.com/api/reference/resources/audio/subresources/transcriptions/methods/create)
- [Chirp 3 HD supported languages](https://docs.cloud.google.com/text-to-speech/docs/chirp3-hd?hl=ko)

Host validation: `node tools/test_gemini_agent.cjs`,
`node tools/test_voice_input.cjs`, `node tools/test_cloud_speech.cjs`. Firmware compilation,
physical display interaction, real authentication, latency and tool execution
on the board remain user-side checks under the repository build rules.
