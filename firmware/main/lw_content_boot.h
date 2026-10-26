/* Boot-time content: read littleworld/content.lwcx from the card and hand the
 * memories to the UI.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the content file from the verified resource root and remembers what it
 * found. Safe to call when there is no card: it records LW_CONTENT_MISSING and
 * leaves the built-in demo table in place. Must run after lw_art_boot(), which
 * is what resolves the root. Returns true when content was accepted. */
bool lw_content_boot(void);

/* Applies the loaded content to the live UI/state. Must run after
 * lw_os_start(): the memories page borrows the array, so the source has to be
 * installed while that page does not exist yet, and the anniversary lives in
 * the state that lw_os_start() initialises. */
void lw_content_boot_apply(void);

/* One-line summary for the boot log and the diagnostics. Never NULL. */
const char *lw_content_boot_report(void);

#ifdef __cplusplus
}
#endif
