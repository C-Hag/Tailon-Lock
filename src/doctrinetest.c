/* doctrinetest.c - measures behind the CPU pilots' doctrine (--doctrine-measure).
 *
 * The doctrine (core/ai.c) leans on what the simulation gives a pilot; these
 * are those numbers, taken from the simulation as it is, nothing changed:
 *
 *   recovery   the speed back after the drone's Cobra, and after a nose-high
 *              slow-down in the jets (the only jet that flies past the stall
 *              is the drone: the others' flight model has no post-stall)
 *   lock       how far the radar locks each jet (does the stealth score
 *              change it?) and the lock cone
 *   missiles   each missile against no defence, decoys, a turn onto the beam,
 *              the beam with decoys, and the last-ditch break
 *   fuel       how long each jet's tank lasts at full power in a turning
 *              fight, and at cruise
 *
 * Two jets in an empty sky, flown by scripts through the CPU's own control
 * path (sim_hands), so the decoy key drops what a CPU pilot's would. */
#include "doctrinetest.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "units.h"
#include "core/game.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"

#define DT (1.0f / 60)
#define KT KT_PER_MPS

/* ---- two jets in an empty sky ---------------------------------------------- */
static Game G;

static void setup(int jetA, int jetB, bool real) {
  int jets[2] = { jetA, jetB };
  GameRules R = { 0 };
  R.realistic = real;
  R.load[0][0] = R.load[1][0] = -1;
  game_new_rules(&G, MODE_2P, DIFF_HARD, false, jets, 1, &R);
  G.ntargets = 0;
  for (int i = 0; i < G.nplanes; i++) {
    Plane *p = &G.planes[i];
    p->lives = 99;
    p->invuln = 0;
    p->is_ai = true; /* the CPU's control path: the scripts below write the stick after it */
    p->ai.diff = DIFF_HARD;
  }
}

static void place(Plane *p, vec3 pos, vec3 f, float speed) {
  p->pos = pos;
  p->f = v3_norm(f);
  p->r = v3_norm(v3_cross(v3(0, 1, 0), p->f));
  p->u = v3_cross(p->f, p->r);
  p->speed = speed;
  p->vel = v3_scale(p->f, speed);
  p->taxi = p->oncat = p->onGround = false;
  p->fuel = 100;
  p->alive = true;
  p->health = 100;
  p->throttle = 1;
}

static float bank_of(const Plane *p) { return atan2f(-p->r.y, p->u.y); }

/* the stick toward a direction, as the CPU steers: roll the lift onto it, pull (hard: all the way) */
static void steer(Plane *p, vec3 dir, bool hard) {
  vec3 d = v3_norm(dir);
  float lx = v3_dot(d, p->r), ly = v3_dot(d, p->u), lz = v3_dot(d, p->f);
  float off = acosf(clampf(lz, -1, 1)), rollErr = atan2f(lx, ly);
  float roll = off < 0.12f ? -bank_of(p) * 1.5f + lx * 6 : rollErr * 2.2f;
  float pitch = (fabsf(rollErr) < 0.9f || off < 0.12f) ? clampf(atan2f(ly, lz) * 3, -0.5f, 1) : 0.2f;
  if (hard && off > 0.2f) pitch = fabsf(rollErr) < 0.6f ? 1 : 0.6f;
  p->ctl.pitch = clampf(pitch, -1, 1);
  p->ctl.roll = clampf(roll, -1, 1);
}

/* a pitch angle and wings level */
static void hold_pitch(Plane *p, float pitch) {
  float pe = pitch - asinf(clampf(p->f.y, -1, 1));
  p->ctl.pitch = clampf(pe * 4, -1, 1);
  p->ctl.roll = clampf(-bank_of(p) * 3, -1, 1);
}

typedef void (*Script)(Plane *p, int who);
static Script script;
static float thr[2];
static void hands(Game *g, Plane *p, float dt, bool after) {
  (void)g, (void)dt;
  if (!after || !script || p->idx > 1) return;
  p->ctl.gun = p->ctl.msl = p->ctl.flr = p->ctl.vec = false;
  script(p, p->idx);
  p->throttle = thr[p->idx];
}

static void step(void) {
  sim_update(&G, DT);
  G.nsounds = 0;
}

/* ---- recovery after a post-stall move or a nose-high slow-down ----------- */
static int phase;
static float phaseT;
static void s_recover(Plane *p, int who) {
  if (who) { /* the other jet: out of the way, far off */
    p->pos = v3(0, 6000, 9000);
    return;
  }
  const JetType *jt = jet_type(p->jet, G.realistic);
  phaseT += DT;
  if (phase == 0) { /* the move */
    if (p->drone) { /* the Cobra as the CPU flies it: the nose held near 90 degrees for 1.4 s, then let fall */
      p->ctl.vec = phaseT < 1.4f;
      p->ctl.pitch = phaseT < 1.4f ? clampf((1.55f - p->aoa) * 4, -1, 1) : 0;
      p->ctl.roll = 0;
      thr[0] = 1;
      if (phaseT > 2.0f) phase = 1, phaseT = 0;
    } else { /* a nose-high slow-down: pull to 70 degrees at idle until at half the corner speed */
      hold_pitch(p, 70 * PI_F / 180);
      thr[0] = 0;
      if (p->speed < 0.5f * jet_corner(jt) || phaseT > 10) phase = 1, phaseT = 0;
    }
  } else { /* the unload: nose 10 degrees below the horizon, full power */
    hold_pitch(p, -10 * PI_F / 180);
    thr[0] = 1;
  }
}

static void recovery(int jet, bool real) {
  setup(jet, jet, real);
  Plane *p = &G.planes[0];
  p->drone = jet == DRONE_JET; /* its thrust vectoring (sim.c fly_drone) */
  const JetType *jt = jet_type(jet, real);
  float vc = jet_corner(jt);
  place(p, v3(0, 4000, -4000), v3(0, 0, 1), vc);
  place(&G.planes[1], v3(0, 6000, 9000), v3(0, 0, 1), 200);
  script = s_recover;
  phase = 0;
  phaseT = 0;
  float minV = 1e9f, y0 = p->pos.y, t150 = -1, tvc = -1, tUnload = 0;
  for (int i = 0; i < 90 * 60; i++) {
    step();
    if (!p->alive) break;
    minV = fminf(minV, p->speed);
    if (phase == 1 && tUnload == 0) tUnload = G.time;
    if (phase == 1 && t150 < 0 && p->speed >= 150) t150 = G.time - tUnload;
    if (phase == 1 && tvc < 0 && p->speed >= 0.95f * vc) {
      tvc = G.time - tUnload;
      break;
    }
  }
  printf("| %s (%s) | %s | %s | %.0f | %.0f | %.1f | %.1f | %.0f |\n", JETS[jet].name, JETS[jet].id, real ? "realistic" : "arcade",
         p->drone ? "Cobra" : "nose-high slow-down", vc * KT, minV * KT, t150, tvc, p->pos.y - y0);
  script = NULL;
}

/* ---- the radar lock: reach and cone ---------------------------------------- */
static vec3 lockPos[2], lockF[2];
static void s_hold(Plane *p, int who) { /* both held where they were put */
  p->pos = lockPos[who];
  p->f = lockF[who];
  p->r = v3_norm(v3_cross(v3(0, 1, 0), p->f));
  p->u = v3_cross(p->f, p->r);
  p->vel = v3_scale(p->f, 200);
  p->speed = 200;
  p->ctl.pitch = p->ctl.roll = 0;
}

static bool locks(int shooter, int target, float dist, float angle, bool classicRadar) {
  setup(shooter, target, false);
  G.classic = classicRadar;
  Plane *p = &G.planes[0];
  if (classicRadar) p->is_ai = false, p->wsel = W_RADAR; /* the classic radar missile selected: a player's weapon */
  lockPos[0] = v3(0, 4000, 0);
  lockF[0] = v3(0, 0, 1);
  lockPos[1] = v3(dist * sinf(angle), 4000, dist * cosf(angle));
  lockF[1] = v3(0, 0, 1);
  place(p, lockPos[0], lockF[0], 200);
  place(&G.planes[1], lockPos[1], lockF[1], 200);
  script = s_hold;
  bool got = false;
  for (int i = 0; i < 60 && !got; i++) {
    if (!p->is_ai) s_hold(p, 0), p->ctl.gun = p->ctl.msl = p->ctl.fire = p->ctl.flr = false;
    step();
    got = p->locked && p->lockTarget.kind == REF_PLANE;
  }
  script = NULL;
  return got;
}

/* ---- missiles against the defences ----------------------------------------- */
enum { DF_NONE, DF_DECOYS, DF_BEAM, DF_BEAM_DECOYS, DF_TURN_DECOYS, DF_BREAK, DF_COUNT };
static const char *const DF_NAME[DF_COUNT] = { "none (straight on)", "decoys only (straight on)", "hard turn to the beam",
                                               "beam, decoys once on the beam", "beam, decoys from the start of the turn",
                                               "beam, last-ditch break at 700 m" };
static int dfDef;
static bool fired;
static float sinceLaunch, decoyT;
static void s_missile(Plane *p, int who) {
  Plane *e = &G.planes[1 - who];
  if (who == 0) { /* the shooter: nose on him, one missile once locked, then keeps the lock (radar guidance) */
    steer(p, v3_sub(e->pos, p->pos), false);
    thr[0] = 0.9f;
    if (!fired && p->locked && ref_eq(p->lockTarget, ref_plane(1)) && p->mslCool <= 0) {
      p->ctl.msl = true;
      fired = true;
    }
    return;
  }
  thr[1] = 1;
  const Missile *m = NULL;
  for (int i = 0; i < G.nmissiles; i++)
    if (G.missiles[i].target.kind == REF_PLANE && G.missiles[i].target.idx == 1) m = &G.missiles[i];
  if (fired) sinceLaunch += DT;
  decoyT -= DT;
  if (!m || sinceLaunch < 0.5f || dfDef == DF_NONE) { /* not yet reacting: wings level */
    hold_pitch(p, 0);
    return;
  }
  vec3 tm = v3_norm(v3_sub(m->pos, p->pos));
  float d = v3_dist(m->pos, p->pos), onBeam = acosf(clampf(v3_dot(tm, p->f), -1, 1));
  vec3 side = v3_cross(tm, v3(0, 1, 0));
  if (v3_len(side) < 0.1f) side = p->r;
  side = v3_norm(side);
  if (v3_dot(side, p->f) < 0) side = v3_scale(side, -1);
  switch (dfDef) {
  case DF_DECOYS: hold_pitch(p, 0); break;
  case DF_BEAM: case DF_BEAM_DECOYS: case DF_TURN_DECOYS: steer(p, v3_add(side, v3(0, -0.08f, 0)), true); break;
  case DF_BREAK:
    if (d < 700) steer(p, v3_add(v3_scale(side, -1), v3_scale(tm, 0.6f)), true);
    else steer(p, v3_add(side, v3(0, -0.08f, 0)), true);
    break;
  }
  bool decoys = dfDef == DF_DECOYS || dfDef == DF_TURN_DECOYS || (dfDef == DF_BEAM_DECOYS && onBeam > 1.1f && onBeam < 2.05f);
  if (decoys && d < 2400 && decoyT <= 0) {
    p->ctl.flr = true;
    decoyT = 0.7f;
  }
}

/* one shot: true when it hit */
static bool shot(int kind, int def, float aspect, float range, uint32_t k) {
  setup(jets_find("af40"), jets_find("ecf90"), false);
  Plane *a = &G.planes[0], *b = &G.planes[1];
  float jit = ((float)(k * 2654435761u >> 8) / 16777216.0f - 0.5f) * 0.35f; /* +- 10 degrees */
  float ang = aspect + jit;
  /* the target flies along +z; the shooter sits at the aspect off his tail, nose on him */
  place(b, v3(0, 4000, 0), v3(0, 0, 1), 250);
  vec3 from = v3(range * sinf(ang), 4000, -range * cosf(ang));
  place(a, from, v3_sub(b->pos, from), 280);
  a->missiles = kind == MK_HEAT ? 2 : 0;
  a->radar = kind == MK_HEAT ? 0 : 2;
  dfDef = def;
  fired = false;
  sinceLaunch = 0;
  decoyT = 0;
  script = s_missile;
  bool launched = false;
  for (int i = 0; i < 25 * 60; i++) {
    step();
    if (G.nmissiles) launched = true;
    if (b->health < 100 || !b->alive) break;
    if (launched && G.nmissiles == 0) break;
    if (!launched && i > 6 * 60) break; /* never got a lock */
  }
  script = NULL;
  return launched && (b->health < 100 || !b->alive);
}

/* ---- fuel ------------------------------------------------------------------ */
/* the fuel burns by the throttle alone (sim.c: 0.18 + 0.55 x throttle, times the jet's burn), so a fight at full
 * power empties the tank as fast as full power straight and level: flown level, turned back at the edges */
static float fuelThr;
static void s_fuel(Plane *p, int who) {
  if (who) {
    p->pos = v3(0, 6000, 9000);
    return;
  }
  hold_pitch(p, 0);
  thr[0] = fuelThr;
  if (fabsf(p->pos.z) > 8000 && p->f.z * p->pos.z > 0) p->f = v3(0, 0, -p->f.z), p->vel = v3_scale(p->f, p->speed);
}

static float endurance(int jet, bool real, bool fight) {
  setup(jet, jet, real);
  Plane *p = &G.planes[0];
  place(p, v3(0, 4000, -4000), v3(0, 0, 1), jet_corner(jet_type(jet, real)));
  place(&G.planes[1], v3(0, 6000, 9000), v3(0, 0, 1), 200);
  fuelThr = fight ? 1 : 0.6f;
  script = s_fuel;
  int i = 0;
  for (; i < 60 * 60 * 30 && p->fuel > 0 && p->alive; i++) {
    p->refuel = false;
    step();
  }
  script = NULL;
  return p->alive ? (float)i * DT : -(float)i * DT; /* negative: it went down before the tank ran dry */
}

int doctrine_measure(void) {
  void (*was)(Game *, Plane *, float, bool) = sim_hands;
  sim_hands = hands;
  printf("# Doctrine measures\n\nFrom the simulation as it is (nothing changed), two jets in an empty sky flown by scripts "
         "through the CPU's own control path.\n");

  printf("\n## Speed back after a post-stall move\n\nThe drone flies the Cobra as the CPU does; the other jets have no "
         "post-stall in their flight model (no Cobra is possible), so they fly the nearest thing: a pull to 70 degrees "
         "at idle until at half the corner speed. Then the unload: 10 degrees nose low, full power. Times from the "
         "start of the unload.\n\n| Jet | Handling | Move | Corner (kt) | Slowest (kt) | To 150 m/s (s) | To 95%% of corner (s) | "
         "Height change (m) |\n|---|---|---|---|---|---|---|---|\n");
  const char *const RJ[] = { "ks55", "af40" };
  for (int real = 0; real < 2; real++) {
    for (int k = 0; k < 2; k++)
      if (jets_find(RJ[k]) >= 0) recovery(jets_find(RJ[k]), real);
    if (JETS[DRONE_JET].mesh[0].nv || JETS[DRONE_JET].g_max > 0) recovery(DRONE_JET, real);
  }

  printf("\n## The radar lock\n\nThe furthest a lock comes (100 m steps, the target dead ahead, held 1 s), modern rules "
         "(every CPU pilot) and the classic radar missile a player can select:\n\n| Target | Stealth score | Modern lock (m) | "
         "Classic radar (m) |\n|---|---|---|---|\n");
  int sh = jets_find("af40") >= 0 ? jets_find("af40") : 0;
  for (int t = 0; t < NJETS; t++) {
    float best[2] = { 0, 0 };
    for (int cl = 0; cl < 2; cl++)
      for (float d = 500; d <= (cl ? 45000 : 20000); d += cl ? 500 : 100)
        if (locks(sh, t, d, 0, cl)) best[cl] = d;
        else if (best[cl] > 0) break;
    printf("| %s (%s) | %.1f | %.0f | %.0f |\n", JETS[t].name, JETS[t].id, JETS[t].stealth, best[0], best[1]);
  }
  float cone = 0;
  for (float a = 0; a < 45; a += 0.25f)
    if (locks(sh, sh, 2000, a * PI_F / 180, false)) cone = a;
  printf("\nThe lock cone (a target 2 km off, 0.25 degree steps): %.2f degrees off the nose (LOCK_CONE %.2f rad = %.1f degrees), "
         "%.1f s to lock (LOCK_TIME). A missile in flight loses its target past 1.3 rad (74 degrees) off its own nose "
         "(sim.c update_missiles).\n", cone, LOCK_CONE, LOCK_CONE * 180 / PI_F, LOCK_TIME);

  printf("\n## Missiles against the defences\n\nAn af40 shoots an ecf90 (250 m/s, 4000 m) from 1.5, 3 and 4.5 km at "
         "0, 45 and 90 degrees off its tail, 10 tries each (+-10 degrees); the defender reacts 0.5 s after the launch. "
         "Decoys: flares against the heat-seeker, chaff against the radar missile, every 0.7 s inside 2.4 km. "
         "Hits out of the shots that launched:\n\n| Defence | Heat-seeker | Radar missile |\n|---|---|---|\n");
  static const float ASP[3] = { 0, 0.785f, 1.571f }, RNG[3] = { 1500, 3000, 4500 };
  for (int def = 0; def < DF_COUNT; def++) {
    printf("| %s |", DF_NAME[def]);
    for (int kind = 0; kind < 2; kind++) {
      int hits = 0, n = 0;
      for (int a = 0; a < 3; a++)
        for (int r = 0; r < 3; r++)
          for (uint32_t k = 0; k < 10; k++) {
            bool h = shot(kind == 0 ? MK_HEAT : MK_RADAR, def, ASP[a], RNG[r], k + 1);
            if (fired) n++, hits += h;
          }
      printf(" %d of %d (%.0f%%) |", hits, n, n ? 100.0 * hits / n : 0);
    }
    printf("\n");
  }

  printf("\n## Fuel\n\nFrom a full tank to dry at full power (a fight: the burn goes by the throttle alone) and "
         "at 0.6 throttle (cruise). For scale: the bases are 18 km apart, 9 km each from the middle; the CPU's bingo "
         "fuel from the middle is shown.\n\n| Jet | Handling | Fight (min) | Cruise (min) | Bingo from the middle |\n|---|---|---|---|---|\n");
  for (int real = 0; real < 2; real++)
    for (int j = 0; j < NJETS; j++) {
      float fight = endurance(j, real, true), cruise = endurance(j, real, false);
      setup(j, j, real);
      place(&G.planes[0], v3(0, 4000, 0), v3(0, 0, 1), 250);
      printf("| %s (%s) | %s | %.1f%s | %.1f%s | %.0f%% |\n", JETS[j].name, JETS[j].id, real ? "realistic" : "arcade",
             fabsf(fight) / 60, fight < 0 ? " (went down)" : "", fabsf(cruise) / 60, cruise < 0 ? " (went down)" : "",
             game_bingo(&G, &G.planes[0]));
    }
  sim_hands = was;
  return 0;
}
