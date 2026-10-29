/* map.h - the four maps from assets/maps (format: assets/maps/FORMAT.md):
 * a 129x129 heightmap at 200 m with a material per cell, bases, carriers,
 * targets, spawns, the tanker orbit, time-of-day presets, weather with puff
 * clouds, and night lights. The loader (assets.c) fills a Map; the game and
 * renderer only read it. */
#ifndef A4C_MAP_H
#define A4C_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "core/mathx.h"

#define MAP_SAMPLES 129
#define MAP_CELLS   128
#define MAP_MAX_PAL 32
#define MAP_MAX_TGT 16
#define MAP_MAX_CARRIERS 4

enum { TOD_DAWN, TOD_DAY, TOD_DUSK, TOD_NIGHT, TOD_COUNT };
enum { WX_CLEAR, WX_SCATTERED, WX_OVERCAST, WX_STORM, WX_COUNT };

typedef struct {
  uint32_t sky_top, sky_hor, fog, sun_color, ambient, water_tint; /* 0xRRGGBB */
  float    fog_density, sun_intensity, ambient_intensity, stars, lights;
  vec3     sun; /* toward the sun (the moon at night) */
} TimeOfDay;

typedef struct { float dx, dy, dz, r; int tier; } Puff;
typedef struct {
  float x, z, base, top, albedo;
  int   npuffs;
  Puff *puffs;
} Cloud;

typedef struct {
  float visibility_km, sun_factor, ambient_factor;
  int   nclouds;
  Cloud *clouds;
} Weather;

typedef struct { int team, type; float x, z, heading; } MapTarget;
typedef struct { int team; float x, z, heading, length, beam, deck; } Carrier;
typedef struct { float x, y, z, size, blink; uint32_t color; } MapLight;

typedef struct Map {
  char  id[24], name[32], blurb[256];
  float origin_x, origin_z, spacing;
  float h[MAP_SAMPLES * MAP_SAMPLES];
  uint8_t mat[MAP_CELLS * MAP_CELLS];
  struct { uint32_t color; bool water; float jitter; } pal[MAP_MAX_PAL];
  int   npal;
  float water_level, water_sky_mix;
  bool  sea;
  uint32_t far_ground;
  TimeOfDay tod[TOD_COUNT];
  Weather   weather[WX_COUNT];
  int   default_weather;
  struct { float x, z, elev, heading; } bases[2];
  Carrier carriers[MAP_MAX_CARRIERS];
  int   ncarriers;
  MapTarget targets[MAP_MAX_TGT];
  int   ntargets;
  struct { float x, y, z, heading; } spawn[2]; /* air spawn over each base */
  float tanker_x, tanker_z, tanker_r, tanker_alt;
  MapLight *lights;
  int   nlights;
} Map;

/* Height of the heightmap surface (water is flat at its level), using the
 * format's fixed two-triangle split. */
float map_height(const Map *m, float x, float z);
/* Is (x, z) on a carrier's deck? Returns its index or -1. */
int   map_carrier_at(const Map *m, float x, float z);

#endif
