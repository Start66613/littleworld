/* Read-only native-pixel frame provider, no PNG/Godot runtime. SPDX-License-Identifier: MIT */
#include "pixel_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lw_storage.h"

#define MAX_CLIPS 24
typedef struct { lw_asset_raw_file_t file; lw_file_cache_t *cache; const char *relative; } frame_source_t;
struct lw_pixel_pack {
    char path[512],names[MAX_CLIPS][33],ids[MAX_CLIPS][64];
    lw_asset_spec_t specs[MAX_CLIPS+1];
    frame_source_t sources[MAX_CLIPS]; lw_file_cache_t *cache; char basename[128];
    lw_named_clip_t clips[MAX_CLIPS],test_clip;
    uint16_t sequences[MAX_CLIPS][32],durations[MAX_CLIPS][32],test_seq[4],test_time[4];
    lw_actor_art_t art[2];
    lw_pixel_pack_t *male,*extra;
    lw_named_clip_t extra_named[MAX_CLIPS];
    lw_asset_spec_t combined[MAX_CLIPS*3];
    unsigned count;
};
static bool read_frame(void *ctx,uint16_t frame,uint8_t *data,size_t bytes)
{
    frame_source_t *src=ctx; if (!src || frame>=src->file.frames || bytes>src->file.frame_stride) return false;
    if (!src->cache) return lw_asset_read_raw_file(&src->file,frame,data,bytes);
    uint64_t offset=(uint64_t)src->file.offset+(uint64_t)frame*src->file.frame_stride;
    bool ok=lw_file_cache_read(src->cache,src->relative,offset,data,bytes);
    if (ok) { unsigned next=(frame+1)%src->file.frames; (void)lw_file_cache_prefetch(src->cache,src->relative,(uint64_t)src->file.offset+(uint64_t)next*src->file.frame_stride,bytes); }
    return ok;
}
lw_file_cache_stats_t lw_pixel_pack_cache_stats(const lw_pixel_pack_t *p)
{
    lw_file_cache_stats_t a=lw_file_cache_stats(p ? p->cache : NULL);
    for (unsigned i=0;p && i<2;i++) { const lw_pixel_pack_t *child=i ? p->extra : p->male; if (!child) continue; lw_file_cache_stats_t b=lw_file_cache_stats(child->cache);
#define SUM(field) a.field+=b.field
        SUM(budget_bytes); SUM(resident_bytes); SUM(peak_bytes); SUM(metadata_bytes); SUM(hits); SUM(misses); SUM(reads); SUM(failures); SUM(evictions); SUM(prefetched); SUM(queue_drops); SUM(pending); SUM(loading);
#undef SUM
    } return a;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|(uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool test_actor(void *context,uint16_t frame,uint8_t *data,size_t bytes)
{
    (void)context; if (bytes!=196608 || frame>=4) return false;
    memset(data,0,bytes);
    /* Deliberately neutral test mannequin, not a generated male concept. */
    for (int y=28;y<226;y++) for (int x=80;x<176;x++) {
        int qx=(x/4)*4,qy=(y/4)*4;
        bool head=(qx-128)*(qx-128)+(qy-70)*(qy-70)<40*40;
        bool body=qx>=92 && qx<164 && qy>=106 && qy<184;
        bool legs=qy>=180 && ((qx>=96 && qx<116)||(qx>=140 && qx<160));
        if (!head && !body && !legs) continue;
        uint16_t color=((qx/8+qy/8)%2) ? 0x9c13 : 0xb4d7;
        if (head && qy>=68 && qy<76 && ((qx>=104 && qx<112)||(qx>=140 && qx<148))) color=frame==3 ? 0xb4d7 : 0x528b;
        size_t i=(size_t)y*256+(unsigned)x; data[i*2]=(uint8_t)color; data[i*2+1]=(uint8_t)(color>>8); data[131072+i]=255;
    }
    return true;
}
static lw_pixel_pack_t *open_budget(const char *path,size_t budget)
{
    if (!path || strlen(path)>=512) return NULL;
    FILE *file=fopen(path,"rb"); if (!file) return NULL;
    uint8_t header[16];
    if (fread(header,1,16,file)!=16 || memcmp(header,"LWPX0001",8) || u16(header+8)!=256 || u16(header+10)!=256 ||
        !u16(header+12) || u16(header+12)>MAX_CLIPS) { fclose(file); return NULL; }
    if (fseek(file,0,SEEK_END)) { fclose(file); return NULL; }
    long file_size=ftell(file); if (file_size<16 || fseek(file,16,SEEK_SET)) { fclose(file); return NULL; }
    lw_pixel_pack_t *pack=calloc(1,sizeof(*pack)); if (!pack) { fclose(file); return NULL; }
    strcpy(pack->path,path); pack->count=u16(header+12);
    const char *slash=strrchr(path,'/'),*back=strrchr(path,'\\'); if (!slash || (back && back>slash)) slash=back;
    const char *base=slash ? slash+1 : path; char directory[512]=".";
    if (strlen(base)<sizeof(pack->basename)) {
        strcpy(pack->basename,base);
        if (slash) { size_t n=(size_t)(slash-path); memcpy(directory,path,n); directory[n]=0; }
        pack->cache=lw_file_cache_open(directory,budget);
    }
    bool ok=true;
    for (unsigned i=0;i<pack->count && ok;i++) {
        uint8_t record[108]; if (fread(record,1,sizeof(record),file)!=sizeof(record)) { ok=false; break; }
        memcpy(pack->names[i],record,32); pack->names[i][32]=0;
        if (!memchr(record,0,32) || !pack->names[i][0]) { ok=false; break; }
        unsigned frames=u16(record+32); int ax=(int16_t)u16(record+34),ay=(int16_t)u16(record+36); uint32_t offset=u32(record+40);
        if (!frames || frames>32 || ax<0 || ax>256 || ay<0 || ay>256 || record[38]>1 ||
            offset<16+108*pack->count || (uint64_t)offset+(uint64_t)frames*196608>(uint64_t)file_size) { ok=false; break; }
        snprintf(pack->ids[i],64,"qianqian.%s",pack->names[i]);
        pack->sources[i]=(frame_source_t){{pack->path,offset,196608,(uint16_t)frames},pack->cache,pack->basename};
        pack->specs[i]=(lw_asset_spec_t){pack->ids[i],256,256,(uint16_t)frames,(int16_t)ax,(int16_t)ay,LV_COLOR_FORMAT_RGB565A8,read_frame,&pack->sources[i]};
        for (unsigned f=0;f<frames;f++) {
            pack->sequences[i][f]=(uint16_t)f; pack->durations[i][f]=u16(record+44+f*2);
            if (pack->durations[i][f]<20 || pack->durations[i][f]>2000) ok=false;
        }
        pack->clips[i].name=pack->names[i];
        pack->clips[i].clip=(lw_clip_t){pack->ids[i],pack->sequences[i],pack->durations[i],(uint16_t)frames,record[38]!=0};
    }
    fclose(file);
    if (ok) {
        for (unsigned i=0;i<4;i++) { pack->test_seq[i]=(uint16_t)i; pack->test_time[i]=125; }
        pack->specs[pack->count]=(lw_asset_spec_t){"test_male.idle",256,256,4,128,226,LV_COLOR_FORMAT_RGB565A8,test_actor,NULL};
        pack->test_clip=(lw_named_clip_t){"idle",{"test_male.idle",pack->test_seq,pack->test_time,4,true}};
        pack->art[0]=(lw_actor_art_t){"TEST A",&pack->test_clip,1,true};
        pack->art[1]=(lw_actor_art_t){"qianqian",pack->clips,pack->count,false};
        ok=lw_assets_register(pack->specs,pack->count+1,2u*1024u*1024u);
    }
    if (!ok) { lw_file_cache_close(pack->cache); free(pack); return NULL; }
    return pack;
}
const lw_actor_art_t *lw_pixel_pack_art(const lw_pixel_pack_t *p) { return p ? p->art : NULL; }
lw_pixel_pack_t *lw_pixel_pack_open(const char *path) { return open_budget(path,4u*196608u); }
static bool combine(lw_pixel_pack_t *p)
{
    unsigned count=p->count; memcpy(p->combined,p->specs,count*sizeof(p->specs[0]));
    if (!p->male) p->combined[count++]=p->specs[p->count];
    if (p->male) { memcpy(p->combined+count,p->male->specs,p->male->count*sizeof(p->specs[0])); count+=p->male->count; }
    if (p->extra) { memcpy(p->combined+count,p->extra->specs,p->extra->count*sizeof(p->specs[0])); count+=p->extra->count; }
    return lw_assets_register(p->combined,count,2u*1024u*1024u);
}
bool lw_pixel_pack_attach_male(lw_pixel_pack_t *p,const char *path)
{
    if (!p || p->male || lw_assets_stats().live_leases) return false;
    lw_pixel_pack_t *male=open_budget(path,2u*196608u); if (!male) return false;
    for (unsigned i=0;i<male->count;i++) snprintf(male->ids[i],sizeof(male->ids[i]),"male.%s",male->names[i]);
    p->male=male; if (!combine(p)) { p->male=NULL; lw_pixel_pack_close(male); combine(p); return false; }
    p->art[0]=(lw_actor_art_t){"male",male->clips,male->count,false}; return true;
}
bool lw_pixel_pack_attach_qianqian_clips(lw_pixel_pack_t *p,const char *path)
{
    if (!p || p->extra || lw_assets_stats().live_leases) return false;
    lw_pixel_pack_t *extra=open_budget(path,2u*196608u); if (!extra) return false;
    if (p->count+extra->count>MAX_CLIPS) { lw_pixel_pack_close(extra); combine(p); return false; }
    p->extra=extra; if (!combine(p)) { p->extra=NULL; lw_pixel_pack_close(extra); combine(p); return false; }
    memcpy(p->extra_named,p->clips,p->count*sizeof(p->clips[0])); memcpy(p->extra_named+p->count,extra->clips,extra->count*sizeof(p->clips[0]));
    p->art[1]=(lw_actor_art_t){"qianqian",p->extra_named,p->count+extra->count,false}; return true;
}
bool lw_pixel_pack_close(lw_pixel_pack_t *p)
{
    if (!p) return true;
    if (!lw_assets_register(NULL,0,2u*1024u*1024u)) return false;
    if (p->male) lw_pixel_pack_close(p->male);
    if (p->extra) lw_pixel_pack_close(p->extra);
    lw_file_cache_close(p->cache); free(p); return true;
}
