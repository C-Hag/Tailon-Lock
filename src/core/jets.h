/* jets.h - the selectable jets (JS JETS, JT, setJet; reference lines 364-388
 * of the hangar version). Their data comes from assets/jets.json; without it
 * there is one jet, the original built-in mesh with neutral stats. */
#ifndef A4C_JETS_H
#define A4C_JETS_H

#include <stdbool.h>

#include "core/mesh.h"

#define MAX_JETS 16

typedef struct {
  char  id[8], des[16], name[24], role[160];
  float g_speed, g_turn, g_climb, g_range; /* hangar scores, 3-10 */
  /* flight-model factors, each within 8% of 1 */
  float turn, roll, speed, climb, burn;
  float tail; /* afterburner glow distance behind the origin */
  float g_max, g_min; /* structural / fly-by-wire limits */
  /* the data sheet (jets_data.json), 0 when there is none: sea-level top
   * speed and corner speed (m/s), thrust to weight, roll rate (rad/s), and
   * the induced drag of a turn (sim_excess_power's factor, from the wing's
   * K and loading). Realistic handling flies these numbers (JETS_REAL). */
  float s_vmax, s_corner, s_tw, s_roll, s_induced;
  Mesh  mesh[2], lod[2]; /* per team, tinted toward the team colour */
} JetType;

extern JetType JETS[MAX_JETS];
extern int     NJETS;
/* The same jets with their flight-model factors set from their data sheets,
 * for realistic handling; arcade keeps the hangar-score factors of JETS. */
extern JetType JETS_REAL[MAX_JETS];
static inline const JetType *jet_type(int jet, bool realistic) { return realistic ? &JETS_REAL[jet] : &JETS[jet]; }
/* The drone of Beat the Drone! sits in the last slot, outside the NJETS
 * that the menus pick from. */
#define DRONE_JET (MAX_JETS - 1)

/* Adds a jet from its hangar scores and two untinted meshes (near and far).
 * Returns its index, or -1 when the table is full. */
int  jets_add(const char *id, const char *des, const char *name, const char *role, float speed, float turn,
              float climb, float range, float tail, const Mesh *near, const Mesh *far);
/* Puts the built-in jet in if nothing was loaded, and the drone. */
void jets_finish(void);
int  jets_find(const char *id);

/* Energy-maneuverability (Boyd): the g the wing can pull at a speed, which
 * grows with the square of the speed until the airframe limit; the corner
 * speed, where it first reaches that limit and the jet turns fastest. */
float jet_lift_g(const JetType *j, float speed);
float jet_corner(const JetType *j);

#endif
