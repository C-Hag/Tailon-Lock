/* test_core.c - checks for the game core, which has no platform code. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/defend.h"
#include "core/fmath.h"
#include "core/game.h"
#include "core/jets.h"
#include "core/map.h"
#include "core/mathx.h"
#include "core/sim.h"
#include "core/world.h"

static int failures;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
      failures++;                                                      \
    }                                                                  \
  } while (0)

/* The first four outputs of the JS mulberry32(1987), which seeds the
 * mountains; the C port must match them bit for bit. */
static void test_rng_matches_js(void) {
  static const uint32_t expect[] = { 117033085u, 799216800u, 2263938144u, 3465845030u };
  Rng r = { 1987u };
  for (int i = 0; i < 4; i++) CHECK(rng_u32(&r) == expect[i]);
}

static void test_vec3(void) {
  vec3 x = v3(1, 0, 0), y = v3(0, 1, 0);
  vec3 z = v3_cross(x, y);
  CHECK(z.x == 0 && z.y == 0 && z.z == 1);
  CHECK(v3_dot(x, y) == 0);
  vec3 n = v3_norm(v3(3, 0, 4));
  CHECK(fabsf(v3_len(n) - 1.0f) < 1e-6f);
}

/* The mountains come from mulberry32(1987) like the JS, so the first one
 * must sit where the browser game puts it. */
static void test_world(void) {
  world_init();
  CHECK(terrain_h(0, 0) == 0); /* the centre is kept clear */
  const Mountain *m = &MOUNTAINS[0];
  CHECK(fabsf(terrain_h(m->x, m->z) - m->h) < 1e-3f);
  CHECK(MESH_JET[0].nv == 24 && MESH_JET[0].nf == 14);
}

/* Two CPU pilots fight a whole match: nothing may go NaN or leave its
 * pool, and the match has to end. */
static void test_demo_match(void) {
  static Game g;
  game_new(&g, MODE_DEMO, DIFF_HARD, false, NULL, 1234u);
  int ticks = 0;
  for (; ticks < 60 * 7200 && !g.over; ticks++) { /* aces flying BFM, with trips home to refuel, can take a while */
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
    for (int i = 0; i < g.nplanes; i++) {
      const Plane *p = &g.planes[i];
      CHECK(!isnan(p->pos.x) && !isnan(p->pos.y) && !isnan(p->pos.z));
      CHECK(fabsf(v3_len(p->f) - 1) < 1e-3f);
    }
    CHECK(g.nparts <= MAX_PARTS && g.nbullets <= MAX_BULLETS && g.nmissiles <= MAX_MISSILES);
    if (failures) break;
  }
  CHECK(g.over);
  printf("demo match: %s after %.0f s (%s)\n", g.winner < 0 ? "draw" : g.winner ? "red wins" : "blue wins",
         ticks / 60.0, g.reason);
}

/* Full pull at fighting speed: with G effects on the pilot greys out and
 * then blacks out; with them off nothing happens. */
static void test_gforces(void) {
  static Game g;
  for (int on = 0; on < 2; on++) {
    game_new(&g, MODE_1P, DIFF_NORMAL, false, NULL, 7u);
    g.g_effects = on;
    Plane *p = &g.planes[0];
    bool gloc = false;
    float peak = 0;
    for (int i = 0; i < 60 * 12; i++) {
      p->ctl.pitch = 1;
      p->throttle = 1;
      p->invuln = 99;
      p->speed = 230; /* held at fighting speed: the pull itself would bleed it */
      sim_update(&g, 1.0f / 60);
      g.nsounds = 0;
      peak = fmaxf(peak, p->gload);
      if (p->gloc > 0) gloc = true;
      if (!p->alive) break;
    }
    CHECK(peak > 7);
    CHECK(gloc == (on == 1));
  }
}

/* The carrier defence with its turrets on their own: waves come, missiles
 * fly, the CIWS shoots some down, and the mission ends one way or another. */
static void test_defend(void) {
  static Game g;
  game_new(&g, MODE_DEFEND, DIFF_NORMAL, false, NULL, 99u);
  CHECK(g.def.on && g.def.nturrets == 5);
  int ticks = 0, maxwave = 0;
  float minhp = g.def.hp;
  for (; ticks < 60 * 60 * 15 && !g.over; ticks++) {
    g.planes[0].ctl.pitch = 0.05f; /* the pilot just circles */
    g.planes[0].ctl.roll = 0.3f;
    g.planes[0].invuln = 99;
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
    if (g.def.wave > maxwave) maxwave = g.def.wave;
    minhp = fminf(minhp, g.def.hp);
  }
  printf("defend: %s after %.0f s, wave %d, carrier %.0f/%.0f, turret kills %d, missiles shot down %d\n",
         g.winner == 0 ? "held" : "sunk", ticks / 60.0, maxwave, g.def.hp, g.def.hpmax, g.def.kills, g.def.ashm_down);
  CHECK(maxwave >= 1);
  CHECK(g.def.kills + g.def.ashm_down > 0);
}

/* The captain walks off the bridge, down to the deck and out to a gun. */
static void walk_to(Game *g, float x, float z) {
  Defense *d = &g->def;
  for (int i = 0; i < 60 * 30; i++) {
    float dx = x - d->cap.x, dz = z - d->cap.z;
    if (dx * dx + dz * dz < 1) break;
    float want = atan2f(dx, dz), err = want - d->cap_yaw;
    while (err > PI_F) err -= 2 * PI_F;
    while (err < -PI_F) err += 2 * PI_F;
    d->cap_turn = clampf(err * 3, -1, 1);
    d->cap_move = fabsf(err) < 0.5f ? 1 : 0;
    d->cap_run = true;
    sim_update(g, 1.0f / 60);
    g->nsounds = 0;
  }
  d->cap_move = d->cap_turn = 0;
}

static void test_captain(void) {
  static Game g;
  game_new(&g, MODE_DEFEND, DIFF_NORMAL, true, NULL, 5u);
  Defense *d = &g.def;
  CHECK(d->cmd_state == CMD_WALK && d->cap_bridge);
  CHECK(defend_usable(&g).kind == USE_TACTICAL);
  walk_to(&g, 31.4f, -48.0f);
  CHECK(defend_usable(&g).kind == USE_DOWN);
  defend_use(&g);
  CHECK(!d->cap_bridge && fabsf(d->cap.y - d->deck) < 0.01f);
  /* round the island to the bow gun */
  walk_to(&g, 18, -20);
  walk_to(&g, -27, 144);
  Usable u = defend_usable(&g);
  CHECK(u.kind == USE_TURRET && u.arg == 0);
  defend_use(&g);
  CHECK(d->cmd_state == CMD_TURRET && d->tur[0].mode == TM_MANUAL);
  /* the island is solid */
  d->cmd_state = CMD_WALK;
  d->cap = v3(20, d->deck, -40);
  d->cap_yaw = PI_F / 2;
  for (int i = 0; i < 120; i++) {
    d->cap_move = 1;
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
  }
  CHECK(d->cap.x <= ISLAND_X0 + 0.01f);
}

/* Beat the Drone!: a CPU ace flies the jet against the CPU drone. The
 * drone must fly its post-stall moves, nothing may break, and the match
 * has to end. */
static void test_drone_match(void) {
  static Game g;
  for (int seed = 1; seed <= 3; seed++) {
    game_new(&g, MODE_DRONE, DIFF_HARD, false, NULL, 77u + seed);
    Plane *jet = &g.planes[0], *dr = &g.planes[1];
    CHECK(!jet->drone && dr->drone && dr->is_ai && dr->jet == DRONE_JET && g.ntargets == 0);
    jet->is_ai = true;
    jet->ai.diff = DIFF_HARD;
    jet->ai.gt = -1;
    int ticks = 0, stunts[4] = { 0 }, prev = STUNT_NONE, moves[BFM_COUNT] = { 0 }, pbfm = -1;
    float maxA = 0;
    for (; ticks < 60 * 60 * 30 && !g.over; ticks++) {
      sim_update(&g, 1.0f / 60);
      g.nsounds = 0;
      for (int i = 0; i < g.nplanes; i++) {
        const Plane *p = &g.planes[i];
        CHECK(!isnan(p->pos.x) && !isnan(p->pos.y) && !isnan(p->pos.z) && !isnan(p->vel.x));
        CHECK(fabsf(v3_len(p->f) - 1) < 1e-3f);
      }
      if (dr->alive) maxA = fmaxf(maxA, dr->aoa);
      if (dr->lastStunt != prev && dr->lastStunt) stunts[dr->lastStunt]++;
      if (dr->ai.bfm != pbfm) moves[dr->ai.bfm]++;
      pbfm = dr->ai.bfm;
      prev = dr->lastStunt;
      if (failures) break;
    }
    CHECK(g.over);
    CHECK(maxA > 1.2f); /* it flew past the stall */
    printf("drone match %d: %s after %.0f s (%s); cobras %d, flips %d, j-turns %d, max aoa %.0f\n", seed,
           g.winner < 0 ? "draw" : g.winner ? "the drone wins" : "the jet wins", ticks / 60.0, g.reason, stunts[STUNT_COBRA],
           stunts[STUNT_FLIP], stunts[STUNT_JTURN], maxA * 57.3f);
    printf("  moves flown:");
    for (int k = BFM_COBRA; k < BFM_COUNT; k++) printf(" %s %d", BFM_NAMES[k], moves[k]);
    printf("; jet kills %d, drone kills %d\n", jet->kills, dr->kills);
  }
}

/* The sim's own maths (core/fmath.c) must give the same bits on every
 * computer: these are the bits it gives (Linux, Windows and macOS alike),
 * and it must stay within about half a float ulp of the exact values. */
static uint32_t bits(float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return u;
}
static void test_fmath(void) {
  static const float in[5] = { 0.5f, 1.0f, -2.5f, 3.14159265f, 100.0f };
  static const uint32_t expect[5][6] = {
    { 0x3ef57744u, 0x3f60a940u, 0x3ea4bc7du, 0x3ba237efu, 0x3fc86da3u, 0x3f86900du },
    { 0x3f576aa4u, 0x3f0a5140u, 0x3f168757u, 0x3c223870u, 0x3fc7cb6au, 0x3f8d763eu },
    { 0xbf193578u, 0xbf4d17bfu, 0xbf83e363u, 0xbccacb01u, 0x3fcc3b07u, 0x3f475f7du },
    { 0xb3bbbd2eu, 0xbf800000u, 0x3f900b1au, 0x3cfeda2fu, 0x3fc51472u, 0x3faf3eecu },
    { 0xbf01a12eu, 0x3f5cc0eeu, 0x3fc7245fu, 0x3fb708f2u, 0x3e103746u, 0x46ac14eeu },
  };
  for (int k = 0; k < 5; k++) {
    float x = in[k];
    float got[6] = { fm_sinf(x), fm_cosf(x), fm_atan2f(x, 1.5f), fm_asinf(x / 101), fm_acosf(x / 101), fm_expf(x / 10) };
    for (int j = 0; j < 6; j++)
      if (bits(got[j]) != expect[k][j]) {
        fprintf(stderr, "fmath: input %d, function %d gives %08x, not %08x\n", k, j, (unsigned)bits(got[j]),
                (unsigned)expect[k][j]);
        failures++;
      }
  }
  double es = 0, ea = 0, eb = 0, ee = 0;
  for (int i = 0; i < 200000; i++) {
    float x = -200.0f + 400.0f * (float)i / 200000.0f, y = -50.0f + 100.0f * (float)((i * 7919) % 200000) / 200000.0f;
    float t = -1.0f + 2.0f * (float)i / 200000.0f, z = -80.0f + 100.0f * (float)i / 200000.0f;
    es = fmax(es, fmax(fabs(fm_sinf(x) - sin(x)), fabs(fm_cosf(x) - cos(x))));
    ea = fmax(ea, fabs(fm_atan2f(y, x) - atan2(y, x)));
    eb = fmax(eb, fmax(fabs(fm_asinf(t) - asin(t)), fabs(fm_acosf(t) - acos(t))));
    ee = fmax(ee, fabs(fm_expf(z) - exp(z)) / exp(z));
  }
  CHECK(es < 6e-8 && ea < 2.4e-7 && eb < 2.4e-7 && ee < 1.2e-7);
  CHECK(fm_sinf(0) == 0 && fm_cosf(0) == 1 && fm_expf(0) == 1 && fm_atan2f(0, 1) == 0);
  CHECK(fm_asinf(1) == fm_atan2f(1, 0) && isnan(fm_acosf(1.5f)) && fm_expf(-200) == 0 && isinf(fm_expf(200)));
  /* pow and tan, for the sound (synth.c, sfx.c): the same bits on every system, and within a float's last bits over
   * the ranges they are used on (semitones, decibels, frequency sweeps, a filter's tan up to 20 kHz) */
  static const float px[5] = { 2.0f, 10.0f, 0.0008f, 3.5f, 1.0594631f }, py[5] = { 0.5833333f, -1.35f, 0.75f, 7.25f, -40.0f };
  static const float tx[5] = { 0.001308997f, 0.065449847f, 0.5f, 1.0f, 1.3089969f };
  static const uint32_t pt[5][2] = { { 0x3fbfc887u, 0x3aab92adu }, { 0x3d36f62bu, 0x3d863ba7u }, { 0x3b9bdf2bu, 0x3f0bda7bu },
                                     { 0x460980ddu, 0x3fc75923u }, { 0x3dcb2fddu, 0x406ed9eau } };
  for (int k = 0; k < 5; k++) {
    uint32_t p = bits(fm_powf(px[k], py[k])), t = bits(fm_tanf(tx[k]));
    if (p != pt[k][0] || t != pt[k][1]) {
      fprintf(stderr, "fmath: pow/tan input %d gives %08x %08x, not %08x %08x\n", k, (unsigned)p, (unsigned)t,
              (unsigned)pt[k][0], (unsigned)pt[k][1]);
      failures++;
    }
  }
  double ep = 0, et = 0;
  for (int i = 0; i < 200000; i++) {
    float n = -96.0f + 192.0f * (float)i / 200000.0f, d = -6.0f + 9.0f * (float)i / 200000.0f;
    float r = 0.0001f + 9.9999f * (float)((i * 7919) % 200000) / 200000.0f, k = (float)i / 200000.0f;
    float a[3] = { fm_powf(2.0f, n / 12), fm_powf(10.0f, d), fm_powf(r, k) };
    double b[3] = { pow(2.0, (double)(n / 12)), pow(10.0, (double)d), pow((double)r, (double)k) };
    for (int j = 0; j < 3; j++) ep = fmax(ep, fabs(a[j] - b[j]) / b[j]);
    float w = 3.14159265f * (10.0f + 19990.0f * (float)i / 200000.0f) / 48000.0f;
    et = fmax(et, fabs(fm_tanf(w) - tan((double)w)) / tan((double)w));
  }
  CHECK(ep < 1.2e-7 && et < 2.4e-7);
  CHECK(fm_powf(0, 2) == 0 && fm_powf(5, 0) == 1 && fm_powf(-2, 2) == 0 && isinf(fm_powf(2, 200)) && fm_powf(2, -200) == 0);
}

/* a network match flies on through the pause with no keys read: the gun and the stick held the frame before let
 * go, so the jet neither fires nor pulls for the 20 s it is paused (before the fix: 240 rounds gone, the stick
 * still back) */
static void test_let_go(void) {
  static Game g;
  GameRules r = { .g_effects = true, .load = { { -1, 0, 0 }, { -1, 0, 0 } } };
  game_new_rules(&g, MODE_1P, DIFF_NORMAL, false, NULL, 7, &r);
  Plane *p = &g.planes[g.locals[0]];
  g.planes[1].inactive = true;
  g.planes[1].alive = false;
  p->ctl.gun = p->ctl.msl = p->ctl.flr = p->ctl.fire = p->ctl.vec = true;
  p->ctl.pitch = 1;
  p->ctl.roll = -1;
  game_let_go(&g);
  float ammo0 = p->ammo;
  for (int i = 0; i < 20 * 60 && p->alive; i++) {
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
  }
  CHECK(p->ammo == ammo0);
  CHECK(!p->ctl.gun && !p->ctl.msl && !p->ctl.flr && !p->ctl.fire && !p->ctl.vec && p->ctl.pitch == 0 && p->ctl.roll == 0);
}

/* a CPU jet full again on the carrier's deck goes to a bow catapult no other jet is on: player 1 starts on the
 * starboard one (the old code put the wingman on it too, and both died in a MID-AIR COLLISION); with both taken it
 * waits */
static void test_catapult(void) {
  static Map M;
  M.spacing = 200;
  M.origin_x = M.origin_z = -12800;
  M.sea = true;
  M.bases[0].z = -20000;
  M.bases[1].z = 20000;
  M.bases[1].heading = 180;
  M.ncarriers = 1;
  M.carriers[0] = (Carrier){ .team = 0, .x = 0, .z = 0, .heading = 0, .length = 330, .beam = 76, .deck = 20 };
  M.tanker_r = 3200;
  M.tanker_alt = 2300;
  world_set_map(&M, 0, 0);
  for (int both = 0; both < 2; both++) {
    static Game g;
    GameRules r = { .ground_start = true, .pairs = true, .load = { { -1, 0, 0 }, { -1, 0, 0 } } };
    game_new_rules(&g, MODE_1P, DIFF_NORMAL, false, NULL, 1, &r);
    for (int i = 0; i < g.nplanes; i++) game_spawn(&g, &g.planes[i]);
    Plane *you = &g.planes[0], *wing = &g.planes[1], *other = &g.planes[2];
    CHECK(you->oncat);
    if (both) { /* an enemy jet parked on the port catapult */
      other->pos = v3(-8, you->pos.y, you->pos.z);
      other->speed = 0;
    }
    /* the wingman has landed, rolled out mid-deck and is full again */
    vec3 at = v3(0, terrain_h(0, -60) + 3, -60);
    wing->pos = at;
    wing->onGround = true;
    wing->taxi = false;
    wing->speed = 0;
    wing->ai.refuel = true;
    wing->ai.site = 1;
    wing->fuel = 100;
    int full[4];
    game_full_load(&g, wing, full);
    wing->missiles = full[0], wing->radar = full[1], wing->ground = full[2];
    sim_update(&g, 1.0f / 60);
    CHECK(you->alive && wing->alive);
    CHECK(v3_dist(you->pos, wing->pos) >= 15);
    if (both) CHECK(wing->ai.refuel && v3_dist(wing->pos, at) < 1); /* waits where it is */
    else CHECK(wing->oncat && !wing->ai.refuel);
  }
  world_set_map(NULL, 0, 0);
}

/* a map's targets: all of them taken, up to the map's own limit (16), and a team with none of its own does not lose
 * for having none left (it used to, on the first tick) */
static void test_map_targets(void) {
  static Map M;
  M.spacing = 200;
  M.origin_x = M.origin_z = -12800;
  M.sea = true;
  M.bases[0].z = -20000;
  M.bases[1].z = 20000;
  M.bases[1].heading = 180;
  M.tanker_r = 3200;
  M.tanker_alt = 2300;
  M.ntargets = MAP_MAX_TGT;
  for (int k = 0; k < MAP_MAX_TGT; k++) M.targets[k] = (MapTarget){ 1, TGT_SHIP, -3000.0f + 400.0f * (float)k, 9000, 0 };
  world_set_map(&M, 0, 0);
  static Game g;
  GameRules r = { .load = { { -1, 0, 0 }, { -1, 0, 0 } } };
  game_new_rules(&g, MODE_1P, DIFF_NORMAL, false, NULL, 3, &r);
  CHECK(g.ntargets == MAP_MAX_TGT);
  for (int i = 0; i < 60; i++) sim_update(&g, 1.0f / 60);
  CHECK(!g.over);
  world_set_map(NULL, 0, 0);
}

/* a jet full again on a carrier's deck goes to that carrier's catapult, not to the one it set out for */
static void test_other_carrier(void) {
  static Map M;
  M.spacing = 200;
  M.origin_x = M.origin_z = -12800;
  M.sea = true;
  M.bases[0].z = -20000;
  M.bases[1].z = 20000;
  M.bases[1].heading = 180;
  M.ncarriers = 2;
  M.carriers[0] = (Carrier){ .team = 0, .x = 0, .z = 0, .heading = 0, .length = 330, .beam = 76, .deck = 20 };
  M.carriers[1] = (Carrier){ .team = 0, .x = 4000, .z = 0, .heading = 0, .length = 330, .beam = 76, .deck = 20 };
  M.tanker_r = 3200;
  M.tanker_alt = 2300;
  world_set_map(&M, 0, 0);
  static Game g;
  GameRules r = { .load = { { -1, 0, 0 }, { -1, 0, 0 } } };
  game_new_rules(&g, MODE_1P, DIFF_NORMAL, false, NULL, 1, &r);
  Plane *wing = &g.planes[1];
  wing->team = 0;
  wing->pos = v3(4000, terrain_h(4000, -60) + 3, -60); /* on the second carrier */
  wing->onGround = true;
  wing->taxi = false;
  wing->speed = 0;
  wing->ai.refuel = true;
  wing->ai.site = 1; /* it set out for the first */
  wing->fuel = 100;
  int full[4];
  game_full_load(&g, wing, full);
  wing->missiles = full[0], wing->radar = full[1], wing->ground = full[2];
  g.planes[0].pos = v3(0, 3000, -9000);
  sim_update(&g, 1.0f / 60);
  CHECK(wing->oncat && fabsf(wing->pos.x - 4008) < 1);
  /* with both jets blue the red side has none at all: its line names the side, and no jet is read at index -1
   * (that read was outside the array, UBSan on the Linux gate) */
  CHECK(g.over && !strcmp(g.reason, "Every jet in the red team is down."));
  world_set_map(NULL, 0, 0);
}

/* the carrier's attackers carry the defence's load (heat-seekers and the anti-ship missile), from the first wave,
 * and every wave comes with its whole load, the radar and air-to-ground missiles too */
static void test_defend_loads(void) {
  static Game g;
  game_new(&g, MODE_DEFEND, DIFF_HARD, false, NULL, 99u);
  int bad = 0;
  for (int i = 0; i < g.nplanes; i++) {
    Plane *p = &g.planes[i];
    if (p->team != 1) continue;
    int full[4];
    game_full_load(&g, p, full);
    bad += full[1] != 0 || p->radar != 0; /* not the strike table's two radar missiles */
    p->radar = 5, p->ground = 0;          /* as a wave might leave them */
  }
  CHECK(bad == 0);
  int wave = g.def.wave;
  for (int t = 0; t < 60 * 60 * 10 && g.def.wave == wave && !g.over; t++) {
    for (int i = 0; i < g.nplanes; i++)
      if (g.planes[i].team == 1 && g.planes[i].alive) g.planes[i].health = -1, g.planes[i].alive = false; /* the wave down */
    g.planes[0].invuln = 99;
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
  }
  CHECK(g.def.wave > wave);
  bad = 0;
  for (int i = 0; i < g.nplanes; i++) {
    const Plane *p = &g.planes[i];
    if (p->team != 1 || !p->alive) continue;
    int full[4];
    game_full_load(&g, p, full);
    bad += p->radar != full[1] || p->ground != full[2];
  }
  CHECK(bad == 0);
}

/* a solo carrier defence ends when the pilot's last jet is down (it used to fly on with nobody, and could later
 * report "held out" as a win); the carrier's footprint is its map size */
static void test_defend_end(void) {
  static Game g;
  game_new(&g, MODE_DEFEND, DIFF_NORMAL, false, NULL, 7u);
  CHECK(!g.def.commander);
  for (int i = 0; i < g.nplanes; i++)
    if (g.planes[i].team == 0) g.planes[i].lives = 0, g.planes[i].alive = false;
  for (int t = 0; t < 60 && !g.over; t++) {
    sim_update(&g, 1.0f / 60);
    g.nsounds = 0;
  }
  CHECK(g.over && g.winner == 1);
}

int main(void) {
  test_fmath();
  test_captain();
  test_defend();
  test_gforces();
  test_rng_matches_js();
  test_vec3();
  test_world();
  test_demo_match();
  test_drone_match();
  test_let_go();
  test_catapult();
  test_map_targets();
  test_other_carrier();
  test_defend_loads();
  test_defend_end();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("core tests passed");
  return 0;
}
