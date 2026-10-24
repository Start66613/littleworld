/* Little World Arcade - breakout rules.
 *
 * Fixed 16 ms simulation tick with 8.8 fixed point positions (1/256 px) so the
 * simulation is deterministic and identical on the host tests and the target.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "arcade/arcade_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARCADE_BREAKOUT_W          1008
#define ARCADE_BREAKOUT_H          468
#define ARCADE_BREAKOUT_COLS       10
#define ARCADE_BREAKOUT_ROWS       6
#define ARCADE_BREAKOUT_BRICKS     (ARCADE_BREAKOUT_COLS * ARCADE_BREAKOUT_ROWS)
#define ARCADE_BREAKOUT_BRICK_W    92
#define ARCADE_BREAKOUT_BRICK_H    30
#define ARCADE_BREAKOUT_BRICK_GAP  8
#define ARCADE_BREAKOUT_BRICK_X0   8
#define ARCADE_BREAKOUT_BRICK_Y0   24
#define ARCADE_BREAKOUT_PADDLE_W   150
#define ARCADE_BREAKOUT_PADDLE_H   18
#define ARCADE_BREAKOUT_PADDLE_Y   (ARCADE_BREAKOUT_H - 40)
#define ARCADE_BREAKOUT_BALL_R     7
#define ARCADE_BREAKOUT_LIVES      3
#define ARCADE_BREAKOUT_TICK_MS    16
#define ARCADE_BREAKOUT_BASE_SPEED 380 /* px per second */
#define ARCADE_BREAKOUT_SCORE_BRICK 10

/** Fixed point helpers: positions/velocities are stored in 1/256 px units. */
#define ARCADE_FP_ONE 256
#define ARCADE_FP(px) ((int32_t)(px) * ARCADE_FP_ONE)

typedef struct {
    /* ball, fixed point */
    int32_t x, y;   /**< centre */
    int32_t vx, vy; /**< per tick */
    /* paddle, fixed point (left edge) */
    int32_t paddle_x;
    uint8_t brick[ARCADE_BREAKOUT_ROWS][ARCADE_BREAKOUT_COLS]; /**< 1 = alive */
    uint16_t bricks_left;
    uint8_t  lives;
    uint32_t score;
    arcade_state_t state;
    uint32_t ticks; /**< simulation ticks since launch, for tests/telemetry */
} arcade_breakout_t;

/** Reset a full round: all bricks, three lives, ball parked on the paddle. */
void arcade_breakout_init(arcade_breakout_t *game);

/** Launch the parked ball. No effect unless the game is in READY state. */
void arcade_breakout_launch(arcade_breakout_t *game);

/** Move the paddle so its centre is at @p x (fixed point, clamped). */
void arcade_breakout_move_paddle(arcade_breakout_t *game, int32_t x);

/** Advance exactly ARCADE_BREAKOUT_TICK_MS. */
void arcade_breakout_tick(arcade_breakout_t *game);

/** Convenience: centre of a brick in pixels. */
void arcade_breakout_brick_rect(int row, int col, int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif
