/* world.c - arena, bases, mountains seeded with 1987, terrainH (JS lines 230-306). */
#include "core/world.h"
#include "core/fmath.h"

const TeamColors TEAM[2] = {
  { 0x8fb3ff, 0x2f52c8, 0x7aa8ff, 0x4d74e0 },
  { 0xff8a6a, 0xb2321c, 0xff7a5c, 0xd2482e },
};

Base BASES[2] = { { 0, -9000, 1 }, { 0, 9000, -1 } };
const Map *MAP;
int MAP_TOD = TOD_DAY, MAP_WX = WX_SCATTERED;

Mountain MOUNTAINS[NUM_MOUNTAINS];
vec3 LIGHT;

static int world_ready;

static void build_mountains(void) {
  Rng rng = { 1987u };
  for (int i = 0; i < NUM_MOUNTAINS; i++) {
    double x, z;
    do {
      x = (rng_f(&rng) * 2 - 1) * 9000;
      z = (rng_f(&rng) * 2 - 1) * 5400;
    } while (sqrt(x * x + z * z) < 1800);
    float s = (float)(rng_f(&rng) * 900 + 700), h = (float)(rng_f(&rng) * 1200 + 500);
    uint32_t low = rng_f(&rng) < 0.5 ? 0x5f6b3a : 0x7a5a34;
    uint32_t top = h > 1150 ? 0xe6e9f0 : 0x6b5a3a;

    Mesh *m = &MOUNTAINS[i].m;
    mesh_reset(m, s * 1.5f);
    mesh_vert(m, -s, 0, -s); mesh_vert(m, s, 0, -s); mesh_vert(m, s, 0, s); mesh_vert(m, -s, 0, s);
    for (int k = 0; k < 4; k++) mesh_vert(m, m->v[k].x * 0.32f, h * 0.68f, m->v[k].z * 0.32f);
    mesh_vert(m, 0, h, 0);
    for (int k = 0; k < 4; k++) {
      int j = (k + 1) % 4;
      mesh_face4(m, k, j, j + 4, k + 4, low);
      mesh_face3(m, k + 4, j + 4, 8, top);
    }
    MOUNTAINS[i].x = (float)x;
    MOUNTAINS[i].z = (float)z;
    MOUNTAINS[i].s = s;
    MOUNTAINS[i].h = h;
  }
}

void world_init(void) {
  if (world_ready) return;
  world_ready = 1;
  LIGHT = v3_norm(v3(0.35f, 0.85f, -0.4f));
  build_mountains();
  mesh_init_builtin();
}

void world_set_map(const Map *m, int tod, int wx) {
  world_init();
  MAP = m;
  MAP_TOD = tod;
  MAP_WX = wx;
  if (!m) {
    BASES[0] = (Base){ 0, -9000, 1 };
    BASES[1] = (Base){ 0, 9000, -1 };
    LIGHT = v3_norm(v3(0.35f, 0.85f, -0.4f));
    return;
  }
  for (int t = 0; t < 2; t++) {
    BASES[t].x = m->bases[t].x;
    BASES[t].z = m->bases[t].z;
    BASES[t].dir = fm_cosf(m->bases[t].heading * PI_F / 180) >= 0 ? 1.0f : -1.0f;
  }
  LIGHT = v3_norm(m->tod[tod].sun);
}

float terrain_h(float x, float z) {
  if (MAP) {
    float h = map_height(MAP, x, z);
    int c = map_carrier_at(MAP, x, z);
    if (c >= 0 && MAP->carriers[c].deck > h) h = MAP->carriers[c].deck;
    return h;
  }
  float h = 0;
  for (int i = 0; i < NUM_MOUNTAINS; i++) {
    const Mountain *m = &MOUNTAINS[i];
    float k = 1 - fmaxf(fabsf(x - m->x), fabsf(z - m->z)) / m->s;
    if (k > 0 && m->h * k > h) h = m->h * k;
  }
  return h;
}
