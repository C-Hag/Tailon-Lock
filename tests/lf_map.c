/* lf_map.c - a libFuzzer target for the map loader (src/assets.c) and the
 * terrain lookups (src/core/map.c, terrain_h in src/core/world.c): the map's
 * JSON, its .h16 heights and its .mat surface. Original code for this
 * project. Build with -DA4C_LIBFUZZER=ON (clang); run it under ASan and UBSan.
 *
 * An input is a flags byte, the lengths of the heights and the surface (16
 * bits each, little-endian), those bytes, and the rest is the map's JSON. The
 * files go in the fuzzer's own assets folder as maps/fz.json, fz.h16 and
 * fz.mat (flag 1: the heights repeated to the full 129 x 129 samples, 2: the
 * surface to the full 128 x 128 cells; otherwise written as they are, so a
 * short file is refused). A map that loads is set as the world's map and its
 * ground looked up across it and around its edges. */
#include "lf_common.h" /* first: it asks for POSIX */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/world.h"

static uint8_t *tiled(const uint8_t *src, size_t n, size_t full) {
  uint8_t *b = malloc(full);
  if (!b) abort();
  for (size_t i = 0; i < full; i++) b[i] = n ? src[i % n] : 0;
  return b;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  static bool ready;
  if (!ready) {
    static const char *const subs[] = { "assets", "assets/maps", NULL };
    static const char INDEX[] = "[{\"id\":\"fz\",\"name\":\"Fuzz\"}]";
    char d[600];
    snprintf(d, sizeof d, "%s/assets/", lf_folder(subs));
    lf_write("assets/maps/maps_index.json", INDEX, sizeof INDEX - 1);
    assets_fuzz_dir(d);
    ready = true;
  }
  if (n < 5) return 0;
  int flags = data[0];
  size_t hn = (size_t)(data[1] | data[2] << 8), mn = (size_t)(data[3] | data[4] << 8);
  data += 5, n -= 5;
  if (hn > n) hn = n;
  const uint8_t *h = data;
  data += hn, n -= hn;
  if (mn > n) mn = n;
  const uint8_t *m = data;
  data += mn, n -= mn;
  if (flags & 1) {
    uint8_t *b = tiled(h, hn, MAP_SAMPLES * MAP_SAMPLES * 2);
    lf_write("assets/maps/fz.h16", b, MAP_SAMPLES * MAP_SAMPLES * 2);
    free(b);
  } else lf_write("assets/maps/fz.h16", h, hn);
  if (flags & 2) {
    uint8_t *b = tiled(m, mn, MAP_CELLS * MAP_CELLS);
    lf_write("assets/maps/fz.mat", b, MAP_CELLS * MAP_CELLS);
    free(b);
  } else lf_write("assets/maps/fz.mat", m, mn);
  lf_write("assets/maps/fz.json", data, n);
  const Map *map = assets_fuzz_map();
  if (!map) return 0;
  world_set_map(map, flags >> 2 & 3, flags >> 4 & 3);
  volatile float sum = 0;
  static const float AT[] = { -1, 0, 0.5f, 1, 63.7f, 127, 127.99f, 128, 129, 1e9f };
  for (size_t i = 0; i < sizeof AT / sizeof *AT; i++)
    for (size_t j = 0; j < sizeof AT / sizeof *AT; j++) {
      float x = map->origin_x + AT[i] * map->spacing, z = map->origin_z + AT[j] * map->spacing;
      sum += map_height(map, x, z) + terrain_h(x, z) + (float)map_carrier_at(map, x, z);
    }
  for (int k = 0; k < map->ncarriers; k++) sum += terrain_h(map->carriers[k].x, map->carriers[k].z);
  world_set_map(NULL, TOD_DAY, WX_CLEAR);
  return 0;
}
