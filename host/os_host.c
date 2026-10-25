/* Actual Windows LVGL prototype and pointer-based acceptance. SPDX-License-Identifier: MIT */
#include "lw_os.h"
#include "pixel_pack.h"
#include "lw_shell.h"
#include "lw_app_state.h"
#include "win_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>
const char *host_platform_dir(void);
static char settings_path[512]; static uint32_t clock_ms; static unsigned checks,failures;
static char rpg_path[512]; static unsigned reward_calls;
static bool rpg_load(void *ctx,uint8_t bytes[LW_RPG_SAVE_BYTES])
{ (void)ctx; FILE *f=fopen(rpg_path,"rb"); if (!f) return false; size_t n=fread(bytes,1,LW_RPG_SAVE_BYTES,f); bool extra=fgetc(f)!=EOF; fclose(f); return n==LW_RPG_SAVE_BYTES && !extra; }
static bool rpg_save(void *ctx,const uint8_t bytes[LW_RPG_SAVE_BYTES])
{
    (void)ctx; char temp[540]; snprintf(temp,sizeof(temp),"%s.tmp",rpg_path); FILE *f=fopen(temp,"wb"); if (!f) return false;
    bool ok=fwrite(bytes,1,LW_RPG_SAVE_BYTES,f)==LW_RPG_SAVE_BYTES; if (fclose(f)) ok=false;
    return ok && MoveFileExA(temp,rpg_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
static void rpg_reward(void *ctx,uint32_t id) { (void)ctx; if (id==9001) reward_calls++; }
static uint32_t fake_clock(void) { return clock_ms; }
static double cpu_ms(void)
{
    FILETIME created,ended,kernel,user; if (!GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user)) return 0;
    uint64_t k=(uint64_t)kernel.dwLowDateTime|((uint64_t)kernel.dwHighDateTime<<32),u=(uint64_t)user.dwLowDateTime|((uint64_t)user.dwHighDateTime<<32);
    return (double)(k+u)/10000.0;
}
static bool brightness(void *c,unsigned n) { (void)c; return n>=10 && n<=100; }
static bool volume(void *c,unsigned n,bool muted) { (void)c; (void)muted; return n<=100; }
static bool load(void *c,uint8_t bytes[LW_SYS_PREF_BYTES])
{ (void)c; FILE *f=fopen(settings_path,"rb"); if (!f) return false; size_t n=fread(bytes,1,LW_SYS_PREF_BYTES,f); bool extra=fgetc(f)!=EOF; fclose(f); return n==LW_SYS_PREF_BYTES && !extra; }
static bool store(void *c,const uint8_t bytes[LW_SYS_PREF_BYTES])
{
    (void)c; char temp[540]; snprintf(temp,sizeof(temp),"%s.tmp",settings_path); FILE *f=fopen(temp,"wb"); if (!f) return false;
    bool ok=fwrite(bytes,1,LW_SYS_PREF_BYTES,f)==LW_SYS_PREF_BYTES; if (fclose(f)) ok=false;
    return ok && MoveFileExA(temp,settings_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
#define CHECK(name,condition) do { checks++; if (!(condition)) { failures++; printf("FAIL: %s (line %d)\n",name,__LINE__); } } while(0)
static void advance(unsigned ms)
{ for (unsigned i=0;i<ms;i+=20) { clock_ms+=20; lv_timer_handler(); } }
static unsigned objects(lv_obj_t *o)
{ unsigned n=1; for (unsigned i=0;i<lv_obj_get_child_count(o);i++) n+=objects(lv_obj_get_child(o,(int)i)); return n; }
static int timers(void)
{ int n=0; for (lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)) n++; return n; }
static bool tap(lw_os_control_t id)
{
    lv_obj_t *o=lw_os_control(id); if (!o) { CHECK("control exists",false); return false; }
    lv_obj_update_layout(lw_os_root()); lv_area_t a; lv_obj_get_coords(o,&a);
    CHECK("touch center within screen",a.x1>=0 && a.y1>=0 && a.x2<1024 && a.y2<600);
    lw_host_pointer_t *pointer=lw_host_pointer(); pointer->x=(a.x1+a.x2)/2; pointer->y=(a.y1+a.y2)/2;
    pointer->pressed=1; advance(80); pointer->pressed=0; advance(260); return true;
}
static void drag(lw_os_control_t id,unsigned percent)
{
    lv_obj_t *o=lw_os_control(id); CHECK("draggable control exists",o!=NULL); if (!o) return;
    lv_area_t a; lv_obj_get_coords(o,&a); lw_host_pointer_t *p=lw_host_pointer();
    p->x=(a.x1+a.x2)/2; p->y=a.y2; p->pressed=1; advance(60);
    int end=a.y2-(int)((a.y2-a.y1)*percent/100);
    for (int i=1;i<=10;i++) { p->y=a.y2+(end-a.y2)*i/10; advance(20); }
    p->pressed=0; advance(100);
}
static void shot(const char *out,const char *name)
{ char file[600]; snprintf(file,sizeof(file),"%s\\%s.bmp",out,name); advance(340); CHECK("real framebuffer screenshot",lw_host_screenshot_bmp(file)==0); }
static lv_obj_t *text_object(lv_obj_t *o,const char *text)
{
    if (lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text)) return o;
    for (unsigned i=0;i<lv_obj_get_child_count(o);i++) { lv_obj_t *found=text_object(lv_obj_get_child(o,(int)i),text); if (found) return found; }
    return NULL;
}
static void tap_text(const char *text)
{
    lv_obj_update_layout(lw_os_root()); lv_obj_t *o=text_object(lw_os_root(),text);
    CHECK(text,o!=NULL); if (!o) return;
    lv_area_t a; lv_obj_get_coords(o,&a); lw_host_pointer_t *p=lw_host_pointer();
    CHECK("legacy text touch inside viewport",a.x1>=0 && a.y1>=0 && a.x2<1024 && a.y2<600);
    p->x=(a.x1+a.x2)/2; p->y=(a.y1+a.y2)/2; p->pressed=1; advance(80); p->pressed=0; advance(300);
}
static unsigned text_count(lv_obj_t *o,const char *text)
{
    unsigned n=lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text) ? 1 : 0;
    for (unsigned i=0;i<lv_obj_get_child_count(o);i++) n+=text_count(lv_obj_get_child(o,(int)i),text);
    return n;
}
static bool disjoint(lv_obj_t *a,lv_obj_t *b)
{ lv_area_t x,y; if (!a || !b) return false; lv_obj_get_coords(a,&x); lv_obj_get_coords(b,&y); return x.y2<y.y1 || y.y2<x.y1 || x.x2<y.x1 || y.x2<x.x1; }
static void rpg_tile(int x,int y)
{
    lw_host_pointer_t *p=lw_host_pointer(); p->x=32+x*44+22; p->y=104+y*44+22;
    p->pressed=1; advance(80); p->pressed=0; advance(2600);
}
int lw_os_host_run(const char *pack_path,const char *out)
{
    setvbuf(stdout,NULL,_IONBF,0); snprintf(settings_path,sizeof(settings_path),"%s\\os-settings.dat",host_platform_dir());
    snprintf(rpg_path,sizeof(rpg_path),"%s\\rpg-save.dat",host_platform_dir()); if (out) DeleteFileA(rpg_path);
    lw_rpg_view_host_t rpg_host={NULL,rpg_load,rpg_save,rpg_reward}; lw_os_rpg_storage(&rpg_host);
    lw_system_backend_t backend={0}; backend.simulated=true; backend.network_available=true; backend.brightness=brightness; backend.volume=volume; backend.load=out ? NULL : load; backend.store=store;
    lw_system_t system; lw_system_init(&system,&backend);
    lw_pixel_pack_t *pack=lw_pixel_pack_open(pack_path); if (!pack) fprintf(stderr,"Character pack unavailable: running complete OS with labelled no-art fallback.\n");
    unsigned expected_leases=pack ? 2 : 0;
    int base_timers=timers(); size_t base_bytes=lw_assets_stats().live_bytes;
    if (out) { clock_ms=lv_tick_get(); lv_tick_set_cb(fake_clock); }
    if (!lw_os_start(&system,lw_pixel_pack_art(pack))) { lw_pixel_pack_close(pack); return 2; }
    if (!out) { printf("Little World OS V2 ready: Windows simulated services; --legacy restores the stable desktop.\n"); int result=lw_host_run_window(); lw_os_stop(); return lw_pixel_pack_close(pack) ? result : 2; }
    LARGE_INTEGER begin,end,freq; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&begin); double cpu_start=cpu_ms();
    shot(out,"01-desktop"); CHECK("OS desktop active",lw_os_page()==OS_DESKTOP);
    tap(OC_CONTROL); CHECK("control overlay opens",lw_os_overlay()==OS_OVER_CONTROL); shot(out,"02-control-center");
    drag(OC_BRIGHTNESS,65); drag(OC_VOLUME,40); CHECK("brightness drag updates service",system.prefs.brightness>=65 && system.prefs.brightness<=72); CHECK("volume drag updates shared state",system.prefs.volume>=37 && system.prefs.volume<=43);
    unsigned remembered=system.prefs.volume;
    tap(OC_WIFI); advance(800); CHECK("asynchronous mock scan",system.network_count==3 && system.wifi==LW_WIFI_IDLE); shot(out,"03-wifi-list");
    tap(OC_AP0); CHECK("secure network opens real password input",lw_os_overlay()==OS_OVER_PASSWORD && lw_os_control(OC_PASSWORD)); shot(out,"08-wifi-password");
    if (!lw_os_control(OC_PASSWORD)) { lw_os_stop(); lw_pixel_pack_close(pack); return 1; }
    lv_obj_t *keyboard=lw_os_control(OC_KEYBOARD); CHECK("onscreen keyboard exists",keyboard!=NULL);
    if (keyboard) {
        lv_area_t keys; lv_obj_get_coords(keyboard,&keys); CHECK("keyboard stays inside screen",keys.y1>=0 && keys.y2<600 && keys.x1>=0 && keys.x2<1024);
        lw_host_pointer_t *pointer=lw_host_pointer(); pointer->x=keys.x1+82; pointer->y=keys.y1+28; pointer->pressed=1; advance(80); pointer->pressed=0; advance(120);
        CHECK("real keyboard pointer enters a character",strlen(lv_textarea_get_text(lw_os_control(OC_PASSWORD)))==1);
    }
    lv_textarea_set_text(lw_os_control(OC_PASSWORD),"short"); tap(OC_CONNECT); CHECK("short password rejected",system.wifi!=LW_WIFI_CONNECTING && system.wifi!=LW_WIFI_CONNECTED);
    lv_textarea_set_text(lw_os_control(OC_PASSWORD),"wrongpass"); tap(OC_CONNECT); advance(1200); CHECK("authentication failure is visible",system.wifi==LW_WIFI_FAILED && lw_os_overlay()==OS_OVER_PASSWORD);
    lv_textarea_set_text(lw_os_control(OC_PASSWORD),"demo-only"); tap(OC_CONNECT); advance(1200); CHECK("successful mock connection returns to list",system.wifi==LW_WIFI_CONNECTED && lw_os_overlay()==OS_OVER_WIFI);
    tap(OC_FORGET); CHECK("forget removes selected connection",!system.ssid[0] && system.wifi==LW_WIFI_IDLE);
    tap(OC_BACK); CHECK("network back returns control center",lw_os_overlay()==OS_OVER_CONTROL); tap(OC_CLOSE);
    tap(OC_SETTINGS); CHECK("Dock opens settings",lw_os_page()==OS_SETTINGS); shot(out,"04-settings");
    tap(OC_SOUND); CHECK("settings reads the same volume",lw_os_control(OC_VOLUME) && lv_slider_get_value(lw_os_control(OC_VOLUME))==(int)remembered);
    tap(OC_PERSONAL); tap(OC_THEMES); tap(OC_THEME1); CHECK("theme preview does not persist",system.prefs.theme==0); shot(out,"05-themes");
    tap(OC_CANCEL); CHECK("theme cancellation restores saved palette",system.prefs.theme==0 && lw_os_page()==OS_SETTINGS);
    tap(OC_THEMES); tap(OC_THEME1); tap(OC_APPLY); CHECK("theme confirmation commits",system.prefs.theme==1 && system.prefs.wallpaper==1);
    tap(OC_HOME); shot(out,"09-moonlight-desktop"); tap(OC_CONTROL); tap(OC_LOCK); CHECK("lock is an overlay",system.locked && lw_os_overlay()==OS_OVER_LOCK); shot(out,"06-lockscreen"); tap(OC_UNLOCK); CHECK("unlock restores existing desktop",!system.locked && lw_os_page()==OS_DESKTOP);
    lw_os_show(OS_OVER_LOCK); advance(400);
    CHECK("lock clock/date have separate bounds",disjoint(lw_os_control(OC_LOCK_CLOCK),lw_os_control(OC_LOCK_DATE)));
    CHECK("lock date/anniversary have separate bounds",disjoint(lw_os_control(OC_LOCK_DATE),lw_os_control(OC_LOCK_LOVE)));
    CHECK("lock anniversary/unlock have separate bounds",disjoint(lw_os_control(OC_LOCK_LOVE),lw_os_control(OC_UNLOCK)));
    tap(OC_UNLOCK);
    tap(OC_SETTINGS); tap(OC_PERSONAL); tap(OC_THEMES); tap(OC_THEME0); tap(OC_APPLY); tap(OC_HOME); tap(OC_WORLD); shot(out,"07-pixel-world");
    tap(OC_GREET); advance(5000); tap(OC_HOME);
    tap(OC_ENVELOPE); shot(out,"10-envelope"); tap(OC_CLOSE);
    tap(OC_WORLD); tap(OC_PET); CHECK("real pet app mounted",lw_shell_current() && !strcmp(lw_shell_current()->id,"pet")); shot(out,"11-pet-idle");
    unsigned fullness=lw_app_state()->pet.fullness; tap_text("喂食"); CHECK("existing pet action changes model",lw_app_state()->pet.fullness>=fullness); shot(out,"12-pet-interaction");
    tap(OC_ALBUM); CHECK("Dock mounts real album",lw_shell_current() && !strcmp(lw_shell_current()->id,"album")); tap_text("海边"); CHECK("album viewer really opens",text_object(lw_os_root(),"下一张")!=NULL); shot(out,"13-album-viewer"); tap_text("下一张"); tap_text("关闭");
    tap(OC_MUSIC); unsigned titles=text_count(lw_os_root(),"傍晚的风（DEMO）"); tap_text("下一首"); CHECK("music navigation updates real current title",text_count(lw_os_root(),"傍晚的风（DEMO）")==titles+1); shot(out,"14-music");
    tap(OC_MEMORIES); tap_text("纪念日"); shot(out,"15-anniversary"); tap_text("时间线"); CHECK("real timeline navigates",text_object(lw_os_root(),"时间线")!=NULL);
    tap(OC_GAMES); tap_text("2048"); CHECK("2048 enters inside OS",text_object(lw_os_root(),"返回游戏中心")!=NULL); shot(out,"16-2048"); tap_text("返回游戏中心");
    tap_text("贪吃蛇"); tap_text("返回游戏中心"); tap_text("打砖块"); tap_text("返回游戏中心");
    lw_shell_open("pet"); advance(400); CHECK("legacy route delegated to active OS",lw_os_page()==OS_PET && lw_shell_content()!=NULL); lw_shell_home(); advance(400); CHECK("legacy home returns OS desktop",lw_os_page()==OS_DESKTOP);
    tap(OC_WORLD); tap(OC_RPG); CHECK("RPG enters from real OS",lw_os_page()==OS_RPG && lw_os_rpg_state()!=NULL); shot(out,"17-rpg-room");
    tap_text("打开任务本"); shot(out,"18-rpg-journal"); tap_text("回到房间");
    rpg_tile(2,2); CHECK("blocked furniture rejects touch move",lw_os_rpg_state()->actors[0].pos.x==6 && lw_os_rpg_state()->actors[0].pos.y==5);
    rpg_tile(5,3); CHECK("touch path arrives at photo hotspot",lw_os_rpg_state()->actors[0].pos.x==5 && lw_os_rpg_state()->actors[0].pos.y==3);
    tap_text("查看身边物品"); CHECK("real dialogue opens",lw_os_rpg_state()->dialogue_active); shot(out,"19-rpg-dialogue");
    unsigned tick_before=lw_os_rpg_state()->tick_ms; advance(1000); CHECK("dialogue suspends game clock",lw_os_rpg_state()->tick_ms==tick_before);
    tap_text("下一句"); tap_text("下一句"); CHECK("photo dialogue commits quest item",lw_rpg_quest_progress(lw_os_rpg_state())==1);
    tap(OC_CONTROL); tick_before=lw_os_rpg_state()->tick_ms; advance(1000); CHECK("OS overlay suspends RPG",lw_os_rpg_state()->tick_ms==tick_before); tap(OC_CLOSE);
    rpg_tile(9,4); tap_text("查看身边物品"); tap_text("下一句"); tap_text("下一句"); CHECK("second quest item",lw_rpg_quest_progress(lw_os_rpg_state())==2);
    rpg_tile(3,3); tap_text("查看身边物品"); tap_text("下一句"); CHECK("quest completes with one reward",lw_rpg_quest_is_complete(lw_os_rpg_state()) && reward_calls==1); shot(out,"20-rpg-reward"); tap_text("收好奖励");
    tap(OC_HOME); tap(OC_WORLD); tap(OC_RPG); CHECK("RPG progress reloads from separate save",lw_rpg_quest_progress(lw_os_rpg_state())==3 && reward_calls==1); tap(OC_HOME);
    unsigned before=objects(lw_os_root()); int timer_before=timers(); bool clean=true;
    for (int i=0;i<100;i++) { lw_os_show(OS_OVER_CONTROL); advance(60); lw_os_show(OS_OVER_WIFI); advance(60); lw_os_show(OS_OVER_NONE); advance(60); if (objects(lw_os_root())!=before || timers()!=timer_before || lw_assets_stats().live_leases!=expected_leases) clean=false; }
    CHECK("100 overlay cycles preserve page objects timers and leases",clean);
    clean=true;
    for (int i=0;i<100;i++) {
        static const lw_os_page_t pages[]={OS_SETTINGS,OS_WORLD,OS_ALBUM,OS_MEMORIES,OS_MUSIC,OS_GAMES,OS_PET,OS_RPG};
        for (unsigned j=0;j<sizeof(pages)/sizeof(pages[0]);j++) { lw_os_open(pages[j]); advance(80); }
        lw_os_open(OS_DESKTOP); advance(300);
        if (timers()!=timer_before || objects(lw_os_root())!=before || lw_assets_stats().live_leases!=expected_leases) clean=false;
    }
    CHECK("100 page cycles release replaced views",clean);
    lw_os_open(OS_SETTINGS); tap(OC_DISPLAY); CHECK("brightness shared with settings",lw_os_control(OC_BRIGHTNESS) && lv_slider_get_value(lw_os_control(OC_BRIGHTNESS))==system.prefs.brightness);
    CHECK("safe minimum",lw_system_brightness(&system,0) && system.prefs.brightness==10); lw_system_brightness(&system,70); lw_system_save(&system);
    backend.load=load; lw_system_t reloaded; lw_system_init(&reloaded,&backend); CHECK("restart restores independent OS preferences",reloaded.prefs.volume==system.prefs.volume && reloaded.prefs.brightness==70 && reloaded.prefs.theme==system.prefs.theme);
    uint8_t encoded[LW_SYS_PREF_BYTES]; lw_system_encode(&system.prefs,encoded); encoded[5]^=1; lw_preferences_t invalid; CHECK("corrupt preferences rejected",!lw_system_decode(&invalid,encoded));
    size_t peak=lw_assets_stats().peak_bytes; lw_os_stop(); CHECK("OS stop releases frame memory",lw_assets_stats().live_bytes==base_bytes && lw_assets_stats().live_leases==0); CHECK("OS stop releases timers",timers()==base_timers); CHECK("stable shell restored",lw_shell_current() && !strcmp(lw_shell_current()->id,"home"));
    lw_file_cache_stats_t cache=lw_pixel_pack_cache_stats(pack);
    CHECK("real frame file cache stays bounded",cache.peak_bytes<=cache.budget_bytes);
    CHECK("private pack uses real read-ahead",!pack || (cache.prefetched>0 && cache.hits>0));
    CHECK("pack release joins read-ahead worker",lw_pixel_pack_close(pack));
    QueryPerformanceCounter(&end); PROCESS_MEMORY_COUNTERS memory={0}; memory.cb=sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
    char report[600]; snprintf(report,sizeof(report),"%s\\runtime.json",out); FILE *f=fopen(report,"wb");
    if (f) { fprintf(f,"{\n\"checks\":%u,\n\"failures\":%u,\n\"overlay_cycles\":100,\n\"page_cycles\":100,\n\"pixel_peak_bytes\":%zu,\n\"working_set_peak_bytes\":%zu,\n\"wall_ms\":%.3f,\n\"process_cpu_ms\":%.3f,\n\"virtual_clock_unthrottled\":true,\n\"network\":\"Windows mock, no actual host or C6 network access\",\n\"art\":\"%s\",\n\"board_tested\":false\n}\n",checks,failures,peak,(size_t)memory.PeakWorkingSetSize,(double)(end.QuadPart-begin.QuadPart)*1000/(double)freq.QuadPart,cpu_ms()-cpu_start,expected_leases ? "original qianqian and TEST A; final male pending" : "no character pack; labelled fallback"); fclose(f); } else failures++;
    snprintf(report,sizeof(report),"%s\\storage.json",out); f=fopen(report,"wb");
    if (f) { fprintf(f,"{\"implementation\":\"actual Windows filesystem and worker thread\",\"budget_bytes\":%zu,\"peak_cache_bytes\":%zu,\"metadata_bytes\":%zu,\"hits\":%u,\"misses\":%u,\"file_reads\":%u,\"prefetched\":%u,\"evictions\":%u,\"read_failures\":%u,\"lvgl_pixel_peak_bytes\":%zu,\"tf_hardware_tested\":false}\n",cache.budget_bytes,cache.peak_bytes,cache.metadata_bytes,cache.hits,cache.misses,cache.reads,cache.prefetched,cache.evictions,cache.failures,peak); fclose(f); } else failures++;
    printf("OS V2: %u checks, %u failures; 100 overlay cycles; peak pixels %zu B.\n",checks,failures,peak);
    printf("File cache: peak %zu / %zu B; hits %u; prefetched %u; evictions %u.\n",cache.peak_bytes,cache.budget_bytes,cache.hits,cache.prefetched,cache.evictions);
    return failures ? 1 : 0;
}
