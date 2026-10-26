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
/* Sets the anniversary the desktop and lock screen count from, and persists it.
 * Returns true when the stored date actually changed. An invalid date is
 * refused, so bad content cannot make the counter read 740312 days again. */
bool lw_ui_set_anniversary(lw_date_t date);
/* The stored anniversary, for callers that only want to read it. */
lw_date_t lw_ui_anniversary(void);
void lw_memories_open_tab(int);
bool lw_arcade_is_paused(void);
uint32_t lw_arcade_steps(void);
void lw_music_shared_volume_changed(void);
