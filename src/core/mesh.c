/* mesh.c - built-in meshes (JS jetMesh, targetMesh, box, cyl, MESH), later
 * .glb through our own loader. One colour per face, as in the JS. */
#include "core/mesh.h"

#include <stdlib.h>
#include <string.h>

#include "core/world.h"

Mesh MESH_JET[2];
Mesh MESH_DRONE;
Mesh MESH_TANKER;
Mesh MESH_CARRIER;
Mesh MESH_CIWS_BASE, MESH_CIWS_GUN, MESH_SAM_BASE, MESH_SAM_BOX;
Mesh MESH_BRIDGE, MESH_CREW;
Mesh MESH_TARGET[2][TGT_TYPES];

/* Keeps the arrays, so a mesh can be rebuilt without leaking. */
void mesh_reset(Mesh *m, float rad) {
  m->nv = m->nf = 0;
  m->rad = rad;
  m->lod = NULL;
}

int mesh_vert(Mesh *m, float x, float y, float z) {
  if (m->nv >= 65535) return m->nv - 1;
  if (m->nv == m->capv) {
    int cap = m->capv ? m->capv * 2 : 32;
    vec3 *v = realloc(m->v, (size_t)cap * sizeof *v);
    if (!v) abort();
    m->v = v;
    m->capv = cap;
  }
  m->v[m->nv] = v3(x, y, z);
  return m->nv++;
}

void mesh_face(Mesh *m, const int *idx, int n, uint32_t col) {
  if (n < 3 || n > FACE_MAX_V) return;
  for (int k = 0; k < n; k++)
    if (idx[k] < 0 || idx[k] >= m->nv) return;
  if (m->nf == m->capf) {
    int cap = m->capf ? m->capf * 2 : 32;
    Face *f = realloc(m->f, (size_t)cap * sizeof *f);
    if (!f) abort();
    m->f = f;
    m->capf = cap;
  }
  Face *f = &m->f[m->nf++];
  f->n = (uint8_t)n;
  for (int k = 0; k < n; k++) f->i[k] = (uint16_t)idx[k];
  f->c = col;
}
void mesh_face3(Mesh *m, int a, int b, int c, uint32_t col) {
  int i[3] = { a, b, c };
  mesh_face(m, i, 3, col);
}
void mesh_face4(Mesh *m, int a, int b, int c, int d, uint32_t col) {
  int i[4] = { a, b, c, d };
  mesh_face(m, i, 4, col);
}

static void box(Mesh *m, float x, float y, float z, float sx, float sy, float sz, uint32_t col, uint32_t top) {
  static const float D[8][3] = { { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 },
                                 { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } };
  int b = m->nv;
  for (int k = 0; k < 8; k++) mesh_vert(m, x + D[k][0] * sx, y + D[k][1] * sy, z + D[k][2] * sz);
  mesh_face4(m, b + 4, b + 5, b + 6, b + 7, top ? top : col);
  mesh_face4(m, b, b + 1, b + 5, b + 4, col);
  mesh_face4(m, b + 1, b + 2, b + 6, b + 5, col);
  mesh_face4(m, b + 2, b + 3, b + 7, b + 6, col);
  mesh_face4(m, b + 3, b, b + 4, b + 7, col);
}

static void cyl(Mesh *m, float x, float y, float z, float rad, float h, uint32_t col, uint32_t top) {
  enum { N = 8 };
  int b = m->nv;
  for (int i = 0; i < N; i++) {
    float a = (float)i / N * PI_F * 2;
    mesh_vert(m, x + cosf(a) * rad, y, z + sinf(a) * rad);
    mesh_vert(m, x + cosf(a) * rad, y + h, z + sinf(a) * rad);
  }
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    mesh_face4(m, b + 2 * i, b + 2 * j, b + 2 * j + 1, b + 2 * i + 1, col);
  }
  int t[N];
  for (int i = 0; i < N; i++) t[i] = b + 2 * i + 1;
  mesh_face(m, t, N, top);
}

static void jet_mesh(Mesh *m, uint32_t body, uint32_t acc) {
  static const float V[24][3] = {
    { 0, 0, 7 }, { 0, 1, 0 }, { -0.9f, 0, 0 }, { 0.9f, 0, 0 }, { 0, -0.8f, 0 }, { 0, 0.7f, -6 }, { -0.7f, 0, -6 }, { 0.7f, 0, -6 }, { 0, -0.5f, -6 },
    { -0.9f, 0, 2 }, { -6, 0, -4 }, { -0.9f, 0, -4.5f }, { 0.9f, 0, 2 }, { 6, 0, -4 }, { 0.9f, 0, -4.5f },
    { 0, 0.7f, -2.5f }, { 0, 3.4f, -6.4f }, { 0, 0.7f, -6 },
    { -0.7f, 0.1f, -4.6f }, { -2.8f, 0.1f, -6.6f }, { -0.7f, 0.1f, -6.4f }, { 0.7f, 0.1f, -4.6f }, { 2.8f, 0.1f, -6.6f }, { 0.7f, 0.1f, -6.4f },
  };
  const uint32_t glass = 0x1d2a44;
  mesh_reset(m, 8);
  for (int k = 0; k < 24; k++) mesh_vert(m, V[k][0], V[k][1], V[k][2]);
  mesh_face3(m, 0, 1, 3, glass); mesh_face3(m, 0, 3, 4, body); mesh_face3(m, 0, 4, 2, body); mesh_face3(m, 0, 2, 1, glass);
  mesh_face4(m, 1, 5, 7, 3, body); mesh_face4(m, 3, 7, 8, 4, body); mesh_face4(m, 4, 8, 6, 2, body); mesh_face4(m, 2, 6, 5, 1, body);
  mesh_face4(m, 5, 6, 8, 7, 0x2a2a2a);
  mesh_face3(m, 9, 10, 11, acc); mesh_face3(m, 12, 14, 13, acc); mesh_face3(m, 15, 16, 17, acc);
  mesh_face3(m, 18, 19, 20, body); mesh_face3(m, 21, 23, 22, body);
}

/* The DR-7 Vesper: an unmanned, tailless lifting body with a cranked kite
 * wing, small canards, two outward-canted fins and a square vectoring
 * nozzle. No canopy: a dorsal intake where a cockpit would be, and a sensor
 * eye glowing under the nose. About 12 m long and 11 m across. */
static void drone_mesh(Mesh *m) {
  const uint32_t body = 0x5d646e, wing = 0x50565f, acc = 0x767e88, dark = 0x1c1e22;
  const uint32_t eye = 0x40f0ff | FACE_GLOW, burn = 0xffa040 | FACE_GLOW;
  mesh_reset(m, 7);
  /* the body: a nose point and four diamond rings, each top, right, bottom, left */
  static const float RING[4][4] = { /* z, top, half width, bottom */
    { 3.6f, 0.75f, 1.0f, -0.55f }, { -1.0f, 0.95f, 1.5f, -0.6f }, { -4.6f, 0.5f, 0.9f, -0.4f }, { -5.6f, 0.4f, 0.6f, -0.35f },
  };
  int nose = mesh_vert(m, 0, 0, 6.6f), ring[4];
  for (int k = 0; k < 4; k++) {
    ring[k] = mesh_vert(m, 0, RING[k][1], RING[k][0]);
    mesh_vert(m, RING[k][2], 0.05f, RING[k][0]);
    mesh_vert(m, 0, RING[k][3], RING[k][0]);
    mesh_vert(m, -RING[k][2], 0.05f, RING[k][0]);
  }
  for (int i = 0; i < 4; i++) mesh_face3(m, nose, ring[0] + i, ring[0] + (i + 1) % 4, body);
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < 4; i++)
      mesh_face4(m, ring[k] + i, ring[k] + (i + 1) % 4, ring[k + 1] + (i + 1) % 4, ring[k + 1] + i, k == 2 ? dark : body);
  mesh_face4(m, ring[3], ring[3] + 1, ring[3] + 2, ring[3] + 3, dark);
  /* the nozzle's glow, just behind its mouth */
  int a = mesh_vert(m, 0, 0.22f, -5.62f), b = mesh_vert(m, 0.34f, 0.03f, -5.62f), c = mesh_vert(m, 0, -0.18f, -5.62f),
      d = mesh_vert(m, -0.34f, 0.03f, -5.62f);
  mesh_face4(m, a, b, c, d, burn);
  /* the sensor eye under the nose, and the flush intake on the back */
  a = mesh_vert(m, -0.3f, -0.36f, 4.9f), b = mesh_vert(m, 0.3f, -0.36f, 4.9f), c = mesh_vert(m, 0.38f, -0.46f, 4.2f),
  d = mesh_vert(m, -0.38f, -0.46f, 4.2f);
  mesh_face4(m, a, b, c, d, eye);
  a = mesh_vert(m, -0.45f, 0.86f, 0.6f), b = mesh_vert(m, 0.45f, 0.86f, 0.6f), c = mesh_vert(m, 0.55f, 0.99f, -0.6f),
  d = mesh_vert(m, -0.55f, 0.99f, -0.6f);
  mesh_face4(m, a, b, c, d, dark);
  for (int s = -1; s <= 1; s += 2) {
    float x = (float)s;
    /* the wing in two panels: inboard to the crank, then out to the tip */
    int w0 = mesh_vert(m, 1.0f * x, 0.02f, 3.0f), w1 = mesh_vert(m, 3.0f * x, 0.02f, -0.4f), w2 = mesh_vert(m, 3.0f * x, 0.02f, -3.4f),
        w3 = mesh_vert(m, 0.9f * x, 0.02f, -4.4f), w4 = mesh_vert(m, 5.4f * x, 0.02f, -3.0f), w5 = mesh_vert(m, 5.6f * x, 0.02f, -3.8f);
    mesh_face4(m, w0, w1, w2, w3, wing);
    mesh_face4(m, w1, w4, w5, w2, wing);
    /* a canard ahead of the wing, and a fin canted out */
    int c0 = mesh_vert(m, 0.9f * x, 0.18f, 3.9f), c1 = mesh_vert(m, 2.5f * x, 0.18f, 2.6f), c2 = mesh_vert(m, 2.4f * x, 0.18f, 2.2f),
        c3 = mesh_vert(m, 0.95f * x, 0.18f, 2.4f);
    mesh_face4(m, c0, c1, c2, c3, acc);
    int f0 = mesh_vert(m, 0.75f * x, 0.45f, -2.6f), f1 = mesh_vert(m, 2.1f * x, 2.2f, -3.9f), f2 = mesh_vert(m, 2.3f * x, 2.2f, -4.9f),
        f3 = mesh_vert(m, 0.8f * x, 0.45f, -4.7f);
    mesh_face4(m, f0, f1, f2, f3, acc);
  }
}

static void target_mesh(Mesh *m, int type, int team) {
  uint32_t tc = TEAM[team].bld;
  mesh_reset(m, type == TGT_HANGAR ? 50 : 30);
  if (type == TGT_SAM) {
    box(m, 0, 0, 0, 16, 3, 16, 0x6c6c6c, 0x7d7d7d);
    box(m, 0, 3, 0, 5, 4, 9, tc, 0);
    box(m, -3, 7, 0, 1.2f, 1.2f, 11, 0xe8e8e8, 0);
    box(m, 3, 7, 0, 1.2f, 1.2f, 11, 0xe8e8e8, 0);
  } else if (type == TGT_FUEL) {
    cyl(m, 0, 0, 0, 20, 18, 0xc9c9c9, tc);
  } else if (type == TGT_RADAR) {
    box(m, 0, 0, 0, 12, 16, 12, 0x959595, tc);
    box(m, 0, 16, 0, 1.5f, 8, 1.5f, 0x777777, 0);
    int b = m->nv;
    mesh_vert(m, -14, 22, 3); mesh_vert(m, 14, 22, 3); mesh_vert(m, 14, 34, -3); mesh_vert(m, -14, 34, -3);
    mesh_face4(m, b, b + 1, b + 2, b + 3, 0xe0e0e0);
  } else if (type == TGT_HANGAR) {
    box(m, 0, 0, 0, 40, 16, 28, 0x7a7a72, 0);
    int b = m->nv;
    mesh_vert(m, -40, 16, -28); mesh_vert(m, 40, 16, -28); mesh_vert(m, 40, 27, 0);
    mesh_vert(m, -40, 27, 0); mesh_vert(m, -40, 16, 28); mesh_vert(m, 40, 16, 28);
    mesh_face4(m, b, b + 1, b + 2, b + 3, tc);
    mesh_face4(m, b + 4, b + 5, b + 2, b + 3, tc);
    mesh_face3(m, b, b + 3, b + 4, 0x7a7a72);
    mesh_face3(m, b + 1, b + 2, b + 5, 0x7a7a72);
  } else if (type == TGT_SHIP) {
    /* frigate escort: hull, deckhouse, mast and a SAM launcher, waterline at 0 */
    mesh_reset(m, 60);
    int b = m->nv;
    mesh_vert(m, 0, 7, 58); mesh_vert(m, -7, 7, 30); mesh_vert(m, -7, 7, -50); mesh_vert(m, 7, 7, -50); mesh_vert(m, 7, 7, 30);
    mesh_vert(m, 0, -2, 54); mesh_vert(m, -6, -2, 28); mesh_vert(m, -6, -2, -48); mesh_vert(m, 6, -2, -48); mesh_vert(m, 6, -2, 28);
    int deck[5] = { b, b + 1, b + 2, b + 3, b + 4 };
    mesh_face(m, deck, 5, 0x6d7378);
    for (int k = 0; k < 5; k++) {
      int j = (k + 1) % 5;
      mesh_face4(m, b + k, b + j, b + 5 + j, b + 5 + k, 0x4a5056);
    }
    box(m, 0, 7, -8, 5, 9, 16, 0x8a9096, 0x9aa0a6);
    box(m, 0, 16, -6, 1, 14, 1, 0x50565c, 0);
    box(m, 0, 7, 20, 3, 3, 4, tc, 0);
    box(m, 0, 7, -40, 4, 4, 5, tc, 0);
  } else {
    box(m, 0, 0, 0, 18, 3, 16, 0x3a3a36, 0);
    box(m, 6, 0, -4, 6, 6, 5, 0x2a2a28, 0);
    box(m, -7, 0, 6, 4, 4, 4, 0x33302c, 0);
  }
}

/* ---- glowing screens for the bridge ------------------------------------- */
enum { SCR_RADAR, SCR_DATA, SCR_MAP, SCR_BARS, SCR_DECK, SCR_KEYS };

/* A flat rectangle: centre c, half-vectors r and u. */
static void rect3(Mesh *m, vec3 c, vec3 r, vec3 u, uint32_t col) {
  int b = m->nv;
  vec3 p[4] = { v3_sub(v3_sub(c, r), u), v3_sub(v3_add(c, r), u), v3_add(v3_add(c, r), u), v3_add(v3_sub(c, r), u) };
  for (int k = 0; k < 4; k++) mesh_vert(m, p[k].x, p[k].y, p[k].z);
  mesh_face4(m, b, b + 1, b + 2, b + 3, col);
}

/* A screen w x h at c, facing d (towards whoever reads it) and leaning back
 * by tilt radians, drawn as glowing shapes: a dark lit face, then the
 * picture a hair in front of it, in the ink colour. */
static void screen(Mesh *m, vec3 c, vec3 d, float tilt, float w, float h, int kind, uint32_t ink) {
  vec3 r, u;
  if (fabsf(d.y) > 0.9f) {
    r = v3(1, 0, 0);
    u = v3(0, 0, -1);
  } else {
    r = v3(-d.z, 0, d.x);
    u = v3_sub(v3(0, cosf(tilt), 0), v3_scale(d, sinf(tilt)));
  }
  vec3 n = v3_norm(v3_cross(r, u));
  if (v3_dot(n, d) < 0) n = v3_scale(n, -1);
  float hw = w / 2, hh = h / 2;
  uint32_t base = ((ink >> 16 & 255) / 9) << 16 | ((ink >> 8 & 255) / 9) << 8 | (ink & 255) / 9;
  uint32_t dim = ((ink >> 16 & 255) / 3) << 16 | ((ink >> 8 & 255) / 3) << 8 | (ink & 255) / 3;
  rect3(m, c, v3_scale(r, hw), v3_scale(u, hh), base | FACE_GLOW);
  vec3 o = v3_add(c, v3_scale(n, 0.012f));
#define AT(x, y) v3_add(v3_add(o, v3_scale(r, (x) * hw)), v3_scale(u, (y) * hh))
#define BAR(x0, y0, x1, y1, col) rect3(m, AT(((x0) + (x1)) / 2, ((y0) + (y1)) / 2), v3_scale(r, ((x1) - (x0)) / 2 * hw), \
                                       v3_scale(u, ((y1) - (y0)) / 2 * hh), (col) | FACE_GLOW)
  BAR(-0.94f, 0.78f, 0.94f, 0.92f, dim); /* title strip */
  switch (kind) {
  case SCR_RADAR: {
    /* range rings as octagons of short bars, a sweep and a few blips */
    float ar = hh / hw; /* keep the rings round on a wide screen */
    for (int ring = 1; ring <= 2; ring++) {
      float rr = 0.32f * ring;
      for (int i = 0; i < 12; i++) {
        float a = i * PI_F / 6, x = sinf(a) * rr * ar, y = cosf(a) * rr - 0.08f;
        BAR(x - 0.025f, y - 0.02f, x + 0.025f, y + 0.02f, ring == 2 ? dim : ink);
      }
    }
    for (int i = 0; i < 6; i++) BAR(-0.008f + i * 0.06f * ar, -0.1f + i * 0.09f, 0.03f + i * 0.06f * ar, -0.06f + i * 0.09f, ink);
    BAR(-0.4f * ar, 0.3f, -0.36f * ar, 0.36f, 0xff5a3c);
    BAR(0.25f * ar, -0.5f, 0.29f * ar, -0.44f, 0xff5a3c);
    BAR(-0.15f * ar, -0.35f, -0.11f * ar, -0.29f, ink);
    break;
  }
  case SCR_DATA:
    for (int i = 0; i < 6; i++) {
      float y = 0.6f - i * 0.24f, len = 0.35f + 0.5f * (float)((i * 37) % 10) / 10;
      BAR(-0.86f, y - 0.05f, -0.86f + len, y + 0.05f, i == 2 ? 0xffffff : ink);
      BAR(0.6f, y - 0.05f, 0.86f, y + 0.05f, dim);
    }
    break;
  case SCR_MAP:
    BAR(-0.7f, -0.3f, -0.1f, 0.35f, 0x2a6a3a);
    BAR(-0.5f, -0.6f, 0.1f, -0.25f, 0x2a6a3a);
    BAR(0.3f, 0.1f, 0.75f, 0.55f, 0x2a6a3a);
    for (int i = 0; i < 5; i++) BAR(-0.8f + i * 0.35f, -0.75f + i * 0.28f, -0.68f + i * 0.35f, -0.65f + i * 0.28f, ink);
    BAR(0.05f, -0.05f, 0.15f, 0.07f, 0xffffff);
    break;
  case SCR_BARS:
    for (int i = 0; i < 5; i++) {
      float x = -0.75f + i * 0.34f, top = -0.6f + 0.25f * (float)((i * 53) % 6);
      BAR(x, -0.75f, x + 0.2f, top, i == 3 ? 0xffb84a : ink);
    }
    break;
  case SCR_DECK:
    /* the flight deck seen from above, jets on it */
    BAR(-0.8f, -0.25f, 0.8f, 0.25f, dim);
    BAR(-0.8f, -0.02f, 0.8f, 0.02f, ink);
    for (int i = 0; i < 4; i++) BAR(-0.6f + i * 0.35f, 0.08f, -0.5f + i * 0.35f, 0.18f, 0xffffff);
    BAR(-0.85f, -0.7f, -0.3f, -0.55f, ink);
    BAR(0.2f, -0.7f, 0.85f, -0.55f, ink);
    break;
  case SCR_KEYS:
    for (int j = 0; j < 2; j++)
      for (int i = 0; i < 8; i++) BAR(-0.9f + i * 0.23f, -0.6f + j * 0.7f, -0.74f + i * 0.23f, -0.1f + j * 0.7f, (i + j) % 5 ? dim : ink);
    break;
  }
#undef BAR
#undef AT
}

void mesh_init_builtin(void) {
  for (int t = 0; t < 2; t++) {
    jet_mesh(&MESH_JET[t], TEAM[t].body, TEAM[t].acc);
    for (int k = 0; k < TGT_TYPES; k++) target_mesh(&MESH_TARGET[t][k], k, t);
  }
  jet_mesh(&MESH_TANKER, 0xa8a8a8, 0x6c6c6c);
  drone_mesh(&MESH_DRONE);
  /* CIWS: a pedestal and a white dome with a six-barrel gun */
  mesh_reset(&MESH_CIWS_BASE, 4);
  box(&MESH_CIWS_BASE, 0, -2, 0, 1.6f, 2, 1.6f, 0x6d7378, 0);
  mesh_reset(&MESH_CIWS_GUN, 4);
  cyl(&MESH_CIWS_GUN, 0, 0, -0.6f, 1.3f, 2.6f, 0xe4e6ea, 0xf0f2f4);
  box(&MESH_CIWS_GUN, 0, 0.9f, 1.0f, 0.25f, 0.5f, 2.6f, 0x3a3c40, 0);
  /* SAM: a turntable and an eight-cell launcher box */
  mesh_reset(&MESH_SAM_BASE, 5);
  box(&MESH_SAM_BASE, 0, -1.5f, 0, 2.2f, 1.5f, 2.2f, 0x6d7378, 0);
  mesh_reset(&MESH_SAM_BOX, 5);
  box(&MESH_SAM_BOX, 0, -1, -1.5f, 1.8f, 2, 3.2f, 0x8a9096, 0x5a6066);

  /* the bridge: 17 m above the deck in the island, a modern glass bridge.
   * Windows all round above a waist-high sill; a long forward console with
   * the tactical workstation in its middle, a row of overhead displays, the
   * air ops station facing the deck to port, the helm to starboard, a plot
   * table, the captain's chair and a big status wall aft. Screens glow. */
  {
    Mesh *m = &MESH_BRIDGE;
    const float F = 37.5f, X0 = 24, X1 = 33, Z0 = -50, Z1 = -34;
    const uint32_t BODY = 0x2a2f37, TOP = 0x15181d, TRIM = 0x3d4550, SEAT = 0x22262c;
    const vec3 AFT = v3(0, 0, -1), STBD = v3(1, 0, 0), PORT = v3(-1, 0, 0), UP = v3(0, 1, 0);
    mesh_reset(m, 70); /* drawn from the carrier origin, so the radius covers the offset */
    int b = m->nv;
    mesh_vert(m, X0, F, Z0); mesh_vert(m, X1, F, Z0); mesh_vert(m, X1, F, Z1); mesh_vert(m, X0, F, Z1);
    mesh_face4(m, b, b + 1, b + 2, b + 3, 0x30353d);
    box(m, (X0 + X1) / 2, F + 3, (Z0 + Z1) / 2, (X1 - X0) / 2 + 0.4f, 0.35f, (Z1 - Z0) / 2 + 0.4f, 0x1c1f24, 0x5a5f66);
    /* sills and slim window posts */
    box(m, (X0 + X1) / 2, F, Z1, (X1 - X0) / 2, 1.05f, 0.15f, 0x3a4048, 0x4a515a);
    box(m, (X0 + X1) / 2, F, Z0, (X1 - X0) / 2, 1.05f, 0.15f, 0x3a4048, 0x4a515a);
    box(m, X0, F, (Z0 + Z1) / 2, 0.15f, 1.05f, (Z1 - Z0) / 2, 0x3a4048, 0x4a515a);
    box(m, X1, F, (Z0 + Z1) / 2, 0.15f, 1.05f, (Z1 - Z0) / 2, 0x3a4048, 0x4a515a);
    for (float z = Z0; z <= Z1 + 0.1f; z += 4) { box(m, X0, F, z, 0.1f, 3, 0.1f, 0x1c1f24, 0); box(m, X1, F, z, 0.1f, 3, 0.1f, 0x1c1f24, 0); }
    for (float x = X0 + 3; x < X1; x += 3) { box(m, x, F, Z0, 0.1f, 3, 0.1f, 0x1c1f24, 0); box(m, x, F, Z1, 0.1f, 3, 0.1f, 0x1c1f24, 0); }
    /* ceiling light strips */
    for (float z = -47; z <= -37; z += 5) box(m, (X0 + X1) / 2, F + 2.93f, z, 3.2f, 0.04f, 0.06f, 0x8a94a8 | FACE_GLOW, 0);
    /* the forward console along the windows */
    box(m, (X0 + X1) / 2, F, -34.65f, 4.1f, 0.92f, 0.45f, BODY, TOP);
    box(m, (X0 + X1) / 2, F + 0.92f, -34.65f, 4.1f, 0.02f, 0.46f, 0x2a6a8a | FACE_GLOW, 0);
    const float fx[4] = { 25.2f, 26.4f, 30.6f, 31.8f };
    const int fk[4] = { SCR_MAP, SCR_DATA, SCR_DATA, SCR_BARS };
    for (int i = 0; i < 4; i++) screen(m, v3(fx[i], F + 1.1f, -34.75f), AFT, 0.45f, 0.5f, 0.32f, fk[i], i < 2 ? 0x40d8ff : 0x7cff9a);
    /* the tactical workstation in the middle, raised, two big screens */
    box(m, 28.5f, F, -35.3f, 1.25f, 1.0f, 0.4f, BODY, TOP);
    box(m, 28.5f, F + 1.1f, -35.08f, 1.3f, 0.46f, 0.06f, 0x1c1f24, 0);
    screen(m, v3(27.85f, F + 1.32f, -35.2f), AFT, 0.25f, 0.58f, 0.42f, SCR_RADAR, 0x5cff8a);
    screen(m, v3(29.15f, F + 1.32f, -35.2f), AFT, 0.25f, 0.58f, 0.42f, SCR_DATA, 0x40d8ff);
    screen(m, v3(28.5f, F + 1.02f, -35.55f), AFT, 1.2f, 0.9f, 0.2f, SCR_KEYS, 0x40d8ff);
    /* overhead displays hung from the roof above the windows */
    for (int i = 0; i < 5; i++) {
      float x = 25.3f + i * 1.6f;
      box(m, x, F + 2.6f, -34.9f, 0.03f, 0.4f, 0.03f, 0x1c1f24, 0);
      box(m, x, F + 2.2f, -34.9f, 0.62f, 0.42f, 0.05f, 0x1c1f24, 0);
      screen(m, v3(x, F + 2.41f, -34.96f), AFT, -0.25f, 0.56f, 0.36f, i == 2 ? SCR_RADAR : i % 2 ? SCR_BARS : SCR_DATA,
             i == 2 ? 0x5cff8a : i < 2 ? 0xffb84a : 0x40d8ff);
    }
    /* air ops to port, looking down on the deck */
    box(m, 24.75f, F, -43, 0.42f, 0.95f, 1.5f, BODY, TOP);
    box(m, 24.5f, F + 0.95f, -43, 0.12f, 0.55f, 1.5f, TRIM, 0);
    screen(m, v3(24.66f, F + 1.25f, -43.7f), STBD, 0.2f, 0.62f, 0.42f, SCR_DECK, 0xffb84a);
    screen(m, v3(24.66f, F + 1.25f, -42.3f), STBD, 0.2f, 0.62f, 0.42f, SCR_DATA, 0xffb84a);
    screen(m, v3(25.05f, F + 0.97f, -43), STBD, 1.25f, 0.5f, 1.2f, SCR_KEYS, 0xffb84a);
    /* the helm to starboard */
    box(m, 32.45f, F, -40, 0.38f, 0.95f, 1.1f, BODY, TOP);
    screen(m, v3(32.3f, F + 1.15f, -40.45f), PORT, 0.35f, 0.5f, 0.36f, SCR_MAP, 0x40d8ff);
    screen(m, v3(32.3f, F + 1.15f, -39.55f), PORT, 0.35f, 0.5f, 0.36f, SCR_BARS, 0x7cff9a);
    cyl(m, 32.0f, F + 0.95f, -40, 0.2f, 0.05f, 0x9aa0a8, 0x9aa0a8); /* the wheel */
    /* the plot table */
    box(m, 28.0f, F, -44.4f, 0.75f, 0.88f, 0.55f, BODY, 0);
    screen(m, v3(28.0f, F + 0.9f, -44.4f), UP, 0, 1.4f, 1.0f, SCR_MAP, 0x40d8ff);
    /* the captain's chair: pedestal, bucket seat, armrests with screens */
    cyl(m, 29.6f, F, -38.6f, 0.12f, 0.45f, 0x4a5058, 0);
    box(m, 29.6f, F + 0.45f, -38.6f, 0.32f, 0.12f, 0.32f, SEAT, 0x2c3138);
    box(m, 29.6f, F + 0.55f, -38.93f, 0.32f, 0.75f, 0.07f, SEAT, 0);
    box(m, 29.6f, F + 1.2f, -38.95f, 0.16f, 0.14f, 0.06f, SEAT, 0);
    box(m, 29.22f, F + 0.57f, -38.55f, 0.06f, 0.16f, 0.3f, SEAT, 0x30353d);
    box(m, 29.98f, F + 0.57f, -38.55f, 0.06f, 0.16f, 0.3f, SEAT, 0x30353d);
    screen(m, v3(29.98f, F + 0.75f, -38.35f), UP, 0, 0.1f, 0.14f, SCR_DATA, 0x40d8ff);
    screen(m, v3(29.22f, F + 0.75f, -38.35f), UP, 0, 0.1f, 0.14f, SCR_BARS, 0x7cff9a);
    /* the status wall aft, over the sill */
    box(m, 26.6f, F + 1.15f, -49.8f, 2.2f, 1.45f, 0.08f, 0x1c1f24, 0);
    screen(m, v3(25.5f, F + 1.9f, -49.7f), v3(0, 0, 1), 0, 2.0f, 1.25f, SCR_RADAR, 0x5cff8a);
    screen(m, v3(27.7f, F + 1.9f, -49.7f), v3(0, 0, 1), 0, 2.0f, 1.25f, SCR_DECK, 0xffb84a);
    /* the stair hatch */
    b = m->nv;
    mesh_vert(m, 30.6f, F + 0.02f, -49.4f); mesh_vert(m, 32.2f, F + 0.02f, -49.4f); mesh_vert(m, 32.2f, F + 0.02f, -47.4f); mesh_vert(m, 30.6f, F + 0.02f, -47.4f);
    mesh_face4(m, b, b + 1, b + 2, b + 3, 0x101214);
    box(m, 30.6f, F, -48.4f, 0.04f, 1.0f, 1.0f, 0xd2b020, 0);
  }
  /* a crewman, 1.8 m, facing +Z */
  mesh_reset(&MESH_CREW, 1.2f);
  box(&MESH_CREW, -0.13f, 0, 0, 0.1f, 0.85f, 0.12f, 0x2a3040, 0);
  box(&MESH_CREW, 0.13f, 0, 0, 0.1f, 0.85f, 0.12f, 0x2a3040, 0);
  box(&MESH_CREW, 0, 0.85f, 0, 0.26f, 0.65f, 0.15f, 0xb8a070, 0);
  box(&MESH_CREW, 0, 1.52f, 0, 0.11f, 0.25f, 0.12f, 0xd8b090, 0x3a3a40);
}
