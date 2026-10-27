#include "lw_ambience.h"
#include "lw_os_design.h"
#include "lw_app_state.h"
#include "lw_ui_clock.h"
#include "lw/lw_platform.h"
#include <stdint.h>

/* Day phase for the scenery.
 *
 * The effective clock (lw_day, reached through lw_ui_clock) decides the phase;
 * this file only translates it for the rendering code and owns the session
 * preview. Before the merge there were three answers to "what time is it" -
 * the platform hook, the DEMO clock and a compile-time constant - which is how
 * the status bar could read 19:30 while the room was painted for noon. The
 * platform hook is kept ONLY as the fallback for the Windows desktop build,
 * where nothing attaches a clock. */
static int preview = -1;          /* minute of day, -1 = follow the clock */
static bool valid;                /* last localtime() result, for callers */

bool lw_ambience_localtime(struct tm *out)
{
    const lw_platform_t *p = lw_platform_get();
    valid = out && p && p->get_time && p->get_time(out) && out->tm_year >= 120;
    return valid;
}

bool lw_ambience_preview(int minute)
{
    if (minute < -1 || minute >= 1440) return false;
    preview = minute;
    return true;
}

bool lw_ambience_previewing(void) { return preview >= 0; }

unsigned lw_ambience_minute(void)
{
    if (preview >= 0) return (unsigned)preview;
    /* The attached clock outranks the platform hook: on the device the hook is
     * fed BY that clock, so reading the hook directly would bypass the trust
     * state and turn a demo clock into an apparently real one. */
    lw_ui_clock_t clock;
    if (lw_ui_clock_read(&clock)) return clock.hour * 60u + clock.minute;
    struct tm t;
    if (lw_ambience_localtime(&t)) return (unsigned)(t.tm_hour * 60 + t.tm_min);
    return (unsigned)(lw_now() % 86400u / 60u);
}

bool lw_ambience_clock_valid(void)
{
    lw_ui_clock_t clock;
    if (lw_ui_clock_read(&clock)) return clock.source == LW_DAY_SOURCE_REAL && clock.trusted;
    struct tm t;
    return lw_ambience_localtime(&t);
}

/* Same five bands on the same boundaries as lw_day_phase_at(), because the two
 * must agree about when evening starts. */
lw_day_phase_t lw_ambience_phase(void)
{
    unsigned m = lw_ambience_minute();
    return m < 300 || m >= 1380 ? LW_DAY_PHASE_LATE_NIGHT
         : m < 540             ? LW_DAY_PHASE_DAWN
         : m < 1020            ? LW_DAY_PHASE_DAY
         : m < 1140            ? LW_DAY_PHASE_DUSK
                               : LW_DAY_PHASE_NIGHT;
}

unsigned lw_ambience_wallpaper(void)
{
    lw_day_phase_t p = lw_ambience_phase();
    return p <= LW_DAY_PHASE_DAY ? 0 : p == LW_DAY_PHASE_DUSK ? 2 : 1;
}

void lw_ambience_refresh(lv_obj_t *o)
{
    if (!o) return;
    /* Indexed by lw_day_phase_t in enum order. A value outside the table must
     * not read past it; lw_day only ever produces 0..4. */
    static const uint32_t color[] = {0xffdeb8, 0xfff7de, 0xffb98b, 0x34385b, 0x242a4d};
    static const uint8_t alpha[] = {12, 0, 8, 0, 25};
    lw_day_phase_t p = lw_ambience_phase();
    if ((unsigned)p >= sizeof(color) / sizeof(color[0])) p = LW_DAY_PHASE_DAY;
    lv_obj_set_style_bg_color(o, lv_color_hex(color[p]), 0);
    lv_obj_set_style_bg_opa(o, alpha[p], 0);
    lv_obj_invalidate(o);
}

lv_obj_t *lw_ambience_layer(lv_obj_t *parent, unsigned room, int y)
{
    (void)room;
    lv_obj_t *o = lw_os_box(parent, 0, y, 1024, 600, 0, 0, 0);
    lw_ambience_refresh(o);
    return o;
}
