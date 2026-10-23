#include "m3_theme.h"
#include "nvs.h"
#include "esp_log.h"

_Static_assert(sizeof(m3_theme_t) == 12, "Theme NVS blob layout");

void m3_theme_defaults(m3_theme_t *out) {
    *out = (m3_theme_t){1, M3_OUTLINE_NONE, M3_CORNER_ROUND, 0, M3_THEME_PRIMARY, M3_THEME_ACCENT};
}
bool m3_theme_valid(const m3_theme_t *p) {
    return p && p->version == 1 && p->outline < M3_OUTLINE_COUNT && p->corners < M3_CORNER_COUNT &&
        p->wallpaper < M3_WALLPAPER_COUNT && p->primary <= 0xffffff && p->accent <= 0xffffff;
}
void m3_theme_load(m3_theme_t *out) {
    m3_theme_defaults(out);
    nvs_handle_t nvs; m3_theme_t saved; size_t length = sizeof(saved);
    if (nvs_open("m3_device", NVS_READONLY, &nvs) != ESP_OK) return;
    if (nvs_get_blob(nvs, "theme", &saved, &length) == ESP_OK && length == sizeof(saved) && m3_theme_valid(&saved)) *out = saved;
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
