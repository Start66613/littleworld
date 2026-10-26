/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the V4 resource medium and verify its payload.
 *
 * Returns true only when an actual, version-compatible resource set was found
 * and every expected scene header validated. false ALWAYS leaves a status in
 * lw_resource_report() explaining why, and is not an error: the caller is
 * expected to continue into the labelled fallback UI.
 *
 * Idempotent: a second call returns the remembered result without touching the
 * medium again. */
bool lw_resource_mount(void);

/* Whether a medium was successfully joined (independent of payload validity). */
bool lw_resource_medium_mounted(void);

/* Runtime override: behave exactly as if no card were inserted, taking the same
 * branch a real mount failure takes. Used by the boot-loop recovery path, which
 * must rule out the card as a crash cause before retrying the full boot. Set it
 * before the first lw_resource_mount() call; this is not a way to unmount. */
void lw_resource_force_no_medium(bool forced);

/* Root of the always-present embedded fallback clip set. */
const char *lw_resource_embedded_root(void);

#ifdef __cplusplus
}
#endif
