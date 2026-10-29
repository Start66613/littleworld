/* Host-owned rendering of the frozen DSH RPG core. SPDX-License-Identifier: MIT */
#pragma once
#include "lvgl.h"
#include "lw_rpg.h"
#include "lw_duo_view.h"
typedef struct lw_rpg_view lw_rpg_view_t;
typedef struct {
    void *user;
    bool (*load)(void *,uint8_t [LW_RPG_SAVE_BYTES]);
    bool (*save)(void *,const uint8_t [LW_RPG_SAVE_BYTES]);
    void (*reward)(void *,uint32_t);
} lw_rpg_view_host_t;
/* Parent owns objects. LV_EVENT_DELETE releases the timer and saves committed progress.
 * Only the pinned demo world is accepted; arbitrary downloaded worlds are unsupported. */
lw_rpg_view_t *lw_rpg_view_create(lv_obj_t *parent,const lw_rpg_view_host_t *host,unsigned theme,const lw_actor_art_t art[2]);
void lw_rpg_view_suspend(lw_rpg_view_t *,bool);
const lw_rpg_state_t *lw_rpg_view_state(const lw_rpg_view_t *);
bool lw_rpg_view_has_art(const lw_rpg_view_t *);
lv_obj_t *lw_rpg_view_actor(const lw_rpg_view_t *,unsigned);
/* Scene projection: fixed core tile coordinates, pixel foot point in map. */
void lw_rpg_view_tile_point(int x,int y,int *px,int *py);
/* Where the RPG page spends its time. A map tap is this page's only interaction,
 * and "walking feels slow" is otherwise just an impression. The counters split a
 * tap into the frozen core's path search, the status label re-layout and the
 * z-order swap, and time the 20 ms refresh() separately so a stall can be
 * attributed. Millisecond resolution (lv_tick_get) is enough: the stalls worth
 * chasing here are hundreds of milliseconds. */
typedef struct {
    unsigned taps, tap_ms, tap_max_ms, core_ms, label_ms;
    unsigned orders, order_ms;
    unsigned frames, refresh_ms, refresh_max_ms, slow_frames;
    /* Split of refresh(): projecting the state, moving the sprite widget, and
     * advancing its frame. They fail for different reasons - the first is
     * arithmetic, the second two touch the asset cache and the draw list. */
    unsigned state_ms, anchor_ms, seek_ms;
} lw_rpg_perf_t;
void lw_rpg_view_perf(lw_rpg_perf_t *out);
void lw_rpg_view_perf_reset(void);
