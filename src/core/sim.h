/* sim.h - flight, guns, missiles, flares, damage, tanker refuel (JS lines 2975-3280).
 * Human controls (ctl and throttle) are written by the input layer before
 * sim_update runs; CPU pilots are steered from inside it. */
#ifndef A4C_SIM_H
#define A4C_SIM_H

#include "core/game.h"

#define GEAR_VLE 150.0f /* m/s (290 kt): the gear's top speed (and the panels' gear lamp red past it) */

void sim_update(Game *g, float dt);
void sim_update_tanker(Game *g, float dt);
void sim_reset_tankers(Game *g);
/* The tanker key: the pilot's side's tanker in, once a match (false, with a message, if not) */
bool sim_call_tanker(Game *g, Plane *p);
/* "TANKER READY", "INBOUND", "ON STATION", "USED", "LOST" or "NO TANKER" (sudden death), for the HUD and the panel */
const char *sim_tanker_status(const Game *g, const Plane *p);
/* The test pilots' hands (testpilot.c): called around every CPU pilot's
 * decision, before (after false) and after it, to fly it as a person would. */
extern void (*sim_hands)(Game *g, Plane *p, float dt, bool after);
/* The acceleration along the flight path at throttle thr (m/s^2): energy. */
float sim_excess_power(const Game *g, const Plane *p, const JetType *jt, float thr);

/* Network play: apply what the other computer reports. */
void sim_remote_down(Game *g, Plane *p, const char *how); /* its jet went down */
void sim_remote_up(Game *g, Plane *p);                    /* and is back from its last snapshot */
void sim_target_fall(Game *g, Target *t, int by);          /* one of its targets fell */
/* the wheel brakes on a runway, m/s/s: arcade stops in a shorter roll-out than realistic */
static inline float sim_brake(const Game *g) { return g->realistic ? 9.0f : 14.0f; }
void sim_remote_missile(Game *g, const Missile *m);       /* it launched a missile */
void sim_remote_flares(Game *g, Plane *p, bool chaff);    /* its jet dropped decoys */
void sim_missile_gone(Game *g, uint32_t id, vec3 pos);    /* one of its missiles blew up */

#endif
