#include "m4_recorder.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static m4_recorder_state_t state;
static bool running;
static atomic_bool finish_requested;

static void put32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (i * 8));
}
void m4_recorder_wav_header(uint8_t out[44], uint32_t bytes) {
    memset(out, 0, 44);
    memcpy(out, "RIFF", 4); put32(out + 4, bytes + 36);
    memcpy(out + 8, "WAVEfmt ", 8); put32(out + 16, 16);
    out[20] = 1; out[22] = 1;
    put32(out + 24, M4_RECORDER_RATE); put32(out + 28, M4_RECORDER_RATE * 2);
    out[32] = 2; out[34] = 16;
    memcpy(out + 36, "data", 4); put32(out + 40, bytes);
}
void m4_recorder_state(m4_recorder_state_t *out) {
    portENTER_CRITICAL(&lock); *out = state; portEXIT_CRITICAL(&lock);
}
unsigned m4_recorder_prepare(void) {
    portENTER_CRITICAL(&lock);
    if (state.busy) { portEXIT_CRITICAL(&lock); return 0; }
    if (++state.job == 0) ++state.job;
    state.busy = true; state.recording = false;
    state.bytes = state.seconds = state.level = 0;
    state.error_code = 0; state.error_stage[0] = 0;
    snprintf(state.message, sizeof(state.message), "Preparing recording");
    ++state.revision;
    atomic_store(&finish_requested, false);
    unsigned job = state.job;
    portEXIT_CRITICAL(&lock);
    return job;
}
void m4_recorder_finish(void) { atomic_store(&finish_requested, true); }
void m4_recorder_cancel(void) {
    portENTER_CRITICAL(&lock);
    atomic_store(&finish_requested, true);
    /* A queued request may be removed before the audio worker sees it. */
    if (state.busy && !running) {
        state.busy = false; ++state.job; ++state.revision;
        snprintf(state.message, sizeof(state.message), "Recording stopped");
    }
    portEXIT_CRITICAL(&lock);
}
static void progress(uint32_t bytes, const int16_t *samples, unsigned count) {
    unsigned peak = 0;
    for (unsigned i = 0; i < count; ++i) {
        int value = samples[i]; unsigned magnitude = value < 0 ? -value : value;
        if (magnitude > peak) peak = magnitude;
    }
    portENTER_CRITICAL(&lock);
    state.bytes = bytes; state.seconds = bytes / (M4_RECORDER_RATE * 2);
    state.level = peak * 100U / 32768U;
    portEXIT_CRITICAL(&lock);
}
static bool record_error(const char *stage) {
    int error = errno;
    portENTER_CRITICAL(&lock);
    state.error_code = error;
    snprintf(state.error_stage, sizeof(state.error_stage), "%s", stage);
    portEXIT_CRITICAL(&lock);
    ESP_LOGE("M4_RECORDER", "Save failed at %s, errno=%d", stage, error);
    return false;
}
static bool checkpoint(FILE *file, uint32_t bytes) {
    uint8_t header[44]; m4_recorder_wav_header(header, bytes);
    /* Commit PCM before rewriting the header; resume at the known PCM end.
     * Do not depend on stdio's cached SEEK_END position after a header rewrite. */
    if (fflush(file)) return record_error("pcm-flush");
    if (fseek(file, 0, SEEK_SET)) return record_error("header-seek");
    if (fwrite(header, 1, 44, file) != 44) return record_error("header-write");
    if (fflush(file)) return record_error("header-flush");
    if (fseek(file, (long)bytes + 44, SEEK_SET)) return record_error("pcm-seek");
    return true;
}
void m4_recorder_run(unsigned job, const m4_voice_capture_t *capture) {
    portENTER_CRITICAL(&lock);
    if (job != state.job || !state.busy || running) { portEXIT_CRITICAL(&lock); return; }
    running = true;
    portEXIT_CRITICAL(&lock);
    const char *message = "Recording stopped";
    FILE *file = NULL;
    int16_t *pcm = NULL;
    char *buffer = NULL;
    char filename[64] = {0}, final_path[160] = {0};
    char temporary[sizeof(final_path) + sizeof(".part") - 1] = {0};
    uint32_t bytes = 0, last_checkpoint = 0;
    bool locked = false, attempted = false, io_ok = true, saved = false;
    if (atomic_load(&finish_requested) || capture->cancelled()) goto done;
    if (!(locked = sd_media_admin_lock())) { message = "SD unavailable or busy"; goto done; }
    if (mkdir(SD_MEDIA_ROOT M4_RECORDER_FOLDER, 0775) != 0 && errno != EEXIST) {
        message = "Cannot create recording folder"; goto done;
    }
    pcm = heap_caps_malloc(160 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm || !buffer) { message = "Recording buffer allocation failed"; goto done; }
    /* Exclusive create prevents overwriting existing recordings, even with a bad clock. */
    for (unsigned attempt = 0; attempt < 8 && !file; ++attempt) {
        time_t now = time(NULL); struct tm date;
        char stamp[32];
        if (now >= 1735689600 && localtime_r(&now, &date)) strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &date);
        else snprintf(stamp, sizeof(stamp), "boot_%llu", (unsigned long long)(esp_timer_get_time() / 1000000));
        snprintf(filename, sizeof(filename), "rec_%s_%08lx.wav", stamp, (unsigned long)esp_random());
        snprintf(final_path, sizeof(final_path), SD_MEDIA_ROOT M4_RECORDER_FOLDER "/%s", filename);
        struct stat existing;
        if (stat(final_path, &existing) == 0) continue;
        snprintf(temporary, sizeof(temporary), "%s.part", final_path);
        int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0664);
        if (fd < 0) { if (errno == EEXIST) continue; break; }
        file = fdopen(fd, "wb");
        if (!file) { close(fd); unlink(temporary); break; }
    }
    if (!file) { message = "Cannot create recording file"; goto done; }
    if (setvbuf(file, buffer, _IOFBF, 4096) != 0 || !checkpoint(file, 0)) {
        message = "SD recording write failed"; io_ok = false; goto done;
    }
    if (atomic_load(&finish_requested) || capture->cancelled()) goto done;
    attempted = true;
    if (!capture->begin()) { message = "Microphone open failed"; goto done; }
    portENTER_CRITICAL(&lock);
    state.recording = true; snprintf(state.message, sizeof(state.message), "Recording to SD card"); ++state.revision;
    portEXIT_CRITICAL(&lock);
    while (bytes < M4_RECORDER_MAX_SECONDS * M4_RECORDER_RATE * 2 &&
           !atomic_load(&finish_requested) && !capture->cancelled()) {
        if (!capture->read(pcm, 160)) {
            if (!capture->cancelled()) message = "Microphone read failed";
            break;
        }
        if (fwrite(pcm, 1, 320, file) != 320) { record_error("pcm-write"); message = "SD recording write failed"; io_ok = false; break; }
        bytes += 320; progress(bytes, pcm, 160);
        if (bytes - last_checkpoint >= M4_RECORDER_RATE * 2) {
            if (!checkpoint(file, bytes)) { message = "SD recording write failed"; io_ok = false; break; }
            last_checkpoint = bytes;
        }
    }
done:
    /* Keep ownership until the microphone, file and SD lock are all released. */
    if (attempted) capture->end();
    if (file) {
        bool complete = io_ok && bytes && checkpoint(file, bytes);
        if (complete && fsync(fileno(file)) != 0) { record_error("sync"); complete = false; }
        if (fclose(file) != 0) { record_error("close"); complete = false; }
        file = NULL;
        if (complete && rename(temporary, final_path) != 0) { record_error("rename"); complete = false; }
        if (complete) {
            saved = true;
            if (!strcmp(message, "Recording stopped")) message = "Recording saved";
        } else if (bytes) message = "Recording incomplete; partial file retained";
        else unlink(temporary); /* Only this operation's file with no captured PCM. */
    }
    free(buffer); free(pcm);
    if (saved) sd_media_changed();
    if (locked) sd_media_admin_unlock();
    portENTER_CRITICAL(&lock);
    state.busy = state.recording = false; state.level = 0; running = false;
    state.bytes = bytes; state.seconds = bytes / (M4_RECORDER_RATE * 2);
    if (saved) { state.saved = true; snprintf(state.filename, sizeof(state.filename), "%s", filename); }
    snprintf(state.message, sizeof(state.message), "%s", message); ++state.revision;
    portEXIT_CRITICAL(&lock);
}
