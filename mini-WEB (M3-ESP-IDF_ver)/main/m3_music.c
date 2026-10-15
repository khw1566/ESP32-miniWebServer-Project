#include "m3_music.h"
#include "m3_audio.h"
#include "m3_memory.h"
#include "m3_wav.h"
#include "m3_utf8.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include <math.h>
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MUSIC_FILE_LIMIT = 256 * 1024 * 1024, MUSIC_PREFETCH_LIMIT = 8 * 1024 * 1024,
       MUSIC_READ_BYTES = 4096, MUSIC_PCM_BYTES = 32768 };
typedef struct { char path[384], name[192]; } track_t;
static M3_PSRAM_BSS track_t catalogs[2][M3_MUSIC_TRACKS];
static unsigned catalog_set;
static bool shuffle_played[M3_MUSIC_TRACKS];
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static M3_PSRAM_BSS m3_music_state_t state;
static atomic_bool refresh, requested_sd, paused;
static const char *extension(const char *path) { const char *p = path ? strrchr(path, '.') : NULL; return p ? p : ""; }
static bool supported(const char *path) {
    const char *ext = extension(path);
    return !strcasecmp(ext, ".wav") || !strcasecmp(ext, ".mp3") || !strcasecmp(ext, ".aac");
}
bool m3_music_path_valid(const char *path) {
    if (!path || !m3_utf8_valid(path) || strlen(path) >= sizeof(catalogs[0][0].path) || strstr(path, "..") || strstr(path, "//")) return false;
    if (strncmp(path, SD_MEDIA_ROOT "/media/", strlen(SD_MEDIA_ROOT "/media/")) && strncmp(path, "/spiffs/", 8)) return false;
    for (const char *p = path; *p; ++p) if ((unsigned char)*p < 32 || *p == 127 || *p == '\\') return false;
    return supported(path);
}
void m3_music_state(m3_music_state_t *out) {
    portENTER_CRITICAL(&lock);
    *out = state; out->paused = atomic_load(&paused); out->shown = 0;
    for (unsigned i = state.page * M3_MUSIC_ROWS; i < state.count && out->shown < M3_MUSIC_ROWS; ++i) {
        m3_music_item_t *row = &out->items[out->shown++]; row->index = i;
        snprintf(row->name, sizeof(row->name), "%s", catalogs[catalog_set][i].name);
    }
    portEXIT_CRITICAL(&lock);
}
void m3_music_refresh(bool sd) { atomic_store(&requested_sd, sd); atomic_store(&refresh, true); }
static void copy_name(char *out, size_t capacity, const char *text) {
    m3_utf8_copy(out, capacity, text);
}
static void scan(const char *folder, track_t *tracks, unsigned *count, unsigned *visited, unsigned depth) {
    if (depth > 3 || *count == M3_MUSIC_TRACKS || *visited >= 1024) return;
    DIR *dir = opendir(folder); if (!dir) return;
    struct dirent *item;
    while (*count < M3_MUSIC_TRACKS && *visited < 1024 && (item = readdir(dir))) {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        ++*visited;
        if (!m3_utf8_valid(item->d_name)) continue;
        char path[384]; int n = snprintf(path, sizeof(path), "%s/%s", folder, item->d_name);
        if (n < 0 || (size_t)n >= sizeof(path) || strstr(path, "..")) continue;
        struct stat st; if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) scan(path, tracks, count, visited, depth + 1);
        else if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= MUSIC_FILE_LIMIT && m3_music_path_valid(path)) {
            track_t *track = &tracks[(*count)++];
            snprintf(track->path, sizeof(track->path), "%s", path);
            const char *relative = !strncmp(path, SD_MEDIA_ROOT "/media/", strlen(SD_MEDIA_ROOT "/media/")) ?
                path + strlen(SD_MEDIA_ROOT "/media/") : path + 8;
            copy_name(track->name, sizeof(track->name), relative);
        }
    }
    closedir(dir);
}
static int compare_tracks(const void *a, const void *b) { return strcasecmp(((const track_t *)a)->name, ((const track_t *)b)->name); }
void m3_music_poll(void) {
    if (!atomic_exchange(&refresh, false)) return;
    m3_audio_state_t audio; m3_audio_state(&audio);
    bool sd = atomic_load(&requested_sd), locked = false;
    const char *message = "Music catalog ready";
    portENTER_CRITICAL(&lock); state.scanning = true; ++state.revision; portEXIT_CRITICAL(&lock);
    if (audio.playing) { message = "Stop audio before changing the music catalog"; goto done; }
    if (sd && !(locked = sd_media_admin_lock())) { message = "SD unavailable or busy"; goto done; }
    unsigned count = 0, visited = 0, next = catalog_set ^ 1;
    scan(sd ? SD_MEDIA_ROOT "/media" : "/spiffs", catalogs[next], &count, &visited, 0);
    qsort(catalogs[next], count, sizeof(track_t), compare_tracks);
    portENTER_CRITICAL(&lock);
    catalog_set = next; state.sd = sd; state.count = count; state.page = state.selected = 0;
    memset(shuffle_played, 0, sizeof(shuffle_played));
    ++state.catalog_revision;
    portEXIT_CRITICAL(&lock);
    if (!count) message = "No music found; add WAV, MP3 or ADTS AAC files";
done:
    if (locked) sd_media_admin_unlock();
    portENTER_CRITICAL(&lock); state.scanning = false;
    snprintf(state.message, sizeof(state.message), "%s", message); ++state.revision; portEXIT_CRITICAL(&lock);
}
void m3_music_page(int direction) {
    portENTER_CRITICAL(&lock);
    unsigned pages = (state.count + M3_MUSIC_ROWS - 1) / M3_MUSIC_ROWS;
    if (direction < 0 && state.page) --state.page;
    if (direction > 0 && state.page + 1 < pages) ++state.page;
    ++state.revision; portEXIT_CRITICAL(&lock);
}
bool m3_music_play(unsigned index, unsigned revision) {
    char path[384], title[192];
    portENTER_CRITICAL(&lock);
    if (revision != state.catalog_revision || index >= state.count || state.scanning) { portEXIT_CRITICAL(&lock); return false; }
    snprintf(path, sizeof(path), "%s", catalogs[catalog_set][index].path);
    snprintf(title, sizeof(title), "%s", catalogs[catalog_set][index].name);
    portEXIT_CRITICAL(&lock);
    if (!m3_audio_music(path)) return false;
    atomic_store(&paused, false);
    portENTER_CRITICAL(&lock); memset(shuffle_played, 0, sizeof(shuffle_played)); state.selected = index; state.page = index / M3_MUSIC_ROWS;
    snprintf(state.title, sizeof(state.title), "%s", title);
    snprintf(state.message, sizeof(state.message), "Music queued"); ++state.revision; portEXIT_CRITICAL(&lock);
    return true;
}
/* count is the end-of-list sentinel. Manual navigation ignores repeat-one. */
static unsigned music_target(unsigned count, unsigned selected, int direction, bool automatic,
                             m3_music_repeat_t mode, bool shuffle, unsigned random, const bool *played) {
    if (!count) return count;
    if (automatic && mode == M3_MUSIC_REPEAT_ONE) return selected;
    if (shuffle && count > 1) {
        if (!automatic) return (selected + 1 + random % (count - 1)) % count;
        unsigned available = 0;
        for (unsigned i = 0; i < count; ++i) if (i != selected && !played[i]) ++available;
        if (!available) {
            if (mode == M3_MUSIC_REPEAT_OFF) return count;
            return (selected + 1 + random % (count - 1)) % count;
        }
        unsigned choice = random % available;
        for (unsigned i = 0; i < count; ++i) if (i != selected && !played[i]) {
            if (!choice) return i;
            --choice;
        }
    }
    if (automatic && mode == M3_MUSIC_REPEAT_OFF && selected + 1 >= count) return count;
    return direction < 0 ? (selected ? selected - 1 : count - 1) : (selected + 1) % count;
}
bool m3_music_next(int direction) {
    unsigned random = esp_random();
    portENTER_CRITICAL(&lock);
    unsigned count = state.count, revision = state.catalog_revision;
    unsigned next = music_target(count, state.selected, direction, false, state.repeat_mode, state.shuffle, random, shuffle_played);
    portEXIT_CRITICAL(&lock);
    return next < count && m3_music_play(next, revision);
}
void m3_music_pause(void) { atomic_store(&paused, !atomic_load(&paused)); }
void m3_music_repeat_mode(m3_music_repeat_t mode) {
    if (mode > M3_MUSIC_REPEAT_ONE || mode < M3_MUSIC_REPEAT_OFF) return;
    portENTER_CRITICAL(&lock); state.repeat_mode = mode; state.repeat = mode != M3_MUSIC_REPEAT_OFF;
    ++state.revision; portEXIT_CRITICAL(&lock);
}
void m3_music_repeat(bool enabled) { m3_music_repeat_mode(enabled ? M3_MUSIC_REPEAT_ALL : M3_MUSIC_REPEAT_OFF); }
void m3_music_shuffle(bool enabled) {
    portENTER_CRITICAL(&lock); state.shuffle = enabled; memset(shuffle_played, 0, sizeof(shuffle_played)); ++state.revision; portEXIT_CRITICAL(&lock);
}
void m3_music_equalizer(unsigned band, int db) {
    if (band >= 3) return;
    if (db < -12) db = -12;
    if (db > 12) db = 12;
    portENTER_CRITICAL(&lock); state.eq_db[band] = db; ++state.revision; portEXIT_CRITICAL(&lock);
}
/* Two complementary one-pole crossovers, independent state per channel.
 * Flat EQ preserves original PCM exactly. Filter history belongs to this track. */
typedef struct { float low[2], upper[2]; } music_filter_t;
static void music_process(int16_t *pcm, unsigned samples, unsigned channels, unsigned rate, music_filter_t *filter) {
    int db[3]; float energy[3] = {0}, gain[3];
    portENTER_CRITICAL(&lock); memcpy(db, state.eq_db, sizeof(db)); portEXIT_CRITICAL(&lock);
    for (unsigned b = 0; b < 3; ++b) gain[b] = powf(10.0f, db[b] / 20.0f);
    float low_alpha = 1.0f - expf(-6.2831853f * 200.0f / rate);
    float upper_alpha = 1.0f - expf(-6.2831853f * 4000.0f / rate);
    bool flat = !db[0] && !db[1] && !db[2];
    for (unsigned i = 0; i < samples; ++i) {
        unsigned ch = i % channels; float x = pcm[i];
        filter->low[ch] += low_alpha * (x - filter->low[ch]);
        filter->upper[ch] += upper_alpha * (x - filter->upper[ch]);
        float bands[3] = {filter->low[ch], filter->upper[ch] - filter->low[ch], x - filter->upper[ch]};
        float y = 0;
        for (unsigned b = 0; b < 3; ++b) { float v = bands[b] * gain[b]; energy[b] += fabsf(v); y += v; }
        if (!flat) pcm[i] = y > 32767 ? 32767 : y < -32768 ? -32768 : (int16_t)y;
    }
    portENTER_CRITICAL(&lock);
    for (unsigned b = 0; b < 3; ++b) {
        unsigned level = samples ? energy[b] * 100.0f / (samples * 12000.0f) : 0;
        state.bands[b] = level > 100 ? 100 : level;
    }
    portEXIT_CRITICAL(&lock);
}
void m3_music_stop(void) { atomic_store(&paused, false); m3_audio_stop(); }
static void status(const char *message, bool active, unsigned percent, uint64_t frames, unsigned rate) {
    portENTER_CRITICAL(&lock); state.active = active; state.progress = percent;
    if (!active) memset(state.bands, 0, sizeof(state.bands));
    state.seconds = rate ? frames / rate : 0;
    snprintf(state.message, sizeof(state.message), "%s", message); ++state.revision; portEXIT_CRITICAL(&lock);
}
static bool wait_playing(const m3_audio_output_t *out) {
    if (atomic_load(&paused)) {
        if (!out->drain()) return false;
        while (atomic_load(&paused) && !out->cancelled()) vTaskDelay(pdMS_TO_TICKS(20));
    }
    return !out->cancelled();
}
void m3_music_run(const char *path, const m3_audio_output_t *out) {
    FILE *file = NULL; uint8_t *cache = NULL, *input = NULL, *pcm = NULL, *file_buffer = NULL; int16_t *stereo = NULL;
    esp_audio_simple_dec_handle_t decoder = NULL;
    bool sd = path && !strncmp(path, SD_MEDIA_ROOT "/", strlen(SD_MEDIA_ROOT "/")), locked = false, ok = false, opened = false;
    const char *error = "Music file unavailable"; size_t size = 0, position = 0;
    uint64_t frames = 0; unsigned rate = 0;
    music_filter_t filter = {0};
    if (!m3_music_path_valid(path)) goto done;
    portENTER_CRITICAL(&lock);
    const char *filename = strrchr(path, '/');
    copy_name(state.title, sizeof(state.title), filename ? filename + 1 : path);
    for (unsigned i = 0; i < state.count; ++i) if (!strcmp(catalogs[catalog_set][i].path, path)) {
        if (state.shuffle && state.repeat_mode == M3_MUSIC_REPEAT_ALL && shuffle_played[i])
            memset(shuffle_played, 0, sizeof(shuffle_played));
        shuffle_played[i] = true;
        state.selected = i; state.page = i / M3_MUSIC_ROWS;
        snprintf(state.title, sizeof(state.title), "%s", catalogs[catalog_set][i].name);
        break;
    }
    portEXIT_CRITICAL(&lock);
    status("Loading music into PSRAM", true, 0, 0, 0); out->message("Loading local music...", true, 0);
    if (sd && !(locked = sd_media_admin_lock())) { error = "SD unavailable or busy"; goto done; }
    file = fopen(path, "rb");
    if (!file) goto done;
    file_buffer = heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file_buffer || setvbuf(file, (char *)file_buffer, _IOFBF, 64 * 1024)) { error = "Music read buffer allocation failed"; goto done; }
    if (fseek(file, 0, SEEK_END)) goto done;
    long length = ftell(file);
    if (length <= 0 || length > MUSIC_FILE_LIMIT || fseek(file, 0, SEEK_SET)) { error = "Music file size invalid"; goto done; }
    size = length;
    input = heap_caps_malloc(MUSIC_READ_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pcm = heap_caps_malloc(MUSIC_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    stereo = heap_caps_malloc(MUSIC_PCM_BYTES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!input || !pcm || !stereo) { error = "Music PSRAM allocation failed"; goto done; }
    /* Most compressed tracks and every Flash file fit in this bounded cache.
     * Bigger SD files stream through a small explicit PSRAM stdio buffer. */
    if (size <= MUSIC_PREFETCH_LIMIT) cache = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (cache) {
        for (size_t loaded = 0; loaded < size && !out->cancelled();) {
            size_t block = size - loaded; if (block > 16384) block = 16384;
            if (fread(cache + loaded, 1, block, file) != block) { error = "Music read failed"; goto done; }
            loaded += block;
        }
    }
    if (out->cancelled()) goto done;
    bool wav = !strcasecmp(extension(path), ".wav");
    m3_wav_info_t wave;
    if (wav) {
        if (!m3_wav_parse_limit(file, &wave, MUSIC_FILE_LIMIT)) { error = "Use PCM16 WAV, 16-48 kHz"; goto done; }
        rate = wave.sample_rate; position = wave.data_offset; size = position + wave.data_bytes;
    } else {
        if (fseek(file, 0, SEEK_SET)) goto done;
        if (!m3_audio_decoders_ready()) { error = "Music decoder registration failed"; goto done; }
        bool aac = !strcasecmp(extension(path), ".aac");
        esp_aac_dec_cfg_t aac_cfg = {.aac_plus_enable = true};
        esp_audio_simple_dec_cfg_t config = {.dec_type = aac ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = aac ? &aac_cfg : NULL, .cfg_size = aac ? sizeof(aac_cfg) : 0};
        if (esp_audio_simple_dec_open(&config, &decoder) != ESP_AUDIO_ERR_OK) { error = "Music decoder unavailable"; goto done; }
    }
    /* Once parsing and prefetch are complete, cached tracks no longer need the
     * SD lease. Photo widgets can read while this immutable PSRAM copy plays. */
    if (cache) {
        fclose(file); file = NULL;
        if (locked) { sd_media_admin_unlock(); locked = false; }
    }
    ok = true;
    while (position < size && !out->cancelled()) {
        if (opened && !wait_playing(out)) { ok = false; break; }
        unsigned count = size - position > MUSIC_READ_BYTES ? MUSIC_READ_BYTES : size - position;
        if (wav) count -= count % (wave.channels * 2);
        if (!count) { ok = false; error = "Incomplete music frame"; break; }
        if (cache) memcpy(input, cache + position, count);
        else if (fread(input, 1, count, file) != count) { ok = false; error = "Music read failed"; break; }
        unsigned offset = 0, stalled = 0;
        while (offset < count && !out->cancelled()) {
            unsigned bytes, consumed, channels;
            if (wav) { memcpy(pcm, input + offset, count - offset); bytes = consumed = count - offset; channels = wave.channels; }
            else {
                esp_audio_simple_dec_raw_t raw = {.buffer = input + offset, .len = count - offset, .eos = position + count == size};
                esp_audio_simple_dec_out_t frame = {.buffer = pcm, .len = MUSIC_PCM_BYTES};
                if (esp_audio_simple_dec_process(decoder, &raw, &frame) != ESP_AUDIO_ERR_OK || raw.consumed > raw.len || frame.decoded_size > MUSIC_PCM_BYTES) {
                    ok = false; error = "Invalid music stream / decoder error"; goto done;
                }
                consumed = raw.consumed; bytes = frame.decoded_size; channels = 0;
                if (bytes) {
                    esp_audio_simple_dec_info_t info = {0};
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK || info.bits_per_sample != 16 ||
                        (info.channel != 1 && info.channel != 2) || info.sample_rate < 8000 || info.sample_rate > 48000 ||
                        (opened && rate != info.sample_rate)) { ok = false; error = "Unsupported music PCM format"; goto done; }
                    rate = info.sample_rate; channels = info.channel;
                }
            }
            if (bytes) {
                if (!channels || bytes % (channels * 2)) { ok = false; error = "Invalid music PCM frame"; goto done; }
                if (!opened) { if (!out->begin(rate)) { ok = false; error = "Music codec open failed"; goto done; } opened = true; }
                if (!wait_playing(out)) { ok = false; break; }
                music_process((int16_t *)pcm, bytes / 2, channels, rate, &filter);
                void *samples = pcm; unsigned write_bytes = bytes;
                if (channels == 1) {
                    for (unsigned i = 0; i < bytes / 2; ++i) stereo[i * 2] = stereo[i * 2 + 1] = ((int16_t *)pcm)[i];
                    samples = stereo; write_bytes *= 2;
                }
                if (!out->write(samples, write_bytes)) { ok = false; error = "Music I2S write failed"; goto done; }
                frames += bytes / (channels * 2);
            }
            if (!consumed) { if (!bytes || ++stalled > 8) { ok = false; error = "Music decoder stalled"; goto done; } }
            else { offset += consumed; stalled = 0; }
        }
        position += count;
        unsigned progress = (uint64_t)position * 100 / size;
        status("Local music playing", true, progress, frames, rate); out->message("Local music playing", true, progress);
    }
    ok = ok && opened && position == size && !out->cancelled();
    if (ok && !out->drain()) { ok = false; error = "Music output drain failed"; }
    if (!opened && !out->cancelled()) error = "No decodable music found";
done:
    if (decoder) esp_audio_simple_dec_close(decoder);
    if (file) fclose(file);
    free(cache); free(input); free(pcm); free(stereo); free(file_buffer);
    if (locked) sd_media_admin_unlock();
    const char *message = out->cancelled() ? "Music stopped" : ok ? "Music complete" : error;
    status(message, false, ok ? 100 : 0, frames, rate); out->message(message, false, ok ? 100 : 0);
    if (ok && !out->cancelled()) {
        char next_path[384] = {0};
        unsigned random = esp_random();
        portENTER_CRITICAL(&lock);
        unsigned next = music_target(state.count, state.selected, 1, true, state.repeat_mode, state.shuffle, random, shuffle_played);
        if (!state.scanning && next < state.count)
            snprintf(next_path, sizeof(next_path), "%s", catalogs[catalog_set][next].path);
        portEXIT_CRITICAL(&lock);
        if (next_path[0]) m3_audio_music_follow(next_path);
    }
}
