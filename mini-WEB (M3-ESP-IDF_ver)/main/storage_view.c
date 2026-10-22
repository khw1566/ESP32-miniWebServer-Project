#include "storage_view.h"
#include "m3_utf8.h"
#include "m3_recorder.h"
#include "m3_tube.h"
#include "sd_media.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static bool mutation_path(const char *path) {
    if (!path || path[0] != '/' || !path[1] || strlen(path) > 255 || !m3_utf8_valid(path)) return false;
    unsigned part = 1;
    for (unsigned i = 1;; ++i) {
        unsigned char c = path[i];
        if (c < 32 && c) return false;
        if (strchr("\\:*?\"<>|", c) && c) return false;
        if (c == '/' || !c) {
            unsigned n = i - part;
            if (!n || path[part] == '.' || path[i - 1] == '.' || path[i - 1] == ' ') return false;
            if (!c) break;
            part = i + 1;
        }
    }
    return true;
}
esp_err_t storage_view_mutate(bool sd, const char *source, const char *destination, bool remove) {
    if (!sd) return ESP_ERR_NOT_SUPPORTED;
    if (!mutation_path(source) || (!remove && !mutation_path(destination))) return ESP_ERR_INVALID_ARG;
    if (!sd && (strchr(source + 1, '/') || (!remove && strchr(destination + 1, '/')))) return ESP_ERR_INVALID_ARG;
    const char *root = SD_MEDIA_ROOT;
    /* Incomplete uploads/recordings have an active owner and are never moved here. */
    const char *ext = strrchr(source, '.');
    if (ext && !strcmp(ext, ".tmp")) return ESP_ERR_INVALID_STATE;
    char from[272], to[272];
    snprintf(from, sizeof(from), "%s%s", root, source);
    struct stat st;
    if (stat(from, &st)) return ESP_ERR_NOT_FOUND;
    if (remove) return (S_ISDIR(st.st_mode) ? rmdir(from) : unlink(from)) == 0 ? ESP_OK : ESP_FAIL;
    size_t length = strlen(source);
    if (!strcmp(source, destination) || (S_ISDIR(st.st_mode) && !strncmp(source, destination, length) && destination[length] == '/')) return ESP_ERR_INVALID_ARG;
    snprintf(to, sizeof(to), "%s%s", root, destination);
    if (!stat(to, &st) || errno != ENOENT) return ESP_ERR_INVALID_STATE;
    char parent[272]; snprintf(parent, sizeof(parent), "%s", to);
    char *slash = strrchr(parent, '/'); if (!slash) return ESP_ERR_INVALID_ARG; *slash = 0;
    if (sd && (stat(parent, &st) || !S_ISDIR(st.st_mode))) return ESP_ERR_NOT_FOUND;
    if (ext && !strcmp(ext, ".part")) {
        if (!sd) return ESP_ERR_INVALID_ARG;
        /* Recover only this recorder's format, with a destination ending in .wav. */
        size_t n = strlen(source);
        if (strncmp(source, M3_RECORDER_FOLDER "/", sizeof(M3_RECORDER_FOLDER)) || n < 10 ||
            strcmp(source + n - 9, ".wav.part") || strlen(destination) != n - 5 || strncmp(source, destination, n - 5)) return ESP_ERR_INVALID_ARG;
        FILE *file = fopen(from, "r+b"); if (!file) return ESP_FAIL;
        uint8_t header[44], expected[44]; bool ok = false;
        if (fread(header, 1, 44, file) == 44 && !fseek(file, 0, SEEK_END)) {
            long size = ftell(file);
            m3_recorder_wav_header(expected, 0);
            if (size > 44 && size <= (long)(M3_RECORDER_MAX_SECONDS * M3_RECORDER_RATE * 2 + 44) && !(size & 1) &&
                !memcmp(header, expected, 4) && !memcmp(header + 8, expected + 8, 32)) {
                m3_recorder_wav_header(header, (uint32_t)size - 44);
                ok = !fseek(file, 0, SEEK_SET) && fwrite(header, 1, 44, file) == 44 && !fflush(file) && !fsync(fileno(file));
            }
        }
        if (fclose(file)) ok = false;
        if (!ok) return ESP_FAIL;
    }
    return rename(from, to) == 0 ? ESP_OK : ESP_FAIL;
}

void storage_capacity_read(bool sd, storage_capacity_t *view) {
    memset(view, 0, sizeof(*view));
    if (!sd) { view->error = ESP_ERR_NOT_SUPPORTED; return; }
    if (!sd_media_ready()) { view->error = ESP_ERR_INVALID_STATE; return; }
    uint64_t free_bytes = 0;
    view->error = esp_vfs_fat_info(SD_MEDIA_ROOT, &view->total, &free_bytes);
    if (view->error == ESP_OK) view->used = view->total >= free_bytes ? view->total-free_bytes : 0;
}
void storage_view_read(bool sd, const char *folder, unsigned offset, storage_view_t *view) {
    memset(view, 0, sizeof(*view));
    view->offset = offset;
    if (!folder || strlen(folder) > 255 || strstr(folder, "..") ||
        (folder[0] && folder[0] != '/') || (!sd && folder[0]) ||
        (sd && folder[0] && strcmp(folder, "/") && !mutation_path(folder))) {
        view->error = ESP_ERR_INVALID_ARG;
        return;
    }
    storage_capacity_t capacity;
    storage_capacity_read(sd, &capacity);
    view->total = capacity.total;
    view->used = capacity.used;
    view->reserved = capacity.reserved;
    view->error = capacity.error;
    if (view->error != ESP_OK) return;
    const char *base = SD_MEDIA_ROOT;
    char directory[272];
    int length = snprintf(directory, sizeof(directory), "%s%s", base, folder);
    if (length < 0 || (size_t)length >= sizeof(directory)) { view->error = ESP_ERR_INVALID_SIZE; return; }
    DIR *dir = opendir(directory);
    if (!dir) {
        if (sd && !strcmp(folder, M3_TUBE_DOWNLOAD_FOLDER) && errno == ENOENT) return;
        view->error = ESP_FAIL; return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        /* Interrupted downloads are never presented as saved videos. */
        const char *extension = strrchr(entry->d_name, '.');
        if (sd && !strcmp(folder, M3_TUBE_DOWNLOAD_FOLDER) && extension && !strcmp(extension, ".part")) continue;
        /* Names are also used for navigation: never turn a truncated UTF-8 name
         * into a different filesystem path. LVGL handles visual ellipsis. */
        if (strlen(entry->d_name) >= sizeof(view->entries[0].name) || !m3_utf8_valid(entry->d_name)) continue;
        char path[sizeof(directory) + 257];
        length = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0 || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) continue;
        unsigned index = view->count++;
        if (index < offset || view->shown == STORAGE_VIEW_ROWS) continue;
        storage_view_entry_t *item = &view->entries[view->shown++];
        memcpy(item->name, entry->d_name, strlen(entry->d_name) + 1);
        item->directory = S_ISDIR(st.st_mode);
        item->bytes = item->directory ? 0 : (uint64_t)st.st_size;
    }
    closedir(dir);
}
