#include "m4_azure_audio.h"
#include "m4_sd_settings.h"
#include "network_manager.h"
#include "https_memory.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { AZURE_RATE = 16000, AZURE_REPLY_BYTES = 4096, AZURE_PCM_LIMIT = 2 * 1024 * 1024 };
static const char *TAG = "M4_AZURE";

bool m4_azure_audio_ready(void) {
    const char *key = m4_sd_azure_key(), *region = m4_sd_azure_region();
    size_t length = strlen(key), region_length = strlen(region);
    if (length < 20 || length > 255 || !region_length || region_length > 31) return false;
    for (size_t i = 0; i < length; ++i) if ((unsigned char)key[i] < 33 || (unsigned char)key[i] > 126) return false;
    for (size_t i = 0; i < region_length; ++i)
        if (!((region[i] >= 'a' && region[i] <= 'z') || (region[i] >= '0' && region[i] <= '9'))) return false;
    return true;
}

static const char *status_error(int status) {
    return status == 401 || status == 403 ? "Azure key/region rejected" :
           status == 429 ? "Azure free quota/rate limit reached" :
           status == 400 ? "Azure request/audio format rejected" : "Azure speech request failed";
}

/* Fixed region suffix prevents SD settings from sending a key to another host. */
static esp_http_client_handle_t client(bool tts) {
    char url[192];
    int length = snprintf(url, sizeof(url), tts ?
        "https://%s.tts.speech.microsoft.com/cognitiveservices/v1" :
        "https://%s.stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1?language=ko-KR&format=simple",
        m4_sd_azure_region());
    if (length < 0 || (size_t)length >= sizeof(url)) return NULL;
    esp_http_client_config_t config = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 3000,
        .buffer_size = 2048, .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true};
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) return NULL;
    esp_http_client_set_header(http, "Ocp-Apim-Subscription-Key", m4_sd_azure_key());
    esp_http_client_set_header(http, "Content-Type", tts ? "application/ssml+xml; charset=utf-8" : "audio/wav; codecs=audio/pcm; samplerate=16000");
    esp_http_client_set_header(http, "User-Agent", "M4-Speech");
    esp_http_client_set_header(http, "Accept", tts ? "audio/pcm" : "application/json");
    if (tts) esp_http_client_set_header(http, "X-Microsoft-OutputFormat", "raw-16khz-16bit-mono-pcm");
    return http;
}

static bool alive(bool (*cancelled)(void *), void *context, int64_t deadline) {
    return !cancelled(context) && esp_timer_get_time() < deadline;
}

static bool post(esp_http_client_handle_t http, const void *body, size_t length,
                 bool (*cancelled)(void *), void *context, int64_t deadline, const char **error) {
    if (!alive(cancelled, context, deadline) || esp_http_client_open(http, (int)length) != ESP_OK) {
        *error = "Azure HTTPS connection failed"; return false;
    }
    size_t sent = 0;
    while (sent < length && alive(cancelled, context, deadline)) {
        size_t block = length - sent; if (block > 4096) block = 4096;
        int count = esp_http_client_write(http, (const char *)body + sent, block);
        if (count == -ESP_ERR_HTTP_EAGAIN) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (count <= 0) { *error = "Azure upload failed"; return false; }
        sent += count;
    }
    if (sent != length) { *error = "Azure upload cancelled/timed out"; return false; }
    int64_t headers;
    do {
        if (!alive(cancelled, context, deadline)) { *error = "Azure response cancelled/timed out"; return false; }
        headers = esp_http_client_fetch_headers(http);
        if (headers == -ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(10));
    } while (headers == -ESP_ERR_HTTP_EAGAIN);
    if (headers < 0) { *error = "Azure response headers failed"; return false; }
    int status = esp_http_client_get_status_code(http);
    if (status != 200) { *error = status_error(status); return false; }
    if (esp_http_client_set_timeout_ms(http, 1000) != ESP_OK) { *error = "Azure response headers failed"; return false; }
    return true;
}

/* Escape user text as data, never as SSML markup. */
static bool xml_text(const char *text, char *out, size_t capacity) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *escape = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" :
                             *p == '"' ? "&quot;" : *p == '\'' ? "&apos;" : NULL;
        size_t count = escape ? strlen(escape) : 1;
        if (used + count >= capacity) return false;
        if (escape) memcpy(out + used, escape, count); else out[used] = (char)*p;
        used += count;
    }
    out[used] = 0; return true;
}
static bool tts_cancelled(void *context) { return ((const m4_audio_output_t *)context)->cancelled(); }

void m4_azure_tts_run(const char *text, const m4_audio_output_t *out) {
    const char *error = "Azure TTS failed";
    bool leased = false, started = false, ok = false;
    esp_http_client_handle_t http = NULL;
    char *escaped = NULL, *body = NULL;
    unsigned char *input = NULL;
    size_t used = 0, total = 0;
    int64_t began = esp_timer_get_time(), deadline = began + 90000000;
    if (!m4_azure_audio_ready() || network_get_mode() != NET_MODE_STA_CONNECTED || !m4_tts_text_valid(text)) {
        out->message("Azure TTS needs Wi-Fi, key/region and valid text", false, 0); return;
    }
    out->message("Azure TTS / preparing Korean audio...", true, 0);
    leased = https_memory_exclusive_begin(5000);
    if (!leased) { error = "TLS busy; stop other HTTPS work"; goto done; }
    escaped = heap_caps_malloc(2305, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    body = heap_caps_malloc(2560, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    input = heap_caps_malloc(1025, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!escaped || !body || !input) { error = "Azure audio PSRAM allocation failed"; goto done; }
    if (!xml_text(text, escaped, 2305)) { error = "Azure TTS text too long"; goto done; }
    int length = snprintf(body, 2560, "<speak version='1.0' xml:lang='ko-KR'><voice name='ko-KR-SunHiNeural'>%s</voice></speak>", escaped);
    if (length < 0 || length >= 2560) { error = "Azure TTS text too long"; goto done; }
    http = client(true);
    if (!http || !post(http, body, length, tts_cancelled, (void *)out, deadline, &error)) goto done;
    while (!esp_http_client_is_complete_data_received(http)) {
        if (!alive(tts_cancelled, (void *)out, deadline)) { error = "Azure TTS cancelled/timed out"; goto done; }
        int count = esp_http_client_read(http, (char *)input + used, 1024);
        if (count == -ESP_ERR_HTTP_EAGAIN || count == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (count < 0) { error = "Azure audio receive failed"; goto done; }
        total += (size_t)count; used += (size_t)count;
        if (total > AZURE_PCM_LIMIT) { error = "Azure audio exceeds limit"; goto done; }
        if (used < 2) continue;
        if (!started) {
            if (!out->begin(AZURE_RATE)) { error = "Azure output codec failed"; goto done; }
            started = true;
            ESP_LOGI(TAG, "TTS first PCM: %lld ms", (long long)((esp_timer_get_time() - began) / 1000));
            out->message("Azure TTS / speaking Korean", true, 0);
        }
        int16_t stereo[1024];
        size_t frames = used / 2;
        for (size_t i = 0; i < frames; ++i)
            stereo[2 * i] = stereo[2 * i + 1] = (int16_t)((unsigned)input[2 * i] | (unsigned)input[2 * i + 1] << 8);
        if (!out->write(stereo, frames * 4)) { error = "Azure playback cancelled/output failed"; goto done; }
        used %= 2;
        if (used) input[0] = input[frames * 2];
    }
    if (used || !started) { error = "Azure returned invalid PCM16 audio"; goto done; }
    ok = !out->cancelled() && out->drain();
    if (!ok) error = "Azure output stopped";
done:
    if (http) esp_http_client_cleanup(http);
    if (leased) https_memory_exclusive_end();
    if (escaped) { mbedtls_platform_zeroize(escaped, 2305); free(escaped); }
    if (body) { mbedtls_platform_zeroize(body, 2560); free(body); }
    if (input) { mbedtls_platform_zeroize(input, 1025); free(input); }
    out->message(ok ? "Azure TTS complete" : error, false, ok ? 100 : 0);
}

bool m4_azure_transcribe(const unsigned char *wav, size_t bytes, char text[385],
                         bool (*cancelled)(void *), void *context, const char **error) {
    bool leased = false, ok = false;
    esp_http_client_handle_t http = NULL;
    char *reply = NULL; cJSON *json = NULL; size_t used = 0;
    int64_t began = esp_timer_get_time(), deadline = began + 60000000;
    text[0] = 0;
    if (!m4_azure_audio_ready() || !wav || bytes < 44 || bytes > 256044) { *error = "Azure STT key/region/audio invalid"; return false; }
    leased = https_memory_exclusive_begin(5000);
    if (!leased) { *error = "TLS busy; stop other HTTPS work"; goto done; }
    reply = heap_caps_calloc(1, AZURE_REPLY_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!reply) { *error = "Azure STT PSRAM allocation failed"; goto done; }
    http = client(false);
    if (!http) { *error = "Azure HTTP allocation failed"; goto done; }
    if (!post(http, wav, bytes, cancelled, context, deadline, error)) goto done;
    while (!esp_http_client_is_complete_data_received(http)) {
        if (!alive(cancelled, context, deadline)) { *error = "Azure STT cancelled/timed out"; goto done; }
        if (used >= AZURE_REPLY_BYTES) { *error = "Azure STT response too large"; goto done; }
        int count = esp_http_client_read(http, reply + used, AZURE_REPLY_BYTES - used);
        if (count == -ESP_ERR_HTTP_EAGAIN || count == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (count < 0) { *error = "Azure STT receive failed"; goto done; }
        used += (size_t)count;
    }
    reply[used] = 0; json = cJSON_Parse(reply);
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "RecognitionStatus");
    const cJSON *recognized = cJSON_GetObjectItemCaseSensitive(json, "DisplayText");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "Success")) { *error = "Azure did not detect clear speech"; goto done; }
    if (!cJSON_IsString(recognized) || !m4_tts_text_valid(recognized->valuestring)) { *error = "Azure recognition empty/too long"; goto done; }
    if (!alive(cancelled, context, deadline)) { *error = "Azure STT cancelled/timed out"; goto done; }
    snprintf(text, 385, "%s", recognized->valuestring); ok = true;
    ESP_LOGI(TAG, "STT upload/recognition: %lld ms", (long long)((esp_timer_get_time() - began) / 1000));
done:
    if (json) cJSON_Delete(json);
    if (reply) { mbedtls_platform_zeroize(reply, AZURE_REPLY_BYTES + 1); free(reply); }
    if (http) esp_http_client_cleanup(http);
    if (leased) https_memory_exclusive_end();
    return ok;
}
