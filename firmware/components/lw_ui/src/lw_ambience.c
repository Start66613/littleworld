#include "lw_ambience.h"
#include "lw_os_design.h"
#include "lw_app_state.h"
#include "lw/lw_platform.h"
#include <stdint.h>
static int preview=-1; static bool valid;
bool lw_ambience_localtime(struct tm *out)
{const lw_platform_t *p=lw_platform_get();valid=out && p && p->get_time && p->get_time(out) && out->tm_year>=120;return valid;}
bool lw_ambience_preview(int minute) {if (minute < -1 || minute>=1440)return false;preview=minute;return true;}
unsigned lw_ambience_minute(void)
{
    if (preview>=0) return (unsigned)preview;
    struct tm t;valid=lw_ambience_localtime(&t);
    return valid ? (unsigned)(t.tm_hour*60+t.tm_min) : (unsigned)(lw_now()%86400/60);
}
bool lw_ambience_clock_valid(void) {struct tm t;return lw_ambience_localtime(&t);}
lw_day_phase_t lw_ambience_phase(void)
{unsigned m=lw_ambience_minute();return m<300 || m>=1380 ? LW_DEEP_NIGHT : m<540 ? LW_DAWN : m<1020 ? LW_DAY : m<1140 ? LW_DUSK : LW_NIGHT;}
unsigned lw_ambience_wallpaper(void) {lw_day_phase_t p=lw_ambience_phase();return p<=LW_DAY ? 0 : p==LW_DUSK ? 2 : 1;}
void lw_ambience_refresh(lv_obj_t *o)
{
    if(!o)return;lw_day_phase_t p=lw_ambience_phase();
    static const uint32_t color[]={0xffdeb8,0xfff7de,0xffb98b,0x34385b,0x242a4d};
    static const uint8_t alpha[]={12,0,8,0,25};
    lv_obj_set_style_bg_color(o,lv_color_hex(color[p]),0);lv_obj_set_style_bg_opa(o,alpha[p],0);lv_obj_invalidate(o);
}
lv_obj_t *lw_ambience_layer(lv_obj_t *parent,unsigned room,int y)
{
    (void)room;lv_obj_t *o=lw_os_box(parent,0,y,1024,600,0,0,0);lw_ambience_refresh(o);return o;
}
