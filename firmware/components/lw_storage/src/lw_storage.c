#include "lw_storage.h"
#include "lw_fault.h"
#include "lw_path.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
/* Frame slots. Four was enough for the old static desktop, where only two
 * images were ever shown, but the animated characters cycle eight 196 KB frames
 * each. With four slots the cache evicted on every frame change, so each sprite
 * frame fell back to a synchronous ~9 ms read from the TF card on the LVGL
 * thread - measured as cache misses=666 evictions=610 over ten seconds of an
 * idle desktop, and as LVGL timer gaps of 128 ms average against a 20 ms period.
 * Sixteen slots cover two full eight-frame cycles of the 256x256 characters.
 *
 * V4.4: sixteen was still derived from 256x256 only. The pet page's daily.lwp
 * is 320x320, i.e. 307,200 B/frame, and its live set is couple.seated 16 +
 * dog.idle 8 = 24 frames (the dog is either on the floor or on the bed, never
 * both - joint_bed in lw_app_pet.c picks one branch for the whole run). 24
 * frames cannot fit in 16 slots no matter how large the byte budget is. The
 * slot count - not the budget - was the binding constraint there, so every pet
 * frame evicted and re-read. 32 slots hold the whole pet live set (24) plus
 * headroom, and the byte budget in
 * lw_art.c still bounds total memory, so a larger slot count cannot
 * over-allocate: reserve() refuses to place a frame unless both a free slot and
 * the remaining byte budget allow it. */
#define ENTRIES 64
#define QUEUE 24
#define MAX_FRAME (4u*1024u*1024u)
typedef struct { char file[128]; uint64_t offset; size_t bytes; uint32_t generation; } cache_key_t;
typedef struct { cache_key_t key; uint8_t *data; uint64_t used; unsigned state; unsigned pins; } entry_t;
struct lw_file_cache {
    char root[512]; entry_t entries[ENTRIES]; cache_key_t queue[QUEUE]; unsigned head,count;
    uint32_t generation; uint64_t clock; bool stop,active; lw_file_cache_stats_t stats;
#ifdef ESP_PLATFORM
    SemaphoreHandle_t mutex,event,done; TaskHandle_t worker;
#else
    CRITICAL_SECTION mutex; HANDLE event,worker;
#endif
};
static void lock(lw_file_cache_t *c) {
#ifdef ESP_PLATFORM
    xSemaphoreTake(c->mutex,portMAX_DELAY);
#else
    EnterCriticalSection(&c->mutex);
#endif
}
static void unlock(lw_file_cache_t *c) {
#ifdef ESP_PLATFORM
    xSemaphoreGive(c->mutex);
#else
    LeaveCriticalSection(&c->mutex);
#endif
}
static void signal_worker(lw_file_cache_t *c) {
#ifdef ESP_PLATFORM
    xSemaphoreGive(c->event);
#else
    SetEvent(c->event);
#endif
}
static void *pixels(size_t bytes) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}
bool lw_storage_relative_path(const char *path)
{
    if (!path || !*path || strlen(path)>=128 || path[0]=='/' || path[strlen(path)-1]=='/') return false;
    const char *part=path;
    for (const char *p=path;;p++) {
        unsigned char ch=(unsigned char)*p;
        if (!ch || ch=='/') {
            size_t n=(size_t)(p-part); if (!n || (n==1 && *part=='.') || (n==2 && part[0]=='.' && part[1]=='.')) return false;
            if (!ch) break;
            part=p+1;
        } else if (!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.')) return false;
    }
    return true;
}
static bool make_key(lw_file_cache_t *c,const char *file,uint64_t offset,size_t bytes,cache_key_t *key)
{
    if (!c || !lw_storage_relative_path(file) || !bytes || bytes>MAX_FRAME || offset>INT32_MAX || bytes>(uint64_t)INT32_MAX-offset) return false;
    memset(key,0,sizeof(*key)); strcpy(key->file,file); key->offset=offset; key->bytes=bytes; return true;
}
static bool same(const cache_key_t *a,const cache_key_t *b)
{ return a->offset==b->offset && a->bytes==b->bytes && a->generation==b->generation && !strcmp(a->file,b->file); }
static bool read_file(lw_file_cache_t *c,const cache_key_t *key,void *dst)
{
    char path[648]; if (!lw_path_join(path,sizeof(path),c->root,key->file)) return false;
    /* Injected read failure: reported exactly like a missing file, so the
     * caller keeps whatever frame is already on screen. */
    bool injected=lw_fault_should_fail(LW_FAULT_CACHE_READ);
    FILE *f=injected ? NULL : fopen(path,"rb"); bool ok=false;
    if (f) { ok=fseek(f,(long)key->offset,SEEK_SET)==0 && fread(dst,1,key->bytes,f)==key->bytes; fclose(f); }
    lock(c); c->stats.reads++; if (!ok) c->stats.failures++; unlock(c); return ok;
}
/* Called while locked. In-flight buffers count against the same byte cap. */
static entry_t *reserve(lw_file_cache_t *c,const cache_key_t *key)
{
    if (key->bytes>c->stats.budget_bytes) return NULL;
    entry_t *slot=NULL;
    for (;;) {
        slot=NULL; entry_t *old=NULL;
        for (unsigned i=0;i<ENTRIES;i++) {
            entry_t *e=&c->entries[i];
            if (!e->state) slot=e;
            /* A borrowed frame is being drawn from right now: evicting it would
             * hand the renderer freed bytes. */
            if (e->state==2 && !e->pins && (!old || e->used<old->used)) old=e;
        }
        if (slot && key->bytes<=c->stats.budget_bytes-c->stats.resident_bytes) break;
        if (!old) return NULL;
        c->stats.resident_bytes-=old->key.bytes; free(old->data); memset(old,0,sizeof(*old)); c->stats.evictions++;
    }
    slot->data=pixels(key->bytes); if (!slot->data) return NULL;
    slot->key=*key; slot->state=1; slot->used=++c->clock;
    c->stats.resident_bytes+=key->bytes; c->stats.loading++;
    if (c->stats.resident_bytes>c->stats.peak_bytes) c->stats.peak_bytes=c->stats.resident_bytes;
    return slot;
}
static bool load_key(lw_file_cache_t *c,cache_key_t key,void *dst)
{
    lock(c); key.generation=c->generation;
    for (unsigned i=0;i<ENTRIES;i++) {
        entry_t *e=&c->entries[i];
        if (e->state && same(&e->key,&key)) {
            if (e->state==2) { e->used=++c->clock; c->stats.hits++; if (dst) memcpy(dst,e->data,key.bytes); unlock(c); return true; }
            /* A worker already owns this key. Demand may read into its own
             * destination without waiting for worker I/O or extra cache RAM. */
            c->stats.misses++; unlock(c); return dst ? read_file(c,&key,dst) : true;
        }
    }
    c->stats.misses++; entry_t *e=reserve(c,&key); unlock(c);
    if (!e) return dst ? read_file(c,&key,dst) : false;
    bool ok=read_file(c,&key,e->data);
    lock(c); c->stats.loading--;
    if (ok && dst) memcpy(dst,e->data,key.bytes);
    if (ok && key.generation==c->generation && !c->stop) { e->state=2; if (!dst) c->stats.prefetched++; }
    else { c->stats.resident_bytes-=e->key.bytes; free(e->data); memset(e,0,sizeof(*e)); }
    unlock(c); return ok;
}
const uint8_t *lw_file_cache_borrow(lw_file_cache_t *c,const char *relative,uint64_t offset,size_t bytes)
{
    if (!c) return NULL;
    cache_key_t key;
    if (!make_key(c,relative,offset,bytes,&key)) return NULL;
    lock(c); key.generation=c->generation;
    for (unsigned i=0;i<ENTRIES;i++) {
        entry_t *e=&c->entries[i];
        if (e->state==2 && same(&e->key,&key)) {
            e->pins++; e->used=++c->clock; c->stats.hits++; c->stats.borrows++;
            const uint8_t *data=e->data; unlock(c); return data;
        }
    }
    /* Not resident. Claim a slot before reading so the bytes can stay pinned;
     * reserve() only skips pinned and in-flight entries, so a cache that is
     * entirely borrowed returns NULL here and the caller copies through
     * lw_file_cache_read as before. */
    c->stats.misses++; c->stats.borrow_misses++;
    entry_t *e=reserve(c,&key); unlock(c);
    if (!e) return NULL;
    bool ok=read_file(c,&key,e->data);
    lock(c); c->stats.loading--;
    if (ok && key.generation==c->generation && !c->stop) {
        e->state=2; e->pins=1; e->used=++c->clock;
        unlock(c); return e->data;
    }
    c->stats.resident_bytes-=e->key.bytes; free(e->data); memset(e,0,sizeof(*e));
    unlock(c); return NULL;
}
void lw_file_cache_unborrow(lw_file_cache_t *c,const void *data)
{
    if (!c || !data) return;
    lock(c);
    for (unsigned i=0;i<ENTRIES;i++) {
        entry_t *e=&c->entries[i];
        if (e->data==data) { if (e->pins) e->pins--; break; }
    }
    unlock(c);
}
static void worker_loop(lw_file_cache_t *c){
    for (;;) {
        cache_key_t key={0}; bool work=false;
        lock(c); bool stop=c->stop;
        if (!stop && c->count) { key=c->queue[c->head]; c->head=(c->head+1)%QUEUE; c->count--; work=true; c->active=true; }
        unlock(c); if (stop) break;
        if (work) { /* Invalidation may have dropped the request before dispatch. */
            lock(c); bool current=key.generation==c->generation; unlock(c);
            if (current) (void)load_key(c,key,NULL);
            lock(c); c->active=false; unlock(c);
        } else {
#ifdef ESP_PLATFORM
            xSemaphoreTake(c->event,portMAX_DELAY);
#else
            WaitForSingleObject(c->event,INFINITE);
#endif
        }
    }
}
#ifdef ESP_PLATFORM
static void worker(void *arg) { lw_file_cache_t *c=arg; worker_loop(c); xSemaphoreGive(c->done); vTaskDelete(NULL); }
#else
static DWORD WINAPI worker(void *arg) { worker_loop(arg); return 0; }
#endif
lw_file_cache_t *lw_file_cache_open(const char *root,size_t budget)
{
    struct stat info;
    if (!root || !*root || strlen(root)>=512 || !budget || budget>16u*1024u*1024u || stat(root,&info) || !(info.st_mode&S_IFDIR)) return NULL;
    lw_file_cache_t *c=calloc(1,sizeof(*c)); if (!c) return NULL;
    strcpy(c->root,root); c->stats.budget_bytes=budget; c->stats.metadata_bytes=sizeof(*c);
#ifdef ESP_PLATFORM
    c->mutex=xSemaphoreCreateMutex(); c->event=xSemaphoreCreateBinary(); c->done=xSemaphoreCreateBinary();
    if (!c->mutex || !c->event || !c->done) {
        if (c->mutex) vSemaphoreDelete(c->mutex);
        if (c->event) vSemaphoreDelete(c->event);
        if (c->done) vSemaphoreDelete(c->done);
        free(c); return NULL;
    }
    if (xTaskCreate(worker,"lw-read-ahead",4096,c,3,&c->worker)!=pdPASS) c->worker=NULL;
#else
    InitializeCriticalSection(&c->mutex); c->event=CreateEventA(NULL,FALSE,FALSE,NULL);
    if (c->event) c->worker=CreateThread(NULL,0,worker,c,0,NULL);
#endif
    if (!c->worker) { lw_file_cache_close(c); return NULL; }
    return c;
}
bool lw_file_cache_read(lw_file_cache_t *c,const char *file,uint64_t offset,void *dst,size_t bytes)
{ cache_key_t key; return dst && make_key(c,file,offset,bytes,&key) && load_key(c,key,dst); }
bool lw_file_cache_prefetch(lw_file_cache_t *c,const char *file,uint64_t offset,size_t bytes)
{
    cache_key_t key; if (!make_key(c,file,offset,bytes,&key)) return false;
    lock(c); key.generation=c->generation;
    for (unsigned i=0;i<ENTRIES;i++) if (c->entries[i].state && same(&c->entries[i].key,&key)) { unlock(c); return true; }
    for (unsigned i=0;i<c->count;i++) if (same(&c->queue[(c->head+i)%QUEUE],&key)) { unlock(c); return true; }
    if (c->count==QUEUE || c->stop) { c->stats.queue_drops++; unlock(c); return false; }
    c->queue[(c->head+c->count)%QUEUE]=key; c->count++; unlock(c); signal_worker(c); return true;
}
void lw_file_cache_invalidate(lw_file_cache_t *c)
{
    if (!c) return;
    lock(c); c->generation++; c->head=c->count=0;
    for (unsigned i=0;i<ENTRIES;i++) if (c->entries[i].state==2) { c->stats.resident_bytes-=c->entries[i].key.bytes; free(c->entries[i].data); memset(&c->entries[i],0,sizeof(entry_t)); }
    unlock(c);
}
lw_file_cache_stats_t lw_file_cache_stats(lw_file_cache_t *c)
{ lw_file_cache_stats_t stats={0}; if (c) { lock(c); stats=c->stats; stats.pending=c->count+(c->active ? 1u : 0u); unlock(c); } return stats; }
void lw_file_cache_close(lw_file_cache_t *c)
{
    if (!c) return;
    lock(c); c->stop=true; c->count=0; unlock(c);
    if (c->worker) {
        signal_worker(c);
#ifdef ESP_PLATFORM
        xSemaphoreTake(c->done,portMAX_DELAY);
#else
        WaitForSingleObject(c->worker,INFINITE); CloseHandle(c->worker);
#endif
    }
    for (unsigned i=0;i<ENTRIES;i++) free(c->entries[i].data);
#ifdef ESP_PLATFORM
    vSemaphoreDelete(c->mutex); vSemaphoreDelete(c->event); vSemaphoreDelete(c->done);
#else
    if (c->event) CloseHandle(c->event);
    DeleteCriticalSection(&c->mutex);
#endif
    free(c);
}
