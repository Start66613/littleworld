/* Little World RPG core implementation.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_rpg.h"

#include <string.h>

#define LW_RPG_STEP_MS 140u
#define LW_RPG_FOLLOW_DISTANCE 2

static const int s_dx[4] = {0, -1, 1, 0};
static const int s_dy[4] = {1, 0, 0, -1};

static int manhattan(lw_rpg_tile_t a, lw_rpg_tile_t b)
{
    int dx = (int)a.x - (int)b.x;
    int dy = (int)a.y - (int)b.y;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx + dy;
}

bool lw_rpg_inside(const lw_rpg_room_t *room, int x, int y)
{
    if (room == NULL) {
        return false;
    }
    return x >= 0 && y >= 0 && x < (int)room->width && y < (int)room->height;
}

bool lw_rpg_walkable(const lw_rpg_room_t *room, int x, int y)
{
    if (!lw_rpg_inside(room, x, y) || room->floor == NULL) {
        return false;
    }
    return room->floor[y * (int)room->width + x] != 0;
}

static void actor_clear_path(lw_rpg_actor_t *actor)
{
    actor->path_len = 0;
    actor->path_index = 0;
    actor->moving = 0;
    actor->step_accum_ms = 0;
}

/* Breadth first search, 4 directions, small fixed queue. Returns path length or
 * 0 when the target is unreachable. The path is a list of direction bytes. */
static int find_path(const lw_rpg_room_t *room, lw_rpg_tile_t from, lw_rpg_tile_t to, uint8_t *out, int out_cap)
{
    if (room == NULL || out == NULL || out_cap <= 0) {
        return 0;
    }
    if (!lw_rpg_walkable(room, from.x, from.y) || !lw_rpg_walkable(room, to.x, to.y)) {
        return 0;
    }
    if (from.x == to.x && from.y == to.y) {
        return 0;
    }

    int16_t prev[LW_RPG_ROOM_TILES];
    uint8_t seen[LW_RPG_ROOM_TILES];
    int16_t queue[LW_RPG_ROOM_TILES];
    int tiles = (int)room->width * (int)room->height;
    if (tiles > LW_RPG_ROOM_TILES) {
        tiles = LW_RPG_ROOM_TILES;
    }
    for (int i = 0; i < tiles; i++) {
        prev[i] = -1;
        seen[i] = 0;
    }

    int head = 0;
    int tail = 0;
    int start = (int)from.y * (int)room->width + (int)from.x;
    int goal = (int)to.y * (int)room->width + (int)to.x;
    queue[tail++] = (int16_t)start;
    seen[start] = 1;

    while (head < tail) {
        int cur = queue[head++];
        if (cur == goal) {
            break;
        }
        int cx = cur % (int)room->width;
        int cy = cur / (int)room->width;
        for (int d = 0; d < 4; d++) {
            int nx = cx + s_dx[d];
            int ny = cy + s_dy[d];
            if (!lw_rpg_walkable(room, nx, ny)) {
                continue;
            }
            int ni = ny * (int)room->width + nx;
            if (seen[ni]) {
                continue;
            }
            seen[ni] = 1;
            prev[ni] = (int16_t)cur;
            queue[tail++] = (int16_t)ni;
        }
    }
    if (!seen[goal]) {
        return 0;
    }

    /* walk backwards to collect directions */
    uint8_t reversed[LW_RPG_MAX_PATH];
    int count = 0;
    int cur = goal;
    while (cur != start && count < LW_RPG_MAX_PATH) {
        int p = prev[cur];
        if (p < 0) {
            return 0;
        }
        int cx = cur % (int)room->width;
        int cy = cur / (int)room->width;
        int px = p % (int)room->width;
        int py = p / (int)room->width;
        uint8_t dir = LW_RPG_DIR_DOWN;
        if (cx > px) {
            dir = LW_RPG_DIR_RIGHT;
        } else if (cx < px) {
            dir = LW_RPG_DIR_LEFT;
        } else if (cy > py) {
            dir = LW_RPG_DIR_DOWN;
        } else {
            dir = LW_RPG_DIR_UP;
        }
        reversed[count++] = dir;
        cur = p;
    }
    for (int i = 0; i < count; i++) {
        out[i] = reversed[count - 1 - i];
    }
    return count > out_cap ? out_cap : count;
}

bool lw_rpg_init(lw_rpg_state_t *state, const lw_rpg_world_t *world)
{
    if (state == NULL || world == NULL || world->room == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->world = world;
    state->actors[LW_RPG_ACTOR_PLAYER].pos = world->room->player_spawn;
    state->actors[LW_RPG_ACTOR_PLAYER].dir = LW_RPG_DIR_DOWN;
    state->actors[LW_RPG_ACTOR_PARTNER].pos = world->room->partner_spawn;
    state->actors[LW_RPG_ACTOR_PARTNER].dir = LW_RPG_DIR_DOWN;
    return true;
}

void lw_rpg_reset(lw_rpg_state_t *state)
{
    if (state == NULL || state->world == NULL) {
        return;
    }
    const lw_rpg_world_t *world = state->world;
    lw_rpg_init(state, world);
}

int lw_rpg_hotspot_at(const lw_rpg_state_t *state, int x, int y)
{
    if (state == NULL || state->world == NULL) {
        return -1;
    }
    const lw_rpg_world_t *w = state->world;
    for (int i = 0; i < (int)w->hotspot_count; i++) {
        if (w->hotspots[i].tile.x == x && w->hotspots[i].tile.y == y) {
            return i;
        }
    }
    return -1;
}

int lw_rpg_hotspot_near(const lw_rpg_state_t *state)
{
    if (state == NULL || state->world == NULL) {
        return -1;
    }
    const lw_rpg_world_t *w = state->world;
    lw_rpg_tile_t p = state->actors[LW_RPG_ACTOR_PLAYER].pos;
    int best = -1;
    int best_d = 99;
    for (int i = 0; i < (int)w->hotspot_count; i++) {
        int d = manhattan(p, w->hotspots[i].tile);
        if (d <= (int)w->hotspots[i].radius && d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

bool lw_rpg_tap_tile(lw_rpg_state_t *state, int x, int y)
{
    if (state == NULL || state->world == NULL) {
        return false;
    }
    const lw_rpg_room_t *room = state->world->room;
    if (!lw_rpg_walkable(room, x, y)) {
        return false;
    }
    lw_rpg_actor_t *player = &state->actors[LW_RPG_ACTOR_PLAYER];
    lw_rpg_tile_t target = {(int16_t)x, (int16_t)y};
    int len = find_path(room, player->pos, target, player->path, LW_RPG_MAX_PATH);
    if (len <= 0) {
        return false;
    }
    player->path_len = (uint8_t)len;
    player->path_index = 0;
    player->moving = 1;
    player->step_accum_ms = 0;
    return true;
}

static void step_actor(lw_rpg_state_t *state, int index)
{
    lw_rpg_actor_t *actor = &state->actors[index];
    if (!actor->moving || actor->path_index >= actor->path_len) {
        actor_clear_path(actor);
        return;
    }
    uint8_t dir = actor->path[actor->path_index++];
    actor->dir = (lw_rpg_dir_t)dir;
    actor->pos.x = (int16_t)(actor->pos.x + s_dx[dir]);
    actor->pos.y = (int16_t)(actor->pos.y + s_dy[dir]);
    actor->anim_phase = (uint8_t)((actor->anim_phase + 1) & 7);
    if (actor->path_index >= actor->path_len) {
        actor_clear_path(actor);
    }
}

static void update_partner(lw_rpg_state_t *state)
{
    lw_rpg_actor_t *player = &state->actors[LW_RPG_ACTOR_PLAYER];
    lw_rpg_actor_t *partner = &state->actors[LW_RPG_ACTOR_PARTNER];
    if (partner->moving) {
        return;
    }
    if (manhattan(partner->pos, player->pos) <= LW_RPG_FOLLOW_DISTANCE) {
        return;
    }
    int len = find_path(state->world->room, partner->pos, player->pos, partner->path, LW_RPG_MAX_PATH);
    if (len <= 0) {
        return;
    }
    partner->path_len = (uint8_t)len;
    partner->path_index = 0;
    partner->moving = 1;
    partner->step_accum_ms = 0;
}

void lw_rpg_tick(lw_rpg_state_t *state, uint32_t dt_ms)
{
    if (state == NULL || state->world == NULL) {
        return;
    }
    state->tick_ms += dt_ms;
    for (int i = 0; i < LW_RPG_MAX_ACTORS; i++) {
        lw_rpg_actor_t *actor = &state->actors[i];
        if (!actor->moving) {
            continue;
        }
        actor->step_accum_ms += dt_ms;
        while (actor->step_accum_ms >= LW_RPG_STEP_MS && actor->moving) {
            actor->step_accum_ms -= LW_RPG_STEP_MS;
            step_actor(state, i);
        }
    }
    update_partner(state);
}

/* --- dialogue ------------------------------------------------------------ */

static const lw_rpg_dialogue_t *find_dialogue(const lw_rpg_world_t *w, uint32_t id)
{
    if (w == NULL || w->dialogues == NULL) {
        return NULL;
    }
    for (int i = 0; i < (int)w->dialogue_count; i++) {
        if (w->dialogues[i].id == id) {
            return &w->dialogues[i];
        }
    }
    return NULL;
}

bool lw_rpg_dialogue_open(lw_rpg_state_t *state, uint32_t dialogue_id)
{
    if (state == NULL || state->world == NULL) {
        return false;
    }
    if (find_dialogue(state->world, dialogue_id) == NULL) {
        return false;
    }
    state->dialogue_active = 1;
    state->dialogue_id = dialogue_id;
    state->line_index = 0;
    state->choice_pending = 0;
    return true;
}

const lw_rpg_dialogue_t *lw_rpg_dialogue_current(const lw_rpg_state_t *state)
{
    if (state == NULL || !state->dialogue_active) {
        return NULL;
    }
    return find_dialogue(state->world, state->dialogue_id);
}

const lw_rpg_line_t *lw_rpg_dialogue_line(const lw_rpg_state_t *state)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(state);
    if (d == NULL || d->lines == NULL || state->line_index >= d->line_count) {
        return NULL;
    }
    return &d->lines[state->line_index];
}

bool lw_rpg_dialogue_has_choices(const lw_rpg_state_t *state)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(state);
    return d != NULL && state->choice_pending && d->choice_count > 0;
}

const lw_rpg_choice_t *lw_rpg_dialogue_choice(const lw_rpg_state_t *state, int index)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(state);
    if (d == NULL || !state->choice_pending || index < 0 || index >= (int)d->choice_count) {
        return NULL;
    }
    return &d->choices[index];
}

static void collect_pending_item(lw_rpg_state_t *state);

bool lw_rpg_dialogue_advance(lw_rpg_state_t *state)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(state);
    if (d == NULL) {
        return false;
    }
    if (state->choice_pending) {
        return false; /* must pick a choice first */
    }
    if ((int)state->line_index + 1 < (int)d->line_count) {
        state->line_index++;
        return false;
    }
    if (d->choice_count > 0) {
        state->choice_pending = 1;
        return false;
    }
    state->dialogue_active = 0;
    collect_pending_item(state);
    return true;
}

bool lw_rpg_dialogue_choose(lw_rpg_state_t *state, int index)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(state);
    if (d == NULL || !state->choice_pending || index < 0 || index >= (int)d->choice_count) {
        return false;
    }
    uint32_t next = d->choices[index].next_id;
    state->choice_pending = 0;
    if (next == 0) {
        state->dialogue_active = 0;
        collect_pending_item(state);
        return true;
    }
    state->dialogue_id = next;
    state->line_index = 0;
    return false;
}

/* --- quest --------------------------------------------------------------- */

static bool cursor_apply_quest(lw_rpg_state_t *state)
{
    const lw_rpg_quest_t *q = state->world->quest;
    if (q == NULL) {
        return false;
    }
    int progress = lw_rpg_quest_progress(state);
    if (progress >= (int)q->item_count && !state->quest_complete) {
        state->quest_complete = 1;
        state->unlocked_reward = q->reward_id;
        state->reward_events++;
        return true;
    }
    return false;
}

static void collect_pending_item(lw_rpg_state_t *state)
{
    if (state->pending_item == 0) {
        return;
    }
    state->collected_mask |= (uint16_t)(1u << (state->pending_item - 1));
    state->pending_item = 0;
    (void)cursor_apply_quest(state);
}

int lw_rpg_quest_total(const lw_rpg_state_t *state)
{
    if (state == NULL || state->world == NULL || state->world->quest == NULL) {
        return 0;
    }
    return (int)state->world->quest->item_count;
}

int lw_rpg_quest_progress(const lw_rpg_state_t *state)
{
    if (state == NULL || state->world == NULL || state->world->quest == NULL) {
        return 0;
    }
    const lw_rpg_quest_t *q = state->world->quest;
    int count = 0;
    for (int i = 0; i < (int)q->item_count; i++) {
        uint8_t item = q->items[i];
        if (item > 0 && (state->collected_mask & (1u << (item - 1))) != 0) {
            count++;
        }
    }
    return count;
}

bool lw_rpg_quest_is_complete(const lw_rpg_state_t *state)
{
    return state != NULL && state->quest_complete != 0;
}

bool lw_rpg_interact(lw_rpg_state_t *state)
{
    if (state == NULL || state->world == NULL || state->dialogue_active) {
        return false;
    }
    int index = lw_rpg_hotspot_near(state);
    if (index < 0) {
        return false;
    }
    const lw_rpg_world_t *w = state->world;
    const lw_rpg_hotspot_t *spot = &w->hotspots[index];
    uint8_t item = spot->item_id;
    if (item > 0 && (state->collected_mask & (1u << (item - 1))) != 0) {
        return false; /* already collected */
    }
    if (w->item_dialogue == NULL || item == 0) {
        return false;
    }
    uint32_t dialogue_id = w->item_dialogue[item - 1];
    if (!lw_rpg_dialogue_open(state, dialogue_id)) {
        return false;
    }
    state->pending_item = item; /* collected when the dialogue closes */
    return true;
}
