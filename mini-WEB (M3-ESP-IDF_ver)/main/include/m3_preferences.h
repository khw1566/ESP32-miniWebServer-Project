#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    /* Reuse the former zero reserved byte: existing settings default to hold. */
    uint8_t version, brightness, max_fps, animations, web_focus, volume, last_volume, wake_mode;
} m3_preferences_t;
#define M3_SYSTEM_FONT_MIN 16
#define M3_SYSTEM_FONT_MAX 24
typedef struct { uint8_t version, family, pixels, reserved; } m3_typography_t;
bool m3_typography_valid(const m3_typography_t *settings);
void m3_typography_get(m3_typography_t *out);
bool m3_typography_update(const m3_typography_t *settings);
void m3_typography_poll(void);
typedef enum { M3_MODE_OK, M3_MODE_BUSY, M3_MODE_SAVE_FAILED, M3_MODE_RESTART_FAILED } m3_mode_result_t;
esp_err_t m3_preferences_init(void);
bool m3_preferences_valid(const m3_preferences_t *settings);
void m3_preferences_get(m3_preferences_t *out);
bool m3_preferences_update(const m3_preferences_t *settings);
void m3_preferences_volume(unsigned percent);
esp_err_t m3_preferences_save(const m3_preferences_t *settings);
void m3_preferences_poll(void);
void m3_preferences_boot_mode(bool force_setup);
bool m3_preferences_web_active(void);
bool m3_preferences_restarting(void);
unsigned m3_preferences_frame_ms(unsigned fps);
m3_mode_result_t m3_preferences_request_mode(bool web_focus);
m3_mode_result_t m3_preferences_restart_with(const m3_preferences_t *settings);
