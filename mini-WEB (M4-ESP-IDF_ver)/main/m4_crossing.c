#include "m4_crossing.h"
#include <string.h>

/* Bounded arithmetic, deterministic lanes: no heap, files or network. */
static uint32_t lane_hash(const m4_crossing_t *game, uint32_t row) {
    uint32_t value = (row % 65521U) * 73U + game->seed % 65521U;
    return (value * 17U + 23U) % 65521U;
}
void m4_crossing_reset(m4_crossing_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->seed = seed;
    game->x = M4_CROSS_WIDTH / 2;
}
m4_cross_lane_t m4_crossing_lane(const m4_crossing_t *game, uint32_t row) {
    if (row % 4U == 0) return M4_CROSS_GRASS;
    return lane_hash(game, row) % 5U == 0 ? M4_CROSS_RIVER : M4_CROSS_ROAD;
}
int m4_crossing_speed(const m4_crossing_t *game, uint32_t row) {
    uint32_t value = lane_hash(game, row);
    int speed = 1 + (value / 7U) % 4U;
    return value % 2U ? speed : -speed;
}
int m4_crossing_offset(const m4_crossing_t *game, uint32_t row) {
    int offset = (int)(lane_hash(game, row) % M4_CROSS_PERIOD) +
        (int)game->ticks * m4_crossing_speed(game, row);
    offset %= M4_CROSS_PERIOD;
    return offset < 0 ? offset + M4_CROSS_PERIOD : offset;
}
int m4_crossing_length(const m4_crossing_t *game, uint32_t row) {
    return m4_crossing_lane(game, row) == M4_CROSS_RIVER ? 126 : 48 + lane_hash(game, row) % 25U;
}
bool m4_crossing_safe(const m4_crossing_t *game) {
    if (game->x < 10 || game->x > M4_CROSS_WIDTH - 10 || game->row < game->camera) return false;
    m4_cross_lane_t lane = m4_crossing_lane(game, game->row);
    if (lane == M4_CROSS_GRASS) return true;
    int relative = (game->x - m4_crossing_offset(game, game->row) + M4_CROSS_PERIOD) % M4_CROSS_PERIOD;
    int length = m4_crossing_length(game, game->row);
    /* Require the whole player to fit on a log. Road collision includes its width. */
    if (lane == M4_CROSS_RIVER) return relative >= 10 && relative <= length - 10;
    return relative > length + 10 && relative < M4_CROSS_PERIOD - 10;
}
void m4_crossing_move(m4_crossing_t *game, m4_game_direction_t direction) {
    if (game->dead) return;
    if (direction == M4_GAME_LEFT) {
        if (game->x - M4_CROSS_CELL < 10) return;
        game->x -= M4_CROSS_CELL;
    } else if (direction == M4_GAME_RIGHT) {
        if (game->x + M4_CROSS_CELL > M4_CROSS_WIDTH - 10) return;
        game->x += M4_CROSS_CELL;
    } else if (direction == M4_GAME_UP) {
        if (game->row >= 1000000U) return;
        ++game->row;
    } else if (direction == M4_GAME_DOWN) {
        if (game->row == 0) return;
        --game->row;
    } else return;
    game->started = true;
    if (game->row > game->best_row) {
        game->best_row = game->row;
        game->idle_ticks = 0;
        if (game->best_row > game->camera + 3U) game->camera = game->best_row - 3U;
    }
    game->dead = !m4_crossing_safe(game);
}
void m4_crossing_tick(m4_crossing_t *game) {
    if (game->dead || !game->started) return;
    /* An exact traffic period avoids overflow and discontinuities on long runs. */
    game->ticks = (game->ticks + 1U) % M4_CROSS_PERIOD;
    if (m4_crossing_lane(game, game->row) == M4_CROSS_RIVER)
        game->x += m4_crossing_speed(game, game->row);
    if (++game->idle_ticks >= 140U) {
        ++game->camera;
        game->idle_ticks = 120U;
    }
    game->dead = !m4_crossing_safe(game);
}
