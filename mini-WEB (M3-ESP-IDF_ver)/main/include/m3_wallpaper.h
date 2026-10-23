#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "m3_theme.h"

/* Home wallpaper drawn from a few LVGL shapes, so no image costs Flash.
 * Live wallpapers move small objects from one slow timer and stop when inactive. */
void m3_wallpaper_build(lv_obj_t *holder, unsigned id, uint32_t primary, uint32_t accent);
void m3_wallpaper_set_active(bool active);
bool m3_wallpaper_live(unsigned id);
/* Three colors that summarize a wallpaper for the settings preview tile. */
void m3_wallpaper_preview(unsigned id, uint32_t primary, uint32_t accent, uint32_t colors[3]);
