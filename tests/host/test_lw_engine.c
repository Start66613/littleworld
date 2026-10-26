/* lw_engine: nine-slice geometry, scene projection, one clock. Host build, MSVC /W4 /WX.
 *
 * Every case here is a defect that shows up as "the UI looks wrong" and is
 * invisible in a code review: a stretched corner, a character whose feet float
 * when the pose changes, a theme that flips twice a second on a boundary, a
 * demo clock presented as the real time.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_day.h"
#include "lw_stage.h"
#include "lw_ui9.h"
#include "lw_ui_asset.h"
#include "lw/lw_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(name, expr) do { \
    checks++; \
    if (!(expr)) { failures++; printf("FAIL %s line %d\n", name, __LINE__); } \
} while (0)

/* ------------------------------------------------------------------ nine-slice */

static void test_nine_slice_basic(void)
{
    /* 32x32 button art with a 6 px border: the classic pixel-art frame. */
    lw_ui9_insets_t insets = {6, 6, 6, 6};
    CHECK("insets valid", lw_ui9_insets_valid(32, 32, &insets));

    lw_ui9_rect_t src[LW_UI9_PIECES];
    lw_ui9_rect_t dst[LW_UI9_PIECES];
    lw_ui9_status_t status = lw_ui9_slice(32, 32, &insets, 100, 50, 200, 120, src, dst);
    CHECK("slice ok", status == LW_UI9_OK);

    /* Corners keep the source size exactly - this is the whole point. */
    CHECK("corner 0 size", src[0].w == 6 && src[0].h == 6);
    CHECK("corner 0 target size", dst[0].w == 6 && dst[0].h == 6);
    CHECK("corner 2 size", dst[2].w == 6 && dst[2].h == 6);
    CHECK("corner 6 size", dst[6].w == 6 && dst[6].h == 6);
    CHECK("corner 8 size", dst[8].w == 6 && dst[8].h == 6);

    /* Edges stretch on exactly one axis. */
    CHECK("top edge target width", dst[1].w == 200 - 12 && dst[1].h == 6);
    CHECK("left edge target height", dst[3].w == 6 && dst[3].h == 120 - 12);
    CHECK("centre stretches both", dst[4].w == 200 - 12 && dst[4].h == 120 - 12);

    /* Source rectangles tile the source without gaps or overlap. */
    CHECK("src top row tiles", src[0].x == 0 && src[0].w + src[1].w + src[2].w == 32);
    CHECK("src left column tiles", src[0].y == 0 && src[0].h + src[3].h + src[6].h == 32);
    CHECK("src centre origin", src[4].x == 6 && src[4].y == 6);

    /* Target rectangles tile the target, anchored at the requested origin. */
    CHECK("dst origin", dst[0].x == 100 && dst[0].y == 50);
    CHECK("dst top-right corner", dst[2].x == 100 + 200 - 6);
    CHECK("dst bottom row y", dst[6].y == 50 + 120 - 6);
    CHECK("dst right edge x", dst[5].x == 100 + 200 - 6);
    CHECK("dst width tiles", dst[0].w + dst[1].w + dst[2].w == 200);
    CHECK("dst height tiles", dst[0].h + dst[3].h + dst[6].h == 120);
}

static void test_nine_slice_rejections(void)
{
    lw_ui9_insets_t too_wide = {20, 4, 20, 4};
    lw_ui9_rect_t src[LW_UI9_PIECES];
    lw_ui9_rect_t dst[LW_UI9_PIECES];
    /* left + right == width leaves no centre column. */
    CHECK("border wider than source refused",
          lw_ui9_slice(32, 32, &too_wide, 0, 0, 100, 100, src, dst) == LW_UI9_BAD_INSETS);
    CHECK("insets invalid", !lw_ui9_insets_valid(32, 32, &too_wide));

    lw_ui9_insets_t insets = {6, 6, 6, 6};
    /* A target smaller than the two corners cannot be built. */
    CHECK("target too small refused",
          lw_ui9_slice(32, 32, &insets, 0, 0, 10, 100, src, dst) == LW_UI9_BAD_TARGET);
    CHECK("zero target refused",
          lw_ui9_slice(32, 32, &insets, 0, 0, 0, 100, src, dst) == LW_UI9_BAD_TARGET);
    CHECK("zero source refused",
          lw_ui9_slice(0, 32, &insets, 0, 0, 100, 100, src, dst) == LW_UI9_BAD_SOURCE);
    CHECK("null insets refused",
          lw_ui9_slice(32, 32, NULL, 0, 0, 100, 100, src, dst) == LW_UI9_BAD_SOURCE);

    /* Exactly the minimum is legal: the centre becomes one pixel. */
    uint16_t min_w = 0;
    uint16_t min_h = 0;
    lw_ui9_minimum(32, 32, &insets, &min_w, &min_h);
    CHECK("minimum is borders plus one", min_w == 13 && min_h == 13);
    CHECK("minimum accepted",
          lw_ui9_slice(32, 32, &insets, 0, 0, min_w, min_h, src, dst) == LW_UI9_OK);
    CHECK("minimum centre is one pixel", dst[4].w == 1 && dst[4].h == 1);
    CHECK("fits minimum", lw_ui9_fits(32, 32, &insets, min_w, min_h));
    CHECK("does not fit below minimum", !lw_ui9_fits(32, 32, &insets, min_w - 1, min_h));

    /* Overflowing the int16 coordinate space is a hard error, not a wrap. */
    CHECK("overflow refused",
          lw_ui9_slice(32, 32, &insets, 32000, 0, 1000, 100, src, dst) == LW_UI9_OVERFLOW);
}

static void test_nine_slice_asymmetric(void)
{
    /* A dialog box with a wide left gutter for an icon and a thin right edge. */
    lw_ui9_insets_t insets = {24, 8, 4, 8};
    lw_ui9_rect_t src[LW_UI9_PIECES];
    lw_ui9_rect_t dst[LW_UI9_PIECES];
    CHECK("asymmetric ok", lw_ui9_slice(96, 64, &insets, 0, 0, 300, 200, src, dst) == LW_UI9_OK);
    CHECK("left corner 24", dst[0].w == 24);
    CHECK("right corner 4", dst[2].w == 4);
    CHECK("top edge 8", dst[1].h == 8);
    CHECK("centre width", dst[4].w == 300 - 28);
    CHECK("centre height", dst[4].h == 200 - 16);
    CHECK("source centre width", src[4].w == 96 - 28);
    CHECK("source right column", src[2].x == 96 - 4 && src[2].w == 4);
}

static void test_button_states(void)
{
    /* 48 px wide, three 24 px bands: normal / pressed / disabled. */
    uint16_t band = 0;
    CHECK("band height divides", lw_ui9_band_height(72, &band) && band == 24);
    CHECK("band height refuses remainder", !lw_ui9_band_height(70, &band));
    CHECK("band height refuses zero", !lw_ui9_band_height(0, &band));

    lw_ui9_rect_t rect;
    CHECK("normal rect", lw_ui9_state_rect(48, 72, LW_UI9_STATE_NORMAL, &rect) && rect.y == 0 &&
                             rect.h == 24 && rect.w == 48);
    CHECK("pressed rect", lw_ui9_state_rect(48, 72, LW_UI9_STATE_PRESSED, &rect) && rect.y == 24);
    CHECK("disabled rect",
          lw_ui9_state_rect(48, 72, LW_UI9_STATE_DISABLED, &rect) && rect.y == 48);
    CHECK("state out of range",
          !lw_ui9_state_rect(48, 72, LW_UI9_STATE_COUNT, &rect));
    CHECK("strip with remainder refused", !lw_ui9_state_rect(48, 71, LW_UI9_STATE_PRESSED, &rect));

    /* The pressed band must be usable as a nine-slice source on its own. */
    lw_ui9_insets_t insets = {8, 4, 8, 4};
    CHECK("pressed band slices", lw_ui9_fits(48, 24, &insets, 120, 40));
}

/* ------------------------------------------------------------------ stage */

static lw_stage_projection_t room(void)
{
    /* 1 world unit = 1 screen pixel at 1024x600, origin at the room's corner. */
    lw_stage_projection_t p = {0, 64, 256};
    return p;
}

static void test_stage_projection(void)
{
    lw_stage_projection_t p = room();
    CHECK("projection valid", lw_stage_projection_valid(&p));
    int32_t x = 0;
    int32_t y = 0;
    CHECK("origin projects", lw_stage_project(&p, (lw_stage_point_t){0, 0}, &x, &y) && x == 0 &&
                                 y == 64);
    CHECK("offset projects", lw_stage_project(&p, (lw_stage_point_t){300, 200}, &x, &y) &&
                                 x == 300 && y == 264);

    /* Round trip: a tap lands where the art was drawn. */
    lw_stage_point_t back;
    CHECK("unproject round trip", lw_stage_unproject(&p, 300, 264, &back) && back.x == 300 &&
                                      back.y == 200);

    /* A scaled room: 2x means 1 world unit is 2 screen pixels. */
    lw_stage_projection_t scaled = {100, 64, 512};
    CHECK("scaled project", lw_stage_project(&scaled, (lw_stage_point_t){10, 20}, &x, &y) &&
                                x == 120 && y == 104);

    /* Zero or absurd scale is refused instead of dividing by zero later. */
    lw_stage_projection_t bad = {0, 0, 0};
    CHECK("zero scale refused", !lw_stage_projection_valid(&bad));
    CHECK("zero scale project refused",
          !lw_stage_project(&bad, (lw_stage_point_t){1, 1}, &x, &y));
    CHECK("null projection refused", !lw_stage_projection_valid(NULL));

    /* A coordinate that cannot be represented must fail, not wrap. 32 Mpx is
     * still an int32, so use a world coordinate that genuinely overflows. */
    lw_stage_projection_t huge = {0, 0, 4096};
    CHECK("overflow refused", !lw_stage_project(&huge, (lw_stage_point_t){2000000000, 0}, &x, &y));

    /* Negative world coordinates round toward the nearest pixel, not toward
     * the origin: truncation would shift everything left of the origin. */
    lw_stage_projection_t half = {0, 0, 128};
    CHECK("negative rounding", lw_stage_project(&half, (lw_stage_point_t){-3, 0}, &x, &y) &&
                                   x == -2);
}

static void test_stage_actor_anchor(void)
{
    lw_stage_projection_t p = room();
    /* 108x212 frame, floor contact at (54, 209), drawn at native size. */
    lw_stage_actor_t actor = {{400, 300}, 54, 209, 212, 212, 256, LW_STAGE_FACE_RIGHT, 0, 0};
    lw_stage_placement_t place;
    CHECK("actor placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("anchor lands on the ground", place.baseline_y == 364);
    /* Top-left = ground - anchor. */
    CHECK("image left", place.screen_x == 400 - 54);
    CHECK("image top", place.screen_y == 364 - 209);
    CHECK("no scaling", place.scale_256 == 256);
    CHECK("facing right not mirrored", !place.mirrored);
    CHECK("actor height", lw_stage_actor_height(&place, 212) == 212);

    /* Same actor, asked to look 106 px tall: the frame scales to 1/2 and the
     * feet must stay on the same pixel. This is the "pose change makes the
     * character float" defect. */
    actor.visible_h = 106;
    CHECK("half-size placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("half-size scale", place.scale_256 == 128);
    CHECK("feet stay on the ground", place.baseline_y == 364);
    CHECK("height halves", lw_stage_actor_height(&place, 212) == 106);
    /* The anchor is scaled with the frame, so the image top moves down by the
     * scaled anchor distance (209 * 128 / 256 = 104.5 -> 105 with rounding),
     * which keeps the feet on the ground line. */
    CHECK("anchor corrected", place.screen_y == 364 - 105);

    /* Facing left flips the art but not the ground contact. */
    actor.visible_h = 212;
    actor.facing = LW_STAGE_FACE_LEFT;
    CHECK("facing left placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("facing left mirrored", place.mirrored);
    CHECK("facing left ground unchanged", place.baseline_y == 364);

    /* Author nudge applies last, in screen space. */
    actor.facing = LW_STAGE_FACE_RIGHT;
    actor.offset_x = 5;
    actor.offset_y = -3;
    CHECK("nudged placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("nudge x", place.screen_x == 400 - 54 + 5);
    CHECK("nudge y", place.screen_y == 364 - 209 - 3);

    /* Degenerate input is refused, never guessed. */
    lw_stage_actor_t zero = {{0, 0}, 0, 0, 0, 0, 256, LW_STAGE_FACE_RIGHT, 0, 0};
    CHECK("zero native height refused", !lw_stage_place_actor(&p, &zero, &place));
    CHECK("null actor refused", !lw_stage_place_actor(&p, NULL, &place));
}

static void test_stage_two_poses_match(void)
{
    /* The actual acceptance test for anchors: two frames of the same character
     * with different native sizes but the same visible height must land their
     * feet on the same pixel and occupy the same height. */
    lw_stage_projection_t p = room();
    lw_stage_actor_t idle = {{500, 400}, 54, 209, 212, 212, 256, LW_STAGE_FACE_RIGHT, 0, 0};
    lw_stage_actor_t wave = {{500, 400}, 52, 226, 226, 226, 256, LW_STAGE_FACE_RIGHT, 0, 0};
    lw_stage_placement_t a;
    lw_stage_placement_t b;
    CHECK("idle placed", lw_stage_place_actor(&p, &idle, &a));
    CHECK("wave placed", lw_stage_place_actor(&p, &wave, &b));
    CHECK("same ground line", a.baseline_y == b.baseline_y);
    CHECK("same on-screen height", lw_stage_actor_height(&a, 212) == 212);
    CHECK("wave taller frame is still 226", lw_stage_actor_height(&b, 226) == 226);

    /* Now normalise both to 200 px: they must agree on height, and the feet
     * must still be on the ground. */
    idle.visible_h = 200;
    wave.visible_h = 200;
    CHECK("idle normalised", lw_stage_place_actor(&p, &idle, &a));
    CHECK("wave normalised", lw_stage_place_actor(&p, &wave, &b));
    CHECK("normalised heights match", lw_stage_actor_height(&a, 212) ==
                                          lw_stage_actor_height(&b, 226));
    CHECK("normalised ground matches", a.baseline_y == b.baseline_y);
}

static void test_stage_occlusion(void)
{
    lw_stage_projection_t p = room();
    /* A sofa occupying screen x 300..700, y 300..420, floor line at 420. */
    lw_stage_occluder_t sofa = {300, 300, 400, 120, 420, LW_STAGE_NEAR_FURNITURE};
    lw_stage_actor_t actor = {{450, 380}, 54, 209, 212, 212, 256, LW_STAGE_FACE_RIGHT, 0, 0};
    lw_stage_placement_t place;
    CHECK("actor placed", lw_stage_place_actor(&p, &actor, &place));

    /* Ground y 380 projects to 444, which is in front of the sofa's 420 line. */
    lw_stage_occlusion_t occ = lw_stage_occlude(&sofa, &place, 108, 212);
    CHECK("in front overlaps", occ.overlaps);
    CHECK("in front not occluded", !occ.occluded);
    CHECK("in front not behind", !lw_stage_behind(&sofa, &place, 108, 212));

    /* Move the actor behind the sofa (smaller world y) and it is occluded. */
    actor.ground.y = 300;
    CHECK("behind placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("behind is behind", lw_stage_behind(&sofa, &place, 108, 212));
    occ = lw_stage_occlude(&sofa, &place, 108, 212);
    CHECK("behind occluded", occ.occluded);
    CHECK("behind clipped from the sofa top", occ.top == 300);
    /* The hidden band is the intersection of the two rectangles. This actor's
     * feet sit above the sofa's baseline, so its own bottom (155 + 212 = 367)
     * is the lower edge of the clip, not the sofa's 420. */
    CHECK("behind clipped to the sofa bottom", occ.bottom == 155 + 212);

    /* Standing exactly on the sofa's baseline is standing in front of it. */
    actor.ground.y = 420 - 64; /* project -> 420 */
    CHECK("baseline placed", lw_stage_place_actor(&p, &actor, &place));
    CHECK("baseline is not behind", !lw_stage_behind(&sofa, &place, 108, 212));

    /* Far away: no overlap, so no work for the renderer. */
    actor.ground.y = 0;
    CHECK("far placed", lw_stage_place_actor(&p, &actor, &place));
    occ = lw_stage_occlude(&sofa, &place, 108, 212);
    CHECK("far does not overlap", !occ.overlaps);

    /* Degenerate inputs are not occluded. */
    CHECK("null occluder safe", !lw_stage_behind(NULL, &place, 108, 212));
    occ = lw_stage_occlude(NULL, &place, 108, 212);
    CHECK("null occluder no overlap", !occ.overlaps && !occ.occluded);
    occ = lw_stage_occlude(&sofa, &place, 0, 0);
    CHECK("zero size no overlap", !occ.overlaps);
}

static void test_stage_dog_house(void)
{
    /* Four states stacked in one 200x400 strip. */
    int16_t y = 0;
    uint16_t h = 0;
    CHECK("empty band", lw_stage_house_rect(200, 400, LW_STAGE_HOUSE_EMPTY, &y, &h) && y == 0 &&
                            h == 100);
    CHECK("sleeping band", lw_stage_house_rect(200, 400, LW_STAGE_HOUSE_SLEEPING, &y, &h) &&
                               y == 100 && h == 100);
    CHECK("looking band", lw_stage_house_rect(200, 400, LW_STAGE_HOUSE_LOOKING, &y, &h) &&
                              y == 200);
    CHECK("stirring band", lw_stage_house_rect(200, 400, LW_STAGE_HOUSE_STIRRING, &y, &h) &&
                               y == 300);
    CHECK("bad state refused", !lw_stage_house_rect(200, 400, LW_STAGE_HOUSE_STATE_COUNT, &y, &h));
    CHECK("uneven strip refused", !lw_stage_house_rect(200, 401, LW_STAGE_HOUSE_EMPTY, &y, &h));

    CHECK("empty shows no pet", !lw_stage_house_shows_pet(LW_STAGE_HOUSE_EMPTY));
    CHECK("sleeping shows the pet", lw_stage_house_shows_pet(LW_STAGE_HOUSE_SLEEPING));
    CHECK("looking shows no pet", !lw_stage_house_shows_pet(LW_STAGE_HOUSE_LOOKING));
    CHECK("stirring shows the pet", lw_stage_house_shows_pet(LW_STAGE_HOUSE_STIRRING));

    /* The opening is the house rectangle inset by the wall thickness. */
    lw_stage_occluder_t house = {600, 380, 200, 160, 540, LW_STAGE_NEAR_FURNITURE};
    int16_t ox = 0;
    int16_t oy = 0;
    uint16_t ow = 0;
    uint16_t oh = 0;
    CHECK("opening computed", lw_stage_house_opening(&house, 24, &ox, &oy, &ow, &oh));
    CHECK("opening x", ox == 624);
    CHECK("opening y", oy == 404);
    CHECK("opening size", ow == 152 && oh == 112);
    CHECK("inset too large refused", !lw_stage_house_opening(&house, 100, &ox, &oy, &ow, &oh));
    CHECK("null house refused", !lw_stage_house_opening(NULL, 24, &ox, &oy, &ow, &oh));
}

/* ------------------------------------------------------------------ day / clock */

static lw_day_t make_day(void)
{
    lw_day_t day;
    lw_day_init(&day, NULL);
    return day;
}

static void test_day_defaults(void)
{
    lw_day_bounds_t bounds;
    lw_day_defaults(&bounds);
    CHECK("bounds valid", lw_day_bounds_valid(&bounds));
    CHECK("dawn 05:00", bounds.dawn_start == 5 * 3600);
    CHECK("day 08:00", bounds.day_start == 8 * 3600);
    CHECK("dusk 17:00", bounds.dusk_start == 17 * 3600);
    CHECK("night 20:00", bounds.night_start == 20 * 3600);
    CHECK("late 23:00", bounds.late_start == 23 * 3600);

    lw_day_bounds_t bad = bounds;
    bad.dawn_start = 0;
    CHECK("midnight dawn refused", !lw_day_bounds_valid(&bad));
    bad = bounds;
    bad.day_start = bounds.dawn_start;
    CHECK("equal boundaries refused", !lw_day_bounds_valid(&bad));
    bad = bounds;
    bad.late_start = 86400;
    CHECK("late at end of day refused", !lw_day_bounds_valid(&bad));
    CHECK("null bounds refused", !lw_day_bounds_valid(NULL));
}

static void test_day_phases(void)
{
    lw_day_t day = make_day();
    CHECK("04:59 late night", lw_day_phase_at(&day, 4 * 3600 + 59 * 60) == LW_DAY_PHASE_LATE_NIGHT);
    CHECK("05:00 dawn", lw_day_phase_at(&day, 5 * 3600) == LW_DAY_PHASE_DAWN);
    CHECK("07:59 dawn", lw_day_phase_at(&day, 7 * 3600 + 59 * 60) == LW_DAY_PHASE_DAWN);
    CHECK("08:00 day", lw_day_phase_at(&day, 8 * 3600) == LW_DAY_PHASE_DAY);
    CHECK("16:59 day", lw_day_phase_at(&day, 16 * 3600 + 59 * 60) == LW_DAY_PHASE_DAY);
    CHECK("17:00 dusk", lw_day_phase_at(&day, 17 * 3600) == LW_DAY_PHASE_DUSK);
    CHECK("19:59 dusk", lw_day_phase_at(&day, 19 * 3600 + 59 * 60) == LW_DAY_PHASE_DUSK);
    CHECK("20:00 night", lw_day_phase_at(&day, 20 * 3600) == LW_DAY_PHASE_NIGHT);
    CHECK("22:59 night", lw_day_phase_at(&day, 22 * 3600 + 59 * 60) == LW_DAY_PHASE_NIGHT);
    CHECK("23:00 late night", lw_day_phase_at(&day, 23 * 3600) == LW_DAY_PHASE_LATE_NIGHT);
    CHECK("23:59 late night", lw_day_phase_at(&day, 23 * 3600 + 59 * 60) == LW_DAY_PHASE_LATE_NIGHT);

    CHECK("morning theme", lw_day_theme(LW_DAY_PHASE_DAY) == LW_DAY_THEME_MORNING);
    CHECK("dawn theme is morning", lw_day_theme(LW_DAY_PHASE_DAWN) == LW_DAY_THEME_MORNING);
    CHECK("dusk theme is sunset", lw_day_theme(LW_DAY_PHASE_DUSK) == LW_DAY_THEME_SUNSET);
    CHECK("night theme", lw_day_theme(LW_DAY_PHASE_NIGHT) == LW_DAY_THEME_NIGHT);
    CHECK("late night theme", lw_day_theme(LW_DAY_PHASE_LATE_NIGHT) == LW_DAY_THEME_NIGHT);
}

static void test_day_sources(void)
{
    lw_day_t day = make_day();
    /* Nothing at all: no clock, and the UI must say so. */
    CHECK("invalid by default", day.source == LW_DAY_SOURCE_INVALID);
    char hm[6];
    lw_day_hm(&day, 0, hm);
    CHECK("invalid shows dashes", strcmp(hm, "--:--") == 0);
    CHECK("invalid needs qualifier", lw_day_needs_qualifier(&day));

    /* The rehearsal clock: present, but never trusted. */
    lw_day_use_demo(&day);
    CHECK("demo source", day.source == LW_DAY_SOURCE_DEMO);
    CHECK("demo not trusted", !day.trusted);
    CHECK("demo still needs qualifier", lw_day_needs_qualifier(&day));
    lw_day_hm(&day, 0, hm);
    CHECK("demo shows 19:30", strcmp(hm, "19:30") == 0);

    /* The demo clock does not drift with uptime - it is a rehearsal. */
    lw_day_t later = make_day();
    lw_day_use_demo(&later);
    lw_day_hm(&later, 10u * 60u * 60u * 1000u, hm);
    CHECK("demo does not drift", strcmp(hm, "19:30") == 0);

    /* Manual beats demo. */
    lw_day_t manual = make_day();
    lw_day_use_demo(&manual);
    CHECK("manual accepted",
          lw_day_set_manual(&manual, 2026, 10, 27, 9, 15, 480, 0));
    CHECK("manual source", manual.source == LW_DAY_SOURCE_MANUAL);
    CHECK("manual trusted", manual.trusted);
    CHECK("manual needs qualifier", lw_day_needs_qualifier(&manual));
    lw_day_hm(&manual, 0, hm);
    CHECK("manual shows the typed time", strcmp(hm, "09:15") == 0);
    CHECK("manual phase is day", manual.phase == LW_DAY_PHASE_DAY);

    /* Manual refuses impossible input instead of storing garbage. */
    CHECK("manual refuses 2025-02-30", !lw_day_set_manual(&manual, 2025, 2, 30, 9, 0, 480, 0));
    CHECK("manual refuses hour 24", !lw_day_set_manual(&manual, 2026, 1, 1, 24, 0, 480, 0));
    CHECK("manual refuses minute 60", !lw_day_set_manual(&manual, 2026, 1, 1, 0, 60, 480, 0));

    /* A real sync outranks manual. */
    lw_day_t real = make_day();
    lw_day_set_manual(&real, 2026, 1, 1, 12, 0, 480, 0);
    CHECK("real accepted", lw_day_set_real(&real, 1793000000, 480, 0));
    CHECK("real source", real.source == LW_DAY_SOURCE_REAL);
    CHECK("real trusted", real.trusted);
    CHECK("real needs no qualifier", !lw_day_needs_qualifier(&real));
    CHECK("real clears manual", !real.manual);

    /* A real clock advances with uptime; a stale one keeps the last value. */
    lw_day_t running = make_day();
    lw_day_set_real(&running, 1793000000, 480, 0);
    char first[6];
    char second[6];
    lw_day_hm(&running, 0, first);
    lw_day_hm(&running, 60u * 1000u, second);
    CHECK("real clock advances", strcmp(first, second) != 0);
    lw_day_mark_stale(&running, 60000);
    CHECK("stale is not trusted", !running.trusted);
    CHECK("stale keeps a time", running.has_time);
    CHECK("stale needs qualifier", lw_day_needs_qualifier(&running));

    /* Clearing a manual override falls back to demo, not to nothing. */
    lw_day_t cleared = make_day();
    lw_day_set_manual(&cleared, 2026, 5, 1, 10, 0, 480, 0);
    lw_day_clear_manual(&cleared, 0);
    CHECK("cleared falls back to demo", cleared.source == LW_DAY_SOURCE_DEMO);
    CHECK("cleared is not manual", !cleared.manual);

    /* clear() really removes the clock. */
    lw_day_clear(&cleared);
    CHECK("clear invalid", cleared.source == LW_DAY_SOURCE_INVALID);
    CHECK("clear has no time", !cleared.has_time);
}

static void test_day_hysteresis(void)
{
    /* A real clock one second before dusk. It must not flip the theme until the
     * new phase has been stable, or a boundary second repaints the room twice. */
    lw_day_t day = make_day();
    /* 2026-10-27 16:59:59 local (UTC+8). */
    int64_t utc = (int64_t)lw_days_from_civil(2026, 10, 27) * 86400 + (16 * 3600 + 59 * 60 + 59) -
                  8 * 3600;
    CHECK("real set", lw_day_set_real(&day, utc, 480, 0));
    CHECK("starts in day", day.phase == LW_DAY_PHASE_DAY);

    /* Tick to the boundary: 17:00:00. Phase must still be DAY (pending). */
    lw_day_tick(&day, 1000);
    CHECK("boundary not committed immediately", day.phase == LW_DAY_PHASE_DAY);
    CHECK("pending is dusk", day.pending == LW_DAY_PHASE_DUSK);

    /* Two seconds later: still not stable long enough. */
    lw_day_tick(&day, 2000);
    CHECK("still not committed", day.phase == LW_DAY_PHASE_DAY);

    /* Past the hysteresis window: committed exactly once. */
    lw_day_tick(&day, 4500);
    CHECK("committed after hysteresis", day.phase == LW_DAY_PHASE_DUSK);
    CHECK("one transition", day.transitions == 1);

    /* Further ticks in the same phase do not count as transitions. */
    lw_day_tick(&day, 6000);
    lw_day_tick(&day, 7000);
    CHECK("no extra transitions", day.transitions == 1);

    /* The whole-second gate: repeated ticks inside one second are cheap no-ops. */
    uint32_t transitions = day.transitions;
    for (unsigned i = 0; i < 50; i++) {
        lw_day_tick(&day, 7000 + i * 20u);
    }
    CHECK("gate holds", day.transitions == transitions);
}

static void test_day_bounds_change(void)
{
    lw_day_t day = make_day();
    int64_t utc = (int64_t)lw_days_from_civil(2026, 10, 27) * 86400 + 18 * 3600 - 8 * 3600;
    lw_day_set_real(&day, utc, 480, 0);
    CHECK("18:00 is dusk", day.phase == LW_DAY_PHASE_DUSK);

    /* The user moves dusk to 20:00: the change applies immediately, not after
     * three seconds. */
    lw_day_bounds_t bounds;
    lw_day_defaults(&bounds);
    bounds.dusk_start = 20 * 3600;
    bounds.night_start = 21 * 3600;
    CHECK("bounds accepted", lw_day_set_bounds(&day, &bounds, 0));
    CHECK("18:00 is now day", day.phase == LW_DAY_PHASE_DAY);
    CHECK("bounds stored", lw_day_bounds(&day)->dusk_start == 20 * 3600);

    /* Invalid bounds are refused and leave the old ones in place. */
    lw_day_bounds_t bad = bounds;
    bad.night_start = bounds.dusk_start;
    CHECK("invalid bounds refused", !lw_day_set_bounds(&day, &bad, 0));
    CHECK("bounds unchanged", lw_day_bounds(&day)->night_start == 21 * 3600);
}

static void test_day_next_boundary(void)
{
    lw_day_t day = make_day();
    /* 19:30 -> the next boundary is 20:00, i.e. 1800 s. */
    int64_t utc = (int64_t)lw_days_from_civil(2026, 10, 27) * 86400 + (19 * 3600 + 30 * 60) -
                  8 * 3600;
    lw_day_set_real(&day, utc, 480, 0);
    CHECK("next boundary in 30 minutes", lw_day_until_next(&day, 0) == 1800);

    /* 04:00 -> the next boundary is dawn at 05:00. */
    int64_t early = (int64_t)lw_days_from_civil(2026, 10, 27) * 86400 + 4 * 3600 - 8 * 3600;
    lw_day_set_real(&day, early, 480, 0);
    CHECK("next boundary is dawn", lw_day_until_next(&day, 0) == 3600);

    /* 23:30 -> wraps to 05:00 tomorrow. */
    int64_t late = (int64_t)lw_days_from_civil(2026, 10, 27) * 86400 + (23 * 3600 + 30 * 60) -
                   8 * 3600;
    lw_day_set_real(&day, late, 480, 0);
    CHECK("wraps to dawn", lw_day_until_next(&day, 0) == 5 * 3600 + 30 * 60);

    /* No clock, no answer. */
    lw_day_t empty = make_day();
    CHECK("no clock no boundary", lw_day_until_next(&empty, 0) == 0);
}

static void test_day_clock_fields(void)
{
    lw_day_t day = make_day();
    CHECK("manual date accepted", lw_day_set_manual(&day, 2026, 10, 27, 14, 5, 480, 0));
    int year = 0;
    int month = 0;
    int date = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    CHECK("clock fields", lw_day_clock(&day, 0, &year, &month, &date, &hour, &minute, &second));
    CHECK("year", year == 2026);
    CHECK("month", month == 10);
    CHECK("day", date == 27);
    CHECK("hour", hour == 14);
    CHECK("minute", minute == 5);
    CHECK("second", second == 0);

    /* A negative UTC offset still produces a sane local clock. */
    lw_day_t west = make_day();
    lw_day_set_manual(&west, 2026, 10, 27, 1, 0, -480, 0);
    CHECK("negative offset clock", lw_day_clock(&west, 0, NULL, NULL, NULL, &hour, &minute, NULL));
    CHECK("negative offset hour", hour == 1 && minute == 0);
}

/* ------------------------------------------------------------------ ui asset */

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/* Build one asset in a caller buffer. Returns total length. */
static size_t build_asset(uint8_t *buf, size_t cap, unsigned kind, unsigned format, unsigned w,
                          unsigned h, unsigned states, unsigned left, unsigned top, unsigned right,
                          unsigned bottom, bool pixel_art)
{
    size_t pixels = (size_t)w * h;
    size_t per_state = format == LW_UI_ASSET_RGB565A8 ? pixels * 3u : pixels * 2u;
    size_t total = LW_UI_ASSET_HEADER_BYTES + per_state * states;
    if (cap < total) {
        return 0;
    }
    memset(buf, 0, total);
    memcpy(buf, "LWUI0001", 8);
    put16(buf + 8, w);
    put16(buf + 10, h);
    buf[12] = (uint8_t)kind;
    buf[13] = (uint8_t)format;
    put16(buf + 14, states);
    put16(buf + 16, left);
    put16(buf + 18, top);
    put16(buf + 20, right);
    put16(buf + 22, bottom);
    put16(buf + 24, pixel_art ? 1u : 0u);
    put32(buf + 28, (unsigned long)(per_state * states));
    return total;
}

static void test_ui_asset_parse(void)
{
    static uint8_t buf[64 * 64 * 3 * 3 + 64];
    /* A 32x32 panel, 6 px border, RGB565, pixel art. */
    size_t len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 32, 32, 1, 6,
                             6, 6, 6, true);
    CHECK("panel asset built", len > LW_UI_ASSET_HEADER_BYTES);
    lw_ui_asset_t asset;
    CHECK("panel parses", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_OK);
    CHECK("panel kind", asset.kind == LW_UI_ASSET_PANEL);
    CHECK("panel size", asset.width == 32 && asset.height == 32);
    CHECK("panel states", asset.states == 1);
    CHECK("panel insets", asset.insets.left == 6 && asset.insets.bottom == 6);
    CHECK("panel pixel art", asset.pixel_art);
    CHECK("panel state bytes", lw_ui_asset_state_bytes(&asset) == 32u * 32u * 2u);
    CHECK("panel payload offset", asset.payload == buf + LW_UI_ASSET_HEADER_BYTES);

    /* Fits: the two 6 px corners plus at least one pixel of centre, so 13 is
     * the smallest legal target and 12 must be refused. */
    CHECK("panel fits 200x120", lw_ui_asset_fits(&asset, 200, 120));
    CHECK("panel fits exactly minimum", lw_ui_asset_fits(&asset, 13, 13));
    CHECK("panel refuses 12", !lw_ui_asset_fits(&asset, 12, 13));
    uint16_t min_w = 0;
    uint16_t min_h = 0;
    lw_ui_asset_minimum(&asset, &min_w, &min_h);
    CHECK("panel minimum", min_w == 13 && min_h == 13);

    /* One state only, so only NORMAL is addressable. */
    lw_ui9_rect_t rect;
    CHECK("panel normal rect", lw_ui_asset_state_rect(&asset, LW_UI9_STATE_NORMAL, &rect));
    CHECK("panel rect covers all", rect.x == 0 && rect.y == 0 && rect.w == 32 && rect.h == 32);
    CHECK("panel has no pressed", !lw_ui_asset_state_rect(&asset, LW_UI9_STATE_PRESSED, &rect));

    /* A 3-state button strip: each band is 24 px tall in a 72 px strip. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_BUTTON, LW_UI_ASSET_RGB565A8, 24, 24, 3, 4, 4,
                      4, 4, true);
    CHECK("button built", len > 0);
    CHECK("button parses", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_OK);
    CHECK("button state bytes", lw_ui_asset_state_bytes(&asset) == 24u * 24u * 3u);
    CHECK("button normal", lw_ui_asset_state_rect(&asset, LW_UI9_STATE_NORMAL, &rect) &&
                               rect.y == 0 && rect.h == 24);
    CHECK("button pressed", lw_ui_asset_state_rect(&asset, LW_UI9_STATE_PRESSED, &rect) &&
                                rect.y == 24 && rect.h == 24);
    CHECK("button disabled", lw_ui_asset_state_rect(&asset, LW_UI9_STATE_DISABLED, &rect) &&
                                 rect.y == 48 && rect.h == 24);
    CHECK("button pixel art", asset.pixel_art);
    CHECK("button fits", lw_ui_asset_fits(&asset, 100, 40));

    /* An icon is stretchable at any size and carries no insets requirement. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_ICON, LW_UI_ASSET_RGB565, 16, 16, 1, 0, 0, 0, 0,
                      false);
    CHECK("icon parses", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_OK);
    CHECK("icon fits 1x1", lw_ui_asset_fits(&asset, 1, 1));
    CHECK("icon minimum zero", (lw_ui_asset_minimum(&asset, &min_w, &min_h), min_w == 0 && min_h == 0));
    CHECK("icon not pixel art", !asset.pixel_art);
}

static void test_ui_asset_rejections(void)
{
    static uint8_t buf[64 * 64 * 3 * 3 + 64];
    lw_ui_asset_t asset;
    size_t len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 32, 32, 1, 6,
                             6, 6, 6, true);

    CHECK("header too short", lw_ui_asset_parse(buf, 31, &asset) == LW_UI_ASSET_TOO_SMALL);
    CHECK("null buffer", lw_ui_asset_parse(NULL, 0, &asset) == LW_UI_ASSET_TOO_SMALL);

    uint8_t saved = buf[0];
    buf[0] = 'X';
    CHECK("bad magic", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_MAGIC);
    buf[0] = saved;

    buf[12] = 9;
    CHECK("bad kind", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_KIND);
    buf[12] = LW_UI_ASSET_PANEL;

    buf[13] = 7;
    CHECK("bad format", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_FORMAT);
    buf[13] = LW_UI_ASSET_RGB565;

    buf[26] = 1;
    CHECK("reserved not zero", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_RESERVED);
    buf[26] = 0;

    /* Zero width. */
    put16(buf + 8, 0);
    CHECK("zero width", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_SIZE);
    put16(buf + 8, 32);

    /* Over the side limit. */
    put16(buf + 10, LW_UI_ASSET_MAX_SIDE + 1);
    CHECK("oversized", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_SIZE);
    put16(buf + 10, 32);

    /* A panel claiming three bands is ambiguous. */
    put16(buf + 14, 3);
    CHECK("panel with three states", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_STATES);
    put16(buf + 14, 1);

    /* A button with one band cannot express pressed art. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_BUTTON, LW_UI_ASSET_RGB565, 24, 24, 1, 4, 4, 4,
                      4, true);
    CHECK("button with one state", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_STATES);

    /* Borders wider than the bitmap: the nine-slice has no centre. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 16, 16, 1, 8, 4, 8, 4,
                      true);
    CHECK("insets leave no centre", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_INSETS);

    /* data_bytes that disagrees with the geometry: the classic truncation bug. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 32, 32, 1, 6, 6, 6, 6,
                      true);
    put32(buf + 28, 100);
    CHECK("payload size mismatch", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_BAD_PAYLOAD);

    /* Correct header, truncated file. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 32, 32, 1, 6, 6, 6, 6,
                      true);
    CHECK("truncated payload", lw_ui_asset_parse(buf, len - 1, &asset) == LW_UI_ASSET_BAD_PAYLOAD);
}

/* ------------------------------------------------------------------ repack
 *
 * The nine-slice copy is the one place where the on-disk layout (one colour
 * plane, then one alpha plane) meets LVGL's requirement that a descriptor point
 * at one contiguous piece. A bug here shows up as a sliced panel with the wrong
 * colours, so the checks below paint a recognisable pattern and read it back.
 */

static uint16_t pixel_at(const uint8_t *plane, unsigned w, unsigned x, unsigned y)
{
    const uint8_t *p = plane + ((size_t)y * w + x) * 2u;
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void paint_asset(uint8_t *buf, unsigned w, unsigned h, unsigned states, unsigned format)
{
    size_t image_h = (size_t)h * states;
    size_t colour_plane = (size_t)w * image_h * 2u;
    for (unsigned y = 0; y < image_h; y++) {
        for (unsigned x = 0; x < w; x++) {
            /* Distinct value per pixel so a row/column mix-up cannot pass. */
            uint16_t value = (uint16_t)((y * w + x) & 0xFFFFu);
            uint8_t *p = buf + LW_UI_ASSET_HEADER_BYTES + ((size_t)y * w + x) * 2u;
            p[0] = (uint8_t)(value & 0xFFu);
            p[1] = (uint8_t)((value >> 8) & 0xFFu);
        }
    }
    if (format == LW_UI_ASSET_RGB565A8) {
        for (size_t i = 0; i < colour_plane / 2u; i++) {
            buf[LW_UI_ASSET_HEADER_BYTES + colour_plane + i] = (uint8_t)(i & 0xFFu);
        }
    }
}

static void test_ui_asset_copy_piece(void)
{
    static uint8_t buf[64 * 64 * 3 * 3 + 64];
    static uint8_t out[64 * 64 * 3];
    lw_ui_asset_t asset;
    lw_ui9_rect_t src;

    /* RGB565 panel, 16x16: copy a 4x4 window and compare every pixel. */
    size_t len = build_asset(buf, sizeof(buf), LW_UI_ASSET_PANEL, LW_UI_ASSET_RGB565, 16, 16, 1, 4, 4,
                             4, 4, true);
    CHECK("rgb565 panel built", len > 0);
    paint_asset(buf, 16, 16, 1, LW_UI_ASSET_RGB565);
    CHECK("rgb565 panel parses", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_OK);

    CHECK("piece bytes rgb565", lw_ui_asset_piece_bytes(&asset, 4, 4) == 4u * 4u * 2u);
    src.x = 3;
    src.y = 5;
    src.w = 4;
    src.h = 4;
    size_t made = lw_ui_asset_copy_piece(&asset, 0, &src, out, sizeof(out));
    CHECK("piece copied rgb565", made == 4u * 4u * 2u);
    bool same = true;
    for (unsigned y = 0; y < 4 && same; y++) {
        for (unsigned x = 0; x < 4; x++) {
            unsigned sx = 3 + x;
            unsigned sy = 5 + y;
            uint16_t want = (uint16_t)((sy * 16u + sx) & 0xFFFFu);
            if (pixel_at(out, 4, x, y) != want) {
                same = false;
                break;
            }
        }
    }
    CHECK("piece pixels rgb565", same);
    /* The output is tightly packed: 4 px per row, not 16. */
    CHECK("piece row stride", pixel_at(out, 4, 0, 1) == (uint16_t)((6u * 16u + 3u) & 0xFFFFu));

    /* RGB565A8 button strip: three bands, alpha plane after all colour rows. */
    len = build_asset(buf, sizeof(buf), LW_UI_ASSET_BUTTON, LW_UI_ASSET_RGB565A8, 12, 12, 3, 3, 3, 3,
                      3, true);
    CHECK("a8 button built", len > 0);
    paint_asset(buf, 12, 12, 3, LW_UI_ASSET_RGB565A8);
    CHECK("a8 button parses", lw_ui_asset_parse(buf, len, &asset) == LW_UI_ASSET_OK);
    CHECK("piece bytes a8", lw_ui_asset_piece_bytes(&asset, 2, 2) == 2u * 2u * 3u);

    /* Band 1 starts at y = 12. Ask for its bottom-right 2x2 corner. */
    src.x = 10;
    src.y = 22;
    src.w = 2;
    src.h = 2;
    made = lw_ui_asset_copy_piece(&asset, LW_UI9_STATE_PRESSED, &src, out, sizeof(out));
    CHECK("piece copied a8", made == 2u * 2u * 3u);
    CHECK("a8 colour plane", pixel_at(out, 2, 0, 0) == (uint16_t)((22u * 12u + 10u) & 0xFFFFu));
    /* Alpha for that pixel lives at colour_out + row*w + col in the output. */
    size_t colour_out = 2u * 2u * 2u;
    size_t image_pixels = 12u * 36u;
    CHECK("a8 alpha plane", out[colour_out] == (uint8_t)(((size_t)22u * 12u + 10u) & 0xFFu));
    CHECK("a8 alpha row", out[colour_out + 2] == (uint8_t)(((size_t)23u * 12u + 10u) & 0xFFu));
    (void)image_pixels;

    /* Out-of-bounds and capacity failures write nothing and return 0. */
    memset(out, 0xAB, sizeof(out));
    src.x = 11;
    src.y = 22;
    src.w = 2;
    src.h = 2;
    CHECK("piece right edge", lw_ui_asset_copy_piece(&asset, 0, &src, out, sizeof(out)) == 0);
    src.x = 10;
    src.y = 35;
    CHECK("piece bottom edge", lw_ui_asset_copy_piece(&asset, 0, &src, out, sizeof(out)) == 0);
    src.x = 10;
    src.y = 22;
    CHECK("piece small buffer", lw_ui_asset_copy_piece(&asset, 0, &src, out, 4) == 0);
    CHECK("piece untouched", out[0] == 0xAB);
    CHECK("piece bad state", lw_ui_asset_copy_piece(&asset, 9, &src, out, sizeof(out)) == 0);
    CHECK("piece null", lw_ui_asset_copy_piece(NULL, 0, &src, out, sizeof(out)) == 0);
}

/* ------------------------------------------------------------------ asset file
 *
 * End-to-end check used by tools/test_ui_assets.ps1: the Python tool writes a
 * .lwu file, and THIS is the real firmware parser reading it back off disk. A
 * round-trip that only compares the Python encoder against itself would not
 * catch a layout disagreement, which is the only bug that matters here.
 */
static int check_asset_file(const char *path)
{
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        printf("cannot open %s\n", path);
        return 1;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return 1;
    }
    long size = ftell(stream);
    if (size <= 0 || size > 8 * 1024 * 1024) {
        fclose(stream);
        printf("implausible size %ld\n", size);
        return 1;
    }
    rewind(stream);
    uint8_t *bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(stream);
        return 1;
    }
    size_t got = fread(bytes, 1, (size_t)size, stream);
    fclose(stream);
    if (got != (size_t)size) {
        free(bytes);
        printf("short read\n");
        return 1;
    }
    lw_ui_asset_t asset;
    lw_ui_asset_status_t status = lw_ui_asset_parse(bytes, got, &asset);
    if (status != LW_UI_ASSET_OK) {
        printf("REJECT %s: %s\n", path, lw_ui_asset_status_text(status));
        free(bytes);
        return 1;
    }
    uint16_t min_w = 0;
    uint16_t min_h = 0;
    lw_ui_asset_minimum(&asset, &min_w, &min_h);
    printf("OK %s kind=%d %ux%u states=%u fmt=%d insets=%u,%u,%u,%u pixel_art=%d "
           "payload=%lu min=%ux%u\n",
           path, (int)asset.kind, asset.width, asset.height, asset.states, (int)asset.format,
           asset.insets.left, asset.insets.top, asset.insets.right, asset.insets.bottom,
           asset.pixel_art ? 1 : 0, (unsigned long)asset.data_bytes, min_w, min_h);
    free(bytes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--check-asset") == 0) {
        return check_asset_file(argv[2]);
    }
    test_nine_slice_basic();
    test_nine_slice_rejections();
    test_nine_slice_asymmetric();
    test_button_states();

    test_stage_projection();
    test_stage_actor_anchor();
    test_stage_two_poses_match();
    test_stage_occlusion();
    test_stage_dog_house();

    test_day_defaults();
    test_day_phases();
    test_day_sources();
    test_day_hysteresis();
    test_day_bounds_change();
    test_day_next_boundary();
    test_day_clock_fields();

    test_ui_asset_parse();
    test_ui_asset_rejections();
    test_ui_asset_copy_piece();

    printf("\nengine: %u checks, %u failure(s)\n", checks, failures);
    if (failures != 0) {
        printf("ENGINE TESTS FAILED\n");
        return 1;
    }
    printf("ENGINE TESTS PASSED\n");
    return 0;
}
