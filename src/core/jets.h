/* jets.h - the selectable jets (JS JETS, JT, setJet; reference lines 364-388
 * of the hangar version). Their data comes from assets/jets.json; without it
 * there is one jet, the original built-in mesh with neutral stats. */
#ifndef A4C_JETS_H
#define A4C_JETS_H

#include "core/mesh.h"

#define MAX_JETS 16

typedef struct {
  char  id[8], des[16], name[24], role[160];
  float g_speed, g_turn, g_climb, g_range; /* hangar scores, 3-10 */
  /* flight-model factors, each within 8% of 1 */
  float turn, roll, speed, climb, burn;
  float tail; /* afterburner glow distance behind the origin */
  float g_max, g_min; /* structural / fly-by-wire limits */
  Mesh  mesh[2], lod[2]; /* per team, tinted toward the team colour */
} JetType;

extern JetType JETS[MAX_JETS];
extern int     NJETS;

/* Adds a jet from its hangar scores and two untinted meshes (near and far).
 * Returns its index, or -1 when the table is full. */
int  jets_add(const char *id, const char *des, const char *name, const char *role, float speed, float turn,
              float climb, float range, float tail, const Mesh *near, const Mesh *far);
/* Puts the built-in jet in if nothing was loaded. */
void jets_finish(void);
int  jets_find(const char *id);

/* Energy-maneuverability (Boyd): the g the wing can pull at a speed, which
 * grows with the square of the speed until the airframe limit; the corner
 * speed, where it first reaches that limit and the jet turns fastest. */
float jet_lift_g(const JetType *j, float speed);
float jet_corner(const JetType *j);

#endif
