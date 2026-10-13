#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#define STORAGE_VIEW_ROWS 5
typedef struct {
    uint64_t total, used, reserved;
    esp_err_t error;
} storage_capacity_t;
typedef struct {
    char name[256];
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
