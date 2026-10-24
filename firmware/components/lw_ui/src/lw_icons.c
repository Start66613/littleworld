/* SPDX-License-Identifier: MIT */
#include "lw_icons.h"

#include <string.h>

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void icon_home(lv_obj_t *p, uint32_t fg)
{
    shape(p, 9, 9, 11, 11, 3, fg);
    shape(p, 24, 9, 11, 11, 3, fg);
    shape(p, 9, 24, 11, 11, 3, fg);
    shape(p, 24, 24, 11, 11, 3, fg);
}

static void icon_album(lv_obj_t *p, uint32_t fg)
{
    shape(p, 8, 11, 28, 22, 6, fg);
    shape(p, 14, 17, 7, 7, 4, 0xFFFFFF);
    shape(p, 23, 23, 9, 6, 2, 0xFFFFFF);
}

static void icon_music(lv_obj_t *p, uint32_t fg)
{
    shape(p, 11, 25, 10, 10, 5, fg);
    shape(p, 19, 8, 3, 19, 1, fg);
    shape(p, 19, 8, 9, 3, 1, fg);
}

static void icon_memories(lv_obj_t *p, uint32_t fg)
{
    shape(p, 9, 11, 26, 22, 6, fg);
    shape(p, 21, 13, 2, 18, 1, 0xFFFFFF);
    shape(p, 12, 15, 6, 2, 1, 0xFFFFFF);
    shape(p, 12, 20, 6, 2, 1, 0xFFFFFF);
}

static void icon_arcade(lv_obj_t *p, uint32_t fg)
{
    shape(p, 7, 14, 30, 17, 8, fg);
    shape(p, 13, 20, 4, 4, 2, 0xFFFFFF);
    shape(p, 19, 20, 4, 4, 2, 0xFFFFFF);
    shape(p, 27, 20, 4, 4, 2, 0xFFFFFF);
}

static void icon_settings(lv_obj_t *p, uint32_t fg)
{
    shape(p, 15, 15, 14, 14, 7, fg);
    shape(p, 18, 18, 8, 8, 4, 0xFFFFFF);
    shape(p, 19, 7, 6, 5, 2, fg);
    shape(p, 19, 32, 6, 5, 2, fg);
    shape(p, 7, 19, 5, 6, 2, fg);
    shape(p, 32, 19, 5, 6, 2, fg);
}

void lw_icon_draw(const char *app_id, lv_obj_t *badge, uint32_t fg)
{
    if (app_id == NULL || badge == NULL) {
        return;
    }
    if (strcmp(app_id, "home") == 0) {
        icon_home(badge, fg);
    } else if (strcmp(app_id, "album") == 0) {
        icon_album(badge, fg);
    } else if (strcmp(app_id, "music") == 0) {
        icon_music(badge, fg);
    } else if (strcmp(app_id, "memories") == 0) {
        icon_memories(badge, fg);
    } else if (strcmp(app_id, "arcade") == 0) {
        icon_arcade(badge, fg);
    } else if (strcmp(app_id, "settings") == 0) {
        icon_settings(badge, fg);
    } else if (strcmp(app_id, "pet") == 0) {
        lw_icon_pet(badge, fg, 0xFFFFFF);
    }
}

void lw_icon_pet(lv_obj_t *badge, uint32_t fg, uint32_t bg)
{
    /* Simple round face: head, two ears, two eyes. */
    shape(badge, 11, 12, 22, 20, 10, fg);
    shape(badge, 11, 8, 8, 8, 4, fg);
    shape(badge, 25, 8, 8, 8, 4, fg);
    shape(badge, 17, 19, 3, 3, 2, bg);
    shape(badge, 24, 19, 3, 3, 2, bg);
}
