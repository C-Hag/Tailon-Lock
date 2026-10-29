/* jets.c - the selectable jets (JS JETS, JT, setJet). */
#include "core/jets.h"
#include "../brand.h"

#include <stdio.h>
#include <string.h>

#include "core/world.h"

JetType JETS[MAX_JETS];
int     NJETS;
JetType JETS_REAL[MAX_JETS];

/* JS fac: hangar score 3..10 -> 0.92..1.08 */
static float fac(float s) { return 0.92f + (s - 3) / 7 * 0.16f; }

/* Blend the paint toward the team colour so blue and red stay readable;
 * glass and nozzles (dark faces) keep their colour. */
static uint32_t tint(uint32_t c, int team) {
  float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255);
  uint32_t t = TEAM[team].body;
  float tr = (float)((t >> 16) & 255), tg = (float)((t >> 8) & 255), tb = (float)(t & 255);
  float L = 0.3f * r + 0.59f * g + 0.11f * b;
  if (L < 45 || (c & FACE_GLOW)) return c;
  float k = L / 150;
  int o[3] = { (int)roundf(r * 0.4f + tr * k * 0.6f), (int)roundf(g * 0.4f + tg * k * 0.6f), (int)roundf(b * 0.4f + tb * k * 0.6f) };
  for (int i = 0; i < 3; i++) o[i] = o[i] < 0 ? 0 : o[i] > 255 ? 255 : o[i];
  return (uint32_t)(o[0] << 16 | o[1] << 8 | o[2]);
}

static void copy_tinted(Mesh *dst, const Mesh *src, int team) {
  mesh_reset(dst, src->rad);
  for (int i = 0; i < src->nv; i++) mesh_vert(dst, src->v[i].x, src->v[i].y, src->v[i].z);
  for (int i = 0; i < src->nf; i++) {
    int idx[FACE_MAX_V];
    for (int k = 0; k < src->f[i].n; k++) idx[k] = src->f[i].i[k];
    mesh_face(dst, idx, src->f[i].n, tint(src->f[i].c, team));
  }
}

int jets_add(const char *id, const char *des, const char *name, const char *role, float speed, float turn,
             float climb, float range, float tail, const Mesh *near, const Mesh *far) {
  if (NJETS >= DRONE_JET) return -1;
  JetType *j = &JETS[NJETS];
  snprintf(j->id, sizeof j->id, "%s", id);
  snprintf(j->des, sizeof j->des, "%s", des);
  snprintf(j->name, sizeof j->name, "%s", name);
  snprintf(j->role, sizeof j->role, "%s", role);
  j->g_speed = speed;
  j->g_turn = turn;
  j->g_climb = climb;
  j->g_range = range;
  j->turn = fac(turn);
  j->roll = fac((turn + speed) / 2);
  j->speed = fac(speed);
  j->climb = fac(climb);
  j->burn = 1 / fac(range);
  j->tail = tail;
  j->g_max = 9;
  j->g_min = -3;
  for (int t = 0; t < 2; t++) {
    copy_tinted(&j->mesh[t], near, t);
    if (far) {
      copy_tinted(&j->lod[t], far, t);
      j->mesh[t].lod = &j->lod[t];
    }
  }
  return NJETS++;
}

/* The DR-7 Vesper. No pilot to black out, so the airframe takes 14 g; the
 * thrust vectoring lives in the flight model (sim.c fly_drone). */
static void add_drone(void) {
  JetType *j = &JETS[DRONE_JET];
  snprintf(j->id, sizeof j->id, "dr7");
  snprintf(j->des, sizeof j->des, "DR-7");
  snprintf(j->name, sizeof j->name, "VESPER");
  snprintf(j->role, sizeof j->role,
           "Unmanned combat drone with a vectoring nozzle. Flies past the stall: the Cobra, the J-turn, the flip.");
  j->g_speed = 8;
  j->g_turn = 10;
  j->g_climb = 9;
  j->g_range = 6;
  j->turn = fac(j->g_turn);
  j->roll = fac(10);
  j->speed = fac(j->g_speed);
  j->climb = fac(j->g_climb);
  j->burn = 1 / fac(j->g_range);
  j->tail = 5.6f;
  j->g_max = 14;
  j->g_min = -8;
  for (int t = 0; t < 2; t++) copy_tinted(&j->mesh[t], &MESH_DRONE, t);
}

/* Realistic handling: the factors that make the flight model (sim.c) fly
 * the sheet's numbers. Top speed is 390 m/s x speed, thrust to weight
 * 1.1 x climb, the 1 g speed 64 / sqrt(turn) m/s (so the corner speed is
 * that x sqrt(g_max)), the roll rate 2.5 rad/s x roll. */
static void make_real(void) {
  for (int i = 0; i < MAX_JETS; i++) {
    JetType *r = &JETS_REAL[i];
    *r = JETS[i];
    if (r->s_vmax > 0) r->speed = r->s_vmax / 390;
    if (r->s_tw > 0) r->climb = r->s_tw / 1.1f;
    if (r->s_corner > 0) {
      float va = r->s_corner / sqrtf(r->g_max);
      r->turn = (64 / va) * (64 / va);
    }
    if (r->s_roll > 0) r->roll = r->s_roll / 2.5f;
  }
}

void jets_finish(void) {
  if (!JETS[DRONE_JET].mesh[0].nv && MESH_DRONE.nv) add_drone();
  make_real();
  if (NJETS) return;
  JetType *j = &JETS[NJETS++];
  memset(j, 0, sizeof *j);
  snprintf(j->id, sizeof j->id, "ace");
  snprintf(j->des, sizeof j->des, "ACE");
  snprintf(j->name, sizeof j->name, "CLASSIC");
  snprintf(j->role, sizeof j->role, "The original " GAME_TITLE " jet.");
  j->turn = j->roll = j->speed = j->climb = j->burn = 1;
  j->g_speed = j->g_turn = j->g_climb = j->g_range = 6.5f;
  j->tail = 6.5f;
  j->g_max = 9;
  j->g_min = -3;
  j->mesh[0] = MESH_JET[0];
  j->mesh[1] = MESH_JET[1];
  make_real();
}

/* the speed the wing holds 1 g at (the stall the game flies to is higher,
 * for safety); a better-turning jet has a lighter wing loading */
static float lift_speed(const JetType *j) { return 64.0f / sqrtf(j->turn); }

float jet_lift_g(const JetType *j, float speed) {
  float k = speed / lift_speed(j);
  return fminf(j->g_max, k * k);
}

float jet_corner(const JetType *j) { return lift_speed(j) * sqrtf(j->g_max); }

int jets_find(const char *id) {
  for (int i = 0; i < NJETS; i++)
    if (!strcmp(JETS[i].id, id)) return i;
  return -1;
}
