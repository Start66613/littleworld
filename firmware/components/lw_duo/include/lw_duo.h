/* Two independent actors and deterministic interaction scheduling.
 * No UI, persistence, device or network dependency. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LW_DUO_ACTORS 2
#define LW_DUO_MIN_DISTANCE 120.0f
typedef enum { LW_FACE_DOWN, LW_FACE_DOWN_LEFT, LW_FACE_LEFT, LW_FACE_UP_LEFT,
               LW_FACE_UP, LW_FACE_UP_RIGHT, LW_FACE_RIGHT, LW_FACE_DOWN_RIGHT } lw_facing_t;
typedef enum { LW_ACTOR_IDLE, LW_ACTOR_WALK, LW_ACTOR_TOUCH, LW_ACTOR_WAVE, LW_ACTOR_SIT,
               LW_ACTOR_LETTER, LW_ACTOR_WINDOW, LW_ACTOR_CELEBRATE, LW_ACTOR_SLEEP } lw_actor_motion_t;
typedef enum { LW_DUO_GREET, LW_DUO_STAND, LW_DUO_SIT, LW_DUO_LETTER, LW_DUO_WINDOW,
               LW_DUO_CELEBRATE, LW_DUO_REST, LW_DUO_INTERACTION_COUNT } lw_interaction_t;
typedef enum { LW_DUO_FREE, LW_DUO_APPROACH, LW_DUO_ALIGN, LW_DUO_ACT } lw_duo_phase_t;
typedef struct {
    float x,y,target_x,target_y;
    lw_facing_t facing;
    lw_actor_motion_t motion;
    bool sleeping,wants_sleep;
    uint32_t wait_ms,pose_ms,animation_ms;
} lw_actor_t;
typedef struct {
    lw_interaction_t id;
    const char *name;
    lw_actor_motion_t motion;
    uint32_t duration_ms;
} lw_interaction_def_t;
typedef struct {
    lw_actor_t actors[LW_DUO_ACTORS];
    lw_duo_phase_t phase;
    lw_interaction_t interaction;
    uint32_t random,phase_ms,interaction_ms,next_joint_ms,completed,cancelled;
    bool autonomous;
} lw_duo_t;

void lw_duo_init(lw_duo_t *duo,uint32_t seed);
void lw_duo_tick(lw_duo_t *duo,uint32_t dt_ms);
bool lw_duo_move(lw_duo_t *duo,unsigned actor,float x,float y);
bool lw_duo_request(lw_duo_t *duo,lw_interaction_t interaction);
void lw_duo_sleep(lw_duo_t *duo,unsigned actor,bool asleep);
void lw_duo_touch(lw_duo_t *duo,unsigned actor);
void lw_duo_cancel(lw_duo_t *duo);
const lw_interaction_def_t *lw_duo_interaction(lw_interaction_t id);
bool lw_duo_separated(const lw_duo_t *duo);
