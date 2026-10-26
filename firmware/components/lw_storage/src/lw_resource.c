/* Resource root resolution and the littleworld-v4.json manifest check.
 *
 * Standard C only: the same code answers for a Windows directory and for a
 * mounted FAT volume. SPDX-License-Identifier: MIT */
#include "lw_resource.h"
#include "lw_fault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_root[256];
static lw_resource_report_t s_report;

static bool read_head(const char *path, uint8_t *out, size_t bytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(out, 1, bytes, f) == bytes;
    fclose(f);
    return ok;
}

static bool join(char *dst, size_t cap, const char *name)
{
    int n = snprintf(dst, cap, "%s/%s", s_root, name);
    return n > 0 && (size_t)n < cap;
}

/* The manifest is deliberately tiny and line oriented; pulling a JSON parser
 * into the firmware for six integers would be a bigger liability than this. */
static bool manifest_line_value(const char *key, const char *block, char *out, size_t cap)
{
    const char *at = strstr(block, key);
    if (!at) return false;
    at = strchr(at, ':');
    if (!at) return false;
    do { at++; } while (*at == ' ' || *at == '\t' || *at == '"');
    size_t n = 0;
    while (at[n] && at[n] != '"' && at[n] != ',' && at[n] != '\n' && at[n] != '\r' && n + 1 < cap) {
        out[n] = at[n];
        n++;
    }
    out[n] = '\0';
    return n > 0;
}

static bool verify_manifest(char *manifest, size_t cap)
{
    if (!join(manifest, cap, LW_RESOURCE_MANIFEST_NAME)) return false;
    FILE *f = fopen(manifest, "rb");
    if (!f) {
        s_report.status = LW_RESOURCE_MANIFEST_MISSING;
        return false;
    }
    char block[1024];
    size_t got = fread(block, 1, sizeof(block) - 1, f);
    fclose(f);
    block[got] = '\0';

    char value[16];
    if (!manifest_line_value("\"schema\"", block, value, sizeof(value))) {
        s_report.status = LW_RESOURCE_MANIFEST_INVALID;
        return false;
    }
    s_report.schema = (unsigned)strtoul(value, NULL, 10);
    if (!manifest_line_value("\"version\"", block, value, sizeof(value))) {
        s_report.status = LW_RESOURCE_MANIFEST_INVALID;
        return false;
    }
    s_report.pack_version = (unsigned)strtoul(value, NULL, 10);

    if (s_report.schema != LW_RESOURCE_SCHEMA) {
        s_report.status = LW_RESOURCE_VERSION_MISMATCH;
        return false;
    }
    /* A NEWER pack may carry clips this firmware does not know how to play, so
     * it is refused rather than partially read. An OLDER pack is accepted only
     * down to the version that introduced the LWPX/LWSR pair. */
    if (s_report.pack_version > LW_RESOURCE_PACK_VERSION || s_report.pack_version < 1u) {
        s_report.status = LW_RESOURCE_VERSION_MISMATCH;
        return false;
    }
    return true;
}

static bool scene_ok(unsigned theme, bool thumbnail)
{
    char name[64], path[320];
    snprintf(name, sizeof(name), "scene-%u%s.lwr", theme, thumbnail ? "-thumb" : "");
    if (!join(path, sizeof(path), name)) return false;
    uint8_t header[16];
    if (!read_head(path, header, sizeof(header))) return false;
    if (memcmp(header, "LWSR0001", 8) != 0) return false;
    unsigned w = (unsigned)header[8] | (unsigned)header[9] << 8;
    unsigned h = (unsigned)header[10] | (unsigned)header[11] << 8;
    return w == (thumbnail ? 256u : 1024u) && h == (thumbnail ? 150u : 600u);
}

const lw_resource_report_t *lw_resource_report(void) { return &s_report; }

const char *lw_resource_status_text(lw_resource_status_t status)
{
    switch (status) {
    case LW_RESOURCE_OK: return "资源已验证";
    case LW_RESOURCE_NOT_CONFIGURED: return "未配置资源目录";
    case LW_RESOURCE_MOUNT_FAILED: return "存储卡未挂载";
    case LW_RESOURCE_ROOT_MISSING: return "资源目录缺失";
    case LW_RESOURCE_MANIFEST_MISSING: return "资源清单缺失";
    case LW_RESOURCE_MANIFEST_INVALID: return "资源清单损坏";
    case LW_RESOURCE_VERSION_MISMATCH: return "资源版本不兼容";
    }
    return "资源状态未知";
}

void lw_resource_set_root(const char *root)
{
    if (!root || strlen(root) >= sizeof(s_root)) {
        s_root[0] = '\0';
        s_report.status = LW_RESOURCE_NOT_CONFIGURED;
        s_report.root[0] = '\0';
        return;
    }
    strcpy(s_root, root);
    strcpy(s_report.root, root);
}

const char *lw_resource_root(void) { return s_root; }

void lw_resource_flag(lw_resource_status_t status)
{
    s_report.status = status;
    s_report.verified = false;
    s_report.themes = 0;
}

bool lw_resource_fallback(void) { return s_report.status != LW_RESOURCE_OK; }

bool lw_resource_verify(void)
{
    memset(&s_report, 0, sizeof(s_report));
    strcpy(s_report.root, s_root);
    s_report.themes = 0;
    if (!s_root[0]) {
        s_report.status = LW_RESOURCE_NOT_CONFIGURED;
        return false;
    }
    char manifest[320];
    if (lw_fault_should_fail(LW_FAULT_RESOURCE_VERIFY)) {
        /* Injected payload corruption: the card is there, the manifest is not
         * usable. The caller must fall back to the labelled UI, not to a blank
         * screen and not to a mount error. */
        s_report.status = LW_RESOURCE_MANIFEST_INVALID;
        return false;
    }
    if (!verify_manifest(manifest, sizeof(manifest))) return false;

    for (unsigned t = 0; t < LW_RESOURCE_THEMES; t++) {
        if (scene_ok(t, false) && scene_ok(t, true)) s_report.themes |= 1u << t;
    }
    if (!s_report.themes) {
        /* The manifest can be right while every payload is missing; that is a
         * broken medium, not a version problem, and the UI must say so. */
        s_report.status = LW_RESOURCE_MANIFEST_INVALID;
        return false;
    }
    s_report.status = LW_RESOURCE_OK;
    s_report.verified = true;
    return true;
}
