#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Wallpapers are drawn from LVGL shapes; ids from M3_WALLPAPER_LIVE_FIRST animate. */
#define M3_WALLPAPER_COUNT 8
#define M3_WALLPAPER_LIVE_FIRST 5
enum { M3_OUTLINE_NONE, M3_OUTLINE_SUBTLE, M3_OUTLINE_ACCENT, M3_OUTLINE_COUNT };
enum { M3_CORNER_ROUND, M3_CORNER_COMPACT, M3_CORNER_SQUARE, M3_CORNER_COUNT };
#define M3_THEME_PRIMARY 0x00e5ef
#define M3_THEME_ACCENT 0xf548ba

/* Stored as its own blob in NVS namespace "m3_device", key "theme". */
typedef struct { uint8_t version, outline, corners, wallpaper; uint32_t primary, accent; } m3_theme_t;
void m3_theme_defaults(m3_theme_t *out);
bool m3_theme_valid(const m3_theme_t *theme);
void m3_theme_load(m3_theme_t *out);
esp_err_t m3_theme_save(const m3_theme_t *theme);
