#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { M4_GAME_LEFT, M4_GAME_RIGHT, M4_GAME_UP, M4_GAME_DOWN } m4_game_direction_t;
typedef struct {
    uint8_t tiles[16]; /* Powers of two; zero means empty. */
    uint32_t score, random;
    bool won;
} m4_2048_t;
void m4_2048_reset(m4_2048_t *game, uint32_t seed);
bool m4_2048_slide(m4_2048_t *game, m4_game_direction_t direction);
bool m4_2048_move(m4_2048_t *game, m4_game_direction_t direction);
bool m4_2048_can_move(const m4_2048_t *game);
