/* hud.h - gunsight, lock box, cockpit panel, radar, attitude ball, bitmap text (JS lines 3487-3660). */
#ifndef TL_HUD_H
#define TL_HUD_H

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

void hud_show_sight(int seat, int style); /* a local seat's gunsight on the nose, SIGHT_* (config.h; the hangar) */
void hud_film_labels(bool on); /* the opening film: only the nearest contact shows its range */
void hud_draw(Framebuffer *fb, Game *g, Plane *p, const HudView *V, bool label);
void hud_panel(Framebuffer *fb, Game *g, Plane *p, float x, float y, float w, float h);
/* Tests (the audit's views): told when a panel text had to be cut to fit, with what it lost. NULL: off. */
extern void (*hud_text_cut_hook)(const char *full, const char *shown);
/* Tests: a text in a panel box still wider than the box after shrinking and cutting (a number, which is never cut). */
extern void (*hud_text_over_hook)(const char *s);
/* Classic map view of the arena, over a player's view. */
void hud_map(Framebuffer *fb, Game *g, Plane *p, float x, float y, float w, float h);

/* What the cockpit's gauges read for p, in SI units: the numbers the panel
 * draws, for the test viewer to hold against the jet's real motion. */
typedef struct { float spd, alt, vs, hdg, gl, fuel, bank, pitch; } Gauges; /* bank, pitch: degrees */
void hud_gauges(Game *g, Plane *p, Gauges *out);

/* Where the last hud_draw put its marks, for the test viewer's picture
 * check: the gunsight, each enemy's box and the range written under it,
 * the lock diamond, the tanker's range and the gun's lead. */
typedef struct { int idx; float x, y, km; } HudFoe;
typedef struct {
  const Plane *p;
  float sx, sy;
  int   nfoe;
  HudFoe foe[MAX_PLANES];
  bool  lock;
  float lx, ly;
  bool  tanker;
  float tankerKm;
  bool  lead;         /* the gun's lead pipper, and where */
  float leadx, leady;
} HudMarks;
extern HudMarks hud_marks;

/* The camera of the first hud_draw since hud_view_p was cleared and whose view it was (the live test player's
 * answer sheet). */
extern HudView      hud_view;
extern const Plane *hud_view_p;
/* Tests (the live test player): each round dial as drawn, its centre and radius in framebuffer pixels, its sweep
 * (degrees clockwise from 12 o'clock) over lo..hi and the value its needle shows; v2 and len2: a second needle's
 * value and length (a share of r; 0: none). NULL: off. */
extern void (*hud_dial_hook)(float cx, float cy, float r, float a0, float a1, float lo, float hi, float v,
                             const char *title, float v2, float len2);
/* Tests (the live test player): a row of store pips as drawn, max boxes of w x h from (x, y), step apart, the first
 * n lit (colour on, the rest off). NULL: off. */
extern void (*hud_pips_hook)(float x, float y, float w, float h, float step, int n, int max, uint32_t on, uint32_t off,
                             int kind); /* kind: 0 heat-seekers, 1 radar, 2 air-to-ground */

#endif
