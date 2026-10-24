/* Little World - Windows host entry point.
 *
 * Two modes:
 *   LittleWorld.exe                 interactive 1024x600 window (mouse = touch)
 *   LittleWorld.exe --auto <dir>    scripted regression run, writes screenshots, exits
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "lw_app_state.h"
#include "lw_ui.h"
#include "win_display.h"

void host_platform_init(void);
const char *host_platform_dir(void);
int lw_auto_run(const char *output_dir);

int main(int argc, char **argv)
{
    host_platform_init();

    const char *auto_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0 && i + 1 < argc) {
            auto_dir = argv[i + 1];
            i++;
        }
    }

    if (auto_dir != NULL) {
        lw_set_demo_clock(true); /* reproducible screenshots */
        /* deterministic start: wipe the save so screenshots are reproducible */
        lw_app_state_init();
        lw_app_state_reset();
        lw_set_demo_clock(true);
    }

    lw_host_display_init();
    lw_ui_start();

    if (auto_dir != NULL) {
        printf("auto mode, screenshots -> %s\n", auto_dir);
        return lw_auto_run(auto_dir);
    }

    printf("Little World desktop simulator ready (%dx%d). Esc quits.\n", LW_HOST_WIDTH, LW_HOST_HEIGHT);
    printf("save directory: %s\n", host_platform_dir());
    fflush(stdout);
    return lw_host_run_window();
}
