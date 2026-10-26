/* SPDX-License-Identifier: MIT */
#include "lw_path.h"

#include <string.h>

/* A root that is a bare separator has no name to join onto: "/" + "x" would
 * produce "//x", which some FAT layers accept and others do not. Refusing it
 * keeps the failure at the caller instead of at the file system. */
static bool root_usable(const char *root)
{
    if (!root || !root[0]) return false;
    if (root[0] == '/' && root[1] == '\0') return false;
    if (root[0] == '\\' && root[1] == '\0') return false;
    return true;
}

bool lw_path_is_safe_relative(const char *relative)
{
    if (!relative || !relative[0]) return false;
    if (relative[0] == '/' || relative[0] == '\\') return false;
    /* "C:" or "C:/..." is an absolute path on the build machine and a legal but
     * meaningless name on FAT; neither belongs under the resource root. */
    if (relative[0] && relative[1] == ':') return false;
    if (strchr(relative, '\\') != NULL) return false;

    /* Walk the components rather than substring-searching: a file called
     * "notes..txt" is fine, a component that is exactly ".." is not. */
    const char *at = relative;
    while (*at) {
        const char *end = strchr(at, '/');
        size_t len = end ? (size_t)(end - at) : strlen(at);
        if (len == 2u && at[0] == '.' && at[1] == '.') return false;
        if (!end) break;
        at = end + 1;
    }
    return true;
}

bool lw_path_join(char *dst, size_t cap, const char *root, const char *relative)
{
    if (!dst || cap == 0u) return false;
    dst[0] = '\0';
    if (!root_usable(root) || !lw_path_is_safe_relative(relative)) return false;

    size_t root_len = strlen(root);
    /* Tolerate one trailing separator so callers that stored "root/" do not
     * produce "root//name". More than one is a typo, not a convention. */
    if (root_len > 1u && root[root_len - 1u] == '/') root_len--;

    size_t rel_len = strlen(relative);
    /* root + '/' + relative + NUL */
    if (root_len + 1u + rel_len + 1u > cap) return false;

    memcpy(dst, root, root_len);
    dst[root_len] = '/';
    memcpy(dst + root_len + 1u, relative, rel_len + 1u);
    return true;
}
