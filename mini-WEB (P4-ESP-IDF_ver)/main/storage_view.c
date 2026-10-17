#include "storage_view.h"
#include "sd_media.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
        (folder[0] && folder[0] != '/') || (!sd && folder[0])) {
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
    if (!dir) { view->error = ESP_FAIL; return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[sizeof(directory) + 257];
        length = snprintf(path, sizeof(path), "%s/%.255s", directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0 || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) continue;
        unsigned index = view->count++;
        if (index < offset || view->shown == STORAGE_VIEW_ROWS) continue;
        storage_view_entry_t *item = &view->entries[view->shown++];
        snprintf(item->name, sizeof(item->name), "%.255s", entry->d_name);
        item->directory = S_ISDIR(st.st_mode);
        item->bytes = item->directory ? 0 : (uint64_t)st.st_size;
    }
    closedir(dir);
}
