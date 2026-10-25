#include "lw_storage.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <direct.h>
static unsigned checks,failures;
#define CHECK(name,expr) do { checks++; if (!(expr)) { failures++; printf("FAIL %s line %d\n",name,__LINE__); } } while(0)
#define FRAME 2048
static bool idle(lw_file_cache_t *c)
{
    for (unsigned i=0;i<3000;i++) { lw_file_cache_stats_t s=lw_file_cache_stats(c); if (!s.pending && !s.loading) return true; Sleep(1); }
    return false;
}
static bool pattern(const uint8_t *data,unsigned frame)
{ for (unsigned i=0;i<FRAME;i++) if (data[i]!=(uint8_t)(frame*37+i)) return false; return true; }
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    _mkdir(argv[1]); char file[600]; snprintf(file,sizeof(file),"%s/frames.dat",argv[1]);
    FILE *f=fopen(file,"wb"); if (!f) return 2;
    for (unsigned i=0;i<16*FRAME;i++) fputc((int)((i/FRAME*37+i%FRAME)&255),f);
    fclose(f);
    const char *bad[]={"../frames.dat","a/../frames.dat","/frames.dat","C:/frames.dat","a\\frames.dat","a//b","a/",".","..",""};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK("reject unsafe relative paths",!lw_storage_relative_path(bad[i]));
    CHECK("nested ASCII paths accepted",lw_storage_relative_path("characters/boy/idle_01.dat"));
    CHECK("missing root gracefully fails",lw_file_cache_open("D:/__littleworld_missing_root__",4096)==NULL);
    lw_file_cache_t *c=lw_file_cache_open(argv[1],3*FRAME); CHECK("existing local filesystem opens",c!=NULL); if (!c) return 2;
    uint8_t data[FRAME],large[4*FRAME];
    CHECK("read precise file slice",lw_file_cache_read(c,"frames.dat",FRAME,data,FRAME) && pattern(data,1));
    uint32_t reads=lw_file_cache_stats(c).reads;
    CHECK("cached read is correct",lw_file_cache_read(c,"frames.dat",FRAME,data,FRAME) && pattern(data,1));
    CHECK("cache hit avoids I/O",lw_file_cache_stats(c).reads==reads && lw_file_cache_stats(c).hits>0);
    CHECK("enqueue actual asynchronous read",lw_file_cache_prefetch(c,"frames.dat",2*FRAME,FRAME)); CHECK("worker finishes",idle(c));
    reads=lw_file_cache_stats(c).reads;
    CHECK("prefetched bytes are correct",lw_file_cache_read(c,"frames.dat",2*FRAME,data,FRAME) && pattern(data,2));
    CHECK("prefetch produced a cache hit",lw_file_cache_stats(c).reads==reads && lw_file_cache_stats(c).prefetched>0);
    CHECK("third frame",lw_file_cache_read(c,"frames.dat",3*FRAME,data,FRAME));
    CHECK("touch LRU",lw_file_cache_read(c,"frames.dat",FRAME,data,FRAME));
    CHECK("evict least recently used",lw_file_cache_read(c,"frames.dat",4*FRAME,data,FRAME) && lw_file_cache_stats(c).evictions>0);
    reads=lw_file_cache_stats(c).reads; CHECK("recent frame survived eviction",lw_file_cache_read(c,"frames.dat",FRAME,data,FRAME) && lw_file_cache_stats(c).reads==reads);
    CHECK("larger-than-budget bypass still reads",lw_file_cache_read(c,"frames.dat",0,large,sizeof(large)) && pattern(large,0));
    CHECK("memory cap includes in-flight buffers",lw_file_cache_stats(c).peak_bytes<=3*FRAME);
    CHECK("missing file returns failure",!lw_file_cache_read(c,"missing.dat",0,data,FRAME));
    CHECK("truncated frame fails",!lw_file_cache_read(c,"frames.dat",16*FRAME-4,data,FRAME));
    CHECK("32bit offset overflow rejected",!lw_file_cache_read(c,"frames.dat",UINT64_MAX,data,FRAME));
    lw_file_cache_invalidate(c); CHECK("invalidate frees ready cache",lw_file_cache_stats(c).resident_bytes==0);
    f=fopen(file,"r+b"); if (!f) return 2; fputc(99,f); fclose(f);
    CHECK("invalidate reads changed file",lw_file_cache_read(c,"frames.dat",0,data,FRAME) && data[0]==99);
    for (unsigned i=0;i<100;i++) { (void)lw_file_cache_prefetch(c,"frames.dat",(i%16)*FRAME,FRAME); lw_file_cache_invalidate(c); }
    CHECK("invalidation finishes bounded",idle(c) && lw_file_cache_stats(c).peak_bytes<=3*FRAME);
    lw_file_cache_close(c);
    DWORD before=0,after=0; GetProcessHandleCount(GetCurrentProcess(),&before);
    bool clean=true;
    for (unsigned i=0;i<100;i++) { c=lw_file_cache_open(argv[1],FRAME); if (!c) { clean=false; break; } (void)lw_file_cache_prefetch(c,"frames.dat",FRAME,FRAME); lw_file_cache_close(c); }
    GetProcessHandleCount(GetCurrentProcess(),&after);
    CHECK("100 close cycles join active workers",clean && after<=before);
    printf("Storage: %u checks, %u failures; handles %lu -> %lu\n",checks,failures,(unsigned long)before,(unsigned long)after);
    return failures ? 1 : 0;
}
