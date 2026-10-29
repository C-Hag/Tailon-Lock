/* sim.h - flight, guns, missiles, flares, damage, tanker refuel (JS lines 2975-3280).
 * Human controls (ctl and throttle) are written by the input layer before
 * sim_update runs; CPU pilots are steered from inside it. */
#ifndef A4C_SIM_H
#define A4C_SIM_H

#include "core/game.h"

void sim_update(Game *g, float dt);
void sim_update_tanker(Game *g, float dt);
/* The acceleration along the flight path at throttle thr (m/s^2): energy. */
float sim_excess_power(const Game *g, const Plane *p, const JetType *jt, float thr);

/* Network play: apply what the other computer reports. */
void sim_remote_down(Game *g, Plane *p, const char *how); /* its jet went down */
void sim_remote_up(Game *g, Plane *p);                    /* and is back from its last snapshot */
void sim_target_fall(Game *g, Target *t, int by);          /* one of its targets fell */
void sim_remote_missile(Game *g, const Missile *m);       /* it launched a missile */
void sim_remote_flares(Game *g, Plane *p, bool chaff);    /* its jet dropped decoys */
void sim_missile_gone(Game *g, uint32_t id, vec3 pos);    /* one of its missiles blew up */

#endif
