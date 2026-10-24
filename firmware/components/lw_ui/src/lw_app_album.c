/* Little World - album (DEMO placeholders until real photos can be imported).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "lw_app_state.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"

#define ALBUM_COUNT 6

static const uint32_t s_tile_colors[ALBUM_COUNT] = {0xFFD9E4, 0xFFE7CB, 0xE4F2E6, 0xE7E0FB, 0xFFF0C9, 0xDDEFF7};
static const char *const s_tile_titles[ALBUM_COUNT] = {"海边", "生日", "散步", "旅行", "厨房", "夜晚"};
static int s_index;

static void show_viewer(int index)
{
    const lw_palette_t *palette = lw_palette();
    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x2A1E26), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(overlay);

    lv_obj_t *card = lw_card(overlay, 0, 0, 720, 460);
    lv_obj_center(card);
    lv_obj_t *photo = lw_soft_card(card, 24, 24, 672, 300);
    lv_obj_set_style_radius(photo, 18, 0);
    lv_obj_set_style_bg_color(photo, lv_color_hex(s_tile_colors[index % ALBUM_COUNT]), 0);

    lv_obj_t *tag = lw_text(photo, "DEMO 占位图 · 真实照片后续从 SD 卡导入", &lw_font_18, palette->dim, 20, 16);
    (void)tag;

    lv_obj_t *title = lw_text(card, "", &lw_font_28, palette->text, 24, 340);
    lv_label_set_text_fmt(title, "%s（%d/%d）", s_tile_titles[index % ALBUM_COUNT], index + 1, ALBUM_COUNT);

    lv_obj_t *prev = lw_button(card, "上一张", 130, 46, palette->card_soft, palette->accent_deep, &lw_font_18);
    lv_obj_set_pos(prev, 24, 396);
    lv_obj_t *next = lw_button(card, "下一张", 130, 46, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(next, 170, 396);
    lv_obj_t *close = lw_button(card, "关闭", 120, 46, palette->lav, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(close, 570, 396);
    (void)prev;
    (void)next;
    (void)close;
}

static void tile_clicked(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    show_viewer(index);
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    const lw_palette_t *palette = lw_palette();

    for (int i = 0; i < ALBUM_COUNT; i++) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *tile = lw_card(content, 32 + col * 324, 16 + row * 200, 300, 184);
        lv_obj_set_style_bg_color(tile, lv_color_hex(s_tile_colors[i]), 0);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, tile_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lw_text(tile, s_tile_titles[i], &lw_font_22, palette->text, 22, 22);
        lw_text(tile, "DEMO", &lw_font_18, palette->dim, 22, 142);
        lw_text(tile, "点开看大图", &lw_font_18, palette->dim, 150, 142);
    }

    lw_text(content, "相册目前使用 DEMO 占位图；接入 SD 卡后会显示真实照片。", &lw_font_18, palette->dim, 32, 404);
    (void)s_index;
}

static void destroy(void)
{
}

const lw_app_t lw_app_album_def = {
    .id = "album",
    .name = "相册",
    .create = create,
    .destroy = destroy,
};
