/* Little World - app registry. Adding an app is one entry here plus its module.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lw_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lw_app_t lw_app_home_def;
extern const lw_app_t lw_app_pet_def;
extern const lw_app_t lw_app_album_def;
extern const lw_app_t lw_app_music_def;
extern const lw_app_t lw_app_memories_def;
extern const lw_app_t lw_app_arcade_def;
extern const lw_app_t lw_app_settings_def;

int lw_app_count(void);
int lw_dock_count(void);
const lw_app_t *lw_dock_at(int index);
const lw_app_t *lw_app_at(int index);
const lw_app_t *lw_app_find(const char *id);

#ifdef __cplusplus
}
#endif
