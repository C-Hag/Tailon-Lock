/* stores.c - the missiles and what the jets carry, as drawn (stores.h).
 *
 * Our own missiles, two classes and two more bodies on the same pattern:
 *   Glint     a short infrared dogfight missile: dark seeker dome, canards behind it, four tail fins, a short
 *             hot boost (the heat-seekers)
 *   Farreach  a long radar-guided missile: longer and thicker, pale radome and a dark band behind it, long
 *             strakes, a longer burn (the radar missiles)
 *   Mattock   an air-to-ground missile: olive, broad wings
 *   Tidewolf  the bombers' anti-ship missile, a ramjet: four intakes along the rear body, a short booster,
 *             then sustained thrust
 * Light grey bodies, four cruciform fins in an X, a small flickering flame and a grey-white smoke trail that
 * lingers and fades. Far away a missile is a thin line with a dot of flame.
 *
 * The trails are kept here, from where the missiles were drawn, not from the sim's smoke (which puffs too far
 * apart at missile speed to read as a trail); the sim's missile puffs are left out of the picture.
 *
 * The stations come from the jet's own mesh: the wing's underside found by casting lines down through it at
 * four places along the span (the outermost a wingtip rail), and four belly stations either side of the keel.
 * Jets that carry their missiles inside have bays instead, whose doors open a moment on a launch. What is on
 * them is what the jet still has (heat, radar, air-to-ground counts, which the network play keeps for the other
 * computer's jet too), laid out from its full load, so a missile fired leaves its station empty until rearmed. */
#include "stores.h"
#include "surfaces.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/jets.h"
#include "core/world.h"
#include "gpu.h"

#define SCALE 1.0f /* life size: the jets' own models carry missiles this size */

/* ---- the models ---------------------------------------------------------------------------------- */
enum { MS_GLINT, MS_FARREACH, MS_MATTOCK, MS_TIDEWOLF, MS_COUNT };
typedef struct {
  float len, rad; /* body, m */
  float burn;     /* the boost: big flame and thick smoke for this long, then the sustainer */
  float flame;    /* the boost flame's length, m */
} MissileType;
static const MissileType MT[MS_COUNT] = {
  { 2.9f, 0.064f, 2.2f, 0.75f },
  { 3.7f, 0.090f, 4.5f, 0.95f },
  { 3.2f, 0.105f, 3.0f, 0.85f },
  { 4.2f, 0.110f, 0.6f, 0.80f },
};
static Mesh MM[MS_COUNT], FLAME, BOX, DOOR, CAVITY, WHEEL, STRUT;

#define C_BODY   0x9a9fa6u
#define C_FIN    0x8c9198
#define C_BAND   0x3a3e44
#define C_DOME   0x22262b
#define C_RADOME 0xe2e0d8
#define C_NOZZLE 0x292929
#define C_OLIVE  0x7f8a5a
#define C_PYLON  0x7a8087
#define C_DOOR   0x646a71
#define C_TYRE   0x1c1d1f
#define C_HUB    0x9da3aa
#define C_GEAR   0xc4c8cc

#define SIDES 8
typedef struct { float z, r; uint32_t c; } Ring; /* a ring of the body, and the colour from the last one to it */

static void body(Mesh *m, float tip, const Ring *rg, int n) {
  int prev[SIDES], cur[SIDES], t = mesh_vert(m, 0, 0, tip);
  for (int k = 0; k < n; k++) {
    for (int i = 0; i < SIDES; i++) {
      float a = (i + 0.5f) * 2 * PI_F / SIDES;
      cur[i] = mesh_vert(m, rg[k].r * cosf(a), rg[k].r * sinf(a), rg[k].z);
    }
    for (int i = 0; i < SIDES; i++) {
      int j = (i + 1) % SIDES;
      if (!k) mesh_face3(m, t, cur[j], cur[i], rg[k].c);
      else mesh_face4(m, prev[i], prev[j], cur[j], cur[i], rg[k].c);
    }
    memcpy(prev, cur, sizeof prev);
  }
  mesh_face(m, prev, SIDES, C_NOZZLE); /* the nozzle's end */
}

/* Four fins in an X: root from z0 back to z1 on a body of radius r, span s beyond it, the tip's leading edge
 * swept back by sw. */
static void fins(Mesh *m, float r, float z0, float z1, float s, float sw, uint32_t c) {
  for (int k = 0; k < 4; k++) {
    float a = (k * 2 + 1) * PI_F / 4, x = cosf(a), y = sinf(a), tz = fmaxf(z0 - sw, z1 + 0.02f);
    mesh_face4(m, mesh_vert(m, x * r, y * r, z0), mesh_vert(m, x * (r + s), y * (r + s), tz),
               mesh_vert(m, x * (r + s), y * (r + s), z1), mesh_vert(m, x * r, y * r, z1), c);
  }
}

/* An ogive nose from the tip at z back over len to the full radius r: rings at quarters. */
static int ogive(Ring *o, float z, float len, float r, uint32_t c) {
  for (int k = 1; k <= 4; k++) {
    float t = k / 4.0f;
    o[k - 1] = (Ring){ z - len * t, r * (2 * t - t * t), c };
  }
  return 4;
}

static void unit_box(Mesh *m, uint32_t c) {
  int v[8];
  for (int i = 0; i < 8; i++) v[i] = mesh_vert(m, i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
  static const int F[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
  for (int f = 0; f < 6; f++) mesh_face4(m, v[F[f][0]], v[F[f][1]], v[F[f][2]], v[F[f][3]], c);
}

static void build(void) {
  Ring rg[16];
  int n;
  /* Glint */
  Mesh *m = &MM[MS_GLINT];
  float R = MT[MS_GLINT].rad, h = MT[MS_GLINT].len / 2;
  mesh_reset(m, h + 0.05f);
  n = 0;
  rg[n++] = (Ring){ h - 0.015f, 0.42f * R, C_DOME };
  rg[n++] = (Ring){ h - 0.05f, 0.75f * R, C_DOME };
  rg[n++] = (Ring){ h - 0.11f, 0.94f * R, C_DOME };
  rg[n++] = (Ring){ h - 0.17f, R, C_DOME };
  rg[n++] = (Ring){ h - 0.38f, R, C_BAND };
  rg[n++] = (Ring){ -h + 0.08f, R, C_BODY };
  rg[n++] = (Ring){ -h + 0.02f, 0.84f * R, C_BODY };
  rg[n++] = (Ring){ -h, 0.64f * R, C_NOZZLE };
  body(m, h, rg, n);
  fins(m, R, h - 0.48f, h - 0.62f, 0.075f, 0.04f, C_FIN);    /* canards */
  fins(m, R, -h + 0.42f, -h + 0.05f, 0.19f, 0.22f, C_FIN);   /* tail */
  /* Farreach */
  m = &MM[MS_FARREACH];
  R = MT[MS_FARREACH].rad, h = MT[MS_FARREACH].len / 2;
  mesh_reset(m, h + 0.05f);
  n = ogive(rg, h, 0.62f, R, C_RADOME);
  rg[n++] = (Ring){ h - 0.78f, R, C_BAND };
  rg[n++] = (Ring){ -h + 0.08f, R, C_BODY };
  rg[n++] = (Ring){ -h + 0.02f, 0.85f * R, C_BODY };
  rg[n++] = (Ring){ -h, 0.66f * R, C_NOZZLE };
  body(m, h, rg, n);
  fins(m, R, 0.75f, -0.30f, 0.055f, 0.6f, C_FIN);            /* strakes */
  fins(m, R, -h + 0.55f, -h + 0.05f, 0.23f, 0.30f, C_FIN);   /* tail */
  /* Mattock */
  m = &MM[MS_MATTOCK];
  R = MT[MS_MATTOCK].rad, h = MT[MS_MATTOCK].len / 2;
  mesh_reset(m, h + 0.1f);
  n = 0;
  rg[n++] = (Ring){ h - 0.03f, 0.6f * R, C_DOME };
  rg[n++] = (Ring){ h - 0.09f, 0.9f * R, C_DOME };
  rg[n++] = (Ring){ h - 0.16f, R, C_DOME };
  rg[n++] = (Ring){ h - 0.32f, R, C_BAND };
  rg[n++] = (Ring){ -h + 0.08f, R, C_OLIVE };
  rg[n++] = (Ring){ -h + 0.02f, 0.85f * R, C_OLIVE };
  rg[n++] = (Ring){ -h, 0.66f * R, C_NOZZLE };
  body(m, h, rg, n);
  fins(m, R, 0.35f, -0.45f, 0.26f, 0.30f, C_OLIVE);          /* wings */
  fins(m, R, -h + 0.45f, -h + 0.05f, 0.16f, 0.18f, C_OLIVE); /* tail */
  /* Tidewolf */
  m = &MM[MS_TIDEWOLF];
  R = MT[MS_TIDEWOLF].rad, h = MT[MS_TIDEWOLF].len / 2;
  mesh_reset(m, h + 0.05f);
  n = ogive(rg, h, 0.7f, R, C_BAND);
  rg[n++] = (Ring){ -h + 0.08f, R, C_BODY };
  rg[n++] = (Ring){ -h + 0.02f, 0.85f * R, C_BODY };
  rg[n++] = (Ring){ -h, 0.7f * R, C_NOZZLE };
  body(m, h, rg, n);
  for (int k = 0; k < 4; k++) { /* the intakes: a shroud along the rear body, open at the front */
    float a = (k * 2 + 1) * PI_F / 4, cx = cosf(a), cy = sinf(a), tx = -cy, ty = cx;
    float r0 = R * 0.92f, r1 = R + 0.075f, w = 0.05f, z0 = 0.1f, z1 = -h + 0.45f;
    int q[2][2][2]; /* [front/back][inner/outer][side] */
    for (int zi = 0; zi < 2; zi++)
      for (int ri = 0; ri < 2; ri++)
        for (int si = 0; si < 2; si++) {
          float rr = ri ? r1 : r0, ss = si ? w : -w;
          q[zi][ri][si] = mesh_vert(m, cx * rr + tx * ss, cy * rr + ty * ss, zi ? z1 : z0 - 0.08f * ri);
        }
    mesh_face4(m, q[0][1][0], q[0][1][1], q[1][1][1], q[1][1][0], C_BODY);   /* outer */
    mesh_face4(m, q[0][0][0], q[0][1][0], q[1][1][0], q[1][0][0], C_FIN);    /* sides */
    mesh_face4(m, q[0][0][1], q[1][0][1], q[1][1][1], q[0][1][1], C_FIN);
    mesh_face4(m, q[0][0][0], q[0][0][1], q[0][1][1], q[0][1][0], C_NOZZLE); /* the inlet */
  }
  fins(m, R, -h + 0.42f, -h + 0.04f, 0.15f, 0.2f, C_FIN);
  /* the flame: a cone 1 m long behind its base, drawn at its length */
  mesh_reset(&FLAME, 1);
  int base[6], tip = mesh_vert(&FLAME, 0, 0, -1);
  for (int i = 0; i < 6; i++) base[i] = mesh_vert(&FLAME, 0.075f * cosf(i * PI_F / 3), 0.075f * sinf(i * PI_F / 3), 0);
  for (int i = 0; i < 6; i++) mesh_face3(&FLAME, tip, base[(i + 1) % 6], base[i], FACE_GLOW | 0xffc66a);
  mesh_face(&FLAME, base, 6, FACE_GLOW | 0xfff3d0);
  mesh_reset(&BOX, 1.5f);
  unit_box(&BOX, C_PYLON);
  mesh_reset(&DOOR, 3); /* x 0 to 1 from the hinge, z -0.5 to 0.5 */
  mesh_face4(&DOOR, mesh_vert(&DOOR, 0, 0, -0.5f), mesh_vert(&DOOR, 1, 0, -0.5f), mesh_vert(&DOOR, 1, 0, 0.5f),
             mesh_vert(&DOOR, 0, 0, 0.5f), C_DOOR);
  mesh_reset(&CAVITY, 3);
  mesh_face4(&CAVITY, mesh_vert(&CAVITY, -0.5f, 0, -0.5f), mesh_vert(&CAVITY, 0.5f, 0, -0.5f),
             mesh_vert(&CAVITY, 0.5f, 0, 0.5f), mesh_vert(&CAVITY, -0.5f, 0, 0.5f), 0x16181b);
  /* the landing gear: a wheel (tyre round the tread, a pale hub on each side) 1 across and 1 wide about the x
   * axis, and a strut, a box in the gear's paint */
  mesh_reset(&WHEEL, 1);
  enum { WN = 10 };
  int side[2][WN];
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < WN; i++) {
      float a = (i + 0.5f) * 2 * PI_F / WN;
      side[k][i] = mesh_vert(&WHEEL, k ? 0.5f : -0.5f, 0.5f * cosf(a), 0.5f * sinf(a));
    }
  for (int i = 0; i < WN; i++) {
    int j = (i + 1) % WN;
    mesh_face4(&WHEEL, side[0][i], side[0][j], side[1][j], side[1][i], C_TYRE);
  }
  int rim[WN];
  for (int i = 0; i < WN; i++) rim[i] = side[1][i];
  mesh_face(&WHEEL, rim, 8, C_HUB); /* FACE_MAX_V corners: a hub of eight of the ten */
  for (int i = 0; i < WN; i++) rim[i] = side[0][WN - 1 - i];
  mesh_face(&WHEEL, rim, 8, C_HUB);
  mesh_reset(&STRUT, 1.5f);
  unit_box(&STRUT, C_GEAR);
}

static void models(void) {
  static bool built;
  if (!built) build();
  built = true;
}

/* which body a missile has */
static int missile_type(const Missile *m) {
  if (m->ashm) return MS_TIDEWOLF;
  if (m->sam || m->kind == MK_RADAR) return MS_FARREACH;
  return m->kind == MK_GROUND ? MS_MATTOCK : MS_GLINT;
}

static void draw_model(Framebuffer *fb, const View *V, int type, vec3 centre, vec3 r, vec3 u, vec3 f) {
  r3d_gloss(0.45f);
  r3d_mesh(fb, V, &MM[type], centre, r, u, f, SCALE, 1);
  r3d_gloss(-1);
}

/* a box from its centre and its three edges (local axes scaled) */
static void box(Framebuffer *fb, const View *V, vec3 c, vec3 r, vec3 u, vec3 f, float w, float h, float l) {
  r3d_mesh(fb, V, &BOX, c, v3_scale(r, w), v3_scale(u, h), v3_scale(f, l), 1, 1);
}

static uint32_t hash(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ b * 2246822519u;
  h ^= h >> 15;
  h *= 2246822519u;
  return h ^ h >> 13;
}

/* ---- the stations of each jet --------------------------------------------------------------------- */
enum { ST_RAIL, ST_PYLON, ST_BELLY, ST_BAY };
#define NST 24
typedef struct {
  vec3    at;   /* where a radar missile's centre sits (jet-local); a thinner one sits as close under the wing */
  float   wing; /* the pylon's top: the wing's underside above it */
  uint8_t kind;
  bool    pref; /* false: an outboard station, for heat-seekers; true: inboard or under the belly, for the rest */
  bool    baked; /* the jet's own model has a missile there (on its own rail or pylon) */
} Station;
typedef struct {
  bool    done, bay;
  int     n, ord[3][NST]; /* each class's stations in loading order */
  Station s[NST];
  float   bay_y, bay_hw, bay_z, bay_len; /* the bay: belly height, half width, centre, length */
  float   rad;
  /* the missiles the jet's model carries: their bodies' boxes and paint, left out of the jet as drawn */
  int      nbody;
  vec3     bmin[NST], bmax[NST];
  int      bface; /* a face of one (team colours: its paint in the other team's copy) */
  /* the engines' nozzle exits: centre, and half width and height of the opening */
  int      nnoz;
  vec3     noz[4];
  float    nozw[4], nozh[4];
  /* the landing gear: the legs' hinges (nose, left main, right main) under the belly and the wings' roots, each
   * leg's length to the axle, and the wheel's size; the wheels stand on the ground at y -3 (where the sim sets
   * a jet on the wheels) */
  vec3     hinge[3];
  float    leg[3], wheel[3];
} Layout;
static Layout LAY[MAX_JETS];
static Mesh   BARE[MAX_JETS][2], BARE_LOD[MAX_JETS][2];

/* The lowest face over the vertical line at (x, z). */
static bool under(const Mesh *M, float x, float z, float *y) {
  bool hit = false;
  for (int k = 0; k < M->nf; k++) {
    const Face *fc = &M->f[k];
    vec3 a = M->v[fc->i[0]];
    for (int t = 1; t + 1 < fc->n; t++) {
      vec3 b = M->v[fc->i[t]], c = M->v[fc->i[t + 1]];
      float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
      if (fabsf(d) < 1e-9f) continue;
      float l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
      float l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d, l3 = 1 - l1 - l2;
      if (l1 < 0 || l2 < 0 || l3 < 0) continue;
      float yy = l1 * a.y + l2 * b.y + l3 * c.y;
      if (!hit || yy < *y) *y = yy;
      hit = true;
    }
  }
  return hit;
}

/* The wing's chord across the line at x: the longest run of the mesh over it, front to back. */
static bool chord(const Mesh *M, float x, float z0, float z1, float *mid, float *len) {
  const int N = 200;
  float dz = (z1 - z0) / N, y, best = 0, run0 = 0;
  bool in = false;
  for (int i = 0; i <= N + 1; i++) {
    float z = z0 + dz * i;
    bool h = i <= N && under(M, x, z, &y);
    if (h && !in) run0 = z;
    if (!h && in && z - run0 > best) {
      best = z - run0;
      *mid = (run0 + z - dz) / 2;
    }
    in = h;
  }
  *len = best;
  return best > 0.3f;
}

static bool bay_jet(int j) {
  static const char *const BAY[] = { "af40", "sy26", "ks55" };
  if (j == DRONE_JET) return true;
  for (int k = 0; k < 3; k++)
    if (!strcmp(JETS[j].id, BAY[k])) return true;
  return false;
}

static int find(int *par, int x) {
  while (par[x] != x) x = par[x] = par[par[x]];
  return x;
}

static float luma(uint32_t c) { return 0.3f * ((c >> 16) & 255) + 0.59f * ((c >> 8) & 255) + 0.11f * (c & 255); }

/* The missiles modelled on the jet: bodies (a part of one colour, long and thin, of 30 faces or more) in the
 * brightest paint of all such parts; the rails and pylons under them are darker or smaller. */
static void find_bodies(Layout *L, const Mesh *M) {
  int *par = malloc(sizeof(int) * (size_t)M->nv);
  if (!par) return;
  for (int i = 0; i < M->nv; i++) par[i] = i;
  for (int k = 0; k < M->nf; k++)
    for (int i = 1; i < M->f[k].n; i++) par[find(par, M->f[k].i[i])] = find(par, M->f[k].i[0]);
  typedef struct { vec3 lo, hi; int nf; uint32_t c; bool one; } Part;
  Part *pt = calloc((size_t)M->nv, sizeof *pt);
  if (!pt) {
    free(par);
    return;
  }
  for (int k = 0; k < M->nf; k++) {
    const Face *fc = &M->f[k];
    Part *q = &pt[find(par, fc->i[0])];
    if (!q->nf) {
      q->lo = q->hi = M->v[fc->i[0]];
      q->c = fc->c;
      q->one = true;
    }
    q->nf++;
    q->one &= fc->c == q->c;
    for (int i = 0; i < fc->n; i++) {
      vec3 v = M->v[fc->i[i]];
      q->lo = v3(fminf(q->lo.x, v.x), fminf(q->lo.y, v.y), fminf(q->lo.z, v.z));
      q->hi = v3(fmaxf(q->hi.x, v.x), fmaxf(q->hi.y, v.y), fmaxf(q->hi.z, v.z));
    }
  }
  float best = 0;
  L->bface = -1;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < M->nv; i++) {
      const Part *q = &pt[i];
      vec3 d = v3_sub(q->hi, q->lo);
      if (q->nf < 30 || !q->one || d.z < 1.5f || d.z > 5 || d.x > 0.35f || d.y > 0.35f) continue;
      if (!pass) best = fmaxf(best, luma(q->c));
      else if (luma(q->c) >= best * 0.9f && L->nbody < NST) {
        L->bmin[L->nbody] = q->lo;
        L->bmax[L->nbody++] = q->hi;
        for (int k = 0; k < M->nf && L->bface < 0; k++)
          if (find(par, M->f[k].i[0]) == i) L->bface = k;
      }
    }
  free(pt);
  free(par);
}

/* A face of a modelled missile: its body's paint, every corner near the body (the fins stand out from it). */
static bool body_face(const Layout *L, const Mesh *M, const Face *fc, uint32_t paint) {
  if (fc->c != paint) return false;
  for (int b = 0; b < L->nbody; b++) {
    bool in = true;
    for (int i = 0; i < fc->n && in; i++) {
      vec3 v = M->v[fc->i[i]];
      in = v.x > L->bmin[b].x - 0.4f && v.x < L->bmax[b].x + 0.4f && v.y > L->bmin[b].y - 0.4f &&
           v.y < L->bmax[b].y + 0.4f && v.z > L->bmin[b].z - 0.1f && v.z < L->bmax[b].z + 0.1f;
    }
    if (in) return true;
  }
  return false;
}

/* (cut: the jet whose control surfaces leave the copy, drawn on their own by surfaces.c; -1: none, the far model) */
static void bare_copy(const Layout *L, Mesh *dst, const Mesh *src, uint32_t paint, int cut) {
  mesh_reset(dst, src->rad);
  for (int i = 0; i < src->nv; i++) mesh_vert(dst, src->v[i].x, src->v[i].y, src->v[i].z);
  for (int k = 0; k < src->nf; k++) {
    const Face *fc = &src->f[k];
    if (L->nbody && body_face(L, src, fc, paint)) continue;
    if (cut >= 0 && surfaces_cut(cut, k)) continue;
    int idx[FACE_MAX_V];
    for (int i = 0; i < fc->n; i++) idx[i] = fc->i[i];
    mesh_face(dst, idx, fc->n, fc->c);
  }
}

/* The nozzle exits: flat discs of the model's dark nozzle paint (which the team colours leave alone), facing
 * aft, at the very back of the jet; a disc further forward is a fairing between the engines. */
static void find_nozzles(Layout *L, const Mesh *M, float zback) {
  int *par = malloc(sizeof(int) * (size_t)M->nv);
  if (!par) return;
  for (int i = 0; i < M->nv; i++) par[i] = i;
  for (int k = 0; k < M->nf; k++)
    if (luma(M->f[k].c & 0xffffff) < 45 && !(M->f[k].c & FACE_GLOW))
      for (int i = 1; i < M->f[k].n; i++) par[find(par, M->f[k].i[i])] = find(par, M->f[k].i[0]);
  vec3 lo[16], hi[16];
  int root[16], n = 0;
  for (int k = 0; k < M->nf; k++) {
    const Face *fc = &M->f[k];
    if (luma(fc->c & 0xffffff) >= 45 || (fc->c & FACE_GLOW)) continue;
    int r = find(par, fc->i[0]), q;
    for (q = 0; q < n && root[q] != r; q++) {}
    if (q == n) {
      if (n == 16) continue;
      root[n] = r;
      lo[n] = hi[n] = M->v[fc->i[0]];
      n++;
    }
    for (int i = 0; i < fc->n; i++) {
      vec3 v = M->v[fc->i[i]];
      lo[q] = v3(fminf(lo[q].x, v.x), fminf(lo[q].y, v.y), fminf(lo[q].z, v.z));
      hi[q] = v3(fmaxf(hi[q].x, v.x), fmaxf(hi[q].y, v.y), fmaxf(hi[q].z, v.z));
    }
  }
  float back = 1e9f;
  bool disc[16];
  for (int q = 0; q < n; q++) {
    vec3 d = v3_sub(hi[q], lo[q]);
    float ratio = d.y > 0.01f ? d.x / d.y : 99;
    disc[q] = d.z < 0.05f && lo[q].z < zback + 2.5f && d.x > 0.3f && ratio > 0.5f && ratio < 2.2f;
    if (disc[q]) back = fminf(back, lo[q].z);
  }
  for (int q = 0; q < n && L->nnoz < 4; q++)
    if (disc[q] && lo[q].z < back + 0.3f) {
      L->noz[L->nnoz] = v3((lo[q].x + hi[q].x) / 2, (lo[q].y + hi[q].y) / 2, lo[q].z);
      L->nozw[L->nnoz] = (hi[q].x - lo[q].x) / 2;
      L->nozh[L->nnoz++] = (hi[q].y - lo[q].y) / 2;
    }
  free(par);
}

static void add_st(Layout *L, vec3 at, float wing, int kind, bool pref, bool baked) {
  for (int i = 0; i < L->n; i++) /* where the model already has a missile, it stays the station */
    if (L->s[i].baked && fabsf(L->s[i].at.x - at.x) < 0.7f && fabsf(L->s[i].at.z - at.z) < 2.5f && fabsf(L->s[i].at.y - at.y) < 1)
      return;
  if (L->n >= NST) return;
  L->s[L->n++] = (Station){ at, wing, (uint8_t)kind, pref, baked };
}

static void lay_out(int j) {
  Layout *L = &LAY[j];
  const Mesh *M = &JETS[j].mesh[0];
  memset(L, 0, sizeof *L);
  L->done = true;
  if (!M->nv) return;
  float S = 0, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < M->nv; i++) {
    S = fmaxf(S, fabsf(M->v[i].x));
    z0 = fminf(z0, M->v[i].z);
    z1 = fmaxf(z1, M->v[i].z);
  }
  float zc = (z0 + z1) / 2, Rr = MT[MS_FARREACH].rad * SCALE, yb = 0;
  L->rad = Rr;
  find_nozzles(L, M, z0);
  /* the belly: its lowest line at the middle, and how wide it stays near that low */
  if (!under(M, 0, zc, &yb)) yb = 0;
  float hw = 0.3f, y;
  for (float x = 0.05f; x < S * 0.3f; x += 0.05f) {
    if (!under(M, x, zc, &y) || y > yb + 0.25f) break;
    hw = x;
  }
  hw = clampf(hw, 0.35f, 1.1f);
  L->bay = bay_jet(j);
  L->bay_y = yb;
  L->bay_hw = hw;
  L->bay_z = zc;
  L->bay_len = fminf(5.2f, (z1 - z0) * 0.3f);
  if (L->bay) {
    /* inside: a side bay each side for a heat-seeker (0-1), then the main bay four across on two levels (2-9:
     * the upper level, staggered forward, then the lower, which its trapeze lowers further); wing pylons after
     * them (10-), only for what the bays cannot hold. For a bay station, wing is how much further it drops. */
    for (int k = 0; k < 2; k++)
      add_st(L, v3((k ? 1 : -1) * (hw + 0.08f), yb + 0.3f, zc + 0.9f), 0, ST_BAY, false, false);
    for (int k = 0; k < 8; k++) {
      static const float AX[4] = { -0.62f, 0.62f, -0.22f, 0.22f };
      int lower = k >= 4;
      add_st(L, v3(AX[k % 4] * hw * 0.85f, yb, zc + (lower ? -0.35f : 0.35f)), lower ? 0.38f : 0, ST_BAY, true, false);
    }
  }
  if (!L->bay) {
    /* the missiles the model carries first: the long ones for radar missiles, outboard first, in pairs */
    find_bodies(L, M);
    int ob[NST];
    for (int i = 0; i < L->nbody; i++) ob[i] = i;
    for (int a = 1; a < L->nbody; a++) /* insertion sort: outboard first, then front first, then left first */
      for (int b = a; b > 0; b--) {
        vec3 p = v3_scale(v3_add(L->bmin[ob[b - 1]], L->bmax[ob[b - 1]]), 0.5f), q = v3_scale(v3_add(L->bmin[ob[b]], L->bmax[ob[b]]), 0.5f);
        float dp = fabsf(p.x) - fabsf(q.x);
        bool swap = dp < -0.05f || (fabsf(dp) < 0.05f && (q.z > p.z + 0.05f || (fabsf(q.z - p.z) <= 0.05f && q.x < p.x)));
        if (!swap) break;
        int t = ob[b];
        ob[b] = ob[b - 1];
        ob[b - 1] = t;
      }
    for (int i = 0; i < L->nbody; i++) {
      vec3 lo = L->bmin[ob[i]], hi = L->bmax[ob[i]];
      add_st(L, v3_scale(v3_add(lo, hi), 0.5f), hi.y, ST_PYLON, hi.z - lo.z > 3.3f, true);
    }
  }
  {
    /* then ours, where it has none: wingtip rails and wing pylons outboard, the belly and the inner pylons (a jet
     * with bays: only the wing pylons, for what its bays cannot hold) */
    static const float FR[4] = { 0.92f, 0.72f, 0.52f, 0.34f };
    for (int w = 0; w < 4; w++)
      for (int side = -1; side <= 1; side += 2) {
        float x = FR[w] * S * side, mid = 0, len = 0, wy = 0;
        if (w == 3) break; /* the inner pylons last, after the belly */
        if (!chord(M, x, z0, z1, &mid, &len) || !under(M, x, mid, &wy)) continue;
        float hang = w ? 0.24f : 0.05f;
        add_st(L, v3(x, wy - hang - Rr, mid - 0.1f * len), wy, w ? ST_PYLON : ST_RAIL, false, false);
      }
    for (int k = 0; k < 4 && !L->bay; k++) { /* semi-recessed under the belly, fore and aft pairs */
      float x = 0.55f * hw * (k & 1 ? 1 : -1), z = zc + (k < 2 ? 1.7f : -1.7f), by;
      if (under(M, x, z, &by)) add_st(L, v3(x, by - Rr * 0.55f, z), by, ST_BELLY, true, false);
    }
    for (int side = -1; side <= 1; side += 2) {
      float x = FR[3] * S * side, mid = 0, len = 0, wy = 0;
      if (chord(M, x, z0, z1, &mid, &len) && under(M, x, mid, &wy))
        add_st(L, v3(x, wy - 0.24f - Rr, mid - 0.1f * len), wy, ST_PYLON, true, false);
    }
  }
  /* the gear: the nose leg a fifth of the length back from the tip, the mains just behind the middle and just
   * outside the belly (clear of its stations and of a bay) */
  float len = z1 - z0, xm = fminf(hw + 0.55f, fmaxf(0.8f, S * 0.28f));
  vec3 gp[3] = { v3(0, 0, z1 - 0.21f * len), v3(-xm, 0, zc - 0.07f * len), v3(xm, 0, zc - 0.07f * len) };
  for (int k = 0; k < 3; k++) {
    float gy = yb;
    if (!under(M, gp[k].x, gp[k].z, &gy)) gy = yb;
    gp[k].y = gy;
    L->wheel[k] = k ? 0.78f : 0.6f;
    L->leg[k] = fmaxf(0.4f, gy + 3 - L->wheel[k] / 2);
    L->hinge[k] = gp[k];
  }
  /* loading orders: heat-seekers outboard first, then the inboard stations from the last; radar missiles the
   * other way round; air-to-ground missiles the wing pylons from the root out, where heavy stores hang. A jet with bays fills them before its
   * pylons: heat-seekers the side bays, then the main bay from its outer end; radar and air-to-ground missiles
   * the main bay from the keel; the pylons, outboard first for heat-seekers, after them */
  int n[3] = { 0, 0, 0 };
  if (L->bay) {
    static const int HEAT[] = { 0, 1, 6, 7, 8, 9, 2, 3, 4, 5 }, MAIN[] = { 4, 5, 2, 3, 8, 9, 6, 7, 0, 1 };
    for (int c = 0; c < 3; c++) {
      for (int i = 0; i < 10 && i < L->n; i++) L->ord[c][n[c]++] = c ? MAIN[i] : HEAT[i];
      if (c)
        for (int i = L->n - 1; i >= 10; i--) L->ord[c][n[c]++] = i;
      else
        for (int i = 10; i < L->n; i++) L->ord[c][n[c]++] = i;
    }
  } else {
  for (int i = 0; i < L->n; i++)
    if (!L->s[i].pref) L->ord[0][n[0]++] = i;
  for (int i = L->n - 1; i >= 0; i--)
    if (L->s[i].pref) L->ord[0][n[0]++] = i;
  for (int i = 0; i < L->n; i++)
    if (L->s[i].pref) L->ord[1][n[1]++] = i;
  for (int i = L->n - 1; i >= 0; i--)
    if (!L->s[i].pref) L->ord[1][n[1]++] = i;
  /* air-to-ground missiles: the wing pylons from the root out (not the tip rails), in pairs; then the rest */
  bool taken[NST] = { false };
  for (;;) {
    int best = -1;
    for (int i = 0; i < L->n; i++) {
      const Station *t = &L->s[i];
      if (taken[i] || t->kind != ST_PYLON || fabsf(t->at.x) < hw + 0.6f) continue;
      if (best < 0 || fabsf(t->at.x) < fabsf(L->s[best].at.x) - 0.05f ||
          (fabsf(fabsf(t->at.x) - fabsf(L->s[best].at.x)) <= 0.05f && t->at.x < L->s[best].at.x))
        best = i;
    }
    if (best < 0) break;
    taken[best] = true;
    L->ord[2][n[2]++] = best;
  }
  for (int i = 0; i < L->n; i++)
    if (L->s[i].pref && !taken[i]) L->ord[2][n[2]++] = i;
  for (int i = L->n - 1; i >= 0; i--)
    if (!L->s[i].pref && !taken[i]) L->ord[2][n[2]++] = i;
  }
  /* the jet as drawn: its model without the missiles it carries (team paint: the same face's in each copy) */
  for (int t = 0; t < 2; t++) {
    const JetType *jt = &JETS[j];
    uint32_t paint = L->nbody && L->bface < jt->mesh[t].nf ? jt->mesh[t].f[L->bface].c : 0;
    bare_copy(L, &BARE[j][t], &jt->mesh[t], paint, j);
    BARE[j][t].lod = NULL;
    if (jt->mesh[t].lod) {
      bare_copy(L, &BARE_LOD[j][t], jt->mesh[t].lod, paint, -1);
      BARE[j][t].lod = &BARE_LOD[j][t];
    }
  }
}

/* the detailed model, for the eye only at ULTRA: drawn in place of the bare airframe (the weapons still hang on it) */
void stores_set_hi(int jet) {
  for (int t = 0; t < 2; t++) BARE[jet][t].hi = &JETS[jet].hi[t];
}

const Mesh *stores_bare(int jet, int team) {
  if (jet < 0 || jet >= MAX_JETS || team < 0 || team > 1) return NULL;
  if (!LAY[jet].done) lay_out(jet);
  return BARE[jet][team].nf ? &BARE[jet][team] : &JETS[jet].mesh[team];
}

/* Which station each class's missiles take, in loading order, from the full load: radar missiles choose
 * first, then air-to-ground missiles, then heat-seekers. They are fired in the same order (wingtips and
 * outboard first, where a pilot sees them go), so with n of a class fired its first n stations are empty. */
typedef struct { int n[3], at[3][NST]; } Slots;
static void slots(const Layout *L, const int *full, Slots *o) {
  bool used[NST] = { false };
  memset(o, 0, sizeof *o);
  static const int FIRST[3] = { 1, 2, 0 };
  for (int q = 0; q < 3; q++) {
    int c = FIRST[q];
    for (int i = 0; i < L->n && o->n[c] < full[c]; i++) {
      int k = L->ord[c][i];
      if (used[k]) continue;
      used[k] = true;
      o->at[c][o->n[c]++] = k;
    }
  }
}

static vec3 local_to_world(const Plane *p, vec3 v) {
  return v3_add(p->pos, v3_add(v3_add(v3_scale(p->r, v.x), v3_scale(p->u, v.y)), v3_scale(p->f, v.z)));
}

static int class_type(const Game *g, const Plane *p, int c) {
  if (c == 0) return g->def.on && p->team == 1 ? MS_TIDEWOLF : MS_GLINT;
  return c == 1 ? MS_FARREACH : MS_MATTOCK;
}

/* A missile's centre on a station: a thinner one hangs as close under its pylon as the radar missile would. */
static vec3 seat(const Layout *L, const Station *s, int type) {
  vec3 a = s->at;
  if (s->kind != ST_BAY && !s->baked) a.y += L->rad - MT[type].rad * SCALE;
  return a;
}

/* ---- what is remembered from frame to frame --------------------------------------------------------- */
#define TRAILS  96
#define TPTS    128
#define TSTEP   0.025f /* a trail point this often */
#define TLIFE   3.0f   /* and how long a trail lingers */
typedef struct {
  bool     used, live;
  uint32_t id;
  int      type;
  float    born;   /* when first seen */
  vec3     off;    /* from where the sim launched it to the station it left, fading out */
  int      n, head;
  vec3     p[TPTS];
  float    t[TPTS];
  bool     boost[TPTS];
} Trail;
static Trail TR[TRAILS];
static struct { int tot; float bayT; bool seen; } PS[MAX_PLANES];
static const Game *last_g;
static float last_t = -1;

static Trail *trail_of(uint32_t id) {
  for (int i = 0; i < TRAILS; i++)
    if (TR[i].used && TR[i].id == id) return &TR[i];
  return NULL;
}

static Trail *trail_new(void) {
  int best = -1;
  float oldest = 1e30f;
  for (int i = 0; i < TRAILS; i++) {
    if (!TR[i].used) return &TR[i];
    float t = TR[i].n ? TR[i].t[TR[i].head] : 0;
    if (!TR[i].live && t < oldest) oldest = t, best = i;
  }
  return best >= 0 ? &TR[best] : NULL;
}

static float age_of(const Game *g, const Trail *T) { return g->time - T->born; }

/* the missile as drawn: where the sim has it, drawn back toward the station it left for a moment */
static vec3 shown_nose(const Game *g, const Missile *m, const Trail *T) {
  if (!T) return m->pos;
  float k = 1 - age_of(g, T) / 0.45f;
  return k > 0 ? v3_add(m->pos, v3_scale(T->off, k * k)) : m->pos;
}

static vec3 nozzle(const Game *g, const Missile *m, const Trail *T) {
  return v3_sub(shown_nose(g, m, T), v3_scale(m->dir, MT[missile_type(m)].len * SCALE));
}

static void update(const Game *g) {
  if (g != last_g || g->time < last_t - 0.25f) {
    memset(TR, 0, sizeof TR);
    memset(PS, 0, sizeof PS);
  }
  if (g == last_g && g->time == last_t) return; /* the same moment again: another view of it */
  last_g = g;
  last_t = g->time;
  for (int i = 0; i < TRAILS; i++) TR[i].live = false;
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    Trail *T = trail_of(m->id);
    if (!T && (T = trail_new()) != NULL) {
      memset(T, 0, sizeof *T);
      T->used = true;
      T->id = m->id;
      T->type = missile_type(m);
      T->born = g->time;
      /* fired from a jet: it starts from the station that has just emptied */
      if (m->owner >= 0 && m->owner < g->nplanes && !m->sam) {
        const Plane *p = &g->planes[m->owner];
        int c = m->ashm || m->kind == MK_HEAT ? 0 : m->kind == MK_RADAR ? 1 : 2, full[4];
        if (p->jet >= 0 && p->jet < MAX_JETS) {
          if (!LAY[p->jet].done) lay_out(p->jet);
          game_full_load(g, p, full);
          Slots S;
          slots(&LAY[p->jet], full, &S);
          int have = c == 0 ? p->missiles : c == 1 ? p->radar : p->ground;
          int fired = S.n[c] - have; /* the station just emptied: the last of those fired */
          if (have >= 0 && fired >= 1) {
            const Layout *L = &LAY[p->jet];
            vec3 at = local_to_world(p, seat(L, &L->s[S.at[c][fired - 1]], T->type));
            vec3 nose = v3_add(at, v3_scale(p->f, MT[T->type].len * SCALE / 2));
            T->off = v3_sub(nose, m->pos);
            if (v3_len(T->off) > 40) T->off = v3(0, 0, 0); /* the jet has moved on (a late network event) */
          }
        }
      }
    }
    if (!T) continue;
    T->live = true;
    float last = T->n ? T->t[T->head] : -1e9f;
    if (g->time - last >= TSTEP) {
      T->head = (T->head + 1) % TPTS;
      if (T->n < TPTS) T->n++;
      T->p[T->head] = nozzle(g, m, T);
      T->t[T->head] = g->time;
      T->boost[T->head] = age_of(g, T) < MT[T->type].burn;
    }
  }
  for (int i = 0; i < TRAILS; i++)
    if (TR[i].used && !TR[i].live && (!TR[i].n || g->time - TR[i].t[TR[i].head] > TLIFE)) TR[i].used = false;
  for (int i = 0; i < g->nplanes && i < MAX_PLANES; i++) {
    const Plane *p = &g->planes[i];
    int tot = p->missiles + p->radar + p->ground;
    if (PS[i].seen && tot < PS[i].tot && p->alive) PS[i].bayT = g->time;
    if (!PS[i].seen) PS[i].bayT = -100;
    PS[i].tot = tot;
    PS[i].seen = true;
  }
}

/* ---- drawing --------------------------------------------------------------------------------------- */
static uint32_t grey(float t) { /* fresh smoke to old */
  int v = (int)(242 - 50 * t), b = (int)(236 - 44 * t);
  return 0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)b;
}

static void draw_trail(Framebuffer *fb, const View *V, const Game *g, const Trail *T, const Missile *m, int budget) {
  if (!T->n) return;
  vec3 a = m ? nozzle(g, m, T) : T->p[T->head];
  /* the puffs spread out on a long trail to stay within the budget, so it is never cut short */
  float total = 0;
  for (int k = 0; k < T->n; k++) {
    int i = (T->head - k + TPTS) % TPTS, j = (i + 1) % TPTS;
    if (g->time - T->t[i] > TLIFE) break;
    total += v3_dist(k ? T->p[j] : a, T->p[i]);
  }
  float minstep = total / (float)budget;
  float ta = g->time;
  bool ba = m ? age_of(g, T) < MT[T->type].burn : T->boost[T->head];
  float carry = m ? 1.2f : 0; /* how far along the next puff is: the smoke starts behind the flame */
  int puffs = 0;
  bool ram = T->type == MS_TIDEWOLF;
  r3d_sprite_kind(SPR_SOFT);
  for (int k = 0; k < T->n; k++) {
    int i = (T->head - k + TPTS) % TPTS;
    vec3 b = T->p[i];
    float tb = T->t[i];
    if (g->time - tb > TLIFE + TSTEP) break;
    vec3 d = v3_sub(b, a);
    float len = v3_len(d);
    /* the hot young trail: a thin bright line, so it never breaks up at range */
    float ya = g->time - (ta + tb) / 2;
    if (ya < 0.8f && len > 0.01f) {
      float al = (1 - ya / 0.8f) * (ba ? 0.9f : 0.6f);
      r3d_seg(fb, V, a, b, grey(0), 0.12f, al, 1.5f);
    }
    /* soft puffs along it, wider and fainter with age */
    while (len > 0.01f && carry <= len && puffs < budget + 8) {
      float s = carry / len, t = ta + (tb - ta) * s, age = g->time - t;
      if (age > TLIFE) break;
      vec3 p = v3_add(a, v3_scale(d, s));
      float fade = 1 - age / TLIFE, rad = (ba ? 0.3f : 0.22f) + age * (ba ? 1.6f : 0.9f);
      float al = (ba ? 0.6f : ram ? 0.2f : 0.32f) * fade * fade;
      r3d_ball(V, p, rad, grey(age / TLIFE), al);
      puffs++;
      vec3 c = to_cam(&V->cam, p);
      float px = c.z > 1 ? c.z * 2.2f / V->F : 0; /* at range one every couple of pixels is enough */
      carry += fmaxf(fmaxf(rad * 0.75f, px), fmaxf(minstep, 0.4f));
    }
    carry -= len;
    a = b;
    ta = tb;
    ba = T->boost[i];
  }
}

static void draw_missile(Framebuffer *fb, const View *V, const Game *g, const Missile *m, const Trail *T) {
  int type = T ? T->type : missile_type(m);
  const MissileType *t = &MT[type];
  vec3 f = m->dir, up = fabsf(f.y) > 0.95f ? v3(1, 0, 0) : v3(0, 1, 0);
  vec3 r = v3_norm(v3_cross(up, f)), u = v3_cross(f, r);
  vec3 nose = shown_nose(g, m, T), half = v3_scale(f, t->len * SCALE / 2);
  vec3 centre = v3_sub(nose, half), tail = v3_sub(centre, half);
  float age = T ? age_of(g, T) : 0;
  bool boost = age < t->burn;
  float fl = 0.8f + 0.4f * (float)(hash(m->id, (uint32_t)(g->time * 60)) & 1023) / 1023;
  float flame = (boost ? t->flame : type == MS_TIDEWOLF ? 0.55f : 0.35f) * fl;
  vec3 cc = to_cam(&V->cam, centre);
  float px = cc.z > 0.5f ? 2 * t->rad * SCALE / cc.z * V->F : 1e9f;
  if (px < 1.6f) {
    /* thinner than a pixel or two: a line its length, a dark speck at the nose where the eye looks for it, and
     * the flame's dot */
    r3d_seg(fb, V, nose, tail, hexc(0x7a8088), 0.25f, 1, 1.5f);
    r3d_sprite_kind(SPR_SOFT);
    r3d_dot(V, v3_sub(nose, v3_scale(f, t->len * 0.15f)), 0.5f, hexc(0x40464e), 0.95f, gpu_active() ? 5 : 2);
    r3d_sprite_kind(SPR_GLOW);
    r3d_dot(V, v3_sub(tail, v3_scale(f, 0.4f)), boost ? 1.4f : 1, hexc(0xffe3a8), boost ? 1 : 0.75f, boost ? 2.5f : 2);
    r3d_sprite_kind(SPR_SOFT);
    return;
  }
  draw_model(fb, V, type, centre, r, u, f);
  r3d_mesh(fb, V, &FLAME, tail, r, u, f, flame, 1);
  r3d_sprite_kind(SPR_GLOW);
  r3d_dot(V, v3_sub(tail, v3_scale(f, 0.12f)), boost ? 0.45f : 0.3f, hexc(0xfff2cc), 1, 1.5f);
  r3d_sprite_kind(SPR_SOFT);
}

void stores_missiles(Framebuffer *fb, const View *V, const Game *g) {
  models();
  update(g);
  int n = 0;
  for (int i = 0; i < TRAILS; i++) n += TR[i].used;
  int budget = n ? 2400 / n : 0; /* puffs a trail, 220 at most: the alpha list has room for clouds too */
  if (budget > 220) budget = 220;
  if (budget < 24) budget = 24;
  for (int i = 0; i < TRAILS; i++)
    if (TR[i].used && !TR[i].live) draw_trail(fb, V, g, &TR[i], NULL, budget);
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    const Trail *T = trail_of(m->id);
    if (T) draw_trail(fb, V, g, T, m, budget);
    draw_missile(fb, V, g, m, T);
  }
}

void (*stores_station_hook)(const Plane *p, vec3 at, int cls, bool loaded, bool shown);
void (*stores_nozzle_hook)(const Plane *p, vec3 at, float throttle, float ab);
void (*stores_gear_hook)(const Plane *p, int leg, vec3 wheel, float down);

void stores_jet(Framebuffer *fb, const View *V, const Game *g, const Plane *p) {
  if (p->jet < 0 || p->jet >= MAX_JETS) return;
  const JetType *jt = &JETS[p->jet];
  vec3 cc = to_cam(&V->cam, p->pos);
  if (cc.z < 1 || jt->mesh[0].rad / cc.z * V->F < 22) return; /* too small to see what it carries */
  models();
  update(g);
  Layout *L = &LAY[p->jet];
  if (!L->done) lay_out(p->jet);
  int full[4];
  game_full_load(g, p, full);
  Slots S;
  slots(L, full, &S);
  int have[3] = { p->missiles, p->radar, p->ground };
  float open = 1;
  if (L->bay) {
    /* the doors: open in a quarter second on a launch, closed again a second later */
    float t = p->idx < MAX_PLANES ? g->time - PS[p->idx].bayT : 99;
    open = t < 0 ? 0 : t < 0.25f ? t / 0.25f : t < 0.95f ? 1 : t < 1.3f ? (1.3f - t) / 0.35f : 0;
  }
  if (L->bay && open > 0) {
    float hw = L->bay_hw * 0.85f, ang = open * 1.45f;
    vec3 ff = v3_scale(p->f, L->bay_len);
    vec3 cav = local_to_world(p, v3(0, L->bay_y - 0.03f, L->bay_z));
    r3d_mesh(fb, V, &CAVITY, cav, v3_scale(p->r, hw * 2), p->u, ff, 1, 1);
    for (int side = -1; side <= 1; side += 2) {
      vec3 hinge = local_to_world(p, v3(side * hw, L->bay_y - 0.04f, L->bay_z));
      vec3 d = v3_add(v3_scale(p->r, -side * cosf(ang) * hw), v3_scale(p->u, -sinf(ang) * hw));
      r3d_mesh(fb, V, &DOOR, hinge, d, v3_cross(ff, d), ff, 1, 1);
    }
  }
  for (int c = 0; c < 3; c++)
    for (int i = 0; i < S.n[c]; i++) {
      const Station *s = &L->s[S.at[c][i]];
      bool loaded = i >= S.n[c] - have[c];
      int type = class_type(g, p, c);
      vec3 at = seat(L, s, type);
      float rad = MT[type].rad * SCALE, len = MT[type].len * SCALE;
      if (s->kind == ST_BAY) {
        if (open <= 0) { /* shut in: there, but not to be seen */
          if (stores_station_hook) stores_station_hook(p, local_to_world(p, at), c, loaded, false);
          continue;
        }
        at.y -= (rad + 0.15f + s->wing) * open; /* lowered out of the bay on its trapeze */
      }
      /* the rail or pylon stays when its missile has gone */
      if (s->baked) {
      } else if (s->kind == ST_RAIL)
        box(fb, V, local_to_world(p, v3(s->at.x, s->wing - 0.06f, s->at.z)), p->r, p->u, p->f, 0.08f, 0.12f, len * 0.7f);
      else if (s->kind == ST_PYLON) {
        float top = s->wing + 0.05f, bot = at.y + rad * 0.7f;
        box(fb, V, local_to_world(p, v3(s->at.x, (top + bot) / 2, s->at.z)), p->r, p->u, p->f, 0.09f, top - bot, len * 0.38f);
      }
      if (loaded) draw_model(fb, V, type, local_to_world(p, at), p->r, p->u, p->f);
      if (stores_station_hook) stores_station_hook(p, local_to_world(p, at), c, loaded, true);
    }
}

/* The landing gear, as far down as it has come (gearT): each leg swings forward up into its well as it
 * retracts, and the well's doors stand open while the gear is out. */
void stores_gear(Framebuffer *fb, const View *V, const Plane *p) {
  if (p->jet < 0 || p->jet >= MAX_JETS || p->drone) return;
  const JetType *jt = &JETS[p->jet];
  vec3 cc = to_cam(&V->cam, p->pos);
  if (cc.z < 1 || jt->mesh[0].rad / cc.z * V->F < 22) return; /* too small to see the wheels */
  if (p->gearT <= 0.01f) {
    if (stores_gear_hook) stores_gear_hook(p, -1, p->pos, p->gearT);
    return;
  }
  models();
  Layout *L = &LAY[p->jet];
  if (!L->done) lay_out(p->jet);
  if (!JETS[p->jet].mesh[0].nv) return;
  float th = (1 - p->gearT) * PI_F / 2, open = fminf(1, p->gearT * 4) * 1.45f;
  vec3 dir = v3_add(v3_scale(p->u, -cosf(th)), v3_scale(p->f, sinf(th))), fwd = v3_cross(p->r, dir);
  for (int k = 0; k < 3; k++) {
    vec3 h = local_to_world(p, L->hinge[k]);
    float l = L->leg[k], w = L->wheel[k];
    box(fb, V, v3_add(h, v3_scale(dir, l / 2)), p->r, dir, fwd, k ? 0.16f : 0.13f, l, k ? 0.16f : 0.13f);
    r3d_mesh(fb, V, &WHEEL, v3_add(h, v3_scale(dir, l)), v3_scale(p->r, k ? 0.28f : 0.2f), v3_scale(dir, w),
             v3_scale(fwd, w), 1, 1);
    if (stores_gear_hook) stores_gear_hook(p, k, v3_add(h, v3_scale(dir, l)), p->gearT);
    /* the doors: the nose well's two, a main's one on its outer side, along the well (forward of the hinge) */
    float dl = (l + w) * 0.65f, dw = k ? 0.34f : 0.2f;
    vec3 ff = v3_scale(p->f, dl);
    for (int sd = -1; sd <= 1; sd += 2) {
      if (k && sd != (k == 1 ? -1 : 1)) continue;
      vec3 at = local_to_world(p, v3(L->hinge[k].x + sd * dw, L->hinge[k].y - 0.02f, L->hinge[k].z + dl / 2));
      vec3 d = v3_add(v3_scale(p->r, -sd * cosf(open) * dw), v3_scale(p->u, -sinf(open) * dw));
      r3d_mesh(fb, V, &DOOR, at, d, v3_cross(ff, d), ff, 1, 1);
    }
  }
}

/* ---- the engines' exhaust -------------------------------------------------------------------------- */
/* Dry thrust shows only a dull orange glow deep in each nozzle. In afterburner (the throttle's top tenth) each
 * nozzle has its plume: a white-hot core at the exit, a translucent blue-violet cone where the fuel burns
 * cleanest, turning orange-yellow toward the tip, and in full burner a row of bright shock diamonds about a
 * nozzle width apart. Translucent soft sprites throughout, so the sky shows through. */
static void plume(const View *V, const Plane *p, vec3 exit, float w, float h, float ab, uint32_t seed) {
  vec3 aft = v3_scale(p->f, -1);
  float r = sqrtf(w * h), fl = 0.92f + 0.16f * (float)(seed & 255) / 255;
  r3d_sprite_kind(SPR_GLOW);
  if (ab <= 0) { /* dry: the glow in the nozzle, stronger with the throttle */
    r3d_ball(V, v3_add(exit, v3_scale(aft, 0.04f)), r * 0.6f, hexc(0xe0562a), 0.15f + 0.25f * p->throttle);
    r3d_sprite_kind(SPR_SOFT);
    return;
  }
  float L = r * (4 + 7 * ab) * fl;
  r3d_ball(V, v3_add(exit, v3_scale(aft, 0.05f)), r * 0.6f, hexc(0xfff3d8), 0.8f); /* the core */
  const int N = 16;
  for (int i = 1; i <= N; i++) {
    float t = (float)i / N;
    uint32_t c = t < 0.4f ? 0x7f8cff : t < 0.72f ? 0xffb24a : 0xff7a2a;
    float a = (t < 0.4f ? 0.3f : t < 0.72f ? 0.26f : 0.2f) * (1 - t * 0.6f) * (0.6f + 0.4f * ab);
    r3d_ball(V, v3_add(exit, v3_scale(aft, L * t)), r * (0.85f - 0.5f * t), hexc(c), a);
  }
  for (int k = 0; ab > 0.5f && k < 4; k++) { /* shock diamonds */
    float d = 2 * r * (0.9f + 1.1f * k);
    if (d > L * 0.8f) break;
    r3d_ball(V, v3_add(exit, v3_scale(aft, d)), r * 0.38f, hexc(0xfff0b8), 0.8f * (1 - k * 0.22f) * (ab - 0.4f));
  }
  r3d_sprite_kind(SPR_SOFT);
}

void stores_exhaust(const View *V, const Game *g, const Plane *p) {
  if (p->fuel <= 0 || p->jet < 0 || p->jet >= MAX_JETS) return;
  const JetType *jt = &JETS[p->jet];
  vec3 cc = to_cam(&V->cam, p->pos);
  float ab = clampf((p->throttle - 0.9f) / 0.1f, 0, 1) * (p->throttle > 0.9f);
  if (cc.z > 1 && jt->mesh[0].rad / cc.z * V->F < 6) { /* far: a speck of flame, as before */
    r3d_sprite_kind(SPR_GLOW);
    r3d_dot(V, v3_sub(p->pos, v3_scale(p->f, jt->tail)), 1.4f + p->throttle * 1.2f, hexc(0xffb347), 1, 1);
    r3d_sprite_kind(SPR_SOFT);
    return;
  }
  Layout *L = &LAY[p->jet];
  if (!L->done) lay_out(p->jet);
  uint32_t seed = hash((uint32_t)p->idx, (uint32_t)(g->time * 60));
  if (!L->nnoz) {
    plume(V, p, v3_sub(p->pos, v3_scale(p->f, jt->tail)), 0.45f, 0.45f, ab, seed);
    if (stores_nozzle_hook) stores_nozzle_hook(p, v3_sub(p->pos, v3_scale(p->f, jt->tail)), p->throttle, ab);
    return;
  }
  for (int i = 0; i < L->nnoz; i++) {
    plume(V, p, local_to_world(p, L->noz[i]), L->nozw[i], L->nozh[i], ab, seed >> (i * 8));
    if (stores_nozzle_hook) stores_nozzle_hook(p, local_to_world(p, L->noz[i]), p->throttle, ab);
  }
}
