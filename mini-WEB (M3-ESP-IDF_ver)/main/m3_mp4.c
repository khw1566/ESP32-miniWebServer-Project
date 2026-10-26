#include "m3_mp4.h"
#include "m3_mp4_demux.h"
#include "esp_h264_dec_sw.h"
#include "esp_aac_dec.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { VIDEO_BYTES = 1024 * 1024, AUDIO_BYTES = 8192, PCM_BYTES = 8192 };
/* The P4 has no H.264 decode block, so high resolutions decode below real time.
 * Late pictures still refresh the screen at a bounded rate, and a large lag
 * resynchronises at the newest sync sample instead of decoding every P frame. */
enum { LATE_US = 100000, STALE_FRAME_US = 250000, RESYNC_US = 400000 };
typedef struct {
    m3_mp4_demux_t *demux;
    m3_mp4_info_t info;
    const m3_audio_output_t *audio;
    const m3_mp4_view_t *view;
    unsigned job;
    esp_h264_dec_handle_t decoder;
    esp_h264_dec_param_sw_handle_t parameters;
    atomic_bool stop, video_done;
    atomic_int video_result;
    portMUX_TYPE clock_lock; /* This structure and its task stacks stay in internal RAM. */
    int64_t origin, pause_at, paused_us, shown_us;
    bool clock_paused;
} player_t;
static bool alive(player_t *p) {
    return !atomic_load(&p->stop) && !p->audio->cancelled() && p->view->current(p->job) &&
        (!atomic_load(&p->video_done) || atomic_load(&p->video_result) == M3_MP4_OK);
}
static int64_t clock_us(player_t *p) {
    int64_t now = esp_timer_get_time(); bool paused = p->view->paused();
    portENTER_CRITICAL(&p->clock_lock);
    if (paused && !p->clock_paused) { p->pause_at = now; p->clock_paused = true; }
    else if (!paused && p->clock_paused) { p->paused_us += now - p->pause_at; p->clock_paused = false; }
    int64_t result = (p->clock_paused ? p->pause_at : now) - p->origin - p->paused_us;
    portEXIT_CRITICAL(&p->clock_lock); return result;
}
static bool wait_time(player_t *p, int64_t target) {
    while (alive(p)) {
        int64_t now = clock_us(p);
        if (!p->view->paused() && now >= target) return true;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}
static m3_mp4_result_t decode(player_t *p, uint8_t *data, unsigned bytes, int64_t pts, bool present, unsigned *pictures) {
    unsigned stalled = 0;
    esp_h264_dec_in_frame_t input = {.raw_data = {.buffer = data, .len = bytes}, .pts = pts < 0 ? 0 : (uint32_t)(pts / 1000)};
    while (input.raw_data.len && alive(p)) {
        esp_h264_dec_out_frame_t output = {0};
        input.consume = 0;
        esp_h264_err_t result = esp_h264_dec_process(p->decoder, &input, &output);
        if (result != ESP_H264_ERR_OK) return result == ESP_H264_ERR_MEM ? M3_MP4_MEMORY : M3_MP4_VIDEO_ERROR;
        if (input.consume > input.raw_data.len || (!input.consume && (!output.out_size || ++stalled > 4))) return M3_MP4_VIDEO_ERROR;
        if (input.consume) stalled = 0;
        if (output.out_size && present) {
            ++*pictures;
            esp_h264_resolution_t size = {0};
            if (esp_h264_dec_get_resolution(p->parameters, &size) != ESP_H264_ERR_OK || size.width != p->info.width || size.height != p->info.height) return M3_MP4_VIDEO_ERROR;
            /* Decode reference frames even when late, but omit their display. */
            int64_t now = pts >= 0 && wait_time(p, pts) ? clock_us(p) : -1;
            if (now >= 0 && (now - pts < LATE_US || now - p->shown_us >= STALE_FRAME_US)) {
                p->shown_us = now;
                int slot = -1; uint8_t *canvas = p->view->acquire(p->job, &slot);
                if (!canvas && slot == -2) return M3_MP4_MEMORY;
                if (canvas) {
                    bool ok = m3_mp4_i420_fit(output.outbuf, output.out_size, size.width, size.height, canvas);
                    p->view->publish(p->job, slot, ok ? (uint32_t)(pts / 1000) : UINT32_MAX, p->info.duration_ms);
                    if (!ok) return M3_MP4_VIDEO_ERROR;
                }
            }
        }
        input.raw_data.buffer += input.consume; input.raw_data.len -= input.consume;
    }
    return alive(p) ? M3_MP4_OK : M3_MP4_STOPPED;
}
static void video_worker(void *argument) {
    player_t *p = argument;
    uint8_t *buffer = heap_caps_malloc(VIDEO_BYTES + 6, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    m3_mp4_result_t result = buffer ? M3_MP4_OK : M3_MP4_MEMORY;
    unsigned pictures = 0;
    static const uint8_t delimiter[6] = {0,0,0,1,9,0xf0};
    for (unsigned index = 0; buffer && index < p->info.video_samples && alive(p); ++index) {
        m3_mp4_sample_t sample;
        if (!m3_mp4_demux_sample(p->demux, true, index, &sample)) { result = M3_MP4_STORAGE; break; }
        int64_t now = clock_us(p);
        if (index && now - sample.pts_us > RESYNC_US) {
            unsigned key = m3_mp4_demux_keyframe(p->demux, index, now);
            if (key != index && !m3_mp4_demux_sample(p->demux, true, index = key, &sample)) { result = M3_MP4_STORAGE; break; }
        }
        if (!m3_mp4_demux_read(p->demux, &sample, buffer, VIDEO_BYTES)) { result = M3_MP4_STORAGE; break; }
        if (!m3_mp4_avcc_sample(buffer, sample.bytes, p->info.nal_length)) { result = M3_MP4_FORMAT; break; }
        memcpy(buffer + sample.bytes, delimiter, sizeof(delimiter));
        result = decode(p, buffer, sample.bytes + sizeof(delimiter), sample.pts_us, true, &pictures);
        if (result != M3_MP4_OK) break;
    }
    if (result == M3_MP4_OK && !pictures) result = M3_MP4_VIDEO_ERROR;
    free(buffer);
    atomic_store(&p->video_result, result);
    atomic_store(&p->video_done, true);
    vTaskDelete(NULL); /* No more accesses to p after publishing completion. */
}
static m3_mp4_result_t audio_track(player_t *p) {
    if (!p->info.audio_samples) {
        while (!atomic_load(&p->video_done) && alive(p)) { clock_us(p); vTaskDelay(pdMS_TO_TICKS(5)); }
        return M3_MP4_OK;
    }
    esp_aac_dec_cfg_t config = {.sample_rate = p->info.sample_rate, .channel = p->info.channels,
        .bits_per_sample = 16, .no_adts_header = true, .aac_plus_enable = false};
    void *decoder = NULL;
    uint8_t *encoded = heap_caps_malloc(AUDIO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *pcm = heap_caps_malloc(PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *stereo = heap_caps_malloc(PCM_BYTES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    m3_mp4_result_t result = M3_MP4_MEMORY;
    if (!encoded || !pcm || !stereo) goto done;
    esp_audio_err_t opened = esp_aac_dec_open(&config, sizeof(config), &decoder);
    if (opened != ESP_AUDIO_ERR_OK) { result = opened == ESP_AUDIO_ERR_MEM_LACK ? M3_MP4_MEMORY : M3_MP4_AUDIO_ERROR; goto done; }
    if (!p->audio->begin(p->info.sample_rate)) { result = M3_MP4_AUDIO_ERROR; goto done; }
    result = M3_MP4_OK;
    bool drained_pause = false;
    for (unsigned index = 0; index < p->info.audio_samples && alive(p); ++index) {
        if (atomic_load(&p->video_done) && atomic_load(&p->video_result) != M3_MP4_OK) break;
        m3_mp4_sample_t sample;
        if (!m3_mp4_demux_sample(p->demux, false, index, &sample) || !m3_mp4_demux_read(p->demux, &sample, encoded, AUDIO_BYTES)) { result = M3_MP4_STORAGE; break; }
        if (sample.pts_us >= (int64_t)p->info.duration_ms * 1000) break;
        esp_audio_dec_in_raw_t input = {.buffer = encoded, .len = sample.bytes};
        esp_audio_dec_out_frame_t output = {.buffer = pcm, .len = PCM_BYTES};
        esp_audio_dec_info_t info = {0};
        esp_audio_err_t decoded = esp_aac_dec_decode(decoder, &input, &output, &info);
        if (decoded != ESP_AUDIO_ERR_OK || input.consumed != sample.bytes || !output.decoded_size || output.decoded_size > PCM_BYTES ||
            info.bits_per_sample != 16 || info.channel != p->info.channels || info.sample_rate != p->info.sample_rate || output.decoded_size % (info.channel * 2)) {
            result = decoded == ESP_AUDIO_ERR_MEM_LACK ? M3_MP4_MEMORY : M3_MP4_AUDIO_ERROR; break;
        }
        unsigned frames = output.decoded_size / (info.channel * 2), skip = 0;
        if (sample.pts_us < 0) {
            uint64_t trim = ((uint64_t)(-sample.pts_us) * info.sample_rate + 999999) / 1000000;
            skip = trim > frames ? frames : (unsigned)trim;
        }
        if (skip == frames) continue;
        while (alive(p) && p->view->paused()) {
            clock_us(p);
            if (!drained_pause && !p->audio->drain()) { result = M3_MP4_AUDIO_ERROR; goto done; }
            drained_pause = true; vTaskDelay(pdMS_TO_TICKS(5));
        }
        drained_pause = false;
        int64_t pts = sample.pts_us < 0 ? 0 : sample.pts_us;
        if (!wait_time(p, pts)) break;
        int16_t *samples = (int16_t *)pcm + skip * info.channel;
        unsigned bytes = (frames - skip) * 4;
        if (info.channel == 1) {
            for (unsigned i = 0; i < frames - skip; ++i) stereo[i * 2] = stereo[i * 2 + 1] = samples[i];
            samples = stereo;
        }
        if (!p->audio->write(samples, bytes)) { result = M3_MP4_AUDIO_ERROR; break; }
        unsigned progress = p->info.duration_ms ? (uint64_t)(pts / 1000) * 100 / p->info.duration_ms : 0;
        p->audio->message("MP4 playing", true, progress > 100 ? 100 : progress);
    }
    if (result == M3_MP4_OK && alive(p) && !p->audio->drain()) result = M3_MP4_AUDIO_ERROR;
done:
    if (decoder) esp_aac_dec_close(decoder);
    free(encoded); free(pcm); free(stereo); return result;
}
static m3_mp4_result_t thumbnail_decode(esp_h264_dec_handle_t decoder, esp_h264_dec_param_sw_handle_t parameters,
    const m3_mp4_info_t *info, uint8_t *data, unsigned bytes, unsigned job, bool (*current)(unsigned), uint8_t *canvas, bool *ready) {
    esp_h264_dec_in_frame_t input = {.raw_data = {.buffer = data, .len = bytes}};
    unsigned stalled = 0;
    while (input.raw_data.len && current(job)) {
        esp_h264_dec_out_frame_t output = {0}; input.consume = 0;
        esp_h264_err_t err = esp_h264_dec_process(decoder, &input, &output);
        if (err != ESP_H264_ERR_OK) return err == ESP_H264_ERR_MEM ? M3_MP4_MEMORY : M3_MP4_VIDEO_ERROR;
        if (input.consume > input.raw_data.len || (!input.consume && (!output.out_size || ++stalled > 4))) return M3_MP4_VIDEO_ERROR;
        if (input.consume) stalled = 0;
        if (output.out_size) {
            esp_h264_resolution_t size = {0};
            if (esp_h264_dec_get_resolution(parameters, &size) != ESP_H264_ERR_OK || size.width != info->width || size.height != info->height ||
                !m3_mp4_i420_fit(output.outbuf, output.out_size, size.width, size.height, canvas)) return M3_MP4_VIDEO_ERROR;
            *ready = true; return M3_MP4_OK;
        }
        input.raw_data.buffer += input.consume; input.raw_data.len -= input.consume;
    }
    return current(job) ? M3_MP4_OK : M3_MP4_STOPPED;
}
m3_mp4_result_t m3_mp4_thumbnail(const char *relative, unsigned job, bool (*current)(unsigned), uint8_t *canvas) {
    if (!current || !canvas || !current(job)) return M3_MP4_STOPPED;
    m3_mp4_demux_t *demux = NULL; m3_mp4_info_t info;
    esp_h264_dec_handle_t decoder = NULL; esp_h264_dec_param_sw_handle_t parameters = NULL;
    uint8_t *buffer = NULL; bool ready = false;
    m3_mp4_result_t result = m3_mp4_demux_open(relative, job, current, &demux, &info);
    if (result != M3_MP4_OK) goto done;
    esp_h264_dec_cfg_sw_t config = {.pic_type = ESP_H264_RAW_FMT_I420};
    esp_h264_err_t err = esp_h264_dec_sw_new(&config, &decoder);
    if (err != ESP_H264_ERR_OK) { result = err == ESP_H264_ERR_MEM ? M3_MP4_MEMORY : M3_MP4_VIDEO_ERROR; goto done; }
    if (esp_h264_dec_open(decoder) != ESP_H264_ERR_OK || esp_h264_dec_sw_get_param_hd(decoder, &parameters) != ESP_H264_ERR_OK) { result = M3_MP4_VIDEO_ERROR; goto done; }
    buffer = heap_caps_malloc(VIDEO_BYTES + 6, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) { result = M3_MP4_MEMORY; goto done; }
    unsigned used = 0;
    for (unsigned i = 0; i < 2; ++i) {
        int bytes = 0; const uint8_t *data = m3_mp4_demux_parameter(demux, i != 0, 0, &bytes);
        if (!data || bytes <= 0 || used + 4 + bytes + 6 > VIDEO_BYTES) { result = M3_MP4_FORMAT; goto done; }
        memcpy(buffer + used, "\0\0\0\1", 4); used += 4; memcpy(buffer + used, data, bytes); used += bytes;
    }
    memcpy(buffer + used, "\0\0\0\1\x09\xf0", 6); used += 6;
    result = thumbnail_decode(decoder, parameters, &info, buffer, used, job, current, canvas, &ready);
    for (unsigned i = 0; result == M3_MP4_OK && !ready && i < info.video_samples && i < 8 && current(job); ++i) {
        m3_mp4_sample_t sample;
        if (!m3_mp4_demux_sample(demux, true, i, &sample) || !m3_mp4_demux_read(demux, &sample, buffer, VIDEO_BYTES)) { result = M3_MP4_STORAGE; break; }
        if (!m3_mp4_avcc_sample(buffer, sample.bytes, info.nal_length)) { result = M3_MP4_FORMAT; break; }
        memcpy(buffer + sample.bytes, "\0\0\0\1\x09\xf0", 6);
        result = thumbnail_decode(decoder, parameters, &info, buffer, sample.bytes + 6, job, current, canvas, &ready);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (result == M3_MP4_OK && !ready) result = M3_MP4_VIDEO_ERROR;
done:
    if (!current(job)) result = M3_MP4_STOPPED;
    if (decoder) { esp_h264_dec_close(decoder); esp_h264_dec_del(decoder); }
    m3_mp4_demux_close(demux); free(buffer); return result;
}
m3_mp4_result_t m3_mp4_play(const char *relative, unsigned job, const m3_audio_output_t *audio, const m3_mp4_view_t *view) {
    if (!relative || !audio || !view || !view->current || !view->paused || !view->acquire || !view->publish) return M3_MP4_FORMAT;
    player_t *p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) return M3_MP4_MEMORY;
    p->clock_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    p->audio = audio; p->view = view; p->job = job;
    atomic_init(&p->stop, false); atomic_init(&p->video_done, false); atomic_init(&p->video_result, M3_MP4_OK);
    bool video_started = false;
    m3_mp4_result_t result = m3_mp4_demux_open(relative, job, view->current, &p->demux, &p->info);
    if (result != M3_MP4_OK) goto done;
    esp_h264_dec_cfg_sw_t config = {.pic_type = ESP_H264_RAW_FMT_I420};
    esp_h264_err_t created = esp_h264_dec_sw_new(&config, &p->decoder);
    if (created != ESP_H264_ERR_OK) { result = created == ESP_H264_ERR_MEM ? M3_MP4_MEMORY : M3_MP4_VIDEO_ERROR; goto done; }
    if (esp_h264_dec_open(p->decoder) != ESP_H264_ERR_OK || esp_h264_dec_sw_get_param_hd(p->decoder, &p->parameters) != ESP_H264_ERR_OK) {
        result = M3_MP4_VIDEO_ERROR; goto done;
    }
    uint8_t parameter[1400]; unsigned used = 0, ignored = 0;
    for (unsigned i = 0; i < 2; ++i) {
        int bytes = 0; const uint8_t *data = m3_mp4_demux_parameter(p->demux, i != 0, 0, &bytes);
        if (!data || bytes <= 0 || used + 4 + bytes + 6 > sizeof(parameter)) { result = M3_MP4_FORMAT; goto done; }
        memset(parameter + used, 0, 3); parameter[used + 3] = 1; used += 4;
        memcpy(parameter + used, data, bytes); used += bytes;
    }
    memcpy(parameter + used, "\0\0\0\1\x09\xf0", 6); used += 6;
    result = decode(p, parameter, used, 0, false, &ignored);
    if (result != M3_MP4_OK) goto done;
    p->origin = esp_timer_get_time() + 150000;
    if (xTaskCreatePinnedToCore(video_worker, "m3_mp4_video", 16384, p, 2, NULL, 0) != pdPASS) { result = M3_MP4_MEMORY; goto done; }
    video_started = true;
    audio->message("MP4 playing", true, 0);
    result = audio_track(p);
    if (result == M3_MP4_OK) {
        while (!atomic_load(&p->video_done) && alive(p)) { clock_us(p); vTaskDelay(pdMS_TO_TICKS(5)); }
        if (atomic_load(&p->video_done)) result = atomic_load(&p->video_result);
    }
done:
    if (!view->current(job) || audio->cancelled()) result = M3_MP4_STOPPED;
    atomic_store(&p->stop, true);
    /* Join before freeing decoder, file, index or renderer context. */
    while (video_started && !atomic_load(&p->video_done)) vTaskDelay(pdMS_TO_TICKS(5));
    if (p->decoder) { esp_h264_dec_close(p->decoder); esp_h264_dec_del(p->decoder); }
    m3_mp4_demux_close(p->demux); free(p);
    audio->message(result == M3_MP4_OK ? "MP4 complete" : result == M3_MP4_STOPPED ? "MP4 stopped" : "MP4 playback failed", false, result == M3_MP4_OK ? 100 : 0);
    return result;
}
