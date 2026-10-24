/* SPDX-License-Identifier: MIT */
#include "arcade/arcade_breakout.h"

#include <string.h>

/* ------------------------------------------------------------------ helpers */

static int32_t speed_to_velocity(int32_t pixels_per_second)
{
    /* px/s -> (1/256 px) per tick */
    return (int32_t)(((int64_t)pixels_per_second * ARCADE_BREAKOUT_TICK_MS * ARCADE_FP_ONE) / 1000);
}

static int32_t isqrt32(uint32_t value)
{
    uint32_t result = 0;
    uint32_t bit = 1u << 30;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)result;
}

static void brick_rect_fp(int row, int col, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = ARCADE_FP(ARCADE_BREAKOUT_BRICK_X0 + col * (ARCADE_BREAKOUT_BRICK_W + ARCADE_BREAKOUT_BRICK_GAP));
    *y = ARCADE_FP(ARCADE_BREAKOUT_BRICK_Y0 + row * (ARCADE_BREAKOUT_BRICK_H + ARCADE_BREAKOUT_BRICK_GAP));
    *w = ARCADE_FP(ARCADE_BREAKOUT_BRICK_W);
    *h = ARCADE_FP(ARCADE_BREAKOUT_BRICK_H);
}

static void park_ball(arcade_breakout_t *game)
{
    game->x = game->paddle_x + ARCADE_FP(ARCADE_BREAKOUT_PADDLE_W) / 2;
    game->y = ARCADE_FP(ARCADE_BREAKOUT_PADDLE_Y) - ARCADE_FP(ARCADE_BREAKOUT_BALL_R);
    game->vx = 0;
    game->vy = 0;
}

static void launch_ball(arcade_breakout_t *game)
{
    int32_t speed = speed_to_velocity(ARCADE_BREAKOUT_BASE_SPEED);
    game->vx = (speed * 3) / 10;
    int32_t rest = speed * speed - game->vx * game->vx;
    game->vy = (rest > 0) ? -isqrt32((uint32_t)rest) : -speed;
}

static void bounce_off_paddle(arcade_breakout_t *game)
{
    int32_t speed = speed_to_velocity(ARCADE_BREAKOUT_BASE_SPEED);
    int32_t paddle_center = game->paddle_x + ARCADE_FP(ARCADE_BREAKOUT_PADDLE_W) / 2;
    int32_t half = ARCADE_FP(ARCADE_BREAKOUT_PADDLE_W) / 2;
    int32_t offset = 0;
    if (half > 0) {
        offset = ((game->x - paddle_center) * ARCADE_FP_ONE) / half;
    }
    if (offset > ARCADE_FP_ONE) {
        offset = ARCADE_FP_ONE;
    }
    if (offset < -ARCADE_FP_ONE) {
        offset = -ARCADE_FP_ONE;
    }

    /* Keep a little horizontal energy so the ball never bounces straight up for ever. */
    int32_t vx = (speed * offset * 3) / (ARCADE_FP_ONE * 4);
    if (vx > -(speed / 5) && vx < (speed / 5)) {
        vx = (offset < 0) ? -(speed / 5) : (speed / 5);
    }
    int32_t rest = speed * speed - vx * vx;
    game->vx = vx;
    game->vy = (rest > 0) ? -isqrt32((uint32_t)rest) : -speed;
    game->y = ARCADE_FP(ARCADE_BREAKOUT_PADDLE_Y) - ARCADE_FP(ARCADE_BREAKOUT_BALL_R);
}

static void lose_life(arcade_breakout_t *game)
{
    if (game->lives > 0) {
        game->lives--;
    }
    if (game->lives == 0) {
        game->state = ARCADE_STATE_OVER;
        game->vx = 0;
        game->vy = 0;
    } else {
        game->state = ARCADE_STATE_READY;
        park_ball(game);
    }
}

/* --------------------------------------------------------------- public API */

void arcade_breakout_init(arcade_breakout_t *game)
{
    memset(game, 0, sizeof(*game));
    for (int row = 0; row < ARCADE_BREAKOUT_ROWS; row++) {
        for (int col = 0; col < ARCADE_BREAKOUT_COLS; col++) {
            game->brick[row][col] = 1;
        }
    }
    game->bricks_left = ARCADE_BREAKOUT_BRICKS;
    game->lives = ARCADE_BREAKOUT_LIVES;
    game->paddle_x = ARCADE_FP((ARCADE_BREAKOUT_W - ARCADE_BREAKOUT_PADDLE_W) / 2);
    game->state = ARCADE_STATE_READY;
    park_ball(game);
}

void arcade_breakout_launch(arcade_breakout_t *game)
{
    if (game->state != ARCADE_STATE_READY) {
        return;
    }
    game->state = ARCADE_STATE_RUNNING;
    launch_ball(game);
}

void arcade_breakout_move_paddle(arcade_breakout_t *game, int32_t x)
{
    int32_t left = x - ARCADE_FP(ARCADE_BREAKOUT_PADDLE_W) / 2;
    int32_t max_left = ARCADE_FP(ARCADE_BREAKOUT_W - ARCADE_BREAKOUT_PADDLE_W);
    if (left < 0) {
        left = 0;
    }
    if (left > max_left) {
        left = max_left;
    }
    game->paddle_x = left;
    if (game->state == ARCADE_STATE_READY) {
        park_ball(game);
    }
}

void arcade_breakout_brick_rect(int row, int col, int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = ARCADE_BREAKOUT_BRICK_X0 + col * (ARCADE_BREAKOUT_BRICK_W + ARCADE_BREAKOUT_BRICK_GAP);
    }
    if (y != NULL) {
        *y = ARCADE_BREAKOUT_BRICK_Y0 + row * (ARCADE_BREAKOUT_BRICK_H + ARCADE_BREAKOUT_BRICK_GAP);
    }
    if (w != NULL) {
        *w = ARCADE_BREAKOUT_BRICK_W;
    }
    if (h != NULL) {
        *h = ARCADE_BREAKOUT_BRICK_H;
    }
}

void arcade_breakout_tick(arcade_breakout_t *game)
{
    if (game->state == ARCADE_STATE_READY) {
        park_ball(game);
        return;
    }
    if (game->state != ARCADE_STATE_RUNNING) {
        return;
    }

    game->ticks++;

    game->x += game->vx;
    game->y += game->vy;

    const int32_t r = ARCADE_FP(ARCADE_BREAKOUT_BALL_R);
    const int32_t width = ARCADE_FP(ARCADE_BREAKOUT_W);
    const int32_t height = ARCADE_FP(ARCADE_BREAKOUT_H);

    /* Side walls */
    if (game->x < r) {
        game->x = r;
        game->vx = -game->vx;
    } else if (game->x > width - r) {
        game->x = width - r;
        game->vx = -game->vx;
    }
    /* Ceiling */
    if (game->y < r) {
        game->y = r;
        game->vy = -game->vy;
    }

    /* Paddle */
    const int32_t paddle_top = ARCADE_FP(ARCADE_BREAKOUT_PADDLE_Y);
    const int32_t paddle_bottom = paddle_top + ARCADE_FP(ARCADE_BREAKOUT_PADDLE_H);
    if (game->vy > 0 && game->y + r >= paddle_top && game->y - r <= paddle_bottom &&
        game->x + r >= game->paddle_x && game->x - r <= game->paddle_x + ARCADE_FP(ARCADE_BREAKOUT_PADDLE_W)) {
        bounce_off_paddle(game);
    }

    /* Bricks: one collision per tick keeps the reflection unambiguous. */
    for (int row = 0; row < ARCADE_BREAKOUT_ROWS && game->state == ARCADE_STATE_RUNNING; row++) {
        for (int col = 0; col < ARCADE_BREAKOUT_COLS; col++) {
            if (!game->brick[row][col]) {
                continue;
            }
            int32_t bx, by, bw, bh;
            brick_rect_fp(row, col, &bx, &by, &bw, &bh);
            int32_t ball_left = game->x - r;
            int32_t ball_right = game->x + r;
            int32_t ball_top = game->y - r;
            int32_t ball_bottom = game->y + r;
            if (ball_right <= bx || ball_left >= bx + bw || ball_bottom <= by || ball_top >= by + bh) {
                continue;
            }

            int32_t overlap_x = ((ball_right < bx + bw) ? ball_right : bx + bw) -
                                ((ball_left > bx) ? ball_left : bx);
            int32_t overlap_y = ((ball_bottom < by + bh) ? ball_bottom : by + bh) -
                                ((ball_top > by) ? ball_top : by);
            if (overlap_x < overlap_y) {
                game->vx = -game->vx;
            } else {
                game->vy = -game->vy;
            }

            game->brick[row][col] = 0;
            game->bricks_left--;
            game->score += ARCADE_BREAKOUT_SCORE_BRICK;
            if (game->bricks_left == 0) {
                game->state = ARCADE_STATE_WON;
                game->vx = 0;
                game->vy = 0;
            }
            break;
        }
    }

    if (game->state != ARCADE_STATE_RUNNING) {
        return;
    }

    /* Bottom: the ball is lost. */
    if (game->y - r > height) {
        lose_life(game);
    }
}
