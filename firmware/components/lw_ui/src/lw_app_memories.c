/* Little World - memories / anniversary world.
 *
 * Four tabs: anniversary overview, timeline, surprise box and the little map.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_app_state.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw_ui_content.h"
#include "lw_os_design.h"
#include "lw_polish.h"
LV_FONT_DECLARE(lw_font_88);
#include "lw/lw_memories.h"
#include "lw/lw_time.h"

static lv_obj_t *s_tab_bar;
static lv_obj_t *s_page;
static lv_obj_t *s_tabs[4];
static lv_obj_t *s_detail_overlay;
static int s_active_tab;
static int s_pending_tab = -1;
static lv_timer_t *s_timer;
static lv_obj_t *s_days_label;
static lv_obj_t *s_days_unit;
static lv_obj_t *s_next_label;
static lv_obj_t *s_content;
static const lw_memory_t *stories; static unsigned story_count;
bool lw_memories_set_source(const lw_memory_t *items,unsigned count)
{
    if (s_page || count>32 || (count && !items)) return false;
    for (unsigned i=0;i<count;i++) if (!items[i].title || !items[i].body || strlen(items[i].title)>128 || strlen(items[i].body)>4096 || !lw_date_valid(items[i].date)) return false;
    stories=items; story_count=count; return true;
}

static void build_tabs(lv_obj_t *content);
static void build_page(void);

static void close_overlay_cb(lv_event_t *event)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(event);
    if (overlay != NULL) {
        lv_obj_delete(overlay);
        if (s_detail_overlay==overlay) s_detail_overlay=NULL;
    }
}

void lw_memories_open_tab(int tab)
{
    s_pending_tab = tab;
}

/* ------------------------------------------------------------------ common */

static void switch_tab(int index)
{
    if (index < 0 || index > 3) {
        return;
    }
    s_active_tab = index;
    if (s_content != NULL) {
        lv_obj_clean(s_content);
        build_tabs(s_content);
        s_page = lv_obj_create(s_content);
        lv_obj_remove_style_all(s_page);
        lv_obj_set_pos(s_page, 0, 54);
        lv_obj_set_size(s_page, LW_SCREEN_W, LW_CONTENT_H - 54);
        lv_obj_remove_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_page, LV_OBJ_FLAG_CLICKABLE);
        build_page();
    }
}

static void tab_clicked(lv_event_t *event)
{
    switch_tab((int)(intptr_t)lv_event_get_user_data(event));
}

static void build_tabs(lv_obj_t *content)
{
    const lw_palette_t *palette = lw_palette();
    static const char *const names[4] = {"纪念日", "时间线", "惊喜盒子", "小地图"};

    s_tab_bar = lv_obj_create(content);
    lv_obj_remove_style_all(s_tab_bar);
    lv_obj_set_pos(s_tab_bar, 32, 6);
    lv_obj_set_size(s_tab_bar, 960, 48);
    lv_obj_remove_flag(s_tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_tab_bar, 10, 0);
    lv_obj_set_style_pad_all(s_tab_bar, 0, 0);
    lv_obj_remove_flag(s_tab_bar, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *tab = lv_obj_create(s_tab_bar);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, 150, 44);
        lv_obj_set_style_radius(tab, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(tab, lv_color_hex(i == s_active_tab ? palette->accent : palette->card), 0);
        lv_obj_set_style_bg_opa(tab, i == s_active_tab ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tab, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *label = lv_label_create(tab);
        lw_style_label(label, &lw_font_18, i == s_active_tab ? 0xFFFFFF : palette->dim);
        lv_label_set_text(label, names[i]);
        lv_obj_center(label);
        s_tabs[i] = tab;
    }
}

/* ------------------------------------------------------------- tab: today */

static void adjust_date(int field, int delta)
{
    lw_state_t *state = lw_app_state();
    lw_date_t date = state->anniversary;
    if (field == 0) {
        date.year += delta;
    } else if (field == 1) {
        date.month += delta;
    } else {
        date.day += delta;
    }
    while (date.month < 1) {
        date.month += 12;
        date.year--;
    }
    while (date.month > 12) {
        date.month -= 12;
        date.year++;
    }
    if (date.day < 1) {
        date.day = 1;
    }
    int max_day = lw_days_in_month(date.year, date.month);
    if (date.day > max_day) {
        date.day = max_day;
    }
    if (lw_date_valid(date)) {
        state->anniversary = date;
        lw_app_state_save();
        /* Rebuild in place: lw_shell_open() would early-out because this app is
         * already the current one, leaving the old date on screen. */
        switch_tab(0);
    }
}

static void date_adjust_clicked(lv_event_t *event)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(event);
    adjust_date((int)(packed / 10), (int)(packed % 10) - 5);
}

static lv_obj_t *date_adjust_button(lv_obj_t *parent, int x, int y, int field, int delta)
{
    const lw_palette_t *palette = lw_palette();
    lv_obj_t *btn = lw_button(parent, delta > 0 ? "+" : "-", 44, 44, palette->card, palette->dim, &lw_font_18);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, date_adjust_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)(field * 10 + delta + 5));
    return btn;
}

static void build_tab_today(lv_obj_t *page)
{
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();

    lv_obj_t *hero = lw_card(page, 32, 16, 470, 340);
    if (lw_shell_host_active()) lw_polish_paper(hero,true);
    lw_os_icon(hero,OS_I_HEART,372,26,58,palette->accent);
    lw_text(hero, "我们在一起", &lw_font_18, palette->dim, 32, 30);
    s_days_label = lw_text(hero, "--", lw_shell_host_active() ? &lw_font_88 : &lw_font_44, palette->accent_deep, 32, 62);
    s_days_unit = lw_text(hero, "天", &lw_font_28, palette->accent_deep, 0, 0);
    lv_obj_align_to(s_days_unit, s_days_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
    s_next_label = lw_text(hero, "", &lw_font_18, palette->dim, 32, lw_shell_host_active() ? 166 : 136);
    lw_text_wrap(hero,"这张卡片记录一起走过的日子。\n真实故事可以慢慢补进来。",&lw_font_22,palette->text,32,200,400);

    lv_obj_t *edit_card = lw_card(page, 522, 16, 470, 208);
    lw_text(edit_card, "纪念日设置（DEMO 可改）", &lw_font_22, palette->text, 28, 18);
    lw_text(edit_card, "起始日期", &lw_font_18, palette->dim, 28, 58);

    lv_obj_t *date_label = lw_text(edit_card, "", &lw_font_28, palette->text, 28, 84);
    lv_label_set_text_fmt(date_label, "%04d-%02d-%02d", state->anniversary.year, state->anniversary.month,
                          state->anniversary.day);

    /* one compact row: 年 - +   月 - +   日 - + */
    lw_text(edit_card,"年",&lw_font_18,palette->dim,28,152);
    date_adjust_button(edit_card,56,140,0,-1); date_adjust_button(edit_card,108,140,0,1);
    lw_text(edit_card,"月",&lw_font_18,palette->dim,176,152);
    date_adjust_button(edit_card,200,140,1,-1); date_adjust_button(edit_card,252,140,1,1);
    lw_text(edit_card,"日",&lw_font_18,palette->dim,320,152);
    date_adjust_button(edit_card,344,140,2,-1); date_adjust_button(edit_card,396,140,2,1);

    lv_obj_t *story = lw_card(page, 522, 238, 470, 120);
    lv_obj_set_style_bg_color(story, lv_color_hex(palette->card_soft), 0);
    lw_text(story, "回忆时间线", &lw_font_22, palette->text, 28, 20);
    lw_text(story, "把一起走过的日子收好，慢慢翻。", &lw_font_18, palette->dim, 28, 54);
    lv_obj_t *open_btn = lw_button(story, "去看看", 130, 44, palette->lav, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(open_btn, 300, 58);
    /* clicking the button switches to the timeline tab */
    lv_obj_add_event_cb(open_btn, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)1);
}

static void refresh_today(void)
{
    lw_state_t *state = lw_app_state();
    lw_date_t today = lw_today();
    if (s_days_label != NULL) {
        int days = lw_days_between(state->anniversary, today) + 1;
        if (days < 1) {
            days = 1;
        }
        lv_label_set_text_fmt(s_days_label, "%d", days);
        if (s_days_unit != NULL) {
            lv_obj_align_to(s_days_unit, s_days_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
        }
    }
    if (s_next_label != NULL) {
        int days = lw_days_until_anniversary(state->anniversary, today);
        if (days == 0) {
            lv_label_set_text(s_next_label, "今天就是纪念日");
        } else {
            lv_label_set_text_fmt(s_next_label, "下一个纪念日还有 %d 天", days);
        }
    }
}

/* ---------------------------------------------------------- tab: timeline */

static void memory_clicked(lv_event_t *event)
{
    const lw_memory_t *memory = (const lw_memory_t *)lv_event_get_user_data(event);
    if (memory == NULL) {
        return;
    }
    const lw_palette_t *palette = lw_palette();
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x2A1E26), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(overlay);
    s_detail_overlay=overlay;

    lv_obj_t *card = lw_card(overlay, 0, 0, 560, 380);
    if (lw_shell_host_active()) lw_polish_paper(card,true);
    lv_obj_center(card);
    lw_os_motion(card,LW_MOVE_CARD);

    lv_obj_t *date = lw_text(card, "", &lw_font_18, palette->dim, 32, 28);
    lv_label_set_text_fmt(date, "%04d-%02d-%02d", memory->date.year, memory->date.month, memory->date.day);
    lv_obj_t *title=lw_text(card,memory->title,&lw_font_28,palette->text,32,58); lv_obj_set_size(title,496,44); lv_label_set_long_mode(title,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *body=lw_os_box(card,32,110,496,180,0,0,0); lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(body,LV_DIR_VER); lv_obj_set_scrollbar_mode(body,LV_SCROLLBAR_MODE_AUTO);
    lw_text_wrap(body,memory->body,&lw_font_22,palette->text,0,0,476);
    lw_text(card, "照片位：以后放真实照片", &lw_font_18, palette->dim, 32, 300);

    lv_obj_t *close = lw_button(card, "收起", 130, 46, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -28, -24);
    lv_obj_add_event_cb(close, close_overlay_cb, LV_EVENT_CLICKED, overlay);
}

static void build_tab_timeline(lv_obj_t *page)
{
    const lw_palette_t *palette = lw_palette();

    lv_obj_t *list = lv_obj_create(page);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 32, 52);
    lv_obj_set_size(list, 960, 322);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lw_os_label(page,stories ? "我们的故事手账" : "示例时间线 · 等待导入真实故事",&lw_font_18,40,16,900,palette->dim);
    int count = stories ? (int)story_count : lw_memories_count();
    for (int i = 0; i < count; i++) {
        const lw_memory_t *memory = stories ? &stories[i] : lw_memories_get(i);
        lv_obj_t *row = lw_card(list, 0, 0, 940, 86);
        if (lw_shell_host_active()) lw_polish_paper(row,false);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, memory_clicked, LV_EVENT_CLICKED, (void *)memory);
        lw_os_motion_touch(row); lw_os_box(row,84,13,3,60,palette->line,2,255); lw_os_box(row,79,26,13,13,palette->accent,7,255);

        lv_obj_t *date = lw_text(row, "", &lw_font_18, palette->dim, 22, 26);
        lv_label_set_text_fmt(date, "%02d-%02d", memory->date.month, memory->date.day);
        lv_obj_set_width(date, 64);

        lw_text(row, memory->title, &lw_font_22, palette->text, 100, 14);
        lw_text(row, "点开看这一段", &lw_font_18, palette->dim, 100, 44);

        lv_obj_t *chev = lw_text(row, ">", &lw_font_22, palette->dim, 900, 26);
        (void)chev;
    }
}

/* ---------------------------------------------------------- tab: surprise */

static void gift_opened(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    state->surprise_unlocked = 1;
    lw_app_state_save();
    switch_tab(2);
}

static void build_tab_surprise(lv_obj_t *page)
{
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();

    lv_obj_t *card = lw_card(page, 32, 18, 470, 340);
    lv_obj_set_style_bg_color(card, lv_color_hex(palette->card_soft), 0);

    lv_obj_t *box = lw_icon_badge(card, 165, 40, 140, palette->accent, 30);
    lv_obj_t *lid = lv_obj_create(box);
    lv_obj_remove_style_all(lid);
    lv_obj_set_pos(lid, 14, 34);
    lv_obj_set_size(lid, 112, 26);
    lv_obj_set_style_radius(lid, 10, 0);
    lv_obj_set_style_bg_color(lid, lv_color_hex(palette->accent_deep), 0);
    lv_obj_set_style_bg_opa(lid, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ribbon = lv_obj_create(box);
    lv_obj_remove_style_all(ribbon);
    lv_obj_set_pos(ribbon, 63, 12);
    lv_obj_set_size(ribbon, 14, 30);
    lv_obj_set_style_radius(ribbon, 6, 0);
    lv_obj_set_style_bg_color(ribbon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ribbon, LV_OPA_80, 0);
    lv_obj_remove_flag(ribbon, LV_OBJ_FLAG_CLICKABLE);

    lw_text_wrap(card, lw_surprise_hint(), &lw_font_18, palette->dim, 40, 196, 390);

    lv_obj_t *open = lw_button(card, state->surprise_unlocked ? "再打开一次" : "打开盒子", 190, 52, palette->accent_deep,
                               0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(open, 140, 262);
    lv_obj_add_event_cb(open, gift_opened, LV_EVENT_CLICKED, NULL);

    lv_obj_t *message = lw_card(page, 522, 18, 470, 340);
    lw_text(message, state->surprise_unlocked ? "里面的留言" : "还没打开", &lw_font_22, palette->text, 30, 24);
    if (state->surprise_unlocked) {
        lw_text_wrap(message, lw_surprise_message(), &lw_font_28, palette->accent_deep, 30, 80, 400);
        lw_text(message, "（DEMO 内容，之后可以换成你自己写的话）", &lw_font_18, palette->dim, 30, 280);
    } else {
        lw_text_wrap(message, "打开左边的盒子，会有一条只写给你的话。", &lw_font_22, palette->dim, 30, 80, 400);
    }
}

/* --------------------------------------------------------------- tab: map */

static void place_clicked(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    const lw_place_t *place = lw_places_get(index);
    if (place == NULL) {
        return;
    }
    lw_state_t *state = lw_app_state();
    state->map_visited |= (uint8_t)(1u << index);
    lw_app_state_save();

    const lw_palette_t *palette = lw_palette();
    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x2A1E26), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(overlay);

    lv_obj_t *card = lw_card(overlay, 0, 0, 520, 300);
    s_detail_overlay=overlay;
    lv_obj_center(card);
    lw_text(card, place->name, &lw_font_28, palette->text, 32, 30);
    lw_text(card, place->subtitle, &lw_font_18, palette->dim, 32, 74);
    lw_text_wrap(card, place->description, &lw_font_22, palette->text, 32, 120, 456);
    lv_obj_t *close = lw_button(card, "返回", 120, 44, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -28, -22);
    lv_obj_add_event_cb(close, close_overlay_cb, LV_EVENT_CLICKED, overlay);
}

static void build_tab_map(lv_obj_t *page)
{
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();
    int count = lw_places_count();
    for (int i = 0; i < count; i++) {
        const lw_place_t *place = lw_places_get(i);
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *card = lw_card(page, 32 + col * 330, 18 + row * 166, 310, 150);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, place_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        uint32_t dot = (i % 2 == 0) ? palette->accent : palette->lav;
        lv_obj_t *badge = lw_icon_badge(card, 22, 22, 44, dot, 14);
        lv_obj_t *inner = lv_obj_create(badge);
        lv_obj_remove_style_all(inner);
        lv_obj_set_size(inner, 16, 16);
        lv_obj_set_pos(inner, 14, 14);
        lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(inner, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(inner, LV_OPA_80, 0);
        lv_obj_remove_flag(inner, LV_OBJ_FLAG_CLICKABLE);

        lw_text(card, place->name, &lw_font_22, palette->text, 80, 24);
        lw_text(card, place->subtitle, &lw_font_18, palette->dim, 80, 58);
        lv_obj_t *visited = lw_text(card, (state->map_visited & (1u << i)) ? "已经去过" : "还没有去过", &lw_font_18,
                                    palette->accent_deep, 22, 110);
        (void)visited;
    }
}

/* ------------------------------------------------------------------ build */

static void build_page(void)
{
    lv_obj_clean(s_page);
    switch (s_active_tab) {
    case 0:
        build_tab_today(s_page);
        refresh_today();
        break;
    case 1:
        build_tab_timeline(s_page);
        break;
    case 2:
        build_tab_surprise(s_page);
        break;
    case 3:
        build_tab_map(s_page);
        break;
    default:
        break;
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_active_tab == 0) {
        refresh_today();
    }
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    s_content = content;
    if (s_pending_tab >= 0 && s_pending_tab < 4) {
        s_active_tab = s_pending_tab;
    }
    s_pending_tab = -1;

    build_tabs(content);

    s_page = lv_obj_create(content);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_pos(s_page, 0, 54);
    lv_obj_set_size(s_page, LW_SCREEN_W, LW_CONTENT_H - 54);
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_CLICKABLE);

    build_page();
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void destroy(void)
{
    if (s_detail_overlay) { lv_obj_delete(s_detail_overlay); s_detail_overlay=NULL; }
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_tab_bar = NULL;
    s_page = NULL;
    s_days_label = NULL;
    s_days_unit = NULL;
    s_next_label = NULL;
    s_content = NULL;
}

const lw_app_t lw_app_memories_def = {
    .id = "memories",
    .name = "回忆",
    .create = create,
    .destroy = destroy,
};
