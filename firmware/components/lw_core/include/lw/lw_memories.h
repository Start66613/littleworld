/* Little World - the memories (anniversary) content model.
 *
 * All text below is clearly marked DEMO placeholder content: the real photos,
 * letters and dates will be imported by the user later.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lw/lw_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lw_date_t date;
    const char *title;
    const char *body;
    const char *photo; /* "" when there is no picture yet */
} lw_memory_t;

typedef struct {
    const char *name;
    const char *subtitle;
    const char *description;
} lw_place_t;

int lw_memories_count(void);
const lw_memory_t *lw_memories_get(int index);

int lw_places_count(void);
const lw_place_t *lw_places_get(int index);

const char *lw_memories_welcome(void);
const char *lw_surprise_message(void);
const char *lw_surprise_hint(void);

#ifdef __cplusplus
}
#endif
