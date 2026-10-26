#pragma once
#include <stdbool.h>
#include <stdint.h>
#define M4_SD_APP_COUNT 16
#define M4_SD_APP_NAME 48
#define M4_SD_APP_WIDTH 160
#define M4_SD_APP_HEIGHT 192
typedef struct {
    unsigned count;
    char names[M4_SD_APP_COUNT][M4_SD_APP_NAME];
    bool mounted, running;
    char error[128];
    int score;
} m4_sd_apps_state_t;
/* Service task owns discovery; UI never scans the card or executes Lua. */
void m4_sd_apps_poll(void);
bool m4_sd_apps_state(m4_sd_apps_state_t *out);
bool m4_sd_apps_start(const char *name);
void m4_sd_apps_stop(void);
/* One-shot input bits: left=1, right=2, rotate=4, drop=8, restart=16. */
void m4_sd_apps_input(unsigned buttons);
bool m4_sd_apps_frame(uint16_t *destination);
