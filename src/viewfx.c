/* viewfx.c - the outside views and the effects for the eye (see viewfx.h). Everything here is drawn from the
 * jets' state as the simulation leaves it, on a clock and a random source of its own. */
#include "viewfx.h"

#include <math.h>
#include <string.h>

#include "core/jets.h"
#include "core/world.h"
#include "gpu.h"
#include "raster.h"

#define CONTRAIL_ALT  6000.0f /* m: above this the exhaust freezes into a trail (higher in life; the game's fights sit lower) */
#define CONTRAIL_N    96      /* samples a jet, every 0.1 s: 9.6 s of trail */
#define CONTRAIL_LIFE 9.0f
#define TIP_N         40      /* wingtip vapour samples, every 1/30 s */
#define TIP_LIFE      1.2f
#define MAX_VP        480     /* spray and steam puffs */

/* the effects' own random numbers: never the game's */
static uint32_t vis_seed = 0x9e3779b9u;
static float vrnd(void) {
  vis_seed = vis_seed * 1664525u + 1013904223u;
  return (float)(vis_seed >> 8) / 16777216.0f;
}

typedef struct { vec3 a, b; float t; bool on; } Sample;

typedef struct {
  /* cameras */
  bool  flyby;
  float camT;
  vec3  cf, cu;
  bool  camOk;
  vec3  spot;
  bool  spotOk, spotDeck;
  int   spots;
  /* effects */
  Sample trail[CONTRAIL_N];
  int    ntrail;
  float  trailT;
  Sample tip[TIP_N];
  int    ntip;
  float  tipT;
  float  abFlash, lastAb;
} PlaneFx;

typedef struct { vec3 p, v; float life, max, s0, s1; uint32_t col; float a; bool spray; } VPart;

static PlaneFx PF[MAX_PLANES];
static VPart vp[MAX_VP];
static int nvp;
static float fxT = -1;
static const Game *fxG;

static float ab_of(const Plane *p) { return clampf((p->throttle - 0.9f) / 0.1f, 0, 1) * (p->throttle > 0.9f); }
static float span_of(const Plane *p) { return JETS[p->jet].mesh[0].rad * 0.55f; }
static float tail_of(const Plane *p) { return JETS[p->jet].tail; }
static bool over_water(float x, float z) {
  return MAP && MAP->sea && map_carrier_at(MAP, x, z) < 0 && map_height(MAP, x, z) <= MAP->water_level + 0.05f;
}

/* ---- views ------------------------------------------------------------ */
int viewfx_mode(const Plane *p) {
  if (!p->chase) return VIEW_COCKPIT;
  return p->idx >= 0 && p->idx < MAX_PLANES && PF[p->idx].flyby ? VIEW_FLYBY : VIEW_CHASE;
}

const char *viewfx_cycle(Plane *p) {
  if (p->idx < 0 || p->idx >= MAX_PLANES) {
    p->chase = !p->chase;
    return p->chase ? "CHASE VIEW" : "COCKPIT VIEW";
  }
  PlaneFx *s = &PF[p->idx];
  if (!p->chase) {
    p->chase = true;
    s->flyby = false;
    s->camOk = false;
    return "CHASE VIEW";
  }
  if (!s->flyby) {
    s->flyby = true;
    s->spotOk = false;
    return "FLYBY VIEW";
  }
  p->chase = false;
  s->flyby = false;
  return "COCKPIT VIEW";
}

static void look_at(Camera *c, vec3 from, vec3 at, vec3 up) {
  c->pos = from;
  c->f = v3_norm(v3_sub(at, from));
  vec3 r = v3_cross(up, c->f);
  if (v3_len(r) < 1e-3f) r = v3_cross(v3(0, 0, 1), c->f);
  c->r = v3_norm(r);
  c->u = v3_cross(c->f, c->r);
}

/* The banking chase: behind and above the jet on a short lag, the horizon leaning a little more than half of the
 * bank, so a turn reads as a turn, as a camera plane flying formation would see it. */
static void chase_cam(const Game *g, const Plane *p, PlaneFx *s, Camera *c) {
  float dt = g->time - s->camT;
  s->camT = g->time;
  vec3 up = v3(0, 1, 0);
  vec3 tu = v3_lerp(up, p->u, 0.6f);
  if (v3_len(tu) < 0.3f || p->u.y < -0.2f) tu = p->u; /* inverted: roll with the jet */
  tu = v3_norm(tu);
  if (!s->camOk || dt < 0 || dt > 0.5f) {
    s->cf = p->f;
    s->cu = tu;
    s->camOk = true;
  } else {
    s->cf = v3_norm(v3_lerp(s->cf, p->f, 1 - expf(-dt / 0.16f)));
    s->cu = v3_norm(v3_lerp(s->cu, tu, 1 - expf(-dt / 0.22f)));
  }
  vec3 pos = v3_add(v3_add(p->pos, v3_scale(s->cf, -38)), v3_scale(s->cu, 9));
  look_at(c, pos, v3_add(p->pos, v3_scale(p->f, 22)), s->cu);
  c->ext = true;
}

/* The flyby: a camera left in the jet's path, low beside it, that it roars past; a long lens follows it away
 * until the next one is set down ahead. On the deck or the runway the camera stands by the catapult or the
 * strip ahead of the jet: the launch camera. */
static void flyby_cam(const Game *g, const Plane *p, PlaneFx *s, Camera *c, float *zoom) {
  (void)g;
  bool deck = p->oncat || p->taxi || p->onGround;
  vec3 dir = p->speed > 20 ? v3_norm(p->vel) : p->f;
  vec3 flat = v3_norm(v3(dir.z, 0, -dir.x)); /* level, to one side of the path */
  if (v3_len(flat) < 0.5f) flat = p->r;
  bool renew = !s->spotOk || s->spotDeck != deck;
  if (s->spotOk && !renew) {
    vec3 d = v3_sub(p->pos, s->spot);
    float along = v3_dot(d, dir), dist = v3_len(d);
    if (along > (deck ? 45 : 320) || dist > 1800) renew = true;
  }
  if (renew) {
    float side = (s->spots++ & 1) ? 1.0f : -1.0f;
    vec3 sp;
    if (deck) sp = v3_add(v3_add(p->pos, v3_scale(p->f, 75)), v3_add(v3_scale(flat, side * 16), v3(0, 3.0f, 0)));
    else sp = v3_add(v3_add(p->pos, v3_scale(dir, 300 + p->speed * 0.4f)), v3_add(v3_scale(flat, side * 40), v3_scale(p->u, 6)));
    float h = terrain_h(sp.x, sp.z) + (deck ? 2.0f : 4.0f);
    if (sp.y < h) sp.y = h;
    s->spot = sp;
    s->spotOk = true;
    s->spotDeck = deck;
  }
  look_at(c, s->spot, p->pos, v3(0, 1, 0));
  c->ext = true;
  float dist = v3_dist(s->spot, p->pos);
  *zoom = clampf(dist / 55, 1, 10); /* the jet about as big as from 55 m on the game's lens */
}

void viewfx_camera(const Game *g, const Plane *p, Camera *c, float *zoom) {
  *zoom = 1;
  if (p->idx < 0 || p->idx >= MAX_PLANES) return;
  PlaneFx *s = &PF[p->idx];
  if (s->flyby) flyby_cam(g, p, s, c, zoom);
  else chase_cam(g, p, s, c);
}

/* ---- effects ---------------------------------------------------------- */
static void puff(vec3 p, vec3 v, float life, float s0, float s1, uint32_t col, float a, bool spray) {
  if (nvp >= MAX_VP) return;
  vp[nvp++] = (VPart){ p, v, life, life, s0, s1, col, a, spray };
}

static void push(Sample *buf, int *n, int cap, Sample s) {
  if (*n == cap) {
    memmove(buf, buf + 1, sizeof(Sample) * (size_t)(cap - 1));
    (*n)--;
  }
  buf[(*n)++] = s;
}

static void reset_all(void) {
  for (int i = 0; i < MAX_PLANES; i++) {
    PF[i].ntrail = PF[i].ntip = 0;
    PF[i].abFlash = 0;
    PF[i].lastAb = 0;
    PF[i].camOk = PF[i].spotOk = false;
  }
  nvp = 0;
}

void viewfx_update(const Game *g) {
  if (g != fxG || g->time < fxT || g->time - fxT > 1.0f) { /* a new match, or a jump: start clean */
    fxG = g;
    fxT = g->time;
    reset_all();
    return;
  }
  float dt = g->time - fxT;
  if (dt <= 0) return;
  fxT = g->time;
  for (int i = 0; i < g->nplanes && i < MAX_PLANES; i++) {
    const Plane *p = &g->planes[i];
    PlaneFx *s = &PF[i];
    if (!p->alive || p->inactive || p->jet < 0 || p->jet >= MAX_JETS) {
      s->ntrail = s->ntip = 0;
      s->lastAb = 0;
      continue;
    }
    /* the afterburner lighting: a flash in the nozzle as it catches */
    float ab = p->fuel > 0 ? ab_of(p) : 0;
    if (ab > 0.5f && s->lastAb <= 0.5f) s->abFlash = 0.35f;
    s->lastAb = ab;
    s->abFlash = fmaxf(0, s->abFlash - dt);
    vec3 noz = v3_sub(p->pos, v3_scale(p->f, tail_of(p)));
    /* contrails: high up, the exhaust freezes into a white trail that spreads and fades */
    if (g->time - s->trailT >= 0.1f) {
      s->trailT = g->time;
      float sp = span_of(p) * 0.18f;
      bool on = p->pos.y > CONTRAIL_ALT && p->fuel > 0 && p->speed > 60;
      if (on || (s->ntrail && s->trail[s->ntrail - 1].on))
        push(s->trail, &s->ntrail, CONTRAIL_N,
             (Sample){ v3_add(noz, v3_scale(p->r, -sp)), v3_add(noz, v3_scale(p->r, sp)), g->time, on });
    }
    while (s->ntrail && g->time - s->trail[0].t > CONTRAIL_LIFE) {
      memmove(s->trail, s->trail + 1, sizeof(Sample) * (size_t)(s->ntrail - 1));
      s->ntrail--;
    }
    /* wingtip vapour: hard turns in the thick air low down */
    if (g->time - s->tipT >= 1.0f / 30) {
      s->tipT = g->time;
      float span = span_of(p);
      bool on = fabsf(p->gload) > 5.0f && p->pos.y < 4000 && p->speed > 120;
      if (on || (s->ntip && s->tip[s->ntip - 1].on))
        push(s->tip, &s->ntip, TIP_N,
             (Sample){ v3_add(p->pos, v3_scale(p->r, -span)), v3_add(p->pos, v3_scale(p->r, span)), g->time, on });
    }
    while (s->ntip && g->time - s->tip[0].t > TIP_LIFE) {
      memmove(s->tip, s->tip + 1, sizeof(Sample) * (size_t)(s->ntip - 1));
      s->ntip--;
    }
    /* sea spray: a fast jet low over the water whips up a rooster tail behind it */
    float agl = MAP ? p->pos.y - MAP->water_level : 1e9f;
    if (agl < 35 && agl > 1 && p->speed > 110 && over_water(p->pos.x, p->pos.z)) {
      float k = (1 - agl / 35) * clampf((p->speed - 110) / 180, 0.2f, 1) * (0.5f + 0.5f * ab);
      float want = 90 * k * dt;
      while (want > 0) {
        if (want < 1 && vrnd() > want) break;
        want -= 1;
        vec3 at = v3_sub(noz, v3_scale(p->f, 4 + 10 * vrnd()));
        at.x += (vrnd() - 0.5f) * 6;
        at.z += (vrnd() - 0.5f) * 6;
        at.y = MAP->water_level + 0.4f;
        vec3 v = v3_add(v3_scale(p->vel, 0.18f), v3((vrnd() - 0.5f) * 8, 4 + 7 * vrnd(), (vrnd() - 0.5f) * 8));
        puff(at, v, 0.7f + 0.7f * vrnd(), 1.4f, 4.5f, 0xf2f6f8, 0.5f, true);
      }
    }
    /* the catapult: steam leaking from the track ahead while the jet waits, a cloud of it behind on the stroke */
    if (p->oncat) {
      float want = (p->cat > 0 ? 24 : 6) * dt;
      while (want > 0) {
        if (want < 1 && vrnd() > want) break;
        want -= 1;
        vec3 at = p->cat > 0 ? v3_add(v3_sub(p->pos, v3_scale(p->f, 2 + 6 * vrnd())), v3_scale(p->r, (vrnd() - 0.5f) * 6))
                             : v3_add(p->pos, v3_scale(p->f, 18 + 60 * vrnd()));
        at.y = terrain_h(at.x, at.z) + 0.3f;
        vec3 v = v3((vrnd() - 0.5f) * 1.6f, 0.9f + 1.4f * vrnd(), (vrnd() - 0.5f) * 1.6f);
        puff(at, v, 1.6f + 1.4f * vrnd(), 1.2f, 3.4f, 0xeef0f2, 0.42f, false);
      }
    }
  }
  for (int i = 0; i < nvp;) {
    VPart *q = &vp[i];
    q->v = v3_scale(q->v, expf(-1.2f * dt));
    if (q->spray) q->v.y -= 7 * dt;
    else q->v.y += 0.3f * dt;
    q->p = v3_add(q->p, v3_scale(q->v, dt));
    q->life -= dt;
    if (q->life <= 0 || (q->spray && MAP && q->p.y < MAP->water_level - 0.5f)) vp[i] = vp[--nvp];
    else i++;
  }
}

/* Wing vapour over the wings in a hard pull, and the cone of vapour round a jet near the speed of sound. */
static void draw_vapour(const View *V, const Game *g, const Plane *p) {
  float gl = fabsf(p->gload);
  float v = p->pos.y < 5000 ? clampf((gl - 6.0f) / 1.5f, 0, 1) : 0;
  float m = p->speed / 340.0f, c = p->pos.y < 4000 ? clampf(1 - fabsf(m - 1.0f) / 0.05f, 0, 1) : 0;
  if (v <= 0 && c <= 0) return;
  if (to_cam(&V->cam, p->pos).z > 1500) return;
  float span = span_of(p), t = g->time;
  r3d_sprite_kind(SPR_SOFT);
  for (int k = 0; v > 0 && k < 10; k++) {
    float sd = k % 2 ? 1.0f : -1.0f, u = (float)(k / 2) / 4.0f, fl = 0.7f + 0.3f * sinf(t * 23 + k * 1.9f + p->idx);
    vec3 at = v3_add(v3_add(v3_add(p->pos, v3_scale(p->r, sd * (1.4f + u * span * 0.75f))), v3_scale(p->u, 0.7f)),
                     v3_scale(p->f, -1.2f - u * 2.4f));
    r3d_ball(V, at, 1.3f + 0.6f * fl, hexc(0xf6f6fa), 0.32f * v * fl);
  }
  if (c > 0) {
    vec3 base = v3_add(p->pos, v3_scale(p->f, 2.2f));
    for (int j = 0; j < 4; j++)
      for (int k = 0; k < 12; k++) {
        float ang = k * PI_F / 6 + t * 3, rad = 2.3f + j * 1.3f;
        vec3 at = v3_add(v3_add(v3_add(base, v3_scale(p->f, -j * 2.6f)), v3_scale(p->r, cosf(ang) * rad)),
                         v3_scale(p->u, sinf(ang) * rad));
        r3d_ball(V, at, 1.2f + j * 0.5f, hexc(j ? 0xeef0f6 : 0xffffff), c * (j ? 0.28f - j * 0.06f : 0.5f));
      }
  }
}

void viewfx_draw(Framebuffer *fb, const View *V, const Game *g) {
  if (g != fxG) return;
  for (int i = 0; i < g->nplanes && i < MAX_PLANES; i++) {
    const Plane *p = &g->planes[i];
    const PlaneFx *s = &PF[i];
    for (int k = 1; k < s->ntrail; k++) {
      const Sample *a = &s->trail[k - 1], *b = &s->trail[k];
      if (!a->on || !b->on) continue;
      float age = g->time - b->t, al = 0.5f * (1 - age / CONTRAIL_LIFE);
      if (al <= 0.02f || to_cam(&V->cam, b->a).z > 14000) continue;
      float w = 1.0f + age * 0.9f;
      r3d_seg(fb, V, a->a, b->a, hexc(0xf4f6f8), w, al, 1.5f);
      r3d_seg(fb, V, a->b, b->b, hexc(0xf4f6f8), w, al, 1.5f);
    }
    /* the newest stretch, from the last sample to just behind the nozzles, so the trail is joined to the jet */
    if (s->ntrail && s->trail[s->ntrail - 1].on && p->alive && p->pos.y > CONTRAIL_ALT && p->jet >= 0 && p->jet < MAX_JETS) {
      const Sample *b = &s->trail[s->ntrail - 1];
      float sp = span_of(p) * 0.18f;
      vec3 noz = v3_sub(p->pos, v3_scale(p->f, tail_of(p) + 6));
      r3d_seg(fb, V, b->a, v3_add(noz, v3_scale(p->r, -sp)), hexc(0xf4f6f8), 1.0f, 0.45f, 1.5f);
      r3d_seg(fb, V, b->b, v3_add(noz, v3_scale(p->r, sp)), hexc(0xf4f6f8), 1.0f, 0.45f, 1.5f);
    }
    for (int k = 1; k < s->ntip; k++) {
      const Sample *a = &s->tip[k - 1], *b = &s->tip[k];
      if (!a->on || !b->on) continue;
      float al = 0.55f * (1 - (g->time - b->t) / TIP_LIFE);
      if (al <= 0.02f || to_cam(&V->cam, b->a).z > 3000) continue;
      r3d_seg(fb, V, a->a, b->a, hexc(0xf4f2f6), 0.4f, al, 1);
      r3d_seg(fb, V, a->b, b->b, hexc(0xf4f2f6), 0.4f, al, 1);
    }
    if (!p->alive || p->inactive || p->jet < 0 || p->jet >= MAX_JETS) continue;
    draw_vapour(V, g, p);
    if (s->abFlash > 0) { /* the burner catching: a white-hot bloom in the nozzle, then the plume as usual */
      float k = s->abFlash / 0.35f;
      vec3 noz = v3_sub(p->pos, v3_scale(p->f, tail_of(p) + 0.6f));
      r3d_sprite_kind(SPR_GLOW);
      r3d_ball(V, noz, 1.2f + 2.6f * k, hexc(0xfff4dc), 0.9f * k);
      r3d_ball(V, v3_sub(noz, v3_scale(p->f, 3)), 2 + 3 * k, hexc(0xffb050), 0.5f * k);
      r3d_sprite_kind(SPR_SOFT);
    }
  }
  r3d_sprite_kind(SPR_SOFT);
  for (int i = 0; i < nvp; i++) {
    const VPart *q = &vp[i];
    float t = 1 - q->life / q->max;
    float z = to_cam(&V->cam, q->p).z;
    if (z > 4000) continue;
    /* close to the camera (the cockpit on the catapult) a puff thins away instead of filling the view */
    float a = q->a * (1 - t) * clampf((v3_dist(q->p, V->cam.pos) - 8) / 22, 0, 1);
    if (a > 0.01f) r3d_ball(V, q->p, lerpf(q->s0, q->s1, t), hexc(q->col), a);
  }
}
