/* Black-box invariants for scheduling, sleep and collision. SPDX-License-Identifier: MIT */
#include "lw_duo.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(name,value) do { checks++; if (!(value)) { failures++; printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,name); } } while (0)
static void run(lw_duo_t *d,unsigned ms) { for (unsigned n=0;n<ms;n+=20) lw_duo_tick(d,20); }
int main(void)
{
    lw_duo_t a,b; lw_duo_init(&a,123); lw_duo_init(&b,123);
    bool deterministic=true,separated=true,bounded=true,continuous=true;
    for (unsigned i=0;i<12000;i++) {
        if (i && i%1500==0) { /* Scripted user wake, never an autonomous wake. */
            for (unsigned j=0;j<2;j++) { lw_duo_sleep(&a,j,false); lw_duo_sleep(&b,j,false); }
        }
        float previous[4]={a.actors[0].x,a.actors[0].y,a.actors[1].x,a.actors[1].y};
        lw_duo_tick(&a,20); lw_duo_tick(&b,20);
        deterministic=deterministic && memcmp(&a,&b,sizeof(a))==0;
        separated=separated && lw_duo_separated(&a);
        for (unsigned j=0;j<2;j++) {
            bounded=bounded && a.actors[j].x>=168 && a.actors[j].x<=856 && a.actors[j].y>=336 && a.actors[j].y<=466;
            float dx=a.actors[j].x-previous[j*2],dy=a.actors[j].y-previous[j*2+1];
            continuous=continuous && dx*dx+dy*dy<=2.08f;
        }
    }
    CHECK("four simulated minutes are deterministic",deterministic);
    CHECK("autonomy never overlaps bodies",separated);
    CHECK("autonomy remains inside room",bounded);
    CHECK("no teleport in movement or interaction",continuous);
    lw_duo_init(&a,5); a.autonomous=false;
    float still=a.actors[1].x;
    CHECK("actor A has independent target",lw_duo_move(&a,0,200,430)); run(&a,800);
    CHECK("A moved at walking speed",a.actors[0].x<300 && a.actors[0].x>260);
    CHECK("B was not dragged with A",a.actors[1].x==still);
    float x=a.actors[0].x; lw_duo_tick(&a,600000);
    CHECK("stalled clock is bounded",fabsf(a.actors[0].x-x)<=14.5f);
    CHECK("invalid actor cannot move",!lw_duo_move(&a,2,400,400));
    CHECK("invalid interaction rejected",!lw_duo_request(&a,(lw_interaction_t)99));
    CHECK("NaN target rejected",!lw_duo_move(&a,0,NAN,400));
    for (unsigned kind=0;kind<LW_DUO_INTERACTION_COUNT;kind++) {
        lw_duo_init(&a,kind+1); a.autonomous=false;
        CHECK("interaction accepted from awake/free",lw_duo_request(&a,(lw_interaction_t)kind));
        CHECK("second interaction cannot steal reservation",!lw_duo_request(&a,LW_DUO_LETTER));
        bool acted=false,sync=true,arrival=true,overlap=false;
        for (unsigned n=0;n<12000;n+=20) {
            lw_duo_tick(&a,20); overlap=overlap || !lw_duo_separated(&a);
            if (a.phase==LW_DUO_ACT) {
                acted=true; sync=sync && a.actors[0].animation_ms==a.actors[1].animation_ms;
                arrival=arrival && fabsf(a.actors[0].y-a.actors[1].y)<0.1f;
            }
            if (a.completed) break;
        }
        CHECK("both reach action phase",acted);
        CHECK("joint timeline synchronized",sync);
        CHECK("joint action waits for aligned slots",arrival);
        CHECK("joint approach avoids collision",!overlap);
        CHECK("joint interaction finishes",a.completed==1 && a.phase==LW_DUO_FREE && a.cancelled==0);
        if (kind==LW_DUO_REST) CHECK("joint rest remains asleep",a.actors[0].sleeping && a.actors[1].sleeping);
        else CHECK("joint completion restores free behavior",!a.actors[0].sleeping && !a.actors[1].sleeping);
    }
    lw_duo_init(&a,9); lw_duo_sleep(&a,1,true); float sx=a.actors[1].x,sy=a.actors[1].y;
    CHECK("sleep blocks coordinator",!lw_duo_request(&a,LW_DUO_GREET));
    lw_duo_touch(&a,1); run(&a,60000);
    CHECK("touch and autonomy never force wake",a.actors[1].sleeping && a.actors[1].x==sx && a.actors[1].y==sy);
    CHECK("sleeping footprint protected",lw_duo_separated(&a));
    lw_duo_sleep(&a,1,false); CHECK("explicit wake works",!a.actors[1].sleeping);
    lw_duo_init(&a,8); a.autonomous=false; (void)lw_duo_request(&a,LW_DUO_STAND); run(&a,6000);
    CHECK("standing close before sleep",a.completed==1);
    float before_x=a.actors[0].x; lw_duo_sleep(&a,0,true);
    CHECK("nearby sleep walks away first",a.actors[0].x==before_x && !a.actors[0].sleeping && a.actors[0].wants_sleep);
    run(&a,6000); CHECK("reserved sleeping spot reached",a.actors[0].sleeping && lw_duo_separated(&a));
    lw_duo_init(&a,8); a.autonomous=false; (void)lw_duo_request(&a,LW_DUO_STAND); run(&a,6000);
    lw_duo_sleep(&a,0,true); lw_duo_cancel(&a);
    CHECK("cancelling pending sleep clears reservation",!a.actors[0].wants_sleep && !a.actors[0].sleeping);
    CHECK("cancelled sleep permits later interaction",lw_duo_request(&a,LW_DUO_GREET));
    lw_duo_init(&a,1); (void)lw_duo_request(&a,LW_DUO_LETTER); run(&a,400);
    lw_duo_touch(&a,0); CHECK("touch cancels both-person reservation safely",a.phase==LW_DUO_FREE && a.cancelled==1 && a.actors[0].motion==LW_ACTOR_TOUCH);
    lw_duo_sleep(&a,1,true); CHECK("only selected actor sleeps",!a.actors[0].sleeping && a.actors[1].sleeping);
    lw_duo_init(&a,99); CHECK("bounds cannot teleport actors",!lw_duo_set_bounds(&a,510,894,380,460));
    CHECK("invalid huge scene rejected",!lw_duo_set_bounds(&a,0,1e30f,0,600));
    CHECK("valid custom bounds accepted",lw_duo_set_bounds(&a,300,800,350,460));
    bool custom=true; for (unsigned i=0;i<6000;i++) { lw_duo_tick(&a,20); for (unsigned j=0;j<2;j++) if (a.actors[j].x<300 || a.actors[j].x>800 || a.actors[j].y<350 || a.actors[j].y>460) custom=false; }
    CHECK("custom bounds contain autonomous actors",custom);
    printf("Duo model: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
