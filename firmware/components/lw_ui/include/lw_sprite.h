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
uint16_t lw_sprite_frame(const lw_sprite_t *sprite);
bool lw_sprite_finished(const lw_sprite_t *sprite);
void lw_sprite_destroy(lw_sprite_t *sprite);
