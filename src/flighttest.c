/* flighttest.c - a test bench for the flight model (--flight-test). Each
 * jet flies a set of manoeuvres with simple autopilots, with no enemy in
 * the sky, and prints what it measured: top speed, climb, sustained turn,
 * roll rate, stall, ceiling, endurance, take-off, carrier launch and
 * landing. Every step is also checked for broken physics: NaNs, axes that
 * are no longer square, a live jet under the ground. */
#include "flighttest.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/game.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"

#define DT (1.0f / 60)
#define KT 1.944f

static int problems;

static void check(const Game *g, const Plane *p, const char *test) {
  static char last[64];
  const char *bad = NULL;
  float fl = v3_len(p->f), ul = v3_len(p->u), rl = v3_len(p->r);
  if (!(p->pos.x == p->pos.x && p->pos.y == p->pos.y && p->pos.z == p->pos.z && p->speed == p->speed)) bad = "NaN position or speed";
  else if (fabsf(fl - 1) > 0.01f || fabsf(ul - 1) > 0.01f || fabsf(rl - 1) > 0.01f) bad = "axes not unit length";
  else if (fabsf(v3_dot(p->f, p->u)) > 0.01f || fabsf(v3_dot(p->f, p->r)) > 0.01f) bad = "axes not square";
  else if (p->alive && !p->taxi && p->pos.y < terrain_h(p->pos.x, p->pos.z) - 1) bad = "alive under the ground";
  else if (p->speed < 0 || p->speed > 600) bad = "speed out of range";
  if (bad && strcmp(last, test) != 0) {
    snprintf(last, sizeof last, "%s", test);
    printf("  !! %s: %s at t=%.1f (y %.0f, speed %.0f)\n", test, bad, g->time, p->pos.y, p->speed);
    problems++;
  }
}

/* A match with only our jet in it: the enemies sit out, no targets. */
static bool realistic;

static Plane *setup(Game *g, int jet, bool classic, bool ground) {
  int jets[2] = { jet, jet };
  GameRules R = { 0 };
  R.classic = classic;
  R.realistic = realistic;
  R.ground_start = ground;
  R.load[0][0] = R.load[1][0] = -1;
  game_new_rules(g, MODE_1P, DIFF_NORMAL, false, jets, 1, &R);
  for (int i = 0; i < g->nplanes; i++)
    if (g->planes[i].hidx < 0) {
      g->planes[i].inactive = true;
      g->planes[i].alive = false;
      g->planes[i].lives = 99;
    }
  g->ntargets = 0;
  Plane *p = &g->planes[g->locals[0]];
  p->lives = 99;
  p->invuln = 0;
  return p;
}

static void place(Plane *p, float alt, float speed) {
  p->pos = v3(0, alt, -4000);
  p->f = v3(0, 0, 1);
  p->u = v3(0, 1, 0);
  p->r = v3(1, 0, 0);
  p->speed = speed;
  p->vel = v3_scale(p->f, speed);
  p->taxi = p->oncat = false;
  p->fuel = 100;
}

static float bank_of(const Plane *p) { return atan2f(-p->r.y, p->u.y); }
static float hdg_of(const Plane *p) { return atan2f(p->f.x, p->f.z); }

/* stick to hold a pitch angle (radians) and a bank */
static void hold(Plane *p, float pitch, float bank) {
  float pe = pitch - asinf(clampf(p->f.y, -1, 1));
  float be = bank - bank_of(p);
  p->ctl.pitch = clampf(pe * 4 + fabsf(bank_of(p)) * 0.35f, -1, 1);
  p->ctl.roll = clampf(be * 3, -1, 1);
}

static void step(Game *g, Plane *p, const char *test) {
  p->ctl.gun = p->ctl.msl = p->ctl.flr = p->ctl.fire = p->ctl.sel = p->ctl.map = p->ctl.ap = false;
  sim_update(g, DT);
  g->nsounds = 0;
  check(g, p, test);
}

static void fly(const char *name, int jet, bool classic) {
  Game *g = calloc(1, sizeof *g);
  Plane *p;
  char line[512];
  int n = 0;
  /* top speed, level at 3000 m */
  p = setup(g, jet, classic, false);
  place(p, 3000, 150);
  p->throttle = 1;
  float vmax = 0, t90 = -1;
  for (int i = 0; i < 60 * 60; i++) {
    hold(p, 0, 0);
    step(g, p, "top speed");
    vmax = fmaxf(vmax, p->speed);
  }
  p = setup(g, jet, classic, false);
  place(p, 3000, 150);
  p->throttle = 1;
  for (int i = 0; i < 60 * 60 && t90 < 0; i++) {
    hold(p, 0, 0);
    step(g, p, "acceleration");
    if (p->speed >= 0.9f * vmax) t90 = g->time;
  }
  /* climb at 30 degrees, full power, from 300 m/s */
  p = setup(g, jet, classic, false);
  place(p, 1000, 300);
  p->throttle = 1;
  float y0 = p->pos.y;
  for (int i = 0; i < 20 * 60; i++) {
    hold(p, 30 * PI_F / 180, 0);
    step(g, p, "climb");
  }
  float climb = (p->pos.y - y0) / 20, vclimb = p->speed;
  /* ceiling: climb at 20 degrees for 4 minutes */
  float ymax = 0;
  for (int i = 0; i < 240 * 60; i++) {
    hold(p, 20 * PI_F / 180, 0);
    step(g, p, "ceiling");
    ymax = fmaxf(ymax, p->pos.y);
  }
  /* sustained turn: 75 degrees of bank and pulling, holding height */
  p = setup(g, jet, classic, false);
  place(p, 3000, 250);
  p->throttle = 1;
  float h0 = 0, gmax = 0, hy0 = 0;
  for (int i = 0; i < 25 * 60; i++) {
    float bank = 75 * PI_F / 180;
    p->ctl.roll = clampf((bank - bank_of(p)) * 3, -1, 1);
    p->ctl.pitch = clampf(0.9f + (3000 - p->pos.y) * 0.004f, -1, 1);
    if (i == 10 * 60) {
      h0 = hdg_of(p);
      hy0 = p->pos.y;
    }
    float before = hdg_of(p);
    step(g, p, "turn");
    (void)before;

  }
  float dh = hdg_of(p) - h0;
  /* count whole turns: heading unwraps over 15 s */
  float turn = 0;
  {
    Game *g2 = calloc(1, sizeof *g2);
    Plane *q = setup(g2, jet, classic, false);
    place(q, 3000, 250);
    q->throttle = 1;
    float prev = hdg_of(q), acc = 0;
    for (int i = 0; i < 8 * 60; i++) {
      /* a hard break: 80 degrees of bank, the stick fully back */
      q->ctl.roll = clampf((80 * PI_F / 180 - bank_of(q)) * 3, -1, 1);
      q->ctl.pitch = 1;
      step(g2, q, "turn rate");
      gmax = fmaxf(gmax, q->gload);
      float h = hdg_of(q), d = h - prev;
      while (d > PI_F) d -= 2 * PI_F;
      while (d < -PI_F) d += 2 * PI_F;
      if (i >= 3 * 60) acc += d;
      prev = h;
    }
    turn = fabsf(acc) / 5 * 180 / PI_F;
    free(g2);
  }
  (void)dh;
  (void)hy0;
  /* roll rate: full aileron for one second */
  p = setup(g, jet, classic, false);
  place(p, 3000, 250);
  p->throttle = 0.7f;
  float racc = 0, pb = bank_of(p);
  for (int i = 0; i < 60; i++) {
    p->ctl.roll = 1;
    p->ctl.pitch = 0;
    step(g, p, "roll");
    float b = bank_of(p), d = b - pb;
    while (d > PI_F) d -= 2 * PI_F;
    while (d < -PI_F) d += 2 * PI_F;
    racc += d;
    pb = b;
  }
  /* stall: idle, holding height, until the stall warning */
  p = setup(g, jet, classic, false);
  place(p, 3000, 200);
  p->throttle = 0;
  float tstall = -1, vstall = 0;
  for (int i = 0; i < 120 * 60 && tstall < 0; i++) {
    p->ctl.roll = clampf(-bank_of(p) * 3, -1, 1);
    p->ctl.pitch = clampf((3000 - p->pos.y) * 0.01f - p->f.y * 3, -1, 1);
    step(g, p, "stall");
    if (p->stall) {
      tstall = g->time;
      vstall = p->speed;
    }
  }
  /* and the recovery: full power, nose down 20 degrees */
  float lost = p->pos.y, trec = -1;
  p->throttle = 1;
  for (int i = 0; i < 30 * 60 && trec < 0; i++) {
    hold(p, -20 * PI_F / 180, 0);
    step(g, p, "stall recovery");
    lost = fminf(lost, p->pos.y);
    if (!p->stall && p->speed > 150) trec = g->time - tstall;
  }
  float hlost = 3000 - lost;
  /* endurance at full power */
  p = setup(g, jet, classic, false);
  place(p, 3000, 300);
  p->throttle = 1;
  float tfuel = -1;
  for (int i = 0; i < 900 * 60 && tfuel < 0; i++) {
    hold(p, 0, 0);
    step(g, p, "endurance");
    if (p->fuel <= 0) tfuel = g->time;
  }
  n += snprintf(line + n, sizeof line - n, "%-6s %-7s top %4.0f kt (90%% in %4.1f s)  climb %5.1f m/s (at %3.0f kt)  ceiling %5.0f ft  "
                "turn %4.1f deg/s at %3.1f g  roll %3.0f deg/s  stall %3.0f kt after %4.1f s, recovers in %4.1f s losing %4.0f m  "
                "fuel %4.0f s", name, classic ? "classic" : "modern", vmax * KT, t90, climb, vclimb * KT, ymax * 3.281f, turn, gmax,
                fabsf(racc) * 180 / PI_F, vstall * KT, tstall, trec, hlost, tfuel);
  printf("%s\n", line);
  free(g);
}

/* Energy and turning, as the fighter manoeuvre books describe them: the
 * turn rate across the speeds (it should peak at corner speed), a zoom climb
 * (speed traded for height, at best v^2 / 2g), a level turn with the stick
 * fully back (it should bleed down toward the speed it can hold), and an
 * unloaded dive (no g, the quickest way to gain speed). */
static void energy_tests(int jet, bool classic) {
  Game *g = calloc(1, sizeof *g);
  const JetType *jt = &JETS[jet];
  char line[400];
  int n = snprintf(line, sizeof line, "  turn deg/s by speed:");
  static const float KTS[] = { 150, 250, 350, 450, 550, 650 };
  float best = 0, bestkt = 0;
  for (int k = 0; k < 6; k++) {
    Plane *p = setup(g, jet, classic, false);
    place(p, 3000, KTS[k] / KT);
    p->throttle = 1;
    /* roll to 90 degrees, then one second of full pull at that speed */
    float prev = 0, acc = 0;
    for (int i = 0; i < 90; i++) {
      p->ctl.roll = clampf((90 * PI_F / 180 - bank_of(p)) * 3, -1, 1);
      p->ctl.pitch = i >= 30 ? 1 : 0;
      p->speed = KTS[k] / KT; /* held: the rate at this speed */
      if (i == 30) prev = hdg_of(p);
      step(g, p, "turn by speed");
      if (i >= 30) {
        float h = hdg_of(p), d = h - prev;
        while (d > PI_F) d -= 2 * PI_F;
        while (d < -PI_F) d += 2 * PI_F;
        acc += d;
        prev = h;
      }
    }
    float r = fabsf(acc) * 180 / PI_F;
    if (r > best) { best = r; bestkt = KTS[k]; }
    n += snprintf(line + n, sizeof line - n, " %.0f kt %.1f", KTS[k], r);
  }
  printf("%s; best at %.0f kt, corner speed %.0f kt\n", line, bestkt, jet_corner(jt) * KT);
  /* zoom: level at 300 m/s, engine at idle, pull up and climb until 120 m/s:
   * height for speed, a little short of the ideal for the drag */
  Plane *p = setup(g, jet, classic, false);
  place(p, 1000, 300);
  p->throttle = 0;
  float y0 = p->pos.y, v0 = p->speed;
  for (int i = 0; i < 60 * 60 && p->speed > 120; i++) {
    hold(p, 60 * PI_F / 180, 0);
    step(g, p, "zoom");
  }
  float gained = p->pos.y - y0, ideal = (v0 * v0 - p->speed * p->speed) / (2 * 9.81f);
  /* level turn, stick fully back, full power, for 20 s from 450 kt */
  p = setup(g, jet, classic, false);
  place(p, 3000, 450 / KT);
  p->throttle = 1;
  float g5 = 0;
  for (int i = 0; i < 20 * 60; i++) {
    float want = acosf(clampf(1 / fmaxf(p->gload, 1.01f), -1, 1)); /* the bank that keeps the lift up */
    p->ctl.roll = clampf((want - bank_of(p)) * 3, -1, 1);
    p->ctl.pitch = clampf(1 + (3000 - p->pos.y) * 0.003f, -1, 1);
    step(g, p, "sustained turn");
    if (i > 15 * 60) g5 = p->gload;
  }
  float vsus = p->speed;
  /* unloaded dive: 30 degrees down, no g, full power, 10 s from 250 kt */
  p = setup(g, jet, classic, false);
  place(p, 6000, 250 / KT);
  p->throttle = 1;
  for (int i = 0; i < 10 * 60; i++) {
    hold(p, -30 * PI_F / 180, 0);
    step(g, p, "unloaded dive");
  }
  printf("  idle zoom from 583 kt: %.0f m (ideal %.0f m); full-stick level turn settles at %.0f kt, %.1f g; "
         "30 degree dive: 250 to %.0f kt in 10 s\n", gained, ideal, vsus * KT, g5, p->speed * KT);
  free(g);
}

static void ground_tests(int jet, bool classic) {
  Game *g = calloc(1, sizeof *g);
  /* runway: team 1 starts on its runway; take the player's seat over to it */
  Plane *p = setup(g, jet, classic, true);
  p->team = 1;
  game_spawn(g, p);
  vec3 start = p->pos;
  p->throttle = 1;
  float tlift = -1, dlift = 0;
  for (int i = 0; i < 60 * 60 && tlift < 0; i++) {
    p->ctl.pitch = p->speed > (classic ? 82 : 94) ? 0.6f : 0;
    p->ctl.roll = 0;
    step(g, p, "take-off");
    if (!p->taxi) {
      tlift = g->time;
      dlift = v3_dist(start, p->pos);
    }
  }
  float ymin = 1e9f;
  for (int i = 0; i < 10 * 60; i++) {
    hold(p, 12 * PI_F / 180, 0);
    step(g, p, "climb-out");
    ymin = fminf(ymin, p->pos.y - terrain_h(p->pos.x, p->pos.z));
  }
  printf("  runway: airborne after %.1f s and %.0f m, %s; lowest point in climb-out %.0f m above ground\n", tlift, dlift,
         p->alive ? "alive" : "CRASHED", ymin);
  /* carrier: team 0 on the catapult */
  p = setup(g, jet, classic, true);
  float deck = p->pos.y;
  bool oncat = p->oncat;
  p->throttle = 1;
  float tl = -1, low = 1e9f;
  for (int i = 0; i < 30 * 60; i++) {
    if (!p->taxi) hold(p, 8 * PI_F / 180, 0);
    step(g, p, "catapult");
    if (!p->taxi && tl < 0) tl = g->time;
    if (!p->taxi) low = fminf(low, p->pos.y);
  }
  printf("  carrier: %s, airborne after %.1f s, lowest %.0f m (deck %.0f m), %s\n", oncat ? "on the catapult" : "no carrier: runway",
         tl, low, deck, p->alive ? "alive" : "CRASHED");
  /* landing: a 3 degree approach to team 0's runway at low power, in the
   * classic arena (the maps' runways sit among hills, a test for pilots) */
  const Map *map = MAP;
  int tod = MAP_TOD, wx = MAP_WX;
  world_set_map(NULL, TOD_DAY, WX_CLEAR);
  p = setup(g, jet, classic, false);
  const Base *b = &BASES[0];
  float hy = terrain_h(b->x, b->z);
  /* from whichever end has open ground under a 1200 m final */
  float d = b->dir, worst[2] = { 0, 0 };
  for (int s = 0; s < 2; s++)
    for (float k = 0; k <= 1200; k += 50) {
      float dd = s ? -b->dir : b->dir;
      worst[s] = fmaxf(worst[s], terrain_h(b->x, b->z - dd * (1000 + k)) - (hy + tanf(3 * PI_F / 180) * k));
    }
  if (worst[1] < worst[0]) d = -b->dir;
  p->pos = v3(b->x, hy + 3 + tanf(3 * PI_F / 180) * 1200, b->z - d * (1000 + 1200));
  p->f = v3(0, 0, d);
  p->u = v3(0, 1, 0);
  p->r = v3_cross(p->u, p->f);
  p->speed = classic ? 85 : 100;
  p->throttle = 0.15f;
  bool touched = false;
  float troll = -1, dstop = 0;
  vec3 td = p->pos;
  float vtd = 0, sink = 0;
  for (int i = 0; i < 120 * 60 && troll < 0; i++) {
    float dist = (p->pos.z - (b->z - d * 1000)) * d; /* past the threshold */
    float want = hy + 3 + tanf(3 * PI_F / 180) * fmaxf(0, -dist) + (dist > 0 ? -2 : 0);
    if (!touched) {
      float pitch = clampf((want - p->pos.y) * 0.02f - 0.05f, -0.12f, 0.12f);
      hold(p, pitch, 0);
      p->throttle = p->speed > (classic ? 88 : 104) ? 0.05f : 0.35f;
    } else {
      p->ctl.pitch = p->ctl.roll = 0;
      p->throttle = 0;
    }
    step(g, p, "landing");
    if (!p->alive) break;
    if (p->onGround && !touched) {
      touched = true;
      td = p->pos;
      vtd = p->speed;
      sink = p->vel.y;
    }
    if (touched && p->speed < 1) {
      troll = g->time;
      dstop = v3_dist(td, p->pos);
    }
  }
  printf("  landing: %s at %.0f kt sinking %.1f m/s, %s (%s), rolled %.0f m to a stop; refuelling %s\n",
         touched ? "touched down" : "never touched down", vtd * KT, sink, p->alive ? "alive" : "CRASHED", p->alive ? "" : p->deathMsg,
         dstop, p->refuel ? "yes" : "no");
  /* and off again from a standstill on the runway */
  float tgo = -1;
  for (int i = 0; i < 60 * 60 && p->alive && tgo < 0; i++) {
    p->throttle = 1;
    p->ctl.pitch = p->speed > (classic ? 82 : 94) ? 0.6f : 0;
    p->ctl.roll = 0;
    float t0 = g->time;
    step(g, p, "second take-off");
    (void)t0;
    if (!p->onGround && p->pos.y > terrain_h(p->pos.x, p->pos.z) + 30) tgo = g->time - troll;
  }
  printf("  take-off again from a stop: %s\n", tgo > 0 ? "yes" : "NO");
  if (tgo < 0)
    printf("    (alive %d, ground %d, speed %.0f kt, %.0f m up, stall %d)\n", p->alive, p->onGround, p->speed * KT,
           p->pos.y - terrain_h(p->pos.x, p->pos.z), p->stall);
  if (tgo < 0) problems++;
  world_set_map(map, tod, wx);
  free(g);
}

int flight_test(void) {
  int twin = assets_map_find("twin-isles");
  world_set_map(assets_map(twin), TOD_DAY, WX_CLEAR);
  printf("flight test on %s, %d jets\n", twin >= 0 ? "twin isles" : "the classic arena", NJETS);
  for (int c = 0; c < 3; c++)
    for (int j = 0; j < NJETS; j++) {
      realistic = c == 2;
      if (c == 2 && j == 0) printf("realistic handling:\n");
      fly(JETS[j].id, j, c == 1);
      energy_tests(j, c == 1);
      ground_tests(j, c == 1);
    }
  printf("%d physics problems\n", problems);
  return problems ? 1 : 0;
}
