/* viewfx.h - the match's outside camera views and the effects that are only for the eye: the banking chase, the
 * flyby and launch cameras, contrails, wingtip and wing vapour, the vapour cone, sea spray, catapult steam and the
 * afterburner's light-up. Render only: nothing here reads or writes the game's random numbers or changes its
 * state, so the simulation, the audit digest and network play are untouched. */
#ifndef TL_VIEWFX_H
#define TL_VIEWFX_H

#include "core/game.h"
#include "render3d.h"

enum { VIEW_COCKPIT, VIEW_CHASE, VIEW_FLYBY, VIEW_COUNT };

/* The view a jet is seen from (its chase flag says cockpit or outside; the flyby is kept here). */
int viewfx_mode(const Plane *p);
/* Tab: cockpit, chase, flyby, cockpit; returns the new view's name for the HUD. */
const char *viewfx_cycle(Plane *p);
/* The outside camera for this jet's view; *zoom > 1 asks for a longer lens than the game's (the flyby). */
void viewfx_camera(const Game *g, const Plane *p, Camera *c, float *zoom);
/* The feel of speed: *fscale < 1 widens the lens, and the camera trembles a little (the view only) */
void viewfx_speed(const Game *g, const Plane *p, Camera *c, float *fscale);
/* Steps the effects to the game's clock (once per new time, however many views draw it). */
void viewfx_update(const Game *g);
/* Draws them into the view (balls and segments, sorted with the scene's other alpha). */
void viewfx_draw(Framebuffer *fb, const View *V, const Game *g);

#endif
