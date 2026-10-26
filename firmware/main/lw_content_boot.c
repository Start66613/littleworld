/* Boot-time content: card file -> memories page and anniversary.
 *
 * The content file is the only place a user can put their own words on the
 * device without rebuilding the firmware. It is therefore treated like any
 * other external input: read once at boot, validated by lw_content_parse(),
 * and only applied when it parses cleanly. A missing card, a missing file, a
 * file saved as UTF-16 or one with a typo in a date must all leave the device
 * exactly as it was - the built-in demo table stays, the saved anniversary
 * stays - because a content mistake must never cost a boot.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_content_boot.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "lw_content.h"
#include "lw_resource.h"
#include "lw_ui_content.h"

static const char *TAG = "lw_content";

/* "littleworld/" lives on the card under the verified root. */
#define LW_CONTENT_DIR "littleworld"
#define LW_CONTENT_FILE "content.lwcx"

static char s_path[192];
static char s_report[192];
static bool s_loaded;

bool lw_content_boot(void)
{
    s_loaded = false;
    s_report[0] = '\0';

    const char *root = lw_resource_root();
    if (!root || !root[0]) {
        /* No card, or safe mode forced it off. Not an error worth a warning:
         * most boots without a card are normal. */
        (void)snprintf(s_report, sizeof(s_report), "content: no card (%s)",
                       lw_resource_status_text(lw_resource_report()->status));
        ESP_LOGI(TAG, "%s", s_report);
        return false;
    }

    int n = snprintf(s_path, sizeof(s_path), "%s/%s/%s", root, LW_CONTENT_DIR, LW_CONTENT_FILE);
    if (n < 0 || (size_t)n >= sizeof(s_path)) {
        (void)snprintf(s_report, sizeof(s_report), "content: path too long");
        ESP_LOGW(TAG, "%s", s_report);
        s_path[0] = '\0';
        return false;
    }

    if (!lw_content_load(s_path)) {
        const lw_content_report_t *report = lw_content_report();
        (void)snprintf(s_report, sizeof(s_report), "content: %s (line %u)",
                       lw_content_status_text(report->status), report->rejected);
        /* A missing file is the normal case on a card that only carries art. */
        if (report->status == LW_CONTENT_MISSING) {
            ESP_LOGI(TAG, "%s", s_report);
        } else {
            ESP_LOGW(TAG, "%s", s_report);
        }
        return false;
    }

    const lw_content_report_t *report = lw_content_report();
    (void)snprintf(s_report, sizeof(s_report),
                   "content: v%u, %u memories, %u photos, %u letters, %u dialogues",
                   report->content_version, report->memories, report->photos, report->letters,
                   report->dialogues);
    ESP_LOGI(TAG, "%s", s_report);
    s_loaded = true;
    return true;
}

void lw_content_boot_apply(void)
{
    if (!s_loaded) {
        return;
    }

    /* The memories page borrows the array, so this has to happen while the
     * page does not exist. lw_memories_set_source() refuses once it does, and
     * that refusal is a bug in the boot order rather than a user error. */
    const lw_memory_t *memories = lw_content_memories();
    unsigned count = lw_content_memory_count();
    if (memories && count) {
        if (!lw_memories_set_source(memories, count)) {
            ESP_LOGW(TAG, "content: memories rejected by the UI (boot order or limits)");
        }
    }

    /* The anniversary is a persisted setting, so only take the file's value
     * when the file actually has one. Overwriting a date the user set on the
     * device with a date from a card they forgot about would be a surprise, so
     * the UI is asked to compare first and it decides whether anything moved. */
    const lw_content_t *content = lw_content_get();
    lw_date_t date = content->anniversary;
    if (lw_date_valid(date)) {
        lw_date_t previous = lw_ui_anniversary();
        if (lw_ui_set_anniversary(date)) {
            ESP_LOGI(TAG, "content: anniversary %04d-%02d-%02d -> %04d-%02d-%02d",
                     previous.year, previous.month, previous.day, date.year, date.month, date.day);
        }
    }
}

const char *lw_content_boot_report(void)
{
    return s_report[0] ? s_report : "content: not read";
}
