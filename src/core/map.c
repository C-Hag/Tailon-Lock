/* map.c - heightmap lookups for the maps (assets/maps/FORMAT.md). */
#include "core/map.h"
#include "core/fmath.h"

static float sample(const Map *m, int i, int j) {
  if (i < 0) i = 0;
  if (j < 0) j = 0;
  if (i >= MAP_SAMPLES) i = MAP_SAMPLES - 1;
  if (j >= MAP_SAMPLES) j = MAP_SAMPLES - 1;
  return m->h[j * MAP_SAMPLES + i];
}

float map_height(const Map *m, float x, float z) {
  float u = (x - m->origin_x) / m->spacing, v = (z - m->origin_z) / m->spacing;
  if (!(u >= 0 && v >= 0 && u < MAP_CELLS && v < MAP_CELLS)) return m->water_level; /* off the map, or not a number */
  int i = (int)u, j = (int)v;
  float fu = u - i, fv = v - j;
  float a = sample(m, i, j), b = sample(m, i + 1, j), c = sample(m, i + 1, j + 1), d = sample(m, i, j + 1);
  return fu >= fv ? a + (b - a) * fu + (c - b) * fv : a + (c - d) * fu + (d - a) * fv;
}

int map_carrier_at(const Map *m, float x, float z) {
  if (!m) return -1;
  for (int k = 0; k < m->ncarriers; k++) {
    const Carrier *c = &m->carriers[k];
    float h = c->heading * PI_F / 180, s = fm_sinf(h), co = fm_cosf(h);
    float dx = x - c->x, dz = z - c->z;
    /* world -> carrier-local: +X starboard, +Z bow */
    float lx = dx * co - dz * s, lz = dx * s + dz * co;
    if (fabsf(lx) < c->beam / 2 && fabsf(lz) < c->length / 2) return k;
  }
  return -1;
}
