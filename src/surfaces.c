/* surfaces.c - the jets' control surfaces (surfaces.h). Original code for this project.
 *
 * Every wing, tailplane, canard and fin of the models is lofted through stations of one airfoil: six corners, the
 * leading edge, 30 % and 68 % of the chord on top and underneath, and a sharp trailing edge. The 68 % pair is the
 * hinge: the strip from it to the trailing edge, between two stations, is a control surface. A station is found
 * from its trailing-edge corner (two neighbours either side of the chord, a leading-edge corner where the 68 %
 * says, and the 30 % pair); stations joined by faces are one surface; which surface is which comes from where it
 * sits on the jet. A canard moves whole, about its 30 % line, as the real ones do (its strip alone was smaller
 * than a pixel on the retro picture). */
#include "surfaces.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/jets.h"
#include "gpu.h"

const char *const SF_NAMES[SF_KINDS] = { "aileron", "elevon", "elevator", "canard", "rudder", "flap" };
void (*surfaces_hook)(const Plane *p, int kind, int side, vec3 te, float deg);

#define MAX_PC 40
#define NB     24 /* neighbours a corner may have */

typedef struct { int le, pa, pb, a, b, te; float chord; } Station; /* pa, pb the 30 % pair; a, b the 68 % */
typedef struct {
  uint8_t kind;
  int8_t  side;
  vec3    h0, axis, te; /* the hinge (a point on it and its direction, turned so a positive angle lifts the edge) */
  Mesh    m[2];         /* its faces, in each team's paint */
} Piece;
typedef struct {
  bool     done;
  int      n;
  Piece    pc[MAX_PC];
  uint8_t *cut; /* per face of the model: part of a piece */
  int      ncut;
  bool     droop; /* no flaps of its own: the elevons droop for them */
} JetSurf;
static JetSurf SURF[MAX_JETS];

static int near_vert(const Mesh *M, vec3 p, float tol) {
  for (int i = 0; i < M->nv; i++)
    if (v3_len(v3_sub(M->v[i], p)) < tol) return i;
  return -1;
}

static int root_of(int *par, int i) {
  while (par[i] != i) i = par[i] = par[par[i]];
  return i;
}

/* The station whose trailing-edge corner is t, if it is one */
static bool station_at(const Mesh *M, const int *nb, const int *nn, int t, Station *s) {
  vec3 T = M->v[t];
  for (int x = 0; x < nn[t]; x++)
    for (int y = x + 1; y < nn[t]; y++) {
      int ai = nb[t * NB + x], bi = nb[t * NB + y];
      vec3 A = M->v[ai], B = M->v[bi], mid = v3_scale(v3_add(A, B), 0.5f), d = v3_sub(T, mid);
      float dl = v3_len(d), th = v3_len(v3_sub(A, B));
      if (dl < 0.1f || th < 1e-3f || th > 0.2f * dl / 0.32f) continue;
      if (fabsf(v3_dot(v3_sub(A, B), d)) > 0.05f * th * dl) continue; /* across the chord, not along it */
      vec3 le = v3_sub(T, v3_scale(d, 1 / 0.32f));
      float c = dl / 0.32f, tol = 0.01f * c + 0.003f;
      int li = near_vert(M, le, tol);
      if (li < 0) continue;
      vec3 p30 = v3_add(le, v3_scale(v3_sub(T, le), 0.3f)); /* and the 30 % pair, as thick as the airfoil says */
      vec3 half = v3_scale(v3_sub(A, mid), 1 / 0.72f);
      int pa = near_vert(M, v3_add(p30, half), tol), pb = near_vert(M, v3_sub(p30, half), tol);
      if (pa < 0 || pb < 0) continue;
      *s = (Station){ li, pa, pb, ai, bi, t, c };
      return true;
    }
  return false;
}

/* A piece: the strip from the 68 % hinge to the trailing edge between two stations, or (whole: an all-moving
 * canard) the whole surface between them, its tip's cap with it, turned about the 30 % line */
static void add_piece(JetSurf *S, int j, const Station *r0, const Station *r1, int kind, vec3 up, bool whole) {
  if (S->n >= MAX_PC) return;
  const Mesh *M = &JETS[j].mesh[0];
  int set[20] = { r0->a, r0->b, r0->te, r1->a, r1->b, r1->te, r0->le, r0->pa, r0->pb, r1->le, r1->pa, r1->pb }, ns = whole ? 12 : 6;
  for (int k = 0; whole && k < M->nf && ns < 20; k++) { /* a cap's middle: a corner whose faces are all on the surface */
    const Face *fc = &M->f[k];
    int out = -1, nout = 0;
    for (int i = 0; i < fc->n; i++) {
      int q;
      for (q = 0; q < ns && set[q] != fc->i[i]; q++) {}
      if (q == ns) out = fc->i[i], nout++;
    }
    if (nout != 1) continue;
    bool all = true;
    for (int k2 = 0; k2 < M->nf && all; k2++) {
      const Face *f2 = &M->f[k2];
      bool has = false, rest = true;
      for (int i = 0; i < f2->n; i++) {
        if (f2->i[i] == out) {
          has = true;
          continue;
        }
        int q;
        for (q = 0; q < ns && set[q] != f2->i[i]; q++) {}
        rest = rest && q < ns;
      }
      all = !has || rest;
    }
    if (all) set[ns++] = out;
  }
  Piece *P = &S->pc[S->n];
  memset(P, 0, sizeof *P);
  P->kind = (uint8_t)kind;
  int ha0 = whole ? r0->pa : r0->a, hb0 = whole ? r0->pb : r0->b, ha1 = whole ? r1->pa : r1->a, hb1 = whole ? r1->pb : r1->b;
  P->h0 = v3_scale(v3_add(M->v[ha0], M->v[hb0]), 0.5f);
  vec3 h1 = v3_scale(v3_add(M->v[ha1], M->v[hb1]), 0.5f);
  P->axis = v3_norm(v3_sub(h1, P->h0));
  P->te = v3_scale(v3_add(M->v[r0->te], M->v[r1->te]), 0.5f);
  P->side = (int8_t)(P->te.x > 0.05f ? 1 : P->te.x < -0.05f ? -1 : 0);
  /* turned so that a positive angle lifts the trailing edge (along up) */
  vec3 arm = v3_sub(P->te, P->h0);
  if (v3_dot(v3_cross(P->axis, arm), up) < 0) P->axis = v3_scale(P->axis, -1);
  int nf = 0;
  for (int t = 0; t < 2; t++) {
    const Mesh *src = &JETS[j].mesh[t];
    Mesh *m = &P->m[t];
    mesh_reset(m, M->rad); /* the jet's own reach: the piece is in its coordinates */
    int map[20];
    for (int q = 0; q < 20; q++) map[q] = -1;
    for (int k = 0; k < src->nf && k < S->ncut; k++) {
      const Face *fc = &src->f[k];
      int idx[FACE_MAX_V], in = 0;
      for (int i = 0; i < fc->n; i++)
        for (int q = 0; q < ns; q++)
          if (fc->i[i] == set[q]) {
            if (map[q] < 0) map[q] = mesh_vert(m, src->v[set[q]].x, src->v[set[q]].y, src->v[set[q]].z);
            idx[i] = map[q];
            in++;
            break;
          }
      if (in != fc->n) continue;
      mesh_face(m, idx, fc->n, fc->c);
      S->cut[k] = 1;
      nf += !t;
    }
  }
  if (nf) S->n++;
}

static void detect(int j) {
  JetSurf *S = &SURF[j];
  S->done = true;
  const Mesh *M = &JETS[j].mesh[0];
  if (!M->nv || !M->nf || j == DRONE_JET) return;
  S->cut = calloc((size_t)M->nf, 1);
  int *nb = malloc(sizeof(int) * (size_t)M->nv * NB), *nn = calloc((size_t)M->nv, sizeof(int));
  Station *st = malloc(sizeof(Station) * (size_t)M->nv);
  int *par = malloc(sizeof(int) * (size_t)M->nv), *of = malloc(sizeof(int) * (size_t)M->nv);
  if (!S->cut || !nb || !nn || !st || !par || !of) goto done;
  S->ncut = M->nf;
  for (int k = 0; k < M->nf; k++) /* each corner's neighbours on its faces */
    for (int x = 0; x < M->f[k].n; x++)
      for (int y = 0; y < M->f[k].n; y++) {
        int a = M->f[k].i[x], b = M->f[k].i[y], q;
        if (a == b) continue;
        for (q = 0; q < nn[a] && nb[a * NB + q] != b; q++) {}
        if (q == nn[a] && nn[a] < NB) nb[a * NB + nn[a]++] = b;
      }
  int ns = 0;
  for (int t = 0; t < M->nv; t++) {
    of[t] = -1;
    if (station_at(M, nb, nn, t, &st[ns])) of[t] = ns++;
  }
  /* stations sharing a face are one surface */
  for (int i = 0; i < ns; i++) par[i] = i;
  for (int k = 0; k < M->nf; k++) {
    int first = -1;
    for (int x = 0; x < M->f[k].n; x++) {
      int s = of[M->f[k].i[x]];
      if (s < 0) continue;
      if (first < 0) first = s;
      else par[root_of(par, s)] = root_of(par, first);
    }
  }
  /* each surface: flat or upright, its stations from the root out, how far out it reaches and where it sits */
  enum { MAXL = 48 };
  int lroot[MAXL], lcount[MAXL], nl = 0;
  bool flat[MAXL];
  float tip[MAXL], zlo[MAXL], zhi[MAXL], ylo[MAXL], yhi[MAXL], cmax[MAXL];
  for (int i = 0; i < ns; i++) {
    int r = root_of(par, i), l;
    for (l = 0; l < nl && lroot[l] != r; l++) {}
    if (l == nl) {
      if (nl == MAXL) continue;
      lroot[nl] = r, lcount[nl] = 0, tip[nl] = 0, zlo[nl] = ylo[nl] = 1e9f, zhi[nl] = yhi[nl] = -1e9f, cmax[nl] = 0;
      vec3 th = v3_sub(M->v[st[i].a], M->v[st[i].b]);
      flat[nl++] = fabsf(th.y) >= 0.7f * v3_len(th);
    }
    vec3 T = M->v[st[i].te];
    lcount[l]++;
    tip[l] = fmaxf(tip[l], fabsf(T.x));
    zlo[l] = fminf(zlo[l], T.z), zhi[l] = fmaxf(zhi[l], T.z);
    ylo[l] = fminf(ylo[l], T.y), yhi[l] = fmaxf(yhi[l], T.y);
    cmax[l] = fmaxf(cmax[l], st[i].chord);
  }
  /* the main wing: the flat surface reaching furthest out (a missile's fins and the pylons are too small or
   * hang under it); a canard ahead of it, a tail behind it */
  float span = 0, wlo = 1e9f, whi = -1e9f;
  for (int l = 0; l < nl; l++)
    if (flat[l] && lcount[l] >= 2 && cmax[l] >= 0.8f) span = fmaxf(span, tip[l]);
  for (int l = 0; l < nl; l++)
    if (flat[l] && lcount[l] >= 2 && cmax[l] >= 0.8f && tip[l] >= span * 0.98f) wlo = fminf(wlo, zlo[l]), whi = fmaxf(whi, zhi[l]);
  bool tail = false;
  for (int l = 0; l < nl; l++)
    if (flat[l] && lcount[l] >= 2 && cmax[l] >= 0.8f && (zlo[l] + zhi[l]) / 2 < wlo - 1) tail = true;
  for (int l = 0; l < nl; l++) {
    if (lcount[l] < 2 || cmax[l] < 0.8f) continue;
    if (!flat[l] && (ylo[l] < 0.1f || yhi[l] < 1)) continue; /* an upright surface under the jet: a ventral fin */
    Station rs[16];
    int n = 0;
    for (int i = 0; i < ns && n < 16; i++)
      if (root_of(par, i) == lroot[l]) rs[n++] = st[i];
    for (int a = 1; a < n; a++) /* from the root out */
      for (int b = a; b > 0; b--) {
        vec3 p = M->v[rs[b - 1].te], q = M->v[rs[b].te];
        if ((flat[l] ? fabsf(p.x) : p.y) <= (flat[l] ? fabsf(q.x) : q.y)) break;
        Station t = rs[b];
        rs[b] = rs[b - 1], rs[b - 1] = t;
      }
    float zc = (zlo[l] + zhi[l]) / 2, x0 = fabsf(M->v[rs[0].te].x), x1 = fabsf(M->v[rs[n - 1].te].x);
    vec3 up = flat[l] ? v3(0, 1, 0) : v3(1, 0, 0);
    for (int i = 0; i + 1 < n; i++) {
      int kind;
      if (!flat[l]) kind = SF_RUDDER;
      else if (zc > whi + 1) kind = SF_CANARD;
      else if (zc < wlo - 1) kind = SF_ELEVATOR;
      else if (tip[l] < span * 0.98f) break; /* an inner wing panel of its own (a swing wing's glove): rigid */
      else {
        /* the main wing's outer half the ailerons (on a delta with no tail, the elevons), its inner half flaps */
        float mid = (fabsf(M->v[rs[i].te].x) + fabsf(M->v[rs[i + 1].te].x)) / 2;
        kind = (mid - x0) >= 0.5f * (x1 - x0) || n == 2 ? (tail ? SF_AILERON : SF_ELEVON) : SF_FLAP;
      }
      add_piece(S, j, &rs[i], &rs[i + 1], kind, up, kind == SF_CANARD); /* canards all-moving */
    }
  }
  S->droop = true;
  for (int i = 0; i < S->n; i++) S->droop = S->droop && S->pc[i].kind != SF_FLAP;
done:
  free(nb), free(nn), free(st), free(par), free(of);
}

bool surfaces_cut(int jet, int face) {
  if (jet < 0 || jet >= MAX_JETS) return false;
  if (!SURF[jet].done) detect(jet);
  return face >= 0 && face < SURF[jet].ncut && SURF[jet].cut[face];
}

void surfaces_count(int jet, int n[SF_KINDS]) {
  memset(n, 0, sizeof(int) * SF_KINDS);
  if (jet < 0 || jet >= MAX_JETS) return;
  if (!SURF[jet].done) detect(jet);
  for (int i = 0; i < SURF[jet].n; i++) n[SURF[jet].pc[i].kind]++;
}

/* v turned about the unit axis k by angle a */
static vec3 turn(vec3 v, vec3 k, float c, float s) {
  return v3_add(v3_add(v3_scale(v, c), v3_scale(v3_cross(k, v), s)), v3_scale(k, v3_dot(k, v) * (1 - c)));
}

/* Degrees: the stick's pitch and roll as each kind of surface takes them (the rudder follows the roll, as a
 * coordinated turn's would), the flaps by their notch on realistic handling (a delta with no flaps droops its
 * elevons a third as far) */
static float deflection(const Game *g, const Plane *p, int kind, int side, bool droop) {
  float pitch = clampf(p->ctl.pitch, -1, 1), roll = clampf(p->ctl.roll, -1, 1), flap = g->realistic ? clampf(p->flapT, 0, 2) : 0;
  switch (kind) {
  case SF_AILERON: return 20 * roll * side;
  case SF_ELEVON: return clampf(16 * pitch + 14 * roll * side - (droop ? 7 * flap : 0), -25, 25);
  case SF_ELEVATOR: return clampf(18 * pitch + 7 * roll * side, -25, 25); /* tailerons: a little roll too */
  case SF_CANARD: return -15 * pitch; /* the nose up: the canard's leading edge up, its trailing edge down */
  case SF_RUDDER: return 15 * roll;
  case SF_FLAP: return -20 * flap;
  default: return 0;
  }
}

void surfaces_draw(Framebuffer *fb, const View *V, const Game *g, const Plane *p) {
  if (p->jet < 0 || p->jet >= MAX_JETS || p->drone) return;
  JetSurf *S = &SURF[p->jet];
  if (!S->done) detect(p->jet);
  if (!S->n) return;
  /* only where the near model is drawn (render3d's own test): the far one has its surfaces in it, rigid */
  const Mesh *M = &JETS[p->jet].mesh[0];
  vec3 cc = to_cam(&V->cam, p->pos);
  if (cc.z < -M->rad || (cc.z > M->rad && M->rad / cc.z * V->F < 1.6f)) return;
  if (M->lod && M->rad / fmaxf(cc.z, 1) * V->F < 45) return;
  int team = p->team & 1;
  /* at ULTRA the detailed model has these surfaces in it at rest: a piece shows only when it moves, and shines as the
   * skin does */
  const bool fine = gpu_ultra() && JETS[p->jet].hi[team].nv > 0;
  if (fine) r3d_gloss(1);
  for (int i = 0; i < S->n; i++) {
    const Piece *P = &S->pc[i];
    float deg = deflection(g, p, P->kind, P->side, S->droop), a = deg * (PI_F / 180), c = cosf(a), s = sinf(a);
    /* in the jet's coordinates: h0 + turn(v - h0); in the world, the jet's frame times the turn */
    vec3 ex = turn(v3(1, 0, 0), P->axis, c, s), ey = turn(v3(0, 1, 0), P->axis, c, s), ez = turn(v3(0, 0, 1), P->axis, c, s);
    vec3 off = v3_sub(P->h0, turn(P->h0, P->axis, c, s));
#define W(v) v3_add(v3_add(v3_scale(p->r, (v).x), v3_scale(p->u, (v).y)), v3_scale(p->f, (v).z))
    if (!fine || fabsf(deg) > 1.5f) r3d_mesh(fb, V, &P->m[team], v3_add(p->pos, W(off)), W(ex), W(ey), W(ez), 1, 1);
    if (surfaces_hook) {
      vec3 te = v3_add(P->h0, turn(v3_sub(P->te, P->h0), P->axis, c, s));
      surfaces_hook(p, P->kind, P->side, v3_add(p->pos, W(te)), deg);
    }
#undef W
  }
  if (fine) r3d_gloss(-1);
}
