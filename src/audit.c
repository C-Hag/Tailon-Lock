/* audit.c - the flight and game audit (--audit). Three parts:
 *
 *   spec      each jet's measured performance against its data sheet
 *             (jets_data.json) and its hangar scores, in both handlings,
 *             and the DR-7 drone against what its description promises;
 *   features  scripted checks of the systems a pilot uses: radar lock,
 *             cannon, missiles, flares and chaff, classic weapons,
 *             autopilot, blackout and redout, catapult and runway starts,
 *             landing and rearming, the tanker, the map edge;
 *   sorties   whole matches with CPU pilots in every seat, N per plane,
 *             cycling through the modes, maps and match rules, with every
 *             plane checked every tick for broken physics or stuck states.
 *
 * Prints a readable log and, with --json FILE, the numbers for the report.
 * Usage: --audit [--part spec|features|sorties] [--sorties N] [--only ID]
 *        [--json FILE] [--out FILE] [--shots DIR]; --part views (only when asked) renders and checks
 *        stills and writes DIR/views.png. Returns non-zero when a check failed. */
#include "audit.h"
#include "units.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/ai.h"
#include "core/game.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"
#include "platform/plat.h"
#include "gpu.h"
#include "hud.h"
#include "ui.h"
#include "raster.h"
#include "render3d.h"
#include "core/safe.h"
#include "surfaces.h"

#define DT (1.0f / 60)
#define KT KT_PER_MPS
#define DEG (180 / PI_F)

static FILE *J;    /* the JSON output, or NULL */
static int   fails; /* failed checks */
static bool  jfirst;

/* ---- JSON helpers: one flat object per record, records in an array ---- */
static void jbegin(const char *kind) {
  if (!J) return;
  fprintf(J, "%s\n{\"kind\":\"%s\"", jfirst ? "" : ",", kind);
  jfirst = false;
}
static void jnum(const char *k, double v) {
  if (J) fprintf(J, ",\"%s\":%.4g", k, v != v ? -1.0 : v);
}
static void jstr(const char *k, const char *v) {
  if (!J) return;
  fprintf(J, ",\"%s\":\"", k);
  for (const char *s = v; *s; s++) fprintf(J, *s == '"' || *s == '\\' ? "\\%c" : "%c", *s);
  fputc('"', J);
}
static void jend(void) {
  if (J) fputc('}', J);
}

/* a named check: printed, counted, and kept for the report */
A4C_PRINTF(4, 5) static bool verdict(const char *area, const char *name, bool ok, const char *fmt, ...) {
  char detail[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof detail, fmt, ap);
  va_end(ap);
  printf("  [%s] %s: %s - %s\n", ok ? "PASS" : "FAIL", area, name, detail);
  if (!ok) fails++;
  jbegin("check");
  jstr("area", area);
  jstr("name", name);
  jnum("ok", ok);
  jstr("detail", detail);
  jend();
  return ok;
}

/* a plane the test cannot do without: stop the whole run with the reason, rather than go on with NULL */
static Plane *must(Plane *p, const char *what) {
  if (!p) {
    fprintf(stderr, "%s: no %s in this game\n", __FILE__, what);
    exit(2);
  }
  return p;
}

/* ---- set-up helpers (as in flighttest.c) -------------------------------- */
static float bank_of(const Plane *p) { return atan2f(-p->r.y, p->u.y); }
static float hdg_of(const Plane *p) { return atan2f(p->f.x, p->f.z); }
static float wrap(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

static void place(Plane *p, vec3 pos, float hdg, float speed) {
  p->pos = pos;
  p->f = v3(sinf(hdg), 0, cosf(hdg));
  p->u = v3(0, 1, 0);
  p->r = v3_cross(p->u, p->f);
  p->speed = speed;
  p->vel = v3_scale(p->f, speed);
  p->taxi = p->oncat = p->onGround = false;
  p->fuel = 100;
  p->health = 100;
  p->invuln = 0;
  p->alive = true;
}

static void hold(Plane *p, float pitch, float bank) {
  float pe = pitch - asinf(clampf(p->f.y, -1, 1));
  p->ctl.pitch = clampf(pe * 4 + fabsf(bank_of(p)) * 0.35f, -1, 1);
  p->ctl.roll = clampf((bank - bank_of(p)) * 3, -1, 1);
}

typedef struct {
  GameRules R;
  GameMode  mode;
  bool      coop;
  int       diff;
} Setup;

/* A match where only the seats we drive fly: the CPU jets sit out unless
 * keep_enemy, targets are gone unless keep_targets. */
static Plane *solo(Game *g, int jet, bool realistic, bool classic, bool gfx, bool keep_enemy, bool keep_targets) {
  int jets[2] = { jet, jet };
  GameRules R = { 0 };
  R.realistic = realistic;
  R.classic = classic;
  R.g_effects = gfx;
  R.load[0][0] = R.load[1][0] = -1;
  game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 1, &R);
  for (int i = 0; i < g->nplanes; i++)
    if (g->planes[i].hidx < 0) {
      Plane *e = &g->planes[i];
      if (keep_enemy) {
        e->is_ai = false; /* a target drone: flies straight, wings level */
        e->jet = jet;
        e->lives = 99;
      } else {
        e->inactive = true;
        e->alive = false;
        e->lives = 99;
      }
    }
  if (!keep_targets) g->ntargets = 0;
  Plane *p = &g->planes[g->locals[0]];
  p->lives = 99;
  p->invuln = 0;
  return p;
}

static Plane *enemy(Game *g) {
  for (int i = 0; i < g->nplanes; i++)
    if (g->planes[i].team == 1) return &g->planes[i];
  return must(NULL, "enemy plane");
}

static void tick(Game *g) {
  sim_update(g, DT);
  g->nsounds = 0;
}

static void clear_buttons(Plane *p) {
  p->ctl.gun = p->ctl.msl = p->ctl.flr = p->ctl.fire = p->ctl.sel = p->ctl.map = p->ctl.ap = p->ctl.stick = false;
}

/* ---- part 1: the data sheets -------------------------------------------- */
typedef struct {
  float vmax_lo, vmax_hi, roc, roc_v, inst, inst_v, sus, sus_v, sus_g, flown, roll, gpeak, stall, fuel_full, fuel_cruise, ceiling;
} Perf;

/* best steady climb at sea level, from the game's own energy equation:
 * the excess power at 1 g, level, over the gravity factor */
static void climb_of(Game *g, Plane *p, const JetType *jt, float *roc, float *at) {
  *roc = *at = 0;
  for (float v = 60; v <= 560; v += 2) {
    place(p, v3(0, 0, 0), 0, v);
    p->gload = 1;
    float a = sim_excess_power(g, p, jt, 1);
    float rc = v * a / (9.81f * (g->realistic ? 1.0f : 1.5f));
    if (rc > *roc) { *roc = rc; *at = v; }
  }
}

/* The top speed level at a height: full power for 150 s. */
static float top_speed(Game *g, int jet, bool realistic, float alt) {
  Plane *p = solo(g, jet, realistic, false, false, false, false);
  place(p, v3(0, alt, -4000), 0, 200);
  p->throttle = 1;
  float vmax = 0;
  for (int i = 0; i < 150 * 60; i++) {
    hold(p, 0, 0);
    clear_buttons(p);
    tick(g);
    p->fuel = 100;
    p->pos = v3(0, alt, -4000); /* held in place: only the speed is measured */
    vmax = fmaxf(vmax, p->speed);
  }
  return vmax;
}

static void measure(int jet, bool realistic, Perf *o) {
  memset(o, 0, sizeof *o); /* before anything can fail: the tables print every field */
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  const JetType *jt = jet_type(jet, realistic);
  o->vmax_lo = top_speed(g, jet, realistic, 300);
  o->vmax_hi = top_speed(g, jet, realistic, 6000);
  Plane *p = solo(g, jet, realistic, false, false, false, false);
  climb_of(g, p, jt, &o->roc, &o->roc_v);
  /* the turns, from the game's own lift and energy equations (the same
   * functions the flight model calls). Instantaneous: the most g the wing
   * gives at a speed; sustained: the g where the excess power at full
   * power runs out, level at sea level. Realistic turns level at
   * g sqrt(n^2 - 1) / v; arcade turns with the wings vertical (its path
   * feels no gravity) at ARCADE_TURN x g (n + 1) / v. */
  for (float kt = 100; kt <= 650; kt += 2) {
    float v = kt / KT, n = jet_lift_g(jt, v);
    float w = realistic ? 9.81f * sqrtf(fmaxf(0, n * n - 1)) / v : 2.0f * 9.81f * (n + 1) / v;
    if (w * DEG > o->inst) { o->inst = w * DEG; o->inst_v = kt; }
    place(p, v3(0, 0, 0), 0, v);
    float ns = 1;
    for (float t = 1; t <= n; t += 0.02f) {
      p->gload = t;
      if (sim_excess_power(g, p, jt, 1) < 0) break;
      ns = t;
    }
    w = realistic ? 9.81f * sqrtf(fmaxf(0, ns * ns - 1)) / v : 2.0f * 9.81f * (ns + 1) / v;
    if (w * DEG > o->sus) { o->sus = w * DEG; o->sus_v = kt; o->sus_g = ns; }
  }
  /* and flown: rolled into the level-turn bank at corner speed, a second
   * of full pull at a held speed; the peak g felt and the heading rate */
  {
    float kt = jet_corner(jt) * KT;
    p = solo(g, jet, realistic, false, false, false, false);
    place(p, v3(0, 3000, -4000), 0, kt / KT);
    float n = jet_lift_g(jt, kt / KT), bank = realistic ? acosf(1 / n) : PI_F / 2;
    rot_pair(&p->u, &p->r, -bank);
    p->throttle = 1;
    float prev = hdg_of(p), acc = 0;
    for (int i = 0; i < 90; i++) {
      p->ctl.roll = clampf((bank - bank_of(p)) * 3, -1, 1);
      p->ctl.pitch = 1;
      p->speed = kt / KT;
      clear_buttons(p);
      tick(g);
      if (i >= 30) acc += wrap(hdg_of(p) - prev);
      prev = hdg_of(p);
      if (i > 30) o->gpeak = fmaxf(o->gpeak, p->gload);
    }
    o->flown = fabsf(acc) * DEG;
  }
  /* roll rate: full aileron for a second */
  p = solo(g, jet, realistic, false, false, false, false);
  place(p, v3(0, 3000, -4000), 0, 250);
  p->throttle = 0.7f;
  float racc = 0, pb = bank_of(p);
  for (int i = 0; i < 60; i++) {
    p->ctl.roll = 1;
    p->ctl.pitch = 0;
    clear_buttons(p);
    tick(g);
    racc += wrap(bank_of(p) - pb);
    pb = bank_of(p);
  }
  o->roll = fabsf(racc) * DEG;
  /* stall speed: idle, holding height, until the warning */
  p = solo(g, jet, realistic, false, false, false, false);
  place(p, v3(0, 3000, -4000), 0, 200);
  p->throttle = 0;
  for (int i = 0; i < 150 * 60 && !p->stall; i++) {
    p->ctl.roll = clampf(-bank_of(p) * 3, -1, 1);
    p->ctl.pitch = clampf((3000 - p->pos.y) * 0.01f - p->f.y * 3, -1, 1);
    clear_buttons(p);
    tick(g);
  }
  o->stall = p->speed * KT;
  /* endurance at full power and at a 60% cruise, level at 3000 m */
  for (int k = 0; k < 2; k++) {
    p = solo(g, jet, realistic, false, false, false, false);
    place(p, v3(0, 3000, -4000), 0, 250);
    p->throttle = k ? 0.6f : 1;
    float t = 0;
    for (int i = 0; i < 1500 * 60 && p->fuel > 0; i++) {
      hold(p, 0, 0);
      clear_buttons(p);
      tick(g);
      t += DT;
    }
    if (k) o->fuel_cruise = t;
    else o->fuel_full = t;
  }
  /* ceiling: the highest it holds a climb in the game's thin air (where
   * the excess power at its best speed runs out), and the hard cap */
  for (float h = 0; h <= 30000; h += 100) {
    float best = -1;
    for (float v = 80; v <= 560; v += 10) {
      place(p, v3(0, h, 0), 0, v);
      p->gload = 1;
      best = fmaxf(best, v * sim_excess_power(g, p, jt, 1));
    }
    if (best < 0.5f * 9.81f) { o->ceiling = h; break; } /* 100 ft/min, the service ceiling */
    o->ceiling = h;
  }
  free(g);
}

/* Spearman's rank correlation of two lists */
static float ranks_r(const float *a, const float *b, int n) {
  float ra[MAX_JETS], rb[MAX_JETS];
  for (int i = 0; i < n; i++) {
    float x = 1, y = 1;
    for (int k = 0; k < n; k++) {
      if (k == i) continue;
      x += a[k] < a[i] ? 1 : a[k] == a[i] ? 0.5f : 0;
      y += b[k] < b[i] ? 1 : b[k] == b[i] ? 0.5f : 0;
    }
    ra[i] = x;
    rb[i] = y;
  }
  float d2 = 0;
  for (int i = 0; i < n; i++) d2 += (ra[i] - rb[i]) * (ra[i] - rb[i]);
  return 1 - 6 * d2 / (n * (float)(n * n - 1));
}

static void spec_part(const char *only) {
  printf("\n== data sheets ==\n");
  Perf P[2][MAX_JETS];
  float s_v[MAX_JETS], s_roc[MAX_JETS], s_sus[MAX_JETS], s_inst[MAX_JETS], s_roll[MAX_JETS], s_rng[MAX_JETS];
  float h_v[MAX_JETS], h_turn[MAX_JETS], h_climb[MAX_JETS], h_rng[MAX_JETS];
  float m_v[2][MAX_JETS], m_roc[2][MAX_JETS], m_sus[2][MAX_JETS], m_inst[2][MAX_JETS], m_roll[MAX_JETS], m_rng[MAX_JETS];
  int n = 0;
  for (int j = 0; j < NJETS; j++) {
    if (only && strcmp(only, JETS[j].id) != 0) continue;
    const JetSpec *s = assets_jet_spec(j);
    for (int r = 0; r < 2; r++) measure(j, r == 1, &P[r][j]);
    const Perf *a = &P[0][j], *q = &P[1][j];
    printf("%-6s %-10s sheet: %4.0f kt SL, climb %3.0f m/s, turn %4.1f sus / %4.1f inst deg/s at %3.0f kt corner, roll %3.0f, "
           "ceiling %5.0f ft, %4.1f/%4.1f g, range %4.0f km\n",
           JETS[j].id, JETS[j].name, s ? s->vmax_sl / 1.852f : 0, s ? s->climb : 0, s ? s->turn_sus : 0, s ? s->turn_inst : 0,
           s ? s->corner / KMH_PER_KT : 0, s ? (float)s->roll : 0, s ? s->ceiling * FT_PER_M : 0, s ? s->g_max : 0, s ? s->g_min : 0,
           s ? s->range : 0);
    for (int r = 0; r < 2; r++) {
      const Perf *o = r ? q : a;
      printf("       %-10s game:  %4.0f kt low / %4.0f kt at 6 km, climb %3.0f m/s at %3.0f kt, turn %4.1f sus (%3.0f kt, %.1f g) / "
             "%4.1f inst at %3.0f kt (flown %4.1f), roll %3.0f, stall %3.0f kt, peak %.1f g, fuel %4.0f s full / %4.0f s cruise, "
             "energy ceiling %5.0f ft (cap %5.0f ft)\n",
             r ? "realistic" : "arcade", o->vmax_lo * KT, o->vmax_hi * KT, o->roc, o->roc_v * KT, o->sus, o->sus_v, o->sus_g, o->inst,
             o->inst_v, o->flown, o->roll, o->stall, o->gpeak, o->fuel_full, o->fuel_cruise, o->ceiling * FT_PER_M, 7500 * FT_PER_M);
      jbegin("perf");
      jstr("jet", JETS[j].id);
      jstr("name", JETS[j].name);
      jstr("des", JETS[j].des);
      jstr("handling", r ? "realistic" : "arcade");
      jnum("vmax_lo_kt", o->vmax_lo * KT);
      jnum("vmax_hi_kt", o->vmax_hi * KT);
      jnum("roc", o->roc);
      jnum("roc_kt", o->roc_v * KT);
      jnum("sus", o->sus);
      jnum("sus_kt", o->sus_v);
      jnum("sus_g", o->sus_g);
      jnum("inst", o->inst);
      jnum("inst_kt", o->inst_v);
      jnum("flown", o->flown);
      jnum("roll", o->roll);
      jnum("stall_kt", o->stall);
      jnum("gpeak", o->gpeak);
      jnum("fuel_full", o->fuel_full);
      jnum("fuel_cruise", o->fuel_cruise);
      jnum("ceiling_ft", o->ceiling * FT_PER_M);
      jnum("g_max", JETS[j].g_max);
      jnum("corner_kt", jet_corner(&JETS[j]) * KT);
      if (s) {
        jnum("s_vmax_kt", s->vmax_sl / 1.852f);
        jnum("s_mach", s->mach_alt);
        jnum("s_climb", s->climb);
        jnum("s_sus", s->turn_sus);
        jnum("s_inst", s->turn_inst);
        jnum("s_corner_kt", s->corner / KMH_PER_KT);
        jnum("s_roll", s->roll);
        jnum("s_ceiling_ft", s->ceiling * FT_PER_M);
        jnum("s_range", s->range);
        jnum("s_stall_kt", 0);
        jnum("s_g_max", s->g_max);
        for (int k = 0; k < 4; k++) jnum((const char *[]){ "h_speed", "h_turn", "h_climb", "h_range" }[k], s->stat[k]);
      }
      jend();
    }
    if (s) {
      s_v[n] = s->vmax_sl;
      s_roc[n] = s->climb;
      s_sus[n] = s->turn_sus;
      s_inst[n] = s->turn_inst;
      s_roll[n] = (float)s->roll;
      s_rng[n] = s->range;
      h_v[n] = s->stat[0];
      h_turn[n] = s->stat[1];
      h_climb[n] = s->stat[2];
      h_rng[n] = s->stat[3];
    }
    for (int r = 0; r < 2; r++) {
      m_v[r][n] = P[r][j].vmax_lo;
      m_roc[r][n] = P[r][j].roc;
      m_sus[r][n] = P[r][j].sus;
      m_inst[r][n] = P[r][j].inst;
    }
    m_roll[n] = P[1][j].roll;
    m_rng[n] = P[0][j].fuel_full;
    /* per-jet checks: the structural limit is kept, the jet flies */
    verdict("spec", JETS[j].id, P[1][j].gpeak <= JETS[j].g_max * 1.05f && P[0][j].gpeak <= JETS[j].g_max * 1.05f,
            "peak g %.1f arcade / %.1f realistic, limit %.1f", P[0][j].gpeak, P[1][j].gpeak, JETS[j].g_max);
    n++;
  }
  if (n >= 3) {
    printf("rank order, game against hangar scores (what the menu shows) and against the sheet (realistic handling):\n");
    struct { const char *what; float *h, *s, *m; } R[] = {
      { "top speed", h_v, s_v, m_v[1] },  { "climb", h_climb, s_roc, m_roc[1] }, { "sustained turn", h_turn, s_sus, m_sus[1] },
      { "instant turn", h_turn, s_inst, m_inst[1] }, { "roll", h_turn, s_roll, m_roll }, { "endurance", h_rng, s_rng, m_rng },
    };
    for (size_t k = 0; k < sizeof R / sizeof R[0]; k++) {
      float rh = ranks_r(R[k].m, R[k].h, n), rs = ranks_r(R[k].m, R[k].s, n);
      printf("  %-15s vs hangar %+.2f, vs sheet %+.2f\n", R[k].what, rh, rs);
      jbegin("rank");
      jstr("what", R[k].what);
      jnum("hangar", rh);
      jnum("sheet", rs);
      jend();
    }
  }
}

/* ---- the drone against its description ---------------------------------- */
static Plane *drone_solo(Game *g, bool realistic, bool gfx) {
  int jets[2] = { 0, 0 };
  GameRules R = { 0 };
  R.realistic = realistic;
  R.g_effects = gfx;
  game_new_rules(g, MODE_DRONE, DIFF_NORMAL, true, jets, 1, &R);
  Plane *p = NULL;
  for (int i = 0; i < g->nplanes; i++) {
    if (g->planes[i].drone) p = &g->planes[i];
    else {
      g->planes[i].inactive = true;
      g->planes[i].alive = false;
    }
  }
  p = must(p, "drone");
  p->lives = 99;
  p->is_ai = false;
  place(p, v3(0, 3000, -4000), 0, 160);
  p->throttle = 1;
  return p;
}

static void drone_part(void) {
  printf("\n== the DR-7 Vesper drone ==\n");
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  const JetType *dj = &JETS[DRONE_JET];
  int same = 0; /* the jet with the nearest hangar scores, to compare against */
  for (int j = 1; j < NJETS; j++)
    if (fabsf(JETS[j].g_speed - dj->g_speed) + fabsf(JETS[j].g_turn - dj->g_turn) <
        fabsf(JETS[same].g_speed - dj->g_speed) + fabsf(JETS[same].g_turn - dj->g_turn))
      same = j;
  for (int r = 0; r < 2; r++) {
    bool real = r == 1;
    const char *hm = real ? "realistic" : "arcade";
    char area[32];
    snprintf(area, sizeof area, "drone %s", hm);
    /* top speed, level, as a jet with speed score 8 */
    Plane *p = drone_solo(g, real, false);
    place(p, v3(0, 300, -4000), 0, 200);
    float vmax = 0, gpk = 0;
    for (int i = 0; i < 120 * 60; i++) {
      hold(p, 0, 0);
      tick(g);
      p->fuel = 100;
      vmax = fmaxf(vmax, p->speed);
    }
    float jv = top_speed(g, same, real, 300);
    verdict(area, "top speed", fabsf(vmax - jv) < 0.12f * jv, "%.0f kt level at 300 m (%s: %.0f kt)", vmax * KT, JETS[same].id, jv * KT);
    /* the airframe: a full pull at fighting speed, without vectoring */
    float best = 0, bestkt = 0;
    for (float kt = 150; kt <= 600; kt += 25) {
      p = drone_solo(g, real, true);
      place(p, v3(0, 3000, -4000), 0, kt / KT);
      float prev = 0, acc = 0;
      for (int i = 0; i < 90; i++) {
        p->ctl.roll = clampf((90 / DEG - bank_of(p)) * 3, -1, 1);
        p->ctl.pitch = i >= 30 ? 1 : 0;
        p->speed = kt / KT;
        p->vel = v3_scale(v3_norm(p->vel), p->speed);
        if (i == 30) prev = hdg_of(p);
        tick(g);
        if (i >= 30) {
          acc += wrap(hdg_of(p) - prev);
          prev = hdg_of(p);
          gpk = fmaxf(gpk, p->gload);
        }
      }
      if (fabsf(acc) * DEG > best) { best = fabsf(acc) * DEG; bestkt = kt; }
    }
    Perf jp;
    memset(&jp, 0, sizeof jp);
    {
      /* the best jet's instantaneous turn, measured the same way */
      for (int j = 0; j < NJETS; j++) {
        for (float kt = 150; kt <= 600; kt += 25) {
          Plane *q = solo(g, j, real, false, false, false, false);
          place(q, v3(0, 3000, -4000), 0, kt / KT);
          float prev = 0, acc = 0;
          for (int i = 0; i < 90; i++) {
            q->ctl.roll = clampf((90 / DEG - bank_of(q)) * 3, -1, 1);
            q->ctl.pitch = i >= 30 ? 1 : 0;
            q->speed = kt / KT;
            if (i == 30) prev = hdg_of(q);
            clear_buttons(q);
            tick(g);
            if (i >= 30) {
              acc += wrap(hdg_of(q) - prev);
              prev = hdg_of(q);
            }
          }
          jp.inst = fmaxf(jp.inst, fabsf(acc) * DEG);
        }
      }
    }
    verdict(area, "out-turns every jet", best > jp.inst, "%.1f deg/s at %.0f kt without vectoring (best jet %.1f deg/s)", best,
            bestkt, jp.inst);
    verdict(area, "14 g airframe", gpk > 9.5f && gpk <= 14.7f, "peak %.1f g in a full pull (limit %.0f g)", gpk, dj->g_max);
    /* no pilot: never greys out with G effects on */
    p = drone_solo(g, real, true);
    place(p, v3(0, 3000, -4000), 0, 300);
    bool gloc = false;
    for (int i = 0; i < 20 * 60; i++) {
      p->ctl.pitch = 1;
      p->ctl.roll = 0.3f;
      p->speed = 300;
      p->vel = v3_scale(v3_norm(p->vel), p->speed);
      tick(g);
      if (p->gloc > 0 || p->gstress > 0) gloc = true;
    }
    verdict(area, "no blackout", !gloc, "20 s of full pull with G effects on: %s", gloc ? "G-LOC" : "flies on");
    /* the endurance, three times a jet's at the same score */
    p = drone_solo(g, real, false);
    place(p, v3(0, 3000, -4000), 0, 250);
    float t = 0;
    for (int i = 0; i < 3000 * 60 && p->fuel > 0; i++) {
      hold(p, 0, 0);
      tick(g);
      t += DT;
    }
    Perf q;
    {
      Plane *jj = solo(g, same, real, false, false, false, false);
      place(jj, v3(0, 3000, -4000), 0, 250);
      jj->throttle = 1;
      q.fuel_full = 0;
      for (int i = 0; i < 1500 * 60 && jj->fuel > 0; i++) {
        hold(jj, 0, 0);
        clear_buttons(jj);
        tick(g);
        q.fuel_full += DT;
      }
    }
    float ratio = t / q.fuel_full * dj->burn / JETS[same].burn; /* as if the range scores were equal */
    verdict(area, "three times the endurance", ratio > 2.6f && ratio < 3.4f, "%.0f s at full power (%s %.0f s; %.1fx at equal range scores)",
            t, JETS[same].id, q.fuel_full, ratio);
    /* the post-stall moves, as flighttest.c flies them */
    /* Cobra */
    p = drone_solo(g, real, false);
    float maxA = 0;
    int st = 0;
    for (int i = 0; i < 60 * 8; i++) {
      bool up = i < 84;
      p->ctl.vec = up;
      p->ctl.pitch = up ? clampf((1.55f - p->aoa) * 4, -1, 1) : 0;
      p->ctl.roll = 0;
      tick(g);
      maxA = fmaxf(maxA, p->aoa);
      if (p->lastStunt) st = p->lastStunt;
    }
    verdict(area, "Cobra", st == STUNT_COBRA && maxA > 1.2f, "nose %.0f deg past the path, announced %s", maxA * DEG,
            st ? STUNT_NAMES[st] : "nothing");
    /* flip */
    p = drone_solo(g, real, false);
    float tdone = -1;
    st = 0;
    for (int i = 0; i < 60 * 8; i++) {
      bool pull = tdone < 0 && fabsf(p->stuntPitch) < 2 * PI_F - 0.15f;
      if (!pull && tdone < 0) tdone = g->time;
      p->ctl.vec = pull;
      p->ctl.pitch = pull ? 1 : 0;
      p->ctl.roll = 0;
      tick(g);
      if (p->lastStunt) st = p->lastStunt;
    }
    verdict(area, "flip", st == STUNT_FLIP && tdone > 0 && tdone < 4, "nose round in %.1f s, announced %s", tdone,
            st ? STUNT_NAMES[st] : "nothing");
    /* J-turn */
    p = drone_solo(g, real, false);
    place(p, v3(0, 3000, -4000), 0, 140);
    vec3 v0 = v3_norm(p->vel);
    float trev = -1;
    st = 0;
    for (int i = 0; i < 60 * 10; i++) {
      float nose = acosf(clampf(v3_dot(p->f, v0), -1, 1));
      p->ctl.vec = trev < 0;
      p->ctl.pitch = nose < 2.9f ? 1 : 0;
      p->ctl.roll = 0;
      tick(g);
      vec3 hv = v3(p->vel.x, 0, p->vel.z);
      if (trev < 0 && v3_len(hv) > 15 && v3_dot(v3_norm(hv), v0) < -0.94f) trev = g->time;
      if (p->lastStunt) st = p->lastStunt;
    }
    verdict(area, "J-turn", st == STUNT_JTURN && trev > 0 && trev < 8, "path reversed in %.1f s, announced %s", trev,
            st ? STUNT_NAMES[st] : "nothing");
    /* a vectored point-and-shoot: the nose can be held 60 degrees off the
     * path long enough to fire */
    p = drone_solo(g, real, false);
    place(p, v3(0, 3000, -4000), 0, 200);
    float held = 0;
    for (int i = 0; i < 60 * 4; i++) {
      p->ctl.vec = true;
      p->ctl.pitch = clampf((1.05f - p->aoa) * 3, -1, 1);
      tick(g);
      if (p->aoa > 0.9f) held += DT;
    }
    verdict(area, "nose pointing", held > 1.0f, "nose held over 50 deg off the path for %.1f s", held);
    /* stall-free: below the stall speed, vectoring, no stall warning and no spin */
    p = drone_solo(g, real, false);
    place(p, v3(0, 3000, -4000), 0, 60);
    bool stall = false;
    float lost = 0;
    for (int i = 0; i < 60 * 3; i++) {
      p->ctl.vec = true;
      p->ctl.pitch = clampf((0.6f - asinf(clampf(p->f.y, -1, 1))) * 3, -1, 1);
      tick(g);
      stall |= p->stall;
      lost = fmaxf(lost, 3000 - p->pos.y);
    }
    verdict(area, "past the stall", !stall && lost < 200, "60 m/s on the nozzle: %s, lost %.0f m", stall ? "STALL" : "no stall", lost);
    jbegin("drone");
    jstr("handling", hm);
    jnum("vmax_kt", vmax * KT);
    jnum("jet_vmax_kt", jv * KT);
    jnum("inst", best);
    jnum("inst_kt", bestkt);
    jnum("best_jet_inst", jp.inst);
    jnum("gpeak", gpk);
    jnum("fuel_full", t);
    jnum("jet_fuel_full", q.fuel_full);
    jnum("cobra_deg", maxA * DEG);
    jnum("flip_s", tdone);
    jnum("jturn_s", trev);
    jend();
  }
  free(g);
}

/* ---- part 2: the systems a pilot uses ----------------------------------- */
static void lock_tests(void) {
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  struct { float dist, ang; bool classic; int wsel; bool want; const char *what; } C[] = {
    { 2000, 0, false, 0, true, "dead ahead, 2 km" },
    { 2000, 15, false, 0, true, "15 deg off the nose" },
    { 2000, 24, false, 0, false, "24 deg off (outside the 19 deg cone)" },
    { 4300, 0, false, 0, true, "4.3 km (inside 4.5 km)" },
    { 5000, 0, false, 0, false, "5 km (outside 4.5 km)" },
    { 2000, 180, false, 0, false, "behind" },
    { 12000, 0, true, W_HEAT, true, "classic heat-seeker, 12 km" },
    { 14000, 0, true, W_HEAT, false, "classic heat-seeker, 14 km (outside 8 mi)" },
    { 30000, 0, true, W_RADAR, true, "classic radar, 30 km" },
    { 1500, 0, true, W_CANNON, true, "classic cannon, 1.5 km" },
    { 2000, 0, true, W_CANNON, false, "classic cannon, 2 km (outside 1 mi)" },
  };
  for (size_t k = 0; k < sizeof C / sizeof C[0]; k++) {
    Plane *p = solo(g, 0, false, C[k].classic, false, true, false);
    Plane *e = enemy(g);
    place(p, v3(0, 4000, -4000), 0, 250);
    float a = C[k].ang / DEG;
    place(e, v3_add(p->pos, v3(sinf(a) * C[k].dist, 0, cosf(a) * C[k].dist)), 0, 250);
    p->wsel = C[k].wsel;
    float tlock = -1;
    for (int i = 0; i < 90; i++) {
      hold(p, 0, 0);
      clear_buttons(p);
      e->ctl.pitch = e->ctl.roll = 0;
      tick(g);
      if (p->locked && tlock < 0) tlock = g->time;
    }
    bool ok = (tlock > 0) == C[k].want && (tlock < 0 || (tlock > LOCK_TIME - 0.05f && tlock < LOCK_TIME + 0.1f));
    if (tlock > 0) verdict("radar lock", C[k].what, ok, "locked after %.2f s", tlock);
    else verdict("radar lock", C[k].what, ok, "no lock");
  }
  /* no lock on a friend, and the lock follows the nearest to the nose */
  {
    int jets[2] = { 0, 0 };
    GameRules R = { 0 };
    R.pairs = true;
    R.load[0][0] = -1;
    game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 3, &R);
    g->ntargets = 0;
    Plane *p = &g->planes[0], *w = &g->planes[1];
    for (int i = 1; i < g->nplanes; i++) g->planes[i].is_ai = false;
    place(p, v3(0, 4000, -4000), 0, 250);
    place(w, v3(0, 4000, -2000), 0, 250);
    for (int i = 2; i < g->nplanes; i++) place(&g->planes[i], v3(3000 * i, 4000, 3000), 0, 250);
    for (int i = 0; i < 90; i++) {
      clear_buttons(p);
      hold(p, 0, 0);
      tick(g);
    }
    verdict("radar lock", "no lock on the wingman", !p->locked && p->lockTarget.kind == REF_NONE, "wingman 2 km ahead: %s",
            p->locked ? "LOCKED" : "ignored");
  }
  /* ground targets: in range and in the cone */
  {
    Plane *p = solo(g, 0, false, false, false, false, true);
    int t = -1;
    for (int i = 0; i < g->ntargets; i++)
      if (g->targets[i].team == 1) t = i;
    if (t >= 0) {
      Target *T = &g->targets[t];
      vec3 from = v3_add(T->pos, v3(0, 600, -2500));
      place(p, from, 0, 200);
      vec3 d = v3_norm(v3_sub(T->pos, from));
      p->f = d;
      p->r = v3_norm(v3_cross(v3(0, 1, 0), p->f));
      p->u = v3_cross(p->f, p->r);
      bool lk = false;
      for (int i = 0; i < 60; i++) {
        clear_buttons(p);
        p->ctl.pitch = p->ctl.roll = 0;
        tick(g);
        lk |= p->locked && p->lockTarget.kind == REF_GROUND;
      }
      verdict("radar lock", "ground target at 2.6 km", lk, "%s %s", TNAME[T->type], lk ? "locked" : "NOT locked");
    }
  }
  free(g);
}

static void weapon_tests(void) {
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  /* cannon: a target 400 m ahead, two seconds of fire */
  for (int r = 0; r < 2; r++) {
    Plane *p = solo(g, 0, r, false, false, true, false);
    Plane *e = enemy(g);
    place(p, v3(0, 4000, -4000), 0, 220);
    place(e, v3(0, 4000, -3600), 0, 220);
    float a0 = p->ammo, h0 = e->health;
    for (int i = 0; i < 120; i++) {
      clear_buttons(p);
      p->ctl.gun = true;
      p->throttle = 0.5f;
      e->throttle = 0.5f;
      hold(p, 0, 0);
      p->speed = e->speed = 220;
      tick(g);
    }
    verdict("weapons", r ? "cannon (realistic)" : "cannon (arcade)", e->health < h0 || !e->alive,
            "%.0f rounds fired at 400 m, target %s (%.0f%% health)", a0 - p->ammo, e->alive ? "hit" : "shot down", fmaxf(0, e->health));
  }
  /* the cannon's rate and the empty gun */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    place(p, v3(0, 4000, -4000), 0, 220);
    p->ammo = 30;
    int n = 0;
    for (int i = 0; i < 300; i++) {
      clear_buttons(p);
      p->ctl.gun = true;
      hold(p, 0, 0);
      float a = p->ammo;
      tick(g);
      if (p->ammo < a) n++;
    }
    verdict("weapons", "gun runs dry", p->ammo == 0 && n == 30, "30 rounds loaded, %d fired, %.0f left", n, p->ammo);
  }
  /* heat-seeker: locked, fired from 2.5 km behind a target flying straight */
  for (int hits = 1; hits <= 3; hits++) {
    int jets[2] = { 0, 0 };
    GameRules R = { 0 };
    R.classic = hits > 1;
    R.hits = hits;
    R.load[0][0] = -1;
    game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 11, &R);
    g->ntargets = 0;
    Plane *p = &g->planes[0], *e = &g->planes[1];
    e->is_ai = false;
    place(p, v3(0, 4000, -4000), 0, 250);
    place(e, v3(0, 4000, -1500), 0, 250);
    p->wsel = W_HEAT; /* classic: the lock reaches as far as the selected weapon */
    int msl0 = p->missiles;
    float tkill = -1;
    for (int i = 0; i < 60 * 40 && e->alive; i++) {
      clear_buttons(p);
      hold(p, 0, 0);
      e->ctl.pitch = e->ctl.roll = 0;
      /* keep the range: the target holds 250 m/s, we hold back */
      p->speed = fminf(p->speed, 250);
      bool can = p->locked && p->mslCool <= 0 && g->nmissiles == 0;
      p->ctl.msl = can && (i & 1);
      tick(g);
      if (!e->alive) tkill = g->time;
    }
    char name[48];
    snprintf(name, sizeof name, "heat-seeker, %d hit%s to kill", hits, hits > 1 ? "s" : "");
    verdict("weapons", name, !e->alive && msl0 - p->missiles == hits, "%d missile%s fired, target %s after %.1f s", msl0 - p->missiles,
            msl0 - p->missiles == 1 ? "" : "s", e->alive ? "still flying" : "down", tkill);
  }
  /* missiles run out */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    place(p, v3(0, 4000, -4000), 0, 250);
    int m0 = p->missiles, n = 0;
    for (int i = 0; i < 60 * 15; i++) {
      clear_buttons(p);
      hold(p, 0, 0);
      p->ctl.msl = (i / 30) & 1;
      int before = g->nmissiles;
      tick(g);
      if (g->nmissiles > before) n++;
    }
    verdict("weapons", "missiles run out", p->missiles == 0 && n == m0, "%d loaded, %d launched, %d left", m0, n, p->missiles);
  }
  /* flares against heat-seekers and chaff against radar missiles: fire at a
   * target that drops one decoy at 1.5 km; 40 tries each */
  for (int chaff = 0; chaff < 2; chaff++) {
    int decoyed = 0, tries = 40;
    for (int s = 0; s < tries; s++) {
      int jets[2] = { 0, 0 };
      GameRules R = { 0 };
      R.classic = chaff;
      R.load[0][0] = chaff ? 2 : -1;
      R.load[0][1] = chaff ? 4 : 0;
      R.load[0][2] = 0;
      game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 100 + s, &R);
      g->ntargets = 0;
      Plane *p = &g->planes[0], *e = &g->planes[1];
      e->is_ai = false;
      place(p, v3(0, 4000, -4000), 0, 250);
      place(e, v3(0, 4000, -1000), 0, 250);
      p->wsel = chaff ? W_RADAR : W_HEAT;
      bool shot = false, dropped = false;
      for (int i = 0; i < 60 * 12; i++) {
        clear_buttons(p);
        clear_buttons(e);
        hold(p, 0, 0);
        p->speed = fminf(p->speed, 250);
        if (p->locked && !shot) {
          if (chaff) p->ctl.fire = true;
          else p->ctl.msl = true;
          shot = true;
        }
        if (shot && !dropped && g->nmissiles && v3_dist(g->missiles[0].pos, e->pos) < 1500) {
          e->ctl.flr = true;
          dropped = true;
        }
        tick(g);
        if (shot && g->nmissiles == 0) break;
      }
      if (e->alive) decoyed++;
    }
    char d[96];
    snprintf(d, sizeof d, "%d of %d missiles pulled off by one %s (the game rolls 60%%)", decoyed, tries, chaff ? "chaff pod" : "flare");
    verdict("weapons", chaff ? "chaff vs radar missile" : "flares vs heat-seeker", decoyed >= tries * 0.35f && decoyed <= tries * 0.85f,
            "%s", d);
  }
  /* classic radar missile: no lock, no launch; lock lost mid-flight, it flies on blind */
  {
    int jets[2] = { 0, 0 };
    GameRules R = { 0 };
    R.classic = true;
    R.load[0][0] = 2;
    R.load[0][1] = 4;
    game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 5, &R);
    g->ntargets = 0;
    Plane *p = &g->planes[0], *e = &g->planes[1];
    e->is_ai = false;
    place(p, v3(0, 4000, -4000), 0, 250);
    place(e, v3(4000, 4000, 4000), PI_F / 2, 250); /* off to the side: no lock */
    p->wsel = W_RADAR;
    clear_buttons(p);
    p->ctl.fire = true;
    tick(g);
    bool none = g->nmissiles == 0 && strstr(p->ev, "NO RADAR LOCK");
    verdict("weapons", "radar missile needs a lock", none, "fire without a lock: %s", p->ev[0] ? p->ev : "(no message)");
    place(e, v3(0, 4000, 2000), 0, 200);
    p->prevFire = true;
    int r0 = p->radar;
    bool turned = false;
    for (int i = 0; i < 60 * 12; i++) {
      clear_buttons(p);
      if (p->locked && p->radar == r0) p->ctl.fire = true;
      if (p->radar < r0 && g->nmissiles && g->time > 3 && !turned) turned = true;
      if (turned) hold(p, 0, 80 / DEG); /* break away: the lock is lost */
      else hold(p, 0, 0);
      p->ctl.pitch = turned ? 1 : p->ctl.pitch;
      tick(g);
    }
    verdict("weapons", "radar missile goes blind without the lock", e->alive && p->radar == r0 - 1,
            "launched, then broke away: target %s", e->alive ? "missed" : "HIT anyway");
  }
  /* classic air-to-ground: too high, too fast, and a hit */
  {
    int jets[2] = { 0, 0 };
    GameRules R = { 0 };
    R.classic = true;
    R.load[0][0] = 4;
    R.load[0][1] = 0;
    R.load[0][2] = 2;
    game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 5, &R);
    for (int i = 0; i < g->nplanes; i++)
      if (g->planes[i].team == 1) { g->planes[i].inactive = true; g->planes[i].alive = false; }
    Plane *p = &g->planes[0];
    /* the enemy site and run-in with the clearest line to it: the missile
     * flies straight at the target, through any hill in the way */
    int t = -1;
    float bestm = -1e9f;
    vec3 from = v3(0, 0, 0);
    for (int i = 0; i < g->ntargets; i++) {
      const Target *c = &g->targets[i];
      if (c->team != 1 || c->type == TGT_SAM || c->type == TGT_SHIP) continue;
      for (int dir = 0; dir < 48; dir++) {
        float a = dir * PI_F / 12, r = dir < 24 ? 2500.0f : 1800.0f;
        vec3 f = v3_add(c->pos, v3(sinf(a) * r, 0, cosf(a) * r));
        f.y = terrain_h(f.x, f.z) + 480;
        float m = 1e9f;
        for (float u = 0; u < 0.95f; u += 0.02f) {
          vec3 q = v3_add(f, v3_scale(v3_sub(c->pos, f), u));
          m = fminf(m, q.y - terrain_h(q.x, q.z));
        }
        if (m > bestm) { bestm = m; t = i; from = f; }
      }
    }
    if (t >= 0) {
      Target *T = &g->targets[t];
      p->wsel = W_GROUND;
      place(p, v3_add(T->pos, v3(0, 1500, -3000)), 0, 200);
      clear_buttons(p);
      p->ctl.fire = true;
      tick(g);
      bool high = strstr(p->ev, "TOO HIGH") != NULL;
      place(p, v3_add(T->pos, v3(0, 300, -3000)), 0, 300);
      p->prevFire = false;
      clear_buttons(p);
      p->ctl.fire = true;
      tick(g);
      bool fast = strstr(p->ev, "TOO FAST") != NULL;
      verdict("weapons", "air-to-ground limits", high && fast, "1500 m up: %s; 583 kt: %s", high ? "refused" : "FIRED", fast ? "refused" : "FIRED");
      place(p, from, 0, 200);
      p->f = v3_norm(v3_sub(T->pos, from));
      p->r = v3_norm(v3_cross(v3(0, 1, 0), p->f));
      p->u = v3_cross(p->f, p->r);
      int g0 = p->ground;
      for (int i = 0; i < 60 * 20 && T->alive; i++) {
        if (g->nmissiles == 0 && p->ground < g0 && v3_dist(p->pos, T->pos) < 1500) { /* round again for the next pass */
          place(p, from, 0, 200);
          p->f = v3_norm(v3_sub(T->pos, from));
          p->r = v3_norm(v3_cross(v3(0, 1, 0), p->f));
          p->u = v3_cross(p->f, p->r);
        }
        clear_buttons(p);
        p->ctl.pitch = p->ctl.roll = 0;
        p->ctl.fire = p->locked && p->mslCool <= 0 && g->nmissiles == 0 && p->lockTarget.kind == REF_GROUND;
        p->prevFire = false;
        p->speed = 200;
        if (p->pos.y - terrain_h(p->pos.x, p->pos.z) < 150) hold(p, 0.2f, 0);
        vec3 mp = g->nmissiles ? g->missiles[0].pos : v3(0,0,0); int nm0 = g->nmissiles; float ml = nm0 ? g->missiles[0].life : 0;
        tick(g);
        if (getenv("DBG") && nm0 && !g->nmissiles) printf("gone at %.0f %.0f %.0f th %.0f tgt %.0f %.0f %.0f d %.0f life %.1f hp %.0f\n", mp.x, mp.y, mp.z, terrain_h(mp.x, mp.z), T->pos.x, T->pos.y, T->pos.z, v3_dist(mp, T->pos), ml, T->hp);
        if (getenv("DBG") && !nm0 && g->nmissiles) printf("launch at %.0f %.0f %.0f agl %.0f\n", p->pos.x, p->pos.y, p->pos.z, p->pos.y - terrain_h(p->pos.x, p->pos.z));
      }
      verdict("weapons", "air-to-ground hit", !T->alive, "%s (%.0f hp) %s with %d missile%s", TNAME[T->type], TDEF[T->type].hp, T->alive ? "still standing" : "destroyed",
              g0 - p->ground, g0 - p->ground == 1 ? "" : "s");
    }
  }
  free(g);
}

static void autopilot_tests(void) {
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  struct { float bank, pitch, alt; const char *what; } C[] = {
    { 0, 0, 3000, "level" }, { 40, 10, 3000, "40 deg bank, climbing" }, { -70, -15, 3000, "70 deg bank, diving" },
    { 170, 0, 3000, "inverted" }, { 0, -20, 120, "120 m, diving" },
  };
  int bad = 0, total = 0;
  for (int j = 0; j < NJETS; j++)
    for (int r = 0; r < 2; r++)
      for (size_t k = 0; k < sizeof C / sizeof C[0]; k++) {
        Plane *p = solo(g, j, r, false, false, false, false);
        place(p, v3(0, C[k].alt + terrain_h(0, -7000), -7000), 0.1f, 240); /* 40 s of flight stays inside the arena */
        p->throttle = 0.8f;
        /* roll and pitch to the attitude */
        float b = C[k].bank / DEG, pt = C[k].pitch / DEG;
        rot_pair(&p->f, &p->u, pt);
        rot_pair(&p->u, &p->r, -b);
        p->r = v3_norm(v3_cross(p->u, p->f));
        p->u = v3_cross(p->f, p->r);
        clear_buttons(p);
        p->ctl.ap = true;
        tick(g);
        float h0 = p->apHdg, a0 = p->apAlt;
        bool died = false;
        for (int i = 0; i < 40 * 60; i++) {
          clear_buttons(p);
          tick(g);
          p->fuel = 100;
          if (!p->alive) { died = true; break; }
        }
        float dh = fabsf(wrap(hdg_of(p) - h0)) * DEG, da = fabsf(p->pos.y - a0), bk = fabsf(bank_of(p)) * DEG;
        bool ok = !died && p->autopilot && dh < 3 && da < 60 && bk < 3;
        total++;
        if (!ok) {
          bad++;
          printf("    autopilot %s %s %s: %s heading off %.1f deg, height off %.0f m, bank %.1f deg\n", JETS[j].id,
                 r ? "realistic" : "arcade", C[k].what, died ? "CRASHED," : "", dh, da, bk);
        }
      }
  verdict("autopilot", "holds height, heading, wings level", bad == 0, "%d of %d engagements (8 jets x 2 handlings x 5 attitudes) held for 40 s",
          total - bad, total);
  /* the stick takes over */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    place(p, v3(0, 3000, -4000), 0, 240);
    clear_buttons(p);
    p->ctl.ap = true;
    tick(g);
    clear_buttons(p);
    tick(g);
    bool on = p->autopilot;
    p->ctl.stick = true;
    p->ctl.pitch = 0.5f;
    tick(g);
    verdict("autopilot", "stick takes over", on && !p->autopilot, "engaged %s, after the stick %s", on ? "yes" : "NO",
            p->autopilot ? "STILL ON" : "off");
    /* and the key again turns it off */
    clear_buttons(p);
    p->ctl.ap = true;
    tick(g);
    clear_buttons(p);
    tick(g);
    p->ctl.ap = true;
    tick(g);
    verdict("autopilot", "key toggles", !p->autopilot, "pressed twice: %s", p->autopilot ? "STILL ON" : "off");
  }
  free(g);
}

static void gforce_tests(void) {
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  for (int j = 0; j < NJETS; j++)
    for (int r = 0; r < 2; r++) {
      char area[32];
      snprintf(area, sizeof area, "G effects %s", r ? "realistic" : "arcade");
      Plane *p = solo(g, j, r, false, true, false, false);
      place(p, v3(0, 4000, -4000), 0, 230);
      p->throttle = 1;
      float tgl = -1, rate_in = 0, peak = 0;
      for (int i = 0; i < 60 * 20 && tgl < 0; i++) {
        clear_buttons(p);
        p->ctl.pitch = 1;
        p->ctl.roll = clampf((80 / DEG - bank_of(p)) * 3, -1, 1);
        p->speed = 230;
        tick(g);
        peak = fmaxf(peak, p->gload);
        if (p->gloc > 0) tgl = g->time;
      }
      /* blacked out: the stick does nothing */
      if (tgl > 0) {
        p->ctl.pitch = 1;
        tick(g);
        rate_in = fabsf(p->pitchRate);
      }
      float back = -1;
      for (int i = 0; i < 60 * 10 && tgl > 0 && back < 0; i++) {
        clear_buttons(p);
        p->ctl.pitch = 0;
        tick(g);
        if (p->gloc <= 0) back = g->time - tgl;
      }
      char name[48];
      snprintf(name, sizeof name, "%.8s G-LOC", JETS[j].id);
      verdict(area, name, tgl > 0 && rate_in < 0.05f && back > 3 && back < 4, "full pull at %.1f g: blackout after %.1f s, no control, back after %.1f s",
              peak, tgl, back);
    }
  /* redout: a hard push */
  {
    Plane *p = solo(g, 0, false, false, true, false, false);
    place(p, v3(0, 5000, -4000), 0, 230);
    float tr = -1;
    for (int i = 0; i < 60 * 20 && tr < 0; i++) {
      clear_buttons(p);
      p->ctl.pitch = -1;
      p->ctl.roll = 0;
      p->speed = 230;
      if (p->pos.y < 2000) p->pos.y = 5000;
      tick(g);
      if (p->gloc > 0) tr = g->time;
    }
    verdict("G effects arcade", "redout", tr > 0 && strstr(p->ev, "REDOUT"), "full push: %s after %.1f s", p->ev, tr);
  }
  /* off: nothing happens */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    place(p, v3(0, 4000, -4000), 0, 230);
    bool any = false;
    for (int i = 0; i < 60 * 20; i++) {
      clear_buttons(p);
      p->ctl.pitch = 1;
      p->speed = 230;
      tick(g);
      any |= p->gloc > 0;
    }
    verdict("G effects arcade", "off means off", !any, "20 s of full pull with G effects off: %s", any ? "BLACKOUT" : "no blackout");
  }
  /* the grey-out builds before the blackout and fades after */
  {
    Plane *p = solo(g, 0, false, false, true, false, false);
    place(p, v3(0, 4000, -4000), 0, 230);
    float s1 = 0;
    for (int i = 0; i < 60 * 1; i++) {
      clear_buttons(p);
      p->ctl.pitch = 1;
      p->speed = 230;
      tick(g);
    }
    s1 = p->gstress;
    for (int i = 0; i < 60 * 3; i++) {
      clear_buttons(p);
      p->ctl.pitch = 0;
      tick(g);
    }
    verdict("G effects arcade", "grey-out fades", s1 > 0.3f && p->gstress < s1 * 0.5f, "stress %.2f after 1 s of pull, %.2f after 3 s level", s1,
            p->gstress);
  }
  free(g);
}

/* Starts: the catapult on every map with a carrier, the runway on every
 * map, landing back and rearming, for every jet. */
static void start_tests(void) {
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  int nm = assets_map_count();
  for (int m = -1; m < nm; m++) {
    const Map *map = m < 0 ? NULL : assets_map(m);
    world_set_map(map, TOD_DAY, WX_CLEAR);
    const char *mname = m < 0 ? "classic arena" : assets_map_name(m);
    for (int team = 0; team < 2; team++) {
      int ok = 0, n = 0;
      char why[160] = "";
      bool cat = false;
      for (int j = 0; j < NJETS; j++)
        for (int r = 0; r < 2; r++) {
          int jets[2] = { j, j };
          GameRules R = { 0 };
          R.ground_start = true;
          R.realistic = r;
          R.load[0][0] = -1;
          game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 1, &R);
          g->ntargets = 0;
          Plane *p = &g->planes[0];
          for (int i = 1; i < g->nplanes; i++) { g->planes[i].inactive = true; g->planes[i].alive = false; }
          p->team = team;
          game_spawn(g, p);
          cat = p->oncat;
          bool taxi0 = p->taxi;
          float tair = -1, low = 1e9f;
          p->throttle = 1;
          for (int i = 0; i < 40 * 60 && p->alive; i++) {
            clear_buttons(p);
            if (p->taxi) {
              p->ctl.pitch = p->speed > 94 ? 0.6f : 0;
              p->ctl.roll = 0;
            } else hold(p, 10 / DEG, 0);
            tick(g);
            if (!p->taxi && tair < 0) tair = g->time;
            if (!p->taxi) low = fminf(low, p->pos.y - terrain_h(p->pos.x, p->pos.z));
          }
          n++;
          if (taxi0 && tair > 0 && p->alive && tair < (cat ? 4 : 15)) ok++;
          else if (!why[0])
            snprintf(why, sizeof why, "%.8s %s: %.80s, airborne after %.1f s, lowest %.0f m", JETS[j].id, r ? "realistic" : "arcade",
                     p->alive ? "alive" : p->deathMsg, tair, low);
        }
      char name[96];
      snprintf(name, sizeof name, "%s, %s from the %s", mname, team ? "red" : "blue", cat ? "catapult" : "runway");
      verdict("starts", name, ok == n, "%d of %d jets airborne and climbing%s%s", ok, n, why[0] ? "; " : "", why);
    }
  }
  /* landing back on the runway and rearming, every jet, classic arena */
  world_set_map(NULL, TOD_DAY, WX_CLEAR);
  {
    int ok = 0, n = 0;
    char why[160] = "";
    for (int j = 0; j < NJETS; j++)
      for (int r = 0; r < 2; r++) {
        Plane *p = solo(g, j, r, false, false, false, false);
        const Base *b = &BASES[0];
        float hy = terrain_h(b->x, b->z), d = b->dir;
        p->pos = v3(b->x, hy + 3 + tanf(3 / DEG) * 1200, b->z - d * 2200);
        p->f = v3(0, 0, d);
        p->u = v3(0, 1, 0);
        p->r = v3_cross(p->u, p->f);
        p->speed = 100;
        p->throttle = 0.15f;
        p->missiles = 0;
        p->ammo = 0;
        p->fuel = 30;
        p->gearDown = true, p->gearT = 1; /* realistic: the gear down for the landing, as a pilot does */
        bool touched = false, rearmed = false;
        for (int i = 0; i < 120 * 60 && p->alive; i++) {
          float dist = (p->pos.z - (b->z - d * 1000)) * d;
          float want = hy + 3 + tanf(3 / DEG) * fmaxf(0, -dist) + (dist > 0 ? -2 : 0);
          clear_buttons(p);
          if (!touched) {
            hold(p, clampf((want - p->pos.y) * 0.02f - 0.05f, -0.12f, 0.12f), 0);
            p->throttle = p->speed > 104 ? 0.05f : 0.35f;
          } else {
            p->ctl.pitch = p->ctl.roll = 0;
            p->throttle = 0;
          }
          tick(g);
          touched |= p->onGround;
          if (p->missiles >= MAX_MSL && p->fuel > 99) { rearmed = true; break; }
        }
        n++;
        if (rearmed) ok++;
        else if (!why[0])
          snprintf(why, sizeof why, "%.8s %s: %.80s, missiles %d, fuel %.0f", JETS[j].id, r ? "realistic" : "arcade",
                   p->alive ? (touched ? "landed" : "never touched down") : p->deathMsg, p->missiles, p->fuel);
      }
    verdict("starts", "land on the runway and rearm", ok == n, "%d of %d jets landed, refuelled and rearmed%s%s", ok, n, why[0] ? "; " : "", why);
  }
  /* a CPU pilot in realistic handling home from the middle of every map, every jet, at every level: down on its
   * runway (or deck) and refuelled with fuel to spare, not round and round until dry (the
   * certificate's audit had 211 jets down with an empty tank on the way home, 204 of them in realistic handling: on
   * finals at 150 m/s, they floated the runway's length at 110 and went round) */
  {
    int ok = 0, n = 0, dry = 0;
    float worst = 1e9f;
    char why[200] = "";
    for (int m = -1; m < nm; m++) {
      const Map *map = m < 0 ? NULL : assets_map(m);
      world_set_map(map, TOD_DAY, WX_CLEAR);
      const char *mname = m < 0 ? "classic arena" : assets_map_name(m);
      for (int j = 0; j < NJETS; j++)
        for (int lv = 0; lv < 3; lv++) {
          Plane *p = solo(g, j, true, false, false, false, false);
          const Base *b = &BASES[p->team];
          p->is_ai = true;
          memset(&p->ai, 0, sizeof p->ai);
          p->ai.diff = lv;
          p->ai.gt = -1;
          p->pos = v3(0, terrain_h(0, 0) + 1500, 0);
          p->f = v3(0, 0, -b->dir); /* the base behind it */
          p->u = v3(0, 1, 0);
          p->r = v3_cross(p->u, p->f);
          p->speed = 200;
          p->vel = v3_scale(p->f, p->speed);
          /* the fuel a CPU pilot turns home at (ai.c: the bingo figure and its reserve; an ace the figure itself) */
          p->fuel = 100;
          float need = game_bingo(g, p);
          p->fuel = fmaxf(22, fminf(80, need * (lv == 2 ? 1.0f : 1.5f) + 20 * game_burn(g, p, 0.3f))) - 0.5f;
          float low = 100;
          bool down = false;
          const char *hd = getenv("A4C_AUDIT_HOME"); /* "LEVEL JET MAP": that trip's every 2 s */
          bool tr = hd && strstr(hd, JETS[j].id) && strstr(hd, mname) && hd[0] - '0' == lv;
          for (int i = 0; i < 600 * 60 && p->alive; i++) {
            tick(g);
            if (tr && i % 120 == 0)
              printf("    t %5.0f x %6.0f z %6.0f agl %5.0f v %4.0f thr %.2f fuel %4.1f ref %d fin %d lined %d miss %d gear %.1f flap %.1f\n", g->time,
                     p->pos.x, p->pos.z, p->pos.y - terrain_h(p->pos.x, p->pos.z), p->speed, p->throttle, p->fuel, p->ai.refuel, p->ai.final_leg,
                     p->ai.lined, p->ai.missed, p->gearT, p->flapT);
            low = fminf(low, p->fuel);
            if (p->onGround && !p->taxi && p->fuel > 94) { down = true; break; }
          }
          n++;
          if (down && low > 0) ok++, worst = fminf(worst, low);
          else {
            dry += low <= 0;
            if (getenv("A4C_AUDIT_HOME")) printf("    home: %s %s level %d: %s, fuel from %.0f, down to %.0f, t %.0f s\n", mname, JETS[j].id, lv,
                                                  !p->alive ? "lost" : down ? "landed dry" : "not down", need, low, g->time);
            if (!why[0])
              snprintf(why, sizeof why, "%s %.8s level %d: %s, fuel down to %.0f", mname, JETS[j].id, lv,
                       !p->alive ? "lost" : down ? "landed dry" : "not down in 10 minutes", low);
          }
        }
    }
    /* (at most one trip in sixty dry, the bar each fix to the way home has reached: 27 dry before the circuit's
     * 120 m, 11 after it, 7 with the gentle pull-up, 1 with no afterburner home) */
    verdict("starts", "CPU home and down on realistic handling", dry * 60 <= n, "%d of %d down and refuelled (%d ran dry, 1 in 60 at most), the lowest tank %.0f %%%s%s",
            ok, n, dry, worst > 100 ? 0 : worst, why[0] ? "; first not: " : "", why);
  }
  /* a CPU jet down on its carrier's deck too far along to stop (the wire missed, 120 m past the touchdown point at
   * 95 m/s), every jet, both handlings: off the bow flying and round again, not into the sea (the audit counted jets
   * rolled off the bow at idle) */
  {
    int twin = assets_map_find("twin-isles"), ok = 0, n = 0;
    char why[160] = "";
    const Map *TM = twin >= 0 ? assets_map(twin) : NULL;
    world_set_map(TM, TOD_DAY, WX_CLEAR);
    for (int j = 0; TM && TM->ncarriers > 0 && j < NJETS; j++)
      for (int r = 0; r < 2; r++) {
        Plane *p = solo(g, j, r, false, false, false, false);
        int ci = -1;
        for (int c = 0; c < TM->ncarriers; c++)
          if (TM->carriers[c].team == p->team) ci = c;
        if (ci < 0) continue;
        const Carrier *cv = &TM->carriers[ci];
        float hh = cv->heading * PI_F / 180; /* ai.c's deck frame: the bow's way, the touchdown point 22 % from the stern */
        vec3 cf = v3(sinf(hh), 0, cosf(hh));
        vec3 tdp = v3_add(v3(cv->x, terrain_h(cv->x, cv->z) + 3, cv->z), v3_scale(cf, -cv->length * (0.5f - 0.22f)));
        p->is_ai = true;
        memset(&p->ai, 0, sizeof p->ai);
        p->ai.diff = 1, p->ai.gt = -1;
        p->ai.refuel = true, p->ai.final_leg = true, p->ai.site = ci + 1;
        p->pos = v3_add(tdp, v3_scale(cf, 120));
        p->pos.y = terrain_h(p->pos.x, p->pos.z) + 3;
        p->f = cf;
        p->u = v3(0, 1, 0);
        p->r = v3_cross(p->u, p->f);
        p->speed = 95;
        p->vel = v3_scale(p->f, p->speed);
        p->onGround = true;
        p->gearDown = true, p->gearT = 1;
        p->throttle = 0;
        p->fuel = 40;
        for (int i = 0; i < 30 * 60 && p->alive; i++) tick(g);
        n++;
        if (p->alive) ok++;
        else if (!why[0]) snprintf(why, sizeof why, "%.8s %s: %.60s", JETS[j].id, r ? "realistic" : "arcade", p->deathMsg ? p->deathMsg : "lost");
      }
    verdict("starts", "CPU too far along its carrier's deck goes round", n > 0 && ok == n, "%d of %d alive 30 s on%s%s", ok, n, why[0] ? "; first lost: " : "", why);
  }
  /* a CPU jet on short final, 15 m over the runway's threshold but 50 m beside its centre line (the wheels count only
   * within 35 m of it), every jet, both handlings: round again, not down on the grass (the audit counted jets
   * touching down 40-70 m off the line at 110 m/s, crashed) */
  {
    int ok = 0, n = 0;
    char why[160] = "";
    world_set_map(NULL, TOD_DAY, WX_CLEAR);
    for (int j = 0; j < NJETS; j++)
      for (int r = 0; r < 2; r++) {
        Plane *p = solo(g, j, r, false, false, false, false);
        const Base *b = &BASES[p->team];
        p->is_ai = true;
        memset(&p->ai, 0, sizeof p->ai);
        p->ai.diff = 1, p->ai.gt = -1;
        p->ai.refuel = true, p->ai.final_leg = true, p->ai.site = 0;
        p->pos = v3(b->x + 50, 0, b->z + b->dir * 900);
        p->pos.y = terrain_h(p->pos.x, p->pos.z) + 15;
        p->f = v3_norm(v3(0, -0.03f, -b->dir));
        p->u = v3_norm(v3_cross(p->f, v3_cross(v3(0, 1, 0), p->f)));
        p->r = v3_cross(p->u, p->f);
        p->speed = 105;
        p->vel = v3_scale(p->f, p->speed);
        p->gearDown = true, p->gearT = 1;
        p->fuel = 40;
        for (int i = 0; i < 60 * 60 && p->alive; i++) tick(g);
        n++;
        if (p->alive) ok++;
        else if (!why[0]) snprintf(why, sizeof why, "%.8s %s: %.60s", JETS[j].id, r ? "realistic" : "arcade", p->deathMsg ? p->deathMsg : "lost");
      }
    verdict("starts", "CPU beside the runway on short final goes round", ok == n, "%d of %d alive a minute later%s%s", ok, n, why[0] ? "; first lost: " : "", why);
  }
  /* the tanker: hold station behind the drogue */
  {
    int twin = assets_map_find("twin-isles");
    world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
    Plane *p = solo(g, 0, false, false, false, false, false);
    const Tanker *T = &g->tankers[p->team & 1];
    bool called = sim_call_tanker(g, p), again = sim_call_tanker(g, p);
    g->tankers[p->team & 1].state = TK_STATION; /* on station at once, not 40 s inbound */
    sim_update_tanker(g, 0);
    verdict("starts", "tanker call once a match", called && !again && !strcmp(sim_tanker_status(g, p), "TANKER ON STATION"),
            "first call %s, second %s; the HUD says %s", called ? "taken" : "refused", again ? "taken" : "refused", sim_tanker_status(g, p));
    p->fuel = 40;
    float f0 = p->fuel;
    for (int i = 0; i < 60 * 6; i++) {
      p->pos = v3_add(T->drogue, v3_scale(T->f, -20));
      p->f = T->f;
      p->u = v3(0, 1, 0);
      p->r = v3_cross(p->u, p->f);
      p->speed = T->speed;
      p->vel = T->vel;
      clear_buttons(p);
      p->ctl.pitch = p->ctl.roll = 0;
      tick(g);
    }
    verdict("starts", "air-to-air refuelling", p->fuel > f0 + 50 && p->alive, "6 s on the drogue: fuel %.0f%% to %.0f%%", f0, p->fuel);
  }
  /* the tanker shot down: three missiles, or about 43 gun rounds; its caller's call is spent and it says LOST */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    sim_call_tanker(g, p);
    Tanker *T = &g->tankers[p->team & 1];
    T->state = TK_STATION;
    sim_update_tanker(g, 0);
    float hp0 = T->hp;
    int rounds = 0;
    while (!T->gone && rounds < 200) { /* one round at a time from the other side, aimed at it */
      Bullet *b = &g->bullets[g->nbullets++];
      memset(b, 0, sizeof *b);
      b->team = !(p->team & 1);
      b->owner = -1;
      b->pos = b->prev = v3_add(T->pos, v3(0, 0, 0.5f));
      b->vel = v3(0, 0, 1);
      b->life = 1;
      tick(g);
      rounds++;
    }
    verdict("starts", "tanker shot down", T->gone && T->state == TK_DOWN && !strcmp(sim_tanker_status(g, p), "TANKER LOST") && rounds >= 40 && rounds <= 45,
            "%.0f of health: down after %d gun rounds; the caller's HUD says %s", hp0, rounds, sim_tanker_status(g, p));
  }
  /* the map edge turns the jet back */
  {
    Plane *p = solo(g, 0, false, false, false, false, false);
    place(p, v3(ARENA_X - 500, 3000, 0), PI_F / 2, 250);
    float far = 0;
    for (int i = 0; i < 60 * 60; i++) {
      clear_buttons(p);
      p->ctl.pitch = clampf((3000 - p->pos.y) * 0.003f, -0.3f, 0.3f);
      p->ctl.roll = 0;
      tick(g);
      far = fmaxf(far, p->pos.x);
    }
    verdict("starts", "map edge turns back", far < ARENA_X + 3000 && p->pos.x < ARENA_X, "flew straight at the edge: furthest %.0f m past it, now %s",
            far - ARENA_X, p->pos.x < ARENA_X ? "back inside" : "STILL OUTSIDE");
  }
  /* classic: off your own end, low, lands you back on the carrier */
  {
    Plane *p = solo(g, 0, false, true, false, false, false);
    place(p, v3(0, 200, -ARENA_Z + 300), PI_F, 250);
    p->missiles = 0;
    bool back = false;
    for (int i = 0; i < 60 * 10 && !back; i++) {
      clear_buttons(p);
      hold(p, 0, 0);
      tick(g);
      back = strstr(p->ev, "BACK ON THE CARRIER") != NULL;
    }
    verdict("starts", "classic: home off your own end", back && p->missiles > 0, "%s", back ? "back on the carrier, rearmed" : "NOT returned");
  }
  /* crash detection off: the ground bounces */
  {
    int jets[2] = { 0, 0 };
    GameRules R = { 0 };
    R.classic = true;
    R.no_crash = true;
    R.load[0][0] = -1;
    game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 1, &R);
    g->ntargets = 0;
    Plane *p = &g->planes[0];
    g->planes[1].inactive = true;
    g->planes[1].alive = false;
    place(p, v3(3000, 400, 0), 0, 250);
    for (int i = 0; i < 60 * 8; i++) {
      clear_buttons(p);
      hold(p, -30 / DEG, 0);
      tick(g);
    }
    verdict("starts", "crash detection off", p->alive && p->pos.y >= terrain_h(p->pos.x, p->pos.z) + 2, "dived into the ground for 8 s: %s",
            p->alive ? "bounced, flying" : p->deathMsg);
  }
  int twin = assets_map_find("twin-isles");
  world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
  free(g);
}

/* ---- part 3: whole matches ---------------------------------------------- */
/* The loadouts the matches rotate through (heat, radar, air-to-ground; heat -1: the jet's own), so every
 * hangar choice is flown, and what each kind of missile was used for. */
#define NLOADS 5
static const struct { const char *name; int load[3]; } LOADS[NLOADS] = {
  { "its own pick", { -1, 0, 0 } }, { "heat", { 8, 0, 0 } }, { "radar", { 2, 6, 0 } },
  { "air-to-ground", { 4, 0, 2 } }, { "mixed", { 4, 4, 0 } },
};
static const char *const LEVELS[3] = { "easy", "normal", "hard" };

/* a match's result for one jet, added up by level and by loadout */
/* mhit: missiles that hit, by kind; trips home to refuel and how they ended: landed on a runway or a
 * carrier's deck, filled up by a pass over the base without landing, or lost on the way */
typedef struct { int n, wins, losses, kills, deaths, hits, heat, radar, ground, mhit[3], trips, runway, deck, pass, lost; } Split;

typedef struct {
  int   sorties, done, draws, wins, losses, faults, timeouts;
  int   fired[2][3];   /* --fired: missiles fired by the jet under test and by the others: heat, radar, air-to-ground */
  int   loads[16][4];  /* --fired: the CPU loadouts seen (heat, radar, ground, how many jets) */
  int   nloads;
  int   shots, msl, flares, locks, glocs, stalls, kills, deaths, crashes, shotdown, collisions, takeoffs, refuels, stunts[4];
  float minutes;
  /* match length: matches past 10 and 30 minutes (30: sudden death); the trips home of the jet under test, timed
   * in their parts: out to the touchdown (or the pass that filled it), the roll-out and the service on the
   * ground until full, and from full to airborne again */
  int   over10, over30, stops, missed;
  float tripS, finalS, groundS, launchS;
  char  fault[6][200];
  int   nfault;
  Split level[3], loadout[NLOADS];
  /* by handling (arcade, realistic) and CPU level, every jet in the match: tanker calls, fills at a tanker,
   * tankers shot down; gun rounds fired, and hits on jets and on tankers */
  int   tk[2][3][6];
} Tally;

static const char *const MODE_NAMES[] = { "demo", "1 player", "2 players", "carrier defence", "beat the drone" };

A4C_PRINTF(2, 3) static void fault(Tally *t, const char *fmt, ...) {
  t->faults++;
  if (t->nfault >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(t->fault[t->nfault++], sizeof t->fault[0], fmt, ap);
  va_end(ap);
}

/* One match with CPU pilots in every seat; plane `jet` (DRONE_JET for the
 * drone) is flown by seat 0, or the drone's seat. */
static int me_load[3];
static bool same_jet;
static int only_diff = -1; /* --diff N: every match at this CPU difficulty (0 easy, 1 normal, 2 hard) */
static void sortie(int jet, int k, Tally *t, uint32_t seed) {
  static Game G;
  Game *g = &G;
  bool drone = jet == DRONE_JET;
  /* the modes for a jet, then the drone's (it only has its own mode) */
  static const struct { GameMode mode; bool coop, pairs; } JM[] = {
    { MODE_1P, false, false }, { MODE_1P, false, true }, { MODE_2P, false, false }, { MODE_2P, true, false },
    { MODE_2P, false, true },  { MODE_DEFEND, false, false }, { MODE_DEFEND, true, false }, { MODE_DRONE, false, false },
  };
  int mi = k % 8;
  GameMode mode = drone ? MODE_DRONE : JM[mi].mode;
  bool coop = drone ? (k % 2 == 1) : JM[mi].coop; /* the drone: CPU drone, or player 2's (flown here by the CPU) */
  GameRules R = { 0 };
  R.pairs = !drone && JM[mi].pairs;
  R.realistic = (k / 8) % 2;
  R.g_effects = (k / 2) % 2;
  R.ground_start = (k / 4) % 2;
  R.classic = k % 5 == 0;
  R.dogfight = R.classic && k % 10 == 0;
  R.no_crash = R.classic && k % 15 == 0;
  R.hits = R.classic ? 1 + k % 3 : 0;
  R.lives = R.classic ? 1 + k % 3 : 0;
  R.load[0][0] = R.load[1][0] = -1;
  /* the jet under test flies the hangar loadouts in turn, with the level (k % 3) every pair of the two in 15
   * matches (the first is its own pick, as the CPU's), unless --me-load gives one */
  int li = me_load[0] >= 0 ? -1 : (k / 3) % NLOADS;
  if (me_load[0] >= 0) memcpy(R.load[0], me_load, sizeof me_load);
  else memcpy(R.load[0], LOADS[li].load, sizeof R.load[0]);
  Difficulty diff = (Difficulty)(only_diff >= 0 ? only_diff : k % 3);
  int nm = assets_map_count();
  int map = k % (nm + 1) - 1;
  const Map *mp = map < 0 ? NULL : assets_map(map);
  if (mode == MODE_DEFEND && (!mp || !mp->ncarriers)) mp = assets_map(assets_map_find("twin-isles")), map = assets_map_find("twin-isles");
  world_set_map(mp, k % TOD_COUNT, mp ? mp->default_weather : WX_CLEAR);
  int jets[2] = { drone ? (int)(seed % NJETS) : jet, drone ? (int)(seed / 7 % NJETS) : (jet + 1 + k) % NJETS };
  if (same_jet && !drone) jets[1] = jet;
  game_new_rules(g, mode, diff, coop, jets, seed, &R);
  /* every seat flown by the CPU */
  Plane *me = NULL;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    if (p->hidx >= 0) {
      p->is_ai = true;
      p->ai.diff = diff;
      p->ai.gt = -1;
      p->ai.mslCool = 4;
    }
    if (drone ? p->drone : p->hidx == 0) me = p;
  }
  char cfg[200];
  snprintf(cfg, sizeof cfg, "%s%s%s, %s, %s, %s%s%s%s%s, %s", MODE_NAMES[mode], coop ? (mode == MODE_DEFEND ? " +commander" : mode == MODE_DRONE ? " (P2 drone)" : " co-op") : "",
           R.pairs ? " 2v2" : "", mp ? assets_map_name(map) : "classic arena", R.realistic ? "realistic" : "arcade",
           (const char *[]){ "easy", "normal", "hard" }[diff], R.classic ? ", classic" : "", R.dogfight ? " dogfight" : "",
           R.g_effects ? ", G effects" : "", R.ground_start && mode != MODE_DEFEND && mode != MODE_DRONE ? ", ground start" : "",
           R.no_crash ? "no crash" : "crash on");
  if (li > 0) { /* its own pick needs no word (and keeps the older matches' lines as they were) */
    size_t n = strlen(cfg);
    snprintf(cfg + n, sizeof cfg - n, ", %s load", LOADS[li].name);
  }
  const JetType *mj = &JETS[me->jet];
  Split one = { 0 };
  bool tripOn = false, tripLanded = false, prevGround = false;
  float tripT = 0, finalT = -1, downT = -1, fullT = -1; /* when the trip began, it turned final, the wheels met the ground, the jet was full */
  int missed0 = 0;
  float prevHp[MAX_PLANES];
  for (int i = 0; i < g->nplanes; i++) prevHp[i] = g->planes[i].health;
  int ticks = 0, limit = 60 * 7200; /* as test_core and the play test: two hours of game time for a match */
  bool prevLock[MAX_PLANES] = { 0 }, prevGloc[MAX_PLANES] = { 0 }, prevStall[MAX_PLANES] = { 0 }, prevTaxi[MAX_PLANES], prevRef[MAX_PLANES] = { 0 };
  bool prevAlive[MAX_PLANES];
  float aliveT[MAX_PLANES] = { 0 }, taxiT[MAX_PLANES] = { 0 }, outT[MAX_PLANES] = { 0 }, overG[MAX_PLANES] = { 0 }, slowT[MAX_PLANES] = { 0 };
  int prevStunt = 0;
  for (int i = 0; i < g->nplanes; i++) {
    prevTaxi[i] = g->planes[i].taxi;
    prevAlive[i] = g->planes[i].alive;
  }
  int faults0 = t->faults;
  uint32_t seen = 0; /* one report per kind of fault per match */
#define FAULT(bit, ...) do { if (!(seen & (1u << (bit)))) { seen |= 1u << (bit); fault(t, __VA_ARGS__); } } while (0)
  /* what each plane fires, by kind (its stores going down), and the loadouts the CPU jets chose */
  int st[MAX_PLANES][3];
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    st[i][0] = q->missiles, st[i][1] = q->radar, st[i][2] = q->ground;
    if (q == me || q->drone || q->load[0] >= 0) continue;
    int full[4], l;
    game_full_load(g, q, full);
    for (l = 0; l < t->nloads && memcmp(t->loads[l], full, 3 * sizeof(int)); l++) {}
    if (l == t->nloads && l < 16) memcpy(t->loads[t->nloads++], full, 3 * sizeof(int)), t->loads[l][3] = 0;
    if (l < 16) t->loads[l][3]++;
  }
  for (; ticks < limit && !(g->over && g->overT <= 0); ticks++) {
    for (int i = 0; i < g->nplanes; i++) {
      const Plane *q = &g->planes[i];
      int now[3] = { q->missiles, q->radar, q->ground };
      for (int c = 0; c < 3; c++) {
        if (now[c] < st[i][c]) t->fired[q == me ? 0 : 1][c] += st[i][c] - now[c];
        st[i][c] = now[c];
      }
    }
    float a0 = me->ammo;
    int m0 = me->missiles + me->radar + me->ground, f0 = me->flares + me->chaff;
    int h0 = me->missiles, r0 = me->radar, g0 = me->ground;
    bool alive0 = me->alive;
    /* our missiles in the air, to tell which kind hit when one is gone and an enemy's hull with it */
    uint32_t mine[MAX_MISSILES];
    int mkind[MAX_MISSILES], nmine = 0;
    for (int i = 0; i < g->nmissiles; i++)
      if (g->missiles[i].owner == me->idx) mine[nmine] = g->missiles[i].id, mkind[nmine++] = g->missiles[i].kind;
    float hp0[MAX_PLANES], thp0[MAX_TARGETS];
    for (int i = 0; i < g->nplanes; i++) hp0[i] = g->planes[i].health;
    for (int i = 0; i < g->ntargets; i++) thp0[i] = g->targets[i].alive ? g->targets[i].hp : 0;
    sim_update(g, DT);
    g->nsounds = 0;
    for (int m = 0; m < nmine; m++) {
      bool still = false;
      for (int i = 0; i < g->nmissiles && !still; i++) still = g->missiles[i].id == mine[m];
      if (still) continue;
      bool hit = false;
      for (int i = 0; i < g->nplanes && !hit; i++) {
        Plane *q = &g->planes[i];
        if (q->team != me->team && q->lastHitBy == me->idx && hp0[i] - fmaxf(q->health, 0) >= 25) {
          hit = true;
          hp0[i] = q->health; /* one missile a hit */
        }
      }
      for (int i = 0; i < g->ntargets && !hit; i++) { /* an air-to-ground missile's target: a ground site or a ship */
        const Target *tg = &g->targets[i];
        float now = tg->alive ? tg->hp : 0;
        if (tg->team != me->team && thp0[i] - now >= 25) {
          hit = true;
          thp0[i] = now;
        }
      }
      if (hit && mkind[m] >= 0 && mkind[m] < 3) one.mhit[mkind[m]]++;
    }
    if (me->alive && alive0) {
      if (me->ammo < a0) t->shots += (int)(a0 - me->ammo);
      int m1 = me->missiles + me->radar + me->ground, f1 = me->flares + me->chaff;
      if (me->missiles < h0) one.heat += h0 - me->missiles;
      if (me->radar < r0) one.radar += r0 - me->radar;
      if (me->ground < g0) one.ground += g0 - me->ground;
      if (m1 < m0) t->msl += m0 - m1;
      if (f1 < f0) t->flares += f0 - f1;
    }
    if (me->lastStunt != prevStunt && me->lastStunt) t->stunts[me->lastStunt]++;
    prevStunt = me->lastStunt;
    for (int i = 0; i < g->nplanes; i++) {
      Plane *p = &g->planes[i];
      const JetType *jt = &JETS[p->jet];
      /* our hits: another jet's hull going down with our name on it */
      if (p != me && p->health < prevHp[i] && p->lastHitBy == me->idx && p->team != me->team) one.hits++;
      prevHp[i] = p->health;
      if (p == me) {
        if (p->locked && !prevLock[i]) t->locks++;
        if (p->gloc > 0 && !prevGloc[i]) t->glocs++;
        if (p->stall && !prevStall[i]) t->stalls++;
        if (prevTaxi[i] && !p->taxi && p->alive) t->takeoffs++;
        if (p->refuel && !prevRef[i]) t->refuels++;
        /* the trip home */
        if (p->ai.refuel && !tripOn) one.trips++, tripOn = true, tripLanded = false, tripT = g->time, finalT = -1, missed0 = p->ai.missed;
        if (tripOn && !tripLanded && p->ai.final_leg && finalT < 0) finalT = g->time;
        if (tripOn && p->onGround && !prevGround && !p->taxi && !tripLanded) {
          tripLanded = true;
          downT = g->time;
          t->stops++;
          t->tripS += downT - tripT;
          t->finalS += downT - (finalT >= 0 ? finalT : downT);
          if (getenv("A4C_AUDIT_TRIPS")) /* each landing of the jet under test, timed */
            printf("      trip %s t=%.0f: %.0f s out, final at %.0f s, touchdown %.0f kt, %.0f m along, %s [%s]\n", p->name, tripT, downT - tripT,
                   finalT - tripT, p->speed * KT, (p->pos.z - BASES[p->team].z) * BASES[p->team].dir, g->realistic ? "realistic" : "arcade", cfg);
          if (MAP && map_carrier_at(MAP, p->pos.x, p->pos.z) >= 0) one.deck++;
          else one.runway++;
        }
        if (tripOn && p->refuel && !prevRef[i] && !p->onGround && !tripLanded) one.pass++, tripLanded = true;
        if (tripOn && prevAlive[i] && !p->alive) one.lost++, tripOn = false;
        if (tripOn && !p->ai.refuel && p->alive) {
          tripOn = false;
          if (downT >= 0) fullT = g->time, t->groundS += fullT - downT, t->missed += p->ai.missed - missed0;
          downT = -1;
        }
        if (fullT >= 0 && (!p->alive || (!p->onGround && !p->taxi))) {
          if (p->alive) t->launchS += g->time - fullT;
          fullT = -1;
        }
        prevGround = p->onGround;
        if (prevAlive[i] && !p->alive) {
          t->deaths++;
          if (p->deathMsg && !strcmp(p->deathMsg, "CRASHED")) t->crashes++;
          else if (p->deathMsg && !strcmp(p->deathMsg, "SHOT DOWN")) t->shotdown++;
          else if (p->deathMsg && strstr(p->deathMsg, "COLLISION")) t->collisions++;
        }
      }
      if (prevAlive[i] && !p->alive && getenv("A4C_AUDIT_DEATHS"))
        printf("      death t=%.0f %s (%s, %s): %s at %.0f %.0f %.0f, %.0f m above ground, %.0f kt, f.y %.2f, bfm %s, fuel %.0f leg %d refuel %d home %.0f m, spawned %.0f s before [%s]\n", g->time,
               p->name, jt->id, p->is_ai ? "cpu" : "seat", p->deathMsg ? p->deathMsg : "?", p->pos.x, p->pos.y, p->pos.z,
               p->pos.y - terrain_h(p->pos.x, p->pos.z), p->speed * KT, p->f.y, BFM_NAMES[p->ai.bfm], p->fuel, p->ai.final_leg, p->ai.refuel, sqrtf((p->pos.x - BASES[p->team].x) * (p->pos.x - BASES[p->team].x) + (p->pos.z - BASES[p->team].z) * (p->pos.z - BASES[p->team].z)), aliveT[i], cfg);
      if (getenv("A4C_AUDIT_TRACE") && !strcmp(getenv("A4C_AUDIT_TRACE"), p->name) && p->alive && ticks % 15 == 0)
        printf("      trace t=%.2f agl %.0f kt %.0f f.y %.2f pitch %.2f roll %.2f bank %.0f g %.1f thr %.2f bfm %s stall %d fuel %.0f refuel %d leg %d x %.0f z %.0f\n", g->time,
               p->pos.y - terrain_h(p->pos.x, p->pos.z), p->speed * KT, p->f.y, p->ctl.pitch, p->ctl.roll, bank_of(p) * DEG, p->gload,
               p->throttle, BFM_NAMES[p->ai.bfm], p->stall, p->fuel, p->ai.refuel, p->ai.final_leg, p->pos.x, p->pos.z);
      aliveT[i] = p->alive ? aliveT[i] + DT : 0;
      prevLock[i] = p->locked;
      prevGloc[i] = p->gloc > 0;
      prevStall[i] = p->stall;
      prevTaxi[i] = p->taxi;
      prevRef[i] = p->refuel;
      prevAlive[i] = p->alive;
      if (!p->alive || p->inactive) {
        taxiT[i] = outT[i] = overG[i] = slowT[i] = 0;
        continue;
      }
      float gh = terrain_h(p->pos.x, p->pos.z);
      const char *who = p == me ? "our jet" : p->name;
      if (!(p->pos.x == p->pos.x && p->pos.y == p->pos.y && p->pos.z == p->pos.z && p->speed == p->speed && p->vel.x == p->vel.x))
        FAULT(0, "%s: NaN at t=%.0f [%s]", who, g->time, cfg);
      else if (fabsf(v3_len(p->f) - 1) > 0.01f || fabsf(v3_dot(p->f, p->u)) > 0.01f || fabsf(v3_dot(p->f, p->r)) > 0.01f)
        FAULT(1, "%s: axes not square at t=%.0f [%s]", who, g->time, cfg);
      if (!p->taxi && p->pos.y < gh - 1) FAULT(2, "%s: alive %.0f m under the ground at t=%.0f [%s]", who, gh - p->pos.y, g->time, cfg);
      if (p->speed < 0 || p->speed > 561) FAULT(3, "%s: speed %.0f at t=%.0f [%s]", who, p->speed, g->time, cfg);
      if (p->pos.y > (g->classic ? 18289 : 7501)) FAULT(4, "%s: above the ceiling at t=%.0f [%s]", who, g->time, cfg);
      if (p->fuel < 0 || p->fuel > 100.01f || p->ammo < 0 || p->missiles < 0 || p->flares < 0 || p->chaff < 0 || p->radar < 0 ||
          p->ground < 0 || p->health > 100.01f)
        FAULT(5, "%s: stores out of range (fuel %.0f ammo %.0f msl %d flr %d) at t=%.0f [%s]", who, p->fuel, p->ammo, p->missiles,
              p->flares, g->time, cfg);
      /* the g limit: the felt g may overshoot for a moment, not hold past it */
      if (p->gload > jt->g_max * 1.1f || p->gload < jt->g_min * 1.5f - 1) overG[i] += DT;
      else overG[i] = 0;
      if (overG[i] > 0.5f) FAULT(6, "%s (%s): %.1f g held past the %.1f g limit at t=%.0f [%s]", who, jt->id, p->gload, jt->g_max, g->time, cfg);
      /* stuck states */
      taxiT[i] = p->taxi ? taxiT[i] + DT : 0;
      if (taxiT[i] > 60) FAULT(7, "%s: still on the %s after 60 s at t=%.0f [%s]", who, p->oncat ? "catapult" : "runway", g->time, cfg);
      outT[i] = p->out ? outT[i] + DT : 0;
      if (outT[i] > 60) FAULT(8, "%s: outside the arena for 60 s at t=%.0f [%s]", who, g->time, cfg);
      if (fabsf(p->pos.x) > ARENA_X + 6000 || fabsf(p->pos.z) > ARENA_Z + 6000) FAULT(9, "%s: far outside the arena at t=%.0f [%s]", who, g->time, cfg);
      slowT[i] = !p->taxi && !p->refuel && p->onGround && p->speed < 5 ? slowT[i] + DT : 0;
      if (slowT[i] > 90) FAULT(10, "%s: parked on the ground for 90 s at t=%.0f [%s]", who, g->time, cfg);
      if (p->locked && p->lockTarget.kind == REF_PLANE && g->planes[p->lockTarget.idx].team == p->team)
        FAULT(11, "%s: locked onto a friend at t=%.0f [%s]", who, g->time, cfg);
    }
    if (g->nparts > MAX_PARTS || g->nbullets > MAX_BULLETS || g->nmissiles > MAX_MISSILES) FAULT(12, "pool overflow [%s]", cfg);
  }
  int kills = 0;
  for (int i = 0; i < g->nplanes; i++) kills += g->planes[i].kc[me->idx];
  t->kills += me->kills;
  (void)kills;
  /* the score adds up: 200 a kill */
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *p = &g->planes[i];
    if (p->lives < 0) FAULT(13, "%s: %d lives [%s]", p->name, p->lives, cfg);
    if (p->score < p->kills * 200) FAULT(14, "%s: score %d below %d kills x 200 [%s]", p->name, p->score, p->kills, cfg);
  }
#undef FAULT
  t->sorties++;
  t->minutes += ticks / 3600.0f;
  t->over10 += ticks > 60 * 600;
  t->over30 += ticks > 60 * 1800;
  if (!g->over) {
    t->timeouts++;
    printf("    timeout after %d min: %s\n", limit / 3600, cfg);
  } else {
    t->done++;
    if (g->winner < 0) t->draws++;
    else if (g->winner == me->team) t->wins++;
    else t->losses++;
  }
  one.n = 1;
  one.wins = g->over && g->winner >= 0 && g->winner == me->team;
  one.losses = g->over && g->winner >= 0 && g->winner != me->team;
  one.kills = me->kills;
  one.deaths = me->lives < 0 ? 0 : (g->lives > 0 ? g->lives : LIVES) - me->lives;
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    int *a = t->tk[g->realistic][diff];
    a[0] += q->tcalled;
    a[1] += q->tankerFills;
    a[3] += q->gunShots;
    a[4] += q->gunHitsJet;
    a[5] += q->gunHitsTanker;
  }
  t->tk[g->realistic][diff][2] += g->tankersDown[0] + g->tankersDown[1];
  Split *sp[2] = { &t->level[diff], li >= 0 ? &t->loadout[li] : NULL };
  for (int s = 0; s < 2 && sp[s]; s++) {
    sp[s]->n += one.n;
    sp[s]->wins += one.wins;
    sp[s]->losses += one.losses;
    sp[s]->kills += one.kills;
    sp[s]->deaths += one.deaths;
    sp[s]->hits += one.hits;
    sp[s]->heat += one.heat;
    sp[s]->radar += one.radar;
    sp[s]->ground += one.ground;
    for (int m = 0; m < 3; m++) sp[s]->mhit[m] += one.mhit[m];
    sp[s]->trips += one.trips;
    sp[s]->runway += one.runway;
    sp[s]->deck += one.deck;
    sp[s]->pass += one.pass;
    sp[s]->lost += one.lost;
  }
  if (getenv("A4C_AUDIT_VERBOSE") || t->faults > faults0)
    printf("    %2d %-6s %-62s %s in %5.1f min, %s; kills %d, deaths %d\n", k, mj->id, cfg, g->over ? "over" : "TIMEOUT", ticks / 3600.0f,
           g->winner < 0 ? "draw" : g->winner == me->team ? "won" : "lost", me->kills, me->lives < 0 ? 0 : (g->lives > 0 ? g->lives : LIVES) - me->lives);
}

static int first_sortie;    /* --from K: start at sortie K (to replay one) */
static int me_load[3] = { -1, 0, 0 }; /* --me-load H,R,G: the jet under test flies this hangar loadout */
static bool same_jet;       /* --same-jet: its opponent flies the same jet */
static bool show_fired;     /* --fired: missiles fired by kind and the CPU loadouts, on lines of their own */
static uint32_t seed_base = 1000; /* --seed S: another set of matches */

static void add_split(Split *a, const Split *b) {
  a->n += b->n;
  a->wins += b->wins;
  a->losses += b->losses;
  a->kills += b->kills;
  a->deaths += b->deaths;
  a->hits += b->hits;
  a->heat += b->heat;
  a->radar += b->radar;
  a->ground += b->ground;
  for (int m = 0; m < 3; m++) a->mhit[m] += b->mhit[m];
  a->trips += b->trips;
  a->runway += b->runway;
  a->deck += b->deck;
  a->pass += b->pass;
  a->lost += b->lost;
}

/* The results by level and by loadout (indented, so the audit's digest, which reads the matches' own lines,
 * is not changed by how they are summed up). */
static void print_splits(const Tally *t, const char *who) {
  for (int k = 0; k < 3 + NLOADS; k++) {
    const Split *s = k < 3 ? &t->level[k] : &t->loadout[k - 3];
    if (!s->n) continue;
    const char *name = k < 3 ? LEVELS[k] : LOADS[k - 3].name;
    printf("      %-6s %-8s %-14s %3d matches: won %3d, lost %3d; %4d kills, %4d deaths, %5d hits; missiles hit/fired: heat %d/%d, "
           "radar %d/%d, air-to-ground %d/%d; trips home %d: landed %d on a runway, %d on a deck, %d filled in a pass, %d lost\n",
           who, k < 3 ? "level" : "loadout", name, s->n, s->wins, s->losses, s->kills, s->deaths, s->hits, s->mhit[MK_HEAT], s->heat,
           s->mhit[MK_RADAR], s->radar, s->mhit[MK_GROUND], s->ground, s->trips, s->runway, s->deck, s->pass, s->lost);
    jbegin(k < 3 ? "level" : "loadout");
    jstr("jet", who);
    jstr("name", name);
    jnum("n", s->n);
    jnum("wins", s->wins);
    jnum("losses", s->losses);
    jnum("kills", s->kills);
    jnum("deaths", s->deaths);
    jnum("hits", s->hits);
    jnum("heat", s->heat);
    jnum("radar", s->radar);
    jnum("ground", s->ground);
    jnum("heat_hit", s->mhit[MK_HEAT]);
    jnum("radar_hit", s->mhit[MK_RADAR]);
    jnum("ground_hit", s->mhit[MK_GROUND]);
    jnum("trips", s->trips);
    jnum("landed_runway", s->runway);
    jnum("landed_deck", s->deck);
    jnum("filled_pass", s->pass);
    jnum("lost_on_trip", s->lost);
    jend();
  }
}

/* how long the matches ran and what a trip home cost (indented: not in the digest) */
static void print_length(const Tally *t, const char *who) {
  int n = t->sorties ? t->sorties : 1, s = t->stops ? t->stops : 1;
  printf("      %-6s length   %3d matches: mean %.1f min, over 10 min %.0f %%, over 30 min %.0f %%; %d landings, %.1f a match: "
         "%.0f s out (%.0f of them on finals), %.0f s on the ground, %.0f s to airborne, %.2f go-arounds\n",
         who, t->sorties, t->minutes / n, 100.0 * t->over10 / n, 100.0 * t->over30 / n, t->stops, (double)t->stops / n, t->tripS / s,
         t->finalS / s, t->groundS / s, t->launchS / s, (double)t->missed / s);
  jbegin("length");
  jstr("jet", who);
  jnum("n", t->sorties);
  jnum("minutes", t->minutes);
  jnum("over10", t->over10);
  jnum("over30", t->over30);
  jnum("landings", t->stops);
  jnum("out_s", t->tripS);
  jnum("final_s", t->finalS);
  jnum("ground_s", t->groundS);
  jnum("launch_s", t->launchS);
  jnum("go_arounds", t->missed);
  jend();
}

static void add_length(Tally *a, const Tally *b) {
  a->sorties += b->sorties;
  a->minutes += b->minutes;
  a->over10 += b->over10;
  a->over30 += b->over30;
  a->stops += b->stops;
  a->missed += b->missed;
  a->tripS += b->tripS;
  a->finalS += b->finalS;
  a->groundS += b->groundS;
  a->launchS += b->launchS;
}

static void sorties_part(int n, const char *only) {
  printf("\n== whole matches, %d per plane ==\n", n);
  Tally all;
  memset(&all, 0, sizeof all);
  for (int j = 0; j <= NJETS; j++) {
    int jet = j == NJETS ? DRONE_JET : j;
    if (only && strcmp(only, JETS[jet].id) != 0) continue;
    Tally t;
    memset(&t, 0, sizeof t);
    uint64_t t0 = plat_ms();
    for (int k = first_sortie; k < first_sortie + n; k++) sortie(jet, k, &t, seed_base + (uint32_t)(jet * 7919 + k * 104729));
    printf("%-6s %-9s %d matches (%d over, %d timeouts) in %.0f game minutes (%.1f s): won %d, lost %d, drawn %d; "
           "%d kills, %d deaths (%d shot down, %d crashed, %d collisions); %d rounds, %d missiles, %d decoys, %d locks, "
           "%d take-offs, %d refuels, %d G-LOCs, %d stalls; cobras %d, flips %d, J-turns %d; %d faults\n",
           JETS[jet].id, JETS[jet].name, t.sorties, t.done, t.timeouts, t.minutes, (plat_ms() - t0) / 1000.0, t.wins, t.losses, t.draws,
           t.kills, t.deaths, t.shotdown, t.crashes, t.collisions, t.shots, t.msl, t.flares, t.locks, t.takeoffs, t.refuels, t.glocs,
           t.stalls, t.stunts[1], t.stunts[2], t.stunts[3], t.faults);
    for (int f = 0; f < t.nfault; f++) printf("    fault: %s\n", t.fault[f]);
    print_length(&t, JETS[jet].id);
    add_length(&all, &t);
    if (show_fired) {
      printf("  fired %s: me heat %d radar %d ground %d; others heat %d radar %d ground %d; CPU loadouts", JETS[jet].id,
             t.fired[0][0], t.fired[0][1], t.fired[0][2], t.fired[1][0], t.fired[1][1], t.fired[1][2]);
      for (int l = 0; l < t.nloads; l++) printf(" %d/%d/%d x%d", t.loads[l][0], t.loads[l][1], t.loads[l][2], t.loads[l][3]);
      printf("\n");
      for (int h = 0; h < 2; h++)
        for (int d = 0; d < 3; d++) {
          const int *a = t.tk[h][d];
          printf("  tanker %s %s %s: calls %d, fills %d, shot down %d; gun rounds %d, hits on jets %d, on tankers %d\n", JETS[jet].id,
                 h ? "realistic" : "arcade", LEVELS[d], a[0], a[1], a[2], a[3], a[4], a[5]);
        }
    }
    print_splits(&t, JETS[jet].id);
    for (int s = 0; s < 3; s++) add_split(&all.level[s], &t.level[s]);
    for (int s = 0; s < NLOADS; s++) add_split(&all.loadout[s], &t.loadout[s]);
    jbegin("sorties");
    jstr("jet", JETS[jet].id);
    jstr("name", JETS[jet].name);
    jnum("n", t.sorties);
    jnum("done", t.done);
    jnum("timeouts", t.timeouts);
    jnum("minutes", t.minutes);
    jnum("wins", t.wins);
    jnum("losses", t.losses);
    jnum("draws", t.draws);
    jnum("kills", t.kills);
    jnum("deaths", t.deaths);
    jnum("shotdown", t.shotdown);
    jnum("crashes", t.crashes);
    jnum("collisions", t.collisions);
    jnum("shots", t.shots);
    jnum("missiles", t.msl);
    jnum("decoys", t.flares);
    jnum("locks", t.locks);
    jnum("takeoffs", t.takeoffs);
    jnum("refuels", t.refuels);
    jnum("glocs", t.glocs);
    jnum("stalls", t.stalls);
    jnum("cobras", t.stunts[1]);
    jnum("flips", t.stunts[2]);
    jnum("jturns", t.stunts[3]);
    jnum("faults", t.faults);
    for (int f = 0; f < t.nfault; f++) {
      char key[24]; /* "fault" and up to 11 digits */
      snprintf(key, sizeof key, "fault%d", f);
      jstr(key, t.fault[f]);
    }
    jend();
    char name[48];
    snprintf(name, sizeof name, "%s, %d matches", JETS[jet].id, t.sorties);
    verdict("matches", name, t.faults == 0 && t.timeouts == 0, "%d faults, %d timeouts", t.faults, t.timeouts);
  }
  printf("\n== every plane, by level and by loadout ==\n");
  print_length(&all, "all");
  print_splits(&all, "all");
}

/* ---- views: what the matches' numbers can't show --------------------------
 * The software renderer draws stills into a framebuffer and the checks read
 * the pixels back: no hole (a block of pure black where the world should
 * be), a sane brightness for the time of day, the horizon where the jet's
 * pitch and roll put it, the sun disc where the light comes from, a jet lit
 * on its sunny side, every text inside the picture, and the instruments
 * reading the jet's own speed, height and heading. Each still also goes on a
 * contact sheet (views.png) for a person to look at. */
#define VW 640
#define VH 360
#define SHEET_COLS 6
#define TW (VW / 3)
#define TH (VH / 3)

static Framebuffer VF;
static int ntext, ntext_out;
static char text_seen[256][48];
static char text_out[4][96];
static uint32_t *sheet;
static int nsheet, sheet_rows;

/* every text's box and ink, for the panel's contrast (the frame's own pictures: VF) */
static struct { int x0, y0, x1, y1; uint32_t ink; char s[24]; } text_box[256];
static void views_text(const Framebuffer *fb, const char *s, int x0, int y0, int x1, int y1, uint32_t ink) {
  if (ntext < 256) {
    snprintf(text_seen[ntext], sizeof text_seen[0], "%s", s);
    text_box[ntext].x0 = x0, text_box[ntext].y0 = y0, text_box[ntext].x1 = x1, text_box[ntext].y1 = y1;
    text_box[ntext].ink = fb == &VF ? ink : 0;
    snprintf(text_box[ntext].s, sizeof text_box[0].s, "%s", s);
  }
  ntext++;
  /* inside the picture (texts cut by a deliberate clip, like the heading tape's, are inside it) */
  if (s[0] && (x0 < 0 || y0 < 0 || x1 > fb->w || y1 > fb->h)) {
    if (ntext_out < 4) snprintf(text_out[ntext_out], sizeof text_out[0], "\"%.40s\" at %d,%d-%d,%d", s, x0, y0, x1, y1);
    ntext_out++;
  }
}

static int ntext_cut, nnum_cut;
static char text_cut[3][64];
/* a label cut to fit is reported; a number cut is wrong (447 kt read as 44) and fails */
static int  ntext_over;
static char text_over[3][40];
static void views_over(const char *s) {
  if (ntext_over < 3) snprintf(text_over[ntext_over], sizeof text_over[0], "\"%.24s\"", s);
  ntext_over++;
}

static void views_cut(const char *full, const char *shown) {
  if (ntext_cut < 3) snprintf(text_cut[ntext_cut], sizeof text_cut[0], "\"%.24s\" as \"%.24s\"", full, shown);
  ntext_cut++;
  if (strpbrk(full, "0123456789")) nnum_cut++;
}

static bool text_has(const char *want) {
  for (int i = 0; i < ntext && i < 256; i++)
    if (strstr(text_seen[i], want)) return true;
  return false;
}

static float luma(uint32_t c) { return 0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255); }

/* the WCAG contrast of a text against what it stands on: its ink against the median of the pixels that touch the
 * ink (an outline or a shadow counts, as for the eye), as the live test player measures it (tools/livepilot) */
static float lin_c(int v) {
  float c = (float)v / 255;
  return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
static float rel_lum(uint32_t c) { return 0.2126f * lin_c((int)(c >> 16 & 255)) + 0.7152f * lin_c((int)(c >> 8 & 255)) + 0.0722f * lin_c((int)(c & 255)); }
static bool ink_near(uint32_t a, uint32_t b) {
  for (int k = 0; k < 24; k += 8)
    if (abs((int)(a >> k & 255) - (int)(b >> k & 255)) > 24) return false;
  return true;
}
static int cmp_float(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}
static float text_contrast(int x0, int y0, int x1, int y1, uint32_t ink) {
  static float L[8192];
  int n = 0;
  for (int y = y0 - 2; y < y1 + 2 && n < 8192; y++)
    for (int x = x0 - 2; x < x1 + 2 && n < 8192; x++) {
      if (x < 0 || y < 0 || x >= VW || y >= VH) continue;
      uint32_t c = VF.px[y * VW + x];
      if (ink_near(c, ink)) continue;
      bool touches = false;
      for (int dy = -1; dy <= 1 && !touches; dy++)
        for (int dx = -1; dx <= 1 && !touches; dx++) {
          int xx = x + dx, yy = y + dy;
          touches = (dx || dy) && xx >= 0 && yy >= 0 && xx < VW && yy < VH && ink_near(VF.px[yy * VW + xx], ink);
        }
      if (touches) L[n++] = rel_lum(c);
    }
  if (!n) return 21; /* no ink drawn (a blank or fully covered text): nothing to read */
  qsort(L, (size_t)n, sizeof *L, cmp_float);
  float bg = L[n / 2], fg = rel_lum(ink);
  return (fmaxf(bg, fg) + 0.05f) / (fminf(bg, fg) + 0.05f);
}
/* the lowest contrast among the panel's texts (below row top): 3:1 at least, the live test player's bar */
static float panel_contrast(int top, char *worst, size_t n) {
  float lo = 21;
  worst[0] = 0;
  for (int i = 0; i < ntext && i < 256; i++) {
    if (!text_box[i].ink || text_box[i].y0 < top || !text_box[i].s[0] || !strcmp(text_box[i].s, " ")) continue;
    float c = text_contrast(text_box[i].x0, text_box[i].y0, text_box[i].x1, text_box[i].y1, text_box[i].ink);
    if (c < lo) lo = c, snprintf(worst, n, "%s", text_box[i].s);
  }
  return lo;
}

/* holes: 16 x 16 blocks of pure black in the picture's top sh rows */
static int black_blocks(int sh) {
  int n = 0;
  for (int by = 0; by + 16 <= sh; by += 16)
    for (int bx = 0; bx + 16 <= VW; bx += 16) {
      bool all = true;
      for (int y = by; y < by + 16 && all; y++)
        for (int x = bx; x < bx + 16 && all; x++) all = (VF.px[y * VW + x] & 0xFFFFFF) == 0;
      n += all;
    }
  return n;
}

static float mean_luma(int sh) {
  double s = 0;
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < VW; x++) s += luma(VF.px[y * VW + x]);
  return (float)(s / ((double)sh * VW));
}

static const char *shots_dir = ".";
static bool shots_given; /* --shots DIR: without it the GPU sea stills go to the system's temp folder and are removed */
/* a failed still, kept as a BMP beside the contact sheet for a closer look */
static void keep_still(const char *name, bool ok) {
  if (ok && !getenv("A4C_VIEWS_ALL")) return; /* A4C_VIEWS_ALL=1: every still, not only the failed ones */
  char path[512];
  int n = snprintf(path, sizeof path, "%s/", shots_dir);
  if (n < 0 || n >= (int)sizeof path - 5) { /* a folder name too long for the path: no still, rather than past it */
    printf("  (no still: the folder's name is too long)\n");
    return;
  }
  for (const char *c = name; *c && n < (int)sizeof path - 5; c++) path[n++] = (*c == ' ' || *c == ',') ? '-' : *c;
  snprintf(path + n, sizeof path - (size_t)n, ".bmp");
  plat_save_bmp(path, VF.px, VW, VH);
}

static void sheet_add(void) {
  if (!sheet) return;
  int col = nsheet % SHEET_COLS, row = nsheet / SHEET_COLS;
  if (row >= sheet_rows) return;
  for (int y = 0; y < TH; y++)
    for (int x = 0; x < TW; x++) {
      unsigned r = 0, gr = 0, b = 0;
      for (int k = 0; k < 9; k++) {
        uint32_t c = VF.px[(y * 3 + k / 3) * VW + x * 3 + k % 3];
        r += (c >> 16) & 255, gr += (c >> 8) & 255, b += c & 255;
      }
      sheet[(row * TH + y) * (SHEET_COLS * TW) + col * TW + x] = (r / 9) << 16 | (gr / 9) << 8 | (b / 9);
    }
  nsheet++;
}

/* the contact sheet as a PNG: stored deflate blocks, our own writer (as tests/test_figure.c) */
static uint32_t crc_png(const uint8_t *d, size_t n, uint32_t c) {
  c = ~c;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  }
  return ~c;
}
static void be32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)(v >> 24), b[1] = (uint8_t)(v >> 16), b[2] = (uint8_t)(v >> 8), b[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
  uint8_t h[8];
  be32(h, (uint32_t)n), memcpy(h + 4, type, 4);
  fwrite(h, 1, 8, f);
  if (n) fwrite(d, 1, n, f);
  be32(h, crc_png(d, n, crc_png((const uint8_t *)type, 4, 0)));
  fwrite(h, 1, 4, f);
}
static bool write_png(const char *path, const uint32_t *px, int W, int H) {
  size_t row = (size_t)W * 3 + 1, raw_n = row * H, z_n = 2 + raw_n + (raw_n / 65535 + 1) * 5 + 4, at = 0, from = 0;
  uint8_t *raw = malloc(raw_n), *z = malloc(z_n);
  FILE *f = raw && z ? fopen(path, "wb") : NULL;
  if (!f) {
    free(raw), free(z);
    return false;
  }
  for (int y = 0; y < H; y++) {
    raw[y * row] = 0;
    for (int x = 0; x < W; x++) {
      uint32_t v = px[(size_t)y * W + x];
      raw[y * row + 1 + x * 3] = (uint8_t)(v >> 16), raw[y * row + 2 + x * 3] = (uint8_t)(v >> 8), raw[y * row + 3 + x * 3] = (uint8_t)v;
    }
  }
  uint32_t a = 1, b = 0;
  z[at++] = 0x78, z[at++] = 0x01;
  for (size_t left = raw_n; left;) {
    size_t n = left > 65535 ? 65535 : left;
    z[at++] = left == n, z[at++] = (uint8_t)n, z[at++] = (uint8_t)(n >> 8), z[at++] = (uint8_t)~n, z[at++] = (uint8_t)(~n >> 8);
    memcpy(z + at, raw + from, n);
    for (size_t i = 0; i < n; i++) a = (a + raw[from + i]) % 65521, b = (b + a) % 65521;
    at += n, from += n, left -= n;
  }
  be32(z + at, b << 16 | a), at += 4;
  static const uint8_t SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  uint8_t ih[13] = { 0 };
  be32(ih, (uint32_t)W), be32(ih + 4, (uint32_t)H), ih[8] = 8, ih[9] = 2;
  fwrite(SIG, 1, 8, f);
  png_chunk(f, "IHDR", ih, 13), png_chunk(f, "IDAT", z, at), png_chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw), free(z);
  return true;
}

/* a jet at (x, y, z), heading h (0 = +z, 90 = +x), pitch and roll in degrees */
static void pose(Plane *p, float x, float y, float z, float h, float pitch, float roll) {
  float hr = h / DEG, pr = pitch / DEG, rr = roll / DEG;
  p->pos = v3(x, y, z);
  p->f = v3(sinf(hr) * cosf(pr), sinf(pr), cosf(hr) * cosf(pr));
  vec3 rl = v3_norm(v3_cross(v3(0, 1, 0), p->f)); /* level right wing */
  vec3 ul = v3_cross(p->f, rl);
  p->r = v3_add(v3_scale(rl, cosf(rr)), v3_scale(ul, -sinf(rr)));
  p->u = v3_cross(p->f, p->r);
  p->vel = v3_scale(p->f, p->speed);
}

static Camera cam_of(const Plane *p) {
  Camera c = { p->pos, p->r, p->u, p->f, false };
  return c;
}

/* the horizon: per column, the row where the colour changes most near where the pitch and roll put it */
static void horizon_test(Game *g, Plane *p, const char *map, float pitch, float roll) {
  pose(p, 0, 300, 0, 200, pitch, roll); /* away from the arena's mountains */
  Camera cam = cam_of(p);
  View V;
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  sheet_add();
  float sx = 0, sy = 0, sxx = 0, sxy = 0, err = 0;
  int n = 0;
  for (int x = 40; x < VW - 40; x += 20) {
    float X = (x - V.cx) / V.F, Y = (p->f.y + X * p->r.y) / p->u.y; /* the ray's world height is 0 there */
    int ye = (int)lroundf(V.cy + Y * V.F), best = -1;
    float bd = 0;
    for (int y = ye - 30; y < ye + 30; y++) {
      if (y < 1 || y >= VH) continue;
      uint32_t a = VF.px[(y - 1) * VW + x], b = VF.px[y * VW + x];
      float d = fabsf((float)((a >> 16) & 255) - ((b >> 16) & 255)) + fabsf((float)((a >> 8) & 255) - ((b >> 8) & 255)) +
                fabsf((float)(a & 255) - (b & 255));
      if (d > bd) bd = d, best = y;
    }
    if (best < 0 || bd < 6) continue;
    err = fmaxf(err, fabsf((float)(best - ye)));
    sx += x, sy += best, sxx += (float)x * x, sxy += (float)x * best, n++;
  }
  float slope = n > 1 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0;
  float seen = -atanf(slope) * DEG; /* the line's tilt; rolling right tilts it left */
  char name[64];
  snprintf(name, sizeof name, "horizon %s pitch %.0f roll %.0f", map, pitch, roll);
  /* the tilt must follow the roll; the line sits below the true horizon by the far ground's haze (reported) */
  keep_still(name, verdict("views", name, n >= 8 && fabsf(seen - roll) <= 3.5f,
                           "%d columns, line tilted %.1f deg; %.0f px (%.1f deg) below the true horizon at worst", n, seen, err,
                           atanf(err / V.F) * DEG));
}

/* the sun disc where LIGHT points, the camera aimed a little to one side of it */
static void sun_test(Game *g, Plane *p, const char *map, const char *tod) {
  vec3 s = v3_norm(LIGHT);
  float h = atan2f(s.x, s.z) * DEG + 9, el = asinf(s.y) * DEG - 4;
  pose(p, 0, 6000, 0, h, el, 0); /* above every mountain, so none hides a low sun */
  Camera cam = cam_of(p);
  View V;
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  sheet_add();
  vec3 c = to_cam(&cam, v3_add(cam.pos, v3_scale(s, 1e5f)));
  float ex = V.cx + c.x / c.z * V.F, ey = V.cy - c.y / c.z * V.F, mx = 0, my = 0, w = 0;
  /* the disc's colour (the sun's, 60 % of the way to white): where it is drawn, and its centre */
  uint32_t sc = MAP ? MAP->tod[MAP_TOD].sun_color : 0xfff4e0;
  int want[3];
  for (int k = 0; k < 3; k++) want[k] = (int)(((sc >> (16 - 8 * k)) & 255) * 0.4f + 255 * 0.6f);
  for (int y = 0; y < VH; y++)
    for (int x = 0; x < VW; x++) {
      uint32_t q = VF.px[y * VW + x];
      if (abs((int)((q >> 16) & 255) - want[0]) <= 3 && abs((int)((q >> 8) & 255) - want[1]) <= 3 && abs((int)(q & 255) - want[2]) <= 3)
        mx += x, my += y, w++;
    }
  mx /= fmaxf(w, 1), my /= fmaxf(w, 1);
  char name[64];
  snprintf(name, sizeof name, "sun %s %s", map, tod);
  keep_still(name, verdict("views", name, w >= 20 && hypotf(mx - ex, my - ey) <= 3,
                           "disc of %d px centred at %.0f,%.0f, the light's direction at %.0f,%.0f", (int)w, mx, my, ex, ey));
}

/* a jet lit by the sun: the same pose drawn under two suns, abeam (as the views have it) and behind the camera (on
 * the faces the camera sees), and those same pixels must come out brighter with the sun on them. Comparing a pixel
 * with itself leaves out the paint and the shape (splitting a side-on jet into halves only compared its dark tail
 * with its light nose); a renderer whose faces ignore the sun's direction draws both alike and fails. */
static void light_test(Game *g, Plane *p, Plane *e, const char *map) {
  vec3 s = v3_norm(LIGHT);
  float sh = atan2f(s.x, s.z) * DEG;
  pose(p, 0, 1500, 0, sh + 90, 0, 0); /* the camera looks across the light: the sun on its left */
  pose(e, p->pos.x + p->f.x * 30, 1500, p->pos.z + p->f.z * 30, sh + 180, 0, 0);
  e->alive = true, e->inactive = false;
  Camera cam = cam_of(p);
  View V;
  static uint32_t bare[VW * VH], abeam[VW * VH];
  e->alive = false;
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  memcpy(bare, VF.px, sizeof bare);
  e->alive = true;
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  memcpy(abeam, VF.px, sizeof abeam);
  vec3 sun = LIGHT;
  LIGHT = v3_norm(v3_add(v3_scale(cam.f, -1), v3_scale(cam.u, 0.25f))); /* behind the camera, a little high */
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  LIGHT = sun;
  sheet_add();
  float side = 0, front = 0;
  int n = 0;
  for (int i = 0; i < VW * VH; i++) {
    if (abeam[i] == bare[i]) continue; /* the jet's own pixels */
    side += luma(abeam[i]), front += luma(VF.px[i]), n++;
  }
  side /= fmaxf(n, 1), front /= fmaxf(n, 1);
  char name[64];
  snprintf(name, sizeof name, "sunlit jet %s", map);
  keep_still(name, verdict("views", name, n > 100 && front > side + 6,
                           "the jet's %d px: %.0f with the sun abeam, %.0f with it behind the camera (on them)", n, side,
                           front));
  e->alive = false, e->inactive = true;
}


/* the horizon you see against the HUD's 0 line: over open sea, wings level, the first row below the true
 * horizon (where a level ray meets the sky) that is no longer the sky's own colour, per column, the median */
static bool open_sea(float x, float z, float h, float water) {
  for (float d = 1500; d <= 24000; d *= 1.25f)
    for (int k = -1; k <= 1; k++) {
      float a = (h + k * 30) / DEG;
      if (terrain_h(x + sinf(a) * d, z + cosf(a) * d) > water + 0.5f) return false;
    }
  return true;
}
static void sea_horizon_test(Game *g, Plane *p, const Map *M, int tod, int wx, float alt, const char *todn, const char *wxn) {
  float bx = 0, bz = 0, bh = -1;
  for (int i = 0; i < 64 && bh < 0; i++) {
    float x = (i % 8 - 3.5f) * 2600, z = (i / 8 - 3.5f) * 2600;
    for (float h = 0; h < 360 && bh < 0; h += 15)
      if (terrain_h(x, z) <= M->water_level + 0.5f && open_sea(x, z, h, M->water_level)) bx = x, bz = z, bh = h;
  }
  if (bh < 0) {
    printf("  (no open sea on %s: water %.1f, height at 0,0 %.1f)\n", M->id, M->water_level, terrain_h(0, 0));
    return; /* no open sea on this map */
  }
  pose(p, bx, M->water_level + alt, bz, bh, 0, 0);
  Camera cam = cam_of(p);
  View V;
  fb_clear(&VF, 0xFF000000u);
  render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
  sheet_add();
  const TimeOfDay *t = &M->tod[tod];
  float offs[64];
  int n = 0;
  for (int x = 20; x < VW - 20 && n < 64; x += 10) {
    int found = -1;
    for (int y = (int)V.cy - 40; y < VH - 3 && found < 0; y++) {
      bool off = true;
      for (int k = 0; k < 3 && off; k++) {
        int yy = y + k;
        float u = fmaxf(0, (V.cy - (yy + 0.5f)) / V.sh); /* where the sky's gradient is at this row */
        uint32_t q = VF.px[yy * VW + x];
        float d = 0;
        for (int c = 16; c >= 0; c -= 8) {
          float sk = ((t->sky_hor >> c) & 255) * (1 - u) + ((t->sky_top >> c) & 255) * u;
          d += fabsf(((q >> c) & 255) - sk);
        }
        off = d > 30;
      }
      if (off) found = y;
    }
    if (found >= 0 && found > V.cy - 6) offs[n++] = found - V.cy; /* above that: a cloud, not the sea */
  }
  for (int i = 1; i < n; i++) /* the median */
    for (int j = i; j > 0 && offs[j] < offs[j - 1]; j--) {
      float tmp = offs[j];
      offs[j] = offs[j - 1], offs[j - 1] = tmp;
    }
  float med = n ? offs[n / 2] : 99, deg = atanf(med / V.F) * DEG, lim = wx == WX_CLEAR || wx == WX_SCATTERED ? 1.0f : 2.0f;
  char name[96];
  snprintf(name, sizeof name, "sea horizon %s %s %s %.0f m", M->id, todn, wxn, alt);
  if (n < 20 && wx >= WX_OVERCAST) { /* under a low overcast the sea's edge is in the cloud: nothing to line up */
    keep_still(name, verdict("views", name, true, "the horizon is hidden in cloud (%d clear columns)", n));
    return;
  }
  keep_still(name, verdict("views", name, n >= 20 && fabsf(deg) <= lim,
                           "the sea starts %.0f px (%.2f deg) below the HUD's 0 line, %d columns, limit %.0f deg", med, deg, n, lim));
}


/* ---- the same horizon on the GPU (--gpu): a window, the GPU renderer, its screenshot read back ---- */
static uint32_t *gshot;
static int gshot_w, gshot_h;
/* a BMP as the screenshot writer saves it (32 bits, BGRA, bottom-up or top-down) into gshot as 0xRRGGBB */
static bool load_bmp(const char *path) {
  size_t n = 0;
  uint8_t *b = plat_load_file(path, &n);
  if (!b || n < 54) return free(b), false;
  uint32_t off = b[10] | b[11] << 8 | b[12] << 16 | (uint32_t)b[13] << 24;
  int32_t w = (int32_t)(b[18] | b[19] << 8 | b[20] << 16 | (uint32_t)b[21] << 24), h = (int32_t)(b[22] | b[23] << 8 | b[24] << 16 | (uint32_t)b[25] << 24);
  int bpp = b[28] | b[29] << 8, ah = h < 0 && h != INT32_MIN ? -h : h;
  size_t row = w > 0 ? ((size_t)w * (size_t)(bpp / 8) + 3) & ~(size_t)3 : 0, all;
  /* the rows as stored, padding included, must all be in the file */
  if ((bpp != 32 && bpp != 24) || w <= 0 || ah <= 0 || !safe_mul(row, (size_t)ah, &all) || off > n || all > n - off)
    return free(b), false;
  free(gshot);
  gshot = malloc((size_t)w * (size_t)ah * 4);
  if (!gshot) return free(b), false;
  gshot_w = w, gshot_h = ah;
  for (int y = 0; y < ah; y++) {
    const uint8_t *r = b + off + row * (size_t)(h < 0 ? y : ah - 1 - y);
    for (int x = 0; x < w; x++) gshot[(size_t)y * w + x] = (uint32_t)r[x * (bpp / 8) + 2] << 16 | r[x * (bpp / 8) + 1] << 8 | r[x * (bpp / 8)];
  }
  free(b);
  return true;
}

/* where the sky visibly ends under the true horizon (row cy): per column, the sharpest step in colour between
 * neighbouring rows from a little above the line to a tenth of the picture below it, above the waves near by (a haze makes the step small
 * but it stays the sharpest); the median, in px */
static int horizon_rows(const uint32_t *px, int W, int H, float cy, float *med) {
  float offs[128];
  int n = 0, step = W / 64 > 1 ? W / 64 : 1, lo = (int)(cy - H * 0.03f), hi = (int)(cy + H * 0.1f);
  int sky0 = (int)cy - 16; /* the 16 px of sky above the HUD's line: a column with cloud there is not read */
  if (sky0 < 1) sky0 = 1;
  if (lo < 1) lo = 1;
  if (hi > H - 1) hi = H - 1;
  for (int x = step / 2; x < W && n < 128; x += step) {
    /* each row's colour averaged over the column's whole strip: waves and foam are noise across it, the horizon
     * is the same all the way along */
    int best = -1;
    float bd = 0, sky = 0, prev[3] = { 0 };
    int y0 = sky0 < lo ? sky0 : lo;
    for (int y = y0 - 1; y < hi; y++) {
      float cur[3] = { 0 };
      for (int xx = x - step / 2; xx < x - step / 2 + step && xx < W; xx++)
        for (int c = 0; c < 3; c++) cur[c] += (float)((px[(size_t)y * W + xx] >> (16 - 8 * c)) & 255) / step;
      float d = fabsf(cur[0] - prev[0]) + fabsf(cur[1] - prev[1]) + fabsf(cur[2] - prev[2]);
      if (y >= lo && d > bd) bd = d, best = y;
      if (y >= sky0 && y < (int)cy - 1 && d > sky) sky = d; /* the biggest step in the sky just above the line */
      for (int c = 0; c < 3; c++) prev[c] = cur[c];
    }
    /* a step at the bottom of the band is the sea's own haze fading, not a horizon: under cloud there is none */
    /* a cloud's edge in the sky just above (a step half as big as the edge found, or more): the column is not read */
    if (best >= 0 && bd >= 4 && best < hi - (hi - lo) / 8 && sky < bd * 0.5f) offs[n++] = best - cy;
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && offs[j] < offs[j - 1]; j--) {
      float t = offs[j];
      offs[j] = offs[j - 1], offs[j - 1] = t;
    }
  *med = n ? offs[n / 2] : 99;
  return n;
}

static void gpu_sea_horizon(Game *g, Plane *p, const Map *M, int wx, float alt, const char *todn, const char *wxn) {
  float bx = 0, bz = 0, bh = -1;
  for (int i = 0; i < 64 && bh < 0; i++) {
    float x = (i % 8 - 3.5f) * 2600, z = (i / 8 - 3.5f) * 2600;
    for (float h = 0; h < 360 && bh < 0; h += 15)
      if (terrain_h(x, z) <= M->water_level + 0.5f && open_sea(x, z, h, M->water_level)) bx = x, bz = z, bh = h;
  }
  if (bh < 0) return;
  pose(p, bx, M->water_level + alt, bz, bh, 0, 0);
  int W, H;
  plat_window_pixels(&W, &H);
  static Framebuffer ov;
  if (!fb_resize(&ov, W, H)) return;
  fb_clear(&ov, 0); /* the HUD layer: see-through */
  Camera cam = cam_of(p);
  View V;
  render_scene(&ov, g, &cam, 0, 0, W, H, &V, p);
  char path[512];
  const char *tmp = getenv("TMPDIR");
  if (!tmp || !*tmp) tmp = getenv("TEMP"); /* Windows */
  snprintf(path, sizeof path, "%s/gpu-sea-%s-%s-%s-%.0f.bmp", shots_given ? shots_dir : tmp && *tmp ? tmp : ".", M->id,
           todn, wxn, alt);
  gpu_screenshot(path);
  gpu_present(&ov);
  char name[96];
  snprintf(name, sizeof name, "GPU sea horizon %s %s %s %.0f m", M->id, todn, wxn, alt);
  bool back = load_bmp(path);
  if (!shots_given) remove(path); /* only read back, never left behind */
  if (!back || gshot_w != W || gshot_h != H) {
    verdict("views", name, false, "no screenshot came back (%s)", path);
    return;
  }
  float med;
  int n = horizon_rows(gshot, W, H, V.cy, &med);
  /* in overcast and storm the level line is fog by design: the sea shows through it only nearer than where the
   * shaders' fog over the sea reaches its limit (render3d's fog, gpu.c's own fog maths), and from this height that
   * nearest clear sea lies atan(alt / d) below the line; clear and scattered skies keep their limit */
  float fs, fr, fm;
  render_fog(&fs, &fr, &fm);
  float d = gpu_sea_fog_full(fs, fr, fm);
  float deg = atanf(med / V.F) * DEG, lim = wx == WX_CLEAR || wx == WX_SCATTERED ? 1.0f : fmaxf(2.0f, atanf(alt / d) * DEG + 0.5f);
  /* only columns with clear sky just above the line are read (horizon_rows): with too few of them in overcast and
   * storm, the horizon counts as hidden in cloud */
  if (n < 20 && wx >= WX_OVERCAST) {
    verdict("views", name, true, "the horizon is hidden in cloud (%d clear columns of 64)", n);
    return;
  }
  verdict("views", name, n >= 20 && fabsf(deg) <= lim,
          "the sea starts %.0f px (%.2f deg) below the HUD's 0 line, %d columns, limit %.2f deg (the fog reaches its limit %.1f km off)",
          med, deg, n, lim, d / 1000);
}

static void gpu_views_part(const char *dir) {
  printf("\n== views on the GPU ==\n");
  shots_dir = dir;
  if (!plat_window_open("views", VW, VH) || !gpu_init(plat_native_window(), 2) || !gpu_active()) {
    verdict("views", "GPU", false, "no GPU renderer: %s", plat_error());
    return;
  }
  printf("  renderer: %s\n", gpu_driver());
  static const char *TODN[TOD_COUNT] = { "dawn", "day", "dusk", "night" }, *WXN[WX_COUNT] = { "clear", "scattered", "overcast", "storm" };
  static const float ALT[3] = { 300, 2000, 6000 };
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  for (int m = 0; m < assets_map_count(); m++) {
    const Map *M = assets_map(m);
    for (int t = 0; t < TOD_NIGHT; t++)
      for (int w = 0; w < WX_COUNT; w++)
        for (int a = 0; a < 3 && w != WX_SCATTERED; a++) {
          world_set_map(M, t, w);
          Plane *q = solo(g, 0, false, false, false, false, false);
          gpu_sea_horizon(g, q, M, w, ALT[a], TODN[t], WXN[w]);
        }
  }
  free(g);
  free(gshot);
  gshot = NULL;
  gpu_quit();
  plat_window_close();
}

/* a whole view (cockpit or chase, HUD and panel): holes, brightness, texts in frame, the instruments' readings */
static void view_test(Game *g, Plane *p, const char *what, int tod, bool chase, bool readings) {
  p->chase = chase;
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  r3d_big_discs = 0;
  fb_text_hook = views_text;
  hud_text_cut_hook = views_cut;
  hud_text_over_hook = views_over;
  fb_clear(&VF, 0xFF000000u);
  ViewOpts o = { true, false, false };
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  fb_text_hook = NULL;
  hud_text_cut_hook = NULL;
  hud_text_over_hook = NULL;
  sheet_add();
  int ph = (int)roundf(fmaxf(52, fminf(VH * 0.24f, VW * 0.21f))), sh = VH - ph;
  int holes = black_blocks(sh);
  float L = mean_luma(sh);
  static const float LO[TOD_COUNT] = { 25, 70, 25, 1 }, HI[TOD_COUNT] = { 220, 235, 220, 110 };
  char name[96], why[192] = "";
  if (readings) {
    char b[3][16];
    float hdg = fmodf(atan2f(p->f.x, p->f.z) * DEG + 360, 360);
    snprintf(b[0], sizeof b[0], "%03d", (int)hdg % 360);
    snprintf(b[1], sizeof b[1], "%d", (int)roundf(p->speed * KT_PER_MPS));
    snprintf(b[2], sizeof b[2], "%d", (int)roundf(p->speed * KMH_PER_MPS));
    char mach[16];
    snprintf(mach, sizeof mach, "%.2f", p->speed / SOUND_MPS);
    /* the speed in knots, km/h or as a Mach number (some panels give the digits only on the dial's needle) */
    /* the speed in digits is optional: some panels (the Soviet ones) give it on a needle only */
    bool h = text_has(b[0]), s = text_has(b[1]) || text_has(b[2]) || text_has(mach);
    snprintf(why, sizeof why, "; heading %s %s, speed %s", b[0], h ? "shown" : "NOT shown", s ? "in digits" : "on needles only");
    readings = h;
  } else readings = true;
  snprintf(name, sizeof name, "%s", what);
  char cuts[320] = "";
  if (ntext_cut)
    snprintf(cuts, sizeof cuts, "; %d labels cut to fit (%d numbers): %s%s%s%s%s", ntext_cut, nnum_cut, text_cut[0], ntext_cut > 1 ? ", " : "",
             ntext_cut > 1 ? text_cut[1] : "", ntext_cut > 2 ? ", " : "", ntext_cut > 2 ? text_cut[2] : "");
  if (ntext_over) {
    size_t n = strlen(cuts);
    snprintf(cuts + n, sizeof cuts - n, "; %d numbers past their box: %s%s%s%s%s", ntext_over, text_over[0], ntext_over > 1 ? ", " : "",
             ntext_over > 1 ? text_over[1] : "", ntext_over > 2 ? ", " : "", ntext_over > 2 ? text_over[2] : "");
  }
  if (!chase) { /* the panel's texts readable: 3:1 against what they stand on */
    char worst[24], cname[128];
    float pc = panel_contrast(sh, worst, sizeof worst);
    snprintf(cname, sizeof cname, "%s, panel text contrast", what);
    verdict("views", cname, pc >= 3, "lowest %.1f:1 (3:1 at least)%s%s", pc, worst[0] ? ": " : "", worst);
  }
  keep_still(name, verdict("views", name, holes == 0 && L >= LO[tod] && L <= HI[tod] && ntext_out == 0 && nnum_cut == 0 && ntext_over == 0 && readings &&
                                             r3d_big_discs == 0,
          "%d holes, brightness %.0f (%.0f-%.0f), %d giant cloud discs, %d texts, %d outside the picture%s%s%s%s", holes, L, LO[tod],
          HI[tod], r3d_big_discs, ntext, ntext_out, ntext_out ? ": " : "", ntext_out ? text_out[0] : "", why, cuts));
}

/* The tankers' range labels near the windscreen's edge: each tanker (ours, low on fuel, and theirs) a few metres
 * inside the view's left and then its right edge, its "TANKER #.#KM" whole inside the view (the view's clip cut it
 * to "KER 9.7KM" there before) */
/* a failed check's example for its message: as much of it as fits, and "..." at the end when it was cut, so a
 * long text never truncates silently (the snprintf's result is looked at, docs/SAFE_C.md) */
static void example(char *ex, size_t size, const char *fmt, ...) A4C_PRINTF(3, 4);
static void example(char *ex, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(ex, size, fmt, ap);
  va_end(ap);
  if (n < 0) ex[0] = 0;
  else if ((size_t)n >= size && size > 4) memcpy(ex + size - 4, "...", 4);
}

static void tanker_label_test(Game *g, Plane *p) {
  float keep_fuel = p->fuel;
  Tanker keep[2] = { g->tankers[0], g->tankers[1] };
  p->fuel = 20;
  p->chase = false;
  ViewOpts o = { true, false, false };
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o); /* the HUD's camera: where the view's edges are */
  const HudView *V = &hud_view;
  for (int side = 0; side < 4; side++) {
    /* 4 km out on the line through a point 4 px inside the view's left, right, top or bottom edge, the two
     * tankers apart along it */
    for (int t = 0; t < 2; t++) {
      Tanker *T = &g->tankers[t];
      float ex0 = side == 0 ? V->vx + 4 : side == 1 ? V->vx + V->vw - 4 : V->cx + (t ? -120.0f : 120.0f);
      float ey = side == 2 ? V->vy + 4 : side == 3 ? V->vy + V->sh - 4 : V->cy + (t ? -60.0f : 60.0f);
      vec3 d = v3_norm(v3_add(V->f, v3_add(v3_scale(V->r, (ex0 - V->cx) / V->F), v3_scale(V->u, (V->cy - ey) / V->F))));
      T->gone = false, T->state = TK_STATION;
      T->pos = v3_add(V->pos, v3_scale(d, 4000));
    }
    ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
    fb_text_hook = views_text;
    fb_clear(&VF, 0xFF000000u);
    hud_view_p = NULL;
    render_view(&VF, g, p, 0, 0, VW, VH, o);
    fb_text_hook = NULL;
    int labels = 0, cut = 0;
    char ex[192] = ""; /* (room for the text and every number in full: nothing cut) */
    for (int i = 0; i < ntext && i < 256; i++) {
      if (strncmp(text_box[i].s, "TANKER ", 7) || !strstr(text_box[i].s, "KM")) continue;
      labels++;
      if (text_box[i].x0 < V->vx || text_box[i].x1 > V->vx + V->vw || text_box[i].y0 < V->vy || text_box[i].y1 > V->vy + V->sh) {
        cut++;
        example(ex, sizeof ex, ": \"%s\" at %d,%d-%d,%d, the view %.0f,%.0f-%.0f,%.0f", text_box[i].s, text_box[i].x0, text_box[i].y0,
                 text_box[i].x1, text_box[i].y1, V->vx, V->vy, V->vx + V->vw, V->vy + V->sh);
      }
    }
    char name[64];
    static const char *const EDGE[4] = { "left", "right", "top", "bottom" };
    snprintf(name, sizeof name, "tanker labels at the view's %s edge", EDGE[side]);
    verdict("views", name, labels == 2 && cut == 0, "%d labels drawn, %d cut by the view's edge%s", labels, cut, ex);
  }
  g->tankers[0] = keep[0], g->tankers[1] = keep[1];
  p->fuel = keep_fuel;
}

/* The lock and the messages under the sight: an enemy locked where the first message line is written, in a hard
 * turn (so "FAST - WIDE TURN" or another is up): no message written over LOCK (it was, in the live pilot's
 * pictures) */
static void lock_message_test(Game *g, Plane *p, Plane *e) {
  Plane keep = *p, keep_e = *e;
  ViewOpts o = { true, false, false };
  p->chase = false;
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  const HudView *V = &hud_view;
  /* the diamond so that its LOCK (13 HUD pixels above it) sits on the messages' first line */
  float ks = fmaxf(1, floorf(fminf(V->vw / 300, V->sh / 150))), ey = V->cy + V->sh * 0.2f + 13 * ks;
  vec3 d = v3_norm(v3_add(V->f, v3_scale(V->u, (V->cy - ey) / V->F)));
  e->pos = v3_add(V->pos, v3_scale(d, 1500));
  e->alive = true, e->inactive = false;
  p->gload = p->gmeter = 5, p->speed = 330; /* fast in a hard turn: a message */
  p->locked = true, p->lockT = 99, p->lockTarget = ref_plane(e->idx);
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  fb_clear(&VF, 0xFF000000u);
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  fb_text_hook = NULL;
  int lock = -1, over = 0;
  char ex[96] = "";
  for (int i = 0; i < ntext && i < 256; i++)
    if (!strcmp(text_box[i].s, "LOCK") && text_box[i].y1 < (int)(V->vy + V->sh)) lock = i;
  for (int i = 0; lock >= 0 && i < ntext && i < 256; i++) {
    if (i == lock || !text_box[i].s[0] || text_box[i].y0 >= (int)(V->vy + V->sh)) continue;
    if (text_box[lock].x0 < text_box[i].x1 && text_box[i].x0 < text_box[lock].x1 && text_box[lock].y0 < text_box[i].y1 &&
        text_box[i].y0 < text_box[lock].y1) {
      over++;
      example(ex, sizeof ex, ": \"%s\" over it", text_box[i].s);
    }
  }
  if (getenv("A4C_VIEWS_DEBUG"))
    for (int i = 0; i < ntext && i < 256; i++) printf("    text %s %d,%d-%d,%d\n", text_box[i].s, text_box[i].x0, text_box[i].y0, text_box[i].x1, text_box[i].y1);
  verdict("views", "the lock's LOCK clear of the messages", lock >= 0 && over == 0, "LOCK %s, %d texts over it%s",
          lock >= 0 ? "drawn" : "NOT drawn", over, ex);
  *p = keep, *e = keep_e;
}

/* An enemy's range label at the view's edges: an enemy just inside the box's margin at the left and the right edge,
 * its "#.#KM" whole inside the view (it ran off the picture there) */
static void range_edge_test(Game *g, Plane *p, Plane *e) {
  Plane keep = *p, keep_e = *e;
  ViewOpts o = { true, false, false };
  p->chase = false;
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  const HudView *V = &hud_view;
  float ks = fmaxf(1, floorf(fminf(V->vw / 300, V->sh / 150)));
  for (int side = 0; side < 3; side++) {
    float ex0 = side == 1 ? V->vx + V->vw - 15 * ks : V->vx + 15 * ks; /* inside the 14-pixel margin the box needs */
    vec3 d = v3_norm(v3_add(V->f, v3_scale(V->r, (ex0 - V->cx) / V->F)));
    if (side == 2) d = v3_norm(v3_add(v3_scale(V->r, -1), v3_scale(V->f, -0.3f))); /* off the view to the left: the edge arrow */
    e->pos = v3_add(V->pos, v3_scale(d, 12000));
    e->alive = true, e->inactive = false;
    ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
    fb_text_hook = views_text;
    fb_clear(&VF, 0xFF000000u);
    hud_view_p = NULL;
    render_view(&VF, g, p, 0, 0, VW, VH, o);
    fb_text_hook = NULL;
    int labels = 0, cut = 0;
    char ex[160] = "";
    for (int i = 0; i < ntext && i < 256; i++) {
      size_t L = strlen(text_box[i].s);
      if (L < 5 || strcmp(text_box[i].s + L - 2, "KM") || !strncmp(text_box[i].s, "TANKER", 6) || text_box[i].y0 >= (int)(V->vy + V->sh))
        continue;
      labels++;
      if (text_box[i].x0 < V->vx || text_box[i].x1 > V->vx + V->vw) {
        cut++;
        example(ex, sizeof ex, ": \"%s\" at %d-%d, the view %.0f-%.0f", text_box[i].s, text_box[i].x0, text_box[i].x1, V->vx, V->vx + V->vw);
      }
      for (int q = 0; q < ntext && q < 256; q++) /* and clear of the readouts at the left (it ran over "G -1.2") */
        if (q != i && text_box[q].s[0] && text_box[q].y0 < (int)(V->vy + V->sh) && text_box[i].x0 < text_box[q].x1 &&
            text_box[q].x0 < text_box[i].x1 && text_box[i].y0 < text_box[q].y1 && text_box[q].y0 < text_box[i].y1) {
          cut++;
          example(ex, sizeof ex, ": \"%s\" over \"%s\"", text_box[i].s, text_box[q].s);
        }
    }
    char name[96];
    static const char *const WHERE[3] = { "at the view's left edge", "at the view's right edge", "by the edge arrow, off the view to the left" };
    snprintf(name, sizeof name, "an enemy's range label %s", WHERE[side]);
    verdict("views", name, labels >= 1 && cut == 0, "%d range labels drawn, %d cut by the view's edge or over another text%s", labels, cut, ex);
  }
  *p = keep, *e = keep_e;
}

/* The start hint over a hazy sky: the HUD alone over the haze's colour as the live pilot's pictures had it low in
 * the view (0x7d98b6), the hint's ink plainly apart from it (60, the colour step the eye and the live pilot's reader
 * need; the outline alone set the dim purple apart) */
static void hint_haze_test(Game *g, Plane *p) {
  Plane keep = *p;
  ViewOpts o = { true, false, false };
  p->chase = false;
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  HudView V = hud_view;
  float t0 = g->time;
  g->time = 1;
  p->gload = p->gmeter = 5, p->speed = 330; /* fast in a hard turn: "FAST - WIDE TURN" too */
  fb_clear(&VF, 0xFF7D98B6u);
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  hud_view_p = NULL;
  hud_draw(&VF, g, p, &V, false);
  fb_text_hook = NULL;
  g->time = t0;
  int dist = -1, found = 0;
  for (int i = 0; i < ntext && i < 256; i++)
    if (!strncmp(text_box[i].s, "TAB  CHASE", 10) || !strncmp(text_box[i].s, "FAST - ", 7)) {
      uint32_t c = text_box[i].ink;
      int d = abs((int)(c >> 16 & 255) - 0x7d) + abs((int)(c >> 8 & 255) - 0x98) + abs((int)(c & 255) - 0xb6);
      dist = found++ ? (d < dist ? d : dist) : d;
    }
  verdict("views", "the start hint's and the corner speed's ink against a hazy sky", found == 2 && dist > 60, "%d apart (60 at least), %d of 2 drawn",
          dist, found);
  *p = keep;
}

/* The match-over page over a bright sky (it is glass over the last picture): every text on it 3:1 against what it
 * stands on, as the live pilot measures it (its column heads in the dim lavender read at 2.4:1 there) */
static void over_page_test(Game *g) {
  fb_clear(&VF, 0xFFD8DEE6u);
  bool over = g->over;
  g->over = true;
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  ui_draw(&VF, SCREEN_OVER, NULL, g, 0);
  fb_text_hook = NULL;
  g->over = over;
  char worst[24];
  float pc = panel_contrast(0, worst, sizeof worst);
  verdict("views", "the match-over page over a bright sky", pc >= 3, "lowest %.1f:1 (3:1 at least)%s%s", pc, worst[0] ? ": " : "", worst);
}

/* The tanker's label and an enemy's range label: an enemy just over the tanker, so its range label (under its box)
 * falls where the tanker's label is written; the two must not overlap (the live pilot read "3.TANKER 19.7KM") */
static void tanker_range_test(Game *g, Plane *p, Plane *e) {
  Plane keep = *p, keep_e = *e;
  Tanker keep_t[2] = { g->tankers[0], g->tankers[1] };
  ViewOpts o = { true, false, false };
  p->chase = false;
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  const HudView *V = &hud_view;
  float ks = fmaxf(1, floorf(fminf(V->vw / 300, V->sh / 150)));
  Tanker *T = &g->tankers[p->team & 1];
  float ty = V->cy - V->sh * 0.1f, ey = ty - 8 * ks - 6 * ks - 6 * ks; /* its range label 6+6 under its box's 6 */
  vec3 dt = v3_norm(v3_add(V->f, v3_scale(V->u, (V->cy - ty) / V->F))), de = v3_norm(v3_add(V->f, v3_scale(V->u, (V->cy - ey) / V->F)));
  T->gone = false, T->state = TK_STATION, T->pos = v3_add(V->pos, v3_scale(dt, 5000));
  g->tankers[!(p->team & 1)].gone = true;
  e->pos = v3_add(V->pos, v3_scale(de, 5000));
  e->alive = true, e->inactive = false;
  p->fuel = 20;
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  fb_clear(&VF, 0xFF000000u);
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  fb_text_hook = NULL;
  int lab = -1, over = 0;
  char ex[96] = "";
  for (int i = 0; i < ntext && i < 256; i++)
    if (!strncmp(text_box[i].s, "TANKER ", 7) && strstr(text_box[i].s, "KM")) lab = i;
  for (int i = 0; lab >= 0 && i < ntext && i < 256; i++) {
    if (i == lab || !text_box[i].s[0] || text_box[i].y0 >= (int)(V->vy + V->sh)) continue;
    if (text_box[lab].x0 < text_box[i].x1 && text_box[i].x0 < text_box[lab].x1 && text_box[lab].y0 < text_box[i].y1 &&
        text_box[i].y0 < text_box[lab].y1) {
      over++;
      example(ex, sizeof ex, ": \"%s\" over it", text_box[i].s);
    }
  }
  verdict("views", "the tanker's label clear of a range label", lab >= 0 && over == 0, "label %s, %d texts over it%s",
          lab >= 0 ? "drawn" : "NOT drawn", over, ex);
  *p = keep, *e = keep_e;
  g->tankers[0] = keep_t[0], g->tankers[1] = keep_t[1];
}

/* The tanker's label and the messages under the sight: our tanker (low on fuel, so its range is written) where
 * the first message line is, in a fast hard turn: no message written over "TANKER #.#KM" (the live pilot read "BINGO
 * - BASE 2 KM" there) */
static void tanker_message_test(Game *g, Plane *p) {
  Plane keep = *p;
  Tanker keep_t[2] = { g->tankers[0], g->tankers[1] };
  ViewOpts o = { true, false, false };
  p->chase = false;
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  const HudView *V = &hud_view;
  float ks = fmaxf(1, floorf(fminf(V->vw / 300, V->sh / 150))), ey = V->cy + V->sh * 0.2f + 8 * ks; /* label 8 above */
  vec3 d = v3_norm(v3_add(V->f, v3_scale(V->u, (V->cy - ey) / V->F)));
  Tanker *T = &g->tankers[p->team & 1];
  T->gone = false, T->state = TK_STATION, T->pos = v3_add(V->pos, v3_scale(d, 5000));
  g->tankers[!(p->team & 1)].gone = true;
  p->fuel = 20, p->gload = p->gmeter = 5, p->speed = 330;
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  fb_clear(&VF, 0xFF000000u);
  hud_view_p = NULL;
  render_view(&VF, g, p, 0, 0, VW, VH, o);
  fb_text_hook = NULL;
  int lab = -1, over = 0;
  char ex[96] = "";
  for (int i = 0; i < ntext && i < 256; i++)
    if (!strncmp(text_box[i].s, "TANKER ", 7) && strstr(text_box[i].s, "KM")) lab = i;
  for (int i = 0; lab >= 0 && i < ntext && i < 256; i++) {
    if (i == lab || !text_box[i].s[0] || text_box[i].y0 >= (int)(V->vy + V->sh)) continue;
    if (text_box[lab].x0 < text_box[i].x1 && text_box[i].x0 < text_box[lab].x1 && text_box[lab].y0 < text_box[i].y1 &&
        text_box[i].y0 < text_box[lab].y1) {
      over++;
      example(ex, sizeof ex, ": \"%s\" over it", text_box[i].s);
    }
  }
  verdict("views", "the tanker's label clear of the messages", lab >= 0 && over == 0, "label %s, %d texts over it%s",
          lab >= 0 ? "drawn" : "NOT drawn", over, ex);
  *p = keep;
  g->tankers[0] = keep_t[0], g->tankers[1] = keep_t[1];
}

/* The same jet's cockpit on a small picture (the retro renderer's 320 x 180 at a 640 x 360 window), its texts
 * only: a panel with a compact layout (the JA 46's) uses it there, and no number may be cut or run past its box
 * in that layout either. (Not on the sheet, and no light or holes measured: the scene is the one just checked.) */
static void view_small(Game *g, Plane *p, const char *what) {
  static Framebuffer SF;
  if (!fb_resize(&SF, 320, 180)) return;
  p->chase = false;
  ntext = ntext_out = ntext_cut = nnum_cut = ntext_over = 0;
  fb_text_hook = views_text;
  hud_text_cut_hook = views_cut;
  hud_text_over_hook = views_over;
  fb_clear(&SF, 0xFF000000u);
  ViewOpts o = { true, false, false };
  render_view(&SF, g, p, 0, 0, 320, 180, o);
  fb_text_hook = NULL;
  hud_text_cut_hook = NULL;
  hud_text_over_hook = NULL;
  verdict("views", what, ntext_out == 0 && nnum_cut == 0 && ntext_over == 0,
          "%d texts, %d outside the picture%s%s, %d labels cut to fit (%d numbers)%s%s, %d numbers past their box%s%s%s%s", ntext, ntext_out,
          ntext_out ? ": " : "", ntext_out ? text_out[0] : "", ntext_cut, nnum_cut, ntext_cut ? ": " : "", ntext_cut ? text_cut[0] : "",
          ntext_over, ntext_over ? ": " : "", ntext_over ? text_over[0] : "", ntext_over > 1 ? ", " : "", ntext_over > 1 ? text_over[1] : "");
}

/* The control surfaces, close: a jet 22 m ahead of the camera, seen from behind and above, with the stick at rest
 * and then fully over (pull and roll), and on realistic handling with the flaps down: every kind of surface the
 * model has is drawn turned (surfaces.c's sheet) and the picture changes at the trailing edge of at least one
 * piece of each kind. */
#define SF_MAXSEEN 48
static struct { int kind; vec3 te; float deg; } sf_seen[SF_MAXSEEN];
static int sf_nseen;
static void sf_hook(const Plane *p, int kind, int side, vec3 te, float deg) {
  (void)p, (void)side;
  if (sf_nseen < SF_MAXSEEN) sf_seen[sf_nseen].kind = kind, sf_seen[sf_nseen].te = te, sf_seen[sf_nseen++].deg = deg;
}

static void surfaces_test(Game *g, Plane *p, Plane *e, int j) {
  static uint32_t rest[VW * VH];
  Plane keep_p = *p, keep_e = *e;
  bool realistic = g->realistic;
  pose(e, 0, 2500, 0, 30, 0, 0);
  e->alive = true, e->inactive = false, e->chase = false, e->flapT = 0, e->ctl.pitch = e->ctl.roll = 0;
  vec3 at = v3_add(v3_sub(e->pos, v3_scale(e->f, 20)), v3(0, 9, 0)), d = v3_norm(v3_sub(e->pos, at));
  pose(p, at.x, at.y, at.z, atan2f(d.x, d.z) * DEG, asinf(d.y) * DEG, 0);
  p->alive = false; /* the camera only */
  Camera cam = cam_of(p);
  View V;
  int have[SF_KINDS], moved[SF_KINDS] = { 0 }, seen[SF_KINDS] = { 0 }, changed = 0;
  surfaces_count(j, have);
  for (int pass = 0; pass < 3; pass++) {
    g->realistic = pass == 2;
    e->ctl.pitch = e->ctl.roll = pass == 1 ? 1 : 0;
    e->flapT = pass == 2 ? 2 : 0;
    sf_nseen = 0;
    surfaces_hook = pass ? sf_hook : NULL;
    fb_clear(&VF, 0xFF000000u);
    render_scene(&VF, g, &cam, 0, 0, VW, VH, &V, p);
    surfaces_hook = NULL;
    if (getenv("A4C_VIEWS_ALL")) {
      char path[512];
      int n = snprintf(path, sizeof path, "%s/surfaces-%s-%d.bmp", shots_dir, JETS[j].id, pass);
      if (n < 0 || (size_t)n >= sizeof path) /* a folder name too long for the path: no picture, never a cut name */
        printf("  (no surfaces picture: the folder's name is too long)\n");
      else
        plat_save_bmp(path, VF.px, VW, VH);
    }
    if (!pass) {
      memcpy(rest, VF.px, sizeof rest);
      continue;
    }
    for (int i = 0; i < VW * VH; i++) changed += VF.px[i] != rest[i];
    for (int i = 0; i < sf_nseen; i++) {
      int k = sf_seen[i].kind;
      if (fabsf(sf_seen[i].deg) < 1 || (pass == 2) != (k == SF_FLAP)) continue;
      seen[k]++;
      vec3 c = to_cam(&cam, sf_seen[i].te);
      if (c.z < 1) continue;
      int sx = (int)lroundf(V.cx + c.x / c.z * V.F), sy = (int)lroundf(V.cy - c.y / c.z * V.F), n = 0;
      for (int y = sy - 4; y <= sy + 4; y++)
        for (int x = sx - 4; x <= sx + 4; x++)
          if (x >= 0 && y >= 0 && x < VW && y < VH) n += VF.px[y * VW + x] != rest[y * VW + x];
      moved[k] += n >= 4;
    }
  }
  g->realistic = realistic;
  char what[96], why[256] = "";
  size_t u = 0;
  bool ok = have[SF_RUDDER] > 0 && (have[SF_AILERON] + have[SF_ELEVON]) > 0 && (have[SF_ELEVATOR] + have[SF_ELEVON]) > 0;
  for (int k = 0; k < SF_KINDS; k++) {
    if (!have[k]) continue;
    ok = ok && moved[k] > 0; /* (a flap's inner end can be under the fuselage from here) */
    u += (size_t)snprintf(why + u, sizeof why - u, "%s%s %d (%d moved on screen)", u ? ", " : "", SF_NAMES[k], have[k], moved[k]);
  }
  snprintf(what, sizeof what, "%.32s control surfaces", JETS[j].id);
  keep_still(what, verdict("views", what, ok, "%s; %d px changed", u ? why : "none found", changed));
  *p = keep_p, *e = keep_e;
}

static void views_part(const char *dir) {
  shots_dir = dir;
  printf("\n== views (software renderer, %d x %d) ==\n", VW, VH);
  if (!fb_resize(&VF, VW, VH)) return;
  int nmaps = assets_map_count();
  sheet_rows = (nmaps * TOD_COUNT * WX_COUNT + nmaps * 6 + NJETS * 8 + SHEET_COLS - 1) / SHEET_COLS;
  sheet = calloc((size_t)sheet_rows * TH * SHEET_COLS * TW, sizeof *sheet);
  static const char *TODN[TOD_COUNT] = { "dawn", "day", "dusk", "night" }, *WXN[WX_COUNT] = { "clear", "scattered", "overcast", "storm" };
  Game *g = calloc(1, sizeof *g);
  if (!g) {
    verdict("memory", __func__, false, "out of memory");
    return;
  }
  /* every map in every light and weather: the sky, the sea and land, the sun */
  for (int m = 0; m < nmaps; m++) {
    const Map *M = assets_map(m);
    for (int t = 0; t < TOD_COUNT; t++)
      for (int w = 0; w < WX_COUNT; w++) {
        world_set_map(M, t, w);
        Plane *p = solo(g, 0, false, false, false, true, true);
        p->speed = 200;
        pose(p, M->bases[0].x, M->bases[0].elev + 600, M->bases[0].z - 3000, 0, -6, 0);
        char what[96];
        snprintf(what, sizeof what, "sky %s %s %s", M->id, TODN[t], WXN[w]);
        view_test(g, p, what, t, false, false);
        if (w == WX_CLEAR && t != TOD_NIGHT && LIGHT.y > 0.05f) sun_test(g, p, M->id, TODN[t]);
      }
    world_set_map(M, TOD_DAY, WX_CLEAR);
    Plane *p = solo(g, 0, false, false, false, true, true);
    p->speed = 200;
    static const float PR[4][2] = { { 0, 0 }, { 0, 30 }, { 0, -45 }, { 8, -20 } };
    (void)PR;
    light_test(g, p, enemy(g), M->id);
    static const float ALT[3] = { 300, 2000, 6000 };
    for (int t = 0; t < TOD_COUNT; t++)
      for (int w = 0; w < WX_COUNT; w++)
        for (int a = 0; a < 3 && t != TOD_NIGHT && w != WX_SCATTERED; a++) {
          world_set_map(M, t, w);
          Plane *q = solo(g, 0, false, false, false, false, false);
          sea_horizon_test(g, q, M, t, w, ALT[a], TODN[t], WXN[w]);
        }
  }
  /* the horizon on the classic arena: flat ground with no haze, so the line is sharp */
  {
    static const float PR[4][2] = { { 0, 0 }, { 0, 30 }, { 0, -45 }, { 8, -20 } };
    world_set_map(NULL, TOD_DAY, WX_CLEAR);
    Plane *p = solo(g, 0, false, true, false, false, false);
    p->speed = 200;
    for (int k = 0; k < 4; k++) horizon_test(g, p, "classic arena", PR[k][0], PR[k][1]);
  }
  /* every jet: its cockpit and chase views, the HUD in its states */
  int twin = assets_map_find("twin-isles");
  world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
  for (int j = 0; j < NJETS; j++) {
    Plane *p = solo(g, j, false, false, false, true, false);
    Plane *e = enemy(g);
    p->speed = 230;
    pose(p, 0, 2500, -2000, 72, 0, 0);
    pose(e, p->pos.x + p->f.x * 1500, 2500, p->pos.z + p->f.z * 1500, 72, 0, 0);
    e->alive = true, e->inactive = false;
    char what[96];
    snprintf(what, sizeof what, "%.32s cockpit", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    snprintf(what, sizeof what, "%.32s chase", JETS[j].id);
    view_test(g, p, what, TOD_DAY, true, false);
    p->ctl.gun = true;
    snprintf(what, sizeof what, "%.32s cockpit, guns", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    p->ctl.gun = false;
    p->locked = true, p->lockT = 99, p->lockTarget = ref_plane(e->idx);
    snprintf(what, sizeof what, "%.32s cockpit, missile lock", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    p->locked = false, p->lockTarget = ref_none();
    int keep_m = p->missiles, keep_r = p->radar;
    p->missiles = p->radar = 0; /* every air-to-air missile gone: lamps and labels in their off state */
    snprintf(what, sizeof what, "%.32s cockpit, missiles gone", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    p->missiles = keep_m, p->radar = keep_r;
    p->fuel = 12;
    snprintf(what, sizeof what, "%.32s cockpit, low fuel", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    p->fuel = 100, p->gload = 9.2f;
    snprintf(what, sizeof what, "%.32s cockpit, 9 g", JETS[j].id);
    view_test(g, p, what, TOD_DAY, false, true);
    p->gload = 1;
    /* every readout at its widest: four-digit speeds, five-digit heights, double-figure g, full stores, a dozen
     * kills, a steep climb: no number may run past its display */
    {
      Plane keep = *p;
      p->speed = 760, p->pos.y = 15200, p->gload = p->gmeter = 12.5f, p->vel.y = 299;
      p->ammo = MAX_AMMO, p->flares = MAX_FLR, p->chaff = MAX_CHAFF, p->kills = 12, p->tkills = 6; /* a jet's own panel: modern stores */
      snprintf(what, sizeof what, "%.32s cockpit, widest readouts", JETS[j].id);
      view_test(g, p, what, TOD_DAY, false, false);
      snprintf(what, sizeof what, "%.32s cockpit, widest readouts, 320 x 180", JETS[j].id);
      if (!strcmp(JETS[j].id, "ja46")) view_small(g, p, what); /* the one panel with a compact layout */
      *p = keep;
    }
    surfaces_test(g, p, e, j);
    if (j == 0) tanker_label_test(g, p), lock_message_test(g, p, e), tanker_message_test(g, p), tanker_range_test(g, p, e), range_edge_test(g, p, e), hint_haze_test(g, p), over_page_test(g);
  }
  free(g);
  if (sheet) {
    char path[512];
    snprintf(path, sizeof path, "%s/views.png", dir);
    bool ok = write_png(path, sheet, SHEET_COLS * TW, sheet_rows * TH);
    printf("  contact sheet: %s (%d stills)%s\n", path, nsheet, ok ? "" : " - COULD NOT BE WRITTEN");
    free(sheet);
    sheet = NULL;
  }
  fb_free(&VF);
}

int audit(int argc, char **argv) {
  const char *part = NULL, *only = NULL, *json = NULL, *shots = ".";
  bool gpu = false;
  int n = 50;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--part") && i + 1 < argc) part = argv[++i];
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--sorties") && i + 1 < argc) n = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
    else if (!strcmp(argv[i], "--json") && i + 1 < argc) json = argv[++i];
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i], shots_given = true; /* where views.png goes */
    else if (!strcmp(argv[i], "--gpu")) gpu = true; /* --part views on the GPU renderer, in a window */
    else if (!strcmp(argv[i], "--from") && i + 1 < argc) first_sortie = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
    else if (!strcmp(argv[i], "--me-load") && i + 1 < argc)
      sscanf(argv[++i], "%d,%d,%d", &me_load[0], &me_load[1], &me_load[2]); /* sorties: the jet under test's loadout */
    else if (!strcmp(argv[i], "--same-jet")) same_jet = true;
    else if (!strcmp(argv[i], "--diff") && i + 1 < argc) only_diff = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX); /* sorties: one CPU difficulty */
    else if (!strcmp(argv[i], "--fired")) show_fired = true;
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed_base = (uint32_t)strtoul(argv[++i], NULL, 10);
    /* the readable log into a file, on its own (the game's log lines stay out of it): Windows has no console */
    else if (!strcmp(argv[i], "--out") && i + 1 < argc && !freopen(argv[++i], "w", stdout)) return 2;
  }
  if (json) {
    J = fopen(json, "w");
    if (J) fputc('[', J);
    jfirst = true;
  }
  int twin = assets_map_find("twin-isles");
  world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
  printf("audit: %d jets and the drone, %d maps\n", NJETS, assets_map_count());
  if (!part || !strcmp(part, "spec")) {
    spec_part(only);
    if (!only || !strcmp(only, "dr7")) drone_part();
  }
  if (!part || !strcmp(part, "features")) {
    printf("\n== systems ==\n");
    lock_tests();
    weapon_tests();
    autopilot_tests();
    gforce_tests();
    start_tests();
  }
  if (!part || !strcmp(part, "sorties")) sorties_part(n, only);
  if (part && !strcmp(part, "views")) {
    if (gpu) gpu_views_part(shots);
    else views_part(shots);
  }
  printf("\n%d failed check%s\n", fails, fails == 1 ? "" : "s");
  if (J) {
    fputs("\n]\n", J);
    fclose(J);
  }
  return fails ? 1 : 0;
}
