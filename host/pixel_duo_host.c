/* Windows execution proof for real qianqian frames. SPDX-License-Identifier: MIT */
#include "pixel_pack.h"
#include "win_display.h"
#include "lw_app_state.h"
#include "lw_theme.h"
#include <math.h>
#include <stdio.h>
#include <windows.h>
#include <psapi.h>

static uint32_t s_tick;
static uint32_t fake_tick(void) { return s_tick; }
static int s_checks,s_failures;
#define CHECK(name,value) do { s_checks++; if (!(value)) { s_failures++; printf("FAIL: %s (%d)\n",name,__LINE__); } } while(0)
static void advance(unsigned ms)
{ for (unsigned t=0;t<ms;t+=20) { s_tick+=20; lv_timer_handler(); } }
static int timer_count(void)
{ int n=0; for (lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)) n++; return n; }
static unsigned objects(lv_obj_t *o)
{ unsigned n=1; for (unsigned i=0;i<lv_obj_get_child_count(o);i++) n+=objects(lv_obj_get_child(o,(int32_t)i)); return n; }
static void shot(const char *dir,const char *name)
{
    char path[512]; snprintf(path,sizeof(path),"%s\\%s.bmp",dir,name); advance(40);
    CHECK("real screenshot written",lw_host_screenshot_bmp(path)==0);
}
typedef struct { lw_duo_t *model; lw_interaction_t kind; int wake_actor; } button_context_t;
static void command(lv_event_t *event)
{
    button_context_t *c=lv_event_get_user_data(event);
    if (c->wake_actor>=0) lw_duo_sleep(c->model,(unsigned)c->wake_actor,false);
    else (void)lw_duo_request(c->model,c->kind);
}
static lv_obj_t *label(lv_obj_t *parent,const char *value,int x,int y,const lv_font_t *font)
{
    lv_obj_t *o=lv_label_create(parent); lv_label_set_text(o,value); lv_obj_set_pos(o,x,y);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(0x554553),0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
static uint64_t filetime(FILETIME value) { return (uint64_t)value.dwLowDateTime|((uint64_t)value.dwHighDateTime<<32); }
static double cpu_ms(void)
{
    FILETIME creation,exit,kernel,user; if (!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) return 0;
    return (double)(filetime(kernel)+filetime(user))/10000.0;
}
int lw_pixel_duo_run(const char *pack_path,const char *output)
{
    setvbuf(stdout,NULL,_IONBF,0);
    lw_pixel_pack_t *pack=lw_pixel_pack_open(pack_path);
    if (!pack) { fprintf(stderr,"Pixel pack unavailable or invalid; run tools/run_pixel_duo.ps1 to export the pinned source.\n"); return 2; }
    lw_duo_t model; lw_duo_init(&model,0x10725u);
    lw_duo_sleep(&model,1,lw_app_state()->pet.sleeping!=0); /* Read legacy state, never change its format. */
    if (output) { s_tick=lv_tick_get(); lv_tick_set_cb(fake_tick); model.autonomous=false; lw_duo_sleep(&model,1,false); }
    lv_obj_t *previous=lv_screen_active(),*screen=lv_obj_create(NULL);
    lv_obj_remove_style_all(screen); lv_obj_set_size(screen,1024,600);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0xfffbf5),0); lv_obj_set_style_bg_opa(screen,255,0);
    label(screen,"Little World",32,18,&lv_font_montserrat_20);
    int days=lw_days_between(lw_app_state()->anniversary,lw_today())+1;
    char anniversary[64]; snprintf(anniversary,sizeof(anniversary),"%d days together%s",days<1 ? 1 : days,lw_time_is_demo() ? " / DEMO" : "");
    label(screen,anniversary,400,24,&lv_font_montserrat_14);
    label(screen,"PIXEL DUO / P1 preview",760,24,&lv_font_montserrat_14);
    lv_obj_t *viewport=lv_obj_create(screen); lv_obj_remove_style_all(viewport);
    lv_obj_set_pos(viewport,0,64); lv_obj_set_size(viewport,1024,440);
    lv_obj_remove_flag(viewport,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    static const char *const names[]={"Greet","Letter","Window","Celebrate","Rest","Wake A","Wake B"};
    static const lw_interaction_t kinds[]={LW_DUO_GREET,LW_DUO_LETTER,LW_DUO_WINDOW,LW_DUO_CELEBRATE,LW_DUO_REST,LW_DUO_GREET,LW_DUO_GREET};
    button_context_t commands[7];
    for (unsigned i=0;i<7;i++) {
        lv_obj_t *button=lv_obj_create(screen); lv_obj_remove_style_all(button);
        lv_obj_set_pos(button,36+(int)i*138,526); lv_obj_set_size(button,124,48);
        lv_obj_set_style_bg_color(button,lv_color_hex(0xeee1eb),0); lv_obj_set_style_bg_opa(button,255,0); lv_obj_set_style_radius(button,14,0);
        lv_obj_remove_flag(button,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *t=label(button,names[i],0,0,&lv_font_montserrat_14); lv_obj_center(t);
        commands[i]=(button_context_t){&model,kinds[i],i>=5 ? (int)i-5 : -1};
        lv_obj_add_event_cb(button,command,LV_EVENT_CLICKED,&commands[i]);
    }
    lv_screen_load(screen);
    int base_timers=timer_count(); unsigned base_objects=objects(screen);
    lw_duo_view_t *view=lw_duo_view_create(viewport,&model,lw_pixel_pack_art(pack));
    if (!view) { lv_screen_load(previous); lv_obj_delete(screen); lw_pixel_pack_close(pack); return 2; }
    if (!output) {
        printf("Native pixel preview: qianqian + TEST A. No accepted male art. Mouse clicks select each actor.\n");
        int result=lw_host_run_window(); lw_duo_view_destroy(view); lv_screen_load(previous); lv_obj_delete(screen);
        return lw_pixel_pack_close(pack) ? result : 2;
    }
    LARGE_INTEGER begin,end,frequency; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin); double cpu_begin=cpu_ms();
    CHECK("one room timer plus two paused players",timer_count()==base_timers+3);
    CHECK("only two active frame leases",lw_assets_stats().live_leases==2 && lw_assets_stats().live_bytes==393216);
    shot(output,"01-duo-idle");
    float still=model.actors[1].x; (void)lw_duo_move(&model,0,210,424); advance(1000);
    CHECK("A moves independently",model.actors[0].x<300 && model.actors[1].x==still);
    shot(output,"02-independent-A");
    (void)lw_duo_move(&model,1,790,434); advance(800);
    CHECK("real qianqian walk frames loaded",model.actors[1].motion==LW_ACTOR_WALK && lw_assets_stats().loaded_frames>8);
    shot(output,"03-qianqian-walk");
    lw_duo_cancel(&model); CHECK("greet accepted",lw_duo_view_request(view,LW_DUO_GREET));
    for (int i=0;i<600 && model.phase!=LW_DUO_ACT;i++) advance(20);
    CHECK("both meet before synchronized action",model.phase==LW_DUO_ACT && model.actors[0].animation_ms==model.actors[1].animation_ms);
    CHECK("bodies do not overlap",lw_duo_separated(&model)); shot(output,"04-meet-and-wave-fallback"); advance(2200);
    CHECK("completed greeting releases reservation",model.completed==1 && model.phase==LW_DUO_FREE);
    CHECK("letter accepted",lw_duo_view_request(view,LW_DUO_LETTER));
    for (int i=0;i<600 && model.phase!=LW_DUO_ACT;i++) advance(20);
    CHECK("letter semantic action synchronized",model.phase==LW_DUO_ACT && model.interaction==LW_DUO_LETTER);
    shot(output,"05-letter-pose-pending"); advance(3000);
    lw_duo_sleep(&model,1,true); advance(6500);
    CHECK("qianqian sleeps after reaching safe footprint",model.actors[1].sleeping && lw_duo_separated(&model));
    float sx=model.actors[1].x,sy=model.actors[1].y;
    CHECK("sleep blocks joint scheduling",!lw_duo_view_request(view,LW_DUO_GREET)); model.autonomous=true; advance(25000); model.autonomous=false;
    CHECK("autonomy never wakes or moves sleeper",model.actors[1].sleeping && model.actors[1].x==sx && model.actors[1].y==sy);
    shot(output,"06-independent-sleep");
    lw_duo_sleep(&model,1,false); lw_duo_cancel(&model);
    lw_duo_view_select(view,1); shot(output,"07-character-menu");
    lw_duo_view_destroy(view); view=NULL;
    CHECK("view deletion restores timer count",timer_count()==base_timers);
    CHECK("view deletion restores object count",objects(screen)==base_objects);
    CHECK("view deletion releases all pixels",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);
    bool cleanup=true;
    for (int i=0;i<100;i++) {
        view=lw_duo_view_create(viewport,&model,lw_pixel_pack_art(pack));
        if (!view) { cleanup=false; break; }
        advance(160); lw_duo_view_destroy(view); view=NULL;
        if (timer_count()!=base_timers || objects(screen)!=base_objects || lw_assets_stats().live_leases || lw_assets_stats().live_bytes) cleanup=false;
    }
    CHECK("100 scene switches release objects/timers/leases",cleanup);
    view=lw_duo_view_create(viewport,&model,lw_pixel_pack_art(pack));
    advance(80); lw_duo_view_set_animations(view,false); uint32_t reads=lw_assets_stats().loaded_frames; advance(2000);
    CHECK("disabled animations load no further frames",lw_assets_stats().loaded_frames==reads);
    lw_duo_view_set_animations(view,true); advance(400);
    size_t peak=lw_assets_stats().peak_bytes; uint32_t loaded=lw_assets_stats().loaded_frames;
    lv_obj_clean(viewport); view=NULL;
    CHECK("parent deletion also destroys room timer",timer_count()==base_timers && lw_assets_stats().live_leases==0);
    CHECK("pack closes only after all leases return",lw_pixel_pack_close(pack)); pack=NULL;
    QueryPerformanceCounter(&end); double elapsed=(double)(end.QuadPart-begin.QuadPart)*1000.0/(double)frequency.QuadPart;
    double used_cpu=cpu_ms()-cpu_begin;
    PROCESS_MEMORY_COUNTERS memory={0}; memory.cb=sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
    char path[512]; snprintf(path,sizeof(path),"%s\\runtime-results.json",output); FILE *report=fopen(path,"wb");
    if (report) {
        fprintf(report,"{\n \"scope\":\"Windows real LVGL P1; qianqian source art and explicit male fixture; not final art or board validation\",\n \"checks\":%d,\n \"failures\":%d,\n \"scene_switches\":100,\n \"pixel_peak_bytes\":%zu,\n \"loaded_frames\":%u,\n \"live_leases_after_exit\":%u,\n \"wall_ms\":%.3f,\n \"process_cpu_ms\":%.3f,\n \"process_peak_working_set_bytes\":%zu,\n \"unthrottled_test\":true,\n \"missing_joint_poses_are_explicit_fallbacks\":true\n}\n",
                s_checks,s_failures,peak,loaded,lw_assets_stats().live_leases,elapsed,used_cpu,(size_t)memory.PeakWorkingSetSize); fclose(report);
    } else s_failures++;
    lv_screen_load(previous); lv_obj_delete(screen);
    printf("PIXEL DUO: %d checks, %d failures, 100 switches, peak pixels %zu B, wall %.1f ms, CPU %.1f ms.\n",s_checks,s_failures,peak,elapsed,used_cpu);
    return s_failures ? 1 : 0;
}
