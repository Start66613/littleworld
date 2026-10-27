/* One full scene + three thumbnail slots. No PNG decoder or hidden file scan. */
#include "lw_visual_assets.h"
#include "lw_path.h"
#include "lw_ambience.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>
typedef struct { lv_image_dsc_t image; uint8_t *pixels; unsigned theme,refs; bool thumbnail; } entry_t;
static entry_t entries[4]; static char directory[512]; static lw_visual_stats_t stats;
static entry_t auxiliary; static char aux_directory[512];
static unsigned u16(const uint8_t *b) { return b[0]|(unsigned)b[1]<<8; }
/* Scene names are relative to the resource root, like every other payload. */
static bool scene_path(char *dst, size_t cap, unsigned theme, bool thumb)
{ char name[32]; snprintf(name,sizeof(name),"scene-%u%s.lwr",theme,thumb ? "-thumb" : ""); return lw_path_join(dst,cap,directory,name); }
static void free_entry(entry_t *e)
{ if (e->pixels) { lv_image_cache_drop(&e->image); stats.live_bytes-=e->image.data_size; lv_free(e->pixels); memset(e,0,sizeof(*e)); } }
bool lw_visual_assets_shutdown(void)
{ if (stats.refs) return false; for (unsigned i=0;i<4;i++) free_entry(&entries[i]); free_entry(&auxiliary); return true; }
bool lw_visual_assets_configure(const char *path)
{
    if (stats.refs || (path && strlen(path)>=sizeof(directory))) return false;
    lw_visual_assets_shutdown(); memset(&stats,0,sizeof(stats)); directory[0]=0; if (path) strcpy(directory,path); return true;
}
bool lw_visual_assets_available(void)
{ if (!directory[0]) return false; char path[600]; if (!scene_path(path,sizeof(path),0,false)) return false; FILE *f=fopen(path,"rb"); if (!f) return false; fclose(f); return true; }
lw_visual_stats_t lw_visual_assets_stats(void) { return stats; }
static entry_t *acquire(unsigned theme,bool thumb)
{
    if (!directory[0] || theme>2) return NULL;
    for (unsigned i=0;i<4;i++) if (entries[i].pixels && entries[i].theme==theme && entries[i].thumbnail==thumb) { stats.hits++; entries[i].refs++; stats.refs++; return &entries[i]; }
    entry_t *e=&entries[thumb ? theme+1 : 0]; if (e->refs) { stats.failures++; return NULL; } free_entry(e);
    char path[600]; if (!scene_path(path,sizeof(path),theme,thumb)) { stats.failures++; return NULL; }
    FILE *f=fopen(path,"rb"); if (!f) { stats.failures++; return NULL; }
    uint8_t header[16]; unsigned w=thumb ? 256 : 1024,h=thumb ? 150 : 600; size_t bytes=(size_t)w*h*2;
    bool valid=fread(header,1,16,f)==16 && !memcmp(header,"LWSR0001",8) && u16(header+8)==w && u16(header+10)==h;
    if (!valid) { fclose(f); stats.failures++; return NULL; }
    uint8_t *data=lv_malloc(bytes); if (!data) { fclose(f); stats.failures++; return NULL; }
    valid=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF; fclose(f);
    if (!valid) { lv_free(data); stats.failures++; return NULL; }
    e->pixels=data; e->theme=theme; e->thumbnail=thumb; e->refs=1;
    e->image.header.magic=LV_IMAGE_HEADER_MAGIC; e->image.header.cf=LV_COLOR_FORMAT_RGB565; e->image.header.w=w; e->image.header.h=h; e->image.header.stride=w*2;
    e->image.data=data; e->image.data_size=bytes; stats.reads++; stats.refs++; stats.live_bytes+=bytes;
    if (stats.live_bytes>stats.peak_bytes) { stats.peak_bytes=stats.live_bytes; }
    return e;
}
static void deleted(lv_event_t *event) { entry_t *e=lv_event_get_user_data(event); e->refs--; stats.refs--; if (e==&auxiliary && !e->refs) free_entry(e); }
lv_obj_t *lw_visual_scene_image(lv_obj_t *parent,unsigned theme,int w,int h,bool thumb)
{
    entry_t *e=acquire(theme,thumb); if (!e) return NULL;
    lv_obj_t *o=lv_image_create(parent); lv_image_set_src(o,&e->image); lv_image_set_antialias(o,false);
    unsigned sx=(unsigned)w*256/e->image.header.w,sy=(unsigned)h*256/e->image.header.h;
    lv_image_set_scale(o,sx<sy ? sx : sy); lv_obj_center(o); lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,e); return o;
}
bool lw_visual_aux_configure(const char *path)
{
    if (stats.refs || (path && strlen(path)>=sizeof(aux_directory))) return false;
    free_entry(&auxiliary); aux_directory[0]=0; if (path) strcpy(aux_directory,path); return true;
}
static lv_obj_t *aux_image(lv_obj_t *parent,const char *name)
{
    if (!aux_directory[0] || auxiliary.refs) return NULL;
    char path[600]; snprintf(path,sizeof(path),"%s/%s.lwr",aux_directory,name);
    FILE *f=fopen(path,"rb"); if (!f) { stats.failures++; return NULL; }
    uint8_t header[16]; const size_t bytes=1024u*600u*2u;
    bool valid=fread(header,1,16,f)==16 && !memcmp(header,"LWSR0001",8) && u16(header+8)==1024 && u16(header+10)==600;
    uint8_t *data=valid ? lv_malloc(bytes) : NULL;
    valid=data && fread(data,1,bytes,f)==bytes && fgetc(f)==EOF; fclose(f);
    if (!valid) { lv_free(data); stats.failures++; return NULL; }
    auxiliary.pixels=data; auxiliary.refs=1; auxiliary.image.header.magic=LV_IMAGE_HEADER_MAGIC;
    auxiliary.image.header.cf=LV_COLOR_FORMAT_RGB565; auxiliary.image.header.w=1024; auxiliary.image.header.h=600; auxiliary.image.header.stride=2048;
    auxiliary.image.data=data; auxiliary.image.data_size=bytes; stats.refs++; stats.reads++; stats.live_bytes+=bytes;
    if (stats.live_bytes>stats.peak_bytes) stats.peak_bytes=stats.live_bytes;
    lv_obj_t *o=lv_image_create(parent); lv_image_set_src(o,&auxiliary.image); lv_image_set_antialias(o,false); lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,&auxiliary); return o;
}
lv_obj_t *lw_visual_rpg_image(lv_obj_t *parent) { return aux_image(parent,"rpg-room"); }
lv_obj_t *lw_visual_daily_image(lv_obj_t *parent)
{
    lw_day_phase_t p=lw_ambience_phase();const char *name=p>=LW_NIGHT ? "daily-room-night" : p<=LW_DAY ? "daily-room-day" : "daily-room";
    lv_obj_t *o=aux_image(parent,name);return o ? o : aux_image(parent,"daily-room");
}
