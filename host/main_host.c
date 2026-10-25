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

int main(int argc, char **argv)
{
    const char *auto_dir = NULL;
    const char *v2_dir = NULL;
    const char *pixel_pack = NULL,*pixel_output = NULL;
    const char *os_pack=NULL,*os_output=NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0 && i + 1 < argc) {
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
            fprintf(stderr,"Usage: LittleWorld.exe [--auto DIR | --ui-v2-test DIR]\n"); return 2;
        }
    }

    if ((auto_dir && v2_dir) || (pixel_pack && (auto_dir || v2_dir)) || (os_pack && (pixel_pack || auto_dir || v2_dir))) return 2;
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
    if (os_pack) return lw_os_host_run(os_pack,os_output);
    if (pixel_pack) return lw_pixel_duo_run(pixel_pack,pixel_output);

    if (v2_dir) return lw_ui_v2_test(v2_dir);

    if (auto_dir != NULL) {
        printf("auto mode, screenshots -> %s\n", auto_dir);
        return lw_auto_run(auto_dir);
    }

    printf("Little World desktop simulator ready (%dx%d). Esc quits.\n", LW_HOST_WIDTH, LW_HOST_HEIGHT);
    printf("save directory: %s\n", host_platform_dir());
    fflush(stdout);
    return lw_host_run_window();
}
