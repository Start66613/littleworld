/* Little World - Windows host entry point.
 *
 * Two modes:
 *   LittleWorld.exe                 interactive 1024x600 window (mouse = touch)
 *   LittleWorld.exe --auto <dir>    scripted regression run, writes screenshots, exits
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lvgl.h"
#include "lw_app_state.h"
#include "lw_ui.h"
#include "win_display.h"

void host_platform_init(void);
const char *host_platform_dir(void);
int lw_auto_run(const char *output_dir);
int lw_ui_v2_test(const char *output_dir);
int lw_pixel_duo_run(const char *pack_path,const char *output_dir);
int lw_os_host_run(const char *pack_path,const char *output_dir);

/* Opening a window is opt-in for scripts, implicit for a human.
 *
 * This binary used to open the 1024x600 simulator window whenever it was
 * started without a test/auto argument. Scripted runs that lost an argument
 * therefore launched a visible window that never exited, and because the
 * window stopped answering messages once the UI wedged, it could not even be
 * closed with the X button - the only way out was rebooting the machine.
 *
 * So the window is now explicit (--window) for anything automated, but a bare
 * double-click still works: when no arguments were given AND stdout is a real
 * console, the window is what the person in front of the machine asked for.
 * A redirected stdout is the signature of a script, and scripts stay headless.
 * win_display.c additionally pumps LVGL on its own thread and kills the window
 * after LW_HOST_WINDOW_MAX_SECONDS, so a wedged UI can always be closed again.
 */
static bool console_is_interactive(void)
{
    DWORD mode = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == NULL || out == INVALID_HANDLE_VALUE) return false;
    return GetConsoleMode(out, &mode) != 0;
}

static int refuse_window(void)
{
    printf("headless build: no window requested.\n");
    printf("  screenshots : --os-v2-test PACK DIR   (or --auto DIR / --ui-v2-test DIR)\n");
    printf("  simulator   : --window                (1024x600 window, Esc to quit)\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *auto_dir = NULL;
    const char *v2_dir = NULL;
    const char *pixel_pack = NULL,*pixel_output = NULL;
    const char *os_pack=NULL,*os_output=NULL;
    bool legacy=false, want_window=false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--legacy") == 0) { legacy=true;
        } else if (strcmp(argv[i], "--window") == 0) {
            want_window=true;
        } else if (strcmp(argv[i], "--auto") == 0 && i + 1 < argc) {
            auto_dir = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--ui-v2-test") == 0 && i + 1 < argc) {
            v2_dir=argv[++i];
        } else if (strcmp(argv[i],"--pixel-duo")==0 && i+1<argc) {
            pixel_pack=argv[++i];
        } else if (strcmp(argv[i],"--pixel-duo-test")==0 && i+2<argc) {
            pixel_pack=argv[++i]; pixel_output=argv[++i];
        } else if (strcmp(argv[i],"--os-v2")==0 && i+1<argc) {
            os_pack=argv[++i];
        } else if (strcmp(argv[i],"--os-v2-test")==0 && i+2<argc) {
            os_pack=argv[++i]; os_output=argv[++i];
        } else {
            fprintf(stderr,"Usage: LittleWorld.exe [--legacy | --os-v2 PACK | --os-v2-test PACK DIR | --auto DIR | --ui-v2-test DIR]\n"); return 2;
        }
    }

    if ((auto_dir && v2_dir) || (pixel_pack && (auto_dir || v2_dir)) || (os_pack && (pixel_pack || auto_dir || v2_dir)) || (legacy && (os_pack || pixel_pack || auto_dir || v2_dir))) return 2;

    /* A double-click (no arguments) or "LittleWorld.exe --legacy" from a real
     * console is a person asking for the simulator; anything with a redirected
     * stdout is a script and stays headless. */
    if (!want_window && console_is_interactive() && (argc == 1 || (legacy && argc == 2))) want_window = true;
    if (auto_dir || v2_dir || pixel_output || os_output) {
        char isolated[512];
        const char *directory=auto_dir ? auto_dir : v2_dir ? v2_dir : pixel_output ? pixel_output : os_output;
        if (snprintf(isolated,sizeof(isolated),"%s\\.state",directory)>=(int)sizeof(isolated)) return 2;
        if (_putenv_s("LW_DATA_DIR",isolated)!=0) return 2;
    }
    host_platform_init();

    if (auto_dir != NULL || v2_dir != NULL || pixel_output || os_output) {
        lw_set_demo_clock(true); /* reproducible screenshots */
        /* deterministic start: wipe the save so screenshots are reproducible */
        lw_app_state_init();
        lw_app_state_reset();
        lw_set_demo_clock(true);
    }

    lw_host_display_init();
    lw_ui_start();
    /* These two entry points open the window themselves when the output
     * directory is NULL, so the opt-in check has to happen here as well. */
    if (os_pack && !os_output && !want_window) return refuse_window();
    if (pixel_pack && !pixel_output && !want_window) return refuse_window();
    if (os_pack) return lw_os_host_run(os_pack,os_output);
    if (pixel_pack) return lw_pixel_duo_run(pixel_pack,pixel_output);

    if (v2_dir) return lw_ui_v2_test(v2_dir);

    if (auto_dir != NULL) {
        printf("auto mode, screenshots -> %s\n", auto_dir);
        return lw_auto_run(auto_dir);
    }

    if (!legacy) {
        os_pack=getenv("LW_PIXEL_PACK");
        if (!os_pack || !*os_pack) os_pack="private/pixel-duo/qianqian/qianqian.lwpx";
        if (!want_window) return refuse_window();
        return lw_os_host_run(os_pack,NULL);
    }

    if (!want_window) return refuse_window();
    printf("Little World desktop simulator ready (%dx%d). Esc quits.\n", LW_HOST_WIDTH, LW_HOST_HEIGHT);
    printf("save directory: %s\n", host_platform_dir());
    fflush(stdout);
    return lw_host_run_window();
}
