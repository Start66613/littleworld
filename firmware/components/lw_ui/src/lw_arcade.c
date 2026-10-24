/* Little World - arcade hub. The games themselves are the tested arcade_core
 * logic from Little World Arcade, wrapped in a full screen game overlay.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "lw_app_state.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "arcade/arcade_2048.h"
#include "arcade/arcade_breakout.h"
#include "arcade/arcade_snake.h"

static lv_obj_t *s_overlay;

static void close_overlay(void)
{
    if (s_overlay != NULL) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
}

static void close_clicked(lv_event_t *event)
{
    (void)event;
    close_overlay();
}

/* 2048 ------------------------------------------------------------------- */

typedef struct {
    arcade_2048_t model;
    arcade_rng_t rng;
    lv_obj_t *tiles[16];
    lv_obj_t *labels[16];
    lv_obj_t *score;
    lv_obj_t *board;
    arcade_point_t press;
    int pressed;
} game_2048_t;

static game_2048_t s_2048;

static uint32_t tile_color(uint16_t value)
{
    switch (value) {
    case 2:
        return 0xFFF4E3;
    case 4:
        return 0xFFE7C6;
    case 8:
        return 0xFFD3A0;
    case 16:
        return 0xFFB877;
    case 32:
        return 0xFF9E76;
    case 64:
        return 0xF97F63;
    case 128:
        return 0xF3C96B;
    default:
        return 0xD98B2B;
    }
}

static void game_2048_refresh(void)
{
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        uint16_t value = s_2048.model.cells[i];
        if (value == 0) {
            lv_obj_add_flag(s_2048.tiles[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_2048.tiles[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_2048.tiles[i], lv_color_hex(tile_color(value)), 0);
        lv_label_set_text_fmt(s_2048.labels[i], "%u", (unsigned)value);
        lv_obj_set_style_text_font(s_2048.labels[i], value < 100 ? &lw_font_44 : &lw_font_28, 0);
        lv_obj_set_style_text_color(s_2048.labels[i], lv_color_hex(value <= 4 ? 0x8A5A2B : 0xFFFFFF), 0);
    }
    lv_label_set_text_fmt(s_2048.score, "得分 %u", (unsigned)s_2048.model.score);
}

static void game_2048_swipe(int dx, int dy)
{
    if (s_2048.model.over) {
        return;
    }
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 30 && ady < 30) {
        return;
    }
    arcade_dir_t dir;
    if (adx >= ady) {
        dir = dx > 0 ? ARCADE_DIR_RIGHT : ARCADE_DIR_LEFT;
    } else {
        dir = dy > 0 ? ARCADE_DIR_DOWN : ARCADE_DIR_UP;
    }
    arcade_2048_result_t result = arcade_2048_move(&s_2048.model, dir, &s_2048.rng);
    if (result.moved) {
        game_2048_refresh();
    }
}

static void game_2048_pressed(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    s_2048.press.x = (int16_t)point.x;
    s_2048.press.y = (int16_t)point.y;
    s_2048.pressed = 1;
    (void)event;
}

static void game_2048_released(lv_event_t *event)
{
    (void)event;
    if (!s_2048.pressed) {
        return;
    }
    s_2048.pressed = 0;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    game_2048_swipe((int)point.x - s_2048.press.x, (int)point.y - s_2048.press.y);
}

static void open_game_2048(void)
{
    const lw_palette_t *palette = lw_palette();
    s_2048.rng.state = 0x1234567u;
    arcade_2048_init(&s_2048.model, &s_2048.rng);

    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(lw_palette()->bg_top), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_move_foreground(overlay);
    s_overlay = overlay;

    lv_obj_t *back = lw_button(overlay, "返回游戏中心", 180, 48, palette->card, palette->text, &lw_font_18);
    lv_obj_set_pos(back, 24, 16);
    lv_obj_add_event_cb(back, close_clicked, LV_EVENT_CLICKED, NULL);

    s_2048.score = lw_pill(overlay, "得分 0", palette->card_soft, palette->accent_deep);
    lv_obj_set_pos(s_2048.score, 820, 26);

    lv_obj_t *board = lv_obj_create(overlay);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, 450, 450);
    lv_obj_align(board, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_style_radius(board, 20, 0);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xF2DFC9), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(board, game_2048_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(board, game_2048_released, LV_EVENT_RELEASED, NULL);

    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        lv_obj_t *tile = lv_obj_create(board);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 100, 100);
        lv_obj_set_pos(tile, 10 + (i % 4) * 110, 10 + (i / 4) * 110);
        lv_obj_set_style_radius(tile, 14, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, &lw_font_44, 0);
        lv_obj_center(label);
        s_2048.tiles[i] = tile;
        s_2048.labels[i] = label;
    }
    game_2048_refresh();
}

/* hub -------------------------------------------------------------------- */

typedef struct {
    const char *title;
    const char *subtitle;
    const char *status;
    uint32_t accent;
    void (*open)(lv_event_t *event);
} hub_card_t;

static void play_2048(lv_event_t *event)
{
    (void)event;
    open_game_2048();
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();

    static const hub_card_t cards[3] = {
        {"2048", "滑动合并数字", "已接入", 0xF6BE6B, play_2048},
        {"贪吃蛇", "越吃越快的小蛇", "移植中", 0x7FD3B4, NULL},
        {"打砖块", "接住小球打光砖块", "移植中", 0xF58FAE, NULL},
    };

    lw_text(content, "小游戏", &lw_font_28, palette->text, 36, 18);
    lw_text(content, "独立 Arcade 小游戏会集中在这里；点开游戏会全屏运行，随时可以回到游戏中心。", &lw_font_18,
            palette->dim, 36, 56);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = lw_card(content, 36 + i * 324, 96, 300, 300);
        lv_obj_set_style_bg_color(card, lv_color_hex(palette->card), 0);
        if (cards[i].open != NULL) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, cards[i].open, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *badge = lw_icon_badge(card, 24, 24, 64, cards[i].accent, 20);
        lv_obj_t *dot = lv_obj_create(badge);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 22, 22);
        lv_obj_set_pos(dot, 21, 21);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_80, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

        lw_text(card, cards[i].title, &lw_font_28, palette->text, 24, 108);
        lw_text(card, cards[i].subtitle, &lw_font_18, palette->dim, 24, 150);
        lw_text(card, cards[i].status, &lw_font_18,
                cards[i].open != NULL ? palette->accent_deep : palette->dim, 24, 184);
        lw_text(card, cards[i].open != NULL ? "点一下开始" : "下一阶段接入", &lw_font_18, palette->dim, 24, 246);
    }

    lv_obj_t *best = lw_text(content, "", &lw_font_18, palette->dim, 36, 410);
    lv_label_set_text_fmt(best, "最高分：2048 %u   贪吃蛇 %u   打砖块 %u", (unsigned)state->arcade_best[0],
                          (unsigned)state->arcade_best[1], (unsigned)state->arcade_best[2]);
    (void)state;
}

static void destroy(void)
{
    close_overlay();
}

const lw_app_t lw_app_arcade_def = {
    .id = "arcade",
    .name = "游戏",
    .create = create,
    .destroy = destroy,
};
