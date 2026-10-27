/* Actual DSH reader + actual frozen payloads; no LVGL drawing or hardware. */
#include "lw_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(n, x) do {checks++; if (!(x)) {failures++; printf("FAIL: %s\n",n);}} while(0)
static unsigned u16(const unsigned char *p) {return p[0] | (unsigned)p[1]<<8;}
static unsigned u32(const unsigned char *p) {return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static const char *files[]={"male.lwp","qianqian-base.lwp","qianqian-wave.lwp","daily.lwp","ui.lwp","skin.lwp"};
static const char *prefixes[]={"male","qianqian","qianqian","v42","v42ui","v43skin"};

static void verify_file(const char *root, unsigned index)
{
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,files[index]);FILE *f=fopen(path,"rb");
    CHECK("open independent expected payload",f!=NULL);if(!f)return;
    unsigned char header[16];CHECK("read expected header",fread(header,1,16,f)==16);
    unsigned w=u16(header+8),h=u16(header+10),count=u16(header+12);
    size_t bytes=(size_t)w*h*3;unsigned char *expected=malloc(bytes);
    CHECK("allocate independent byte comparator",expected!=NULL);if(!expected){fclose(f);return;}
    for(unsigned i=0;i<count;i++){
        unsigned char r[108];CHECK("seek independent record",fseek(f,16L+(long)i*108L,SEEK_SET)==0);
        CHECK("read independent record",fread(r,1,108,f)==108);char id[96];snprintf(id,sizeof(id),"%s.%s",prefixes[index],(const char *)r);
        const lw_asset_spec_t *s=lw_assets_find(id);CHECK("registered geometry and anchor",s && s->width==w && s->height==h && s->anchor_x==(int)u16(r+34) && s->anchor_y==(int)u16(r+36));
        unsigned frames=u16(r+32);
        for(unsigned frame=0;frame<frames;frame++){
            CHECK("seek expected frame",fseek(f,(long)((uint64_t)u32(r+40)+(uint64_t)frame*bytes),SEEK_SET)==0);
            CHECK("read expected frame",fread(expected,1,bytes,f)==bytes);
            lw_asset_lease_t *l=lw_asset_acquire(id,(uint16_t)frame);CHECK("acquire actual frame",l!=NULL);
            if(l){const lv_image_dsc_t *d=lw_asset_image(l);CHECK("actual stride and frame bytes",d && d->header.stride==w*2 && d->data_size==bytes);
                CHECK("entire RGB565 and alpha planes match disk",d && !memcmp(d->data,expected,bytes));lw_asset_release(l);}
        }
        CHECK("out-of-range frame refused",lw_asset_acquire(id,(uint16_t)frames)==NULL);
    }
    free(expected);fclose(f);
}

int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    bool probe=!strcmp(argv[2],"probe");
    if(probe){for(unsigned i=0;i<6;i++){(void)lw_pack_registry_reset();lw_pack_t info;
        lw_pack_status_t s=lw_pack_registry_add(argv[1],files[i],prefixes[i],NULL,&info);
        printf("%s: %s\n",files[i],lw_pack_status_text(s));}
        (void)lw_pack_registry_reset();return 0;}
    CHECK("initial reset",lw_pack_registry_reset());
    for(unsigned i=0;i<6;i++){lw_pack_t info;lw_pack_status_t s=lw_pack_registry_add(argv[1],files[i],prefixes[i],NULL,&info);
        CHECK("register real pack",s==LW_PACK_OK);if(s!=LW_PACK_OK){printf("%s: %s\n",files[i],lw_pack_status_text(s));return 1;}}
    CHECK("single shared registry publishes",lw_pack_registry_register(2u*1024u*1024u));
    for(unsigned i=0;i<6;i++)verify_file(argv[1],i);
    unsigned count=0;const lw_named_clip_t *q=lw_pack_registry_clips("qianqian",&count);
    CHECK("original Qianqian 13 clips plus wave retained",q && count==14);
    const char *held_ids[]={"v43skin.atlas","male.idle","qianqian.idle","qianqian.wave","v42ui.sheet"};
    lw_asset_lease_t *held[5]={0};for(unsigned i=0;i<5;i++){held[i]=lw_asset_acquire(held_ids[i],0);CHECK("representative scene lease",held[i]!=NULL);}
    CHECK("representative 1999872 byte occupancy",lw_assets_stats().live_bytes==1999872u);
    CHECK("lease ceiling refuses another daily frame",lw_asset_acquire("v42.couple.seated",0)==NULL);
    CHECK("reset refused while atlas/frames live",!lw_pack_registry_reset());
    CHECK("refused reset preserves atlas",lw_assets_find("v43skin.atlas")!=NULL);
    for(unsigned i=0;i<5;i++)lw_asset_release(held[i]);
    CHECK("all leases and pixels freed",lw_assets_stats().live_bytes==0 && lw_assets_stats().live_leases==0);
    CHECK("final reset",lw_pack_registry_reset());
    printf("V4.3 real pack compatibility: %u checks, %u failures.\n",checks,failures);return failures ? 1 : 0;
}
