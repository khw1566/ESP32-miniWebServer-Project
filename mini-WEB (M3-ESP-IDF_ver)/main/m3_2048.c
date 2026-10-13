#include "m3_2048.h"
#include <string.h>
#include <limits.h>
static uint32_t random_next(m3_2048_t *game) {
    uint32_t x = game->random;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    game->random = x; return x;
}
static void spawn(m3_2048_t *game) {
    unsigned empty[16], count = 0;
    for (unsigned i = 0; i < 16; ++i) if (!game->tiles[i]) empty[count++] = i;
    if (count) {
        unsigned index = empty[random_next(game) % count];
        game->tiles[index] = random_next(game) % 10 ? 1 : 2;
    }
}
static unsigned cell(m3_game_direction_t direction, unsigned line, unsigned step) {
    if (direction == M3_GAME_LEFT) return line * 4 + step;
    if (direction == M3_GAME_RIGHT) return line * 4 + 3 - step;
    if (direction == M3_GAME_UP) return step * 4 + line;
    return (3 - step) * 4 + line;
}
bool m3_2048_slide(m3_2048_t *game, m3_game_direction_t direction) {
    if (!game || direction < M3_GAME_LEFT || direction > M3_GAME_DOWN) return false;
    bool changed = false;
    for (unsigned line = 0; line < 4; ++line) {
        uint8_t input[4] = {0}, output[4] = {0};
        unsigned count = 0, written = 0;
        for (unsigned step = 0; step < 4; ++step) {
            uint8_t tile = game->tiles[cell(direction, line, step)];
            if (tile) input[count++] = tile;
        }
        for (unsigned i = 0; i < count; ++i) {
            uint8_t tile = input[i];
            if (i + 1 < count && input[i + 1] == tile && tile < 30) {
                ++tile; ++i;
                uint32_t points = UINT32_C(1) << tile;
                game->score = UINT32_MAX - game->score < points ? UINT32_MAX : game->score + points;
            }
            if (tile >= 11) game->won = true;
            output[written++] = tile;
        }
        for (unsigned step = 0; step < 4; ++step) {
            unsigned index = cell(direction, line, step);
            if (game->tiles[index] != output[step]) changed = true;
            game->tiles[index] = output[step];
        }
    }
    return changed;
}
bool m3_2048_can_move(const m3_2048_t *game) {
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        uint8_t tile = game->tiles[y * 4 + x];
        if (!tile) return true;
        if (tile < 30 && ((x < 3 && tile == game->tiles[y * 4 + x + 1]) ||
                          (y < 3 && tile == game->tiles[(y + 1) * 4 + x]))) return true;
    }
    return false;
}
bool m3_2048_move(m3_2048_t *game, m3_game_direction_t direction) {
    if (!m3_2048_slide(game, direction)) return false;
    spawn(game); return true;
}
void m3_2048_reset(m3_2048_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game)); game->random = seed ? seed : UINT32_C(0x48a2048);
    spawn(game); spawn(game);
}
