/* Little World - music player (UI + simulated playback, no audio output yet).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_app_state.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"

#define TRACK_COUNT 3

static const char *const s_track_titles[TRACK_COUNT] = {"小世界 · 主题（DEMO）", "傍晚的风（DEMO）", "回家的路（DEMO）"};
static const char *const s_track_artist[TRACK_COUNT] = {"占位曲目 1", "占位曲目 2", "占位曲目 3"};
static const int s_track_seconds[TRACK_COUNT] = {168, 205, 192};

static int s_track;
static int s_position;
static int s_playing;

static lv_obj_t *s_title;
static lv_obj_t *s_artist;
static lv_obj_t *s_progress;
static lv_obj_t *s_time_label;
static lv_obj_t *s_play_label;
static lv_obj_t *s_volume_label;
static lv_obj_t *s_rows[TRACK_COUNT];
static lv_obj_t *s_row_titles[TRACK_COUNT];
static lv_obj_t *s_row_artists[TRACK_COUNT];
static lv_timer_t *s_timer;

static void refresh(void)
{
    lw_state_t *state = lw_app_state();
    const lw_palette_t *palette = lw_palette();

    if (s_title != NULL) {
        lv_label_set_text(s_title, s_track_titles[s_track]);
    }
    if (s_artist != NULL) {
        lv_label_set_text(s_artist, s_track_artist[s_track]);
    }
    if (s_progress != NULL) {
        lw_bar_set(s_progress, (s_position * 100) / s_track_seconds[s_track]);
    }
    if (s_time_label != NULL) {
        lv_label_set_text_fmt(s_time_label, "%d:%02d / %d:%02d", s_position / 60, s_position % 60,
                              s_track_seconds[s_track] / 60, s_track_seconds[s_track] % 60);
    }
    if (s_play_label != NULL) {
        lv_label_set_text(s_play_label, s_playing ? "暂停" : "播放");
    }
    if (s_volume_label != NULL) {
        lv_label_set_text_fmt(s_volume_label, "%d%%", state->music_volume);
    }

    for (int i = 0; i < TRACK_COUNT; i++) {
        int active = (i == s_track);
        if (s_rows[i] == NULL) {
            continue;
        }
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(active ? palette->accent : palette->card_soft), 0);
        lv_obj_set_style_bg_opa(s_rows[i], active ? LV_OPA_20 : LV_OPA_COVER, 0);
        if (s_row_titles[i] != NULL) {
            lv_obj_set_style_text_color(s_row_titles[i], lv_color_hex(active ? palette->accent_deep : palette->text), 0);
        }
        if (s_row_artists[i] != NULL) {
            lv_obj_set_style_text_color(s_row_artists[i], lv_color_hex(active ? palette->accent_deep : palette->dim), 0);
        }
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_playing) {
        return;
    }
    s_position++;
    if (s_position >= s_track_seconds[s_track]) {
        s_position = 0;
        s_track = (s_track + 1) % TRACK_COUNT;
    }
    refresh();
}

static void play_toggle(lv_event_t *event)
{
    (void)event;
    s_playing = !s_playing;
    refresh();
}

static void next_track(lv_event_t *event)
{
    (void)event;
    s_track = (s_track + 1) % TRACK_COUNT;
    s_position = 0;
    refresh();
}

static void prev_track(lv_event_t *event)
{
    (void)event;
    s_track = (s_track + TRACK_COUNT - 1) % TRACK_COUNT;
    s_position = 0;
    refresh();
}

static void track_clicked(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= TRACK_COUNT) {
        return;
    }
    s_track = index;
    s_position = 0;
    refresh();
}

static void volume_up(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    if (state->music_volume <= 90) {
        state->music_volume += 10;
    } else {
        state->music_volume = 100;
    }
    lw_app_state_save();
    refresh();
}

static void volume_down(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    if (state->music_volume >= 10) {
        state->music_volume -= 10;
    } else {
        state->music_volume = 0;
    }
    lw_app_state_save();
    refresh();
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    const lw_palette_t *palette = lw_palette();
    s_playing = 0;
    s_position = 0;

    lv_obj_t *player = lw_card(content, 32, 14, 470, 412);
    lw_text(player, "正在播放", &lw_font_18, palette->dim, 32, 26);
    s_title = lw_text(player, "", &lw_font_28, palette->text, 32, 54);
    s_artist = lw_text(player, "", &lw_font_18, palette->dim, 32, 96);

    lv_obj_t *art = lw_soft_card(player, 32, 128, 180, 180);
    lv_obj_set_style_radius(art, 26, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(palette->lav), 0);
    /* music note drawn from primitives (no glyph dependency) */
    lv_obj_t *note_head = lv_obj_create(art);
    lv_obj_remove_style_all(note_head);
    lv_obj_set_size(note_head, 46, 40);
    lv_obj_set_pos(note_head, 56, 104);
    lv_obj_set_style_radius(note_head, 20, 0);
    lv_obj_set_style_bg_color(note_head, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(note_head, LV_OPA_90, 0);
    lv_obj_remove_flag(note_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *note_stem = lv_obj_create(art);
    lv_obj_remove_style_all(note_stem);
    lv_obj_set_size(note_stem, 8, 62);
    lv_obj_set_pos(note_stem, 94, 46);
    lv_obj_set_style_radius(note_stem, 4, 0);
    lv_obj_set_style_bg_color(note_stem, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(note_stem, LV_OPA_90, 0);
    lv_obj_remove_flag(note_stem, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *note_flag = lv_obj_create(art);
    lv_obj_remove_style_all(note_flag);
    lv_obj_set_size(note_flag, 26, 10);
    lv_obj_set_pos(note_flag, 94, 46);
    lv_obj_set_style_radius(note_flag, 5, 0);
    lv_obj_set_style_bg_color(note_flag, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(note_flag, LV_OPA_90, 0);
    lv_obj_remove_flag(note_flag, LV_OBJ_FLAG_CLICKABLE);

    s_progress = lw_bar(player, 236, 168, 200, 10, palette->accent);
    s_time_label = lw_text(player, "", &lw_font_18, palette->dim, 236, 188);

    lv_obj_t *prev = lw_button(player, "上一首", 90, 46, palette->card_soft, palette->text, &lw_font_18);
    lv_obj_set_pos(prev, 236, 226);
    lv_obj_t *play = lw_button(player, "播放", 120, 46, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(play, 336, 226);
    s_play_label = lv_obj_get_child(play, 0);
    lv_obj_t *next = lw_button(player, "下一首", 90, 46, palette->card_soft, palette->text, &lw_font_18);
    lv_obj_set_pos(next, 466, 226);

    lw_text(player, "音量", &lw_font_18, palette->dim, 32, 330);
    s_volume_label = lw_text(player, "", &lw_font_18, palette->text, 132, 330);
    lv_obj_t *vol_down = lw_button(player, "-", 44, 40, palette->card_soft, palette->text, &lw_font_22);
    lv_obj_set_pos(vol_down, 200, 322);
    lv_obj_t *vol_up = lw_button(player, "+", 44, 40, palette->card_soft, palette->text, &lw_font_22);
    lv_obj_set_pos(vol_up, 254, 322);

    lv_obj_t *list = lw_card(content, 522, 14, 470, 412);
    lw_text(list, "播放列表", &lw_font_22, palette->text, 28, 24);
    for (int i = 0; i < TRACK_COUNT; i++) {
        lv_obj_t *row = lw_soft_card(list, 28, 76 + i * 82, 414, 64);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, track_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_rows[i] = row;
        s_row_titles[i] = lw_text(row, s_track_titles[i], &lw_font_18, palette->text, 20, 12);
        s_row_artists[i] = lw_text(row, s_track_artist[i], &lw_font_18, palette->dim, 20, 38);
    }
    lw_text_wrap(list, "DEMO：播放器逻辑与进度可用，板载扬声器音频驱动尚未接入。", &lw_font_18, palette->dim, 28, 340, 400);

    lv_obj_add_event_cb(play, play_toggle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next, next_track, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(prev, prev_track, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(vol_up, volume_up, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(vol_down, volume_down, LV_EVENT_CLICKED, NULL);

    refresh();
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void destroy(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_progress = NULL;
    s_time_label = NULL;
    s_play_label = NULL;
    s_volume_label = NULL;
    s_title = NULL;
    s_artist = NULL;
    for (int i = 0; i < TRACK_COUNT; i++) {
        s_rows[i] = NULL;
        s_row_titles[i] = NULL;
        s_row_artists[i] = NULL;
    }
    s_playing = 0;
}

const lw_app_t lw_app_music_def = {
    .id = "music",
    .name = "音乐",
    .create = create,
    .destroy = destroy,
};
