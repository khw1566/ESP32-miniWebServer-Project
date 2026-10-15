#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "m3_2048.h"

#define M3_CROSS_CELL 44
#define M3_CROSS_WIDTH 396
#define M3_CROSS_ROWS 9
#define M3_CROSS_PERIOD 198
typedef enum { M3_CROSS_GRASS, M3_CROSS_ROAD, M3_CROSS_RIVER } m3_cross_lane_t;
typedef struct {
    uint32_t seed, row, best_row, camera;
    unsigned ticks, idle_ticks;
    int x;
    bool started, dead;
} m3_crossing_t;

void m3_crossing_reset(m3_crossing_t *game, uint32_t seed);
m3_cross_lane_t m3_crossing_lane(const m3_crossing_t *game, uint32_t row);
int m3_crossing_speed(const m3_crossing_t *game, uint32_t row);
int m3_crossing_offset(const m3_crossing_t *game, uint32_t row);
int m3_crossing_length(const m3_crossing_t *game, uint32_t row);
bool m3_crossing_safe(const m3_crossing_t *game);
void m3_crossing_move(m3_crossing_t *game, m3_game_direction_t direction);
void m3_crossing_tick(m3_crossing_t *game);
