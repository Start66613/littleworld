/* Little World - app descriptors and the shell (status bar, content, dock).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lw_app {
    const char *id;
    const char *name;
    /** Build the page inside @p content. @p self is the app being opened. */
    void (*create)(lv_obj_t *content, const struct lw_app *self);
    /** Release timers/allocations created by create(). LVGL objects are deleted by the shell. */
    void (*destroy)(void);
} lw_app_t;

void lw_shell_start(void);

/** Switch the content area to the app with @p id (no-op when already open). */
void lw_shell_open(const char *id);

void lw_shell_home(void);

/** Re-apply the palette to the whole shell and rebuild the current page. */
void lw_shell_refresh_theme(void);

/** Container that pages are built into (used by widgets that need coordinates). */
lv_obj_t *lw_shell_content(void);

const lw_app_t *lw_shell_current(void);

#ifdef __cplusplus
}
#endif
