/* Little World - scene space, anchors and occlusion (pure C, host tested).
 *
 * The problem this solves: a character is authored as a bitmap with an anchor
 * pixel (the point that touches the floor), but the scene places furniture at
 * world coordinates. Without one shared projection, every new pose needs a
 * hand-tuned screen offset and the character "jumps" when the animation changes
 * because a different frame has a different anchor.
 *
 * Three coordinate spaces, and exactly one function between each pair:
 *
 *   asset   pixel inside a frame, origin top-left
 *   actor   metres-ish world units on the floor plane, origin at the room's
 *           back-left corner, +x right, +y toward the viewer (down the screen)
 *   screen  device pixels, origin top-left of the scene viewport
 *
 * The projection is deliberately trivial (no perspective, no rotation): this is
 * a 2D pixel-art room, and a fake camera would cost a divide per object for a
 * result the art already encodes. What it does buy is consistency - the same
 * ground line, the same scale, the same facing for every actor and prop.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draw order, back to front. The numeric order IS the z order: the renderer
 * creates one LVGL container per layer and never re-sorts objects. */
typedef enum {
    LW_STAGE_BACKGROUND = 0,   /* the painted room, no interaction */
    LW_STAGE_FAR_FURNITURE,    /* bookshelf, window frame: always behind */
    LW_STAGE_FLOOR,            /* rugs and floor decals */
    LW_STAGE_CONTACT_SHADOW,   /* one soft ellipse per actor, on the floor */
    LW_STAGE_ACTOR,            /* characters */
    LW_STAGE_NEAR_FURNITURE,   /* sofa front, dog-house rim: occludes actors */
    LW_STAGE_FOREGROUND,       /* vignette, dust motes, nothing interactive */
    LW_STAGE_UI,               /* text and controls, always on top */
    LW_STAGE_LAYER_COUNT
} lw_stage_layer_t;

typedef enum {
    LW_STAGE_FACE_RIGHT = 0,
    LW_STAGE_FACE_LEFT = 1
} lw_stage_facing_t;

/* A point on the floor plane, in world units. */
typedef struct {
    int32_t x, y;
} lw_stage_point_t;

/* The projection: where the world's origin lands on screen, and how many screen
 * pixels one world unit is worth. scale_256 follows the LVGL convention so the
 * value can be handed to lv_image_set_scale() unchanged. */
typedef struct {
    int32_t origin_x, origin_y;
    uint16_t scale_256;
} lw_stage_projection_t;

/* Everything needed to place one actor, in the order the art pipeline supplies
 * it. anchor_* is the frame pixel that touches the floor; visible_h is how tall
 * the character should look on screen, which is what keeps a taller pose from
 * growing the character. */
typedef struct {
    lw_stage_point_t ground;    /* world position of the feet */
    uint16_t anchor_x, anchor_y; /* floor-contact pixel inside the frame */
    uint16_t visible_h;          /* desired on-screen height, 0 = native */
    uint16_t native_h;           /* source frame height */
    uint16_t scale_256;          /* scene scale for this actor */
    lw_stage_facing_t facing;
    int16_t offset_x, offset_y;  /* author nudges, applied last */
} lw_stage_actor_t;

typedef struct {
    int16_t screen_x, screen_y; /* top-left of the image object */
    int16_t baseline_y;         /* screen y of the floor-contact point */
    uint16_t scale_256;         /* after the visible_h correction */
    bool mirrored;
} lw_stage_placement_t;

/* --- projection --------------------------------------------------------- */

/* Refuse a projection that would make every coordinate collapse or overflow. */
bool lw_stage_projection_valid(const lw_stage_projection_t *p);

/* World point -> screen point. Returns false on overflow rather than wrapping,
 * because a wrapped coordinate puts an actor on the wrong side of the room. */
bool lw_stage_project(const lw_stage_projection_t *p, lw_stage_point_t world, int32_t *screen_x,
                      int32_t *screen_y);

/* Screen point -> world point (used by hit testing: a tap is in screen space,
 * hotspots are authored in world space). */
bool lw_stage_unproject(const lw_stage_projection_t *p, int32_t screen_x, int32_t screen_y,
                        lw_stage_point_t *world);

/* --- actor placement ---------------------------------------------------- */

/* Compute the image position and scale for one actor.
 *
 * The correction that matters: when visible_h differs from native_h the actor
 * is scaled, and the anchor must stay glued to the ground. Scaling around the
 * image's top-left corner would lift the feet off the floor by
 * anchor_y * (1 - scale), which is exactly the "character floats or sinks when
 * the pose changes" bug. */
bool lw_stage_place_actor(const lw_stage_projection_t *p, const lw_stage_actor_t *actor,
                          lw_stage_placement_t *out);

/* Height the actor will actually occupy on screen after placement. Lets the
 * caller verify that two poses of the same character match. */
uint16_t lw_stage_actor_height(const lw_stage_placement_t *placement, uint16_t native_h);

/* --- occlusion ---------------------------------------------------------- */

/* One rectangular region that hides whatever is behind it. The depth decides
 * who wins: an actor whose ground y is above (behind) the occluder's baseline
 * is drawn behind it. This is what makes "sit on the sofa" hide the legs and
 * "stand in front of the sofa" hide the sofa instead. */
typedef struct {
    int16_t x, y, w, h; /* screen space, already projected */
    int16_t baseline_y; /* world y of the occluder's floor contact, screen space */
    lw_stage_layer_t layer;
} lw_stage_occluder_t;

typedef struct {
    bool occluded;      /* the actor must be drawn behind this occluder */
    bool overlaps;      /* the rectangles touch at all */
    int16_t top;        /* first screen row that must be clipped, when occluded */
    int16_t bottom;     /* last screen row (exclusive) */
} lw_stage_occlusion_t;

/* Does the actor's ground point sit behind the occluder's baseline? Strictly
 * behind: standing exactly on the baseline means standing in front of it,
 * which is what a user expects when they walk up to the sofa. */
bool lw_stage_behind(const lw_stage_occluder_t *occluder, const lw_stage_placement_t *placement,
                     uint16_t native_w, uint16_t native_h);

/* Combine one occluder with one actor. The caller clips the actor's draw area to
 * [top, bottom) when occluded. Multiple occluders are resolved by calling this
 * once per occluder and keeping the largest occluded span, which is enough for
 * a 2D room where props do not overlap each other. */
lw_stage_occlusion_t lw_stage_occlude(const lw_stage_occluder_t *occluder,
                                      const lw_stage_placement_t *placement, uint16_t native_w,
                                      uint16_t native_h);

/* --- dog house ----------------------------------------------------------
 * The dog house has four authored states. The engine only needs to know which
 * band of the strip to show and whether the pet is visible at all. */
typedef enum {
    LW_STAGE_HOUSE_EMPTY = 0,
    LW_STAGE_HOUSE_SLEEPING = 1,
    LW_STAGE_HOUSE_LOOKING = 2,
    LW_STAGE_HOUSE_STIRRING = 3,
    LW_STAGE_HOUSE_STATE_COUNT
} lw_stage_house_state_t;

/* Source band of one house state inside a vertical strip of equal bands. */
bool lw_stage_house_rect(uint16_t strip_w, uint16_t strip_h, lw_stage_house_state_t state,
                         int16_t *y, uint16_t *h);
/* Whether the pet sprite should be drawn for this state (only the sleeping and
 * stirring states put the pet inside the house). */
bool lw_stage_house_shows_pet(lw_stage_house_state_t state);
/* Clip the pet to the house opening: the pet is only visible through the door,
 * so the caller clips to this rectangle while the pet is inside. */
bool lw_stage_house_opening(const lw_stage_occluder_t *house, int16_t inset, int16_t *x,
                            int16_t *y, uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif
