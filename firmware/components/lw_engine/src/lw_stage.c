/* Scene projection and occlusion. No LVGL, no allocation. SPDX-License-Identifier: MIT */
#include "lw_stage.h"

#include <limits.h>
#include <stddef.h>

#define LW_STAGE_MAX_SCALE 4096u

bool lw_stage_projection_valid(const lw_stage_projection_t *p)
{
    if (p == NULL) {
        return false;
    }
    if (p->scale_256 == 0 || p->scale_256 > LW_STAGE_MAX_SCALE) {
        return false;
    }
    /* The origin itself is unconstrained (a room may legitimately start above
     * the viewport), but it must be representable. */
    return true;
}

static bool mul_ok(int64_t value, int32_t *out)
{
    if (value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

bool lw_stage_project(const lw_stage_projection_t *p, lw_stage_point_t world, int32_t *screen_x,
                      int32_t *screen_y)
{
    if (!lw_stage_projection_valid(p)) {
        return false;
    }
    /* Round to nearest rather than truncate: truncation of a negative world
     * coordinate rounds toward the origin and shifts every object behind the
     * room's origin by one pixel. */
    int64_t dx = (int64_t)world.x * (int64_t)p->scale_256;
    int64_t dy = (int64_t)world.y * (int64_t)p->scale_256;
    int64_t rx = (dx >= 0 ? dx + 128 : dx - 128) / 256;
    int64_t ry = (dy >= 0 ? dy + 128 : dy - 128) / 256;
    int32_t sx = 0;
    int32_t sy = 0;
    if (!mul_ok((int64_t)p->origin_x + rx, &sx) || !mul_ok((int64_t)p->origin_y + ry, &sy)) {
        return false;
    }
    if (screen_x != NULL) {
        *screen_x = sx;
    }
    if (screen_y != NULL) {
        *screen_y = sy;
    }
    return true;
}

bool lw_stage_unproject(const lw_stage_projection_t *p, int32_t screen_x, int32_t screen_y,
                        lw_stage_point_t *world)
{
    if (!lw_stage_projection_valid(p) || world == NULL) {
        return false;
    }
    int64_t wx = ((int64_t)screen_x - p->origin_x) * 256 / p->scale_256;
    int64_t wy = ((int64_t)screen_y - p->origin_y) * 256 / p->scale_256;
    if (wx < INT32_MIN || wx > INT32_MAX || wy < INT32_MIN || wy > INT32_MAX) {
        return false;
    }
    world->x = (int32_t)wx;
    world->y = (int32_t)wy;
    return true;
}

bool lw_stage_place_actor(const lw_stage_projection_t *p, const lw_stage_actor_t *actor,
                          lw_stage_placement_t *out)
{
    if (p == NULL || actor == NULL || out == NULL || !lw_stage_projection_valid(p)) {
        return false;
    }
    if (actor->native_h == 0) {
        return false;
    }
    int32_t ground_x = 0;
    int32_t ground_y = 0;
    if (!lw_stage_project(p, actor->ground, &ground_x, &ground_y)) {
        return false;
    }

    /* Combine the scene scale with the visible-height correction. The actor is
     * authored at native_h and should occupy visible_h on screen; when the art
     * pipeline already drew it at the right size the two are equal and the
     * correction is exactly 256 (no change). */
    uint32_t scale = actor->scale_256 != 0 ? actor->scale_256 : p->scale_256;
    if (actor->visible_h != 0 && actor->visible_h != actor->native_h) {
        uint64_t corrected = (uint64_t)scale * actor->visible_h;
        corrected = (corrected + actor->native_h / 2u) / actor->native_h;
        if (corrected == 0 || corrected > LW_STAGE_MAX_SCALE) {
            return false;
        }
        scale = (uint32_t)corrected;
    }

    /* The anchor is the pixel that touches the floor. Scaling happens around
     * the image's top-left, so the anchor's own offset must be scaled the same
     * way and rounded to nearest; computing the correction as a delta and
     * truncating leaves the feet half a pixel off at fractional scales, which
     * is visible as a one-pixel bob when a pose changes size. */
    int64_t scaled_anchor_x =
        ((int64_t)actor->anchor_x * scale + 128) / 256;
    int64_t scaled_anchor_y =
        ((int64_t)actor->anchor_y * scale + 128) / 256;
    int64_t left = (int64_t)ground_x - scaled_anchor_x + actor->offset_x;
    int64_t top = (int64_t)ground_y - scaled_anchor_y + actor->offset_y;
    if (left < INT16_MIN || left > INT16_MAX || top < INT16_MIN || top > INT16_MAX) {
        return false;
    }
    out->screen_x = (int16_t)left;
    out->screen_y = (int16_t)top;
    out->baseline_y = (int16_t)ground_y;
    out->scale_256 = (uint16_t)scale;
    out->mirrored = actor->facing == LW_STAGE_FACE_LEFT;
    return true;
}

uint16_t lw_stage_actor_height(const lw_stage_placement_t *placement, uint16_t native_h)
{
    if (placement == NULL || native_h == 0) {
        return 0;
    }
    uint64_t scaled = (uint64_t)native_h * placement->scale_256 / 256u;
    if (scaled > 0xFFFFu) {
        return 0xFFFFu;
    }
    return (uint16_t)scaled;
}

bool lw_stage_behind(const lw_stage_occluder_t *occluder, const lw_stage_placement_t *placement,
                     uint16_t native_w, uint16_t native_h)
{
    (void)native_w;
    (void)native_h;
    if (occluder == NULL || placement == NULL) {
        return false;
    }
    /* The actor's floor contact is its own baseline, not the bottom of the
     * image box: an anchor above the frame bottom leaves transparent rows
     * under the feet, and using the box would sink the actor behind a sofa it
     * is actually standing in front of. */
    return placement->baseline_y < occluder->baseline_y;
}

lw_stage_occlusion_t lw_stage_occlude(const lw_stage_occluder_t *occluder,
                                      const lw_stage_placement_t *placement, uint16_t native_w,
                                      uint16_t native_h)
{
    lw_stage_occlusion_t result = {false, false, 0, 0};
    if (occluder == NULL || placement == NULL || native_w == 0 || native_h == 0) {
        return result;
    }
    uint16_t draw_h = lw_stage_actor_height(placement, native_h);
    uint32_t draw_w = (uint32_t)native_w * placement->scale_256 / 256u;
    int32_t a_left = placement->screen_x;
    int32_t a_top = placement->screen_y;
    int32_t a_right = a_left + (int32_t)draw_w;
    int32_t a_bottom = a_top + (int32_t)draw_h;
    int32_t o_left = occluder->x;
    int32_t o_top = occluder->y;
    int32_t o_right = o_left + occluder->w;
    int32_t o_bottom = o_top + occluder->h;
    bool overlaps = a_left < o_right && o_left < a_right && a_top < o_bottom && o_top < a_bottom;
    result.overlaps = overlaps;
    if (!overlaps) {
        return result;
    }
    if (!lw_stage_behind(occluder, placement, native_w, native_h)) {
        return result;
    }
    result.occluded = true;
    result.top = (int16_t)(o_top > a_top ? o_top : a_top);
    int32_t bottom = o_bottom < a_bottom ? o_bottom : a_bottom;
    if (bottom < INT16_MIN || bottom > INT16_MAX) {
        bottom = INT16_MAX;
    }
    result.bottom = (int16_t)bottom;
    return result;
}

bool lw_stage_house_rect(uint16_t strip_w, uint16_t strip_h, lw_stage_house_state_t state,
                         int16_t *y, uint16_t *h)
{
    if (strip_w == 0 || strip_h == 0) {
        return false;
    }
    if ((unsigned)state >= LW_STAGE_HOUSE_STATE_COUNT) {
        return false;
    }
    if (strip_h % LW_STAGE_HOUSE_STATE_COUNT != 0) {
        return false;
    }
    uint16_t band = (uint16_t)(strip_h / LW_STAGE_HOUSE_STATE_COUNT);
    if (band == 0) {
        return false;
    }
    if (y != NULL) {
        *y = (int16_t)(band * (uint16_t)state);
    }
    if (h != NULL) {
        *h = band;
    }
    return true;
}

bool lw_stage_house_shows_pet(lw_stage_house_state_t state)
{
    return state == LW_STAGE_HOUSE_SLEEPING || state == LW_STAGE_HOUSE_STIRRING;
}

bool lw_stage_house_opening(const lw_stage_occluder_t *house, int16_t inset, int16_t *x,
                            int16_t *y, uint16_t *w, uint16_t *h)
{
    if (house == NULL || house->w <= 0 || house->h <= 0 || inset < 0) {
        return false;
    }
    if ((int32_t)inset * 2 >= house->w || (int32_t)inset * 2 >= house->h) {
        return false;
    }
    int32_t ox = (int32_t)house->x + inset;
    int32_t oy = (int32_t)house->y + inset;
    uint16_t ow = (uint16_t)(house->w - inset * 2);
    uint16_t oh = (uint16_t)(house->h - inset * 2);
    if (ox < INT16_MIN || ox > INT16_MAX || oy < INT16_MIN || oy > INT16_MAX) {
        return false;
    }
    if (x != NULL) {
        *x = (int16_t)ox;
    }
    if (y != NULL) {
        *y = (int16_t)oy;
    }
    if (w != NULL) {
        *w = ow;
    }
    if (h != NULL) {
        *h = oh;
    }
    return true;
}
