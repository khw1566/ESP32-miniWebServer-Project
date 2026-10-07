#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#define STORAGE_VIEW_ROWS 3
/* FATFS long names are capped at CONFIG_FATFS_MAX_LFN (128); SPIFFS at 32. */
#define STORAGE_VIEW_NAME_MAX 129
typedef struct {
    uint64_t total, used, reserved;
    esp_err_t error;
} storage_capacity_t;
typedef struct {
    char name[STORAGE_VIEW_NAME_MAX];
    uint64_t bytes;
    bool directory;
} storage_view_entry_t;
typedef struct {
    uint64_t total, used, reserved;
    unsigned count, shown, offset;
    esp_err_t error;
    storage_view_entry_t entries[STORAGE_VIEW_ROWS];
} storage_view_t;
void storage_view_read(bool sd, const char *folder, unsigned offset, storage_view_t *view);
void storage_capacity_read(bool sd, storage_capacity_t *capacity);
