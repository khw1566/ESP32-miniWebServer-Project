/* Included by m3_gallery.c: one worker owns catalog traversal and silent thumbnails. */
static M3_PSRAM_BSS m3_gallery_library_t library_state, library_next;
static M3_PSRAM_BSS uint8_t thumbnail_mailbox[M3_GALLERY_THUMB_BYTES];
static M3_PSRAM_BSS DIR *library_dirs[128];
static M3_PSRAM_BSS unsigned library_lengths[128];
/* 0 free, 1 writing, 2 ready, 3 UI copying. Control remains in internal RAM. */
static atomic_uint thumbnail_owner;
static unsigned thumbnail_generation, thumbnail_index;

bool m3_gallery_path_valid(const char *path) {
    if (!path || path[0] != '/' || !path[1] || strlen(path) > 255 || !m3_utf8_valid(path)) return false;
    for (const char *p = path; *p; ++p)
        if ((unsigned char)*p < 32 || *p == 127 || *p == '\\' || *p == ':' ||
            (*p == '/' && (!p[1] || p[1] == '/' || p[1] == '.'))) return false;
    return true;
}
static bool library_current(unsigned job) {
    return atomic_load(&library_active) && job == atomic_load(&generation);
}
void m3_gallery_library_state(m3_gallery_library_t *out) {
    portENTER_CRITICAL(&lock); *out = library_state; portEXIT_CRITICAL(&lock);
}
bool m3_gallery_library_request(unsigned page) {
    if (!requests || page > UINT_MAX / M3_GALLERY_TILES) return false;
    request_t request = {.page = page, .library = true, .generation = atomic_fetch_add(&generation, 1) + 1};
    atomic_store(&active, false); atomic_store(&library_active, true); atomic_store(&mp4_selected, false);
    portENTER_CRITICAL(&lock);
    library_state.status = M3_GALLERY_LOADING; library_state.generation = request.generation;
    library_state.page = page; library_state.shown = 0; library_state.done_mask = library_state.error_mask = 0;
    ++library_state.revision;
    portEXIT_CRITICAL(&lock);
    if (xQueueOverwrite(requests, &request) == pdPASS) return true;
    atomic_store(&library_active, false); return false;
}
bool m3_gallery_thumbnail_copy(unsigned job, unsigned index, uint8_t *out, size_t capacity) {
    if (!out || capacity < M3_GALLERY_THUMB_BYTES || index >= M3_GALLERY_TILES) return false;
    unsigned expected = 2;
    if (!atomic_compare_exchange_strong(&thumbnail_owner, &expected, 3)) return false;
    if (thumbnail_generation != job || !library_current(job)) { atomic_store(&thumbnail_owner, 0); return false; }
    if (thumbnail_index != index) { atomic_store(&thumbnail_owner, 2); return false; }
    memcpy(out, thumbnail_mailbox, M3_GALLERY_THUMB_BYTES);
    atomic_store(&thumbnail_owner, 0);
    return true;
}
static void library_publish(unsigned job) {
    portENTER_CRITICAL(&lock);
    if (library_current(job)) { library_next.revision = library_state.revision + 1; library_state = library_next; }
    portEXIT_CRITICAL(&lock);
}
/* Linear depth-first traversal. FAT directory handles do not occupy regular-file descriptors.
 * 128 levels cover every nonempty-component path that fits the 255-byte path contract. */
static bool library_scan(unsigned page, unsigned job) {
    memset(&library_next, 0, sizeof(library_next)); library_next.generation = job; library_next.page = page;
    library_next.status = M3_GALLERY_READY;
    if (!sd_media_ready() || !sd_media_admin_lock()) return false;
    library_dirs[0] = opendir(SD_MEDIA_ROOT); sd_media_admin_unlock();
    if (!library_dirs[0]) return false;
    unsigned depth = 0, visited = 0; char relative[256] = "";
    library_lengths[0] = 0;
    bool ok = true;
    while (library_current(job)) {
        if (!sd_media_admin_lock()) { ok = false; break; }
        errno = 0; struct dirent *entry = readdir(library_dirs[depth]);
        if (!entry) {
            bool failed = errno != 0;
            closedir(library_dirs[depth]); library_dirs[depth] = NULL; sd_media_admin_unlock();
            if (failed) { ok = false; break; }
            if (!depth) break;
            relative[library_lengths[--depth]] = 0; continue;
        }
        char child[256], path[272]; struct stat st;
        int n = snprintf(child, sizeof(child), "%s/%s", relative, entry->d_name);
        bool usable = n > 0 && (size_t)n < sizeof(child) && m3_gallery_path_valid(child);
        if (usable) {
            snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, child);
            usable = !stat(path, &st);
        }
        if (usable && S_ISDIR(st.st_mode) && depth + 1 < 128) {
            DIR *dir = opendir(path);
            if (dir) { ++depth; library_dirs[depth] = dir; library_lengths[depth] = n; memcpy(relative, child, n + 1); }
            else ok = false;
        } else if (usable && S_ISREG(st.st_mode) && m3_gallery_media(child)) {
            unsigned index = library_next.count++;
            if (index >= page * M3_GALLERY_TILES && library_next.shown < M3_GALLERY_TILES) {
                m3_gallery_item_t *item = &library_next.items[library_next.shown++];
                memcpy(item->path, child, n + 1); item->video = m3_gallery_video(child);
            }
        }
        sd_media_admin_unlock();
        if (++visited % 16 == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    for (unsigned i = 0; i <= depth; ++i) if (library_dirs[i]) { closedir(library_dirs[i]); library_dirs[i] = NULL; }
    return ok && library_current(job);
}
static bool library_image(const char *relative, unsigned job, uint8_t *canvas) {
    char path[272]; snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, relative);
    if (!sd_media_admin_lock()) return false;
    FILE *file = fopen(path, "rb"); long length = 0;
    if (file && !fseek(file, 0, SEEK_END)) { length = ftell(file); if (fseek(file, 0, SEEK_SET)) length = 0; }
    sd_media_admin_unlock();
    bool video = m3_gallery_video(relative), ok = false;
    uint8_t *input = NULL;
    if (!file || length <= 0 || (!video && length > INPUT_LIMIT)) goto done;
    const char *extension = strrchr(relative, '.');
    if (video && extension && !strcasecmp(extension, ".avi")) {
        uint8_t header[4096]; bool mjpeg = false;
        if (!sd_media_admin_lock()) goto done;
        size_t n = fread(header, 1, sizeof(header), file);
        if (n >= 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "AVI ", 4))
            for (size_t i = 12; i + 4 <= n; ++i) if (!memcmp(header + i, "MJPG", 4) || !memcmp(header + i, "mjpg", 4)) { mjpeg = true; break; }
        if (fseek(file, 0, SEEK_SET)) mjpeg = false;
        sd_media_admin_unlock();
        if (!mjpeg) goto done;
    }
    size_t capacity = 0;
    input = jpeg_alloc_decoder_mem(video ? JPEG_LIMIT : length,
        &(jpeg_decode_memory_alloc_cfg_t){.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER}, &capacity);
    if (!input || capacity < (video ? JPEG_LIMIT : (size_t)length)) goto done;
    size_t size = 0;
    if (video) {
        if (!sd_media_admin_lock()) goto done;
        size = next_jpeg(file, input, job, library_current); sd_media_admin_unlock();
    } else while (size < (size_t)length && library_current(job)) {
        size_t chunk = length - size; if (chunk > 32768) chunk = 32768;
        if (!sd_media_admin_lock()) goto done;
        size_t got = fread(input + size, 1, chunk, file); sd_media_admin_unlock();
        if (got != chunk) goto done;
        size += got; vTaskDelay(pdMS_TO_TICKS(1));
    }
    ok = size && library_current(job) && (video || size == (size_t)length) && m3_photo_decode(input, size, canvas);
done:
    if (file) fclose(file);
    free(input); return ok;
}
static bool library_gif(const char *relative, unsigned job, uint8_t *canvas) {
    m3_gif_t *gif = NULL;
    m3_gif_result_t result = m3_gif_open(relative, job, library_current, &gif);
    if (result == M3_GIF_OK) result = m3_gif_frame(gif, canvas, NULL);
    m3_gif_close(gif);
    return result == M3_GIF_OK || result == M3_GIF_END;
}
static void thumbnail_scale(const uint8_t *canvas, uint8_t *out) {
    /* A square crop from the already aspect-fitted canvas preserves pixel proportions. */
    unsigned edge = M3_PHOTO_W < M3_PHOTO_H ? M3_PHOTO_W : M3_PHOTO_H;
    unsigned left = (M3_PHOTO_W - edge) / 2, top = (M3_PHOTO_H - edge) / 2;
    for (unsigned y = 0; y < M3_GALLERY_THUMB_H; ++y) for (unsigned x = 0; x < M3_GALLERY_THUMB_W; ++x) {
        size_t source = ((top + y * edge / M3_GALLERY_THUMB_H) * M3_PHOTO_W + left + x * edge / M3_GALLERY_THUMB_W) * 2;
        size_t target = (y * M3_GALLERY_THUMB_W + x) * 2;
        out[target] = canvas[source]; out[target + 1] = canvas[source + 1];
    }
}
static void gallery_library_run(unsigned page, unsigned job) {
    if (!library_current(job)) return;
    if (!library_scan(page, job)) { library_next.status = M3_GALLERY_STORAGE; library_publish(job); return; }
    if (!library_next.shown && library_next.count) {
        page = (library_next.count - 1) / M3_GALLERY_TILES;
        if (!library_scan(page, job)) { library_next.status = M3_GALLERY_STORAGE; library_publish(job); return; }
    }
    library_publish(job);
    uint8_t *canvas = library_next.shown ? heap_caps_malloc(M3_PHOTO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (library_next.shown && !canvas) { library_next.status = M3_GALLERY_MEMORY; library_publish(job); return; }
    for (unsigned i = 0; i < library_next.shown && library_current(job); ++i) {
        const char *path = library_next.items[i].path, *ext = strrchr(path, '.');
        bool ok = m3_gallery_supported(path) && (!strcasecmp(ext, ".mp4") ? m3_mp4_thumbnail(path, job, library_current, canvas) == M3_MP4_OK :
            !strcasecmp(ext, ".gif") ? library_gif(path, job, canvas) : library_image(path, job, canvas));
        if (!library_current(job)) break;
        library_next.done_mask |= 1u << i;
        if (!ok) { library_next.error_mask |= 1u << i; library_publish(job); continue; }
        while (library_current(job)) {
            unsigned owner = atomic_load(&thumbnail_owner);
            if ((owner == 0 || owner == 2) && atomic_compare_exchange_strong(&thumbnail_owner, &owner, 1)) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!library_current(job)) { if (atomic_load(&thumbnail_owner) == 1) atomic_store(&thumbnail_owner, 0); break; }
        thumbnail_scale(canvas, thumbnail_mailbox); thumbnail_generation = job; thumbnail_index = i;
        atomic_store(&thumbnail_owner, 2); library_publish(job);
        while (library_current(job) && atomic_load(&thumbnail_owner) != 0) vTaskDelay(pdMS_TO_TICKS(5));
    }
    free(canvas);
}
