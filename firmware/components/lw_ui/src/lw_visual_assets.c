/* Full-screen artwork cache.
 *
 * The three theme scenes, the RPG room and the pool of pet rooms are all
 * 1024x600 RGB565 - 1.2 MB each. They used to share a single "one scene, one
 * auxiliary room" pair of slots, which was fine while a session sat on one page
 * but made every page switch re-read over a megabyte from the card: measured
 * 670 ms per switch at the card's 1.9 MB/s. A small LRU pool keeps the handful
 * of images a session actually cycles through (desktop scene + RPG room + pet
 * room), so switching starts costing a lookup instead of a card read.
 *
 * The pool is bounded on purpose: PSRAM is the resource this board keeps running
 * out of (see BOOT_OPTIMIZATION.md §7), so the slot count is a constant someone
 * has to think about, not "as much as fits".
 */
#include "lw_visual_assets.h"
#include "lw_path.h"
#include "lw_ambience.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>

/* Four, not three: a session that has visited the desktop, RPG and pet page wants
 * the ambience scene, the preference scene, the RPG room and the pet room at
 * once. With three the desktop's own scene load evicts the RPG room the boot
 * warm-up just paid for, and the warm-up looks like it does nothing. */
#define VISUAL_FULL_SLOTS 4
#define VISUAL_THUMB_SLOTS 3
#define VISUAL_KEY 24

typedef struct {
    lv_image_dsc_t image;
    uint8_t *pixels;
    char key[VISUAL_KEY];
    unsigned refs;
    uint32_t used;
} entry_t;

static entry_t full_slots[VISUAL_FULL_SLOTS];
static entry_t thumb_slots[VISUAL_THUMB_SLOTS];
static char directory[512];
static char aux_directory[512];
static lw_visual_stats_t stats;
static uint32_t use_clock;

static unsigned u16(const uint8_t *b) { return b[0]|(unsigned)b[1]<<8; }

/* Scene names are relative to the resource root, like every other payload. */
static bool scene_path(char *dst, size_t cap, unsigned theme, bool thumb)
{ char name[32]; snprintf(name,sizeof(name),"scene-%u%s.lwr",theme,thumb ? "-thumb" : ""); return lw_path_join(dst,cap,directory,name); }

static void free_entry(entry_t *e)
{ if (e->pixels) { lv_image_cache_drop(&e->image); stats.live_bytes-=e->image.data_size; lv_free(e->pixels); memset(e,0,sizeof(*e)); } }

static void free_pool(entry_t *pool, unsigned count) { for (unsigned i=0;i<count;i++) free_entry(&pool[i]); }

static entry_t *lookup(entry_t *pool, unsigned count, const char *key)
{ for (unsigned i=0;i<count;i++) if (pool[i].pixels && strcmp(pool[i].key,key)==0) return &pool[i]; return NULL; }

/* Least recently used slot that nobody is drawing from. A slot that is still
 * referenced stays put: evicting it would leave an lv_image pointing at freed
 * bytes. */
static entry_t *reserve(entry_t *pool, unsigned count)
{
    entry_t *worst=NULL;
    for (unsigned i=0;i<count;i++) {
        if (!pool[i].pixels) return &pool[i];
        if (pool[i].refs) continue;
        if (!worst || pool[i].used<worst->used) worst=&pool[i];
    }
    if (worst) free_entry(worst);
    return worst;
}

bool lw_visual_assets_shutdown(void)
{
    if (stats.refs) return false;
    free_pool(full_slots,VISUAL_FULL_SLOTS); free_pool(thumb_slots,VISUAL_THUMB_SLOTS);
    return true;
}

bool lw_visual_assets_configure(const char *path)
{
    if (stats.refs || (path && strlen(path)>=sizeof(directory))) return false;
    lw_visual_assets_shutdown(); memset(&stats,0,sizeof(stats)); memset(full_slots,0,sizeof(full_slots)); memset(thumb_slots,0,sizeof(thumb_slots)); use_clock=0;
    directory[0]=0; if (path) strcpy(directory,path); return true;
}

bool lw_visual_assets_available(void)
{ if (!directory[0]) return false; char path[600]; if (!scene_path(path,sizeof(path),0,false)) return false; FILE *f=fopen(path,"rb"); if (!f) return false; fclose(f); return true; }

lw_visual_stats_t lw_visual_assets_stats(void) { return stats; }

/* Read one card into an already reserved slot, taking no reference. Drawing
 * rights are what load() adds on top; the bytes are the expensive part. */
static bool fill(entry_t *e, const char *key, const char *path, unsigned w, unsigned h)
{
    FILE *f=fopen(path,"rb"); if (!f) { stats.failures++; return false; }
    const size_t bytes=(size_t)w*h*2u; uint8_t header[16];
    bool valid=fread(header,1,16,f)==16 && !memcmp(header,"LWSR0001",8) && u16(header+8)==w && u16(header+10)==h;
    if (!valid) { fclose(f); stats.failures++; return false; }
    uint8_t *data=lv_malloc(bytes); if (!data) { fclose(f); stats.failures++; return false; }
    valid=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF; fclose(f);
    if (!valid) { lv_free(data); stats.failures++; return false; }
    e->pixels=data; e->refs=0; e->used=++use_clock;
    snprintf(e->key,sizeof(e->key),"%s",key);
    e->image.header.magic=LV_IMAGE_HEADER_MAGIC; e->image.header.cf=LV_COLOR_FORMAT_RGB565;
    e->image.header.w=w; e->image.header.h=h; e->image.header.stride=w*2;
    e->image.data=data; e->image.data_size=bytes; stats.reads++; stats.live_bytes+=bytes;
    if (stats.live_bytes>stats.peak_bytes) stats.peak_bytes=stats.live_bytes;
    return true;
}

static entry_t *load(entry_t *pool, unsigned count, const char *key, const char *path, unsigned w, unsigned h)
{
    entry_t *e=lookup(pool,count,key);
    if (e) { stats.hits++; e->refs++; stats.refs++; e->used=++use_clock; return e; }
    e=reserve(pool,count);
    if (!e) { stats.failures++; return NULL; }
    if (!fill(e,key,path,w,h)) return NULL;
    e->refs=1; stats.refs++;
    return e;
}

/* Put a card in the pool without giving it a reference. Nothing depends on this
 * succeeding: if the read fails or the pool evicts the slot again, the page that
 * wanted it simply pays the card read it always used to pay. */
static bool warm(entry_t *pool, unsigned count, const char *key, const char *path, unsigned w, unsigned h)
{
    if (lookup(pool,count,key)) return true;
    entry_t *e=reserve(pool,count);
    if (!e) return false;
    return fill(e,key,path,w,h);
}

static void deleted(lv_event_t *event)
{ entry_t *e=lv_event_get_user_data(event); if (e->refs) e->refs--; if (stats.refs) stats.refs--; }

static lv_obj_t *place(lv_obj_t *parent, entry_t *e, int w, int h)
{
    if (!e) return NULL;
    lv_obj_t *o=lv_image_create(parent); lv_image_set_src(o,&e->image); lv_image_set_antialias(o,false);
    unsigned sx=(unsigned)w*256/e->image.header.w,sy=(unsigned)h*256/e->image.header.h;
    lv_image_set_scale(o,sx<sy ? sx : sy); lv_obj_center(o);
    lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,e);
    return o;
}

lv_obj_t *lw_visual_scene_image(lv_obj_t *parent,unsigned theme,int w,int h,bool thumb)
{
    if (!directory[0] || theme>2) return NULL;
    char path[600]; if (!scene_path(path,sizeof(path),theme,thumb)) return NULL;
    char key[VISUAL_KEY]; snprintf(key,sizeof(key),"scene-%u%s",theme,thumb ? "-thumb":"");
    entry_t *e=load(thumb ? thumb_slots : full_slots,thumb ? VISUAL_THUMB_SLOTS : VISUAL_FULL_SLOTS,
                    key,path,thumb ? 256u : 1024u,thumb ? 150u : 600u);
    return place(parent,e,w,h);
}

bool lw_visual_aux_configure(const char *path)
{
    if (stats.refs || (path && strlen(path)>=sizeof(aux_directory))) return false;
    free_pool(full_slots,VISUAL_FULL_SLOTS);
    aux_directory[0]=0; if (path) strcpy(aux_directory,path); return true;
}

static lv_obj_t *aux_image(lv_obj_t *parent,const char *name)
{
    if (!aux_directory[0]) return NULL;
    char path[600]; snprintf(path,sizeof(path),"%s/%s.lwr",aux_directory,name);
    entry_t *e=load(full_slots,VISUAL_FULL_SLOTS,name,path,1024u,600u);
    if (!e) return NULL;
    lv_obj_t *o=lv_image_create(parent); lv_image_set_src(o,&e->image); lv_image_set_antialias(o,false);
    lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,e); return o;
}

lv_obj_t *lw_visual_rpg_image(lv_obj_t *parent) { return aux_image(parent,"rpg-room"); }

/* Boot warm-up entry points. They read through the same slot pool the pages use,
 * so "warm" means "the next load() of this name is a lookup". */
bool lw_visual_prefetch_scene(unsigned theme)
{
    if (!directory[0] || theme>2) return false;
    char path[600]; if (!scene_path(path,sizeof(path),theme,false)) return false;
    char key[VISUAL_KEY]; snprintf(key,sizeof(key),"scene-%u",theme);
    return warm(full_slots,VISUAL_FULL_SLOTS,key,path,1024u,600u);
}

bool lw_visual_prefetch_room(const char *name)
{
    if (!aux_directory[0] || !name || !name[0]) return false;
    char path[600]; snprintf(path,sizeof(path),"%s/%s.lwr",aux_directory,name);
    return warm(full_slots,VISUAL_FULL_SLOTS,name,path,1024u,600u);
}

/* One place decides which room card the pet page shows, so the warm-up can ask
 * for the same file instead of guessing from the phase and getting it wrong. */
const char *lw_visual_daily_name(void)
{
    lw_day_phase_t p=lw_ambience_phase();
    return p>=LW_NIGHT ? "daily-room-night" : p<=LW_DAY ? "daily-room-day" : "daily-room";
}

lv_obj_t *lw_visual_daily_image(lv_obj_t *parent)
{
    const char *name=lw_visual_daily_name();
    lv_obj_t *o=aux_image(parent,name);return o ? o : aux_image(parent,"daily-room");
}
