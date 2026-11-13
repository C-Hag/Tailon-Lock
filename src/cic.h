/* cic.h - the carrier's control room in the opening film, drawn in code
 * (cic.c). Original code for this project. */
#ifndef TL_CIC_H
#define TL_CIC_H

#include "raster.h"

/* The control room at film time t, filling all of out (opaque); the shot runs from t0 to t1. */
void cic_draw(Framebuffer *out, float t, float t0, float t1);

/* The radar scope alone, filling all of out (opaque), for a shot from t0 to t1. */
void cic_radar_draw(Framebuffer *out, float t, float t0, float t1);

/* The menu's background: the room after the film, quiet, its big tactical display framing the menu's box (x, y,
 * w x h pixels, drawn at scale k) on an empty face; t the menu's clock. Fills all of out (opaque). */
void cic_menu_draw(Framebuffer *out, float t, int x, int y, int w, int h, int k);

#endif
