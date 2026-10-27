/* Little World RPG core - pure C, no LVGL / no IDF dependencies.
 *
 * The module owns the room grid, the two actors, hotspots, one quest, a tiny
 * dialogue runtime and a versioned save blob. Rendering and input stay in the
 * host layer (Codex's OS or the standalone mock), so this can be unit tested on
 * the desktop and later compiled into the firmware unchanged.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LW_RPG_H
#define LW_RPG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LW_RPG_MAX_ACTORS 2
#define LW_RPG_MAX_HOTSPOTS 8
#define LW_RPG_MAX_QUEST_ITEMS 8
#define LW_RPG_MAX_LINES 8
#define LW_RPG_MAX_CHOICES 3
#define LW_RPG_MAX_DIALOGUES 8
#define LW_RPG_MAX_PATH 64
#define LW_RPG_ROOM_TILES 128 /* 16 x 8 */

#define LW_RPG_ACTOR_PLAYER 0
#define LW_RPG_ACTOR_PARTNER 1

typedef enum {
    LW_RPG_DIR_DOWN = 0,
    LW_RPG_DIR_LEFT = 1,
    LW_RPG_DIR_RIGHT = 2,
    LW_RPG_DIR_UP = 3,
} lw_rpg_dir_t;

typedef struct {
    int16_t x;
    int16_t y;
} lw_rpg_tile_t;

typedef struct {
    const char *name;
    uint8_t width; /* tiles */
    uint8_t height;
    const uint8_t *floor; /* width*height, 1 = walkable */
    lw_rpg_tile_t player_spawn;
    lw_rpg_tile_t partner_spawn;
} lw_rpg_room_t;

typedef struct {
    uint8_t item_id; /* quest item this hotspot counts for */
    lw_rpg_tile_t tile;
    uint8_t radius; /* interact range in tiles, manhattan */
    const char *label;
} lw_rpg_hotspot_t;

typedef struct {
    const char *speaker;
    const char *text;
} lw_rpg_line_t;

typedef struct {
    const char *text;
    uint32_t next_id; /* 0 = end of dialogue */
} lw_rpg_choice_t;

typedef struct {
    uint32_t id;
    uint8_t line_count;
    const lw_rpg_line_t *lines;
    uint8_t choice_count;
    const lw_rpg_choice_t *choices; /* asked after the last line */
} lw_rpg_dialogue_t;

typedef struct {
    uint32_t id;
    const char *title;
    const char *hint;
    uint8_t item_count;
    const uint8_t *items;
    uint32_t reward_id;
    const char *reward_label;
} lw_rpg_quest_t;

typedef struct {
    const lw_rpg_room_t *room;
    const lw_rpg_hotspot_t *hotspots;
    uint8_t hotspot_count;
    const lw_rpg_quest_t *quest;
    const lw_rpg_dialogue_t *dialogues;
    uint8_t dialogue_count;
    /* dialogue opened per quest item, index = item id - 1 */
    const uint32_t *item_dialogue;
} lw_rpg_world_t;

typedef struct {
    lw_rpg_tile_t pos;
    lw_rpg_dir_t dir;
    uint8_t moving;
    uint8_t anim_phase; /* 0..7 walking frame */
    uint8_t path[LW_RPG_MAX_PATH];
    uint8_t path_len;
    uint8_t path_index;
    uint32_t step_accum_ms;
} lw_rpg_actor_t;

typedef struct {
    const lw_rpg_world_t *world;
    lw_rpg_actor_t actors[LW_RPG_MAX_ACTORS];
    uint16_t collected_mask; /* bit per item id */
    uint8_t quest_complete;
    uint32_t unlocked_reward;
    uint8_t dialogue_active;
    uint32_t dialogue_id;
    uint8_t line_index;
    uint8_t choice_pending;
    uint8_t pending_item; /* item collected when the open dialogue closes */
    uint32_t reward_events; /* count of on_reward calls, for the host */
    uint32_t tick_ms;
} lw_rpg_state_t;

/* --- lifecycle ----------------------------------------------------------- */
bool lw_rpg_init(lw_rpg_state_t *state, const lw_rpg_world_t *world);
void lw_rpg_reset(lw_rpg_state_t *state);

/* --- queries ------------------------------------------------------------- */
bool lw_rpg_inside(const lw_rpg_room_t *room, int x, int y);
bool lw_rpg_walkable(const lw_rpg_room_t *room, int x, int y);
int lw_rpg_hotspot_at(const lw_rpg_state_t *state, int x, int y); /* index or -1 */
int lw_rpg_hotspot_near(const lw_rpg_state_t *state);            /* index or -1 */

/* Continuous render position in tile units. `x`/`y` are the logical tile plus
 * the fraction of the step currently in flight (0..1), so a host can draw a
 * walk that stays smooth between the fixed LW_RPG_STEP_MS logic steps instead
 * of snapping once per tile. Returns the logical tile when the actor stands. */
void lw_rpg_actor_point(const lw_rpg_state_t *state, int index, float *x, float *y);

/* --- input --------------------------------------------------------------- */
/* Tap a tile: the player walks there (BFS). Returns false when unreachable. */
bool lw_rpg_tap_tile(lw_rpg_state_t *state, int x, int y);
/* Interact with the hotspot in range: opens its dialogue. */
bool lw_rpg_interact(lw_rpg_state_t *state);

/* --- time ---------------------------------------------------------------- */
/* Advance movement and the partner follow logic by dt milliseconds. */
void lw_rpg_tick(lw_rpg_state_t *state, uint32_t dt_ms);

/* --- dialogue ------------------------------------------------------------ */
bool lw_rpg_dialogue_open(lw_rpg_state_t *state, uint32_t dialogue_id);
const lw_rpg_dialogue_t *lw_rpg_dialogue_current(const lw_rpg_state_t *state);
const lw_rpg_line_t *lw_rpg_dialogue_line(const lw_rpg_state_t *state);
bool lw_rpg_dialogue_has_choices(const lw_rpg_state_t *state);
const lw_rpg_choice_t *lw_rpg_dialogue_choice(const lw_rpg_state_t *state, int index);
/* Advance one line; when the last line is passed the dialogue closes and the
 * pending item (if any) is collected. Returns true when the dialogue closed. */
bool lw_rpg_dialogue_advance(lw_rpg_state_t *state);
bool lw_rpg_dialogue_choose(lw_rpg_state_t *state, int index);

/* --- quest --------------------------------------------------------------- */
int lw_rpg_quest_progress(const lw_rpg_state_t *state);
int lw_rpg_quest_total(const lw_rpg_state_t *state);
bool lw_rpg_quest_is_complete(const lw_rpg_state_t *state);

/* --- save ---------------------------------------------------------------- */
#define LW_RPG_SAVE_BYTES 40
uint32_t lw_rpg_crc32(const uint8_t *data, size_t length);
int lw_rpg_save_encode(const lw_rpg_state_t *state, uint8_t *out, size_t capacity);
int lw_rpg_save_decode(lw_rpg_state_t *state, const uint8_t *in, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* LW_RPG_H */
