/* surfaces.h - the jets' control surfaces, moving with the stick: ailerons, elevators (tailerons on a jet with a
 * tail, elevons on a tailless delta), canards, rudders, and on realistic handling the flaps. Found in each jet's
 * model where its wings and fins have their hinge, cut out of the bare model (stores_bare) and drawn turned about
 * that hinge, on both renderers. The picture's alone: the sim never reads it. */
#ifndef TL_SURFACES_H
#define TL_SURFACES_H

#include "render3d.h"

enum { SF_AILERON, SF_ELEVON, SF_ELEVATOR, SF_CANARD, SF_RUDDER, SF_FLAP, SF_KINDS };
extern const char *const SF_NAMES[SF_KINDS];

/* Whether face k of the jet's (near) model is part of a control surface, so the bare model leaves it out. */
bool surfaces_cut(int jet, int face);
/* The jet's control surfaces at their deflections, where its near model is drawn (the far one is rigid). */
void surfaces_draw(Framebuffer *fb, const View *V, const Game *g, const Plane *p);
/* How many pieces of each kind the jet's model has (n[SF_KINDS]); for the audit and the tests. */
void surfaces_count(int jet, int n[SF_KINDS]);

/* For the live test player's answer sheet (eyes.c): each piece drawn this frame, its kind, its side (-1 left of the
 * centre line, 1 right, 0 on it), the middle of its trailing edge in the world and its deflection in degrees
 * (positive: the trailing edge up, or to the right for a rudder). Unset, nothing is told. */
extern void (*surfaces_hook)(const Plane *p, int kind, int side, vec3 te, float deg);

#endif
