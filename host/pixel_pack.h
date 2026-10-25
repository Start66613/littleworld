/* SPDX-License-Identifier: MIT */
#pragma once
#include "lw_storage.h"
#include "lw_duo_view.h"
typedef struct lw_pixel_pack lw_pixel_pack_t;
lw_pixel_pack_t *lw_pixel_pack_open(const char *path);
bool lw_pixel_pack_attach_male(lw_pixel_pack_t *pack,const char *path);
bool lw_pixel_pack_attach_qianqian_clips(lw_pixel_pack_t *pack,const char *path);
const lw_actor_art_t *lw_pixel_pack_art(const lw_pixel_pack_t *pack);
bool lw_pixel_pack_close(lw_pixel_pack_t *pack);
lw_file_cache_stats_t lw_pixel_pack_cache_stats(const lw_pixel_pack_t *pack);
