#include "m3_mp4_demux.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

/* Long files: ~3 h of 60 fps video or ~4 h of 48 kHz AAC per track; the index lives in PSRAM. */
enum { MOOV_LIMIT = 16 * 1024 * 1024, INDEX_BUDGET = 16 * 1024 * 1024, SAMPLE_LIMIT = 700000,
    VIDEO_SAMPLE_LIMIT = 1024 * 1024, AUDIO_SAMPLE_LIMIT = 8192, DURATION_LIMIT_S = 4 * 3600 };
typedef union allocation allocation_t;
typedef struct { allocation_t *head; size_t used; bool failed; } pool_t;
union allocation { max_align_t alignment; struct { allocation_t *next, *previous; pool_t *pool; size_t bytes; } item; };
static pool_t *allocating; /* Protected while minimp4 builds either playback or thumbnail indexes. */
static atomic_flag indexing = ATOMIC_FLAG_INIT;
static void *mp4_alloc(size_t bytes) {
    if (!allocating || !bytes || bytes > INDEX_BUDGET || allocating->used > INDEX_BUDGET - bytes) {
        if (allocating) allocating->failed = true;
        return NULL;
    }
    allocation_t *a = heap_caps_malloc(sizeof(*a) + bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a) { allocating->failed = true; return NULL; }
    a->item = (typeof(a->item)){.next = allocating->head, .pool = allocating, .bytes = bytes};
    if (a->item.next) a->item.next->item.previous = a;
    allocating->head = a; allocating->used += bytes;
    return a + 1;
}
static void mp4_free(void *pointer) {
    if (!pointer) return;
    allocation_t *a = (allocation_t *)pointer - 1; pool_t *pool = a->item.pool;
    if (a->item.previous) a->item.previous->item.next = a->item.next;
    else pool->head = a->item.next;
    if (a->item.next) a->item.next->item.previous = a->item.previous;
    pool->used -= a->item.bytes; free(a);
}
static void *mp4_realloc(void *pointer, size_t bytes) {
    if (!pointer) return mp4_alloc(bytes);
    if (!bytes) { mp4_free(pointer); return NULL; }
    allocation_t *old = (allocation_t *)pointer - 1;
    void *next = mp4_alloc(bytes);
    if (next) { memcpy(next, pointer, bytes < old->item.bytes ? bytes : old->item.bytes); mp4_free(pointer); }
    return next;
}
#define malloc mp4_alloc
#define realloc mp4_realloc
#define free mp4_free
#define MINIMP4_IMPLEMENTATION
/* Upstream's byte-reader switch intentionally falls through. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "../third_party/minimp4/minimp4.h"
#pragma GCC diagnostic pop
#undef malloc
#undef realloc
#undef free

#define BOX(a,b,c,d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))
typedef struct {
    uint32_t seen, timescale, trim, samples;
    uint32_t *sync; /* Zero-based stss entries; absent means every sample is a sync sample. */
    unsigned nal_length, width, height, sync_count;
    bool avc, audio;
} track_probe_t;
struct m3_mp4_demux {
    MP4D_demux_t index;
    pool_t pool;
    FILE *file;
    uint64_t size, moov_offset;
    uint8_t *moov;
    size_t moov_size;
    struct { uint64_t first, end; } media[8];
    unsigned media_count, tracks, job;
    int video, audio;
    bool (*current)(unsigned);
    track_probe_t probe[4];
};
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static bool read_at(m3_mp4_demux_t *d, uint64_t offset, void *buffer, size_t bytes) {
    if (offset > d->size || bytes > d->size - offset || offset > LONG_MAX) return false;
    size_t copied = 0;
    while (copied < bytes && d->current(d->job)) {
        size_t block = bytes - copied; if (block > 16384) block = 16384;
        if (!sd_media_admin_lock()) return false;
        bool ok = !fseek(d->file, (long)(offset + copied), SEEK_SET) && fread((uint8_t *)buffer + copied, 1, block, d->file) == block;
        sd_media_admin_unlock();
        if (!ok) return false;
        copied += block;
    }
    return copied == bytes;
}
static int index_read(int64_t offset, void *buffer, size_t bytes, void *token) {
    m3_mp4_demux_t *d = token;
    if (offset < 0 || !d->current(d->job)) return -1;
    uint64_t at = offset;
    if (at >= d->moov_offset && at - d->moov_offset <= d->moov_size && bytes <= d->moov_size - (size_t)(at - d->moov_offset)) {
        memcpy(buffer, d->moov + (size_t)(at - d->moov_offset), bytes); return 0;
    }
    return read_at(d, at, buffer, bytes) ? 0 : -1;
}
static bool avcc(track_probe_t *track, const uint8_t *p, size_t size) {
    if (track->avc || size < 7 || size > 4096 || p[0] != 1 || p[1] != 66 || !(p[2] & 0x40) || (p[4] & 3) != 3) return false;
    size_t at = 6; if ((p[5] & 31) != 1) return false;
    if (at + 2 > size) return false;
    unsigned length = ((unsigned)p[at] << 8) | p[at + 1]; at += 2;
    if (!length || length > size - at || !m3_mp4_sps_valid(p + at, length, &track->width, &track->height)) return false;
    at += length;
    if (at >= size || p[at++] != 1 || at + 2 > size) return false;
    length = ((unsigned)p[at] << 8) | p[at + 1]; at += 2;
    if (!length || length > 1024 || length > size - at || (p[at] & 31) != 8 || at + length != size) return false;
    track->avc = true; track->nal_length = 4; return true;
}
/* Validate box bounds/table counts before handing untrusted sizes to minimp4.
 * Only ordinary, nonfragmented avc1/mp4a files enter the decoder path. */
static bool boxes(m3_mp4_demux_t *d, const uint8_t *data, size_t bytes, unsigned depth, track_probe_t *track) {
    if (depth > 10) return false;
    size_t at = 0;
    while (at < bytes) {
        if (!d->current(d->job) || bytes - at < 8) return false;
        uint64_t length = be32(data + at); unsigned header = 8;
        uint32_t type = be32(data + at + 4);
        if (length == 1) { if (bytes - at < 16) return false; length = be64(data + at + 8); header = 16; }
        if (length < header || length > bytes - at) return false;
        const uint8_t *p = data + at + header; size_t n = (size_t)length - header;
        unsigned flag = type == BOX('s','t','s','d') ? 1 : type == BOX('s','t','t','s') ? 2 : type == BOX('s','t','s','z') ? 4 :
            type == BOX('s','t','s','c') ? 8 : type == BOX('s','t','c','o') || type == BOX('c','o','6','4') ? 16 :
            type == BOX('c','t','t','s') ? 32 : type == BOX('m','d','h','d') ? 64 : type == BOX('h','d','l','r') ? 128 : type == BOX('e','l','s','t') ? 256 :
            type == BOX('s','t','s','s') ? 512 : 0;
        if (flag) { if (!track || (track->seen & flag)) return false; track->seen |= flag; }
        if (type == BOX('m','v','e','x') || type == BOX('m','o','o','f') || type == BOX('s','i','n','f') || type == BOX('s','e','n','c')) return false;
        if (type == BOX('t','r','a','k')) {
            if (track || d->tracks >= 4 || !boxes(d, p, n, depth + 1, &d->probe[d->tracks++])) return false;
        } else if (type == BOX('m','o','o','v') || type == BOX('m','d','i','a') || type == BOX('m','i','n','f') ||
            type == BOX('s','t','b','l') || type == BOX('e','d','t','s')) {
            if (!boxes(d, p, n, depth + 1, track)) return false;
        } else if (type == BOX('s','t','s','d')) {
            if (n < 16 || be32(p) || be32(p + 4) != 1 || be32(p + 8) != n - 8) return false;
            uint32_t codec = be32(p + 12);
            if (codec == BOX('a','v','c','1')) {
                if (n < 94 || !boxes(d, p + 94, n - 94, depth + 1, track)) return false;
            } else if (codec == BOX('m','p','4','a')) {
                if (n < 44 || p[24] || p[25] || !boxes(d, p + 44, n - 44, depth + 1, track)) return false;
                track->audio = true;
            } else return false;
        } else if (type == BOX('a','v','c','C')) {
            if (!track || !avcc(track, p, n)) return false;
        } else if (type == BOX('m','d','h','d')) {
            unsigned pos = n && p[0] == 1 ? 20 : 12;
            if (n < pos + 8 || p[0] > 1 || !(track->timescale = be32(p + pos))) return false;
        } else if (type == BOX('e','l','s','t')) {
            if (n != 20 || be32(p) || be32(p + 4) != 1 || be32(p + 12) > INT32_MAX || be32(p + 16) != 0x10000) return false;
            track->trim = be32(p + 12);
        } else if (type == BOX('s','t','s','s')) {
            if (n < 8 || be32(p)) return false;
            unsigned count = be32(p + 4);
            if (!count || count > SAMPLE_LIMIT || n != 8 + (uint64_t)count * 4) return false;
            track->sync = heap_caps_malloc((size_t)count * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!track->sync) return false;
            for (unsigned i = 0; i < count; ++i) {
                uint32_t sample = be32(p + 8 + i * 4);
                if (!sample || (i && sample - 1 <= track->sync[i - 1])) return false;
                track->sync[i] = sample - 1;
            }
            track->sync_count = count;
        } else if (type == BOX('s','t','s','z')) {
            if (n < 12 || be32(p)) return false;
            unsigned count = be32(p + 8), fixed = be32(p + 4);
            if (!count || count > SAMPLE_LIMIT || n != 12 + (fixed ? 0 : (uint64_t)count * 4)) return false;
            track->samples = count;
        } else if (type == BOX('s','t','t','s') || type == BOX('s','t','s','c') || type == BOX('s','t','c','o') ||
            type == BOX('c','o','6','4') || type == BOX('c','t','t','s')) {
            if (n < 8 || be32(p)) return false;
            unsigned count = be32(p + 4);
            unsigned unit = type == BOX('s','t','s','c') ? 12 : type == BOX('s','t','c','o') ? 4 : 8;
            if (!count || count > SAMPLE_LIMIT || n != 8 + (uint64_t)count * unit) return false;
            uint64_t samples = 0, duration = 0; unsigned previous = 0;
            for (unsigned i = 0; i < count; ++i) {
                const uint8_t *entry = p + 8 + i * unit;
                if (type == BOX('s','t','t','s')) {
                    unsigned amount = be32(entry), delta = be32(entry + 4);
                    samples += amount; duration += (uint64_t)amount * delta;
                    if (!amount || !delta || samples > SAMPLE_LIMIT || duration > UINT32_MAX) return false;
                } else if (type == BOX('c','t','t','s')) {
                    if (!be32(entry) || be32(entry + 4)) return false;
                } else if (type == BOX('s','t','s','c')) {
                    unsigned first = be32(entry);
                    if ((!i && first != 1) || first <= previous || !be32(entry + 4) || be32(entry + 4) > SAMPLE_LIMIT || be32(entry + 8) != 1) return false;
                    previous = first;
                }
            }
        }
        at += (size_t)length;
    }
    return at == bytes;
}
static bool sample_valid(m3_mp4_demux_t *d, bool video, unsigned index, m3_mp4_sample_t *sample) {
    int selected = video ? d->video : d->audio;
    if (selected < 0) return false;
    MP4D_track_t *track = &d->index.track[selected];
    if (index >= track->sample_count) return false;
    unsigned bytes = 0, timestamp = 0, duration = 0;
    uint64_t offset = MP4D_frame_offset(&d->index, selected, index, &bytes, &timestamp, &duration);
    if (!bytes || bytes > (video ? VIDEO_SAMPLE_LIMIT : AUDIO_SAMPLE_LIMIT) || !duration) return false;
    bool inside = false;
    for (unsigned i = 0; i < d->media_count; ++i)
        if (offset >= d->media[i].first && offset <= d->media[i].end && bytes <= d->media[i].end - offset) inside = true;
    if (!inside) return false;
    *sample = (m3_mp4_sample_t){.offset = offset, .bytes = bytes,
        .pts_us = ((int64_t)timestamp - d->probe[selected].trim) * 1000000 / track->timescale,
        .duration_us = (uint64_t)duration * 1000000 / track->timescale};
    return true;
}
bool m3_mp4_demux_sample(m3_mp4_demux_t *d, bool video, unsigned index, m3_mp4_sample_t *sample) {
    return d && sample && sample_valid(d, video, index, sample);
}
bool m3_mp4_demux_read(m3_mp4_demux_t *d, const m3_mp4_sample_t *sample, uint8_t *out, size_t capacity) {
    return d && sample && out && sample->bytes <= capacity && read_at(d, sample->offset, out, sample->bytes);
}
static int64_t video_pts(const m3_mp4_demux_t *d, unsigned sample) {
    const MP4D_track_t *t = &d->index.track[d->video];
    return ((int64_t)t->timestamp[sample] - d->probe[d->video].trim) * 1000000 / t->timescale;
}
unsigned m3_mp4_demux_keyframe(m3_mp4_demux_t *d, unsigned index, int64_t until_us) {
    if (!d || d->video < 0) return index;
    const track_probe_t *probe = &d->probe[d->video];
    unsigned count = probe->sync ? probe->sync_count : d->index.track[d->video].sample_count;
    /* Timestamps were validated as nondecreasing, so the last sync sample at or before until_us is found by bisection. */
    unsigned low = 0, high = count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2, sample = probe->sync ? probe->sync[middle] : middle;
        if (video_pts(d, sample) <= until_us) low = middle + 1; else high = middle;
    }
    if (!low) return index;
    unsigned sample = probe->sync ? probe->sync[low - 1] : low - 1;
    return sample > index ? sample : index;
}
const uint8_t *m3_mp4_demux_parameter(m3_mp4_demux_t *d, bool pps, unsigned index, int *size) {
    if (!d || d->video < 0 || !size || index) return NULL;
    return pps ? MP4D_read_pps(&d->index, d->video, index, size) : MP4D_read_sps(&d->index, d->video, index, size);
}
void m3_mp4_demux_close(m3_mp4_demux_t *d) {
    if (!d) return;
    /* Every allocation carries its own pool; close only frees and never selects an allocator. */
    MP4D_close(&d->index);
    while (d->pool.head) mp4_free(d->pool.head + 1);
    for (unsigned i = 0; i < 4; ++i) free(d->probe[i].sync);
    if (d->file) fclose(d->file);
    free(d->moov); free(d);
}
static bool audio_config(const MP4D_track_t *track, m3_mp4_info_t *info) {
    /* FFmpeg's optional sync extension explicitly says SBR is absent. */
    if (!track->dsi || (track->dsi_bytes != 2 && (track->dsi_bytes != 5 || memcmp(track->dsi + 2, "\x56\xe5\0", 3))) ||
        (track->dsi[0] >> 3) != 2 || (track->dsi[1] & 7)) return false;
    unsigned frequency = ((track->dsi[0] & 7) << 1) | (track->dsi[1] >> 7), channels = (track->dsi[1] >> 3) & 15;
    static const unsigned rates[] = {96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
    if (frequency >= 12 || rates[frequency] > 48000 || channels < 1 || channels > 2 ||
        track->SampleDescription.audio.samplerate_hz != rates[frequency] || track->SampleDescription.audio.channelcount != channels) return false;
    info->sample_rate = rates[frequency]; info->channels = channels; info->audio_samples = track->sample_count;
    return true;
}
m3_mp4_result_t m3_mp4_demux_open(const char *relative, unsigned job, bool (*current)(unsigned), m3_mp4_demux_t **out, m3_mp4_info_t *info) {
    if (!out || !info || !current || !relative || relative[0] != '/' || strlen(relative) > 255 || strstr(relative, "..") || strchr(relative, '\\')) return M3_MP4_FORMAT;
    *out = NULL; memset(info, 0, sizeof(*info));
    m3_mp4_demux_t *d = heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!d) return M3_MP4_MEMORY;
    d->job = job; d->current = current; d->video = d->audio = -1;
    m3_mp4_result_t result = M3_MP4_FORMAT;
    char path[272]; snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, relative);
    if (!sd_media_admin_lock()) { result = M3_MP4_STORAGE; goto done; }
    d->file = fopen(path, "rb"); long length = 0;
    if (d->file && !fseek(d->file, 0, SEEK_END)) length = ftell(d->file);
    sd_media_admin_unlock();
    /* ftell returns -1 past the 2 GiB long range, which also rejects those files. */
    if (!d->file || length < 16) { result = M3_MP4_STORAGE; goto done; }
    d->size = length;
    bool ftyp = false; unsigned roots = 0;
    for (uint64_t at = 0; at < d->size;) {
        uint8_t header[16]; if (++roots > 1024 || d->size - at < 8 || !read_at(d, at, header, 8)) goto done;
        uint64_t bytes = be32(header); unsigned head = 8; uint32_t type = be32(header + 4);
        if (bytes == 1) { if (d->size - at < 16 || !read_at(d, at + 8, header + 8, 8)) goto done; bytes = be64(header + 8); head = 16; }
        else if (!bytes) bytes = d->size - at;
        if (bytes < head || bytes > d->size - at || type == BOX('m','o','o','f')) goto done;
        if (type == BOX('f','t','y','p')) ftyp = true;
        if (type == BOX('m','o','o','v')) {
            if (d->moov || bytes > MOOV_LIMIT) goto done;
            d->moov = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!d->moov) { result = M3_MP4_MEMORY; goto done; }
            d->moov_offset = at; d->moov_size = bytes;
            if (!read_at(d, at, d->moov, bytes) || !boxes(d, d->moov + head, bytes - head, 0, NULL)) goto done;
        } else if (type == BOX('m','d','a','t')) {
            if (d->media_count >= 8) goto done;
            d->media[d->media_count].first = at + head; d->media[d->media_count++].end = at + bytes;
        }
        at += bytes;
    }
    if (!ftyp || !d->moov || !d->tracks || !d->media_count) goto done;
    while (atomic_flag_test_and_set(&indexing)) {
        if (!current(job)) { result = M3_MP4_STOPPED; goto done; }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    allocating = &d->pool;
    bool indexed = MP4D_open(&d->index, index_read, d, d->size) != 0;
    allocating = NULL;
    atomic_flag_clear(&indexing);
    if (!indexed) { if (d->pool.failed) result = M3_MP4_MEMORY; goto done; }
    if (d->index.track_count != d->tracks) goto done;
    for (unsigned i = 0; i < d->tracks; ++i) {
        MP4D_track_t *t = &d->index.track[i]; track_probe_t *p = &d->probe[i];
        if ((p->seen & 223) != 223 || !t->timescale || t->timescale != p->timescale || p->trim > (uint64_t)t->timescale * 2 ||
            t->sample_count != p->samples || t->timestamp_count != t->sample_count || !t->entry_size || !t->timestamp || !t->duration || !t->sample_to_chunk || !t->chunk_offset) goto done;
        if (t->handler_type == MP4D_HANDLER_TYPE_VIDE && p->avc && t->object_type_indication == MP4_OBJECT_TYPE_AVC && d->video < 0) {
            d->video = i; info->width = p->width; info->height = p->height; info->nal_length = p->nal_length; info->video_samples = t->sample_count;
            if (t->SampleDescription.video.width != p->width || t->SampleDescription.video.height != p->height) goto done;
        } else if (t->handler_type == MP4D_HANDLER_TYPE_SOUN && p->audio && t->object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 && d->audio < 0) {
            if (!audio_config(t, info)) goto done;
            d->audio = i;
        } else goto done;
        if (p->sync && (int)i == d->video && p->sync[p->sync_count - 1] >= t->sample_count) goto done;
        uint32_t previous = 0;
        for (unsigned s = 0; s < t->sample_count; ++s) {
            if (!current(job) || (s && t->timestamp[s] < previous) || !t->duration[s] || t->duration[s] > t->timescale ||
                (uint64_t)t->timestamp[s] + t->duration[s] > (uint64_t)t->timescale * DURATION_LIMIT_S) goto done;
            previous = t->timestamp[s];
            if (!(s & 4095)) vTaskDelay(1); /* Long indexes must not starve the idle task. */
        }
    }
    if (d->video < 0) goto done;
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned count = pass ? info->audio_samples : info->video_samples;
        for (unsigned i = 0; i < count; ++i) {
            m3_mp4_sample_t sample; if (!sample_valid(d, !pass, i, &sample)) goto done;
            if (!(i & 4095)) { if (!current(job)) goto done; vTaskDelay(1); }
        }
    }
    m3_mp4_sample_t last;
    if (!sample_valid(d, true, info->video_samples - 1, &last) || last.pts_us + (int64_t)last.duration_us <= 0) goto done;
    info->duration_ms = (last.pts_us + last.duration_us) / 1000;
    free(d->moov); d->moov = NULL; d->moov_size = 0;
    *out = d; return M3_MP4_OK;
done:
    if (!current(job)) result = M3_MP4_STOPPED;
    m3_mp4_demux_close(d); return result;
}
