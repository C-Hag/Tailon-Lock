/* hud.h - gunsight, lock box, cockpit panel, radar, attitude ball, bitmap text (JS lines 3487-3660). */
#ifndef A4C_HUD_H
#define A4C_HUD_H

#include <stdbool.h>

#include "core/game.h"
#include "raster.h"

#define HUDC  0xa8f08c
#define DIM   0x8a83c9
#define INK   0xe8e4ff
#define WARN  0xff5a48
#define AMBER 0xffd25a

typedef struct {
  vec3  pos, r, u, f;
  float vx, vy, vw, sh, cx, cy, F;
} HudView;

void hud_draw(Framebuffer *fb, Game *g, Plane *p, const HudView *V, bool label);
void hud_panel(Framebuffer *fb, Game *g, Plane *p, float x, float y, float w, float h);
/* Classic map view of the arena, over a player's view. */
void hud_map(Framebuffer *fb, Game *g, Plane *p, float x, float y, float w, float h);

#endif
