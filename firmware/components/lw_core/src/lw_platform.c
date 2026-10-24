/* SPDX-License-Identifier: MIT */
#include "lw/lw_platform.h"

static const lw_platform_t *s_platform;

void lw_platform_set(const lw_platform_t *platform)
{
    s_platform = platform;
}

const lw_platform_t *lw_platform_get(void)
{
    return s_platform;
}
