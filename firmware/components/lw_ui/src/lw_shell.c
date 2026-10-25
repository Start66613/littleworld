/* SPDX-License-Identifier: MIT */
#include "lw_shell.h"

#include <stdio.h>
#include <string.h>

#include "lw_app_state.h"
#include "lw_apps.h"
#include "lw_icons.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw/lw_pet.h"

#define DOCK_ITEM_W 150
#define DOCK_ITEM_H 72

static lv_obj_t *s_screen;
static lv_obj_t *s_content;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_demo_pill;
static lv_obj_t *s_dock_badges[8];
static lv_obj_t *s_dock_labels[8];
static int s_dock_count;
static const lw_app_t *s_current;
static lv_timer_t *s_clock_timer;
static lv_timer_t *s_state_timer;
static uint32_t s_last_save;
static const lw_shell_host_t *s_host;

static const char *const s_weekdays[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* --------------------------------------------------------------- top bar */

static void update_clock(void)
{
    if (s_clock_label == NULL) {
        return;
    }
    uint32_t now = lw_now();
    int seconds_of_day = (int)(now % 86400u);
    int hour = seconds_of_day / 3600;
    int minute = (seconds_of_day % 3600) / 60;

    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
    lv_label_set_text(s_clock_label, clock);

    lw_date_t today = lw_today();
    int weekday = (int)((lw_days_from_civil(today.year, today.month, today.day) % 7 + 11) % 7);
    char date[48];
    snprintf(date, sizeof(date), "%d月%d日 %s", today.month, today.day, s_weekdays[weekday]);
    lv_label_set_text(s_date_label, date);

    if (s_demo_pill != NULL) {
        if (lw_time_is_demo()) {
            lv_obj_remove_flag(s_demo_pill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_demo_pill, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ------------------------------------------------------------------ dock */

static void dock_clicked(lv_event_t *event)
{
    const char *id = (const char *)lv_event_get_user_data(event);
    if (id != NULL) {
        lw_shell_open(id);
    }
}

static void update_dock_styles(void)
{
    const lw_palette_t *palette = lw_palette();
    for (int i = 0; i < s_dock_count; i++) {
        const lw_app_t *app = lw_dock_at(i);
        int active = (s_current != NULL) && (strcmp(app->id, s_current->id) == 0);
        if (s_dock_badges[i] != NULL) {
            lv_obj_set_style_bg_color(s_dock_badges[i], lv_color_hex(active ? palette->accent : palette->card_soft), 0);
            lv_obj_t *icon = lv_obj_get_child(s_dock_badges[i], 0);
            (void)icon;
        }
        if (s_dock_labels[i] != NULL) {
            lv_obj_set_style_text_color(s_dock_labels[i], lv_color_hex(active ? palette->accent_deep : palette->dim), 0);
        }
    }
}

static void build_dock(lv_obj_t *parent)
{
    const lw_palette_t *palette = lw_palette();
    lv_obj_t *dock = lw_card(parent, 32, LW_SCREEN_H - LW_DOCK_H + 4, 960, LW_DOCK_H - 12);
    lv_obj_set_style_radius(dock, 30, 0);
    lv_obj_set_style_shadow_opa(dock, LV_OPA_20, 0);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);

    s_dock_count = lw_dock_count();
    int total_w = s_dock_count * DOCK_ITEM_W;
    int start_x = (960 - total_w) / 2;

    for (int i = 0; i < s_dock_count; i++) {
        const lw_app_t *app = lw_dock_at(i);
        lv_obj_t *item = lv_obj_create(dock);
        lv_obj_remove_style_all(item);
        lv_obj_set_pos(item, start_x + i * DOCK_ITEM_W, 6);
        lv_obj_set_size(item, DOCK_ITEM_W, DOCK_ITEM_H);
        lv_obj_set_style_radius(item, 22, 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(palette->accent), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item, dock_clicked, LV_EVENT_CLICKED, (void *)app->id);

        lv_obj_t *badge = lw_icon_badge(item, (DOCK_ITEM_W - 44) / 2, 2, 44, palette->card_soft, 15);
        lw_icon_draw(app->id, badge, palette->dim);

        lv_obj_t *label = lw_text(item, app->name, &lw_font_18, palette->dim, 0, 50);
        lv_obj_set_width(label, DOCK_ITEM_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        s_dock_badges[i] = badge;
        s_dock_labels[i] = label;
    }
}

/* -------------------------------------------------------------- lifecycle */

static void stop_current_app(void)
{
    if (s_current != NULL && s_current->destroy != NULL) {
        s_current->destroy();
    }
}

void lw_shell_open(const char *id)
{
    if (s_host) { s_host->open(id); return; }
    const lw_app_t *app = lw_app_find(id);
    if (app == NULL || s_content == NULL) {
        return;
    }
    if (s_current == app) {
        return;
    }

    stop_current_app();
    lv_obj_clean(s_content);
    s_current = app;
    app->create(s_content, app);
    lw_theme_enter_anim(s_content);
    update_dock_styles();
}

void lw_shell_home(void)
{
    lw_shell_open("home");
}

lv_obj_t *lw_shell_content(void)
{
    if (s_host) return s_host->content();
    return s_content;
}

const lw_app_t *lw_shell_current(void)
{
    if (s_host) return s_host->current();
    return s_current;
}

void lw_shell_refresh_theme(void)
{
    if (s_host) { s_host->refresh(); return; }
    if (s_screen == NULL) {
        return;
    }
    lw_theme_paint_background(s_screen);
    const lw_app_t *current = s_current;
    s_current = NULL;
    if (current != NULL) {
        stop_current_app();
        lv_obj_clean(s_content);
        lw_shell_open(current->id);
    }
    update_dock_styles();
}

void lw_shell_bind_host(const lw_shell_host_t *host)
{
    if (!s_host && host) {
        stop_current_app();
        s_current=NULL;
        if (s_content) lv_obj_clean(s_content);
    }
    s_host=host;
    if (!host && s_content) lw_shell_home();
}

/* ------------------------------------------------------------------ timers */

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_clock();
}

static void state_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    lw_pet_tick(&state->pet, now);

    if (s_last_save == 0 || now > s_last_save + 60) {
        s_last_save = now;
        lw_app_state_save();
    }
}

/* ------------------------------------------------------------------- start */

void lw_shell_start(void)
{
    lw_theme_init();

    s_screen = lv_obj_create(NULL);
    lw_theme_paint_background(s_screen);

    /* Top bar */
    lv_obj_t *title = lw_text(s_screen, "Little World", &lw_font_28, lw_palette()->text, 36, 12);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    s_demo_pill = lw_pill(s_screen, "DEMO 时间", lw_palette()->card_soft, lw_palette()->lav_deep);
    lv_obj_set_pos(s_demo_pill, 232, 20);

    s_clock_label = lw_text(s_screen, "--:--", &lw_font_28, lw_palette()->text, 0, 8);
    lv_obj_align(s_clock_label, LV_ALIGN_TOP_RIGHT, -92, 6);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_date_label = lw_text(s_screen, "", &lw_font_18, lw_palette()->dim, 0, 38);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_RIGHT, -92, 36);
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *settings_btn = lw_icon_button(s_screen, LV_SYMBOL_SETTINGS, 44, lw_palette()->card,
                                            lw_palette()->lav_deep);
    lv_obj_set_pos(settings_btn, 956, 10);
    lv_obj_add_event_cb(settings_btn, dock_clicked, LV_EVENT_CLICKED, (void *)"settings");

    /* Content area for pages */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, LW_TOPBAR_H);
    lv_obj_set_size(s_content, LW_SCREEN_W, LW_CONTENT_H);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_CLICKABLE);

    build_dock(s_screen);

    lv_screen_load(s_screen);

    update_clock();
    lw_shell_open("home");

    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    (void)s_clock_timer;
    s_state_timer = lv_timer_create(state_timer_cb, 10000, NULL);
    (void)s_state_timer;
}
