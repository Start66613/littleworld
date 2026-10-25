/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lw_duo_view.h"

/* Embedded character art (LWFA v1). The blob is produced offline by
 * tools/build_character_pack.py from the approved sprites and linked into the
 * application image, so the firmware has real art without a TF card.
 *
 * All calls belong to the LVGL thread. Registering replaces the registry and
 * therefore needs no outstanding leases. */
typedef struct {
    const uint8_t *data;
    size_t size;
} lw_char_pack_blob_t;

/* Validates the whole blob before touching the registry; false leaves the
 * previous state untouched. */
bool lw_char_pack_register(const lw_char_pack_blob_t *blob);

/* Two actors in a fixed order: [0] male, [1] qianqian. NULL when not ready. */
const lw_actor_art_t *lw_char_pack_art(void);

/* Ids are stable for logs and diagnostics: "male.idle", "qianqian.idle". */
void lw_char_pack_release(void);
