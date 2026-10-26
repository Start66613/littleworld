/* Little World V4.1 resource root
 *
 * This directory is the SINGLE place that answers "where do the V4 runtime
 * resources live on this target?". Everything else (scenes, character packs,
 * the manifest check) asks lw_resource_root() instead of guessing a path.
 *
 * Directory layout expected under the resolved root:
 *
 *   littleworld-v4.json          version manifest (schema below)
 *   scene-0.lwr                  theme 0 = sakura-morning    (LWSR0001 1024x600)
 *   scene-1.lwr                  theme 1 = moonlight-dream   (LWSR0001 1024x600)
 *   scene-2.lwr                  theme 2 = strawberry-sunset (LWSR0001 1024x600)
 *   scene-0-thumb.lwr ...        256x150 thumbnails, one per theme
 *   male.lwp                     LWPX0001 pack: idle / blink / wave
 *   qianqian-wave.lwp            LWPX0001 pack: wave
 *
 * The theme numbers are not negotiable: they come from assets/visual-v4/
 * SCENES.json and are what the UI palette table and the settings enum already
 * encode. Reordering the files here reorders the product.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LW_RESOURCE_OK = 0,
    LW_RESOURCE_NOT_CONFIGURED, /* nobody set a root: nothing was ever looked for */
    LW_RESOURCE_MOUNT_FAILED,   /* the storage joined nothing: no card, no volume */
    LW_RESOURCE_ROOT_MISSING,   /* storage is up but the directory is not there */
    LW_RESOURCE_MANIFEST_MISSING,
    LW_RESOURCE_MANIFEST_INVALID,
    LW_RESOURCE_VERSION_MISMATCH,
} lw_resource_status_t;

typedef struct {
    lw_resource_status_t status;
    /* Where the assets were found; empty when nothing was found. */
    char root[256];
    /* The manifest's own values, when one was read. */
    unsigned schema;
    unsigned pack_version;
    /* Which of the three themes actually exist on this medium. */
    unsigned themes;
    /* True when the assets were verified this run, false when only looked up. */
    bool verified;
} lw_resource_report_t;

/* Registered by the platform entry point. The root is copied. */
void lw_resource_set_root(const char *root);
const char *lw_resource_root(void);

/* What the last lw_resource_verify() found. Never NULL. */
const lw_resource_report_t *lw_resource_report(void);
const char *lw_resource_status_text(lw_resource_status_t status);

/* Full check: root present, manifest readable and version-compatible, scene
 * files present with a valid 16-byte LWSR header. Cheap enough to call once at
 * boot; it reads a handful of headers, not the 12 MB payload. */
bool lw_resource_verify(void);

/* Marked when the medium arrived without a usable resource set. The UI shows
 * the labelled fallback instead of pretending the art loaded. */
void lw_resource_flag(lw_resource_status_t status);
bool lw_resource_fallback(void);

/* Identity of the format the firmware knows. A pack built for a different
 * contract is refused rather than half-read. */
#define LW_RESOURCE_SCHEMA 1u
#define LW_RESOURCE_PACK_VERSION 4u
#define LW_RESOURCE_MANIFEST_NAME "littleworld-v4.json"
#define LW_RESOURCE_THEMES 3u

#ifdef __cplusplus
}
#endif
