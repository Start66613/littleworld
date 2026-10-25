/* Export the actual Little World LVGL wallpaper, not a generated UI concept. */
#include "win_display.h"
#include "lw_os_design.h"
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    lw_host_display_init();
    lw_os_wallpaper(lv_screen_active(),0,0,0,1024,600);
    lv_refr_now(NULL);
    return lw_host_screenshot_bmp(argv[1]);
}
