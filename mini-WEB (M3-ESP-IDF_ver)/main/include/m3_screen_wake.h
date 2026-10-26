#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { M3_WAKE_HOLD, M3_WAKE_DOUBLE_TAP };
typedef struct {
    bool blocked, down, moved, tap, fired;
    uint32_t start, last_tap;
    int x, y, tap_x, tap_y;
} m3_screen_wake_t;

/* Poll only while a full-screen input shield is visible. All times are ms.
 * Drain the screen-off press; unsigned subtraction also handles tick wrap. */
static inline void m3_screen_wake_reset(m3_screen_wake_t *s) {
    *s = (m3_screen_wake_t){.blocked = true};
}
static inline bool m3_screen_wake_sample(m3_screen_wake_t *s, unsigned mode,
                                       uint32_t now, bool pressed, int x, int y) {
    if (s->fired) return false;
    if (s->blocked) { if (!pressed) s->blocked = false; return false; }
    if (pressed) {
        if (!s->down) {
            s->down = true; s->moved = false; s->start = now; s->x = x; s->y = y;
        }
        if (x - s->x > 40 || s->x - x > 40 || y - s->y > 40 || s->y - y > 40) s->moved = true;
        if (mode == M3_WAKE_HOLD && !s->moved && (uint32_t)(now - s->start) >= 2000) {
            s->fired = true; return true;
        }
    } else if (s->down) {
        s->down = false;
        if (mode == M3_WAKE_DOUBLE_TAP && !s->moved && (uint32_t)(now - s->start) <= 250) {
            if (s->tap && (uint32_t)(now - s->last_tap) <= 400 &&
                x - s->tap_x <= 60 && s->tap_x - x <= 60 && y - s->tap_y <= 60 && s->tap_y - y <= 60) {
                s->fired = true; return true;
            }
            s->tap = true; s->last_tap = now; s->tap_x = x; s->tap_y = y;
        } else s->tap = false;
    }
    if (s->tap && (uint32_t)(now - s->last_tap) > 400) s->tap = false;
    return false;
}
