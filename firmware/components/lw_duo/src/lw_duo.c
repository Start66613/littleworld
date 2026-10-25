/* SPDX-License-Identifier: MIT */
#include "lw_duo.h"
#include <math.h>
#include <string.h>

static const lw_interaction_def_t s_catalog[]={
    {LW_DUO_GREET,"Meet and wave",LW_ACTOR_WAVE,1600},
    {LW_DUO_STAND,"Side by side",LW_ACTOR_IDLE,2200},
    {LW_DUO_SIT,"Sit together",LW_ACTOR_SIT,2800},
    {LW_DUO_LETTER,"Exchange a letter",LW_ACTOR_LETTER,2400},
    {LW_DUO_WINDOW,"Look out the window",LW_ACTOR_WINDOW,3200},
    {LW_DUO_CELEBRATE,"Anniversary",LW_ACTOR_CELEBRATE,2600},
    {LW_DUO_REST,"Rest together",LW_ACTOR_SLEEP,2200}
};
static float clamp(float x,float lo,float hi) { return x<lo ? lo : x>hi ? hi : x; }
static uint32_t random_next(lw_duo_t *d)
{ uint32_t x=d->random; x^=x<<13; x^=x>>17; x^=x<<5; return d->random=x; }
static uint32_t subtract(uint32_t x,uint32_t amount) { return x>amount ? x-amount : 0; }
static float distance2(float ax,float ay,float bx,float by) { float x=ax-bx,y=ay-by; return x*x+y*y; }
static float radius(const lw_actor_t *a) { return a->sleeping ? 112.0f : 60.0f; }
static bool arrived(const lw_actor_t *a) { return distance2(a->x,a->y,a->target_x,a->target_y)<0.01f; }
static lw_facing_t facing(float dx,float dy)
{
    if (fabsf(dx)>fabsf(dy)*2.0f) return dx<0 ? LW_FACE_LEFT : LW_FACE_RIGHT;
    if (fabsf(dy)>fabsf(dx)*2.0f) return dy<0 ? LW_FACE_UP : LW_FACE_DOWN;
    if (dy<0) return dx<0 ? LW_FACE_UP_LEFT : LW_FACE_UP_RIGHT;
    return dx<0 ? LW_FACE_DOWN_LEFT : LW_FACE_DOWN_RIGHT;
}
const lw_interaction_def_t *lw_duo_interaction(lw_interaction_t id)
{ return (unsigned)id<LW_DUO_INTERACTION_COUNT ? &s_catalog[id] : NULL; }
bool lw_duo_separated(const lw_duo_t *d)
{ float min=d ? radius(&d->actors[0])+radius(&d->actors[1]) : 0; return d && distance2(d->actors[0].x,d->actors[0].y,d->actors[1].x,d->actors[1].y)>=min*min-0.01f; }
static void settle(lw_actor_t *a)
{
    a->target_x=a->x; a->target_y=a->y; a->pose_ms=0; a->animation_ms=0; a->wants_sleep=false;
    a->motion=a->sleeping ? LW_ACTOR_SLEEP : LW_ACTOR_IDLE;
}
void lw_duo_init(lw_duo_t *d,uint32_t seed)
{
    if (!d) return;
    memset(d,0,sizeof(*d)); d->random=seed ? seed : 1; d->autonomous=true; d->next_joint_ms=9000;
    for (unsigned i=0;i<2;i++) {
        d->actors[i].x=i ? 678.0f : 342.0f; d->actors[i].y=i ? 434.0f : 414.0f;
        d->actors[i].facing=LW_FACE_DOWN; settle(&d->actors[i]); d->actors[i].wait_ms=1800+i*1100;
    }
}
static void release_reservation(lw_duo_t *d)
{
    d->phase=LW_DUO_FREE; d->phase_ms=0; d->interaction_ms=0; d->next_joint_ms=8000;
    for (unsigned i=0;i<2;i++) { settle(&d->actors[i]); d->actors[i].wait_ms=1800+i*500; }
}
void lw_duo_cancel(lw_duo_t *d)
{
    if (!d) return;
    if (d->phase!=LW_DUO_FREE) d->cancelled++;
    release_reservation(d);
}
bool lw_duo_move(lw_duo_t *d,unsigned index,float x,float y)
{
    if (!d || index>=2 || d->phase!=LW_DUO_FREE || d->actors[index].sleeping || !isfinite(x) || !isfinite(y)) return false;
    lw_actor_t *a=&d->actors[index]; a->target_x=clamp(x,168,856); a->target_y=clamp(y,336,466);
    if (!arrived(a)) { a->motion=LW_ACTOR_WALK; a->animation_ms=0; a->pose_ms=6000; }
    return true;
}
bool lw_duo_request(lw_duo_t *d,lw_interaction_t kind)
{
    if (!d || !lw_duo_interaction(kind) || d->phase!=LW_DUO_FREE || d->actors[0].sleeping || d->actors[1].sleeping || d->actors[0].wants_sleep || d->actors[1].wants_sleep) return false;
    float center=clamp((d->actors[0].x+d->actors[1].x)*0.5f,280,744);
    float y=kind==LW_DUO_WINDOW ? 350.0f : clamp((d->actors[0].y+d->actors[1].y)*0.5f,360,448);
    unsigned left=d->actors[0].x<=d->actors[1].x ? 0 : 1;
    for (unsigned i=0;i<2;i++) {
        float half_gap=kind==LW_DUO_REST ? 130.0f : 76.0f;
        lw_actor_t *a=&d->actors[i]; a->target_x=center+(i==left ? -half_gap : half_gap); a->target_y=y;
        a->target_x=clamp(a->target_x,168,856);
        a->motion=arrived(a) ? LW_ACTOR_IDLE : LW_ACTOR_WALK; a->animation_ms=0; a->pose_ms=0;
    }
    d->interaction=kind; d->phase=LW_DUO_APPROACH; d->phase_ms=0; d->interaction_ms=0;
    return true;
}
void lw_duo_sleep(lw_duo_t *d,unsigned i,bool asleep)
{
    if (!d || i>=2) return;
    if (d->phase!=LW_DUO_FREE) lw_duo_cancel(d);
    lw_actor_t *a=&d->actors[i],*other=&d->actors[1-i];
    a->wants_sleep=false;
    if (asleep && distance2(a->x,a->y,other->x,other->y)<(112.0f+radius(other))*(112.0f+radius(other))) {
        /* Walk to a wider spot first; never move or wake the other actor. */
        a->wants_sleep=true;
        (void)lw_duo_move(d,i,a->x<other->x ? 168.0f : 856.0f,a->y);
    } else { a->sleeping=asleep; settle(a); a->wait_ms=2200; }
}
void lw_duo_touch(lw_duo_t *d,unsigned i)
{
    if (!d || i>=2 || d->actors[i].sleeping) return; /* Explicit wake is a separate action. */
    if (d->phase!=LW_DUO_FREE) lw_duo_cancel(d);
    lw_actor_t *a=&d->actors[i]; a->wants_sleep=false; settle(a); a->facing=LW_FACE_DOWN; a->motion=LW_ACTOR_TOUCH; a->pose_ms=1400;
}
static void move_step(lw_duo_t *d,unsigned index,uint32_t ms)
{
    lw_actor_t *a=&d->actors[index],*other=&d->actors[1-index];
    if (a->sleeping || arrived(a)) return;
    float dx=a->target_x-a->x,dy=a->target_y-a->y,dist=sqrtf(dx*dx+dy*dy);
    float amount=fminf(dist,72.0f*(float)ms/1000.0f);
    float nx=a->x+dx/dist*amount,ny=a->y+dy/dist*amount;
    a->facing=facing(dx,dy);
    float min=radius(a)+radius(other);
    if (distance2(nx,ny,other->x,other->y)<min*min) return;
    a->x=nx; a->y=ny; a->motion=LW_ACTOR_WALK;
    if (arrived(a)) {
        if (a->wants_sleep) { a->sleeping=true; a->wants_sleep=false; settle(a); }
        else { a->motion=LW_ACTOR_IDLE; a->wait_ms=1600+random_next(d)%2800; }
    }
}
static void step(lw_duo_t *d,uint32_t ms)
{
    for (unsigned i=0;i<2;i++) if (!d->actors[i].sleeping || d->actors[i].motion==LW_ACTOR_SLEEP) d->actors[i].animation_ms+=ms;
    if (d->phase!=LW_DUO_FREE) {
        if (d->actors[0].sleeping || d->actors[1].sleeping) { lw_duo_cancel(d); return; }
        d->phase_ms+=ms;
        if (d->phase==LW_DUO_APPROACH) {
            move_step(d,0,ms); move_step(d,1,ms);
            if (arrived(&d->actors[0]) && arrived(&d->actors[1])) {
                unsigned left=d->actors[0].x<=d->actors[1].x ? 0 : 1;
                for (unsigned i=0;i<2;i++) {
                    d->actors[i].facing=d->interaction==LW_DUO_WINDOW ? LW_FACE_UP : i==left ? LW_FACE_RIGHT : LW_FACE_LEFT;
                    d->actors[i].motion=LW_ACTOR_IDLE;
                }
                d->phase=LW_DUO_ALIGN; d->phase_ms=0;
            } else if (d->phase_ms>=10000) lw_duo_cancel(d);
        } else if (d->phase==LW_DUO_ALIGN && d->phase_ms>=250) {
            d->phase=LW_DUO_ACT; d->phase_ms=0; d->interaction_ms=0;
            for (unsigned i=0;i<2;i++) { d->actors[i].motion=lw_duo_interaction(d->interaction)->motion; d->actors[i].animation_ms=0; }
        } else if (d->phase==LW_DUO_ACT) {
            d->interaction_ms+=ms;
            if (d->phase_ms>=lw_duo_interaction(d->interaction)->duration_ms) {
                bool rest=d->interaction==LW_DUO_REST; d->completed++;
                release_reservation(d);
                if (rest) for (unsigned i=0;i<2;i++) { d->actors[i].sleeping=true; settle(&d->actors[i]); }
            }
        }
        return;
    }
    for (unsigned i=0;i<2;i++) {
        lw_actor_t *a=&d->actors[i];
        if (a->sleeping) continue;
        if (a->motion==LW_ACTOR_WALK) {
            a->pose_ms=subtract(a->pose_ms,ms); move_step(d,i,ms);
            if (!a->pose_ms) { a->wants_sleep=false; settle(a); a->wait_ms=1000; }
        } else if (a->pose_ms) {
            a->pose_ms=subtract(a->pose_ms,ms);
            if (!a->pose_ms) { settle(a); a->wait_ms=2000; }
        } else if (d->autonomous) {
            a->wait_ms=subtract(a->wait_ms,ms);
            if (!a->wait_ms) {
                float x=200.0f+(float)(random_next(d)%625),y=350.0f+(float)(random_next(d)%105);
                if (distance2(x,y,d->actors[1-i].x,d->actors[1-i].y)>=160.0f*160.0f) (void)lw_duo_move(d,i,x,y);
                a->wait_ms=1600+random_next(d)%2200;
            }
        }
    }
    if (d->autonomous) {
        d->next_joint_ms=subtract(d->next_joint_ms,ms);
        if (!d->next_joint_ms) {
            (void)lw_duo_request(d,(lw_interaction_t)(random_next(d)%LW_DUO_INTERACTION_COUNT));
            d->next_joint_ms=12000+random_next(d)%9000;
        }
    }
}
void lw_duo_tick(lw_duo_t *d,uint32_t ms)
{
    if (!d) return;
    /* Stalls do not teleport actors or burst through queued interactions. */
    if (ms>200) ms=200;
    while (ms) { uint32_t chunk=ms>20 ? 20 : ms; step(d,chunk); ms-=chunk; }
}
