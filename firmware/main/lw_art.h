/* Boot-time character art selection for the ESP32-P4 build.
 *
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lw_duo_view.h"

/* Build the actor art table the OS will render.
 *
 * Order of preference, per clip:
 *   1. the TF card resource set (LWPX packs, full V4 animation)
 *   2. the LWFA pack embedded in the app image (always present)
 *
 * The result is never NULL when the embedded pack is intact: the device must
 * show real characters with no card. Returns NULL only when neither source
 * produced a usable clip, which the caller reports as the labelled fallback.
 *
 * The returned table is file-scope and stays valid for the process lifetime. */
const lw_actor_art_t *lw_art_boot(const uint8_t *embedded, size_t embedded_size);

/* Human-readable summary of what actually loaded, for the boot log. */
const char *lw_art_report(void);

/* Detach the registry and close the card cache. Call only after every sprite
 * that borrowed a clip from this table has been destroyed. */
void lw_art_release(void);
