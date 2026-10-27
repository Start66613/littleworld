/* SPDX-License-Identifier: MIT */
#pragma once
#include "lw_assets.h"

typedef struct {
    const char *asset_id;
    const uint16_t *sequence, *duration_ms;
    uint16_t count;
    bool loop;
} lw_clip_t;
typedef struct lw_sprite lw_sprite_t;

/* Owner is the image object: deleting its parent also destroys the player.
 * Clip arrays are borrowed, must live until replaced or the object is deleted. */
lw_sprite_t *lw_sprite_create(lv_obj_t *parent, int anchor_x, int anchor_y, uint16_t scale_256);
lv_obj_t *lw_sprite_object(lw_sprite_t *sprite);
bool lw_sprite_play(lw_sprite_t *sprite, const lw_clip_t *clip);
bool lw_sprite_seek(lw_sprite_t *sprite, uint32_t elapsed_ms);
void lw_sprite_pause(lw_sprite_t *sprite);
void lw_sprite_resume(lw_sprite_t *sprite);
void lw_sprite_set_enabled(lw_sprite_t *sprite, bool enabled);
/* Nearest-neighbour rendering. V4.3 explicitly permits bounded proportion
 * correction (standing 288/256, seated 282/256); source art stays untouched. */
bool lw_sprite_set_pixel_mode(lw_sprite_t *sprite);
void lw_sprite_set_anchor(lw_sprite_t *sprite,int x,int y);
/* External scene clock pauses the player's timer; seek controls both actors. */
void lw_sprite_set_external_clock(lw_sprite_t *sprite,bool external);
uint16_t lw_sprite_frame(const lw_sprite_t *sprite);
bool lw_sprite_finished(const lw_sprite_t *sprite);
void lw_sprite_destroy(lw_sprite_t *sprite);

/* Live sprite count and how many are actually advancing frames right now.
 *
 * "Running" uses the same condition the frame timer uses (clip set, enabled,
 * not paused, not finished), so a panel that shows a still frame because an
 * animation was paused is reported as not running rather than as healthy. Used
 * by the diagnostics; either output pointer may be NULL. */
void lw_sprite_stats(unsigned *live, unsigned *running);
