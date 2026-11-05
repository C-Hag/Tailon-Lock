/* watch.c - the test viewer (--watch). CPU pilots fly every seat; the
 * viewer follows one jet at a time (keys 1-8, Tab) from its cockpit (C),
 * behind it (V), around it (O, the arrows turn the camera) or on the map
 * (M), at 1x to 8x speed (+ and -, P pauses).
 *
 * The instrument check (I shows it): every tick each jet's real motion is
 * measured from where it actually went in the world, not from the numbers
 * the flight model keeps, and held against what its cockpit gauges read:
 * airspeed and climb from the distance flown, the slip between the nose
 * and the path, the g from the change in velocity (filtered as the
 * gauge is), the altitude, and the fuel, which may only rise while
 * refuelling. A reading off by more than its tolerance counts against
 * that gauge; the viewer shows it in red and --watch logs a summary when
 * each match ends. */
#include "watch.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/jets.h"
#include "core/world.h"
#include "gpu.h"
#include "hud.h"
#include "net.h"
#include "render3d.h"
#include "testpilot.h"

const char *const CK_NAME[CK_COUNT] = { "AIRSPEED KT", "CLIMB FT/MIN", "SLIP DEG", "G", "ALTITUDE FT", "FUEL %",
                                               "BANK DEG", "PITCH DEG", "ENEMY BOX PX", "RANGE KM", "LOCK DEG", "TANKER KM",
                                               "LEAD MISS M" };

typedef struct {
  float seen, bad, worst; /* seconds checked, seconds off, the worst difference */
  float firstBad;         /* match time it was first off, or -1 */
  float worstT;           /* and when the worst came */
  char  worstWhat[24];    /* what the jet was doing then */
} Chan;

typedef struct {
  bool  have;
  int   n;          /* ticks measured since the last jump */
  vec3  pos, v;     /* last position, last measured velocity */
  float g, fuel;    /* the measured g (filtered like the gauge), last fuel */
  float gauge[CK_COUNT], world[CK_COUNT];
  bool  off[CK_COUNT], live[CK_COUNT]; /* live: checked this tick */
  Chan  ch[CK_COUNT];
} Track;

static struct {
  int   follow, view, speed, paused;
  bool  check;
  float orbit, orbitUp;
  Track t[MAX_PLANES];
} W = { 0, WV_CHASE, 1, 0, true, 0.6f, 0.25f, { { 0 } } };

int watch_view_of(const char *name) {
  static const char *const N[WV_COUNT] = { "cockpit", "chase", "orbit", "map" };
  for (int i = 0; i < WV_COUNT; i++)
    if (!strcmp(name, N[i])) return i;
  return -1;
}

void watch_start(Game *g, int follow, int view) {
  memset(W.t, 0, sizeof W.t);
  for (int i = 0; i < MAX_PLANES; i++)
    for (int c = 0; c < CK_COUNT; c++) W.t[i].ch[c].firstBad = -1;
  if (follow >= 0) W.follow = follow;
  if (view >= 0) W.view = view;
  if (W.follow >= g->nplanes) W.follow = 0;
}

int watch_reps(void) { return W.paused ? 0 : W.speed; }

void watch_speed(int x) { W.speed = x >= 8 ? 8 : x >= 4 ? 4 : x >= 2 ? 2 : 1; }

bool watch_key(Game *g, const PlatKey *k) {
  if (k->repeat && k->code != PK_LEFT && k->code != PK_RIGHT && k->code != PK_UP && k->code != PK_DOWN) return false;
  int c = k->key;
  if (c >= '1' && c <= '8' && c - '1' < g->nplanes) W.follow = c - '1';
  else if (c == '\t') W.follow = (W.follow + 1) % g->nplanes;
  else if (c == 'c') W.view = WV_COCKPIT;
  else if (c == 'v') W.view = WV_CHASE;
  else if (c == 'o') W.view = WV_ORBIT;
  else if (c == 'm') W.view = WV_MAP;
  else if (c == 'i') W.check = !W.check;
  else if (c == '+' || c == '=') W.speed = W.speed < 8 ? W.speed * 2 : 8;
  else if (c == '-') W.speed = W.speed > 1 ? W.speed / 2 : 1;
  else if (c == 'p' || c == ' ') W.paused = !W.paused;
  else if (k->code == PK_LEFT) W.orbit -= 0.08f;
  else if (k->code == PK_RIGHT) W.orbit += 0.08f;
  else if (k->code == PK_UP) W.orbitUp = fminf(1.3f, W.orbitUp + 0.06f);
  else if (k->code == PK_DOWN) W.orbitUp = fmaxf(-0.4f, W.orbitUp - 0.06f);
  else return false;
  return true;
}

/* ---- the instrument check -------------------------------------------------- */
static float wrap180(float a) {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

static const char *doing(const Game *g, const Plane *p);

/* d: how far the gauge is off; bad: further than it may be */
static void mark(Track *T, int c, float gauge, float world, float d, bool bad, const Game *g, const Plane *p, float dt) {
  float t = g->time;
  T->gauge[c] = gauge;
  T->world[c] = world;
  T->off[c] = bad;
  T->live[c] = true;
  Chan *C = &T->ch[c];
  C->seen += dt;
  if (!T->off[c]) return;
  C->bad += dt;
  if (C->firstBad < 0) C->firstBad = t;
  if (d <= C->worst) return;
  C->worst = d;
  C->worstT = t;
  snprintf(C->worstWhat, sizeof C->worstWhat, "%s", p->out ? "TURNED BACK AT THE EDGE" : doing(g, p));
}

static void judge(Track *T, int c, float gauge, float world, float tol, const Game *g, const Plane *p, float dt) {
  float d = fabsf(gauge - world);
  mark(T, c, gauge, world, d, d > tol, g, p, dt);
}

void watch_tick(Game *g, float dt) {
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    Track *T = &W.t[i];
    memset(T->live, 0, CK_PICTURE * sizeof T->live[0]);
    memset(T->off, 0, CK_PICTURE * sizeof T->off[0]);
    if (!p->alive || p->inactive || (p->remote && g->net)) { /* a watched stream's jets are all remote, and checked */
      T->have = false;
      continue;
    }
    Gauges G;
    hud_gauges(g, p, &G);
    vec3 d = v3_sub(p->pos, T->pos);
    /* a respawn, a landing back on the carrier, or the first tick: start again */
    if (!T->have || v3_len(d) > 700 * dt) {
      T->have = true;
      T->n = 0;
      T->pos = p->pos;
      T->fuel = G.fuel;
      T->g = G.gl;
      continue;
    }
    vec3 v = v3_scale(d, 1 / dt);
    float spd = v3_len(v);
    if (T->n >= 1) {
      /* the load factor: acceleration along the jet's up axis, plus gravity's share of it */
      vec3  a = v3_scale(v3_sub(v, T->v), 1 / dt);
      float n = v3_dot(a, p->u) / 9.81f + p->u.y;
      T->g += (n - T->g) * fminf(1, dt * 6);
    }
    T->pos = p->pos;
    T->v = v;
    T->n++;
    if (T->n < 4) {
      T->fuel = G.fuel;
      continue;
    }
    judge(T, CK_SPEED, G.spd * 1.944f, spd * 1.944f, fmaxf(3, spd * 1.944f * 0.02f), g, p, dt);
    judge(T, CK_CLIMB, G.vs * 196.85f, v.y * 196.85f, fmaxf(200, fabsf(v.y) * 196.85f * 0.05f), g, p, dt);
    /* the heading gauge shows the nose; a jet that is not side-slipping goes where it points */
    float hs = sqrtf(v.x * v.x + v.z * v.z);
    if (hs > 25 && !p->onGround && !p->taxi && !p->drone) {
      float track = atan2f(v.x, v.z) * (180 / PI_F);
      judge(T, CK_SLIP, 0, wrap180(G.hdg - track), 3, g, p, dt);
    }
    if (!p->onGround && !p->taxi) judge(T, CK_G, G.gl, T->g, fmaxf(0.4f, fabsf(T->g) * 0.12f), g, p, dt);
    judge(T, CK_ALT, G.alt * 3.281f, fmaxf(0, p->pos.y) * 3.281f, 10, g, p, dt);
    /* fuel: shown as the change per second; it may only rise while refuelling */
    float rate = (G.fuel - T->fuel) / dt;
    T->fuel = G.fuel;
    judge(T, CK_FUEL, G.fuel, G.fuel, 1, g, p, dt);
    T->world[CK_FUEL] = rate;
    if (rate > 0.01f && !p->refuel) {
      T->off[CK_FUEL] = true;
      T->ch[CK_FUEL].bad += dt;
      if (T->ch[CK_FUEL].firstBad < 0) T->ch[CK_FUEL].firstBad = g->time;
    }
  }
}

/* ---- the picture check ---------------------------------------------------- */
static bool project(const View *V, vec3 w, float *sx, float *sy) {
  vec3 c = to_cam(&V->cam, w);
  if (c.z < 1) return false;
  *sx = V->cx + c.x / c.z * V->F;
  *sy = V->cy - c.y / c.z * V->F;
  return true;
}

/* After the watched jet's view is drawn: the horizon as the 3D world drew
 * it against the attitude instruments, and every HUD mark against where
 * the thing it marks really is on the screen. */
static void picture_check(Game *g, Plane *p, const View *V, float dt) {
  Track *T = &W.t[p->idx];
  memset(T->live + CK_PICTURE, 0, (CK_COUNT - CK_PICTURE) * sizeof T->live[0]);
  memset(T->off + CK_PICTURE, 0, (CK_COUNT - CK_PICTURE) * sizeof T->off[0]);
  if (!p->alive || hud_marks.p != p) return;
  Gauges G;
  hud_gauges(g, p, &G);
  /* the horizon: two points far out at eye level either side of the heading */
  float h = atan2f(V->cam.f.x, V->cam.f.z), ax, ay, bx, by;
  vec3  e = V->cam.pos;
  if (!p->ctl.look && fabsf(V->cam.f.y) < 0.8f && project(V, v3_add(e, v3(sinf(h - 0.3f) * 1e5f, 0, cosf(h - 0.3f) * 1e5f)), &ax, &ay) &&
      project(V, v3_add(e, v3(sinf(h + 0.3f) * 1e5f, 0, cosf(h + 0.3f) * 1e5f)), &bx, &by)) {
    float len = hypotf(bx - ax, by - ay);
    float bank = atan2f(-(by - ay), bx - ax) * (180 / PI_F);
    /* the nose's height above the horizon line: F tan(pitch) pixels from the centre */
    float s = ((V->cx - ax) * (by - ay) - (V->cy - ay) * (bx - ax)) / len;
    float pitch = atan2f(s, V->F) * (180 / PI_F);
    float db = fabsf(wrap180(G.bank - bank));
    mark(T, CK_BANK, G.bank, bank, db, db > 2, g, p, dt);
    /* from outside, the camera is not the nose: only the cockpit's horizon is the pitch */
    if (!V->cam.ext) judge(T, CK_PITCH, G.pitch, pitch, 1, g, p, dt);
  }
  /* each enemy's box on the jet itself, and the range under it */
  float worst = 0, rworst = 0;
  for (int i = 0; i < hud_marks.nfoe; i++) {
    const HudFoe *F = &hud_marks.foe[i];
    const Plane *q = &g->planes[F->idx];
    float qx, qy;
    if (!project(V, q->pos, &qx, &qy)) continue;
    worst = fmaxf(worst, hypotf(qx - F->x, qy - F->y));
    rworst = fmaxf(rworst, fabsf(F->km - v3_dist(q->pos, p->pos) / 1000));
  }
  if (hud_marks.nfoe > 0) {
    mark(T, CK_BOX, 0, worst, worst, worst > 2, g, p, dt);
    mark(T, CK_RANGE, 0, rworst, rworst, rworst > 0.05f, g, p, dt);
  }
  /* a lock only on something inside the radar's cone off the nose */
  vec3 lp;
  if (hud_marks.lock && ref_get(g, p->lockTarget, &lp, NULL)) {
    float a = acosf(clampf(v3_dot(v3_norm(v3_sub(lp, p->pos)), p->f), -1, 1)) * (180 / PI_F);
    float cone = LOCK_CONE * (180 / PI_F);
    mark(T, CK_LOCK, a, cone, a, a > cone + 0.5f, g, p, dt);
  }
  /* the gun's lead: rounds fired at the pipper (from the jet, at 1150 m/s plus its own velocity) pass
   * within a jet's length of where the enemy will be, if it flies straight on */
  const Plane *foe = game_enemy_of(g, p->team, p->pos);
  if (hud_marks.lead && foe && foe->alive) {
    vec3 d = v3_norm(v3_add(v3_add(v3_scale(V->cam.f, V->F), v3_scale(V->cam.r, hud_marks.leadx - V->cx)),
                            v3_scale(V->cam.u, V->cy - hud_marks.leady)));
    vec3 r0 = v3_sub(foe->pos, p->pos), w = v3_sub(v3_sub(foe->vel, p->vel), v3_scale(d, 1150));
    float t = fmaxf(0, -v3_dot(r0, w) / fmaxf(v3_dot(w, w), 1e-3f));
    float miss = v3_len(v3_add(r0, v3_scale(w, t)));
    mark(T, CK_LEAD, 0, miss, miss, miss > 15, g, p, dt);
  }
  /* the tanker's range, written on the HUD when fuel is low */
  if (hud_marks.tanker && p->fuel < 35) {
    float km = v3_dist(g->tankers[p->team & 1].pos, p->pos) / 1000;
    judge(T, CK_TANKER, hud_marks.tankerKm, km, 0.1f, g, p, dt);
  }
}

float watch_off(int plane, int ck) {
  const Chan *C = &W.t[plane].ch[ck];
  return C->seen > 0 ? C->bad / C->seen : -1;
}

float watch_worst(int plane, int ck) { return W.t[plane].ch[ck].worst; }

void watch_report(const Game *g) {
  for (int i = 0; i < g->nplanes; i++) {
    const Track *T = &W.t[i];
    char line[700];
    int  n = snprintf(line, sizeof line, "CHECK %s (%s):", g->planes[i].name, JETS[g->planes[i].jet].name);
    for (int c = 0; c < CK_COUNT && n < (int)sizeof line - 100; c++) {
      const Chan *C = &T->ch[c];
      if (C->seen <= 0) continue;
      if (C->bad <= 0) n += snprintf(line + n, sizeof line - n, " %s ok;", CK_NAME[c]);
      else
        n += snprintf(line + n, sizeof line - n, " %s OFF %.0f%% (first t=%.0f, worst %.1f at t=%.1f %s);", CK_NAME[c],
                      100 * C->bad / C->seen, C->firstBad, C->worst, C->worstT, C->worstWhat);
    }
    plat_log("%s", line);
  }
}

/* ---- drawing ------------------------------------------------------------- */
static Camera orbit_cam(const Plane *p, float t) {
  float a = W.orbit + t * 0.12f, e = W.orbitUp, r = 70;
  Camera c;
  c.pos = v3_add(p->pos, v3(sinf(a) * cosf(e) * r, sinf(e) * r, cosf(a) * cosf(e) * r));
  float h = terrain_h(c.pos.x, c.pos.z) + 3;
  if (c.pos.y < h) c.pos.y = h;
  c.f = v3_norm(v3_sub(p->pos, c.pos));
  c.r = v3_norm(v3_cross(v3(0, 1, 0), c.f));
  c.u = v3_cross(c.f, c.r);
  c.ext = true;
  return c;
}

static const char *doing(const Game *g, const Plane *p) {
  if (!p->alive) return "SHOT DOWN";
  if (p->gloc > 0) return "BLACKED OUT";
  if (p->taxi) return p->oncat ? "ON THE CATAPULT" : "TAKE-OFF ROLL";
  if (p->refuel) return p->pos.y - terrain_h(p->pos.x, p->pos.z) < 60 ? "TAKE-OFF" : "REFUELLING";
  if (p->onGround) return "ON THE GROUND";
  if (p->stall) return "STALLED";
  if (p->ai.refuel) return "GOING FOR FUEL";
  const Plane *e = game_enemy_of((Game *)g, p->team, p->pos);
  bool close = e && e->alive && v3_dist(e->pos, p->pos) < 3000;
  if (!close && p->ai.gt >= 0 && p->ai.gt < g->ntargets) return "GROUND ATTACK";
  if (p->is_ai && p->ai.bfm >= 0 && p->ai.bfm < BFM_COUNT) return BFM_NAMES[p->ai.bfm];
  return "FLYING";
}

static void row(Framebuffer *fb, int x, int y, int s, const char *a, const char *b, const char *c, const char *d,
                uint32_t col) {
  fb_text(fb, a, (float)x, (float)y, s, col, ALIGN_LEFT, 1);
  fb_text(fb, b, (float)(x + 22 * 6 * s), (float)y, s, col, ALIGN_RIGHT, 1);
  fb_text(fb, c, (float)(x + 30 * 6 * s), (float)y, s, col, ALIGN_RIGHT, 1);
  fb_text(fb, d, (float)(x + 37 * 6 * s), (float)y, s, col, ALIGN_RIGHT, 1);
}

static void draw_check(Framebuffer *fb, const Plane *p, const Track *T, int s) {
  int lh = 10 * s, w = 38 * 6 * s, h = (CK_COUNT + 4) * lh, x = fb->w - w - 8 * s, y = 8 * s + lh / 2;
  fb_rect_a(fb, x - 4 * s, y - lh / 2 - 4 * s, w + 8 * s, h, hexc(0), 0.55f);
  for (int c = 0; c < CK_COUNT; c++) {
    if (c == 0 || c == CK_PICTURE) {
      row(fb, x, y, s, c ? "PICTURE CHECK" : "INSTRUMENT CHECK", c ? "SHOWN" : "GAUGE", "WORLD", "OFF", hexc(AMBER));
      y += lh;
    }
    const Chan *C = &T->ch[c];
    char g[24], wv[24], pc[16];
    bool whole = c == CK_SPEED || c == CK_CLIMB || c == CK_ALT;
    snprintf(g, sizeof g, whole ? "%.0f" : "%.1f", T->gauge[c]);
    snprintf(wv, sizeof wv, whole ? "%.0f" : "%.1f", T->world[c]);
    if (c == CK_FUEL) snprintf(wv, sizeof wv, "%+.2f/S", T->world[c]);
    if (c == CK_BOX || c == CK_RANGE || c == CK_LEAD) snprintf(g, sizeof g, "OFF BY"); /* the world column is the miss */
    snprintf(pc, sizeof pc, C->seen > 0 ? "%.0f%%" : "-", C->seen > 0 ? 100 * C->bad / C->seen : 0);
    if (!T->live[c]) g[0] = wv[0] = '-', g[1] = wv[1] = 0;
    row(fb, x, y, s, CK_NAME[c], g, wv, pc, hexc(T->off[c] ? WARN : HUDC));
    y += lh;
  }
  char b[64];
  snprintf(b, sizeof b, "AGL %.0f FT", fmaxf(0, p->pos.y - terrain_h(p->pos.x, p->pos.z)) * 3.281f);
  fb_text(fb, b, (float)x, (float)y, s, hexc(DIM), ALIGN_LEFT, 1);
}

void watch_render(Framebuffer *fb, Game *g) {
  int Wd = fb->w, H = fb->h, s = Wd >= 1100 ? 2 : 1;
  if (W.follow >= g->nplanes) W.follow = 0;
  Plane *p = &g->planes[W.follow];
  fb_clear(fb, gpu_active() ? 0 : 0xFF000000u);
  if (W.view == WV_MAP) {
    hud_map(fb, g, p, 0, 0, (float)Wd, (float)H);
  } else if (W.view == WV_ORBIT && p->alive) {
    Camera cam = orbit_cam(p, g->time);
    View V;
    render_scene(fb, g, &cam, 0, 0, Wd, H, &V, NULL);
  } else {
    p->chase = W.view != WV_COCKPIT;
    hud_marks.p = NULL;
    render_view(fb, g, p, 0, 0, Wd, H, (ViewOpts){ W.view == WV_COCKPIT, false, false });
    picture_check(g, p, &r3d_last, (float)W.speed / 60);
  }
  fb_clip_reset(fb);

  /* who is being watched and what the pilot is doing */
  int lh = 10 * s, x = 8 * s, y = 8 * s + lh / 2;
  char b[160];
  const char *who = tp_who(W.follow), *said = tp_said(g, W.follow);
  fb_rect_a(fb, x - 4 * s, y - lh / 2 - 4 * s, 46 * 6 * s, (who[0] ? 4 : 3) * lh + 4 * s, hexc(0), 0.55f);
  snprintf(b, sizeof b, "WATCHING %d/%d  %s  %s", W.follow + 1, g->nplanes, p->name, JETS[p->jet].name);
  fb_text(fb, b, (float)x, (float)y, s, hexc(TEAM[p->team].ui), ALIGN_LEFT, 1);
  snprintf(b, sizeof b, "%s  K%d L%d  FUEL %.0f  HULL %.0f", doing(g, p), p->kills, p->lives, p->fuel, fmaxf(0, p->health));
  fb_text(fb, b, (float)x, (float)(y + lh), s, hexc(INK), ALIGN_LEFT, 1);
  snprintf(b, sizeof b, "T %.0f S  %dX%s  %s", g->time, W.speed, W.paused ? " PAUSED" : "", p->ev[0] ? p->ev : "");
  fb_text(fb, b, (float)x, (float)(y + 2 * lh), s, hexc(DIM), ALIGN_LEFT, 1);
  if (who[0]) {
    snprintf(b, sizeof b, "%s: %s", who, said ? said : "...");
    fb_text(fb, b, (float)x, (float)(y + 3 * lh), s, hexc(said ? AMBER : DIM), ALIGN_LEFT, 1);
  }
  if (W.check) draw_check(fb, p, &W.t[W.follow], s);
  if (net_watch_code()) {
    snprintf(b, sizeof b, "WATCH CODE %s", net_watch_code());
    fb_text(fb, b, (float)x, (float)(H - (W.view == WV_COCKPIT ? (int)roundf(fmaxf(52, fminf(H * 0.24f, Wd * 0.21f))) : 0) - 3 * lh),
            s, hexc(AMBER), ALIGN_LEFT, 1);
  }
  /* the keys, above the cockpit panel when it is shown (render_view's height for it) */
  int ph = W.view == WV_COCKPIT ? (int)roundf(fmaxf(52, fminf(H * 0.24f, Wd * 0.21f))) : 0;
  fb_text(fb, "1-8/TAB JET  C COCKPIT  V CHASE  O ORBIT  M MAP  I CHECK  +/- SPEED  P PAUSE", (float)(Wd / 2),
          (float)(H - ph - lh), s, hexc(DIM), ALIGN_CENTER, 1);
}
