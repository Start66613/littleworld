/* Demo world accessor (kept separate so the core does not hardcode content).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LW_RPG_DEMO_H
#define LW_RPG_DEMO_H

#include "lw_rpg.h"

#ifdef __cplusplus
extern "C" {
#endif

const lw_rpg_world_t *lw_rpg_demo_world(void);

#ifdef __cplusplus
}
#endif

#endif /* LW_RPG_DEMO_H */
