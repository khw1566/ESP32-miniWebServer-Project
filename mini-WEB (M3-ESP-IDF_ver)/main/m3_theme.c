#include "m3_theme.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

_Static_assert(sizeof(m3_theme_t) == 16, "Theme NVS blob layout");

/* Dark is the original M3 look; White, Modern and Classic are complete styles. */
static const m3_theme_style_t styles[M3_STYLE_COUNT] = {
    [M3_STYLE_DARK] = {{0x0a0e14, 0x151b25, 0x202938, 0x2b3647, 0xe9eef5, 0x8c99ab, 0x0e131b,
        0x0d121a, 0x2a3445, 0x18202c, 0xc3cede, 0x3a1d27, 0xff9aa8},
        M3_THEME_PRIMARY, M3_THEME_ACCENT, M3_OUTLINE_NONE, M3_CORNER_ROUND, 0, 1},
    [M3_STYLE_WHITE] = {{0xf3f5f8, 0xfefefe, 0xe9edf2, 0xd3dae3, 0x18212c, 0x5d6878, 0xf8fafc,
        0xdfe4ea, 0xfcfdfe, 0xc9d1db, 0x2b3542, 0xfde4e7, 0xc5283d},
        0x2563eb, 0xdb2777, M3_OUTLINE_SUBTLE, M3_CORNER_ROUND, 8, 0},
    [M3_STYLE_MODERN] = {{0x0b0b0c, 0x17171a, 0x232327, 0x313137, 0xf4f4f6, 0x9b9ba3, 0x111113,
        0x101012, 0x2a2a2f, 0x1c1c20, 0xc8c8ce, 0x3b1a1c, 0xff8a8f},
        0x0a84ff, 0x30d158, M3_OUTLINE_NONE, M3_CORNER_COMPACT, 6, 1},
    [M3_STYLE_CLASSIC] = {{0x1c1812, 0x28221a, 0x372f24, 0x4a4031, 0xf2e8d5, 0xb2a58b, 0x221d16,
        0x1f1a14, 0x3a3126, 0x2c251c, 0xd9ccb2, 0x43201a, 0xf2a48f},
        0xd4a24c, 0xb4532a, M3_OUTLINE_ACCENT, M3_CORNER_SQUARE, 2, 1},
};
const m3_theme_style_t *m3_theme_style(unsigned style) { return &styles[style < M3_STYLE_COUNT ? style : 0]; }
void m3_theme_from_style(unsigned style, m3_theme_t *out) {
    const m3_theme_style_t *s = m3_theme_style(style);
    *out = (m3_theme_t){2, s->outline, s->corners, s->wallpaper, (uint8_t)(style < M3_STYLE_COUNT ? style : 0), {0}, s->primary, s->accent};
}
void m3_theme_defaults(m3_theme_t *out) { m3_theme_from_style(M3_STYLE_DARK, out); }
bool m3_theme_valid(const m3_theme_t *p) {
    return p && p->version == 2 && p->outline < M3_OUTLINE_COUNT && p->corners < M3_CORNER_COUNT &&
        p->wallpaper < M3_WALLPAPER_COUNT && p->style < M3_STYLE_COUNT && !p->reserved[0] && !p->reserved[1] &&
        !p->reserved[2] && p->primary <= 0xffffff && p->accent <= 0xffffff;
}
void m3_theme_load(m3_theme_t *out) {
    m3_theme_defaults(out);
    nvs_handle_t nvs; uint8_t blob[sizeof(m3_theme_t)]; size_t length = sizeof(blob);
    if (nvs_open("m3_device", NVS_READONLY, &nvs) != ESP_OK) return;
    if (nvs_get_blob(nvs, "theme", blob, &length) == ESP_OK) {
        m3_theme_t saved = {0};
        if (length == sizeof(saved)) memcpy(&saved, blob, sizeof(saved));
        else if (length == 12 && blob[0] == 1) {
            /* Version 1: version, outline, corners, wallpaper, primary, accent. */
            saved = (m3_theme_t){2, blob[1], blob[2], blob[3], M3_STYLE_DARK, {0}, 0, 0};
            memcpy(&saved.primary, blob + 4, 4); memcpy(&saved.accent, blob + 8, 4);
        }
        if (m3_theme_valid(&saved)) *out = saved;
    }
    nvs_close(nvs);
}
esp_err_t m3_theme_save(const m3_theme_t *p) {
    if (!m3_theme_valid(p)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("m3_device", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "theme", p, sizeof(*p));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) ESP_LOGW("M3_THEME", "Theme save failed: %s", esp_err_to_name(err));
    return err;
}
