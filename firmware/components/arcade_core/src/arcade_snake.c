/* SPDX-License-Identifier: MIT */
#include "arcade/arcade_snake.h"

#include <string.h>

static int is_opposite(arcade_dir_t a, arcade_dir_t b)
{
    return ((a == ARCADE_DIR_UP && b == ARCADE_DIR_DOWN) || (a == ARCADE_DIR_DOWN && b == ARCADE_DIR_UP) ||
            (a == ARCADE_DIR_LEFT && b == ARCADE_DIR_RIGHT) || (a == ARCADE_DIR_RIGHT && b == ARCADE_DIR_LEFT));
}

static void step_delta(arcade_dir_t dir, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;
    switch (dir) {
    case ARCADE_DIR_UP:
        *dy = -1;
        break;
    case ARCADE_DIR_DOWN:
        *dy = 1;
        break;
    case ARCADE_DIR_LEFT:
        *dx = -1;
        break;
    case ARCADE_DIR_RIGHT:
    default:
        *dx = 1;
        break;
    }
}

void arcade_snake_place_food(arcade_snake_t *game, arcade_rng_t *rng)
{
    uint8_t occupied[ARCADE_SNAKE_ROWS][ARCADE_SNAKE_COLS];
    memset(occupied, 0, sizeof(occupied));
    for (int i = 0; i < game->length; i++) {
        int x = game->body[i].x;
        int y = game->body[i].y;
        if (x >= 0 && x < ARCADE_SNAKE_COLS && y >= 0 && y < ARCADE_SNAKE_ROWS) {
            occupied[y][x] = 1;
        }
    }

    int free_cells = ARCADE_SNAKE_COLS * ARCADE_SNAKE_ROWS - game->length;
    if (free_cells <= 0) {
        game->won = 1;
        game->alive = 0;
        return;
    }

    int pick = arcade_rng_below(rng, free_cells);
    for (int y = 0; y < ARCADE_SNAKE_ROWS; y++) {
        for (int x = 0; x < ARCADE_SNAKE_COLS; x++) {
            if (occupied[y][x]) {
                continue;
            }
            if (pick == 0) {
                game->food.x = (int16_t)x;
                game->food.y = (int16_t)y;
                return;
            }
            pick--;
        }
    }
}

void arcade_snake_init(arcade_snake_t *game, arcade_rng_t *rng)
{
    memset(game, 0, sizeof(*game));
    int cx = ARCADE_SNAKE_COLS / 2;
    int cy = ARCADE_SNAKE_ROWS / 2;
    game->body[0].x = (int16_t)cx;
    game->body[0].y = (int16_t)cy;
    game->body[1].x = (int16_t)(cx - 1);
    game->body[1].y = (int16_t)cy;
    game->body[2].x = (int16_t)(cx - 2);
    game->body[2].y = (int16_t)cy;
    game->length = 3;
    game->dir = ARCADE_DIR_RIGHT;
    game->queue_len = 0;
    game->alive = 1;
    game->won = 0;
    arcade_snake_place_food(game, rng);
}

int arcade_snake_turn(arcade_snake_t *game, arcade_dir_t dir)
{
    if (!game->alive || game->queue_len >= 2) {
        return 0;
    }
    arcade_dir_t reference = (game->queue_len > 0) ? game->queue[game->queue_len - 1] : game->dir;
    if (dir == reference || is_opposite(dir, reference)) {
        return 0;
    }
    game->queue[game->queue_len++] = dir;
    return 1;
}

uint32_t arcade_snake_step_ms(const arcade_snake_t *game)
{
    /* Signed arithmetic: 220 - n*6 must clamp instead of wrapping around. */
    int32_t speed = (int32_t)ARCADE_SNAKE_START_MS - (int32_t)game->foods_eaten * (int32_t)ARCADE_SNAKE_STEP_MS;
    if (speed < (int32_t)ARCADE_SNAKE_MIN_MS) {
        speed = (int32_t)ARCADE_SNAKE_MIN_MS;
    }
    return (uint32_t)speed;
}

int arcade_snake_step(arcade_snake_t *game, arcade_rng_t *rng)
{
    if (!game->alive) {
        return ARCADE_SNAKE_EVENT_NONE;
    }

    if (game->queue_len > 0) {
        game->dir = game->queue[0];
        game->queue[0] = game->queue[1];
        game->queue_len--;
    }

    int dx = 0;
    int dy = 0;
    step_delta(game->dir, &dx, &dy);

    int head_x = game->body[0].x + dx;
    int head_y = game->body[0].y + dy;

    if (head_x < 0 || head_x >= ARCADE_SNAKE_COLS || head_y < 0 || head_y >= ARCADE_SNAKE_ROWS) {
        game->alive = 0;
        return ARCADE_SNAKE_EVENT_DIED;
    }

    int eating = (head_x == game->food.x && head_y == game->food.y);
    int check_len = game->length - (eating ? 0 : 1);
    for (int i = 0; i < check_len; i++) {
        if (game->body[i].x == head_x && game->body[i].y == head_y) {
            game->alive = 0;
            return ARCADE_SNAKE_EVENT_DIED;
        }
    }

    int new_len = game->length + (eating ? 1 : 0);
    if (new_len > ARCADE_SNAKE_MAX_LEN) {
        new_len = ARCADE_SNAKE_MAX_LEN;
    }
    for (int i = new_len - 1; i >= 1; i--) {
        game->body[i] = game->body[i - 1];
    }
    game->body[0].x = (int16_t)head_x;
    game->body[0].y = (int16_t)head_y;
    game->length = (int16_t)new_len;

    int events = ARCADE_SNAKE_EVENT_MOVED;
    if (eating) {
        game->foods_eaten++;
        game->score += 10;
        events |= ARCADE_SNAKE_EVENT_ATE;
        arcade_snake_place_food(game, rng);
        if (game->won) {
            events |= ARCADE_SNAKE_EVENT_WON;
        }
    }
    return events;
}
