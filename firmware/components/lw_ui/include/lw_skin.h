#pragma once
#include "lvgl.h"
typedef enum {SK_DOCK,SK_TIME,SK_STATUS_GROUP,SK_ACTION,SK_SELECTED,SK_PRESSED,SK_COLLAPSED,SK_CLOSE,SK_BOOKMARK,SK_NOTE,SK_BOOK,SK_CC_PANEL,SK_CC_TILE,SK_CC_SELECTED,SK_CC_PRESSED,SK_SLIDER,SK_KNOB,SK_COUNT} lw_skin_id_t;
/* Atlas uses one shared, reference-counted existing asset lease. Missing art
 * returns NULL and retains native controls. Every widget detaches on delete. */
lv_obj_t *lw_skin_place(lv_obj_t *,lw_skin_id_t,int x,int y);
lv_obj_t *lw_skin_fit(lv_obj_t *,lw_skin_id_t,int x,int y,int w,int h);
lv_obj_t *lw_skin_panel(lv_obj_t *,lw_skin_id_t,int w,int h,int border);
bool lw_skin_button(lv_obj_t *,lw_skin_id_t normal,lw_skin_id_t selected,lw_skin_id_t pressed);
void lw_skin_select(lv_obj_t *,bool selected);
void lw_skin_slider(lv_obj_t *slider);
unsigned lw_skin_references(void);
