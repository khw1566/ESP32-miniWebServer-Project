#include "m4_boot_splash.h"
#include "m4_board.h"
#include "m4_layout.h"
#include "m4_boot_logo.inc"
#include "m4_boot_product_mark.inc"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

extern const lv_font_t m4_font_splash, m4_font_splash_small;
static const char *TAG = "m4_splash";

#define CANVAS_W 400
#define CANVAS_H 420
#define CANVAS_Y 64
#define PITCH 11.6f
#define DOT_RADIUS 4.3f
#define WAVE_PX 7.0f
#define MIN_HOLD_MS 3400  /* Text intro length, counted from when the intro starts. */
#define MAX_HOLD_MS 30000 /* Safety net if a boot step never reports. */
#define MAX_FRAME_MS 50   /* Logo clock step cap, so a stalled LVGL task does not skip the ripple. */
#define BAR_W 300
#define BAR_Y 724
#define FADE_MS 450
#define BG 0x050B1A
#define ACCENT 0x00E5EF
#define ACCENT_2 0xF548BA
#define PRODUCT_TEXT_X 24
#define PRODUCT_TEXT_W 352
#define PRODUCT_MARK_X 376
#define PRODUCT_MARK_Y 648

static lv_obj_t *root, *canvas, *caption, *english, *headline_1, *headline_2, *product_mark, *bar_fill, *status;
static lv_timer_t *frame_timer;
static uint16_t *pixels;
static uint32_t started_ms, last_frame_ms, intro_ms, logo_ms;
static bool leaving, intro_started;
static float shown_percent;
static int shown_step = -1, shown_label = -1;
/* Written by any boot task, read by the LVGL timer. */
static atomic_int reached_step = -1;
static atomic_bool active;

/* Keep these strings in sync with STATUS in tools/generate_boot_splash.py. */
static const struct { uint8_t percent; const char *text; } steps[] = {
    [M4_BOOT_DISPLAY] = {8, "화면 준비 중"},
    [M4_BOOT_WORKERS] = {20, "음성·미디어 엔진 시작"},
    [M4_BOOT_UI] = {40, "홈 화면 구성 중"},
    [M4_BOOT_STORAGE] = {58, "저장소 확인 중"},
    [M4_BOOT_NETWORK] = {70, "Wi-Fi 모듈 시작"},
    [M4_BOOT_CONNECT] = {82, "Wi-Fi 연결 중"},
    [M4_BOOT_WEB] = {94, "웹 서버 시작"},
    [M4_BOOT_READY] = {100, "준비 완료"},
};

void m4_boot_splash_step(m4_boot_step_t step) {
    if (!atomic_load(&active) || (unsigned)step > M4_BOOT_READY) return;
    int seen = atomic_load(&reached_step);
    while (seen < (int)step && !atomic_compare_exchange_weak(&reached_step, &seen, (int)step)) {}
}

static inline uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
/* Coverage-weighted blend of one colour into an RGB565 pixel. */
static inline void blend(uint16_t *px, unsigned r, unsigned g, unsigned b, unsigned cover) {
    unsigned v = *px, pr = (v >> 8) & 0xF8, pg = (v >> 3) & 0xFC, pb = (v << 3) & 0xF8;
    *px = rgb565(pr + (int)((int)r - (int)pr) * (int)cover / 255, pg + (int)((int)g - (int)pg) * (int)cover / 255,
                 pb + (int)((int)b - (int)pb) * (int)cover / 255);
}
static void draw_dot(float cx, float cy, float radius, unsigned r, unsigned g, unsigned b) {
    if (radius < 0.3f) return;
    int x0 = (int)floorf(cx - radius - 1), x1 = (int)ceilf(cx + radius + 1);
    int y0 = (int)floorf(cy - radius - 1), y1 = (int)ceilf(cy + radius + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CANVAS_W - 1) x1 = CANVAS_W - 1;
    if (y1 > CANVAS_H - 1) y1 = CANVAS_H - 1;
    for (int y = y0; y <= y1; ++y) {
        float dy = y + 0.5f - cy;
        for (int x = x0; x <= x1; ++x) {
            float dx = x + 0.5f - cx, edge = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (edge <= 0) continue;
            blend(&pixels[y * CANVAS_W + x], r, g, b, edge >= 1 ? 255 : (unsigned)(edge * 255));
        }
    }
}
/* Dots ripple out from the centre, then a diagonal wave keeps rolling through
 * the logo, lifting each dot and brightening it on the crest. */
static void render_frame(float t) {
    const uint16_t bg = rgb565((BG >> 16) & 0xFF, (BG >> 8) & 0xFF, BG & 0xFF);
    const uint32_t pair = (uint32_t)bg << 16 | bg;
    uint32_t *words = (uint32_t *)pixels;
    for (size_t i = 0; i < CANVAS_W * CANVAS_H / 2; ++i) words[i] = pair;
    const float origin_x = (CANVAS_W - (M4_BOOT_LOGO_GRID - 1) * PITCH) / 2;
    const float origin_y = (CANVAS_H - (M4_BOOT_LOGO_GRID - 1) * PITCH) / 2;
    const float centre = (M4_BOOT_LOGO_GRID - 1) / 2.0f, front = t * 30.0f;
    for (int gy = 0; gy < M4_BOOT_LOGO_GRID; ++gy) {
        for (int gx = 0; gx < M4_BOOT_LOGO_GRID; ++gx) {
            unsigned index = m4_boot_logo_dots[gy * M4_BOOT_LOGO_GRID + gx];
            if (!index) continue;
            float distance = sqrtf((gx - centre) * (gx - centre) + (gy - centre) * (gy - centre));
            float reveal = (front - distance) / 4.0f;
            if (reveal <= 0) continue;
            if (reveal > 1) reveal = 1;
            float grow = 1 - (1 - reveal) * (1 - reveal);
            float phase = 0.42f * gx + 0.24f * gy - t * 4.2f;
            float wave = sinf(phase);
            float lift = WAVE_PX * wave * (1 + 1.5f * (1 - reveal));
            float shine = 0.62f + 0.38f * (0.5f + 0.5f * sinf(phase + 0.6f));
            uint32_t colour = m4_boot_logo_palette[index];
            draw_dot(origin_x + gx * PITCH, origin_y + gy * PITCH - lift,
                     DOT_RADIUS * grow * (0.86f + 0.14f * wave),
                     (unsigned)(((colour >> 16) & 0xFF) * shine), (unsigned)(((colour >> 8) & 0xFF) * shine),
                     (unsigned)((colour & 0xFF) * shine));
        }
    }
    lv_canvas_set_buffer(canvas, pixels, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
}

static void set_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }
static void set_rise(void *obj, int32_t v) { lv_obj_set_style_translate_y(obj, v, 0); }
static void set_scale(void *obj, int32_t v) { lv_obj_set_style_transform_scale(obj, v, 0); }
/* Separate callback: LVGL replaces a running animation with the same var and exec_cb. */
static void set_pulse(void *obj, int32_t v) { lv_obj_set_style_transform_scale(obj, v, 0); }
static void set_glow(void *obj, int32_t v) {
    lv_obj_set_style_text_color(obj, lv_color_mix(lv_color_hex(0xFFFFFF), lv_color_hex(ACCENT), (lv_opa_t)v), 0);
}
static void animate(lv_obj_t *obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t delay, uint32_t duration,
                    lv_anim_path_cb_t path, uint32_t playback, uint32_t repeats) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to); lv_anim_set_delay(&a, delay); lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_early_apply(&a, false); /* Hold the styled start state until each step's delay. */
    if (playback) { lv_anim_set_playback_duration(&a, playback); lv_anim_set_repeat_count(&a, repeats); }
    lv_anim_start(&a);
}

static void finished(lv_anim_t *a) { (void)a; lv_obj_delete_async(root); }
static void leave(void) {
    if (leaving) return;
    leaving = true;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, root); lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP); lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in); lv_anim_set_completed_cb(&a, finished);
    lv_anim_start(&a);
}
/* The bar eases toward the last reported step and creeps a little past it while
 * a slow step (Wi-Fi) runs, but never shows 100 % before boot is ready. */
static void update_progress(int step) {
    float goal = step < 0 ? 0 : steps[step].percent;
    if (step >= 0 && step < M4_BOOT_READY) {
        float next = steps[step + 1].percent - 2;
        if (shown_percent >= goal - 0.5f && shown_percent < next) goal = shown_percent + 0.06f;
    }
    shown_percent += goal > shown_percent ? (goal - shown_percent) * 0.14f + 0.04f : 0;
    if (shown_percent > goal && step == M4_BOOT_READY) shown_percent = goal;
    lv_obj_set_width(bar_fill, (int32_t)(BAR_W * shown_percent / 100));
    int percent = (int)shown_percent;
    if (step != shown_step || percent != shown_label) {
        shown_step = step; shown_label = percent;
        char line[64];
        snprintf(line, sizeof(line), "%s  %d%%", step < 0 ? steps[0].text : steps[step].text, percent);
        lv_label_set_text(status, line);
    }
}
/* College name, then the headline, then the product name lands large and pulses. */
static void start_intro(void) {
    intro_started = true;
    intro_ms = lv_tick_get();
    animate(caption, set_opa, 0, 255, 100, 400, lv_anim_path_ease_out, 0, 0);
    animate(english, set_opa, 0, 255, 250, 400, lv_anim_path_ease_out, 0, 0);
    animate(headline_1, set_opa, 0, 255, 400, 450, lv_anim_path_ease_out, 0, 0);
    animate(headline_1, set_rise, 24, 0, 400, 450, lv_anim_path_ease_out, 0, 0);
    animate(headline_2, set_opa, 0, 255, 800, 300, lv_anim_path_linear, 0, 0);
    animate(headline_2, set_scale, 460, 256, 800, 650, lv_anim_path_overshoot, 0, 0);
    animate(product_mark, set_opa, 0, 255, 1500, 300, lv_anim_path_ease_out, 0, 0);
    animate(headline_2, set_pulse, 256, 284, 1700, 260, lv_anim_path_ease_in_out, 260, 2);
    animate(headline_2, set_glow, 0, 255, 1700, 260, lv_anim_path_ease_in_out, 260, 2);
}
static void tick(lv_timer_t *timer) {
    (void)timer;
    uint32_t dt = lv_tick_elaps(last_frame_ms);
    last_frame_ms = lv_tick_get();
    logo_ms += dt < MAX_FRAME_MS ? dt : MAX_FRAME_MS;
    render_frame(logo_ms / 1000.0f);
    int step = atomic_load(&reached_step);
    /* m4_ui_start builds the home UI under the LVGL lock and stalls every frame
     * meanwhile, so the text intro waits until that build is over. */
    if (!intro_started && step >= M4_BOOT_STORAGE) start_intro();
    update_progress(step);
    bool ready = step == M4_BOOT_READY && shown_percent >= 99.5f;
    if ((ready && intro_started && lv_tick_elaps(intro_ms) >= MIN_HOLD_MS) || lv_tick_elaps(started_ms) >= MAX_HOLD_MS)
        leave();
}
/* Touch skips the splash only once the home UI exists underneath it. */
static void touched(lv_event_t *e) {
    (void)e;
    if (atomic_load(&reached_step) >= M4_BOOT_STORAGE && lv_tick_elaps(started_ms) > 600) leave();
}
static void deleted(lv_event_t *e) {
    (void)e;
    atomic_store(&active, false);
    if (frame_timer) lv_timer_delete(frame_timer);
    frame_timer = NULL;
    heap_caps_free(pixels);
    pixels = NULL; root = canvas = caption = english = headline_1 = headline_2 = product_mark = bar_fill = status = NULL;
}

static lv_obj_t *text(const char *value, const lv_font_t *font, uint32_t colour, int y) {
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, value);
    lv_obj_set_width(label, M4_WIDTH);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    lv_obj_set_style_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(label, 0, y);
    return label;
}

esp_err_t m4_boot_splash_start(void) {
    if (!m4_board_display()) return ESP_ERR_INVALID_STATE;
    /* About 330 KiB; PSRAM is ample and only the LVGL task touches it. */
    pixels = heap_caps_aligned_alloc(64, CANVAS_W * CANVAS_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) { ESP_LOGW(TAG, "No PSRAM for splash; skipping"); return ESP_ERR_NO_MEM; }
    if (!lvgl_port_lock(1000)) { heap_caps_free(pixels); pixels = NULL; return ESP_ERR_TIMEOUT; }
    leaving = intro_started = false;
    shown_percent = 0; shown_step = shown_label = -1; logo_ms = 0;
    root = m4_layout_create_root(lv_layer_top());
    if (!root) { lvgl_port_unlock(); heap_caps_free(pixels); pixels = NULL; return ESP_ERR_NO_MEM; }
    lv_obj_set_size(root, M4_WIDTH, M4_HEIGHT);
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, touched, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);

    canvas = lv_canvas_create(root);
    lv_obj_set_pos(canvas, (M4_WIDTH - CANVAS_W) / 2, CANVAS_Y);
    started_ms = last_frame_ms = lv_tick_get();
    render_frame(0);

    caption = text("울산과학대학교", &m4_font_splash_small, 0xE2E8F0, CANVAS_Y + CANVAS_H + 6);
    english = text("ULSAN COLLEGE", &m4_font_splash_small, 0x94A3B8, CANVAS_Y + CANVAS_H + 36);
    lv_obj_set_style_text_letter_space(english, 4, 0);
    headline_1 = text("창업경진대회", &m4_font_splash, 0xF8FAFC, 594);
    headline_2 = text("MiniWEB 시제품", &m4_font_splash, ACCENT, 650);
    lv_obj_set_width(headline_2, PRODUCT_TEXT_W);
    lv_obj_set_pos(headline_2, PRODUCT_TEXT_X, 650);
    lv_obj_set_style_transform_pivot_x(headline_2, PRODUCT_TEXT_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(headline_2, m4_font_splash.line_height / 2, 0);
    product_mark = lv_image_create(root);
    lv_image_set_src(product_mark, &m4_boot_product_mark);
    lv_obj_set_pos(product_mark, PRODUCT_MARK_X, PRODUCT_MARK_Y);
    lv_obj_set_style_opa(product_mark, LV_OPA_TRANSP, 0);
    lv_obj_t *track = lv_obj_create(root);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, BAR_W, 8);
    lv_obj_set_pos(track, (M4_WIDTH - BAR_W) / 2, BAR_Y);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x1E293B), 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    bar_fill = lv_obj_create(track);
    lv_obj_remove_style_all(bar_fill);
    lv_obj_set_size(bar_fill, 0, 8);
    lv_obj_set_style_radius(bar_fill, 4, 0);
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar_fill, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_grad_color(bar_fill, lv_color_hex(ACCENT_2), 0);
    lv_obj_set_style_bg_grad_dir(bar_fill, LV_GRAD_DIR_HOR, 0);
    status = text(steps[0].text, &m4_font_splash_small, 0x94A3B8, BAR_Y + 18);
    lv_obj_set_style_opa(status, LV_OPA_COVER, 0); /* Progress is visible from the first frame. */

    frame_timer = lv_timer_create(tick, 33, NULL);
    if (!frame_timer) lv_obj_delete(root); /* Never leave a splash that cannot dismiss itself. */
    else { atomic_store(&reached_step, -1); atomic_store(&active, true); m4_boot_splash_step(M4_BOOT_DISPLAY); }
    lvgl_port_unlock();
    return pixels ? ESP_OK : ESP_ERR_NO_MEM;
}
