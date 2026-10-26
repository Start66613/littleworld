/* Joining a relative name onto the resource root.
 *
 * This exists because the resource root is an ABSOLUTE directory that already
 * ends in "littleworld" (/sdcard/littleworld on the board), and every consumer
 * used to build its own path with its own snprintf. One of them appended
 * "littleworld/" a second time and produced
 * /sdcard/littleworld/littleworld/content.lwcx, which is a directory that does
 * not exist on any card the packaging tool writes. The bug was invisible in
 * every test because no test ever built a directory tree shaped like the card.
 *
 * So the rule is written down once, here, and the tests exercise it against a
 * real directory tree:
 *
 *   - the root must be non-empty and must not be a bare "/" or "\\";
 *   - the relative part must be a plain relative path: no leading separator,
 *     no drive letter, no ".." component, no backslashes;
 *   - a trailing separator on the root is tolerated and not doubled;
 *   - overflow is a hard failure, never a truncated path.
 *
 * Plain C, no ESP-IDF, so the host tests compile this exact file.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when `relative` may be appended to a root: non-empty, no leading
 * separator, no drive prefix, no backslash, and no ".." path component.
 * A "." component is allowed and kept verbatim; nothing normalises it. */
bool lw_path_is_safe_relative(const char *relative);

/* dst = root + "/" + relative. Returns false (and writes an empty string) when
 * either argument is unusable or the result would not fit. `dst` and `root`
 * may not overlap. */
bool lw_path_join(char *dst, size_t cap, const char *root, const char *relative);

#ifdef __cplusplus
}
#endif
