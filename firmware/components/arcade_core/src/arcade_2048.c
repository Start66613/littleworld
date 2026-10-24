/* SPDX-License-Identifier: MIT */
#include "arcade/arcade_2048.h"

#include <string.h>

static int cell_index(int row, int col)
{
    return row * ARCADE_2048_N + col;
}

/* Map position along a line to a board cell index.
 * LEFT/RIGHT walk a row, UP/DOWN walk a column. */
static int line_cell(arcade_dir_t dir, int line, int step)
{
    switch (dir) {
    case ARCADE_DIR_LEFT:
        return cell_index(line, step);
    case ARCADE_DIR_RIGHT:
        return cell_index(line, ARCADE_2048_N - 1 - step);
    case ARCADE_DIR_UP:
        return cell_index(step, line);
    case ARCADE_DIR_DOWN:
    default:
        return cell_index(ARCADE_2048_N - 1 - step, line);
    }
}

static int spawn_tile(arcade_2048_t *game, arcade_rng_t *rng, int *out_index, uint16_t *out_value)
{
    uint16_t empty[ARCADE_2048_CELLS];
    int count = 0;
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        if (game->cells[i] == 0) {
            empty[count++] = (uint16_t)i;
        }
    }
    if (count == 0) {
        return 0;
    }
    int pick = arcade_rng_below(rng, count);
    int index = empty[pick];
    /* Classic 2048 distribution: 90% twos, 10% fours. */
    uint16_t value = (arcade_rng_below(rng, 10) == 0) ? 4u : 2u;
    game->cells[index] = value;
    if (out_index != NULL) {
        *out_index = index;
    }
    if (out_value != NULL) {
        *out_value = value;
    }
    return 1;
}

void arcade_2048_init(arcade_2048_t *game, arcade_rng_t *rng)
{
    memset(game, 0, sizeof(*game));
    (void)spawn_tile(game, rng, NULL, NULL);
    (void)spawn_tile(game, rng, NULL, NULL);
}

int arcade_2048_can_move(const arcade_2048_t *game)
{
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        if (game->cells[i] == 0) {
            return 1;
        }
    }
    for (int row = 0; row < ARCADE_2048_N; row++) {
        for (int col = 0; col < ARCADE_2048_N; col++) {
            uint16_t value = game->cells[cell_index(row, col)];
            if (col + 1 < ARCADE_2048_N && game->cells[cell_index(row, col + 1)] == value) {
                return 1;
            }
            if (row + 1 < ARCADE_2048_N && game->cells[cell_index(row + 1, col)] == value) {
                return 1;
            }
        }
    }
    return 0;
}

uint16_t arcade_2048_max_tile(const arcade_2048_t *game)
{
    uint16_t max = 0;
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        if (game->cells[i] > max) {
            max = game->cells[i];
        }
    }
    return max;
}

arcade_2048_result_t arcade_2048_move(arcade_2048_t *game, arcade_dir_t dir, arcade_rng_t *rng)
{
    arcade_2048_result_t result;
    memset(&result, 0, sizeof(result));
    result.spawn_index = -1;

    uint8_t merged_flag[ARCADE_2048_CELLS];
    memset(merged_flag, 0, sizeof(merged_flag));

    int changed = 0;

    for (int line = 0; line < ARCADE_2048_N; line++) {
        uint16_t in[ARCADE_2048_N];
        uint16_t out[ARCADE_2048_N];
        int out_n = 0;

        for (int step = 0; step < ARCADE_2048_N; step++) {
            in[step] = game->cells[line_cell(dir, line, step)];
        }
        memset(out, 0, sizeof(out));

        for (int step = 0; step < ARCADE_2048_N; step++) {
            uint16_t value = in[step];
            if (value == 0) {
                continue;
            }
            if (out_n > 0 && out[out_n - 1] == value && !merged_flag[line * ARCADE_2048_N + out_n - 1]) {
                /* Merge into the previous tile; only one merge per tile per move. */
                uint16_t merged = (uint16_t)(value * 2u);
                out[out_n - 1] = merged;
                result.gained += merged;
                result.merged_mask |= (1u << line_cell(dir, line, out_n - 1));
                merged_flag[line * ARCADE_2048_N + out_n - 1] = 1;
                if (merged >= ARCADE_2048_TARGET) {
                    game->won = 1;
                }
            } else {
                out[out_n] = value;
                out_n++;
            }
        }

        for (int step = 0; step < ARCADE_2048_N; step++) {
            int index = line_cell(dir, line, step);
            if (game->cells[index] != out[step]) {
                changed = 1;
            }
            game->cells[index] = out[step];
        }
    }

    if (!changed) {
        result.moved = 0;
        game->over = arcade_2048_can_move(game) ? 0 : 1;
        return result;
    }

    result.moved = 1;
    game->score += result.gained;
    game->moves++;

    int spawn_index = -1;
    uint16_t spawn_value = 0;
    if (spawn_tile(game, rng, &spawn_index, &spawn_value)) {
        result.spawn_index = spawn_index;
        result.spawn_value = spawn_value;
    }
    game->over = arcade_2048_can_move(game) ? 0 : 1;
    return result;
}
