/* mesh.h - meshes: the built-in ones (JS jetMesh, targetMesh, MESH) and the
 * selectable jets loaded from assets/jets.json. One colour per face, as in
 * the JS. Arrays are allocated once at start-up and never freed. */
#ifndef A4C_MESH_H
#define A4C_MESH_H

#include <stdint.h>
#include <stdio.h>

#include "core/mathx.h"

#define FACE_MAX_V 8

/* A face colour with this bit glows: drawn at full brightness, unlit. */
#define FACE_GLOW 0x01000000u

typedef struct {
  uint8_t  n;
  uint16_t i[FACE_MAX_V];
  uint32_t c; /* 0xRRGGBB */
} Face;

typedef struct Mesh {
  vec3  *v;
  Face  *f;
  int    nv, nf, capv, capf;
  float  rad;
  const struct Mesh *lod; /* far version, or NULL */
} Mesh;

enum { TGT_SAM, TGT_FUEL, TGT_RADAR, TGT_HANGAR, TGT_SHIP, TGT_RUBBLE, TGT_TYPES };

extern Mesh MESH_JET[2];
/* the DR-7 Vesper, the unmanned drone of Beat the Drone! (untinted) */
extern Mesh MESH_DRONE;
extern Mesh MESH_TANKER;
extern Mesh MESH_CARRIER; /* CVN-90, filled by the asset loader (empty without it) */
/* carrier turrets, built around their pivot with the barrels along +Z */
extern Mesh MESH_CIWS_BASE, MESH_CIWS_GUN, MESH_SAM_BASE, MESH_SAM_BOX;
/* the carrier's bridge interior (carrier-local, deck at 20.5) and a crewman */
extern Mesh MESH_BRIDGE, MESH_CREW;
extern Mesh MESH_TARGET[2][TGT_TYPES];

void mesh_reset(Mesh *m, float rad);
int  mesh_vert(Mesh *m, float x, float y, float z);
void mesh_face(Mesh *m, const int *idx, int n, uint32_t col);
void mesh_face3(Mesh *m, int a, int b, int c, uint32_t col);
void mesh_face4(Mesh *m, int a, int b, int c, int d, uint32_t col);
void mesh_init_builtin(void);
/* The mesh as canonical text, for golden checks: its radius and counts, then
 * every vertex (%.9g: a float exactly) and every face (corners, colour). */
void mesh_dump(const Mesh *m, FILE *f);

#endif
