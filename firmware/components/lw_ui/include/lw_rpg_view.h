/* Host-owned rendering of the frozen DSH RPG core. SPDX-License-Identifier: MIT */
#pragma once
#include "lvgl.h"
#include "lw_rpg.h"
typedef struct lw_rpg_view lw_rpg_view_t;
typedef struct {
    void *user;
    bool (*load)(void *,uint8_t [LW_RPG_SAVE_BYTES]);
    bool (*save)(void *,const uint8_t [LW_RPG_SAVE_BYTES]);
    void (*reward)(void *,uint32_t);
} lw_rpg_view_host_t;
/* Parent owns objects. LV_EVENT_DELETE releases the timer and saves committed progress.
 * Only the pinned demo world is accepted; arbitrary downloaded worlds are unsupported. */
lw_rpg_view_t *lw_rpg_view_create(lv_obj_t *parent,const lw_rpg_view_host_t *host,unsigned theme);
void lw_rpg_view_suspend(lw_rpg_view_t *,bool);
const lw_rpg_state_t *lw_rpg_view_state(const lw_rpg_view_t *);
