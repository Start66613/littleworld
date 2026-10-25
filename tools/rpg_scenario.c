/* Scripted RPG scenario: drives the core and prints one JSON snapshot per
 * checkpoint so the Python preview renderer can draw real states.
 *
 * Build (MSVC): cl /nologo /utf-8 /W4 /WX /I rpg\include /I rpg\data
 *   tools\rpg_scenario.c rpg\src\lw_rpg.c rpg\src\lw_rpg_save.c
 *   rpg\data\lw_rpg_home.c /Fe:build\rpg\rpg_scenario.exe /Fo:build\rpg\
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_rpg.h"
#include "lw_rpg_demo.h"

static void emit(const char *tag, const lw_rpg_state_t *s)
{
    const lw_rpg_dialogue_t *d = lw_rpg_dialogue_current(s);
    const lw_rpg_line_t *line = lw_rpg_dialogue_line(s);
    char speaker[64] = "";
    char text[256] = "";
    if (line != NULL) {
        snprintf(speaker, sizeof(speaker), "%s", line->speaker);
        snprintf(text, sizeof(text), "%s", line->text);
    }
    printf("{\"tag\":\"%s\",\"px\":%d,\"py\":%d,\"qx\":%d,\"qy\":%d,"
           "\"moving\":%d,\"progress\":%d,\"total\":%d,\"complete\":%d,"
           "\"dialogue\":%d,\"line\":%d,\"speaker\":\"%s\",\"text\":\"%s\"}\n",
           tag, s->actors[0].pos.x, s->actors[0].pos.y, s->actors[1].pos.x,
           s->actors[1].pos.y, s->actors[0].moving, lw_rpg_quest_progress(s),
           lw_rpg_quest_total(s), lw_rpg_quest_is_complete(s) ? 1 : 0,
           s->dialogue_active ? 1 : 0, (int)s->line_index, speaker, text);
    (void)d;
}

static void ticks(lw_rpg_state_t *s, int n)
{
    for (int i = 0; i < n; i++) {
        lw_rpg_tick(s, 140);
    }
}

int main(void)
{
    lw_rpg_state_t s;
    if (!lw_rpg_init(&s, lw_rpg_demo_world())) {
        printf("{\"error\":\"init\"}\n");
        return 1;
    }
    emit("spawn", &s);

    /* walk toward the photo box */
    lw_rpg_tap_tile(&s, 5, 3);
    ticks(&s, 2);
    emit("walking", &s);
    ticks(&s, 6);
    emit("at_photo", &s);

    lw_rpg_interact(&s);
    emit("dialogue_photo", &s);
    lw_rpg_dialogue_advance(&s);
    emit("dialogue_photo_line2", &s);
    lw_rpg_dialogue_advance(&s);
    emit("photo_done", &s);

    /* letter desk */
    lw_rpg_tap_tile(&s, 9, 4);
    ticks(&s, 14);
    lw_rpg_interact(&s);
    emit("dialogue_letter", &s);
    lw_rpg_dialogue_advance(&s);
    lw_rpg_dialogue_advance(&s);
    emit("letter_done", &s);

    /* plant completes the quest */
    lw_rpg_tap_tile(&s, 3, 3);
    ticks(&s, 26);
    lw_rpg_interact(&s);
    emit("dialogue_plant", &s);
    lw_rpg_dialogue_advance(&s);
    emit("quest_complete", &s);

    /* save -> load, prove the state survives */
    uint8_t blob[LW_RPG_SAVE_BYTES];
    int n = lw_rpg_save_encode(&s, blob, sizeof(blob));
    lw_rpg_state_t loaded;
    lw_rpg_init(&loaded, lw_rpg_demo_world());
    int rc = lw_rpg_save_decode(&loaded, blob, sizeof(blob));
    printf("{\"tag\":\"save_check\",\"bytes\":%d,\"decode\":%d,\"progress\":%d,\"complete\":%d}\n",
           n, rc, lw_rpg_quest_progress(&loaded), lw_rpg_quest_is_complete(&loaded) ? 1 : 0);
    return 0;
}
