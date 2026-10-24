/* Little World Arcade - 2048 rules.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "arcade/arcade_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARCADE_2048_N      4
#define ARCADE_2048_CELLS  (ARCADE_2048_N * ARCADE_2048_N)
#define ARCADE_2048_TARGET 2048

typedef struct {
    uint16_t cells[ARCADE_2048_CELLS]; /**< row major, 0 = empty, else 2/4/8... */
    uint32_t score;
    uint32_t moves;
    uint8_t  won;    /**< a 2048 tile has been created */
    uint8_t  over;   /**< no move is possible any more */
} arcade_2048_t;

typedef struct {
    int      moved;         /**< the board changed */
    uint32_t gained;        /**< points scored by merges */
    uint32_t merged_mask;   /**< bit i set: cell i is the result of a merge */
    int      spawn_index;   /**< cell index of the new tile, or -1 */
    uint16_t spawn_value;   /**< value of the new tile, or 0 */
} arcade_2048_result_t;

/** Reset the board and place the two starting tiles. */
void arcade_2048_init(arcade_2048_t *game, arcade_rng_t *rng);

/**
 * Apply one swipe.
 * @return what changed; the caller uses it for animation and sound.
 */
arcade_2048_result_t arcade_2048_move(arcade_2048_t *game, arcade_dir_t dir, arcade_rng_t *rng);

/** True when at least one swipe would change the board. */
int arcade_2048_can_move(const arcade_2048_t *game);

/** Highest tile on the board (0 when empty). */
uint16_t arcade_2048_max_tile(const arcade_2048_t *game);

#ifdef __cplusplus
}
#endif
