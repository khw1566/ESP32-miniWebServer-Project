#include "m4_audio_apps.h"
#include "esp_vfs.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { const char *path; const unsigned char *data; size_t size; } voice_asset_t;
#include "m4_tts_assets.inc"
typedef struct { bool used; const voice_asset_t *asset; size_t offset; } voice_fd_t;
static voice_fd_t descriptors[8];
static bool mounted;
static const char *const directories[] = {"", "/espeak-ng-data", "/espeak-ng-data/lang", "/espeak-ng-data/lang/gmw", "/espeak-ng-data/voices"};
typedef struct { char path[64]; unsigned next; struct dirent entry; } voice_dir_t;
static const voice_asset_t *find_asset(const char *path) {
    for (unsigned i = 0; i < sizeof(voice_assets) / sizeof(voice_assets[0]); ++i)
        if (!strcmp(path, voice_assets[i].path)) return &voice_assets[i];
    return NULL;
}
static int voice_stat(const char *path, struct stat *out) {
    memset(out, 0, sizeof(*out));
    const voice_asset_t *asset = find_asset(path);
    if (asset) { out->st_mode = S_IFREG | 0444; out->st_size = asset->size; return 0; }
    for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
        if (!strcmp(path, directories[i]) || (!strcmp(path, "/") && !i)) { out->st_mode = S_IFDIR | 0555; return 0; }
    errno = ENOENT; return -1;
}
static int voice_open(const char *path, int flags, int mode) {
    (void)mode;
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND))) { errno = EROFS; return -1; }
    const voice_asset_t *asset = find_asset(path);
    if (!asset) { errno = ENOENT; return -1; }
    for (unsigned i = 0; i < 8; ++i) if (!descriptors[i].used) {
        descriptors[i] = (voice_fd_t){.used = true, .asset = asset}; return (int)i;
    }
    errno = EMFILE; return -1;
}
static voice_fd_t *get_fd(int fd) {
    if (fd < 0 || fd >= 8 || !descriptors[fd].used) { errno = EBADF; return NULL; }
    return &descriptors[fd];
}
static ssize_t voice_read(int fd, void *data, size_t count) {
    voice_fd_t *file = get_fd(fd); if (!file) return -1;
    size_t left = file->offset < file->asset->size ? file->asset->size - file->offset : 0;
    if (count > left) count = left;
    if (count) memcpy(data, file->asset->data + file->offset, count);
    file->offset += count; return (ssize_t)count;
}
static off_t voice_seek(int fd, off_t offset, int whence) {
    voice_fd_t *file = get_fd(fd); if (!file) return -1;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
    int64_t position = offset + (int64_t)(whence == SEEK_SET ? 0 : whence == SEEK_CUR ? file->offset : file->asset->size);
    if (position < 0 || position > INT32_MAX) { errno = EINVAL; return -1; }
    file->offset = (size_t)position; return (off_t)position;
}
static int voice_close(int fd) { voice_fd_t *file = get_fd(fd); if (!file) return -1; file->used = false; return 0; }
static int voice_fstat(int fd, struct stat *out) { voice_fd_t *file = get_fd(fd); return file ? voice_stat(file->asset->path, out) : -1; }
static DIR *voice_opendir(const char *path) {
    struct stat st;
    if (voice_stat(path, &st) || !S_ISDIR(st.st_mode)) { errno = ENOTDIR; return NULL; }
    voice_dir_t *dir = calloc(1, sizeof(*dir));
    if (!dir) { errno = ENOMEM; return NULL; }
    snprintf(dir->path, sizeof(dir->path), "%s", !strcmp(path, "/") ? "" : path);
    return (DIR *)dir;
}
static struct dirent *voice_readdir(DIR *handle) {
    voice_dir_t *dir = (voice_dir_t *)handle;
    unsigned dir_count = sizeof(directories) / sizeof(directories[0]);
    unsigned file_count = sizeof(voice_assets) / sizeof(voice_assets[0]);
    size_t prefix = strlen(dir->path);
    while (dir->next < dir_count + file_count) {
        unsigned index = dir->next++;
        const char *path = index < dir_count ? directories[index] : voice_assets[index - dir_count].path;
        if (strncmp(path, dir->path, prefix) || path[prefix] != '/') continue;
        const char *name = path + prefix + 1;
        if (!*name || strchr(name, '/')) continue;
        memset(&dir->entry, 0, sizeof(dir->entry));
        snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", name);
        dir->entry.d_type = index < dir_count ? DT_DIR : DT_REG;
        return &dir->entry;
    }
    return NULL;
}
static int voice_closedir(DIR *dir) { free(dir); return 0; }
int m4_tts_data_mount(void) {
    if (mounted) return ESP_OK;
    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT, .open = voice_open, .read = voice_read, .lseek = voice_seek,
        .close = voice_close, .stat = voice_stat, .fstat = voice_fstat,
        .opendir = voice_opendir, .readdir = voice_readdir, .closedir = voice_closedir
    };
    esp_err_t result = esp_vfs_register("/tts", &vfs, NULL);
    mounted = result == ESP_OK; return result;
}
