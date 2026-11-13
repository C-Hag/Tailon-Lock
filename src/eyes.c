/* eyes.c - the link to the live test player (eyes.h). Original code for this project. */
#include "eyes.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hud.h"
#include "raster.h"
#include "render3d.h"
#include "core/safe.h"
#include "stores.h"
#include "surfaces.h"

#if defined(_WIN32)
bool eyes_init(void) { return false; }
bool eyes_on(void) { return false; }
bool eyes_due(long frame) { (void)frame; return false; }
void eyes_begin(const Framebuffer *fb) { (void)fb; }
void eyes_send(long frame, const char *state, const char *page, Game *g, const uint32_t *px, int w, int h, int fbw,
               int fbh, bool gpu) {
  (void)frame, (void)state, (void)page, (void)g, (void)px, (void)w, (void)h, (void)fbw, (void)fbh, (void)gpu;
}
bool eyes_wait(void (*cmd)(const char *line)) { (void)cmd; return false; }
#else
#include <unistd.h>

static int  rfd = -1, wfd = -1, every = 4;
static bool recording;
static const Framebuffer *main_fb; /* the picture the HUD and menus draw into */

/* what the HUD and panels drew this frame */
#define MAX_TEXTS 384
#define MAX_DIALS 24
static struct { int x0, y0, x1, y1; char s[64]; } texts[MAX_TEXTS];
static struct { float cx, cy, r, a0, a1, lo, hi, v, v2, len2; char title[16]; } dials[MAX_DIALS];
static struct { float x, y, w, h, step; int n, max, kind; uint32_t on, off; } pips[8];
/* what was drawn on the jets (stores.h's hooks), kept for the local player's own: world points, projected at the
 * end with the HUD's camera */
#define MAX_ST 24
static struct { const Plane *p; vec3 at; int cls; bool loaded, shown; } stn[MAX_ST];
static struct { const Plane *p; vec3 at; float thr, ab; } noz[4];
static struct { const Plane *p; int leg; vec3 at; float down; } legs[4];
#define MAX_CS 48
static struct { const Plane *p; int kind, side; vec3 te; float deg; } cs[MAX_CS];
static int ntexts, ndials, npips, nstn, nnoz, nlegs, ncs;
static void (*prev_text_hook)(const Framebuffer *, const char *, int, int, int, int, uint32_t);

static void text_hook(const Framebuffer *fb, const char *s, int x0, int y0, int x1, int y1, uint32_t ink) {
  if (prev_text_hook) prev_text_hook(fb, s, x0, y0, x1, y1, ink);
  if (!recording || fb != main_fb || ntexts >= MAX_TEXTS || !s[0]) return;
  texts[ntexts].x0 = x0, texts[ntexts].y0 = y0, texts[ntexts].x1 = x1, texts[ntexts].y1 = y1;
  snprintf(texts[ntexts].s, sizeof texts[ntexts].s, "%s", s);
  ntexts++;
}

static void dial_hook(float cx, float cy, float r, float a0, float a1, float lo, float hi, float v, const char *title, float v2,
                      float len2) {
  if (!recording || ndials >= MAX_DIALS) return;
  dials[ndials].cx = cx, dials[ndials].cy = cy, dials[ndials].r = r, dials[ndials].a0 = a0, dials[ndials].a1 = a1;
  dials[ndials].lo = lo, dials[ndials].hi = hi, dials[ndials].v = v, dials[ndials].v2 = v2, dials[ndials].len2 = len2;
  snprintf(dials[ndials].title, sizeof dials[ndials].title, "%s", title ? title : "-");
  for (char *c = dials[ndials].title; *c; c++)
    if (*c == ' ') *c = '_';
  ndials++;
}

static void pips_hook(float x, float y, float w, float h, float step, int n, int max, uint32_t on, uint32_t off, int kind) {
  if (!recording || npips >= 8) return;
  pips[npips].x = x, pips[npips].y = y, pips[npips].w = w, pips[npips].h = h, pips[npips].step = step;
  pips[npips].n = n, pips[npips].max = max, pips[npips].on = on, pips[npips].off = off, pips[npips].kind = kind;
  npips++;
}

static void station_hook(const Plane *p, vec3 at, int cls, bool loaded, bool shown) {
  if (!recording || nstn >= MAX_ST) return;
  stn[nstn].p = p, stn[nstn].at = at, stn[nstn].cls = cls, stn[nstn].loaded = loaded, stn[nstn].shown = shown;
  nstn++;
}

static void nozzle_hook(const Plane *p, vec3 at, float thr, float ab) {
  if (!recording || nnoz >= 4) return;
  noz[nnoz].p = p, noz[nnoz].at = at, noz[nnoz].thr = thr, noz[nnoz].ab = ab;
  nnoz++;
}

static void gear_hook(const Plane *p, int leg, vec3 at, float down) {
  if (!recording || nlegs >= 4) return;
  legs[nlegs].p = p, legs[nlegs].leg = leg, legs[nlegs].at = at, legs[nlegs].down = down;
  nlegs++;
}

static void surface_hook(const Plane *p, int kind, int side, vec3 te, float deg) {
  if (!recording || ncs >= MAX_CS) return;
  cs[ncs].p = p, cs[ncs].kind = kind, cs[ncs].side = side, cs[ncs].te = te, cs[ncs].deg = deg;
  ncs++;
}

bool eyes_init(void) {
  const char *e = getenv("TL_EYES");
  if (!e || sscanf(e, "%d,%d", &rfd, &wfd) != 2 || rfd < 0 || wfd < 0) {
    rfd = wfd = -1;
    return false;
  }
  if (getenv("TL_EYES_EVERY")) every = (int)safe_int(getenv("TL_EYES_EVERY"), 0, INT_MIN, INT_MAX);
  if (every < 1) every = 1;
  prev_text_hook = fb_text_hook;
  fb_text_hook = text_hook;
  hud_dial_hook = dial_hook;
  hud_pips_hook = pips_hook;
  stores_station_hook = station_hook;
  stores_nozzle_hook = nozzle_hook;
  stores_gear_hook = gear_hook;
  surfaces_hook = surface_hook;
  return true;
}

bool eyes_on(void) { return wfd >= 0; }
bool eyes_due(long frame) { return wfd >= 0 && frame % every == 0; }

void eyes_begin(const Framebuffer *fb) {
  main_fb = fb;
  ntexts = ndials = npips = nstn = nnoz = nlegs = ncs = 0;
  hud_view_p = NULL; /* set again by this frame's hud_draw, if there is one */
  recording = true;
}

static void put(const void *b, size_t n) {
  const char *p = b;
  while (n > 0 && wfd >= 0) {
    ssize_t k = write(wfd, p, n);
    if (k <= 0) {
      wfd = -1; /* the player has gone */
      return;
    }
    p += k, n -= (size_t)k;
  }
}

static char   sheet[1 << 16];
static size_t used;
static void line(const char *f, ...) TL_PRINTF(1, 2);
static void line(const char *f, ...) {
  va_list a;
  va_start(a, f);
  int n = vsnprintf(sheet + used, sizeof sheet - used, f, a);
  va_end(a);
  if (n > 0 && used + (size_t)n < sizeof sheet) used += (size_t)n;
}

/* where the HUD's camera puts a world point (fb pixels); false behind the eye */
static bool project(vec3 w, float *sx, float *sy, float *z) {
  const HudView *V = &hud_view;
  vec3 d = v3_sub(w, V->pos);
  float cz = v3_dot(d, V->f);
  *z = cz;
  if (cz <= NEAR_Z) return false;
  *sx = V->cx + v3_dot(d, V->r) / cz * V->F;
  *sy = V->cy - v3_dot(d, V->u) / cz * V->F;
  return true;
}

void eyes_send(long frame, const char *state, const char *page, Game *g, const uint32_t *px, int w, int h, int fbw,
               int fbh, bool gpu) {
  recording = false;
  if (wfd < 0) return;
  used = 0;
  line("F %ld %s %s %d %d %d %d %d %d\n", frame, state, page, w, h, fbw, fbh, gpu ? 1 : 0, g->classic ? 1 : 0);
  Plane *p = g->nlocals > 0 ? &g->planes[g->locals[0]] : NULL;
  bool view = p && hud_view_p == p;
  if (p) {
    float hdg = fmodf(atan2f(p->f.x, p->f.z) * 180 / PI_F + 360, 360), pitch = asinf(clampf(p->f.y, -1, 1)) * 180 / PI_F;
    float bank = atan2f(-p->r.y, p->u.y) * 180 / PI_F;
    line("P %d %s %s %.2f %.1f %.1f %.2f %.2f %.2f %.2f %.1f %.1f %d %d %d %d %d %d %d %d %d %.2f %d\n", p->alive,
         p->mapview ? "map" : p->chase ? "chase" : "cockpit", p->jet >= 0 && p->jet < NJETS ? JETS[p->jet].id : "-", p->speed,
         p->pos.y, hdg, pitch, bank, p->gmeter, p->gload, p->fuel, p->health, (int)p->ammo, p->missiles, p->flares, p->wsel,
         p->locked, p->warn, p->stall, p->onGround || p->taxi, p->gloc > 0, p->throttle, p->lives);
    line("PG %.2f\n", p->gstress); /* the grey-out (hud.c's g_vision greys and narrows the view above 1) */
    line("Q %d %d %d %d %d %d\n", (int)p->ammo, p->missiles, p->radar, p->ground, p->flares, p->chaff);
  }
  if (view) {
    const HudView *V = &hud_view;
    line("V %.2f %.2f %.2f %.2f %.2f %.2f %.3f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n", V->vx, V->vy, V->vw, V->sh,
         V->cx, V->cy, V->F, V->r.x, V->r.y, V->r.z, V->u.x, V->u.y, V->u.z, V->f.x, V->f.y, V->f.z);
    float sx = 0, sy = 0, z = 0; /* the M line prints them even for a missile off the screen */
    for (int i = 0; i < g->nplanes; i++) {
      Plane *e = &g->planes[i];
      if (e == p || !e->alive) continue;
      bool on = project(e->pos, &sx, &sy, &z);
      line("E %d %d %d %.1f %.1f %.1f %.3f %d\n", i, e->team != p->team, on, on ? sx : 0, on ? sy : 0, z,
           v3_dist(e->pos, p->pos) / 1000, on && r3d_cloud_between(hud_view.pos, e->pos)); /* last: behind a cloud */
    }
    for (int i = 0; i < hud_marks.nfoe && hud_marks.p == p; i++) /* the last HUD's marks: only if they are p's */
      line("B %d %.1f %.1f %.3f\n", hud_marks.foe[i].idx, hud_marks.foe[i].x, hud_marks.foe[i].y, hud_marks.foe[i].km);
    if (hud_marks.lock && hud_marks.p == p) line("L %.1f %.1f\n", hud_marks.lx, hud_marks.ly);
    for (int i = 0; i < g->nmissiles; i++) {
      const Missile *m = &g->missiles[i];
      if (m->owner != p->idx) continue;
      bool on = project(m->pos, &sx, &sy, &z);
      float back_x = 0, back_y = 0, bz;
      bool bon = project(v3_sub(m->pos, v3_scale(m->dir, 40)), &back_x, &back_y, &bz); /* 40 m back along its path */
      line("M %u %d %d %.1f %.1f %.1f %.1f %.1f %.1f %.1f %d\n", m->id, m->kind, on && bon, sx, sy, z, back_x, back_y,
           m->life, v3_dist(m->pos, p->pos), on && r3d_cloud_between(hud_view.pos, m->pos));
    }
    int nb = 0;
    for (int i = 0; i < g->nbullets && nb < 24; i++) {
      const Bullet *b = &g->bullets[i];
      if (b->owner != p->idx || !project(b->pos, &sx, &sy, &z)) continue;
      line("R %.1f %.1f %.1f\n", sx, sy, z);
      nb++;
    }
    if (p->chase && project(p->pos, &sx, &sy, &z)) line("S %.1f %.1f %.1f\n", sx, sy, z);
    /* the player's own jet as drawn: stations (W cls loaded shown on x y z), nozzles (N on x y z throttle
     * afterburner), gear legs (G leg on x y z down) and control surfaces (C kind side on x y z degrees: the middle of
     * its trailing edge, surfaces.h's kinds and sides) */
    for (int i = 0; i < nstn; i++)
      if (stn[i].p == p) {
        bool on = project(stn[i].at, &sx, &sy, &z);
        line("W %d %d %d %d %.1f %.1f %.2f\n", stn[i].cls, stn[i].loaded, stn[i].shown, on, on ? sx : 0, on ? sy : 0, z);
      }
    for (int i = 0; i < nnoz; i++)
      if (noz[i].p == p) {
        bool on = project(noz[i].at, &sx, &sy, &z);
        line("N %d %.1f %.1f %.2f %.2f %.2f\n", on, on ? sx : 0, on ? sy : 0, z, noz[i].thr, noz[i].ab);
      }
    for (int i = 0; i < nlegs; i++)
      if (legs[i].p == p) {
        bool on = legs[i].leg >= 0 && project(legs[i].at, &sx, &sy, &z);
        line("G %d %d %.1f %.1f %.2f %.2f\n", legs[i].leg, on, on ? sx : 0, on ? sy : 0, on ? z : 0, legs[i].down);
      }
    for (int i = 0; i < ncs; i++)
      if (cs[i].p == p) {
        bool on = project(cs[i].te, &sx, &sy, &z);
        line("C %d %d %d %.1f %.1f %.2f %.1f\n", cs[i].kind, cs[i].side, on, on ? sx : 0, on ? sy : 0, on ? z : 0, cs[i].deg);
      }
  }
  for (int i = 0; i < ntexts; i++)
    line("T %d %d %d %d %s\n", texts[i].x0, texts[i].y0, texts[i].x1, texts[i].y1, texts[i].s);
  for (int i = 0; i < ndials; i++)
    line("D %.1f %.1f %.1f %.1f %.1f %.2f %.2f %.3f %s %.3f %.2f\n", dials[i].cx, dials[i].cy, dials[i].r, dials[i].a0, dials[i].a1,
         dials[i].lo, dials[i].hi, dials[i].v, dials[i].title, dials[i].v2, dials[i].len2); /* last: a second needle */
  for (int i = 0; i < npips; i++)
    line("K %.1f %.1f %.1f %.1f %.2f %d %d %06x %06x %d\n", pips[i].x, pips[i].y, pips[i].w, pips[i].h, pips[i].step, pips[i].n,
         pips[i].max, pips[i].on & 0xFFFFFFu, pips[i].off & 0xFFFFFFu, pips[i].kind);
  line("PX %d %d\n", px ? w : 0, px ? h : 0);
  put(sheet, used);
  if (px) put(px, (size_t)w * (size_t)h * 4);
}

bool eyes_wait(void (*cmd)(const char *line)) {
  if (wfd < 0 || rfd < 0) return false;
  char buf[256];
  size_t n = 0;
  for (;;) {
    char c;
    ssize_t k = read(rfd, &c, 1);
    if (k <= 0) {
      wfd = rfd = -1;
      return false;
    }
    if (c != '\n') {
      if (n < sizeof buf - 1) buf[n++] = c;
      continue;
    }
    buf[n] = 0;
    n = 0;
    if (!strcmp(buf, "go")) return true;
    cmd(buf);
  }
}
#endif
