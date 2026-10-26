#include "m3_wallpaper.h"

/* Every wallpaper is a vertical gradient plus a handful of shapes. Live variants
 * move at most 32 small objects from one timer (10 to 20 updates per second), so
 * only the moved areas redraw, and the timer pauses whenever Home is not shown. */
enum { WALL_W = 480, WALL_H = 712, MAX_PARTS = 32, STARS = 30 };
enum { WALL_AURORA, WALL_MIDNIGHT, WALL_SUNSET, WALL_OCEAN, WALL_MONO, WALL_STARS, WALL_DRIFT, WALL_BUBBLES, WALL_DAWN,
    WALL_CLOUDS, WALL_PETALS, WALL_BOKEH };
_Static_assert(WALL_BOKEH + 1 == M3_WALLPAPER_COUNT, "wallpaper ids");
static const uint32_t backgrounds[M3_WALLPAPER_COUNT][2] = {
    {0x0b1424, 0x1d0f2c}, {0x020308, 0x0a1020}, {0x1b1036, 0x6b2338}, {0x04263d, 0x02111d},
    {0x1c1c1c, 0x0a0a0a}, {0x02040a, 0x0e1730}, {0x070b16, 0x160b24}, {0x031a2a, 0x06324a}, {0xeef3fb, 0xd6e0ef},
    {0xcfe3f8, 0xf3f8fd}, {0xfdf1f5, 0xf5e1ea}, {0xf2f5fb, 0xe1e9f4}};
/* Timer period per wallpaper; zero means static. */
static const uint16_t periods[M3_WALLPAPER_COUNT] = {0, 0, 0, 0, 0, 90, 70, 50, 0, 60, 55, 70};
static lv_obj_t *parts[MAX_PARTS], *meteor;
static int16_t part_x[MAX_PARTS], part_y[MAX_PARTS], meteor_x, meteor_y;
static uint8_t part_size[MAX_PARTS], part_speed[MAX_PARTS];
static unsigned part_count, wall_id = WALL_AURORA;
static uint32_t seed, phase;
static lv_timer_t *timer;
static bool active;

static unsigned random_below(unsigned limit) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % limit;
}
static lv_color_t blend(uint32_t a, uint32_t b, uint8_t ratio) { return lv_color_mix(lv_color_hex(a), lv_color_hex(b), ratio); }
static lv_obj_t *shape(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, lv_color_t color, lv_opa_t opa) {
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, color, 0); lv_obj_set_style_bg_opa(obj, opa, 0);
    return obj;
}
static lv_obj_t *ring(lv_obj_t *parent, int32_t x, int32_t y, int32_t size, lv_color_t color, lv_opa_t line, lv_opa_t fill) {
    lv_obj_t *obj = shape(parent, x, y, size, size, LV_RADIUS_CIRCLE, color, fill);
    lv_obj_set_style_border_color(obj, color, 0); lv_obj_set_style_border_opa(obj, line, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    return obj;
}
static void remember(lv_obj_t *obj, int32_t x, int32_t y, unsigned size, unsigned speed) {
    if (part_count >= MAX_PARTS) return;
    parts[part_count] = obj; part_x[part_count] = (int16_t)x; part_y[part_count] = (int16_t)y;
    part_size[part_count] = (uint8_t)size; part_speed[part_count++] = (uint8_t)speed;
}
static int16_t sway(unsigned i, int amplitude) {
    return (int16_t)(lv_trigo_sin((int16_t)((phase * 6 + i * 47) % 360)) * amplitude >> LV_TRIGO_SHIFT);
}

static void build_static(lv_obj_t *holder, unsigned id, uint32_t primary, uint32_t accent) {
    if (id == WALL_AURORA) {
        shape(holder, -140, 300, 460, 460, LV_RADIUS_CIRCLE, blend(primary, backgrounds[id][0], 170), LV_OPA_20);
        shape(holder, 250, -120, 340, 340, LV_RADIUS_CIRCLE, lv_color_hex(accent), LV_OPA_10);
    } else if (id == WALL_MIDNIGHT) {
        lv_obj_t *glow = shape(holder, 0, WALL_H - 320, WALL_W, 320, 0, lv_color_hex(primary), LV_OPA_COVER);
        lv_obj_set_style_bg_grad_color(glow, lv_color_hex(primary), 0); lv_obj_set_style_bg_grad_dir(glow, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_opa(glow, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_grad_opa(glow, LV_OPA_30, 0);
        /* Crescent: a pale disc partly covered by a disc of the sky color. */
        shape(holder, 360, 90, 70, 70, LV_RADIUS_CIRCLE, lv_color_hex(0xe5e7eb), LV_OPA_80);
        shape(holder, 380, 78, 70, 70, LV_RADIUS_CIRCLE, lv_color_hex(backgrounds[id][0]), LV_OPA_COVER);
    } else if (id == WALL_SUNSET) {
        lv_obj_t *sun = shape(holder, 110, WALL_H - 380, 260, 260, LV_RADIUS_CIRCLE, lv_color_hex(0xfbbf24), LV_OPA_60);
        lv_obj_set_style_bg_grad_color(sun, lv_color_hex(0xf97316), 0); lv_obj_set_style_bg_grad_dir(sun, LV_GRAD_DIR_VER, 0);
        shape(holder, -80, WALL_H - 230, 360, 200, LV_RADIUS_CIRCLE, lv_color_hex(0x12081f), LV_OPA_90);
        shape(holder, 0, WALL_H - 170, WALL_W, 170, 0, lv_color_hex(0x12081f), LV_OPA_90);
    } else if (id == WALL_OCEAN) {
        for (unsigned i = 0; i < 3; ++i)
            shape(holder, -210, WALL_H - 280 + (int32_t)i * 70, 900, 320, LV_RADIUS_CIRCLE,
                blend(primary, 0x0e7490, 110), (lv_opa_t)(26 + i * 10));
    } else if (id == WALL_MONO) {
        ring(holder, 60, 120, 360, lv_color_hex(0xffffff), 46, LV_OPA_TRANSP);
        ring(holder, 200, 360, 220, lv_color_hex(0xffffff), 30, LV_OPA_TRANSP);
    } else if (id == WALL_DAWN) {
        shape(holder, -120, 380, 440, 440, LV_RADIUS_CIRCLE, lv_color_hex(primary), 26);
        shape(holder, 240, -100, 320, 320, LV_RADIUS_CIRCLE, lv_color_hex(accent), 20);
    }
}
/* A cloud is three overlapping discs inside one moving container. */
static lv_obj_t *cloud(lv_obj_t *holder, int32_t x, int32_t y, unsigned size) {
    lv_obj_t *group = lv_obj_create(holder); lv_obj_remove_style_all(group);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(group, x, y); lv_obj_set_size(group, size * 2, size);
    lv_color_t white = lv_color_hex(0xfdfeff);
    shape(group, 0, size / 3, size, size * 2 / 3, LV_RADIUS_CIRCLE, white, 210);
    shape(group, size / 2, 0, size, size, LV_RADIUS_CIRCLE, white, 220);
    shape(group, size, size / 3, size, size * 2 / 3, LV_RADIUS_CIRCLE, white, 210);
    return group;
}
static void build_live(lv_obj_t *holder, unsigned id, uint32_t primary, uint32_t accent) {
    if (id == WALL_STARS) {
        shape(holder, -100, 120, 420, 420, LV_RADIUS_CIRCLE, lv_color_hex(accent), 24);
        for (unsigned i = 0; i < STARS; ++i) {
            unsigned size = 2 + random_below(4);
            int32_t x = (int32_t)random_below(WALL_W), y = (int32_t)random_below(WALL_H - 40);
            lv_obj_t *star = shape(holder, x, y, size, size, LV_RADIUS_CIRCLE,
                lv_color_hex(i % 5 ? 0xffffff : primary), (lv_opa_t)(40 + random_below(216)));
            remember(star, x, y, size, 0);
        }
        /* Shooting star: a short fading streak that crosses the sky every few seconds. */
        meteor = shape(holder, -200, -200, 70, 3, 2, lv_color_hex(0xffffff), LV_OPA_COVER);
        lv_obj_set_style_bg_grad_color(meteor, lv_color_hex(0xffffff), 0); lv_obj_set_style_bg_grad_dir(meteor, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_main_opa(meteor, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_grad_opa(meteor, LV_OPA_COVER, 0);
        meteor_x = -200; meteor_y = -200;
    } else if (id == WALL_DRIFT) {
        static const int16_t base[3][3] = {{-60, 340, 300}, {230, 40, 260}, {150, 420, 220}};
        const lv_color_t colors[3] = {lv_color_hex(primary), lv_color_hex(accent), blend(primary, accent, 128)};
        static const lv_opa_t opacity[3] = {64, 54, 46};
        for (unsigned i = 0; i < 3; ++i) {
            lv_obj_t *orb = shape(holder, base[i][0], base[i][1], base[i][2], base[i][2], LV_RADIUS_CIRCLE, colors[i], opacity[i]);
            remember(orb, base[i][0], base[i][1], 0, 2 + i);
        }
    } else if (id == WALL_BUBBLES) {
        for (unsigned i = 0; i < 14; ++i) {
            unsigned size = 12 + random_below(28);
            int32_t x = (int32_t)random_below(WALL_W - size), y = (int32_t)random_below(WALL_H);
            lv_obj_t *bubble = ring(holder, x, y, size, lv_color_hex(primary), LV_OPA_60, 26);
            remember(bubble, x, y, size, 2 + size / 14);
        }
    } else if (id == WALL_CLOUDS) {
        shape(holder, 300, 60, 120, 120, LV_RADIUS_CIRCLE, lv_color_hex(0xfff3c4), 230); /* sun */
        for (unsigned i = 0; i < 6; ++i) {
            unsigned size = 44 + random_below(40);
            int32_t x = (int32_t)random_below(WALL_W), y = 40 + (int32_t)i * 110 + (int32_t)random_below(40);
            remember(cloud(holder, x, y, size), x, y, size, 1 + i % 2);
        }
    } else if (id == WALL_PETALS) {
        const lv_color_t colors[3] = {blend(accent, 0xffffff, 120), blend(0xf9a8d4, 0xffffff, 200), blend(primary, 0xffffff, 90)};
        for (unsigned i = 0; i < 18; ++i) {
            unsigned size = 10 + random_below(8);
            int32_t x = (int32_t)random_below(WALL_W - size), y = (int32_t)random_below(WALL_H);
            lv_obj_t *petal = shape(holder, x, y, size, size + 5, size / 2 + 2, colors[i % 3], 200);
            lv_obj_set_style_radius(petal, (int32_t)size, 0);
            remember(petal, x, y, size, 1 + random_below(3));
        }
    } else if (id == WALL_BOKEH) {
        for (unsigned i = 0; i < 12; ++i) {
            unsigned size = 30 + random_below(60);
            int32_t x = (int32_t)random_below(WALL_W - size / 2), y = (int32_t)random_below(WALL_H);
            lv_obj_t *glow = shape(holder, x, y, size, size, LV_RADIUS_CIRCLE, lv_color_hex(i % 2 ? primary : accent), (lv_opa_t)(36 + random_below(40)));
            remember(glow, x, y, size, 1 + i % 2);
        }
    }
}
static void tick(lv_timer_t *unused) {
    (void)unused;
    ++phase;
    if (wall_id == WALL_STARS) {
        for (unsigned k = 0; k < 5 && part_count; ++k)
            lv_obj_set_style_bg_opa(parts[random_below(part_count)], (lv_opa_t)(30 + random_below(226)), 0);
        if (meteor_x < -100 && phase % 40 == 0) { meteor_x = (int16_t)(40 + random_below(260)); meteor_y = (int16_t)(20 + random_below(220)); }
        if (meteor_x >= -100) {
            meteor_x += 12; meteor_y += 4;
            if (meteor_x > WALL_W) meteor_x = meteor_y = -200;
            lv_obj_set_pos(meteor, meteor_x, meteor_y);
        }
    } else if (wall_id == WALL_DRIFT) {
        for (unsigned i = 0; i < part_count; ++i) {
            int16_t angle = (int16_t)((phase * part_speed[i] + i * 120) % 360);
            lv_obj_set_pos(parts[i], part_x[i] + (lv_trigo_sin(angle) * 70 >> LV_TRIGO_SHIFT),
                part_y[i] + (lv_trigo_cos(angle) * 55 >> LV_TRIGO_SHIFT));
        }
    } else if (wall_id == WALL_BUBBLES || wall_id == WALL_BOKEH) {
        /* Rise, wrap to the bottom and sway sideways so the motion is not mechanical. */
        for (unsigned i = 0; i < part_count; ++i) {
            part_y[i] -= part_speed[i];
            if (part_y[i] < -(int16_t)part_size[i]) {
                part_y[i] = (int16_t)(WALL_H + random_below(80));
                part_x[i] = (int16_t)random_below(WALL_W - part_size[i] / 2);
            }
            lv_obj_set_pos(parts[i], part_x[i] + sway(i, wall_id == WALL_BOKEH ? 14 : 6), part_y[i]);
        }
    } else if (wall_id == WALL_PETALS) {
        for (unsigned i = 0; i < part_count; ++i) {
            part_y[i] += part_speed[i];
            if (part_y[i] > WALL_H) { part_y[i] = (int16_t)-(int)(20 + random_below(60)); part_x[i] = (int16_t)random_below(WALL_W - 20); }
            lv_obj_set_pos(parts[i], part_x[i] + sway(i, 18), part_y[i]);
        }
    } else if (wall_id == WALL_CLOUDS) {
        if (phase % 2) return; /* clouds drift at half rate */
        for (unsigned i = 0; i < part_count; ++i) {
            part_x[i] += part_speed[i];
            if (part_x[i] > WALL_W) part_x[i] = (int16_t)-(int)(part_size[i] * 2 + random_below(60));
            lv_obj_set_x(parts[i], part_x[i]);
        }
    }
}
static void timer_sync(void) {
    if (!timer) return;
    if (active && m3_wallpaper_live(wall_id)) lv_timer_resume(timer);
    else lv_timer_pause(timer);
}

bool m3_wallpaper_live(unsigned id) { return id < M3_WALLPAPER_COUNT && periods[id]; }
bool m3_wallpaper_light(unsigned id) { return id >= WALL_DAWN && id < M3_WALLPAPER_COUNT; }
void m3_wallpaper_build(lv_obj_t *holder, unsigned id, uint32_t primary, uint32_t accent) {
    if (id >= M3_WALLPAPER_COUNT) id = WALL_AURORA;
    lv_obj_clean(holder);
    part_count = 0; phase = 0; wall_id = id; seed = 0x2545f491u + id; meteor = NULL;
    lv_obj_set_style_bg_opa(holder, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(holder, lv_color_hex(backgrounds[id][0]), 0);
    lv_obj_set_style_bg_grad_color(holder, lv_color_hex(backgrounds[id][1]), 0);
    lv_obj_set_style_bg_grad_dir(holder, LV_GRAD_DIR_VER, 0);
    if (m3_wallpaper_live(id)) build_live(holder, id, primary, accent);
    else build_static(holder, id, primary, accent);
    if (!timer) { timer = lv_timer_create(tick, 1000, NULL); lv_timer_pause(timer); }
    lv_timer_set_period(timer, periods[id] ? periods[id] : 1000);
    timer_sync();
}
void m3_wallpaper_set_active(bool value) {
    active = value; timer_sync();
}
void m3_wallpaper_preview(unsigned id, uint32_t primary, uint32_t accent, uint32_t colors[3]) {
    if (id >= M3_WALLPAPER_COUNT) id = WALL_AURORA;
    static const uint32_t fixed[M3_WALLPAPER_COUNT] = {0, 0, 0xfbbf24, 0, 0x9ca3af, 0xffffff, 0, 0, 0, 0xfdfeff, 0, 0};
    colors[0] = backgrounds[id][0]; colors[1] = backgrounds[id][1];
    colors[2] = fixed[id] ? fixed[id] : id == WALL_DRIFT || id == WALL_PETALS ? accent : primary;
}
