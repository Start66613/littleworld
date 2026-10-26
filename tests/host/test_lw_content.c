/* lw_content: the V4.2 content container. Host build, MSVC /W4 /WX.
 *
 * The parser is the gate between "a file a user edited on Windows" and "text
 * drawn on the device". Every rejection case below is a mistake a real editor
 * makes: a BOM from Notepad, CRLF, a forgotten closing tag, a 29 February date
 * in a non-leap year, a photo path pointing outside the package.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_content.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(name, expr) do { \
    checks++; \
    if (!(expr)) { failures++; printf("FAIL %s line %d\n", name, __LINE__); } \
} while (0)

static const char *k_good =
    "LWCX0001\n"
    "schema=1\n"
    "content_version=1\n"
    "anniversary=2024-10-25\n"
    "welcome=这里收着我们一起走过的日子。\\n慢一点翻。\n"
    "\n"
    "[memory]\n"
    "date=2024-10-25\n"
    "title=故事开始\n"
    "body=示例内容：这一天我们在心里定下了一个小约定。\n"
    "[/memory]\n"
    "\n"
    "[memory]\n"
    "date=2025-02-14\n"
    "title=第一个情人节\n"
    "body=示例内容：一起吃了顿很慢的晚饭。\n"
    "photo=photos/001.lwph\n"
    "[/memory]\n"
    "\n"
    "[album]\n"
    "title=海边\n"
    "photo=photos/002.lwph\n"
    "[/album]\n"
    "\n"
    "[letter]\n"
    "title=给未来的你\n"
    "body=示例内容：这封信还没有写完。\n"
    "[/letter]\n"
    "\n"
    "[dialogue]\n"
    "id=10\n"
    "line=男方|照片盒里有一叠空白的相纸。\n"
    "line=qianqian|（示例）先把它们放进第一页吧。\n"
    "[/dialogue]\n";

static bool parse(const char *text)
{
    return lw_content_parse(text, strlen(text));
}

/* Every rejection must also leave the model empty: a half-loaded content set
 * would render a timeline with holes and no way to notice. */
static void expect_failure(const char *name, const char *text, lw_content_status_t want)
{
    bool ok = parse(text);
    const lw_content_report_t *report = lw_content_report();
    checks++;
    if (ok || report->status != want) {
        failures++;
        printf("FAIL %s (got %s, wanted %s)\n", name,
               lw_content_status_text(report->status), lw_content_status_text(want));
    }
    checks++;
    if (lw_content_memory_count() != 0u || lw_content_get()->photo_count != 0u) {
        failures++;
        printf("FAIL %s left content behind\n", name);
    }
}

static void test_good_file(void)
{
    CHECK("good file parses", parse(k_good));
    const lw_content_report_t *r = lw_content_report();
    CHECK("status ok", r->status == LW_CONTENT_OK);
    CHECK("version 1", r->content_version == 1u);
    CHECK("two memories", r->memories == 2u);
    CHECK("one photo", r->photos == 1u);
    CHECK("one letter", r->letters == 1u);
    CHECK("one dialogue", r->dialogues == 1u);
    CHECK("two lines", r->lines == 2u);
    CHECK("byte count recorded", r->bytes == (unsigned)strlen(k_good));

    const lw_content_t *c = lw_content_get();
    CHECK("anniversary parsed", c->anniversary.year == 2024 && c->anniversary.month == 10 &&
                                c->anniversary.day == 25);
    CHECK("welcome keeps the line break", strstr(c->welcome, "\n慢一点翻") != NULL);
    CHECK("welcome is UTF-8", strstr(c->welcome, "这里收着") != NULL);
    CHECK("first memory title", strcmp(c->memories[0].title, "故事开始") == 0);
    CHECK("first memory has no photo", strcmp(c->memories[0].photo, "") == 0);
    CHECK("second memory photo", strcmp(c->memories[1].photo, "photos/001.lwph") == 0);
    CHECK("photo title", strcmp(c->photos[0].title, "海边") == 0);
    CHECK("letter body", strstr(c->letters[0].body, "还没有写完") != NULL);
    CHECK("dialogue id", c->dialogues[0].id == 10);
    CHECK("dialogue first line", c->dialogues[0].first_line == 0u);
    CHECK("dialogue line count", c->dialogues[0].line_count == 2u);
    CHECK("line speaker", strcmp(c->lines[0].speaker, "男方") == 0);
    CHECK("line text", strstr(c->lines[1].text, "第一页") != NULL);
    CHECK("borrowed memory array matches", lw_content_memories() == c->memories);
    CHECK("borrowed memory count", lw_content_memory_count() == 2u);
}

static void test_utf8(void)
{
    const char ok[] = "LWCX0001\ncontent_version=1\nwelcome=你好\n";
    CHECK("BMP is valid UTF-8", lw_content_utf8_valid(ok, strlen(ok)));
    CHECK("CJK is valid", lw_content_utf8_valid("中文字", strlen("中文字")));
    CHECK("emoji is valid", lw_content_utf8_valid("\xF0\x9F\x98\x80", 4));
    CHECK("overlong rejected", !lw_content_utf8_valid("\xC0\xAF", 2));
    CHECK("surrogate rejected", !lw_content_utf8_valid("\xED\xA0\x80", 3));
    CHECK("truncated rejected", !lw_content_utf8_valid("\xE4\xB8", 2));
    CHECK("lone continuation rejected", !lw_content_utf8_valid("\x80", 1));
    CHECK("above U+10FFFF rejected", !lw_content_utf8_valid("\xF5\x80\x80\x80", 4));
    CHECK("NUL-free buffer accepted", lw_content_utf8_valid("abc", 3));

    expect_failure("bad UTF-8 in a value",
                   "LWCX0001\ncontent_version=1\nwelcome=\xE4\xB8\n", LW_CONTENT_BAD_UTF8);
}

static void test_bom_and_crlf(void)
{
    const char *bom = "\xEF\xBB\xBFLWCX0001\r\ncontent_version=1\r\n"
                      "welcome=示例\r\n"
                      "[memory]\r\ndate=2024-10-25\r\ntitle=t\r\nbody=b\r\n[/memory]\r\n";
    CHECK("BOM and CRLF accepted", parse(bom));
    CHECK("one memory after CRLF", lw_content_memory_count() == 1u);
    CHECK("CR stripped from the title", strcmp(lw_content_get()->memories[0].title, "t") == 0);
    CHECK("CR stripped from the body", strcmp(lw_content_get()->memories[0].body, "b") == 0);
}

static void test_rejections(void)
{
    expect_failure("empty file", "", LW_CONTENT_BAD_MAGIC);
    expect_failure("wrong magic", "LWCX0002\ncontent_version=1\n", LW_CONTENT_BAD_MAGIC);
    expect_failure("magic missing", "content_version=1\n", LW_CONTENT_BAD_MAGIC);
    expect_failure("bad schema", "LWCX0001\nschema=2\ncontent_version=1\n", LW_CONTENT_BAD_SCHEMA);
    expect_failure("future version", "LWCX0001\ncontent_version=9\n", LW_CONTENT_BAD_VERSION);
    expect_failure("version zero", "LWCX0001\ncontent_version=0\n", LW_CONTENT_BAD_VERSION);
    expect_failure("no version at all", "LWCX0001\nschema=1\n", LW_CONTENT_MALFORMED);
    expect_failure("unknown top-level key", "LWCX0001\ncontent_version=1\nnope=1\n", LW_CONTENT_MALFORMED);
    expect_failure("line without equals", "LWCX0001\ncontent_version=1\nhello\n", LW_CONTENT_MALFORMED);
    expect_failure("unclosed memory",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=t\nbody=b\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("mismatched close tag",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=t\nbody=b\n[/album]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("stray close tag", "LWCX0001\ncontent_version=1\n[/memory]\n", LW_CONTENT_MALFORMED);
    expect_failure("unknown section", "LWCX0001\ncontent_version=1\n[whatever]\n", LW_CONTENT_MALFORMED);
    expect_failure("memory without a date",
                   "LWCX0001\ncontent_version=1\n[memory]\ntitle=t\nbody=b\n[/memory]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("memory without a title",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\nbody=b\n[/memory]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("memory without a body",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=t\n[/memory]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("unknown key inside a memory",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=t\nbody=b\nnope=1\n[/memory]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("album without a photo",
                   "LWCX0001\ncontent_version=1\n[album]\ntitle=t\n[/album]\n", LW_CONTENT_MALFORMED);
    expect_failure("album with an empty photo",
                   "LWCX0001\ncontent_version=1\n[album]\ntitle=t\nphoto=\n[/album]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("letter without a body",
                   "LWCX0001\ncontent_version=1\n[letter]\ntitle=t\n[/letter]\n", LW_CONTENT_MALFORMED);
    expect_failure("dialogue without an id",
                   "LWCX0001\ncontent_version=1\n[dialogue]\nline=a|b\n[/dialogue]\n", LW_CONTENT_MALFORMED);
    expect_failure("dialogue without lines",
                   "LWCX0001\ncontent_version=1\n[dialogue]\nid=10\n[/dialogue]\n", LW_CONTENT_MALFORMED);
    expect_failure("dialogue line without a speaker separator",
                   "LWCX0001\ncontent_version=1\n[dialogue]\nid=10\nline=hello\n[/dialogue]\n",
                   LW_CONTENT_MALFORMED);
    expect_failure("bad escape", "LWCX0001\ncontent_version=1\nwelcome=a\\qb\n", LW_CONTENT_MALFORMED);
}

static void test_dates(void)
{
    expect_failure("month 13", "LWCX0001\ncontent_version=1\nanniversary=2024-13-01\n", LW_CONTENT_BAD_DATE);
    expect_failure("day 32", "LWCX0001\ncontent_version=1\nanniversary=2024-01-32\n", LW_CONTENT_BAD_DATE);
    expect_failure("29 Feb in a non-leap year",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=2023-02-29\ntitle=t\nbody=b\n[/memory]\n",
                   LW_CONTENT_BAD_DATE);
    expect_failure("date without padding", "LWCX0001\ncontent_version=1\nanniversary=2024-1-5\n", LW_CONTENT_BAD_DATE);
    expect_failure("date in the wrong order", "LWCX0001\ncontent_version=1\nanniversary=25-10-2024\n",
                   LW_CONTENT_BAD_DATE);
    expect_failure("memory date before 1970",
                   "LWCX0001\ncontent_version=1\n[memory]\ndate=1900-01-01\ntitle=t\nbody=b\n[/memory]\n",
                   LW_CONTENT_BAD_DATE);

    CHECK("29 Feb in a leap year accepted",
          parse("LWCX0001\ncontent_version=1\n[memory]\ndate=2024-02-29\ntitle=t\nbody=b\n[/memory]\n"));
}

static void test_limits(void)
{
    /* 33 memories: one over LW_CONTENT_MAX_MEMORIES. */
    static char big[LW_CONTENT_MAX_BYTES];
    size_t at = 0;
    at += (size_t)snprintf(big + at, sizeof(big) - at, "LWCX0001\ncontent_version=1\n");
    for (unsigned i = 0; i < LW_CONTENT_MAX_MEMORIES + 1u; i++) {
        at += (size_t)snprintf(big + at, sizeof(big) - at,
                               "[memory]\ndate=2024-10-25\ntitle=t%u\nbody=b\n[/memory]\n", i);
    }
    expect_failure("too many memories", big, LW_CONTENT_TOO_MANY);

    /* A title one byte over the limit. */
    static char longtitle[600];
    memset(longtitle, 'a', sizeof(longtitle));
    longtitle[LW_CONTENT_TITLE_MAX + 1u] = '\0';
    char file[900];
    snprintf(file, sizeof(file),
             "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=%s\nbody=b\n[/memory]\n",
             longtitle);
    expect_failure("title over the limit", file, LW_CONTENT_TOO_MANY);

    /* Exactly at the limit is fine. */
    longtitle[LW_CONTENT_TITLE_MAX] = '\0';
    snprintf(file, sizeof(file),
             "LWCX0001\ncontent_version=1\n[memory]\ndate=2024-10-25\ntitle=%s\nbody=b\n[/memory]\n",
             longtitle);
    CHECK("title at the limit accepted", parse(file));

    /* A file bigger than the accepted maximum. */
    static char huge[LW_CONTENT_MAX_BYTES + 8u];
    memset(huge, 'x', sizeof(huge));
    memcpy(huge, "LWCX0001\ncontent_version=1\n", 26);
    checks++;
    if (lw_content_parse(huge, sizeof(huge))) { failures++; printf("FAIL oversized file accepted\n"); }
    checks++;
    if (lw_content_report()->status != LW_CONTENT_TOO_LARGE) {
        failures++; printf("FAIL oversized file status %s\n",
                           lw_content_status_text(lw_content_report()->status));
    }
}

static void test_comments_and_blanks(void)
{
    const char *text = "# prepared on the computer\n"
                       "\n"
                       "LWCX0001\n"
                       "   # indented comment\n"
                       "content_version=1\n"
                       "\n"
                       "  anniversary = 2024-10-25  \n"
                       "welcome=hello\n";
    CHECK("comments, blanks and spaces accepted", parse(text));
    CHECK("spaces around the value trimmed",
          lw_content_get()->anniversary.year == 2024 &&
          lw_content_get()->anniversary.month == 10);
    CHECK("no memories is a valid file", lw_content_memory_count() == 0u);
    CHECK("memories pointer NULL when empty", lw_content_memories() == NULL);
}

static void test_clear_and_reload(void)
{
    CHECK("load a good file", parse(k_good));
    CHECK("two memories before clear", lw_content_memory_count() == 2u);
    lw_content_clear();
    CHECK("clear empties the model", lw_content_memory_count() == 0u);
    CHECK("clear empties the report", lw_content_report()->memories == 0u);
    CHECK("clear reports missing", lw_content_report()->status == LW_CONTENT_MISSING);

    CHECK("reload after clear", parse(k_good));
    CHECK("two memories after reload", lw_content_memory_count() == 2u);

    /* A failed reload must not keep the previous content. */
    CHECK("failed reload returns false", !parse("LWCX0001\ncontent_version=1\n[memory]\n"));
    CHECK("failed reload drops the old content", lw_content_memory_count() == 0u);
}

static void test_missing_file(void)
{
    CHECK("missing file returns false", !lw_content_load("D:/__littleworld_no_such_content__.lwcx"));
    CHECK("missing file status", lw_content_report()->status == LW_CONTENT_MISSING);
    CHECK("missing file records the path",
          strstr(lw_content_report()->source, "__littleworld_no_such_content__") != NULL);
    CHECK("missing file leaves no content", lw_content_memory_count() == 0u);
}

int main(int argc, char **argv)
{
    /* Converter hook: tools/studio_to_device.py --verify runs THIS binary, so a
     * Studio package is checked by the shipping parser rather than by a Python
     * re-implementation of it. */
    if (argc == 3 && strcmp(argv[1], "--check-content") == 0) {
        if (!lw_content_load(argv[2])) {
            const lw_content_report_t *report = lw_content_report();
            printf("REJECT %s: %s (line %u)\n", argv[2],
                   lw_content_status_text(report->status), report->rejected);
            return 1;
        }
        const lw_content_t *content = lw_content_get();
        printf("OK %s v%u memories=%u photos=%u letters=%u dialogues=%u lines=%u bytes=%u\n",
               argv[2], content->content_version, content->memory_count, content->photo_count,
               content->letter_count, content->dialogue_count, content->line_count,
               lw_content_report()->bytes);
        return 0;
    }

    test_good_file();
    test_utf8();
    test_bom_and_crlf();
    test_rejections();
    test_dates();
    test_limits();
    test_comments_and_blanks();
    test_clear_and_reload();
    test_missing_file();

    printf("\ncontent: %u checks, %u failure(s)\n", checks, failures);
    if (failures) { printf("CONTENT TESTS FAILED\n"); return 1; }
    printf("CONTENT TESTS PASSED\n");
    return 0;
}
