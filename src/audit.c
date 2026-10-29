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
 *        [--json FILE]. Returns non-zero when a check failed. */
#include "audit.h"

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

#define DT (1.0f / 60)
#define KT 1.944f
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
static bool verdict(const char *area, const char *name, bool ok, const char *fmt, ...) {
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
  return NULL;
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
  Game *g = calloc(1, sizeof *g);
  const JetType *jt = jet_type(jet, realistic);
  memset(o, 0, sizeof *o);
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
           s ? s->corner / 1.852f : 0, s ? (float)s->roll : 0, s ? s->ceiling * 3.281f : 0, s ? s->g_max : 0, s ? s->g_min : 0,
           s ? s->range : 0);
    for (int r = 0; r < 2; r++) {
      const Perf *o = r ? q : a;
      printf("       %-10s game:  %4.0f kt low / %4.0f kt at 6 km, climb %3.0f m/s at %3.0f kt, turn %4.1f sus (%3.0f kt, %.1f g) / "
             "%4.1f inst at %3.0f kt (flown %4.1f), roll %3.0f, stall %3.0f kt, peak %.1f g, fuel %4.0f s full / %4.0f s cruise, "
             "energy ceiling %5.0f ft (cap %5.0f ft)\n",
             r ? "realistic" : "arcade", o->vmax_lo * KT, o->vmax_hi * KT, o->roc, o->roc_v * KT, o->sus, o->sus_v, o->sus_g, o->inst,
             o->inst_v, o->flown, o->roll, o->stall, o->gpeak, o->fuel_full, o->fuel_cruise, o->ceiling * 3.281f, 7500 * 3.281f);
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
      jnum("ceiling_ft", o->ceiling * 3.281f);
      jnum("g_max", JETS[j].g_max);
      jnum("corner_kt", jet_corner(&JETS[j]) * KT);
      if (s) {
        jnum("s_vmax_kt", s->vmax_sl / 1.852f);
        jnum("s_mach", s->mach_alt);
        jnum("s_climb", s->climb);
        jnum("s_sus", s->turn_sus);
        jnum("s_inst", s->turn_inst);
        jnum("s_corner_kt", s->corner / 1.852f);
        jnum("s_roll", s->roll);
        jnum("s_ceiling_ft", s->ceiling * 3.281f);
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
  p->lives = 99;
  p->is_ai = false;
  place(p, v3(0, 3000, -4000), 0, 160);
  p->throttle = 1;
  return p;
}

static void drone_part(void) {
  printf("\n== the DR-7 Vesper drone ==\n");
  Game *g = calloc(1, sizeof *g);
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
    verdict("radar lock", C[k].what, ok, tlock > 0 ? "locked after %.2f s" : "no lock%s", tlock > 0 ? tlock : 0, "");
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
  /* the tanker: hold station behind the drogue */
  {
    int twin = assets_map_find("twin-isles");
    world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
    Plane *p = solo(g, 0, false, false, false, false, false);
    const Tanker *T = &g->tanker;
    p->fuel = 40;
    float f0 = p->fuel;
    for (int i = 0; i < 60 * 5; i++) {
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
    verdict("starts", "air-to-air refuelling", p->fuel > f0 + 50 && p->alive, "5 s on the drogue: fuel %.0f%% to %.0f%%", f0, p->fuel);
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
typedef struct {
  int   sorties, done, draws, wins, losses, faults, timeouts;
  int   shots, msl, flares, locks, glocs, stalls, kills, deaths, crashes, shotdown, collisions, takeoffs, refuels, stunts[4];
  float minutes;
  char  fault[6][200];
  int   nfault;
} Tally;

static const char *const MODE_NAMES[] = { "demo", "1 player", "2 players", "carrier defence", "beat the drone" };

static void fault(Tally *t, const char *fmt, ...) {
  t->faults++;
  if (t->nfault >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(t->fault[t->nfault++], sizeof t->fault[0], fmt, ap);
  va_end(ap);
}

/* One match with CPU pilots in every seat; plane `jet` (DRONE_JET for the
 * drone) is flown by seat 0, or the drone's seat. */
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
  Difficulty diff = (Difficulty)(k % 3);
  int nm = assets_map_count();
  int map = k % (nm + 1) - 1;
  const Map *mp = map < 0 ? NULL : assets_map(map);
  if (mode == MODE_DEFEND && (!mp || !mp->ncarriers)) mp = assets_map(assets_map_find("twin-isles")), map = assets_map_find("twin-isles");
  world_set_map(mp, k % TOD_COUNT, mp ? mp->default_weather : WX_CLEAR);
  int jets[2] = { drone ? (int)(seed % NJETS) : jet, drone ? (int)(seed / 7 % NJETS) : (jet + 1 + k) % NJETS };
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
  const JetType *mj = &JETS[me->jet];
  int ticks = 0, limit = 60 * 60 * 45; /* test_core allows 40 minutes for a match of aces */
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
  for (; ticks < limit && !(g->over && g->overT <= 0); ticks++) {
    float a0 = me->ammo;
    int m0 = me->missiles + me->radar + me->ground, f0 = me->flares + me->chaff;
    bool alive0 = me->alive;
    sim_update(g, DT);
    g->nsounds = 0;
    if (me->alive && alive0) {
      if (me->ammo < a0) t->shots += (int)(a0 - me->ammo);
      int m1 = me->missiles + me->radar + me->ground, f1 = me->flares + me->chaff;
      if (m1 < m0) t->msl += m0 - m1;
      if (f1 < f0) t->flares += f0 - f1;
    }
    if (me->lastStunt != prevStunt && me->lastStunt) t->stunts[me->lastStunt]++;
    prevStunt = me->lastStunt;
    for (int i = 0; i < g->nplanes; i++) {
      Plane *p = &g->planes[i];
      const JetType *jt = &JETS[p->jet];
      if (p == me) {
        if (p->locked && !prevLock[i]) t->locks++;
        if (p->gloc > 0 && !prevGloc[i]) t->glocs++;
        if (p->stall && !prevStall[i]) t->stalls++;
        if (prevTaxi[i] && !p->taxi && p->alive) t->takeoffs++;
        if (p->refuel && !prevRef[i]) t->refuels++;
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
  if (!g->over) {
    t->timeouts++;
    printf("    timeout after 45 min: %s\n", cfg);
  } else {
    t->done++;
    if (g->winner < 0) t->draws++;
    else if (g->winner == me->team) t->wins++;
    else t->losses++;
  }
  if (getenv("A4C_AUDIT_VERBOSE") || t->faults > faults0)
    printf("    %2d %-6s %-62s %s in %5.1f min, %s; kills %d, deaths %d\n", k, mj->id, cfg, g->over ? "over" : "TIMEOUT", ticks / 3600.0f,
           g->winner < 0 ? "draw" : g->winner == me->team ? "won" : "lost", me->kills, me->lives < 0 ? 0 : (g->lives > 0 ? g->lives : LIVES) - me->lives);
}

static int first_sortie;    /* --from K: start at sortie K (to replay one) */
static uint32_t seed_base = 1000; /* --seed S: another set of matches */

static void sorties_part(int n, const char *only) {
  printf("\n== whole matches, %d per plane ==\n", n);
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
}

int audit(int argc, char **argv) {
  const char *part = NULL, *only = NULL, *json = NULL;
  int n = 50;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--part") && i + 1 < argc) part = argv[++i];
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--sorties") && i + 1 < argc) n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--json") && i + 1 < argc) json = argv[++i];
    else if (!strcmp(argv[i], "--from") && i + 1 < argc) first_sortie = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed_base = (uint32_t)strtoul(argv[++i], NULL, 10);
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
  printf("\n%d failed check%s\n", fails, fails == 1 ? "" : "s");
  if (J) {
    fputs("\n]\n", J);
    fclose(J);
  }
  return fails ? 1 : 0;
}
