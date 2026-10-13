#include "m3_audio_apps.h"
#include "espeak-ng/speak_lib.h"
#include "espeak-ng/espeak_ng.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const m3_audio_output_t *sink;
static bool write_failed;
/* Synchronous eSpeak callbacks run on the sole audio worker. */
static int speech_pcm(short *samples, int count, espeak_EVENT *events) {
    (void)events;
    if (sink->cancelled()) return 1;
    if (!samples || count <= 0) return 0;
    int16_t stereo[480 * 2];
    for (int offset = 0; offset < count; offset += 480) {
        int frames = count - offset; if (frames > 480) frames = 480;
        for (int i = 0; i < frames; ++i) stereo[i * 2] = stereo[i * 2 + 1] = samples[offset + i];
        if (!sink->write(stereo, (unsigned)frames * 4)) { write_failed = true; return 1; }
        taskYIELD();
    }
    return 0;
}
bool m3_tts_text_valid(const char *text) {
    if (!text || !*text || strlen(text) > 384) return false;
    bool content = false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p < 0x80) {
            if ((*p < 32 && *p != '\n' && *p != '\t') || *p == 127) return false;
            if (*p > 32) content = true;
            ++p; continue;
        }
        /* Korean/ASCII/Unicode BMP text; reject malformed and overlong UTF-8. */
        unsigned first = *p++, value, count;
        if (first >= 0xc2 && first <= 0xdf) { value = first & 31; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 15; count = 2; }
        else return false;
        for (unsigned i = 0; i < count; ++i) {
            if ((*p & 0xc0) != 0x80) return false;
            value = (value << 6) | (*p++ & 63);
        }
        if ((count == 2 && value < 0x800) || (value >= 0xd800 && value <= 0xdfff)) return false;
        content = true;
    }
    return content;
}
void m3_tts_run(const char *text, unsigned speed, const m3_audio_output_t *out) {
    out->message("Korean TTS initializing locally...", true, 0);
    if (m3_tts_data_mount() != 0) { out->message("TTS voice data mount failed", false, 0); return; }
    /* Use the status-returning API: legacy espeak_Initialize can mask load errors. */
    espeak_ng_ERROR_CONTEXT context = NULL;
    espeak_ng_InitializePath("/tts");
    espeak_ng_STATUS result = espeak_ng_Initialize(&context);
    espeak_ng_ClearErrorContext(&context);
    if (result != ENS_OK) { out->message("TTS engine/data initialization failed", false, 0); espeak_Terminate(); return; }
    bool ok = espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 40, NULL) == ENS_OK &&
              espeak_SetVoiceByName("ko") == EE_OK;
    if (!ok) { out->message("Korean TTS voice unavailable", false, 0); espeak_Terminate(); return; }
    int rate = espeak_ng_GetSampleRate();
    sink = out; write_failed = false;
    espeak_SetSynthCallback(speech_pcm);
    espeak_SetParameter(espeakRATE, speed < 100 ? 100 : speed > 260 ? 260 : speed, 0);
    espeak_SetParameter(espeakVOLUME, 80, 0);
    if (out->cancelled() || !out->begin((uint32_t)rate)) ok = false;
    if (ok) {
        out->message("Speaking Korean / offline eSpeak NG", true, 0);
        result = espeak_ng_Synthesize(text, strlen(text) + 1, 0, POS_CHARACTER, 0,
                                     espeakCHARS_UTF8 | espeakENDPAUSE, NULL, NULL);
        ok = result == ENS_OK && !write_failed && !out->cancelled();
        if (ok) ok = out->drain();
    }
    espeak_Terminate(); sink = NULL;
    out->message(ok ? "Korean TTS complete / offline" : out->cancelled() ? "TTS stopped" : "TTS synthesis/output failed", false, ok ? 100 : 0);
}
