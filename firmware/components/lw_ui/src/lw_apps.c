/* SPDX-License-Identifier: MIT */
#include "lw_apps.h"

#include <string.h>

/* Every app, including the ones that are only reachable from the desktop
 * (the pet has no dock slot; it lives on the home screen). */
static const lw_app_t *const s_apps[] = {
    &lw_app_home_def,
    &lw_app_pet_def,
    &lw_app_album_def,
    &lw_app_music_def,
    &lw_app_memories_def,
    &lw_app_arcade_def,
    &lw_app_settings_def,
};

/* Order of the dock icons. */
static const int s_dock[] = {0, 2, 3, 4, 5, 6};

int lw_app_count(void)
{
    return (int)(sizeof(s_apps) / sizeof(s_apps[0]));
}

const lw_app_t *lw_app_at(int index)
{
    if (index < 0 || index >= lw_app_count()) {
        return 0;
    }
    return s_apps[index];
}

const lw_app_t *lw_app_find(const char *id)
{
    if (id == 0) {
        return 0;
    }
    for (int i = 0; i < lw_app_count(); i++) {
        if (strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return 0;
}

int lw_dock_count(void)
{
    return (int)(sizeof(s_dock) / sizeof(s_dock[0]));
}

const lw_app_t *lw_dock_at(int index)
{
    if (index < 0 || index >= lw_dock_count()) {
        return 0;
    }
    return s_apps[s_dock[index]];
}
