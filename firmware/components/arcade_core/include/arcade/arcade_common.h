/* Little World Arcade - shared, platform independent game types.
 *
 * This component is deliberately free of ESP-IDF and LVGL dependencies so the
 * rules can be unit tested on the build machine and reused later by the formal
 * Little World desktop shell.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Games known to the arcade shell. Order matches the save-file score slots. */
typedef enum {
    ARCADE_GAME_2048 = 0,
    ARCADE_GAME_SNAKE = 1,
    ARCADE_GAME_BREAKOUT = 2,
    ARCADE_GAME_COUNT = 3,
} arcade_game_id_t;

typedef enum {
    ARCADE_DIR_UP = 0,
    ARCADE_DIR_RIGHT = 1,
    ARCADE_DIR_DOWN = 2,
    ARCADE_DIR_LEFT = 3,
} arcade_dir_t;

typedef enum {
    ARCADE_STATE_READY = 0,   /**< waiting for the player (e.g. launch) */
    ARCADE_STATE_RUNNING = 1, /**< simulation advancing */
    ARCADE_STATE_PAUSED = 2,
    ARCADE_STATE_OVER = 3,    /**< lost */
    ARCADE_STATE_WON = 4,     /**< objective reached */
} arcade_state_t;

typedef struct {
    int16_t x;
    int16_t y;
} arcade_point_t;

/** Deterministic xorshift32 generator (never returns 0 as the internal state). */
typedef struct {
    uint32_t state;
} arcade_rng_t;

/** Seed the generator. A seed of 0 is replaced by a fixed non-zero constant. */
void arcade_rng_seed(arcade_rng_t *rng, uint32_t seed);

/** Next pseudo random value. */
uint32_t arcade_rng_next(arcade_rng_t *rng);

/** Uniform value in [0, bound). Returns 0 when bound <= 0. */
int32_t arcade_rng_below(arcade_rng_t *rng, int32_t bound);

#ifdef __cplusplus
}
#endif
