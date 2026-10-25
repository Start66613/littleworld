/* Substitute NVS backend tests for the framed record store.
 *
 * The fake NVS models the behaviours that matter on the device: first boot,
 * corrupted or truncated records, a full partition, failing writes and a
 * reboot. It also proves that new records never touch the legacy namespace.
 *
 * SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lw_rpg.h"
#include "lw_store.h"
#include "lw_system.h"

static unsigned checks, failures;
#define CHECK(name, value)                 \
    do {                                   \
        checks++;                          \
        if (!(value)) {                    \
            failures++;                    \
            printf("FAIL: %s\n", name);    \
        }                                  \
    } while (0)

/* ------------------------------------------------------------- fake NVS --- */

#define FAKE_SLOTS 4
#define FAKE_BYTES 128

typedef struct {
    char ns[16];
    char key[16];
    uint8_t data[FAKE_BYTES];
    size_t length;
    bool used;
} fake_slot_t;

typedef struct {
    fake_slot_t slots[FAKE_SLOTS];
    bool fail_write;   /* nvs_set_blob/nvs_commit error */
    bool partition_full;
} fake_nvs_t;

static fake_nvs_t flash;

static fake_slot_t *find(const char *ns, const char *key, bool create)
{
    for (int i = 0; i < FAKE_SLOTS; i++) {
        if (flash.slots[i].used && !strcmp(flash.slots[i].ns, ns) && !strcmp(flash.slots[i].key, key)) {
            return &flash.slots[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < FAKE_SLOTS; i++) {
        if (!flash.slots[i].used) {
            flash.slots[i].used = true;
            strncpy(flash.slots[i].ns, ns, sizeof(flash.slots[i].ns) - 1);
            strncpy(flash.slots[i].key, key, sizeof(flash.slots[i].key) - 1);
            flash.slots[i].length = 0;
            return &flash.slots[i];
        }
    }
    return NULL;
}

typedef struct {
    const char *ns;
    const char *key;
} fake_key_t;

static bool fake_read(void *context, uint8_t *out, size_t capacity, size_t *length)
{
    const fake_key_t *key = (const fake_key_t *)context;
    if (flash.partition_full) {
        return false; /* a full partition cannot even open a handle */
    }
    fake_slot_t *slot = find(key->ns, key->key, false);
    if (slot == NULL || slot->length == 0 || slot->length > capacity) {
        return false;
    }
    memcpy(out, slot->data, slot->length);
    *length = slot->length;
    return true;
}

static bool fake_write(void *context, const uint8_t *data, size_t length)
{
    const fake_key_t *key = (const fake_key_t *)context;
    if (flash.fail_write || flash.partition_full) {
        return false;
    }
    fake_slot_t *slot = find(key->ns, key->key, true);
    if (slot == NULL || length > FAKE_BYTES) {
        return false;
    }
    memcpy(slot->data, data, length);
    slot->length = length;
    return true;
}

/* --------------------------------------------------------------- tests --- */

static fake_key_t k_prefs = {"lwo2.sys", "prefs"};
static fake_key_t k_rpg = {"lwo2.rpg", "save"};
static fake_key_t k_legacy = {"littleworld", "save"};

static lw_store_io_t prefs_io(void)
{
    lw_store_io_t io = {&k_prefs, fake_read, fake_write};
    return io;
}

static lw_store_io_t rpg_io(void)
{
    lw_store_io_t io = {&k_rpg, fake_read, fake_write};
    return io;
}

#define KIND_PREFS 1u
#define KIND_RPG 2u
#define VERSION 1u

static unsigned last_brightness;
static bool fake_brightness(void *context, unsigned value)
{
    (void)context;
    last_brightness = value;
    return value >= 10 && value <= 100;
}

static bool store_load_prefs(void *context, uint8_t out[LW_SYS_PREF_BYTES])
{
    (void)context;
    lw_store_io_t io = prefs_io();
    return lw_store_load(&io, KIND_PREFS, VERSION, out, LW_SYS_PREF_BYTES) == LW_STORE_OK;
}

static bool store_save_prefs(void *context, const uint8_t in[LW_SYS_PREF_BYTES])
{
    (void)context;
    lw_store_io_t io = prefs_io();
    return lw_store_save(&io, KIND_PREFS, VERSION, in, LW_SYS_PREF_BYTES) == LW_STORE_OK;
}

int main(void)
{
    memset(&flash, 0, sizeof(flash));
    lw_store_io_t prefs = prefs_io();
    lw_store_io_t rpg = rpg_io();

    /* First boot: nothing stored yet. */
    uint8_t prefs_bytes[LW_SYS_PREF_BYTES];
    CHECK("first boot reports missing", lw_store_load(&prefs, KIND_PREFS, VERSION, prefs_bytes,
                                                     sizeof(prefs_bytes)) == LW_STORE_MISSING);

    /* Valid system preferences roundtrip. */
    lw_preferences_t p = {55, 30, 2, 1, true, false, true};
    lw_system_encode(&p, prefs_bytes);
    CHECK("preferences save accepted", lw_store_save(&prefs, KIND_PREFS, VERSION, prefs_bytes,
                                                     sizeof(prefs_bytes)) == LW_STORE_OK);
    uint8_t readback[LW_SYS_PREF_BYTES];
    CHECK("preferences load accepted", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                     sizeof(readback)) == LW_STORE_OK);
    lw_preferences_t restored;
    CHECK("preferences decode", lw_system_decode(&restored, readback) && restored.brightness == 55 &&
                                    restored.volume == 30 && restored.theme == 2 && restored.muted);

    /* Corrupted payload: CRC must reject it and the caller keeps defaults. */
    fake_slot_t *slot = find("lwo2.sys", "prefs", false);
    CHECK("record slot exists", slot != NULL);
    uint8_t saved[FAKE_BYTES];
    size_t saved_length = slot->length;
    memcpy(saved, slot->data, saved_length);
    slot->data[10] ^= 0x5A;
    CHECK("corrupted payload rejected", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                      sizeof(readback)) == LW_STORE_BAD_CRC);
    CHECK("corruption does not erase the slot", slot->length == saved_length);
    memcpy(slot->data, saved, saved_length);

    /* Foreign and future records are refused, never rewritten. */
    uint8_t foreign[LW_STORE_MAX_RECORD];
    memset(foreign, 0x11, sizeof(foreign));
    memcpy(slot->data, foreign, 32);
    slot->length = 32;
    CHECK("foreign magic rejected", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                  sizeof(readback)) == LW_STORE_BAD_MAGIC);

    size_t written = 0;
    CHECK("encode for another version", lw_store_encode(KIND_PREFS, 9, prefs_bytes, sizeof(prefs_bytes),
                                                        foreign, sizeof(foreign), &written));
    memcpy(slot->data, foreign, written);
    slot->length = written;
    CHECK("future version rejected", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                   sizeof(readback)) == LW_STORE_BAD_VERSION);

    CHECK("encode for another consumer", lw_store_encode(KIND_RPG, VERSION, prefs_bytes,
                                                         sizeof(prefs_bytes), foreign, sizeof(foreign),
                                                         &written));
    memcpy(slot->data, foreign, written);
    slot->length = written;
    CHECK("wrong kind rejected", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                               sizeof(readback)) == LW_STORE_BAD_KIND);
    uint8_t rpg_probe[LW_RPG_SAVE_BYTES];
    CHECK("rpg reader rejects a shorter payload", lw_store_load(&prefs, KIND_RPG, VERSION, rpg_probe,
                                                               sizeof(rpg_probe)) == LW_STORE_BAD_LENGTH);

    /* Torn write: valid header, payload cut short. */
    lw_system_encode(&p, prefs_bytes);
    CHECK("encode full record", lw_store_encode(KIND_PREFS, VERSION, prefs_bytes, sizeof(prefs_bytes),
                                                foreign, sizeof(foreign), &written));
    memcpy(slot->data, foreign, written - 3);
    slot->length = written - 3;
    CHECK("torn write rejected", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                               sizeof(readback)) == LW_STORE_BAD_LENGTH);

    /* Failing writes must not claim success and must keep the previous record. */
    memcpy(slot->data, saved, saved_length);
    slot->length = saved_length;
    flash.fail_write = true;
    CHECK("failed write reports io error", lw_store_save(&prefs, KIND_PREFS, VERSION, prefs_bytes,
                                                         sizeof(prefs_bytes)) == LW_STORE_IO_ERROR);
    CHECK("failed write keeps the old record", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                             sizeof(readback)) == LW_STORE_OK);
    flash.fail_write = false;

    /* Full partition: reads and writes fail, stored bytes survive untouched. */
    flash.partition_full = true;
    CHECK("full partition load is not a valid record", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                                     sizeof(readback)) == LW_STORE_MISSING);
    CHECK("full partition save fails", lw_store_save(&prefs, KIND_PREFS, VERSION, prefs_bytes,
                                                     sizeof(prefs_bytes)) == LW_STORE_IO_ERROR);
    flash.partition_full = false;
    CHECK("data survived the full partition", lw_store_load(&prefs, KIND_PREFS, VERSION, readback,
                                                            sizeof(readback)) == LW_STORE_OK);

    /* RPG save lives in its own namespace and keeps 40 bytes. */
    uint8_t rpg_bytes[LW_RPG_SAVE_BYTES];
    for (unsigned i = 0; i < sizeof(rpg_bytes); i++) {
        rpg_bytes[i] = (uint8_t)(i * 7u + 3u);
    }
    CHECK("rpg save accepted", lw_store_save(&rpg, KIND_RPG, VERSION, rpg_bytes, sizeof(rpg_bytes)) ==
                                   LW_STORE_OK);
    uint8_t rpg_read[LW_RPG_SAVE_BYTES];
    CHECK("rpg save roundtrip", lw_store_load(&rpg, KIND_RPG, VERSION, rpg_read, sizeof(rpg_read)) ==
                                    LW_STORE_OK &&
                                   !memcmp(rpg_read, rpg_bytes, sizeof(rpg_bytes)));

    /* Legacy pet save is a different namespace and must be untouched. */
    fake_slot_t *legacy = find("littleworld", "save", true);
    CHECK("legacy slot created", legacy != NULL);
    legacy->length = 8;
    memset(legacy->data, 0xAB, legacy->length);
    CHECK("writing new records leaves the legacy blob alone", legacy->length == 8 && legacy->data[0] == 0xAB);
    CHECK("new records use their own namespaces", find("lwo2.sys", "prefs", false) != NULL &&
                                                      find("lwo2.rpg", "save", false) != NULL);

    /* Reboot: a fresh system instance restores preferences from flash. */
    lw_system_backend_t backend = {0};
    backend.load = store_load_prefs;
    backend.store = store_save_prefs;
    backend.brightness = fake_brightness;
    backend.network_available = false;
    lw_system_t system;
    lw_system_init(&system, &backend);
    CHECK("reboot restores preferences", system.prefs.brightness == 55 && system.prefs.theme == 2);
    CHECK("unsupported network is explicit", system.wifi == LW_WIFI_UNSUPPORTED);

    lw_system_brightness(&system, 88);
    CHECK("changed preference saves", lw_system_save(&system) && !system.dirty);
    lw_system_t rebooted;
    lw_system_init(&rebooted, &backend);
    CHECK("second reboot keeps the new brightness", rebooted.prefs.brightness == 88);

    printf("Store backend: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
