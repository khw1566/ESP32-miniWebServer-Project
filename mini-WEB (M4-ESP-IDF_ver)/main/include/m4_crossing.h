#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "m4_2048.h"

#define M4_CROSS_CELL 44
#define M4_CROSS_WIDTH 396
#define M4_CROSS_ROWS 9
#define M4_CROSS_PERIOD 198
typedef enum { M4_CROSS_GRASS, M4_CROSS_ROAD, M4_CROSS_RIVER } m4_cross_lane_t;
typedef struct {
    uint32_t seed, row, best_row, camera;
    unsigned ticks, idle_ticks;
    int x;
    bool started, dead;
} m4_crossing_t;

void m4_crossing_reset(m4_crossing_t *game, uint32_t seed);
m4_cross_lane_t m4_crossing_lane(const m4_crossing_t *game, uint32_t row);
int m4_crossing_speed(const m4_crossing_t *game, uint32_t row);
int m4_crossing_offset(const m4_crossing_t *game, uint32_t row);
int m4_crossing_length(const m4_crossing_t *game, uint32_t row);
bool m4_crossing_safe(const m4_crossing_t *game);
void m4_crossing_move(m4_crossing_t *game, m4_game_direction_t direction);
void m4_crossing_tick(m4_crossing_t *game);
