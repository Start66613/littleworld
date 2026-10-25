/* Bounded read-only file frame cache. No LVGL calls on the worker. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct lw_file_cache lw_file_cache_t;
typedef struct {
    size_t budget_bytes,resident_bytes,peak_bytes,metadata_bytes;
    uint32_t hits,misses,reads,failures,evictions,prefetched,queue_drops;
    unsigned pending,loading;
} lw_file_cache_stats_t;
/* Existing directory only. Root is caller-selected, not scanned or created.
 * Up to four frame buffers and eight queued read-ahead requests. Pixels use
 * PSRAM-only allocation on ESP; allocation failure falls back to uncached I/O. */
lw_file_cache_t *lw_file_cache_open(const char *root,size_t budget_bytes);
bool lw_file_cache_read(lw_file_cache_t *,const char *relative,uint64_t offset,void *dst,size_t bytes);
bool lw_file_cache_prefetch(lw_file_cache_t *,const char *relative,uint64_t offset,size_t bytes);
/* Invalidates READY data immediately; in-flight old-generation results are discarded. */
void lw_file_cache_invalidate(lw_file_cache_t *);
lw_file_cache_stats_t lw_file_cache_stats(lw_file_cache_t *);
/* LVGL-owner call, no simultaneous demand read. Cancels queue, joins worker,
 * then releases pixels/handles. Close all caches before unmounting storage. */
void lw_file_cache_close(lw_file_cache_t *);
bool lw_storage_relative_path(const char *);
