#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t version, brightness, max_fps, animations, web_focus, volume, last_volume, reserved;
} m4_preferences_t;
typedef enum { M4_MODE_OK, M4_MODE_BUSY, M4_MODE_SAVE_FAILED, M4_MODE_RESTART_FAILED } m4_mode_result_t;
esp_err_t m4_preferences_init(void);
bool m4_preferences_valid(const m4_preferences_t *settings);
void m4_preferences_get(m4_preferences_t *out);
bool m4_preferences_update(const m4_preferences_t *settings);
void m4_preferences_volume(unsigned percent);
esp_err_t m4_preferences_save(const m4_preferences_t *settings);
void m4_preferences_poll(void);
void m4_preferences_boot_mode(bool force_setup);
bool m4_preferences_web_active(void);
bool m4_preferences_restarting(void);
unsigned m4_preferences_frame_ms(unsigned fps);
m4_mode_result_t m4_preferences_request_mode(bool web_focus);
m4_mode_result_t m4_preferences_restart_with(const m4_preferences_t *settings);
