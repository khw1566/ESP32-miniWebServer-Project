#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define M3_READER_MAX_BYTES (4U * 1024U * 1024U)
#define M3_READER_WINDOW 8192
typedef struct {
    /* font_family uses the former zero reserved byte; version-1 settings remain valid. */
    uint8_t version, font_px, theme, letter_space, line_space, margin, background, font_family;
} m3_reader_settings_t;
typedef enum { M3_READER_IDLE, M3_READER_LOADING, M3_READER_READY, M3_READER_NO_SD,
    M3_READER_BUSY, M3_READER_IO, M3_READER_TOO_LARGE, M3_READER_ENCODING, M3_READER_MEMORY } m3_reader_status_t;
typedef struct {
    m3_reader_status_t status;
    unsigned generation, revision, bytes, resume;
    bool available, save_failed;
    char path[256];
} m3_reader_state_t;
esp_err_t m3_reader_start(void);
bool m3_reader_supported(const char *name);
bool m3_reader_path_valid(const char *path);
bool m3_reader_settings_valid(const m3_reader_settings_t *settings);
void m3_reader_settings(m3_reader_settings_t *out);
bool m3_reader_configure(const m3_reader_settings_t *settings);
bool m3_reader_open(const char *path);
bool m3_reader_resume(void);
void m3_reader_close(void);
void m3_reader_state(m3_reader_state_t *out);
/* Copies a UTF-8 window into UI-owned memory. No borrowed document pointers. */
size_t m3_reader_copy(unsigned generation, unsigned offset, char *out, size_t capacity);
unsigned m3_reader_align(unsigned generation, unsigned offset);
void m3_reader_progress(unsigned generation, unsigned offset);
/* In-place UTF-8 BOM/CRLF/control normalization; text needs bytes+1 capacity.
 * Returns false for binary/invalid input. */
bool m3_reader_normalize(char *text, size_t bytes, size_t *length);
