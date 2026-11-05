/* stores.h - what the jets carry and what they fire, as it is drawn: the missile
 * models, their exhaust and smoke trails, and every jet's stations (wingtip
 * rails, wing pylons, belly stations, internal bays) loaded from the jet's
 * stores. All of it is the picture's alone: the sim never reads it. */
#ifndef A4C_STORES_H
#define A4C_STORES_H

#include "render3d.h"

/* The missiles in flight, with their flames and trails, and the trails of
 * missiles already gone, fading. */
void stores_missiles(Framebuffer *fb, const View *V, const Game *g);
/* One jet's stations and what is still on them (bay doors open a moment
 * after a launch from inside). */
void stores_jet(Framebuffer *fb, const View *V, const Game *g, const Plane *p);
/* The landing gear, as far down as it is (realistic: the pilot's; arcade: on its own), and its doors. */
void stores_gear(Framebuffer *fb, const View *V, const Plane *p);
/* The jet's model without the missiles modelled on it (stores_jet draws what is really there). */
const Mesh *stores_bare(int jet, int team);
/* The engines' exhaust at each nozzle of the jet's model: a glow at dry thrust, plumes in afterburner. */
void stores_exhaust(const View *V, const Game *g, const Plane *p);

/* For the live test player's answer sheet (eyes.c), what was drawn on a jet this frame: each station of its full
 * load (cls 0 heat, 1 radar, 2 air-to-ground; loaded: its missile on it; shown: not shut in a closed bay), each
 * nozzle's exhaust (throttle, afterburner share 0-1) and each gear leg's wheel (leg 0 nose, 1-2 mains; how far down; leg -1:
 * the gear up, nothing drawn). Unset, nothing is told. */
extern void (*stores_station_hook)(const Plane *p, vec3 at, int cls, bool loaded, bool shown);
extern void (*stores_nozzle_hook)(const Plane *p, vec3 at, float throttle, float ab);
extern void (*stores_gear_hook)(const Plane *p, int leg, vec3 wheel, float down);

#endif
