/* test_core.c - checks for the SDL-free game core. */
#include <stdio.h>

#include "core/defend.h"
#include "core/game.h"
#include "core/jets.h"
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
  for (; ticks < 60 * 60 * 40 && !g.over; ticks++) { /* aces flying BFM can take a while */
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

int main(void) {
  test_captain();
  test_defend();
  test_gforces();
  test_rng_matches_js();
  test_vec3();
  test_world();
  test_demo_match();
  test_drone_match();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("core tests passed");
  return 0;
}
