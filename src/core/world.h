/* world.h - arena, bases, mountains seeded with 1987, terrainH (JS lines 230-306). */
#ifndef A4C_WORLD_H
#define A4C_WORLD_H

#include <stdint.h>

#include "core/mathx.h"
#include "core/map.h"
#include "core/mesh.h"

#define NEAR_Z   1.0f
#define FAR_Z    24000.0f
#define ARENA_X  9500.0f
#define ARENA_Z  11500.0f
#define MAX_MSL  6
#define MAX_FLR  16
#define MAX_AMMO 500
#define LIVES    3
#define LOCK_CONE 0.33f
#define LOCK_TIME 0.7f

/* Colours as 0xRRGGBB (alpha is added when drawn). */
typedef struct { uint32_t body, acc, ui, bld; } TeamColors;
extern const TeamColors TEAM[2];

typedef struct { float x, z, dir; } Base;
extern Base BASES[2];

#define NUM_MOUNTAINS 16
typedef struct { float x, z, s, h; Mesh m; } Mountain;
extern Mountain MOUNTAINS[NUM_MOUNTAINS];

extern vec3 LIGHT;

/* The map being flown, or NULL for the classic arena with the 1987
 * mountains; its time of day and weather. */
extern const Map *MAP;
extern int MAP_TOD, MAP_WX;

/* Builds the mountains and meshes. Safe to call more than once. */
void  world_init(void);
/* Switches the world to a map (NULL: classic) and sets the light. */
void  world_set_map(const Map *m, int tod, int wx);
/* Ground height, including water and carrier decks. */
float terrain_h(float x, float z);

#endif
