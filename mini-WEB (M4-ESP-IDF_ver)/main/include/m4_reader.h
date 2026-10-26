#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define M4_READER_MAX_BYTES (4U * 1024U * 1024U)
#define M4_READER_WINDOW 8192
typedef struct {
    uint8_t version, font_px, theme, letter_space, line_space, margin, background, reserved;
} m4_reader_settings_t;
typedef enum { M4_READER_IDLE, M4_READER_LOADING, M4_READER_READY, M4_READER_NO_SD,
    M4_READER_BUSY, M4_READER_IO, M4_READER_TOO_LARGE, M4_READER_ENCODING, M4_READER_MEMORY } m4_reader_status_t;
typedef struct {
    m4_reader_status_t status;
    unsigned generation, revision, bytes, resume;
    bool available, save_failed;
    char path[256];
} m4_reader_state_t;
esp_err_t m4_reader_start(void);
bool m4_reader_supported(const char *name);
bool m4_reader_path_valid(const char *path);
bool m4_reader_settings_valid(const m4_reader_settings_t *settings);
void m4_reader_settings(m4_reader_settings_t *out);
bool m4_reader_configure(const m4_reader_settings_t *settings);
bool m4_reader_open(const char *path);
bool m4_reader_resume(void);
void m4_reader_close(void);
void m4_reader_state(m4_reader_state_t *out);
/* Copies a UTF-8 window into UI-owned memory. No borrowed document pointers. */
size_t m4_reader_copy(unsigned generation, unsigned offset, char *out, size_t capacity);
unsigned m4_reader_align(unsigned generation, unsigned offset);
void m4_reader_progress(unsigned generation, unsigned offset);
/* In-place UTF-8 BOM/CRLF/control normalization; text needs bytes+1 capacity.
 * Returns false for binary/invalid input. */
bool m4_reader_normalize(char *text, size_t bytes, size_t *length);
