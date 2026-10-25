/* Demo room, hotspots, dialogue and quest for the RPG vertical slice.
 *
 * All text is obviously editable placeholder content: no real dates, no real
 * memories, nothing that claims to be the couple's private history.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_rpg.h"

/* 16 x 8 tiles. 1 = walkable floor, 0 = blocked (walls / furniture). */
static const uint8_t s_floor[16 * 8] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const lw_rpg_room_t s_room = {
    .name = "our_room",
    .width = 16,
    .height = 8,
    .floor = s_floor,
    .player_spawn = {6, 5},
    .partner_spawn = {9, 5},
};

/* Three hotspots: photo box, letter desk, potted plant. They sit next to
 * blocked furniture tiles so the player has to walk to them. */
static const lw_rpg_hotspot_t s_hotspots[] = {
    {.item_id = 1, .tile = {5, 3}, .radius = 1, .label = "照片盒"},
    {.item_id = 2, .tile = {9, 4}, .radius = 1, .label = "信封桌"},
    {.item_id = 3, .tile = {3, 3}, .radius = 1, .label = "小盆栽"},
};

static const lw_rpg_line_t s_photo_lines[] = {
    {"男方", "照片盒里有一叠空白的相纸。"},
    {"qianqian", "（示例）先把它们放进第一页吧。"},
};

static const lw_rpg_line_t s_letter_lines[] = {
    {"男方", "桌上有一封还没写地址的信。"},
    {"qianqian", "（示例）留到最后一页再封口。"},
};

static const lw_rpg_line_t s_plant_lines[] = {
    {"qianqian", "（示例）小盆栽今天也长高了一点点。"},
};

static const lw_rpg_choice_t s_finish_choices[] = {
    {"放回照片盒", 0},
    {"再摆一会儿", 0},
};

static const lw_rpg_dialogue_t s_dialogues[] = {
    {.id = 10, .line_count = 2, .lines = s_photo_lines, .choice_count = 0, .choices = NULL},
    {.id = 11, .line_count = 2, .lines = s_letter_lines, .choice_count = 0, .choices = NULL},
    {.id = 12, .line_count = 1, .lines = s_plant_lines, .choice_count = 0, .choices = NULL},
    {.id = 13, .line_count = 1, .lines = s_plant_lines, .choice_count = 2, .choices = s_finish_choices},
};

static const uint8_t s_quest_items[] = {1, 2, 3};

static const lw_rpg_quest_t s_quest = {
    .id = 100,
    .title = "整理回忆盒",
    .hint = "找到照片、信封和小盆栽，把它们放回盒子。",
    .item_count = 3,
    .items = s_quest_items,
    .reward_id = 9001,
    .reward_label = "新摆件：小相框",
};

/* item id (1-based) -> dialogue id */
static const uint32_t s_item_dialogue[] = {10, 11, 12};

const lw_rpg_world_t *lw_rpg_demo_world(void)
{
    static const lw_rpg_world_t world = {
        .room = &s_room,
        .hotspots = s_hotspots,
        .hotspot_count = (uint8_t)(sizeof(s_hotspots) / sizeof(s_hotspots[0])),
        .quest = &s_quest,
        .dialogues = s_dialogues,
        .dialogue_count = (uint8_t)(sizeof(s_dialogues) / sizeof(s_dialogues[0])),
        .item_dialogue = s_item_dialogue,
    };
    return &world;
}
