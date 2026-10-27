/* Little World - V4.2 content container parser. See lw_content.h for the shape.
 *
 * Design notes worth keeping:
 *  - No allocation. Everything lives in one static arena, because a content
 *    file is read once at boot and the device must still start when the heap
 *    is fragmented. Failure to parse never allocates, so it cannot make things
 *    worse.
 *  - Values are copied, not referenced. The caller's buffer is usually a file
 *    image that is freed right after the call.
 *  - A failed parse clears the previous content instead of leaving a mixture.
 *    Half a content set is worse than none: the UI would show a timeline with
 *    a hole in it and no way to tell.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_content.h"
#include "lw_path.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ state */

typedef struct {
    lw_memory_t memories[LW_CONTENT_MAX_MEMORIES];
    lw_content_photo_t photos[LW_CONTENT_MAX_PHOTOS];
    lw_content_letter_t letters[LW_CONTENT_MAX_LETTERS];
    lw_content_dialogue_t dialogues[LW_CONTENT_MAX_DIALOGUES];
    lw_content_line_t lines[LW_CONTENT_MAX_LINES];
    char arena[LW_CONTENT_MAX_BYTES + 1u];
    size_t used;
} content_arena_t;

static content_arena_t s_arena;
static lw_content_t s_content;
static lw_content_report_t s_report;

typedef enum {
    SEC_NONE = 0,
    SEC_MEMORY,
    SEC_ALBUM,
    SEC_LETTER,
    SEC_DIALOGUE
} section_t;

/* --------------------------------------------------------------- helpers */

static void reset_state(void)
{
    memset(&s_arena, 0, sizeof(s_arena));
    memset(&s_content, 0, sizeof(s_content));
}

static const char *status_name(lw_content_status_t status)
{
    switch (status) {
    case LW_CONTENT_OK: return "ok";
    case LW_CONTENT_MISSING: return "missing";
    case LW_CONTENT_TOO_LARGE: return "too large";
    case LW_CONTENT_BAD_MAGIC: return "bad magic";
    case LW_CONTENT_BAD_SCHEMA: return "bad schema";
    case LW_CONTENT_BAD_VERSION: return "unsupported version";
    case LW_CONTENT_BAD_UTF8: return "invalid UTF-8";
    case LW_CONTENT_BAD_DATE: return "invalid date";
    case LW_CONTENT_TOO_MANY: return "over a limit";
    case LW_CONTENT_MALFORMED: return "malformed";
    default: return "unknown";
    }
}

/* Strict UTF-8: no overlong encodings, no surrogates, nothing above U+10FFFF.
 * The firmware prints these bytes straight to LVGL, so a broken sequence would
 * show up as a missing glyph at best and a truncated label at worst. */
bool lw_content_utf8_valid(const char *text, size_t length)
{
    if (!text) return false;
    size_t i = 0;
    while (i < length) {
        unsigned char c = (unsigned char)text[i];
        unsigned extra;
        uint32_t cp;
        if (c < 0x80u) { i++; continue; }
        if (c >= 0xC2u && c <= 0xDFu) { extra = 1; cp = c & 0x1Fu; }
        else if (c >= 0xE0u && c <= 0xEFu) { extra = 2; cp = c & 0x0Fu; }
        else if (c >= 0xF0u && c <= 0xF4u) { extra = 3; cp = c & 0x07u; }
        else return false;
        if (i + extra >= length) return false;
        for (unsigned k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)text[i + k];
            if ((cc & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
        }
        if (extra == 2 && cp < 0x800u) return false;
        if (extra == 3 && cp < 0x10000u) return false;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
        if (cp > 0x10FFFFu) return false;
        i += extra + 1u;
    }
    return true;
}

static char *arena_alloc(size_t bytes)
{
    if (s_arena.used + bytes > sizeof(s_arena.arena)) return NULL;
    char *at = &s_arena.arena[s_arena.used];
    s_arena.used += bytes;
    return at;
}

typedef enum {
    STORE_OK = 0,
    STORE_MALFORMED,  /* bad escape or a control character */
    STORE_TOO_LONG,   /* over the field limit or out of arena */
    STORE_NO_ROOM
} store_result_t;

/* Defined with the parser below; declared here because store_failed() needs it. */
static bool fail(lw_content_status_t status, unsigned line);

/* Copy a value, decoding \\ and \n. The caller must report the reason it
 * failed: "the title is too long" and "the title contains a stray backslash"
 * are different mistakes and the PC-side checker prints different advice. */
static store_result_t store_value(const char *src, size_t len, size_t limit, const char **out)
{
    /* Worst case: one output byte per input byte. */
    char *dst = arena_alloc(len + 1u);
    if (!dst) return STORE_NO_ROOM;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '\\' && i + 1u < len) {
            char next = src[i + 1u];
            if (next == 'n') { c = '\n'; i++; }
            else if (next == '\\') { c = '\\'; i++; }
            else return STORE_MALFORMED;
        }
        if ((unsigned char)c < 0x20u && c != '\n') return STORE_MALFORMED;
        if (n > limit) return STORE_TOO_LONG;
        dst[n++] = c;
    }
    if (n > limit) return STORE_TOO_LONG;
    dst[n] = '\0';
    *out = dst;
    return STORE_OK;
}

/* Map a store failure onto the status the report should carry. */
static bool store_failed(store_result_t result, unsigned line)
{
    return fail(result == STORE_MALFORMED ? LW_CONTENT_MALFORMED : LW_CONTENT_TOO_MANY, line);
}

static void trim_span(const char **begin, size_t *len)
{
    const char *b = *begin;
    size_t n = *len;
    while (n && (*b == ' ' || *b == '\t')) { b++; n--; }
    while (n && (b[n - 1u] == ' ' || b[n - 1u] == '\t')) n--;
    *begin = b;
    *len = n;
}

static bool span_equals(const char *at, size_t len, const char *literal)
{
    size_t n = strlen(literal);
    return len == n && memcmp(at, literal, n) == 0;
}

static bool span_starts(const char *at, size_t len, const char *prefix)
{
    size_t n = strlen(prefix);
    return len >= n && memcmp(at, prefix, n) == 0;
}

static bool parse_date(const char *at, size_t len, lw_date_t *out)
{
    if (len != 10u) return false;
    if (at[4] != '-' || at[7] != '-') return false;
    int year = 0, month = 0, day = 0;
    for (int i = 0; i < 4; i++) {
        if (at[i] < '0' || at[i] > '9') return false;
        year = year * 10 + (at[i] - '0');
    }
    for (int i = 5; i < 7; i++) {
        if (at[i] < '0' || at[i] > '9') return false;
        month = month * 10 + (at[i] - '0');
    }
    for (int i = 8; i < 10; i++) {
        if (at[i] < '0' || at[i] > '9') return false;
        day = day * 10 + (at[i] - '0');
    }
    lw_date_t date = { year, month, day };
    if (!lw_date_valid(date)) return false;
    *out = date;
    return true;
}

static bool parse_unsigned(const char *at, size_t len, unsigned *out)
{
    if (!len || len > 9u) return false;
    unsigned value = 0;
    for (size_t i = 0; i < len; i++) {
        if (at[i] < '0' || at[i] > '9') return false;
        value = value * 10u + (unsigned)(at[i] - '0');
    }
    *out = value;
    return true;
}

/* ---------------------------------------------------------------- parser */

typedef struct {
    section_t section;
    unsigned index;         /* index of the record being filled */
    bool have_date;
    bool have_title;
    bool have_body;
    bool have_id;
    bool have_path;
} parse_ctx_t;

static bool fail(lw_content_status_t status, unsigned line)
{
    s_report.status = status;
    if (line && !s_report.rejected) s_report.rejected = line;
    reset_state();
    s_report.content_version = 0;
    s_report.memories = s_report.photos = 0;
    s_report.letters = s_report.dialogues = s_report.lines = 0;
    s_report.status = status;
    return false;
}

/* Close the section currently being filled, validating what is mandatory. */
static bool close_section(parse_ctx_t *ctx, unsigned line)
{
    switch (ctx->section) {
    case SEC_NONE:
        return true;
    case SEC_MEMORY:
        if (!ctx->have_date || !ctx->have_title || !ctx->have_body) {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        if (!s_arena.memories[ctx->index].photo) s_arena.memories[ctx->index].photo = "";
        s_content.memory_count++;
        break;
    case SEC_ALBUM:
        if (!ctx->have_title || !ctx->have_path) return fail(LW_CONTENT_MALFORMED, line);
        s_content.photo_count++;
        break;
    case SEC_LETTER:
        if (!ctx->have_title || !ctx->have_body) return fail(LW_CONTENT_MALFORMED, line);
        s_content.letter_count++;
        break;
    case SEC_DIALOGUE:
        if (!ctx->have_id || s_arena.dialogues[ctx->index].line_count == 0) {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        s_content.dialogue_count++;
        break;
    default:
        return fail(LW_CONTENT_MALFORMED, line);
    }
    ctx->section = SEC_NONE;
    ctx->have_date = ctx->have_title = ctx->have_body = ctx->have_id = ctx->have_path = false;
    return true;
}

static bool open_section(parse_ctx_t *ctx, const char *name, size_t len, unsigned line)
{
    if (!close_section(ctx, line)) return false;
    if (span_equals(name, len, "memory")) {
        if (s_content.memory_count >= LW_CONTENT_MAX_MEMORIES) return fail(LW_CONTENT_TOO_MANY, line);
        ctx->section = SEC_MEMORY;
        ctx->index = s_content.memory_count;
    } else if (span_equals(name, len, "album")) {
        if (s_content.photo_count >= LW_CONTENT_MAX_PHOTOS) return fail(LW_CONTENT_TOO_MANY, line);
        ctx->section = SEC_ALBUM;
        ctx->index = s_content.photo_count;
    } else if (span_equals(name, len, "letter")) {
        if (s_content.letter_count >= LW_CONTENT_MAX_LETTERS) return fail(LW_CONTENT_TOO_MANY, line);
        ctx->section = SEC_LETTER;
        ctx->index = s_content.letter_count;
    } else if (span_equals(name, len, "dialogue")) {
        if (s_content.dialogue_count >= LW_CONTENT_MAX_DIALOGUES) return fail(LW_CONTENT_TOO_MANY, line);
        ctx->section = SEC_DIALOGUE;
        ctx->index = s_content.dialogue_count;
        s_arena.dialogues[ctx->index].first_line = s_content.line_count;
    } else {
        return fail(LW_CONTENT_MALFORMED, line);
    }
    return true;
}

static bool assign_key(parse_ctx_t *ctx, const char *key, size_t key_len,
                       const char *value, size_t value_len, unsigned line)
{
    if (ctx->section == SEC_NONE) {
        if (span_equals(key, key_len, "schema")) {
            unsigned schema = 0;
            if (!parse_unsigned(value, value_len, &schema)) return fail(LW_CONTENT_MALFORMED, line);
            if (schema != LW_CONTENT_SCHEMA) return fail(LW_CONTENT_BAD_SCHEMA, line);
            return true;
        }
        if (span_equals(key, key_len, "content_version")) {
            unsigned version = 0;
            if (!parse_unsigned(value, value_len, &version)) return fail(LW_CONTENT_MALFORMED, line);
            if (version < 1u || version > LW_CONTENT_VERSION) return fail(LW_CONTENT_BAD_VERSION, line);
            s_content.content_version = version;
            return true;
        }
        if (span_equals(key, key_len, "anniversary")) {
            if (!parse_date(value, value_len, &s_content.anniversary)) return fail(LW_CONTENT_BAD_DATE, line);
            return true;
        }
        if (span_equals(key, key_len, "welcome")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_BODY_MAX, &s_content.welcome);
            if (st != STORE_OK) return store_failed(st, line);
            return true;
        }
        return fail(LW_CONTENT_MALFORMED, line);
    }

    switch (ctx->section) {
    case SEC_MEMORY: {
        lw_memory_t *m = &s_arena.memories[ctx->index];
        if (span_equals(key, key_len, "date")) {
            if (!parse_date(value, value_len, &m->date)) return fail(LW_CONTENT_BAD_DATE, line);
            ctx->have_date = true;
        } else if (span_equals(key, key_len, "title")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_TITLE_MAX, &m->title);
            if (st != STORE_OK) return store_failed(st, line);
            ctx->have_title = true;
        } else if (span_equals(key, key_len, "body")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_BODY_MAX, &m->body);
            if (st != STORE_OK) return store_failed(st, line);
            ctx->have_body = true;
        } else if (span_equals(key, key_len, "photo")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_PATH_MAX, &m->photo);
            if (st != STORE_OK) return store_failed(st, line);
        } else {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        return true;
    }
    case SEC_ALBUM: {
        lw_content_photo_t *p = &s_arena.photos[ctx->index];
        if (span_equals(key, key_len, "title")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_TITLE_MAX, &p->title);
            if (st != STORE_OK) return store_failed(st, line);
            ctx->have_title = true;
        } else if (span_equals(key, key_len, "photo")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_PATH_MAX, &p->path);
            if (st != STORE_OK) return store_failed(st, line);
            if (!p->path[0]) return fail(LW_CONTENT_MALFORMED, line);
            ctx->have_path = true;
        } else {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        return true;
    }
    case SEC_LETTER: {
        lw_content_letter_t *l = &s_arena.letters[ctx->index];
        if (span_equals(key, key_len, "title")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_TITLE_MAX, &l->title);
            if (st != STORE_OK) return store_failed(st, line);
            ctx->have_title = true;
        } else if (span_equals(key, key_len, "body")) {
            store_result_t st = store_value(value, value_len, LW_CONTENT_BODY_MAX, &l->body);
            if (st != STORE_OK) return store_failed(st, line);
            ctx->have_body = true;
        } else {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        return true;
    }
    case SEC_DIALOGUE: {
        lw_content_dialogue_t *d = &s_arena.dialogues[ctx->index];
        if (span_equals(key, key_len, "id")) {
            unsigned id = 0;
            if (!parse_unsigned(value, value_len, &id) || id > 999u) return fail(LW_CONTENT_MALFORMED, line);
            d->id = (int)id;
            ctx->have_id = true;
        } else if (span_equals(key, key_len, "line")) {
            const char *bar = memchr(value, '|', value_len);
            if (!bar) return fail(LW_CONTENT_MALFORMED, line);
            size_t speaker_len = (size_t)(bar - value);
            const char *speaker = NULL;
            store_result_t st = store_value(value, speaker_len, LW_CONTENT_SPEAKER_MAX, &speaker);
            if (st != STORE_OK) return store_failed(st, line);
            const char *text = NULL;
            st = store_value(bar + 1, value_len - speaker_len - 1u, LW_CONTENT_BODY_MAX, &text);
            if (st != STORE_OK) return store_failed(st, line);
            if (s_content.line_count >= LW_CONTENT_MAX_LINES) return fail(LW_CONTENT_TOO_MANY, line);
            s_arena.lines[s_content.line_count].speaker = speaker;
            s_arena.lines[s_content.line_count].text = text;
            s_content.line_count++;
            d->line_count++;
        } else {
            return fail(LW_CONTENT_MALFORMED, line);
        }
        return true;
    }
    default:
        return fail(LW_CONTENT_MALFORMED, line);
    }
}

bool lw_content_parse(const char *text, size_t length)
{
    reset_state();
    memset(&s_report, 0, sizeof(s_report));
    s_report.status = LW_CONTENT_MALFORMED;

    if (!text) return fail(LW_CONTENT_MISSING, 0);
    if (length > LW_CONTENT_MAX_BYTES) return fail(LW_CONTENT_TOO_LARGE, 0);
    s_report.bytes = (unsigned)length;

    /* Tolerate a UTF-8 BOM: Windows editors add one and it is not content. */
    size_t at = 0;
    if (length >= 3u && (unsigned char)text[0] == 0xEFu &&
        (unsigned char)text[1] == 0xBBu && (unsigned char)text[2] == 0xBFu) {
        at = 3u;
    }
    if (!lw_content_utf8_valid(text + at, length - at)) return fail(LW_CONTENT_BAD_UTF8, 0);

    const char *magic = "LWCX0001";
    size_t magic_len = 8u;
    bool seen_magic = false;
    parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    unsigned line_no = 0;
    while (at < length) {
        size_t start = at;
        while (at < length && text[at] != '\n') at++;
        size_t end = at;
        if (at < length) at++; /* consume the newline */
        line_no++;

        if (end > start && text[end - 1u] == '\r') end--;
        const char *line = text + start;
        size_t len = end - start;
        trim_span(&line, &len);
        if (len == 0) continue;
        if (line[0] == '#') continue;

        if (!seen_magic) {
            if (len != magic_len || memcmp(line, magic, magic_len) != 0) {
                return fail(LW_CONTENT_BAD_MAGIC, line_no);
            }
            seen_magic = true;
            continue;
        }

        if (line[0] == '[') {
            if (len < 3u || line[len - 1u] != ']') return fail(LW_CONTENT_MALFORMED, line_no);
            const char *name = line + 1;
            size_t name_len = len - 2u;
            if (name_len && name[0] == '/') {
                name++;
                name_len--;
                /* A closing tag must match the open section. */
                if (ctx.section == SEC_NONE) return fail(LW_CONTENT_MALFORMED, line_no);
                const char *expect = ctx.section == SEC_MEMORY ? "memory"
                                   : ctx.section == SEC_ALBUM ? "album"
                                   : ctx.section == SEC_LETTER ? "letter" : "dialogue";
                if (!span_equals(name, name_len, expect)) return fail(LW_CONTENT_MALFORMED, line_no);
                if (!close_section(&ctx, line_no)) return false;
            } else if (!open_section(&ctx, name, name_len, line_no)) {
                return false;
            }
            continue;
        }

        const char *eq = memchr(line, '=', len);
        if (!eq) return fail(LW_CONTENT_MALFORMED, line_no);
        const char *key = line;
        size_t key_len = (size_t)(eq - line);
        const char *value = eq + 1;
        size_t value_len = len - key_len - 1u;
        trim_span(&key, &key_len);
        trim_span(&value, &value_len);
        if (!key_len) return fail(LW_CONTENT_MALFORMED, line_no);
        if (!assign_key(&ctx, key, key_len, value, value_len, line_no)) return false;
    }

    if (!seen_magic) return fail(LW_CONTENT_BAD_MAGIC, 1);
    if (ctx.section != SEC_NONE) return fail(LW_CONTENT_MALFORMED, line_no);
    if (s_content.content_version == 0u) return fail(LW_CONTENT_MALFORMED, 0);

    s_content.memories = s_arena.memories;
    s_content.photos = s_arena.photos;
    s_content.letters = s_arena.letters;
    s_content.dialogues = s_arena.dialogues;
    s_content.lines = s_arena.lines;
    if (!s_content.welcome) s_content.welcome = "";

    s_report.status = LW_CONTENT_OK;
    s_report.content_version = s_content.content_version;
    s_report.memories = s_content.memory_count;
    s_report.photos = s_content.photo_count;
    s_report.letters = s_content.letter_count;
    s_report.dialogues = s_content.dialogue_count;
    s_report.lines = s_content.line_count;
    return true;
}

bool lw_content_resolve_path(char *dst, size_t cap, const char *root)
{
    if (!dst || cap == 0u) return false;
    dst[0] = '\0';
    if (!root || !root[0]) return false;
    /* lw_path_join() owns the rules: it rejects a bare separator root, a
     * relative name that escapes the root and any overflow. The content file
     * name is a constant here, so the only variable is the root. */
    return lw_path_join(dst, cap, root, LW_CONTENT_FILE_NAME);
}

bool lw_content_load(const char *path)
{
    reset_state();
    memset(&s_report, 0, sizeof(s_report));
    s_report.status = LW_CONTENT_MISSING;
    if (!path || !path[0]) return false;

    size_t n = strlen(path);
    if (n >= sizeof(s_report.source)) n = sizeof(s_report.source) - 1u;
    memcpy(s_report.source, path, n);
    s_report.source[n] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    /* LW_CONTENT_MAX_BYTES is 32 KiB, which is bigger than the whole main task
     * stack (CONFIG_ESP_MAIN_TASK_STACK_SIZE = 12288). A stack frame of that
     * size trips the FreeRTOS stack canary the moment a card is actually
     * mounted, which is exactly the case the no-card path never reaches: the
     * board rebooted in a loop with
     *   Guru Meditation Error: Core 0 panic'ed (Stack protection fault)
     *   lw_content_load at lw_content.c:521
     * Reads happen once at boot before the UI task starts, so a single static
     * buffer is enough and it cannot be entered concurrently. */
    static char buffer[LW_CONTENT_MAX_BYTES + 1u];
    size_t got = fread(buffer, 1, sizeof(buffer), f);
    bool more = fgetc(f) != EOF;
    fclose(f);
    if (more) {
        memset(&s_report, 0, sizeof(s_report));
        s_report.status = LW_CONTENT_TOO_LARGE;
        memcpy(s_report.source, path, n);
        s_report.source[n] = '\0';
        return false;
    }
    bool ok = lw_content_parse(buffer, got);
    /* lw_content_parse() resets the report; restore the path it came from. */
    memcpy(s_report.source, path, n);
    s_report.source[n] = '\0';
    return ok;
}

void lw_content_clear(void)
{
    reset_state();
    memset(&s_report, 0, sizeof(s_report));
    s_report.status = LW_CONTENT_MISSING;
}

const lw_content_t *lw_content_get(void)
{
    return &s_content;
}

const lw_content_report_t *lw_content_report(void)
{
    return &s_report;
}

const char *lw_content_status_text(lw_content_status_t status)
{
    return status_name(status);
}

const lw_memory_t *lw_content_memories(void)
{
    return s_content.memory_count ? s_arena.memories : NULL;
}

unsigned lw_content_memory_count(void)
{
    return s_content.memory_count;
}
