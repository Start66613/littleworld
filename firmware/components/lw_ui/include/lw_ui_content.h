/* UI-thread, borrowed sources. No filesystem or hardware initialization. */
#pragma once
#include "lvgl.h"
#include "lw/lw_memories.h"
typedef struct { const lv_image_dsc_t *image; void *token; } lw_album_picture_t;
typedef struct {
    void *context; unsigned count;
    const char *(*title)(void *,unsigned);
    bool (*load)(void *,unsigned,lw_album_picture_t *);
    void (*release)(void *,lw_album_picture_t *);
} lw_album_source_t;
/* Set while closed; at most one decoded photo. Loader owns decode/budget.
 * Source and immutable images outlive use; NULL means no real photos. */
bool lw_album_set_source(const lw_album_source_t *);
unsigned lw_album_live_pictures(void);
bool lw_memories_set_source(const lw_memory_t *,unsigned);
void lw_memories_open_tab(int);
bool lw_arcade_is_paused(void);
uint32_t lw_arcade_steps(void);
