#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { M3_GAME_LEFT, M3_GAME_RIGHT, M3_GAME_UP, M3_GAME_DOWN } m3_game_direction_t;
typedef struct {
    uint8_t tiles[16]; /* Powers of two; zero means empty. */
    uint32_t score, random;
    bool won;
} m3_2048_t;
void m3_2048_reset(m3_2048_t *game, uint32_t seed);
bool m3_2048_slide(m3_2048_t *game, m3_game_direction_t direction);
bool m3_2048_move(m3_2048_t *game, m3_game_direction_t direction);
bool m3_2048_can_move(const m3_2048_t *game);
