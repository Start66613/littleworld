/* SPDX-License-Identifier: MIT */
#include "lw_ui.h"

#include "lw_app_state.h"
#include "lw_shell.h"

void lw_ui_start(void)
{
    lw_app_state_init();
    lw_shell_start();
}
