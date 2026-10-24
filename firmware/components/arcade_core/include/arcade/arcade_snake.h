/* Little World Arcade - snake rules.
 *
 * Grid based, walls are deadly, one food item at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "arcade/arcade_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARCADE_SNAKE_COLS      28
#define ARCADE_SNAKE_ROWS      13
#define ARCADE_SNAKE_MAX_LEN   (ARCADE_SNAKE_COLS * ARCADE_SNAKE_ROWS)
#define ARCADE_SNAKE_START_MS  220
#define ARCADE_SNAKE_MIN_MS    90
#define ARCADE_SNAKE_STEP_MS   6   /* speed-up per food */

typedef struct {
    arcade_point_t body[ARCADE_SNAKE_MAX_LEN]; /**< body[0] is the head */
    int16_t  length;
    arcade_dir_t dir;      /**< direction used by the last step */
    arcade_dir_t queue[2]; /**< buffered turns, applied one per step */
    int8_t   queue_len;
    arcade_point_t food;
    uint32_t score;
    uint32_t foods_eaten;
    uint8_t  alive;
    uint8_t  won;          /**< the serpent filled the whole grid */
} arcade_snake_t;

typedef enum {
    ARCADE_SNAKE_EVENT_NONE  = 0,
    ARCADE_SNAKE_EVENT_MOVED = 1 << 0,
    ARCADE_SNAKE_EVENT_ATE   = 1 << 1,
    ARCADE_SNAKE_EVENT_DIED  = 1 << 2,
    ARCADE_SNAKE_EVENT_WON   = 1 << 3,
} arcade_snake_event_t;

/** Reset to length 3, centred, heading right, with one food item placed. */
void arcade_snake_init(arcade_snake_t *game, arcade_rng_t *rng);

/**
 * Queue a turn. Turns opposite to the last accepted direction (and duplicates)
 * are rejected so a fast double swipe cannot make the snake bite itself.
 * @return 1 when the turn was accepted.
 */
int arcade_snake_turn(arcade_snake_t *game, arcade_dir_t dir);

/** Advance one grid cell. Returns a bit mask of arcade_snake_event_t. */
int arcade_snake_step(arcade_snake_t *game, arcade_rng_t *rng);

/** Current step interval in milliseconds, shrinking as the snake grows. */
uint32_t arcade_snake_step_ms(const arcade_snake_t *game);

/** Place a new food item on a free cell (also used after init). */
void arcade_snake_place_food(arcade_snake_t *game, arcade_rng_t *rng);

#ifdef __cplusplus
}
#endif
