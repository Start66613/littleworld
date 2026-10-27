/* Real LVGL + DSH sprite diagnostics, without a display driver or board. */
#include "lw_pack.h"
#include "lw_sprite.h"
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(n,x) do{checks++;if(!(x)){failures++;printf("FAIL: %s\n",n);}}while(0)
static unsigned timers(void){unsigned n=0;lv_timer_t *t=NULL;while((t=lv_timer_get_next(t)))n++;return n;}
static const lw_clip_t *clip(const char *actor,const char *name)
{unsigned n=0;const lw_named_clip_t *c=lw_pack_registry_clips(actor,&n);for(unsigned i=0;i<n;i++)if(!strcmp(c[i].name,name))return &c[i].clip;return NULL;}

int main(int argc,char **argv)
{
    if(argc!=2)return 2;lv_init();lv_display_t *display=lv_display_create(1024,600);
    CHECK("headless actual LVGL display",display!=NULL);lv_obj_t *root=lv_obj_create(lv_screen_active());
    CHECK("reset registry",lw_pack_registry_reset());lw_pack_t p;
    CHECK("real male pack",lw_pack_registry_add(argv[1],"male.lwp","male",NULL,&p)==LW_PACK_OK);
    CHECK("real couple/bed pack",lw_pack_registry_add(argv[1],"daily.lwp","v42",NULL,&p)==LW_PACK_OK);
    CHECK("real atlas",lw_pack_registry_add(argv[1],"skin.lwp","v43skin",NULL,&p)==LW_PACK_OK);
    CHECK("publish shared registry",lw_pack_registry_register(2u*1024u*1024u));
    unsigned baseline=timers();lw_asset_lease_t *atlas=lw_asset_acquire("v43skin.atlas",0);CHECK("hold shared atlas",atlas!=NULL);
    for(unsigned i=0;i<20;i++){
        lw_sprite_t *standing=lw_sprite_create(root,480,478,288),*seated=lw_sprite_create(root,516,368,282);
        CHECK("288 and 282 sprites create",standing && seated);
        if(!standing || !seated)return 1;
        CHECK("nearest-neighbor accepted at approved scale",lw_sprite_set_pixel_mode(standing) && lw_sprite_set_pixel_mode(seated));
        CHECK("real standing and seated play",lw_sprite_play(standing,clip("male","idle")) && lw_sprite_play(seated,clip("v42","couple.seated")));
        unsigned live=0;lw_sprite_stats(&live,NULL);CHECK("DSH live diagnostics preserved",live==2);
        CHECK("frame seek works",lw_sprite_seek(standing,160) && lw_sprite_seek(seated,160));
        lv_obj_t *image=lw_sprite_object(standing);lv_obj_update_layout(image);
        CHECK("standing foot anchor fixed",lv_obj_get_x(image)==480-128*288/256 && lv_obj_get_y(image)==478-225*288/256);
        lw_sprite_destroy(standing);lw_sprite_destroy(seated);lw_sprite_stats(&live,NULL);
        CHECK("DSH diagnostics clean after delete",live==0);CHECK("sprite timers restore baseline",timers()==baseline);
        lw_sprite_t *bed=lw_sprite_create(root,906,464,256);CHECK("320 dog-bed sprite create",bed!=NULL);
        CHECK("complete bed animation plays",lw_sprite_play(bed,clip("v42","dogbed.look")) && lw_sprite_seek(bed,125));
        lw_sprite_destroy(bed);CHECK("bed timer released",timers()==baseline);
    }
    CHECK("unsupported scale still refused",lw_sprite_create(root,0,0,321)==NULL);
    lw_asset_release(atlas);CHECK("pixels and leases released",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);
    CHECK("registry teardown",lw_pack_registry_reset());lv_obj_delete(root);lv_display_delete(display);lv_deinit();
    printf("V4.3 real LVGL sprite bridge: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
