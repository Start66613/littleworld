/* Little World - the V4.2 content container ("LWCX").
 *
 * Why a text container and not JSON: the firmware has no JSON parser and
 * pulling one in for a handful of strings would be a bigger liability than
 * this line scanner. The PC side does the hard work (UTF-8 validation, length
 * limits, date checks) and emits exactly the shape this parser accepts, so a
 * bad file is caught on the computer, not on the device.
 *
 * Format (UTF-8, LF or CRLF, `#` starts a comment line):
 *
 *   LWCX0001
 *   schema=1
 *   content_version=1
 *   anniversary=2024-10-25
 *   welcome=第一行\n第二行            (optional, \n is a literal two chars)
 *
 *   [memory]
 *   date=2024-10-25
 *   title=故事开始
 *   body=示例内容：……
 *   photo=photos/001.lwph            (optional, "" when there is no picture)
 *   [/memory]
 *
 *   [album]
 *   title=海边
 *   photo=photos/002.lwph
 *   [/album]
 *
 *   [letter]
 *   title=给未来的你
 *   body=……
 *   [/letter]
 *
 *   [dialogue]
 *   id=10
 *   line=男方|照片盒里有一叠空白的相纸。
 *   [/dialogue]
 *
 * Nothing here invents content: every string in a file comes from the user.
 * The sample that ships in the repository is marked 示例内容 everywhere.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lw/lw_memories.h"
#include "lw/lw_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest accepted file. A content file is text; 32 KiB is already enormous
 *  (32 memories with full-length bodies fit comfortably). */
#define LW_CONTENT_MAX_BYTES 32768u
#define LW_CONTENT_MAX_MEMORIES 32u
#define LW_CONTENT_MAX_PHOTOS 64u
#define LW_CONTENT_MAX_LETTERS 8u
#define LW_CONTENT_MAX_DIALOGUES 16u
#define LW_CONTENT_MAX_LINES 64u
#define LW_CONTENT_TITLE_MAX 128u
#define LW_CONTENT_BODY_MAX 512u
#define LW_CONTENT_PATH_MAX 96u
/* A speaker label is a name, not a sentence. 24 UTF-8 bytes is eight Chinese
 * characters: enough for "男方"/"阿妈"/"陌生女人" plus a qualifier, and small
 * enough that a runaway string cannot eat the arena. The first limit here was
 * 16, which rejected the Studio demo package's own speaker label - a real
 * conversion failure, found by running the bridge against it. */
#define LW_CONTENT_SPEAKER_MAX 24u

typedef enum {
    LW_CONTENT_OK = 0,
    LW_CONTENT_MISSING,      /* no file at the path */
    LW_CONTENT_TOO_LARGE,    /* over LW_CONTENT_MAX_BYTES */
    LW_CONTENT_BAD_MAGIC,    /* not LWCX0001 */
    LW_CONTENT_BAD_SCHEMA,   /* schema != 1 */
    LW_CONTENT_BAD_VERSION,  /* content_version outside 1..LW_CONTENT_VERSION */
    LW_CONTENT_BAD_UTF8,     /* invalid or truncated multi-byte sequence */
    LW_CONTENT_BAD_DATE,     /* a date that lw_date_valid() rejects */
    LW_CONTENT_TOO_MANY,     /* a count or a string over its limit */
    LW_CONTENT_MALFORMED     /* unknown key, unclosed section, stray value */
} lw_content_status_t;

/** Bump when the on-card shape changes in a way the firmware must know about. */
#define LW_CONTENT_VERSION 1u
#define LW_CONTENT_SCHEMA 1u

/** Album index entry. The picture itself is not decoded here. */
typedef struct {
    const char *title;
    const char *path;
} lw_content_photo_t;

typedef struct {
    const char *title;
    const char *body;
} lw_content_letter_t;

typedef struct {
    const char *speaker;
    const char *text;
} lw_content_line_t;

typedef struct {
    int id;
    unsigned first_line;
    unsigned line_count;
} lw_content_dialogue_t;

typedef struct {
    unsigned content_version;
    lw_date_t anniversary;
    const char *welcome;
    const lw_memory_t *memories;
    unsigned memory_count;
    const lw_content_photo_t *photos;
    unsigned photo_count;
    const lw_content_letter_t *letters;
    unsigned letter_count;
    const lw_content_dialogue_t *dialogues;
    unsigned dialogue_count;
    const lw_content_line_t *lines;
    unsigned line_count;
} lw_content_t;

typedef struct {
    lw_content_status_t status;
    unsigned content_version;
    unsigned memories, photos, letters, dialogues, lines;
    unsigned bytes;      /* size of the accepted file */
    unsigned rejected;   /* first offending line, 0 when unknown */
    char source[128];    /* path the report refers to, may be "" */
} lw_content_report_t;

/**
 * Parse @p text (which need not be NUL terminated) into the module's static
 * arena. On success every pointer in lw_content_get() points into that arena
 * and stays valid until the next parse or clear. On failure the previous
 * content is discarded: a half-parsed file is never handed out.
 */
bool lw_content_parse(const char *text, size_t length);

/** Read and parse a file. Sets LW_CONTENT_MISSING when it cannot be opened. */
bool lw_content_load(const char *path);

/** The file name the device looks for, with no directory part. */
#define LW_CONTENT_FILE_NAME "content.lwcx"

/**
 * Build the absolute path of the content file inside @p root.
 *
 * The resource root ALREADY ends in "littleworld" (/sdcard/littleworld on the
 * board), so the content file sits directly under it. This is the single place
 * that turns a root into a content path, so the device loader and the tests
 * cannot disagree about the shape - the bug this function replaces appended
 * "littleworld/" a second time and asked for a directory no card contains.
 *
 * @p root is normally lw_resource_root(). Returns false and writes an empty
 * string when the root is empty, the name would not fit, or the join rule in
 * lw_path.h rejects the root.
 */
bool lw_content_resolve_path(char *dst, size_t cap, const char *root);

/** Drop the current content; lw_content_get() then reports zero entries. */
void lw_content_clear(void);

/** Never NULL. Points at an empty model when nothing was parsed. */
const lw_content_t *lw_content_get(void);

/** Report of the last parse/load attempt. Never NULL. */
const lw_content_report_t *lw_content_report(void);

const char *lw_content_status_text(lw_content_status_t status);

/**
 * The memories as an array the UI can borrow directly, or NULL when there are
 * none. This is the same lw_memory_t the built-in DEMO table uses, so the
 * existing lw_memories_set_source() accepts it unchanged.
 */
const lw_memory_t *lw_content_memories(void);
unsigned lw_content_memory_count(void);

/** True when the text is well formed UTF-8 (no overlongs, no surrogates). */
bool lw_content_utf8_valid(const char *text, size_t length);

#ifdef __cplusplus
}
#endif
