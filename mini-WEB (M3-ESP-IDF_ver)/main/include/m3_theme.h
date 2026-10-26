#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Wallpapers are drawn from LVGL shapes; m3_wallpaper_live() tells which ones animate. */
#define M3_WALLPAPER_COUNT 12
enum { M3_OUTLINE_NONE, M3_OUTLINE_SUBTLE, M3_OUTLINE_ACCENT, M3_OUTLINE_COUNT };
enum { M3_CORNER_ROUND, M3_CORNER_COMPACT, M3_CORNER_SQUARE, M3_CORNER_COUNT };
enum { M3_STYLE_DARK, M3_STYLE_WHITE, M3_STYLE_MODERN, M3_STYLE_CLASSIC, M3_STYLE_COUNT };
#define M3_THEME_PRIMARY 0x00e5ef
#define M3_THEME_ACCENT 0xf548ba

/* Neutral roles of one style. Every value is distinct so a style change can find them by value. */
typedef struct {
    uint32_t ink, surface, surface_hi, line, text, muted, field;
    uint32_t key_bg, key_item, key_checked, key_text, danger_bg, danger_text;
    uint32_t shade_top, shade_bottom, glass; /* activity panel gradient and translucent panels */
} m3_palette_t;
#define M3_PALETTE_ROLES (sizeof(m3_palette_t) / sizeof(uint32_t))
typedef struct {
    m3_palette_t palette;
    uint32_t primary, accent;
    uint8_t outline, corners, wallpaper, dark;
} m3_theme_style_t;

/* Stored as its own blob in NVS namespace "m3_device", key "theme". Version 1 (12 bytes,
 * no style) is migrated on load to the dark style. */
typedef struct { uint8_t version, outline, corners, wallpaper, style, reserved[3]; uint32_t primary, accent; } m3_theme_t;
const m3_theme_style_t *m3_theme_style(unsigned style);
void m3_theme_from_style(unsigned style, m3_theme_t *out);
void m3_theme_defaults(m3_theme_t *out);
bool m3_theme_valid(const m3_theme_t *theme);
void m3_theme_load(m3_theme_t *out);
esp_err_t m3_theme_save(const m3_theme_t *theme);
