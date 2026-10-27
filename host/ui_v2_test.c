/* Real LVGL integration tests; diagnostic patterns are NOT production art.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "lw_assets.h"
#include "lw_sprite.h"
#include "lw_scene.h"
#include "lw_world.h"
#include "lw_app_state.h"
#include "lw_theme.h"
#include "win_display.h"

static int s_checks, s_failed;
#define CHECK(name,expr) do { s_checks++; if (!(expr)) { s_failed++; printf("FAIL: %s (line %d)\n",name,__LINE__); } } while(0)
typedef struct { int w,h; bool alpha; int fail_frame; unsigned reads; } fixture_t;
static fixture_t s_pet={32,32,true,-1,0}, s_room={64,48,false,-1,0};
static uint32_t s_tick;
static uint32_t test_tick(void) { return s_tick; }
static bool read_fixture(void *context, uint16_t frame, uint8_t *data, size_t bytes)
{
    fixture_t *f=context; f->reads++;
    if (frame==f->fail_frame || bytes!=(size_t)f->w*f->h*(f->alpha ? 3u : 2u)) return false;
    size_t pixels=(size_t)f->w*f->h;
    for (size_t i=0;i<pixels;i++) {
        unsigned x=(unsigned)(i%f->w), y=(unsigned)(i/f->w);
        uint16_t color=((x/4+y/4)%2) ? (uint16_t)(0xe490+frame*3) : (uint16_t)(0xaf1e+frame*17);
        data[i*2]=(uint8_t)color; data[i*2+1]=(uint8_t)(color>>8);
        if (f->alpha) data[pixels*2+i]=(x<3 || y<3 || x>=(unsigned)f->w-3 || y>=(unsigned)f->h-3) ? 0 :
                                       (x<5 || y<5 ? 128 : 255);
    }
    return true;
}
static const lw_asset_spec_t s_specs[]={
    {"pet.idle",32,32,3,16,28,LV_COLOR_FORMAT_RGB565A8,read_fixture,&s_pet},
    {"room.home",64,48,1,0,0,LV_COLOR_FORMAT_RGB565,read_fixture,&s_room}
};
static const uint16_t s_sequence[]={0,1,2}, s_duration[]={80,140,100};
static const lw_clip_t s_loop={"pet.idle",s_sequence,s_duration,3,true};
static const lw_clip_t s_once={"pet.idle",s_sequence,s_duration,3,false};
static void pump(unsigned ms)
{
    for (unsigned i=0;i<ms;i+=10) { s_tick+=10; lv_timer_handler(); }
}
static int timer_count(void)
{
    int n=0; for (lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)) n++;
    return n;
}
static unsigned object_count(lv_obj_t *o)
{
    unsigned n=1, children=lv_obj_get_child_count(o);
    for (unsigned i=0;i<children;i++) n+=object_count(lv_obj_get_child(o,(int32_t)i));
    return n;
}
static int s_action_count;
static lw_scene_action_t s_action;
static void action(lw_scene_action_t value, void *context)
{ s_action=value; s_action_count++; (*(int*)context)++; }
static void click(int x,int y)
{
    pump(40); /* Render/layout the newly created page before starting a gesture. */
    lw_host_pointer_t *p=lw_host_pointer(); p->x=x; p->y=y; p->pressed=1; pump(100); p->pressed=0; pump(120);
}
static void shot(const char *dir,const char *name)
{
    char path[512]; pump(30); snprintf(path,sizeof(path),"%s\\%s.bmp",dir,name);
    CHECK("real LVGL screenshot saved",lw_host_screenshot_bmp(path)==0);
}
static void caption(lv_obj_t *parent,int y,const char *text)
{
    lv_obj_t *l=lv_label_create(parent); lv_label_set_text(l,text); lv_obj_set_pos(l,32,y);
    lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0); lv_obj_set_style_text_color(l,lv_color_hex(0x594b65),0);
}

int lw_ui_v2_test(const char *output_dir)
{
    setvbuf(stdout,NULL,_IONBF,0);
    printf("UI V2 TECHNICAL TEST: synthetic checker patterns, not final artwork\n");
    s_tick=lv_tick_get(); lv_tick_set_cb(test_tick);
    lv_obj_t *previous=lv_screen_active();
    lv_obj_t *screen=lv_obj_create(NULL); lv_obj_remove_style_all(screen); lv_obj_set_size(screen,1024,600);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0xfffaf4),0); lv_obj_set_style_bg_opa(screen,255,0);
    lv_screen_load(screen);
    caption(screen,16,"Little World 2.0 / TECHNICAL FIXTURE / NOT FINAL ART");

    CHECK("valid registry",lw_assets_register(s_specs,2,2u*1024u*1024u));
    CHECK("pixel budget rejects oversized configuration",!lw_assets_register(s_specs,2,3u*1024u*1024u));
    CHECK("null nonempty registry rejected",!lw_assets_register(NULL,1,4096));
    lw_asset_spec_t duplicate[]={s_specs[0],s_specs[0]};
    CHECK("duplicate ids rejected atomically",!lw_assets_register(duplicate,2,4096));
    lw_asset_spec_t invalid=s_specs[0]; invalid.frames=33;
    CHECK("frame count bounded",!lw_assets_register(&invalid,1,4096));
    invalid=s_specs[0]; invalid.width=0;
    CHECK("zero dimensions rejected",!lw_assets_register(&invalid,1,4096));
    invalid=s_specs[0]; invalid.format=LV_COLOR_FORMAT_ARGB8888;
    CHECK("unsupported format rejected",!lw_assets_register(&invalid,1,4096));
    invalid=s_specs[0]; invalid.anchor_y=33;
    CHECK("invalid anchor rejected",!lw_assets_register(&invalid,1,4096));
    CHECK("registry preserved after failed replacement",lw_assets_find("pet.idle")==&s_specs[0]);
    CHECK("missing id returns no lease",!lw_asset_acquire("missing",0));
    CHECK("frame out of bounds returns no lease",!lw_asset_acquire("pet.idle",3));
    lw_asset_lease_t *lease=lw_asset_acquire("pet.idle",0);
    CHECK("frame acquired",lease!=NULL);
    CHECK("correct planar bytes",lw_asset_image(lease) && lw_asset_image(lease)->data_size==3072);
    CHECK("active registry cannot be replaced",!lw_assets_register(s_specs,2,4096));
    lw_asset_release(lease);
    CHECK("lease release returns to zero",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);
    CHECK("small budget supported",lw_assets_register(s_specs,2,3072));
    lease=lw_asset_acquire("pet.idle",0);
    CHECK("budget allows exact size",lease!=NULL);
    CHECK("budget rejects next allocation",!lw_asset_acquire("pet.idle",1));
    lw_asset_release(lease);
    CHECK("restore registry",lw_assets_register(s_specs,2,2u*1024u*1024u));
    s_pet.fail_frame=1;
    CHECK("provider failure handled",!lw_asset_acquire("pet.idle",1));
    CHECK("failed load leaks no pixels",lw_assets_stats().live_bytes==0);
    s_pet.fail_frame=-1;

    /* Standard file backend is the same C provider usable with an IDF VFS. */
    char raw_path[512]; snprintf(raw_path,sizeof(raw_path),"%s\\.state\\fixture.raw",output_dir);
    FILE *raw=fopen(raw_path,"wb");
    CHECK("create local test payload",raw!=NULL);
    if (raw) { const uint8_t bytes[]={1,2,3,4,5,6,7,8}; fwrite(bytes,1,sizeof(bytes),raw); fclose(raw); }
    lw_asset_raw_file_t file={raw_path,0,4,2}; uint8_t pixel[4]={0};
    CHECK("raw backend seeks second frame",lw_asset_read_raw_file(&file,1,pixel,4) && pixel[0]==5 && pixel[3]==8);
    CHECK("raw backend rejects past end",!lw_asset_read_raw_file(&file,2,pixel,4));
    CHECK("raw backend rejects undersized stride",!lw_asset_read_raw_file(&file,0,pixel,5));
    file.offset=7;
    CHECK("truncated payload fails",!lw_asset_read_raw_file(&file,0,pixel,4));

    int before=timer_count();
    lw_sprite_t *sprite=lw_sprite_create(screen,180,160,256);
    CHECK("sprite owns one timer",sprite && timer_count()==before+1);
    CHECK("sprite rejects beyond approved proportion ceiling",!lw_sprite_create(screen,0,0,321));
    CHECK("play loop",lw_sprite_play(sprite,&s_loop));
    CHECK("starts on frame zero",lw_sprite_frame(sprite)==0);
    CHECK("first interval",lw_sprite_seek(sprite,79) && lw_sprite_frame(sprite)==0);
    CHECK("variable duration boundary",lw_sprite_seek(sprite,80) && lw_sprite_frame(sprite)==1);
    CHECK("second interval",lw_sprite_seek(sprite,219) && lw_sprite_frame(sprite)==1);
    CHECK("third frame",lw_sprite_seek(sprite,220) && lw_sprite_frame(sprite)==2);
    CHECK("loop wraps",lw_sprite_seek(sprite,320) && lw_sprite_frame(sprite)==0);
    CHECK("late update skips directly",lw_sprite_seek(sprite,100000) && lw_sprite_frame(sprite)==1);
    unsigned reads=s_pet.reads;
    CHECK("same frame seek succeeds",lw_sprite_seek(sprite,100001));
    CHECK("same frame never reloaded",s_pet.reads==reads);
    CHECK("play finite",lw_sprite_play(sprite,&s_once));
    CHECK("once holds final frame",lw_sprite_seek(sprite,999) && lw_sprite_frame(sprite)==2 && lw_sprite_finished(sprite));
    CHECK("restart loop",lw_sprite_play(sprite,&s_loop));
    lw_sprite_pause(sprite); unsigned held=lw_sprite_frame(sprite); pump(400);
    CHECK("pause keeps frame",lw_sprite_frame(sprite)==held);
    lw_sprite_resume(sprite); pump(90);
    CHECK("resume excludes paused time",lw_sprite_frame(sprite)==1);
    lw_sprite_set_enabled(sprite,false); pump(500);
    CHECK("animations off shows first frame",lw_sprite_frame(sprite)==0);
    reads=s_pet.reads; pump(400);
    CHECK("disabled player loads nothing",s_pet.reads==reads);
    lw_sprite_set_enabled(sprite,true); pump(90);
    CHECK("animations on restarts phase",lw_sprite_frame(sprite)==1);
    CHECK("restart before failure",lw_sprite_play(sprite,&s_loop)); s_pet.fail_frame=1;
    CHECK("load failure returns false",!lw_sprite_seek(sprite,80));
    CHECK("failed frame preserves old image",lw_sprite_frame(sprite)==0 && lw_assets_stats().live_leases==1);
    s_pet.fail_frame=-1;
    CHECK("load can recover",lw_sprite_seek(sprite,80) && lw_sprite_frame(sprite)==1);
    const uint16_t bad_sequence[]={3},bad_time[]={0},good_time[]={80};
    lw_clip_t bad={"pet.idle",bad_sequence,good_time,1,true};
    CHECK("invalid sequence rejected",!lw_sprite_play(sprite,&bad));
    bad.sequence=s_sequence; bad.duration_ms=bad_time;
    CHECK("zero duration rejected",!lw_sprite_play(sprite,&bad));
    CHECK("old clip intact after failure",lw_sprite_seek(sprite,220) && lw_sprite_frame(sprite)==2);
    caption(screen,204,"32x32 RGB565+A8 / stable bottom-center anchor / variable frame durations");
    shot(output_dir,"01-sprite-alpha");
    lw_sprite_destroy(sprite);
    CHECK("explicit deletion removes timer",timer_count()==before);
    CHECK("explicit deletion releases pixels",lw_assets_stats().live_bytes==0);

    lv_obj_t *viewport=lv_obj_create(screen); lv_obj_remove_style_all(viewport); lv_obj_set_pos(viewport,0,64); lv_obj_set_size(viewport,1024,440);
    lv_obj_remove_flag(viewport,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    unsigned objects=object_count(screen); before=timer_count(); int callbacks=0;
    lw_scene_t *scene=lw_scene_create(viewport,64,440,action,&callbacks);
    CHECK("scene constructed",scene!=NULL);
    CHECK("background loaded lazily",lw_scene_image(scene,LW_LAYER_BACKGROUND,"room.home",32,90));
    CHECK("missing image can fall back",!lw_scene_image(scene,LW_LAYER_FOREGROUND,"unavailable",0,64));
    CHECK("layer order is stable",lv_obj_get_index(lw_scene_layer(scene,LW_LAYER_BACKGROUND))<lv_obj_get_index(lw_scene_layer(scene,LW_LAYER_CHARACTER)) &&
                                     lv_obj_get_index(lw_scene_layer(scene,LW_LAYER_FOREGROUND))<lv_obj_get_index(lw_scene_layer(scene,LW_LAYER_HOTSPOTS)));
    lw_hotspot_t hit={120,100,64,64,LW_HIT_FEED};
    CHECK("valid screen-space hotspot",lw_scene_hotspot(scene,&hit));
    hit.w=47; CHECK("small target rejected",!lw_scene_hotspot(scene,&hit));
    hit.w=64; hit.x=1000; CHECK("outside viewport rejected",!lw_scene_hotspot(scene,&hit));
    hit.x=120; hit.y=40; CHECK("topbar target rejected",!lw_scene_hotspot(scene,&hit));
    sprite=lw_scene_sprite(scene,200,160,256); CHECK("scene sprite plays",lw_sprite_play(sprite,&s_loop));
    click(150,130); CHECK("pointer delivers transparent hotspot action",callbacks==1 && s_action==LW_HIT_FEED);
    click(700,480); CHECK("outside target does not fire",callbacks==1);
    caption(lw_scene_layer(scene,LW_LAYER_OVERLAY),240,"Tiny checkerboard = diagnostic image; transparent 64x64 hit area tested with real input.");
    shot(output_dir,"02-scene-layers");
    lw_scene_destroy(scene);
    CHECK("parent scene deletion removes sprite timer",timer_count()==before);
    CHECK("parent scene deletion releases all leases",lw_assets_stats().live_leases==0 && lw_assets_stats().live_bytes==0);
    CHECK("parent scene deletion restores object count",object_count(screen)==objects);

    /* Exercise both blueprint layouts and their action-only business boundary. */
    lw_world_t *world=lw_world_create(viewport,LW_WORLD_HOME,action,&callbacks);
    CHECK("home blueprint built",world!=NULL);
    CHECK("diagnostic textures are not production-sized art",!lw_world_has_art(world));
    lw_state_t *state=lw_app_state();
    lw_world_refresh(world,&state->pet,lw_now(),state->anniversary,lw_today());
    click(800,170); CHECK("anniversary action delivered",s_action==LW_HIT_ANNIVERSARY);
    click(850,360); CHECK("letter action delivered",s_action==LW_HIT_LETTER);
    caption(lw_scene_layer(lw_world_scene(world),LW_LAYER_OVERLAY),300,"HOME BLUEPRINT / no accepted room or character / not a visual approval preview");
    shot(output_dir,"03-home-blueprint");
    lw_world_destroy(world);
    world=lw_world_create(viewport,LW_WORLD_PET,action,&callbacks);
    lw_world_refresh(world,&state->pet,lw_now(),state->anniversary,lw_today());
    click(620,390); CHECK("feed furniture action delivered",s_action==LW_HIT_FEED);
    click(620,260); CHECK("toy furniture action delivered",s_action==LW_HIT_PLAY);
    click(150,330); CHECK("bed furniture action delivered",s_action==LW_HIT_REST);
    click(414,340); CHECK("resident pet action delivered",s_action==LW_HIT_PET);
    shot(output_dir,"04-pet-blueprint");
    lw_world_destroy(world);
    before=timer_count(); objects=object_count(screen);
    for (int i=0;i<50;i++) {
        world=lw_world_create(viewport,i%2 ? LW_WORLD_PET : LW_WORLD_HOME,action,&callbacks);
        pump(150); lw_world_destroy(world);
    }
    CHECK("50 page cycles: no object growth",object_count(screen)==objects);
    CHECK("50 page cycles: no timer growth",timer_count()==before);
    CHECK("50 page cycles: no outstanding pixels or leases",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);

    CHECK("unregister with zero leases",lw_assets_register(NULL,0,2u*1024u*1024u));
    world=lw_world_create(viewport,LW_WORLD_HOME,action,&callbacks);
    CHECK("missing entire pack is reported",world && !lw_world_has_art(world));
    lw_world_destroy(world);
    CHECK("missing pack cleanup",lw_assets_stats().live_bytes==0);

    /* Production-size budget exercise without loading an entire animation. */
    fixture_t full_room={1024,600,false,-1,0},full_pet={256,256,true,-1,0};
    lw_asset_spec_t full[]={
        {"room",1024,600,1,0,0,LV_COLOR_FORMAT_RGB565,read_fixture,&full_room},
        {"pet",256,256,16,128,236,LV_COLOR_FORMAT_RGB565A8,read_fixture,&full_pet}
    };
    CHECK("register production-sized provider",lw_assets_register(full,2,2u*1024u*1024u));
    lw_asset_lease_t *bg=lw_asset_acquire("room",0), *current=lw_asset_acquire("pet",0), *next=lw_asset_acquire("pet",1);
    CHECK("room plus two frames fit",bg && current && next);
    CHECK("actual pixel bytes match budget",lw_assets_stats().live_bytes==1622016u);
    CHECK("only requested frames read",full_pet.reads==2);
    CHECK("outstanding leases block unregister",!lw_assets_register(NULL,0,2u*1024u*1024u));
    size_t peak=lw_assets_stats().peak_bytes;
    lw_asset_release(next); lw_asset_release(current); lw_asset_release(bg);
    CHECK("production-sized cleanup",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);
    CHECK("clear borrowed stack registry",lw_assets_register(NULL,0,2u*1024u*1024u));
    lv_obj_delete(viewport);
    lv_screen_load(previous); lv_obj_delete(screen);
    char report_path[512]; snprintf(report_path,sizeof(report_path),"%s\\test-results.json",output_dir);
    FILE *report=fopen(report_path,"wb");
    if (!report) return 2;
    fprintf(report,"{\n  \"scope\": \"real Windows LVGL technical fixtures, not final art or board tests\",\n  \"checks\": %d,\n  \"failures\": %d,\n  \"cycles\": 50,\n  \"production_pixel_peak_bytes\": %zu,\n  \"live_leases\": %u,\n  \"live_bytes\": %zu\n}\n",
            s_checks,s_failed,peak,lw_assets_stats().live_leases,lw_assets_stats().live_bytes);
    fclose(report);
    printf("UI V2 TESTS: %d checks, %d failures; production pixel peak %zu B; live pixels 0.\n",s_checks,s_failed,peak);
    return s_failed ? 1 : 0;
}
