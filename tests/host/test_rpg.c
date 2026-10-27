/* Host tests for the Little World RPG core. Plain C, no framework.
 *
 * Build:  cl /W4 /WX /I rpg\include /I rpg\data tests\host\test_rpg.c rpg\src\lw_rpg.c
 *         rpg\src\lw_rpg_save.c rpg\data\lw_rpg_home.c /Fe:test_rpg.exe
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_rpg.h"
#include "lw_rpg_demo.h"

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        s_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            s_failures++;                                                               \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        s_checks++;                                                                     \
        long _va = (long)(a);                                                           \
        long _vb = (long)(b);                                                           \
        if (_va != _vb) {                                                               \
            s_failures++;                                                               \
            printf("FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b,  \
                   _va, _vb);                                                           \
        }                                                                               \
    } while (0)

static void run_ticks(lw_rpg_state_t *state, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        lw_rpg_tick(state, 140);
    }
}

static void test_init(void)
{
    lw_rpg_state_t state;
    CHECK(lw_rpg_init(&state, lw_rpg_demo_world()));
    CHECK_EQ(state.actors[LW_RPG_ACTOR_PLAYER].pos.x, 6);
    CHECK_EQ(state.actors[LW_RPG_ACTOR_PLAYER].pos.y, 5);
    CHECK_EQ(state.actors[LW_RPG_ACTOR_PARTNER].pos.x, 9);
    CHECK_EQ(lw_rpg_quest_total(&state), 3);
    CHECK_EQ(lw_rpg_quest_progress(&state), 0);
    CHECK(!lw_rpg_quest_is_complete(&state));
    CHECK_EQ(state.reward_events, 0);
}

static void test_grid(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    const lw_rpg_room_t *room = state.world->room;
    CHECK(lw_rpg_inside(room, 0, 0));
    CHECK(!lw_rpg_inside(room, -1, 0));
    CHECK(!lw_rpg_inside(room, 16, 0));
    CHECK(lw_rpg_walkable(room, 6, 5));
    CHECK(!lw_rpg_walkable(room, 2, 2)); /* furniture block */
    CHECK(!lw_rpg_walkable(room, -1, 3));
}

static void test_movement(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    CHECK(lw_rpg_tap_tile(&state, 5, 3));
    CHECK(state.actors[0].moving);
    run_ticks(&state, 8);
    CHECK_EQ(state.actors[0].pos.x, 5);
    CHECK_EQ(state.actors[0].pos.y, 3);
    CHECK(!state.actors[0].moving);

    CHECK(!lw_rpg_tap_tile(&state, 2, 2)); /* blocked tile */
    CHECK(lw_rpg_tap_tile(&state, 0, 0));
    run_ticks(&state, 32);
    CHECK_EQ(state.actors[0].pos.x, 0);
    CHECK_EQ(state.actors[0].pos.y, 0);
}

static void test_partner_follow(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    CHECK(lw_rpg_tap_tile(&state, 1, 6));
    run_ticks(&state, 24);
    CHECK_EQ(state.actors[0].pos.x, 1);
    CHECK_EQ(state.actors[0].pos.y, 6);
    /* the partner must have closed the distance below the follow threshold */
    int dx = state.actors[1].pos.x - state.actors[0].pos.x;
    int dy = state.actors[1].pos.y - state.actors[0].pos.y;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    CHECK(dx + dy <= 2);
}

static void test_interaction_and_quest(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());

    CHECK(lw_rpg_tap_tile(&state, 5, 3));
    run_ticks(&state, 8);
    CHECK_EQ(lw_rpg_hotspot_near(&state), 0);
    CHECK(lw_rpg_interact(&state));
    CHECK(state.dialogue_active);
    CHECK(lw_rpg_dialogue_current(&state) != NULL);
    if (lw_rpg_dialogue_current(&state) != NULL) {
        CHECK_EQ(lw_rpg_dialogue_current(&state)->id, 10);
    }
    CHECK_EQ(strcmp(lw_rpg_dialogue_line(&state)->speaker, "男方"), 0);

    CHECK(!lw_rpg_dialogue_advance(&state)); /* line 2 */
    CHECK_EQ(state.line_index, 1);
    CHECK(lw_rpg_dialogue_advance(&state)); /* closes */
    CHECK(!state.dialogue_active);
    CHECK_EQ(lw_rpg_quest_progress(&state), 1);
    CHECK_EQ(state.collected_mask & 1u, 1u);

    /* interacting again on a collected hotspot does nothing */
    CHECK(!lw_rpg_interact(&state));

    /* second item */
    CHECK(lw_rpg_tap_tile(&state, 10, 4));
    run_ticks(&state, 12);
    CHECK(lw_rpg_interact(&state));
    CHECK(!lw_rpg_dialogue_advance(&state));
    CHECK(lw_rpg_dialogue_advance(&state));
    CHECK_EQ(lw_rpg_quest_progress(&state), 2);
    CHECK(!lw_rpg_quest_is_complete(&state));

    /* third item completes the quest once */
    CHECK(lw_rpg_tap_tile(&state, 3, 3));
    run_ticks(&state, 24);
    CHECK(lw_rpg_interact(&state));
    CHECK(lw_rpg_dialogue_advance(&state));
    CHECK(lw_rpg_quest_is_complete(&state));
    CHECK_EQ(state.unlocked_reward, 9001);
    CHECK_EQ(state.reward_events, 1);
    CHECK_EQ(lw_rpg_quest_progress(&state), 3);
}

static void test_choices(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    CHECK(lw_rpg_dialogue_open(&state, 13));
    CHECK(!lw_rpg_dialogue_advance(&state)); /* last single line -> choices */
    CHECK(state.choice_pending);
    CHECK(lw_rpg_dialogue_has_choices(&state));
    CHECK_EQ(strcmp(lw_rpg_dialogue_choice(&state, 0)->text, "放回照片盒"), 0);
    CHECK(lw_rpg_dialogue_choice(&state, 5) == NULL);
    CHECK(!lw_rpg_dialogue_advance(&state)); /* blocked until a choice */
    CHECK(lw_rpg_dialogue_choose(&state, 0));
    CHECK(!state.dialogue_active);
}

static void test_save_roundtrip(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    CHECK(lw_rpg_tap_tile(&state, 5, 3));
    run_ticks(&state, 8);
    CHECK(lw_rpg_interact(&state));
    CHECK(!lw_rpg_dialogue_advance(&state)); /* line 2 */
    CHECK(lw_rpg_dialogue_advance(&state));  /* close */

    uint8_t blob[LW_RPG_SAVE_BYTES];
    CHECK_EQ(lw_rpg_save_encode(&state, blob, sizeof(blob)), LW_RPG_SAVE_BYTES);

    lw_rpg_state_t loaded;
    lw_rpg_init(&loaded, lw_rpg_demo_world());
    CHECK_EQ(lw_rpg_save_decode(&loaded, blob, sizeof(blob)), 0);
    CHECK_EQ(loaded.actors[0].pos.x, state.actors[0].pos.x);
    CHECK_EQ(loaded.actors[0].pos.y, state.actors[0].pos.y);
    CHECK_EQ(loaded.collected_mask, state.collected_mask);
    CHECK_EQ(lw_rpg_quest_progress(&loaded), 1);

    /* corrupt one byte -> CRC failure */
    uint8_t bad[LW_RPG_SAVE_BYTES];
    memcpy(bad, blob, sizeof(bad));
    bad[10] ^= 0xFFu;
    CHECK_EQ(lw_rpg_save_decode(&loaded, bad, sizeof(bad)), -4);

    /* wrong length / magic / version */
    CHECK_EQ(lw_rpg_save_decode(&loaded, blob, sizeof(blob) - 1), -1);
    memcpy(bad, blob, sizeof(bad));
    bad[0] ^= 0x01u;
    CHECK_EQ(lw_rpg_save_decode(&loaded, bad, sizeof(bad)), -2);
    memcpy(bad, blob, sizeof(bad));
    bad[4] = 9;
    CHECK(lw_rpg_save_decode(&loaded, bad, sizeof(bad)) != 0);

    /* out of bounds player position is rejected even with a valid CRC */
    memcpy(bad, blob, sizeof(bad));
    bad[6] = 200;
    uint32_t crc = lw_rpg_crc32(bad, LW_RPG_SAVE_BYTES - 4);
    bad[36] = (uint8_t)(crc & 0xFFu);
    bad[37] = (uint8_t)((crc >> 8) & 0xFFu);
    bad[38] = (uint8_t)((crc >> 16) & 0xFFu);
    bad[39] = (uint8_t)((crc >> 24) & 0xFFu);
    CHECK_EQ(lw_rpg_save_decode(&loaded, bad, sizeof(bad)), -5);
}

static void test_reset(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    CHECK(lw_rpg_tap_tile(&state, 5, 3));
    run_ticks(&state, 8);
    CHECK(lw_rpg_interact(&state));
    lw_rpg_reset(&state);
    CHECK_EQ(state.actors[0].pos.x, 6);
    CHECK_EQ(state.actors[0].pos.y, 5);
    CHECK_EQ(state.collected_mask, 0);
    CHECK(!state.quest_complete);
    CHECK(!state.dialogue_active);
}

/* The MVP fluidity fix: the render point must advance in sub-tile amounts
 * between the core's fixed 140 ms logic steps. Before this, hosts only saw the
 * tile index and had to fake motion with a translate animation that restarted
 * at every tile boundary. */
static void test_render_interpolation(void)
{
    lw_rpg_state_t state;
    lw_rpg_init(&state, lw_rpg_demo_world());
    float x, y;
    lw_rpg_actor_point(&state, LW_RPG_ACTOR_PLAYER, &x, &y);
    /* Standing still reports the exact logical tile. */
    CHECK_EQ((int)(x + 0.5f), 6);
    CHECK_EQ((int)(y + 0.5f), 5);

    CHECK(lw_rpg_tap_tile(&state, 10, 7)); /* 5 tiles right, 4 down: 9 steps */
    float px = x, py = y;
    int sub_tile_frames = 0;
    float worst = 0.0f;
    /* 7 frames of 20 ms is one logic step, so 80 frames covers a 9-step walk. */
    for (int i = 0; i < 80; i++) {
        lw_rpg_tick(&state, 20);
        lw_rpg_actor_point(&state, LW_RPG_ACTOR_PLAYER, &x, &y);
        float dx = x - px, dy = y - py;
        if (dx < 0.0f) dx = -dx;
        if (dy < 0.0f) dy = -dy;
        float step = dx > dy ? dx : dy;
        if (step > worst) worst = step;
        /* A 20 ms frame may cross one logic-step boundary, so the largest legal
         * jump is about one step plus a frame: 1/7 tile. Whole-tile jumps mean
         * the host is snapping again. */
        CHECK(step <= 0.16f);
        if (step > 0.0f && step < 0.99f) sub_tile_frames++;
        /* Never past the target tile. */
        CHECK(x <= 10.0f + 0.001f && x >= 5.0f - 0.001f);
        CHECK(y <= 7.0f + 0.001f && y >= 3.0f - 0.001f);
        px = x; py = y;
    }
    /* (6,5) -> (10,7) is six steps, so ~42 of the 80 frames are walking. Every
     * one of them must land on a sub-tile position; a snapping host would show
     * only six distinct whole-tile positions here. */
    CHECK(sub_tile_frames > 30);
    CHECK(!state.actors[LW_RPG_ACTOR_PLAYER].moving);
    CHECK_EQ((int)(x + 0.5f), 10);
    CHECK_EQ((int)(y + 0.5f), 7);
    CHECK(worst > 0.0f); /* the walk really moved */

    /* Path bookkeeping: the direction in flight must be the one still to come,
     * otherwise the interpolated walk always trails one tile behind. */
    CHECK(lw_rpg_tap_tile(&state, 10, 5));
    lw_rpg_tick(&state, 140);
    lw_rpg_tick(&state, 70);
    lw_rpg_actor_point(&state, LW_RPG_ACTOR_PLAYER, &x, &y);
    CHECK(state.actors[LW_RPG_ACTOR_PLAYER].moving);
    CHECK(y < 7.0f && y > 5.0f); /* halfway up, not still on y==7 */
}

int main(void)
{
#define RUN(fn)                                                                         \
    do {                                                                                \
        printf("-- %s\n", #fn);                                                         \
        fflush(stdout);                                                                 \
        fn();                                                                           \
    } while (0)
    RUN(test_init);
    RUN(test_grid);
    RUN(test_movement);
    RUN(test_partner_follow);
    RUN(test_render_interpolation);
    RUN(test_interaction_and_quest);
    RUN(test_choices);
    RUN(test_save_roundtrip);
    RUN(test_reset);

    printf("\n%d checks, %d failure(s)\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("RPG CORE TESTS PASSED\n");
    }
    return s_failures == 0 ? 0 : 1;
}
