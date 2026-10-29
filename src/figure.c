/* figure.c - the figure engine (see figure.h), the same as the browser game's assets/figure/figure.js: a skeleton
 * sized by Leonardo's canon, forward kinematics, lofted rings along the bones, a head built on the face's thirds, the
 * Navy kit, flat-shaded drawing and the canon measured on the mesh. The arithmetic is in double, as JavaScript's is,
 * so the two make the same triangles (tests/test_figure.c compares them). Original code for this project. */
#include "figure.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the canon, as fractions of the height H ---- */
#define V_SPAN 1.0
#define V_HEAD (1.0 / 8)
#define V_FACE (1.0 / 10)
#define V_CHEST_TOP (1.0 / 6)
#define V_CHEST_UNDER (1.0 / 4)
#define V_SHOULDERS (1.0 / 4)
#define V_ELBOW_TIP (1.0 / 4)
#define V_ELBOW_PIT (1.0 / 8)
#define V_HAND (1.0 / 10)
#define V_FOOT (1.0 / 7)
#define V_KNEE (1.0 / 4)
#define V_CROTCH (1.0 / 2)
#define V_NAVEL 0.6

#define PI 3.14159265358979323846
#define D (PI / 180)

typedef struct { double v[3]; } V3;
static V3 v3(double a, double b, double c) { return (V3){ { a, b, c } }; }
static V3 add(V3 a, V3 b) { return v3(a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2]); }
static V3 sub(V3 a, V3 b) { return v3(a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2]); }
static V3 mul(V3 a, double k) { return v3(a.v[0] * k, a.v[1] * k, a.v[2] * k); }
static double dot(V3 a, V3 b) { return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2]; }
static V3 cross(V3 a, V3 b) { return v3(a.v[1] * b.v[2] - a.v[2] * b.v[1], a.v[2] * b.v[0] - a.v[0] * b.v[2], a.v[0] * b.v[1] - a.v[1] * b.v[0]); }
static double len3(V3 a) { return sqrt(a.v[0] * a.v[0] + a.v[1] * a.v[1] + a.v[2] * a.v[2]); }
static V3 norm(V3 a) {
  double l = len3(a);
  return mul(a, 1 / (l != 0 ? l : 1));
}
static double sgn(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
static double spow(double c, double e) { return sgn(c) * pow(fabs(c), e); }

typedef struct { double m[9]; } M3;
static M3 mmul(M3 a, M3 b) {
  M3 r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
  return r;
}
static V3 mv(M3 m, V3 v) {
  return v3(m.m[0] * v.v[0] + m.m[1] * v.v[1] + m.m[2] * v.v[2], m.m[3] * v.v[0] + m.m[4] * v.v[1] + m.m[5] * v.v[2],
            m.m[6] * v.v[0] + m.m[7] * v.v[1] + m.m[8] * v.v[2]);
}
static M3 rx(double a) { double c = cos(a), s = sin(a); return (M3){ { 1, 0, 0, 0, c, -s, 0, s, c } }; }
static M3 ry(double a) { double c = cos(a), s = sin(a); return (M3){ { c, 0, s, 0, 1, 0, -s, 0, c } }; }
static M3 rz(double a) { double c = cos(a), s = sin(a); return (M3){ { c, -s, 0, s, c, 0, 0, 0, 1 } }; }
/* a joint's rotation from its pose angles in degrees: bend about x, twist about the bone, spread about z */
static M3 rot(const float p[3]) { return mmul(mmul(rz(p[2] * D), ry(p[1] * D)), rx(p[0] * D)); }

/* ---- the skeleton from the canon ---- */
typedef struct {
  double H, chin, deltoid, crown, hairline, brow, nose, chest_top, chest_under, navel, crotch, knee, ankle, foot_len;
  double sh_x, sh_y, hip_x, hip_y, upper, fore, hand, palm;
} Skel;

static Skel skeleton(double H) {
  Skel K;
  K.H = H;
  K.chin = H * (1 - V_HEAD);
  K.deltoid = 0.026 * H;
  K.sh_x = V_SHOULDERS * H / 2 - K.deltoid; /* the shoulder joint: the widest shoulders less the deltoid */
  double pit_x = K.sh_x + K.deltoid, elbow_x = pit_x + V_ELBOW_PIT * H, tip_x = V_SPAN * H / 2, wrist_x = tip_x - V_HAND * H;
  K.crown = H;
  K.hairline = K.chin + V_FACE * H;
  K.brow = K.chin + V_FACE * H * 2 / 3;
  K.nose = K.chin + V_FACE * H / 3;
  K.chest_top = H * (1 - V_CHEST_TOP);
  K.chest_under = H * (1 - V_CHEST_UNDER);
  K.navel = V_NAVEL * H;
  K.crotch = V_CROTCH * H;
  K.knee = V_KNEE * H;
  K.ankle = 0.04 * H;
  K.foot_len = V_FOOT * H;
  K.sh_y = H * (1 - V_CHEST_TOP) - 0.012 * H;
  K.hip_x = 0.055 * H;
  K.hip_y = V_CROTCH * H + 0.025 * H;
  K.upper = elbow_x - K.sh_x;
  K.fore = wrist_x - elbow_x;
  K.hand = V_HAND * H;
  K.palm = 0.56 * V_HAND * H;
  return K;
}

/* ---- the bones and forward kinematics ---- */
typedef struct { int parent; V3 at, dir; double len; } Bone;
typedef struct { M3 R; V3 at, end, dir; double len; } Joint;

static void bones(const Skel *K, Bone B[FB_COUNT]) {
  V3 up = v3(0, 1, 0), down = v3(0, -1, 0);
  B[FB_HIPS] = (Bone){ -1, v3(0, K->hip_y, 0), up, K->chest_under - K->hip_y };
  B[FB_CHEST] = (Bone){ FB_HIPS, v3(0, K->chest_under, 0), up, K->chest_top - K->chest_under };
  B[FB_NECK] = (Bone){ FB_CHEST, v3(0, K->chest_top, 0), up, K->chin + 0.01 * K->H - K->chest_top };
  B[FB_HEAD] = (Bone){ FB_NECK, v3(0, K->chin + 0.01 * K->H, 0), up, K->crown - K->chin - 0.01 * K->H };
  for (int s = 0; s < 2; s++) {
    double x = s ? -1 : 1;
    B[FB_SHOULDER_L + s] = (Bone){ FB_CHEST, v3(0, K->sh_y, 0), v3(x, 0, 0), K->sh_x };
    B[FB_UPPER_L + s] = (Bone){ FB_SHOULDER_L + s, v3(x * K->sh_x, K->sh_y, 0), v3(x, 0, 0), K->upper };
    B[FB_FORE_L + s] = (Bone){ FB_UPPER_L + s, v3(x * (K->sh_x + K->upper), K->sh_y, 0), v3(x, 0, 0), K->fore };
    B[FB_HAND_L + s] = (Bone){ FB_FORE_L + s, v3(x * (K->sh_x + K->upper + K->fore), K->sh_y, 0), v3(x, 0, 0), K->hand };
    B[FB_THIGH_L + s] = (Bone){ FB_HIPS, v3(x * K->hip_x, K->hip_y, 0), down, K->hip_y - K->knee };
    B[FB_SHIN_L + s] = (Bone){ FB_THIGH_L + s, v3(x * K->hip_x, K->knee, 0), down, K->knee - K->ankle };
    B[FB_FOOT_L + s] = (Bone){ FB_SHIN_L + s, v3(x * K->hip_x, K->ankle, 0), v3(0, 0, 1), K->foot_len - 0.03 * K->H };
  }
}

static void solve(const Skel *K, const FigPose *pose, Joint W[FB_COUNT]) {
  Bone B[FB_COUNT];
  bones(K, B);
  M3 root_rot = ry(pose->yaw * D);
  V3 root = v3(pose->root[0], pose->root[1], pose->root[2]);
  for (int n = 0; n < FB_COUNT; n++) { /* the enum's order has every parent before its children */
    const Bone *b = &B[n];
    M3 local = rot(pose->ang[n]);
    Joint *w = &W[n];
    if (b->parent < 0) {
      w->R = mmul(root_rot, local);
      w->at = add(mv(root_rot, b->at), root);
    } else {
      const Joint *p = &W[b->parent];
      w->R = mmul(p->R, local);
      w->at = add(p->at, mv(p->R, sub(b->at, B[b->parent].at)));
    }
    w->dir = b->dir;
    w->len = b->len;
    w->end = add(w->at, mv(w->R, mul(b->dir, b->len)));
  }
}

/* ---- the mesh ---- */
#define SEG 10
#define HN 24

typedef struct {
  FigTri *t;
  int n, max;
  bool full;
} Out;

static void put(Out *o, V3 a, V3 b, V3 c, int role, int part, bool two) {
  if (o->n >= o->max) {
    o->full = true;
    return;
  }
  FigTri *t = &o->t[o->n++];
  for (int k = 0; k < 3; k++) t->v[0][k] = (float)a.v[k], t->v[1][k] = (float)b.v[k], t->v[2][k] = (float)c.v[k];
  t->role = (uint8_t)role;
  t->part = (uint8_t)part;
  t->two = two;
  t->shade = FS_ROLE;
  t->tag = t->emit = 0;
}
/* a triangle with its own colour */
static void put_own(Out *o, V3 a, V3 b, V3 c, int role, int part, bool two, const double col[3]) {
  put(o, a, b, c, role, part, two);
  if (o->full) return;
  FigTri *t = &o->t[o->n - 1];
  t->shade = FS_OWN;
  for (int k = 0; k < 3; k++) t->col[k] = (float)col[k];
}
/* a smooth triangle: each corner with its normal and colour */
static void put_smooth(Out *o, V3 a, V3 b, V3 c, int role, int part, bool two, const V3 n[3], const double (*cs)[3]) {
  put(o, a, b, c, role, part, two);
  if (o->full) return;
  FigTri *t = &o->t[o->n - 1];
  t->shade = FS_SMOOTH;
  for (int q = 0; q < 3; q++)
    for (int k = 0; k < 3; k++) t->ns[q][k] = (float)n[q].v[k], t->cs[q][k] = (float)cs[q][k];
}

typedef struct { V3 along, across, fwd; } Frame;
static Frame frame(const Joint *w) {
  Frame F;
  F.along = mv(w->R, w->dir);
  V3 fwd0 = fabs(w->dir.v[2]) > 0.9 ? v3(0, -1, 0) : v3(0, 0, 1);
  F.fwd = mv(w->R, fwd0);
  F.across = norm(cross(F.along, F.fwd));
  return F;
}

/* a ring: [t along the bone, half-width, half-depth, forward offset, role] */
typedef struct { double t, w, d, z; int role; } Ring;

static void sides(const Skel *K, const Joint *w, const Ring *rings, int nr, int part, Out *o) {
  Frame F = frame(w);
  V3 base[8][SEG];
  for (int k = 0; k < nr; k++) {
    V3 c = add(w->at, mul(F.along, rings[k].t * w->len));
    for (int i = 0; i < SEG; i++) {
      double a = (double)i / SEG * PI * 2, cx = spow(cos(a), 0.8), cz = spow(sin(a), 0.8);
      base[k][i] = add(c, add(mul(F.across, cx * rings[k].w * K->H), mul(F.fwd, cz * rings[k].d * K->H + rings[k].z * K->H)));
    }
  }
  for (int k = 0; k + 1 < nr; k++)
    for (int i = 0; i < SEG; i++) {
      int j = (i + 1) % SEG;
      put(o, base[k][i], base[k + 1][j], base[k][j], rings[k].role, part, false);
      put(o, base[k][i], base[k + 1][i], base[k + 1][j], rings[k].role, part, false);
    }
  for (int e = 0; e < 2; e++) { /* close both ends */
    int k = e ? nr - 1 : 0;
    V3 c = v3(0, 0, 0);
    for (int i = 0; i < SEG; i++) c = add(c, base[k][i]);
    c = mul(c, 1.0 / SEG);
    for (int i = 0; i < SEG; i++) {
      if (e) put(o, base[k][(i + 1) % SEG], base[k][i], c, rings[k].role, part, false);
      else put(o, base[k][i], base[k][(i + 1) % SEG], c, rings[k].role, part, false);
    }
  }
}

/* a tube between two points, its cross-section a rounded box (half-width along `across`) */
static void tube(const Skel *K, V3 a, V3 b, V3 across, double w0, double d0, double w1, double d1, int role, int part, Out *o) {
  V3 along = norm(sub(b, a)), x = norm(sub(across, mul(along, dot(across, along)))), z = cross(x, along);
  V3 A[SEG], B[SEG];
  for (int i = 0; i < SEG; i++) {
    double t = (double)i / SEG * PI * 2, cx = spow(cos(t), 0.8), cz = spow(sin(t), 0.8);
    A[i] = add(a, add(mul(x, cx * w0 * K->H), mul(z, cz * d0 * K->H)));
    B[i] = add(b, add(mul(x, cx * w1 * K->H), mul(z, cz * d1 * K->H)));
  }
  for (int i = 0; i < SEG; i++) {
    int j = (i + 1) % SEG;
    put(o, A[i], B[j], A[j], role, part, false);
    put(o, A[i], B[i], B[j], role, part, false);
    put(o, A[i], A[j], a, role, part, false);
    put(o, B[j], B[i], b, role, part, false);
  }
}

/* ---- the head, by the face's thirds (see figure.js for the table's meaning) ---- */
static const double HEAD[18][4] = { /* u, half-width, forward (the face's profile), back: about 3/4 as broad as tall */
  { 0.0, 0.112, 0.30, -0.05 }, { 0.04, 0.16, 0.40, -0.1 }, { 0.1, 0.224, 0.41, -0.2 }, { 0.16, 0.264, 0.43, -0.3 },
  { 0.2, 0.28, 0.44, -0.34 }, { 0.24, 0.296, 0.45, -0.38 }, { 0.267, 0.312, 0.44, -0.4 }, { 0.33, 0.336, 0.43, -0.43 },
  { 0.4, 0.352, 0.44, -0.46 }, { 0.47, 0.368, 0.43, -0.48 }, { 0.533, 0.368, 0.465, -0.49 }, { 0.6, 0.368, 0.46, -0.5 },
  { 0.7, 0.368, 0.445, -0.5 }, { 0.8, 0.36, 0.41, -0.49 }, { 0.88, 0.328, 0.34, -0.44 }, { 0.94, 0.256, 0.24, -0.34 },
  { 0.98, 0.144, 0.12, -0.18 }, { 1.0, 0.032, 0.02, -0.03 } };
#define NHEAD 18

static void head_at(double u, double *hw, double *zf, double *zb) {
  for (int k = 0; k + 1 < NHEAD; k++)
    if (u <= HEAD[k + 1][0]) {
      double t = (u - HEAD[k][0]) / (HEAD[k + 1][0] - HEAD[k][0]);
      *hw = HEAD[k][1] + (HEAD[k + 1][1] - HEAD[k][1]) * t;
      *zf = HEAD[k][2] + (HEAD[k + 1][2] - HEAD[k][2]) * t;
      *zb = HEAD[k][3] + (HEAD[k + 1][3] - HEAD[k][3]) * t;
      return;
    }
  *hw = HEAD[NHEAD - 1][1], *zf = HEAD[NHEAD - 1][2], *zb = HEAD[NHEAD - 1][3];
}
static void surf(double u, double a, double *x, double *z) {
  double hw, zf, zb, ca = cos(a), sa = sin(a);
  head_at(u, &hw, &zf, &zb);
  *x = spow(ca, 0.85) * hw;
  *z = sa > 0 ? pow(sa, 0.9) * zf : sa * -zb;
}
static double angle_at(double u, double x) {
  double hw, zf, zb;
  head_at(u, &hw, &zf, &zb);
  double c = x / hw;
  if (c < -1) c = -1;
  if (c > 1) c = 1;
  return acos(spow(c, 1 / 0.85));
}

static void quad(Out *o, V3 a, V3 b, V3 c, V3 d, int role, int part) {
  put(o, a, b, c, role, part, true);
  put(o, a, c, d, role, part, true);
}

/* ---- the face engine (see figure.js: the same landmarks, shapes, expressions and colours) ---- */
#define FU 36
#define FA 40
#define EYE_U 0.5
typedef struct { double EW, eyeX, inner, outer, eyeU, brow, noseBase, hairline, mouthU, mouthX, noseHalf, earTop, earBottom; } Land;
static Land landmarks(void) {
  double hw, zf, zb;
  head_at(EYE_U, &hw, &zf, &zb);
  Land L;
  L.EW = 2 * hw / 5; /* five eyes across the head at the eyes */
  L.eyeX = L.EW, L.inner = L.EW / 2, L.outer = 1.5 * L.EW, L.eyeU = EYE_U;
  L.brow = 2.0 / 3 * 0.8, L.noseBase = 1.0 / 3 * 0.8, L.hairline = 0.8, L.mouthU = 0.8 / 3 * 2 / 3, L.mouthX = L.EW, L.noseHalf = L.EW / 2;
  L.earTop = 2.0 / 3 * 0.8, L.earBottom = 1.0 / 3 * 0.8;
  return L;
}
static double gauss(double d2, double r) { return exp(-d2 / (r * r)); }
static double sq(double x) { return x * x; }
static double maxd(double a, double b) { return a > b ? a : b; }
static double mind(double a, double b) { return a < b ? a : b; }
/* the face's own value noise: a hash of a lattice, smoothly blended */
static double hash2(double fi, double fj) {
  int64_t i = (int64_t)fi, j = (int64_t)fj;
  uint32_t h = (uint32_t)(i * 374761393 + j * 668265263);
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return h / 4294967296.0;
}
static double noise2(double x, double y) {
  double i = floor(x), j = floor(y), fx = x - i, fy = y - j, sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  double a = hash2(i, j), b = hash2(i + 1, j), c = hash2(i, j + 1), d = hash2(i + 1, j + 1);
  return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
typedef struct { double open, wide, round, smile, browUp, frown, blink; } Ex;

static void head_shape(double u, double a, const FigFace *fp, const Ex *ex, const Land *L, double *ox, double *oz, double *ou) {
  double x, z;
  surf(u, a, &x, &z);
  int front = sin(a) > 0;
  double ax = fabs(x), dz = 0, du = 0;
  if (front) {
    dz += (0.015 + 0.04 * fp->brow) * gauss(sq(u - 0.56) * 6 + sq(maxd(0, ax - 0.32)), 0.06);
    dz -= (0.02 + 0.05 * fp->socket) * gauss(sq(u - L->eyeU) + sq(ax - L->eyeX), 0.075);
    dz += (0.01 + 0.035 * fp->cheek) * gauss(sq(u - 0.4) + sq(ax - 0.3), 0.09);
    dz += (0.01 + 0.04 * fp->chin) * gauss(sq(u - 0.05) * 3 + x * x, 0.1);
    double mw = L->mouthX * (1 + 0.25 * ex->wide + 0.15 * ex->smile - 0.35 * ex->round);
    double inMouth = gauss(sq(maxd(0, ax - mw * 0.8)), 0.05);
    dz += (0.008 + 0.022 * fp->lips) * gauss(sq(u - L->mouthU - 0.028), 0.022) * inMouth * (1 + 0.6 * ex->round);
    dz += (0.008 + 0.026 * fp->lips) * gauss(sq(u - L->mouthU + 0.03), 0.024) * inMouth * (1 + 0.6 * ex->round);
    dz -= 0.012 * gauss(sq(u - L->mouthU), 0.01) * inMouth;
    double fold = L->noseHalf + (L->mouthX - L->noseHalf) * (L->noseBase - u) / (L->noseBase - L->mouthU);
    dz -= 0.012 * fp->age * gauss(sq(ax - fold) + 0, 0.03) * (u < L->noseBase && u > L->mouthU - 0.02 ? 1 : 0);
    du += ex->smile * 0.02 * gauss(sq(u - L->mouthU) + sq(ax - L->mouthX), 0.06);
    dz += ex->smile * 0.012 * gauss(sq(u - 0.36) + sq(ax - 0.26), 0.07);
  }
  if (u < 0.3) x *= 1 + 0.22 * (fp->jaw - 0.5) * (1 - u / 0.3);
  double n = sqrt(x * x + z * z);
  if (n == 0) n = 1;
  *ox = x + x / n * dz, *oz = z + z / n * dz, *ou = u + du;
}

typedef struct {
  const Skel *K;
  Frame F;
  V3 bottom;
  double hH, jawA, MW;
  Land L;
  const FigFace *fp;
  const FigSpec *fig;
} HeadCtx;
static double jaw_weight(const HeadCtx *h, double u, double x) {
  const Land *L = &h->L;
  double soft = u >= L->mouthU + 0.02 ? 0 : u <= L->mouthU - 0.06 ? 1 : (L->mouthU + 0.02 - u) / 0.08;
  double sharp = u < L->mouthU ? 1 : 0, inside = maxd(0, mind(1, (h->MW * 1.05 - fabs(x)) / (h->MW * 0.25)));
  return sharp * inside + soft * (1 - inside);
}
static V3 HP0(const HeadCtx *h, double u, double x, double z) {
  return add(h->bottom, add(mul(h->F.along, u * h->hH), add(mul(h->F.across, x * h->hH), mul(h->F.fwd, z * h->hH))));
}
static V3 HP(const HeadCtx *h, double u, double x, double z) {
  double k = jaw_weight(h, u, x) * h->jawA;
  if (k != 0) {
    double du = u - 0.34, dz = z - -0.12, c = cos(k), s = sin(k);
    u = 0.34 + du * c - dz * s;
    z = -0.12 + du * s + dz * c;
  }
  return HP0(h, u, x, z);
}
static double smooth01(double e0, double e1, double v) {
  double t = maxd(0, mind(1, (v - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}
static double hair_amount(const Land *L, double u, double x, double z) {
  double a = smooth01(L->hairline - 0.02, L->hairline + 0.03, u + 0.015 * cos(x * 6)) * smooth01(-0.1, 0.0, z + 0.6 - u);
  double b = smooth01(0.0, -0.1, z) * smooth01(0.38, 0.46, u);
  double c = smooth01(0.33, 0.4, fabs(x)) * smooth01(0.56, 0.64, u) * smooth01(0.3, 0.18, z);
  return maxd(a, maxd(b, c));
}
static void tone(const HeadCtx *h, double u, double x, double z, double c[3]) {
  const FigFace *fp = h->fp;
  const Land *L = &h->L;
  double nv = 0.96 + 0.08 * noise2(x * 9 + 3, u * 9);
  for (int i = 0; i < 3; i++) c[i] = h->fig->skin[i] * nv;
  double warm = 0.08 * gauss(sq(u - 0.38) + sq(fabs(x) - 0.26), 0.08) + 0.06 * gauss(sq(u - 0.32) + x * x, 0.05);
  c[0] *= 1 + warm, c[1] *= 1 - warm * 0.3, c[2] *= 1 - warm * 0.4;
  double lip = gauss(sq(u - L->mouthU) * 1.2, 0.045) * gauss(sq(maxd(0, fabs(x) - L->mouthX * 0.85)), 0.03) * (z > 0.2 ? 1 : 0);
  static const double LIPC[3] = { 170, 82, 80 };
  if (lip > 0.05)
    for (int i = 0; i < 3; i++) c[i] = c[i] * (1 - lip * 0.35) + LIPC[i] * lip * 0.35;
  double st = fp->stubble * (u < L->mouthU + 0.04 ? 1 : u < L->noseBase ? maxd(0, fabs(x) - L->noseHalf) * 3 : 0) * (z > -0.2 ? 1 : 0);
  if (st > 0) {
    double k = mind(1, st) * (0.25 + 0.2 * noise2(x * 40, u * 40));
    for (int i = 0; i < 3; i++) c[i] = c[i] * (1 - k) + h->fig->hair[i] * 0.6 * k;
  }
  if (fp->age > 0.2 && z > 0.2) {
    double fl = u > 0.62 && u < 0.76 ? pow(maxd(0, sin((u - 0.62) * 90)), 8) : 0;
    double cf = gauss(sq(u - L->eyeU) + sq(fabs(x) - L->outer - 0.05), 0.05) * pow(maxd(0, sin(u * 160)), 6);
    double k = (fl + cf) * 0.12 * fp->age;
    for (int i = 0; i < 3; i++) c[i] *= 1 - k;
  }
}

static void head(const Skel *K, const Joint *w, const FigSpec *fig, Out *o) {
  HeadCtx h;
  h.K = K;
  h.F = frame(w);
  h.hH = K->H / 8;
  h.bottom = add(w->at, mul(h.F.along, -0.01 * K->H));
  h.L = landmarks();
  h.fp = &fig->shape;
  h.fig = fig;
  const Land *L = &h.L;
  const FigFace *fp = &fig->shape;
  Ex ex = { fig->face.open, fig->face.wide, fig->face.round, fig->face.smile, fig->face.brow_up, fig->face.frown, fig->face.blink };
  h.jawA = ex.open * 0.26;
  h.MW = L->mouthX * (1 + 0.25 * ex.wide + 0.15 * ex.smile - 0.35 * ex.round);
  /* the grid's rows: finer round the face, and two rows at the mouth's line, the lips' parting */
  double us[FU + 3];
  int nu = 0, lip_row = -1;
  for (int k = 0; k <= FU; k++) {
    double t = (double)k / FU, u = t * t * (3 - 2 * t) * 0.5 + t * 0.5;
    if (fabs(u - L->mouthU) > 0.008) us[nu++] = u;
  }
  us[nu++] = L->mouthU - 0.002;
  us[nu++] = L->mouthU + 0.002;
  for (int a = 1; a < nu; a++) /* sort */
    for (int b = a; b > 0 && us[b] < us[b - 1]; b--) {
      double t = us[b];
      us[b] = us[b - 1], us[b - 1] = t;
    }
  for (int k = 0; k < nu; k++)
    if (us[k] == L->mouthU - 0.002) lip_row = k;
  static V3 GP[FU + 3][FA], GN[FU + 3][FA];
  static double GC[FU + 3][FA][3], GX[FU + 3][FA], GZ[FU + 3][FA];
  static bool GH[FU + 3][FA];
  for (int k = 0; k < nu; k++)
    for (int i = 0; i < FA; i++) {
      double a = (double)i / FA * PI * 2, x, z, uu;
      head_shape(us[k], a, fp, &ex, L, &x, &z, &uu);
      GP[k][i] = HP(&h, uu, x, z);
      double hm = hair_amount(L, us[k], x, z), sk[3], nh = 0.9 + 0.2 * noise2(x * 30 + 7, us[k] * 30);
      tone(&h, us[k], x, z, sk);
      for (int c = 0; c < 3; c++) GC[k][i][c] = sk[c] * (1 - hm) + fig->hair[c] * nh * hm;
      GH[k][i] = hm > 0.5, GX[k][i] = x, GZ[k][i] = z;
    }
  for (int k = 0; k < nu; k++)
    for (int i = 0; i < FA; i++) {
      V3 a = GP[k + 1 < nu ? k + 1 : nu - 1][i], b = GP[k > 0 ? k - 1 : 0][i], c = GP[k][(i + 1) % FA], d = GP[k][(i + FA - 1) % FA];
      GN[k][i] = norm(cross(sub(c, d), sub(a, b)));
    }
  for (int k = 0; k + 1 < nu; k++)
    for (int i = 0; i < FA; i++) {
      int j = (i + 1) % FA;
      if (k == lip_row && GZ[k][i] > 0.2 && fabs(GX[k][i]) < h.MW * 0.98 && fabs(GX[k][j]) < h.MW * 0.98) continue;
      int r1 = GH[k][i] && GH[k + 1][j] && GH[k][j] ? FR_HAIR : FR_SKIN, r2 = GH[k][i] && GH[k + 1][i] && GH[k + 1][j] ? FR_HAIR : FR_SKIN;
      V3 n1[3] = { GN[k][i], GN[k + 1][j], GN[k][j] }, n2[3] = { GN[k][i], GN[k + 1][i], GN[k + 1][j] };
      const double *c1[3] = { GC[k][i], GC[k + 1][j], GC[k][j] }, *c2[3] = { GC[k][i], GC[k + 1][i], GC[k + 1][j] };
      double cc1[3][3], cc2[3][3];
      for (int q = 0; q < 3; q++)
        for (int c = 0; c < 3; c++) cc1[q][c] = c1[q][c], cc2[q][c] = c2[q][c];
      put_smooth(o, GP[k][i], GP[k + 1][j], GP[k][j], r1, FP_HEAD, false, n1, (const double(*)[3])cc1);
      put_smooth(o, GP[k][i], GP[k + 1][i], GP[k + 1][j], r2, FP_HEAD, false, n2, (const double(*)[3])cc2);
    }
  V3 c0 = HP(&h, 0, 0, 0.12);
  double t0c[3];
  tone(&h, 0, 0, 0.1, t0c);
  for (int i = 0; i < FA; i++) put_own(o, GP[0][i], GP[0][(i + 1) % FA], c0, FR_SKIN, FP_HEAD, false, t0c);
  { /* the mouth's inside, dark, and the upper teeth when the jaw opens */
    double mz = 0.38, mw = h.MW * 0.95;
    static const double DARK[3] = { 52, 20, 22 }, TEETH[3] = { 214, 208, 196 };
    double u0 = L->mouthU - 0.02 - ex.open * 0.09, u1 = L->mouthU + 0.01;
    V3 A = HP0(&h, u0, -mw, mz), B = HP0(&h, u0, mw, mz), C2 = HP0(&h, u1, mw, mz), D2 = HP0(&h, u1, -mw, mz);
    put_own(o, A, B, C2, FR_MOUTH, FP_FACE, true, DARK);
    put_own(o, A, C2, D2, FR_MOUTH, FP_FACE, true, DARK);
    if (ex.open > 0.05) {
      u0 = L->mouthU - 0.006 - 0.012 * ex.open, u1 = L->mouthU + 0.004;
      A = HP0(&h, u0, -mw * 0.7, mz + 0.03), B = HP0(&h, u0, mw * 0.7, mz + 0.03), C2 = HP0(&h, u1, mw * 0.7, mz + 0.03), D2 = HP0(&h, u1, -mw * 0.7, mz + 0.03);
      put_own(o, A, B, C2, FR_MOUTH, FP_FACE, true, TEETH);
      put_own(o, A, C2, D2, FR_MOUTH, FP_FACE, true, TEETH);
    }
  }
  { /* the nose: rings from between the eyes to the tip, then in to the base between the wings */
    double bridge = 0.6 + 0.8 * fp->bridge, tipk = 0.7 + 0.6 * fp->tip;
    double NRd[7][3] = { { 0.53, 0.04, 0.01 * bridge }, { 0.47, 0.05, 0.03 * bridge }, { 0.41, 0.06, 0.05 * bridge }, { 0.35, 0.075, 0.075 * tipk },
                         { 0.31, 0.09, 0.09 * tipk }, { 0.285, L->noseHalf, 0.07 * tipk }, { 0.268, L->noseHalf * 0.85, 0.035 } };
    V3 NP[7][9], NN[7][9];
    double NC[7][9][3];
    for (int k = 0; k < 7; k++) {
      double sx, zf;
      surf(NRd[k][0], PI / 2, &sx, &zf);
      for (int i = 0; i <= 8; i++) {
        double a = PI * i / 8, x = cos(a) * NRd[k][1], zz = zf + sin(a) * NRd[k][2] - 0.004;
        NP[k][i] = HP(&h, NRd[k][0], x, zz);
        tone(&h, NRd[k][0], x, zz, NC[k][i]);
      }
    }
    for (int k = 0; k < 7; k++)
      for (int i = 0; i <= 8; i++) {
        V3 up = NP[k > 0 ? k - 1 : 0][i], dn = NP[k < 6 ? k + 1 : 6][i], l = NP[k][i > 0 ? i - 1 : 0], r = NP[k][i < 8 ? i + 1 : 8];
        NN[k][i] = norm(cross(sub(r, l), sub(dn, up)));
      }
    for (int k = 0; k + 1 < 7; k++)
      for (int i = 0; i < 8; i++) {
        V3 n1[3] = { NN[k][i], NN[k + 1][i], NN[k + 1][i + 1] }, n2[3] = { NN[k][i], NN[k + 1][i + 1], NN[k][i + 1] };
        double c1[3][3], c2[3][3];
        for (int c = 0; c < 3; c++)
          c1[0][c] = NC[k][i][c], c1[1][c] = NC[k + 1][i][c], c1[2][c] = NC[k + 1][i + 1][c], c2[0][c] = NC[k][i][c], c2[1][c] = NC[k + 1][i + 1][c],
          c2[2][c] = NC[k][i + 1][c];
        put_smooth(o, NP[k][i], NP[k + 1][i], NP[k + 1][i + 1], FR_SKIN, FP_FACE, true, n1, (const double(*)[3])c1);
        put_smooth(o, NP[k][i], NP[k + 1][i + 1], NP[k][i + 1], FR_SKIN, FP_FACE, true, n2, (const double(*)[3])c2);
      }
    static const double NOSTRIL[3] = { 70, 40, 38 };
    for (int e = 0; e < 2; e++) {
      double sx = e ? -1 : 1, xx, zf;
      surf(0.275, PI / 2, &xx, &zf);
      put_own(o, HP(&h, 0.272, sx * 0.02, zf + 0.06), HP(&h, 0.272, sx * 0.065, zf + 0.04), HP(&h, 0.278, sx * 0.045, zf + 0.075), FR_MOUTH, FP_FACE, true, NOSTRIL);
    }
  }
  /* the eyes: eyeballs in their sockets, the iris and pupil turned to the gaze, lids that close to blink */
  V3 gaze = h.F.fwd;
  if (fig->face.has_gaze) gaze = norm(sub(v3(fig->face.gaze[0], fig->face.gaze[1], fig->face.gaze[2]), add(h.bottom, mul(h.F.along, L->eyeU * h.hH))));
  for (int e = 0; e < 2; e++) {
    double sx = e ? -1 : 1, rE = L->EW * 0.5, xx, zf;
    surf(L->eyeU, angle_at(L->eyeU, L->eyeX * sx), &xx, &zf);
    V3 ec = HP(&h, L->eyeU, L->eyeX * sx, zf - rE * 0.9 - 0.03 * fp->socket);
    double RE = rE * h.hH;
    V3 ax = h.F.across, up = h.F.along, fw = h.F.fwd;
    { /* the eyeball */
      static const double WHITE[3] = { 222, 216, 208 };
      V3 pts[7][10];
      for (int k = 0; k <= 6; k++) {
        double b = -PI / 2 + PI * k / 6;
        for (int i = 0; i < 10; i++) {
          double a = (double)i / 10 * PI * 2;
          pts[k][i] = add(ec, add(mul(ax, cos(b) * cos(a) * RE), add(mul(up, sin(b) * RE), mul(fw, cos(b) * sin(a) * RE))));
        }
      }
      for (int k = 0; k < 6; k++)
        for (int i = 0; i < 10; i++) {
          int j = (i + 1) % 10;
          put_own(o, pts[k][i], pts[k + 1][j], pts[k][j], FR_EYE_WHITE, FP_EYE, false, WHITE);
          put_own(o, pts[k][i], pts[k + 1][i], pts[k + 1][j], FR_EYE_WHITE, FP_EYE, false, WHITE);
        }
    }
    V3 g = norm(add(mul(gaze, 1), mul(fw, 0.4))), gx = norm(cross(up, g)), gy = cross(g, gx);
    double iris[3] = { fig->iris[0], fig->iris[1], fig->iris[2] };
    static const double PUPIL[3] = { 10, 10, 12 }, SHINE[3] = { 235, 235, 230 };
    const double *dcol[3] = { iris, PUPIL, SHINE };
    static const double DR[3] = { 0.46, 0.19, 0.065 }, DL[3] = { 1.0, 1.012, 1.022 };
    for (int d = 0; d < 3; d++) {
      V3 c = add(ec, mul(g, RE * DL[d])), ring[10];
      for (int i = 0; i < 10; i++) {
        double a = (double)i / 10 * PI * 2;
        ring[i] = add(c, add(mul(gx, cos(a) * RE * DR[d]), mul(gy, sin(a) * RE * DR[d])));
      }
      for (int i = 0; i < 10; i++) put_own(o, c, ring[i], ring[(i + 1) % 10], FR_IRIS, FP_EYE, true, dcol[d]);
    }
    double open = -0.36 + (0.62 + 0.1 * ex.browUp - 0.08 * ex.frown) * (1 - ex.blink);
    for (int l = 0; l < 2; l++) { /* the lids: the upper comes down to blink, the lower stays */
      double from = l ? -0.38 : PI / 2, to = l ? -PI / 2 : open, lc[3];
      tone(&h, L->eyeU + (l ? -0.04 : 0.04), L->eyeX, zf, lc);
      for (int c = 0; c < 3; c++) lc[c] *= l ? 0.95 : 0.92;
      V3 pts[4][11];
      double r = RE * 1.07;
      for (int k = 0; k <= 3; k++) {
        double b = from + (to - from) * k / 3;
        for (int i = 0; i <= 10; i++) {
          double a = PI * 0.08 + PI * 0.84 * i / 10;
          pts[k][i] = add(ec, add(mul(ax, cos(b) * cos(a) * r), add(mul(up, sin(b) * r), mul(fw, cos(b) * sin(a) * r))));
        }
      }
      for (int k = 0; k < 3; k++)
        for (int i = 0; i < 10; i++) {
          put_own(o, pts[k][i], pts[k + 1][i + 1], pts[k][i + 1], FR_LID, FP_EYE, true, lc);
          put_own(o, pts[k][i], pts[k + 1][i], pts[k + 1][i + 1], FR_LID, FP_EYE, true, lc);
        }
    }
    { /* the brow: a band of hair over the ridge, the inner end lifting or drawing down */
      V3 bp[5][2];
      for (int q = 0; q < 5; q++) {
        double k = q * 0.25, bu = 0.57 + 0.035 * ex.browUp * (1 - k * 0.3) - 0.025 * ex.frown * (1 - k);
        double x = sx * (L->inner * 0.9 + (L->outer + 0.06 - L->inner * 0.9) * k), bx, zb;
        surf(bu, angle_at(bu, x), &bx, &zb);
        bp[q][0] = HP(&h, bu + 0.018 * sin(k * PI), x, zb + 0.022);
        bp[q][1] = HP(&h, bu - 0.012, x, zb + 0.022);
      }
      for (int q = 0; q + 1 < 5; q++) {
        put(o, bp[q][0], bp[q + 1][0], bp[q + 1][1], FR_BROW, FP_FACE, true);
        put(o, bp[q][0], bp[q + 1][1], bp[q][1], FR_BROW, FP_FACE, true);
      }
    }
    { /* the ear, between the brow line and the nose's base, flat against the head */
      V3 ec2 = HP(&h, (L->earTop + L->earBottom) / 2, 0.375 * sx, -0.06);
      double half = (L->earTop - L->earBottom) / 2 * h.hH;
      V3 e0 = add(ec2, mul(h.F.along, -half)), e1 = add(ec2, mul(h.F.along, half * 0.15)), e2 = add(ec2, mul(h.F.along, half));
      tube(K, e0, e1, h.F.fwd, 0.006, 0.003, 0.012, 0.0035, FR_SKIN, FP_NONE, o);
      tube(K, e1, e2, h.F.fwd, 0.012, 0.0035, 0.007, 0.003, FR_SKIN, FP_NONE, o);
    }
  }
  const FigKit *C = &fig->kit;
  if (C->cap) { /* the ball cap: the crown from just above the brows over the top, the bill over the eyes */
    static V3 caps[NHEAD][HN];
    int nc = 0;
    for (int k = 0; k < NHEAD; k++) {
      if (HEAD[k][0] < 0.7) continue;
      double u = HEAD[k][0], hw = HEAD[k][1] * 1.06 + 0.02, zf = HEAD[k][2] * 1.06 + 0.02, zb = HEAD[k][3] * 1.06 - 0.02;
      for (int i = 0; i < HN; i++) {
        double a = (double)i / HN * PI * 2, ca = cos(a), sa = sin(a);
        caps[nc][i] = add(h.bottom, add(mul(h.F.along, (u + 0.03) * h.hH),
                                        add(mul(h.F.across, spow(ca, 0.85) * hw * h.hH), mul(h.F.fwd, (sa > 0 ? sa * zf : -sa * zb) * h.hH))));
      }
      nc++;
    }
    for (int k = 0; k + 1 < nc; k++)
      for (int i = 0; i < HN; i++) {
        int j = (i + 1) % HN;
        put(o, caps[k][i], caps[k + 1][j], caps[k][j], FR_CAP, FP_NONE, false);
        put(o, caps[k][i], caps[k + 1][i], caps[k + 1][j], FR_CAP, FP_NONE, false);
      }
    quad(o, HP(&h, 0.725, -0.35, 0.44), HP(&h, 0.725, 0.35, 0.44), HP(&h, 0.69, 0.28, 0.86), HP(&h, 0.69, -0.28, 0.86), FR_CAP, FP_NONE);
    if (C->gold) quad(o, HP(&h, 0.712, -0.31, 0.6), HP(&h, 0.712, 0.31, 0.6), HP(&h, 0.692, 0.27, 0.84), HP(&h, 0.692, -0.27, 0.84), FR_GOLD, FP_NONE);
    quad(o, HP(&h, 0.79, -0.18, 0.46), HP(&h, 0.79, 0.18, 0.46), HP(&h, 0.88, 0.15, 0.4), HP(&h, 0.88, -0.15, 0.4), FR_GOLD, FP_NONE);
  }
  if (C->headset) { /* a band over the crown, cups over the ears, a boom to the corner of the mouth */
    V3 band[9];
    for (int i = 0; i <= 8; i++) {
      double a = PI * i / 8;
      band[i] = HP(&h, 0.4 + sin(a) * 0.62, cos(a) * 0.42, -0.03);
    }
    for (int i = 0; i + 1 < 9; i++) tube(K, band[i], band[i + 1], h.F.fwd, 0.004, 0.003, 0.004, 0.003, FR_KIT, FP_NONE, o);
    for (int e = 0; e < 2; e++) {
      double sx = e ? -1 : 1;
      V3 c = HP(&h, 0.4, 0.42 * sx, -0.04);
      tube(K, sub(c, mul(h.F.across, 0.04 * sx * h.hH)), add(c, mul(h.F.across, 0.04 * sx * h.hH)), h.F.fwd, 0.011, 0.012, 0.011, 0.012, FR_KIT, FP_NONE, o);
    }
    tube(K, HP(&h, 0.38, 0.42, 0.0), HP(&h, 0.22, 0.2, 0.45), h.F.across, 0.0015, 0.0015, 0.0015, 0.0015, FR_KIT, FP_NONE, o);
    tube(K, HP(&h, 0.22, 0.2, 0.45), HP(&h, 0.2, 0.08, 0.5), h.F.across, 0.004, 0.004, 0.004, 0.004, FR_KIT, FP_NONE, o);
  }
}

/* ---- uniform details ---- */
static V3 on_chest(const Joint *ch, const Frame *F, double H, double y, double x, double z) {
  return add(ch->at, add(mul(F->along, y * H), add(mul(F->across, x * H), mul(F->fwd, z * H))));
}
static void kit(const Skel *K, const Joint W[FB_COUNT], const FigSpec *fig, Out *o) {
  const FigKit *C = &fig->kit;
  const Joint *ch = &W[FB_CHEST];
  Frame F = frame(ch);
  double H = K->H;
#define ON(y, x, z) on_chest(ch, &F, H, (y), (x), (z))
  quad(o, ON(0.085, -0.03, 0.05), ON(0.085, 0.03, 0.05), ON(0.06, 0.006, 0.071), ON(0.06, -0.006, 0.071), FR_TEE, FP_NONE);
  for (int e = 0; e < 2; e++) { /* breast pockets */
    double sx = e ? -1 : 1;
    quad(o, ON(0.05, 0.02 * sx, 0.075), ON(0.05, 0.065 * sx, 0.072), ON(0.0, 0.065 * sx, 0.074), ON(0.0, 0.02 * sx, 0.077), FR_POCKET, FP_NONE);
  }
  if (C->nametape) quad(o, ON(0.06, 0.02, 0.0745), ON(0.06, 0.07, 0.072), ON(0.052, 0.07, 0.0725), ON(0.052, 0.02, 0.075), FR_TAPE, FP_NONE);
  if (C->device) /* the collar devices: an anchor for a chief, the oak leaf for a commander */
    for (int e = 0; e < 2; e++) {
      double sx = e ? -1 : 1;
      quad(o, ON(0.083, 0.035 * sx, 0.052), ON(0.083, 0.05 * sx, 0.048), ON(0.07, 0.05 * sx, 0.056), ON(0.07, 0.035 * sx, 0.06),
           C->device == FD_ANCHOR ? FR_ANCHOR : FR_OAK, FP_NONE);
    }
#undef ON
  if (C->clipboard) { /* in the left hand */
    const Joint *hj = &W[FB_HAND_L];
    Frame hf = frame(hj);
    V3 c = add(hj->at, mul(hf.along, K->palm * 0.6));
    V3 ax = mul(hf.along, 0.06 * H), ay = mul(cross(hf.along, hf.across), 0.085 * H), n = mul(hf.across, -0.012 * H);
    V3 cn = add(c, n);
    quad(o, add(cn, sub(ay, ax)), add(cn, add(ay, ax)), add(cn, sub(mul(ay, -1), mul(ax, -1))), add(cn, sub(mul(ay, -1), ax)), FR_PAPER, FP_NONE);
  }
}

/* ring tables in fractions of H (a fit adult build) */
static const double ARM_UPPER[4][3] = { { 0, 0.026, 0.027 }, { 0.3, 0.025, 0.026 }, { 0.65, 0.022, 0.022 }, { 1, 0.019, 0.019 } };
static const double ARM_FORE[4][3] = { { 0, 0.019, 0.019 }, { 0.3, 0.02, 0.018 }, { 0.75, 0.016, 0.013 }, { 1, 0.0135, 0.01 } };
static const double ARM_PALM[3][3] = { { 0, 0.016, 0.009 }, { 0.45, 0.026, 0.009 }, { 1, 0.025, 0.008 } };
static const double LEG_THIGH[4][4] = { { 0, 0.046, 0.05, 0 }, { 0.4, 0.043, 0.045, 0 }, { 0.85, 0.03, 0.032, 0 }, { 1, 0.027, 0.03, 0 } };
static const double LEG_SHIN[4][4] = { { 0, 0.027, 0.03, 0 }, { 0.3, 0.028, 0.034, -0.004 }, { 0.75, 0.02, 0.021, 0 }, { 1, 0.016, 0.018, 0 } };

int fig_build(const FigSpec *fig, FigTri *outp, int max) {
  Out O = { outp, 0, max, false }, *o = &O;
  Skel K = skeleton(fig->height > 0 ? fig->height : 1.75);
  double H = K.H;
  Joint W[FB_COUNT];
  solve(&K, &fig->pose, W);
  /* the trunk: rings from the crotch to the base of the neck, wider at the chest, the waist taken in at the navel;
   * below the chest's joint they follow the hips, above it the chest */
  static const double TRD[9][4] = { { 0.07, 0.055, 0 }, { 0.088, 0.062, -0.004 }, { 0.084, 0.058, 0 }, { 0.078, 0.057, 0.004 }, { 0.083, 0.06, 0.004 },
                                    { 0.092, 0.066, 0.01 }, { 0.097, 0.066, 0.01 }, { 0.096, 0.056, 0 }, { 0.05, 0.042, 0 } };
  static const int TRR[9] = { FR_TROUSERS, FR_TROUSERS, FR_BELT, FR_SHIRT, FR_SHIRT, FR_SHIRT, FR_SHIRT, FR_SHIRT, FR_COLLAR };
  double TY[9] = { K.crotch - 0.02 * H, K.crotch + 0.03 * H, K.crotch + 0.07 * H, K.navel - 0.015 * H, K.navel + 0.05 * H,
                   K.chest_under, K.chest_under + 0.04 * H, K.sh_y - 0.004 * H, K.chest_top + 0.004 * H };
  V3 tb[9][SEG];
  for (int r = 0; r < 9; r++) {
    const Joint *b = TY[r] <= K.chest_under ? &W[FB_HIPS] : &W[FB_CHEST];
    double b0 = TY[r] <= K.chest_under ? K.hip_y : K.chest_under, t = (TY[r] - b0) / b->len;
    Frame F = frame(b);
    V3 c = add(b->at, mul(F.along, t * b->len));
    for (int i = 0; i < SEG; i++) {
      double a = (double)i / SEG * PI * 2, cx = spow(cos(a), 0.65), cz = spow(sin(a), 0.8);
      tb[r][i] = add(c, add(mul(F.across, cx * TRD[r][0] * H), mul(F.fwd, cz * TRD[r][1] * H + TRD[r][2] * H)));
    }
  }
  for (int k = 0; k + 1 < 9; k++)
    for (int i = 0; i < SEG; i++) {
      int j = (i + 1) % SEG;
      put(o, tb[k][i], tb[k + 1][j], tb[k][j], TRR[k], FP_TRUNK, false);
      put(o, tb[k][i], tb[k + 1][i], tb[k + 1][j], TRR[k], FP_TRUNK, false);
    }
  Ring neck[3] = { { 0, 0.034, 0.036, 0, FR_SKIN }, { 0.6, 0.031, 0.033, 0.003, FR_SKIN }, { 1.1, 0.03, 0.032, 0.006, FR_SKIN } };
  sides(&K, &W[FB_NECK], neck, 3, FP_LIMB, o);
  head(&K, &W[FB_HEAD], fig, o);
  for (int s = 0; s < 2; s++) {
    Ring up[5] = { { -0.12, 0.024, 0.026, 0, FR_SLEEVE } }, fo[4], pa[3], th[4], sh[4];
    for (int i = 0; i < 4; i++) up[i + 1] = (Ring){ ARM_UPPER[i][0], ARM_UPPER[i][1], ARM_UPPER[i][2], 0, FR_SLEEVE };
    for (int i = 0; i < 4; i++) fo[i] = (Ring){ ARM_FORE[i][0], ARM_FORE[i][1], ARM_FORE[i][2], 0, fig->kit.rolled && i >= 1 ? FR_SKIN : FR_SLEEVE };
    for (int i = 0; i < 3; i++) pa[i] = (Ring){ ARM_PALM[i][0], ARM_PALM[i][1], ARM_PALM[i][2], 0, FR_SKIN };
    for (int i = 0; i < 4; i++) th[i] = (Ring){ LEG_THIGH[i][0], LEG_THIGH[i][1], LEG_THIGH[i][2], LEG_THIGH[i][3], FR_TROUSERS };
    for (int i = 0; i < 4; i++) sh[i] = (Ring){ LEG_SHIN[i][0], LEG_SHIN[i][1], LEG_SHIN[i][2], LEG_SHIN[i][3], FR_TROUSERS };
    sides(&K, &W[FB_UPPER_L + s], up, 5, FP_LIMB, o);
    sides(&K, &W[FB_FORE_L + s], fo, 4, FP_LIMB, o);
    /* the hand: the palm, the fingers curled by the pose, the thumb */
    const Joint *hw = &W[FB_HAND_L + s];
    int part = s ? FP_HAND_R : FP_HAND_L;
    Joint palm = *hw;
    palm.len = K.palm;
    sides(&K, &palm, pa, 3, part, o);
    double curl = fig->pose.curl[s] * D, sg = s ? 1 : -1;
    Frame F = frame(hw);
    V3 knuckle = add(hw->at, mul(F.along, K.palm));
    V3 f1 = norm(add(mul(F.along, cos(curl)), mul(cross(F.across, F.along), sin(curl) * sg)));
    V3 f2 = norm(add(mul(F.along, cos(curl * 2.1)), mul(cross(F.across, F.along), sin(curl * 2.1) * sg)));
    V3 p1 = add(knuckle, mul(f1, (K.hand - K.palm) * 0.55)), p2 = add(p1, mul(f2, (K.hand - K.palm) * 0.45));
    tube(&K, knuckle, p1, F.across, 0.023, 0.0072, 0.021, 0.0066, FR_SKIN, part, o);
    tube(&K, p1, p2, F.across, 0.021, 0.0066, 0.015, 0.005, FR_SKIN, part, o);
    V3 t0 = add(hw->at, add(mul(F.along, K.palm * 0.25), mul(F.fwd, 0.012 * H)));
    V3 t1 = add(t0, add(mul(norm(add(F.along, mul(F.fwd, 0.9))), 0.045 * H), mul(f1, 0.01 * H)));
    tube(&K, t0, t1, F.across, 0.009, 0.008, 0.007, 0.006, FR_SKIN, part, o);
    sides(&K, &W[FB_THIGH_L + s], th, 4, FP_LIMB, o);
    sides(&K, &W[FB_SHIN_L + s], sh, 4, FP_LIMB, o);
    /* the boot: from the heel to the toe */
    const Joint *fw = &W[FB_FOOT_L + s];
    Frame ff = frame(fw);
    V3 downf = mv(fw->R, v3(0, -1, 0));
    V3 heel = add(fw->at, add(mul(ff.along, -0.03 * H), mul(downf, 0.02 * H)));
    V3 toe = add(fw->at, add(mul(ff.along, fw->len), mul(downf, 0.025 * H)));
    tube(&K, heel, toe, mv(fw->R, v3(1, 0, 0)), 0.022, 0.022, 0.02, 0.012, FR_BOOT, FP_NONE, o);
  }
  kit(&K, W, fig, o);
  return O.full ? -1 : O.n;
}

/* ---- the look ---- */
static const uint8_t PAL_COVERALLS[FR_COUNT][3] = { [FR_SHIRT] = { 44, 58, 86 }, [FR_SLEEVE] = { 44, 58, 86 }, [FR_TROUSERS] = { 40, 53, 80 },
  [FR_BELT] = { 36, 46, 70 }, [FR_COLLAR] = { 44, 58, 86 }, [FR_POCKET] = { 38, 50, 76 }, [FR_TAPE] = { 24, 30, 44 }, [FR_TEE] = { 44, 58, 86 } };
static const uint8_t PAL_DUNGAREES[FR_COUNT][3] = { [FR_SHIRT] = { 118, 146, 184 }, [FR_SLEEVE] = { 118, 146, 184 }, [FR_TROUSERS] = { 38, 48, 76 },
  [FR_BELT] = { 30, 28, 26 }, [FR_COLLAR] = { 118, 146, 184 }, [FR_POCKET] = { 108, 136, 174 }, [FR_TAPE] = { 30, 36, 52 }, [FR_TEE] = { 228, 226, 218 } };
static const uint8_t PAL_KHAKI[FR_COUNT][3] = { [FR_SHIRT] = { 176, 150, 104 }, [FR_SLEEVE] = { 176, 150, 104 }, [FR_TROUSERS] = { 166, 140, 96 },
  [FR_BELT] = { 40, 32, 24 }, [FR_COLLAR] = { 176, 150, 104 }, [FR_POCKET] = { 160, 136, 94 }, [FR_TAPE] = { 140, 118, 82 }, [FR_TEE] = { 232, 230, 222 } };
static const uint8_t PAL_SHARED[FR_COUNT][3] = { [FR_BOOT] = { 22, 22, 24 }, [FR_CAP] = { 22, 30, 50 }, [FR_GOLD] = { 214, 172, 72 },
  [FR_KIT] = { 20, 22, 24 }, [FR_EYE_WHITE] = { 214, 208, 200 }, [FR_IRIS] = { 52, 40, 30 }, [FR_MOUTH] = { 70, 30, 30 },
  [FR_PAPER] = { 228, 226, 214 }, [FR_ANCHOR] = { 214, 172, 72 }, [FR_OAK] = { 200, 204, 210 } };

void fig_colour(const FigSpec *f, int role, float c[3]) {
  for (int i = 0; i < 3; i++) {
    double s = f->skin[i], hr = f->hair[i];
    switch (role) {
    case FR_SKIN: c[i] = (float)s; break;
    case FR_SKIN_SHADE: c[i] = (float)(s * 0.72); break;
    case FR_HAIR: c[i] = (float)hr; break;
    case FR_BROW: c[i] = (float)(hr * 0.8); break;
    case FR_LID: c[i] = (float)(s * 0.7); break;
    case FR_LIP: c[i] = (float)(s * (i == 0 ? 0.86 : i == 1 ? 0.68 : 0.66)); break;
    default: {
      const uint8_t *u = (f->kit.uniform == FU_KHAKI ? PAL_KHAKI : f->kit.uniform == FU_DUNGAREES ? PAL_DUNGAREES : PAL_COVERALLS)[role];
      if (role == FR_SHIRT || role == FR_SLEEVE || role == FR_TROUSERS || role == FR_BELT || role == FR_COLLAR || role == FR_POCKET ||
          role == FR_TAPE || role == FR_TEE)
        c[i] = u[i];
      else c[i] = PAL_SHARED[role][i];
    }
    }
  }
}

/* ---- drawing ---- */
typedef struct { float z; int idx, tag; float pts[3][2], col[3]; bool grad; FigGrad g; } Shaded;
static int by_depth(const void *a, const void *b) {
  const Shaded *x = a, *y = b;
  if (x->z != y->z) return x->z < y->z ? 1 : -1; /* far first */
  return x->idx - y->idx;
}

/* the gradient that reproduces three corner colours over a triangle (see figure.js) */
static bool gradient(const float P[3][2], const double cs[3][3], FigGrad *G) {
  double x1 = P[1][0] - P[0][0], y1 = P[1][1] - P[0][1], x2 = P[2][0] - P[0][0], y2 = P[2][1] - P[0][1], det = x1 * y2 - x2 * y1;
  if (fabs(det) < 1e-9) return false;
  double lum[3];
  for (int q = 0; q < 3; q++) lum[q] = 0.3 * cs[q][0] + 0.59 * cs[q][1] + 0.11 * cs[q][2];
  double a = lum[1] - lum[0], b = lum[2] - lum[0], gx = (a * y2 - b * y1) / det, gy = (b * x1 - a * x2) / det, gl = sqrt(gx * gx + gy * gy);
  if (gl < 1e-6) return false;
  double ux = gx / gl, uy = gy / gl, lo = INFINITY, hi = -INFINITY;
  for (int q = 0; q < 3; q++) {
    double pr = (P[q][0] - P[0][0]) * ux + (P[q][1] - P[0][1]) * uy;
    if (pr < lo) lo = pr;
    if (pr > hi) hi = pr;
  }
  for (int i = 0; i < 3; i++) {
    double ai = cs[1][i] - cs[0][i], bi = cs[2][i] - cs[0][i], fx = (ai * y2 - bi * y1) / det, fy = (bi * x1 - ai * x2) / det, d = fx * ux + fy * uy;
    double v0 = cs[0][i] + d * lo, v1 = cs[0][i] + d * hi;
    G->c0[i] = (float)(v0 < 0 ? 0 : v0 > 255 ? 255 : v0);
    G->c1[i] = (float)(v1 < 0 ? 0 : v1 > 255 ? 255 : v1);
  }
  G->x0 = (float)(P[0][0] + ux * lo), G->y0 = (float)(P[0][1] + uy * lo), G->x1 = (float)(P[0][0] + ux * hi), G->y1 = (float)(P[0][1] + uy * hi);
  return true;
}

static void shade(V3 nn, const double base[3], V3 ld, V3 view, const FigLight *light, double out[3]) {
  double kl = dot(nn, ld), vv = dot(nn, view);
  if (kl < 0) kl = 0;
  if (vv < 0) vv = 0;
  double rim = pow(1 - vv, 3);
  for (int q = 0; q < 3; q++) {
    double v = base[q] * (0.24 + 0.85 * kl * light->key[q] / 255) + 255 * rim * 0.2 * light->rim[q] / 255;
    out[q] = v > 255 ? 255 : v;
  }
}

#define FIG_MAX_TRIS 8192
void fig_draw(const FigSpec *figs, int n, const FigCam *cam, const FigLight *light, FigFill fill, void *ud) {
  FigTri *tris = malloc(sizeof *tris * (size_t)FIG_MAX_TRIS * (size_t)(n > 0 ? n : 1));
  FigMesh *m = malloc(sizeof *m * (size_t)(n > 0 ? n : 1));
  if (tris && m) {
    for (int k = 0; k < n; k++) {
      int nt = fig_build(&figs[k], tris + (size_t)k * FIG_MAX_TRIS, FIG_MAX_TRIS);
      m[k] = (FigMesh){ tris + (size_t)k * FIG_MAX_TRIS, nt > 0 ? nt : 0, &figs[k] };
    }
    fig_draw_meshes(m, n, cam, light, fill, ud);
  }
  free(tris);
  free(m);
}

void fig_draw_meshes(const FigMesh *m, int n, const FigCam *cam, const FigLight *light, FigFill fill, void *ud) {
  int total = 0;
  for (int k = 0; k < n; k++) total += m[k].n;
  Shaded *all = malloc(sizeof *all * (size_t)(total > 0 ? total : 1));
  if (!all) return;
  int na = 0;
  V3 cp = v3(cam->pos[0], cam->pos[1], cam->pos[2]), cf = v3(cam->f[0], cam->f[1], cam->f[2]);
  V3 cu = v3(cam->u[0], cam->u[1], cam->u[2]), cr = v3(cam->r[0], cam->r[1], cam->r[2]), ld = v3(light->dir[0], light->dir[1], light->dir[2]);
  for (int k = 0; k < n; k++) {
    for (int i = 0; i < m[k].n; i++) {
      const FigTri *t = &m[k].tris[i];
      V3 a = v3(t->v[0][0], t->v[0][1], t->v[0][2]), b = v3(t->v[1][0], t->v[1][1], t->v[1][2]), c = v3(t->v[2][0], t->v[2][1], t->v[2][2]);
      V3 nn = norm(cross(sub(b, a), sub(c, a)));
      V3 ce = mul(add(add(a, b), c), 1.0 / 3), d = sub(ce, cp);
      double z = dot(d, cf);
      if (z < 0.05) continue;
      if (dot(nn, d) > 0) {
        if (!t->two) continue;
        nn = mul(nn, -1);
      }
      Shaded *s = &all[na];
      s->z = (float)z;
      s->idx = na;
      s->tag = t->tag;
      s->grad = false;
      for (int q = 0; q < 3; q++) {
        V3 p = q == 0 ? a : q == 1 ? b : c, qd = sub(p, cp);
        double zz = dot(qd, cf);
        if (zz < 0.05) zz = 0.05;
        s->pts[q][0] = (float)(cam->cx + dot(qd, cr) / zz * cam->F);
        s->pts[q][1] = (float)(cam->cy - dot(qd, cu) / zz * cam->F);
      }
      V3 view = norm(mul(d, -1));
      if (t->shade == FS_LIT) {
        for (int q = 0; q < 3; q++) s->col[q] = t->col[q];
      } else if (t->shade == FS_SMOOTH) {
        double cs[3][3];
        for (int q = 0; q < 3; q++) {
          V3 vn = v3(t->ns[q][0], t->ns[q][1], t->ns[q][2]);
          if (dot(vn, view) < 0) vn = mul(vn, -1);
          double base[3] = { t->cs[q][0], t->cs[q][1], t->cs[q][2] };
          shade(vn, base, ld, view, light, cs[q]);
        }
        for (int q = 0; q < 3; q++) s->col[q] = (float)((cs[0][q] + cs[1][q] + cs[2][q]) / 3);
        s->grad = gradient((const float(*)[2])s->pts, (const double(*)[3])cs, &s->g);
      } else {
        float bf[3] = { 255, 0, 255 };
        if (t->shade == FS_OWN) bf[0] = t->col[0], bf[1] = t->col[1], bf[2] = t->col[2];
        else if (m[k].spec) fig_colour(m[k].spec, t->role, bf);
        double base[3] = { bf[0], bf[1], bf[2] }, out[3];
        shade(nn, base, ld, view, light, out);
        for (int q = 0; q < 3; q++) s->col[q] = (float)out[q];
      }
      na++;
    }
  }
  qsort(all, (size_t)na, sizeof *all, by_depth);
  for (int i = 0; i < na; i++) fill((const float(*)[2])all[i].pts, all[i].col, all[i].grad ? &all[i].g : NULL, all[i].tag, ud);
  free(all);
}

void fig_joint(const FigSpec *f, int bone, float at[3]) {
  Skel K = skeleton(f->height > 0 ? f->height : 1.75);
  Joint W[FB_COUNT];
  solve(&K, &f->pose, W);
  for (int k = 0; k < 3; k++) at[k] = (float)W[bone].at.v[k];
}

/* ---- poses ---- */
static void pose_clear(FigPose *p) { memset(p, 0, sizeof *p); }

void fig_pose_vitruvian(float Hf, FigPose *p) {
  double H = Hf;
  Skel K = skeleton(H);
  double drop = H / 14, leg = K.hip_y, spread = acos(1 - drop / leg) / D, arm = K.upper + K.fore + K.hand;
  double s = (H - (K.sh_y - drop)) / arm, raise = asin(s < 1 ? s : 1) / D;
  pose_clear(p);
  p->root[1] = (float)-drop;
  p->ang[FB_UPPER_L][2] = (float)raise, p->ang[FB_UPPER_R][2] = (float)-raise;
  p->ang[FB_THIGH_L][2] = (float)spread, p->ang[FB_THIGH_R][2] = (float)-spread;
}

void fig_pose_stand(FigPose *p) {
  pose_clear(p);
  p->ang[FB_UPPER_L][2] = -84, p->ang[FB_UPPER_R][2] = 84;
  p->ang[FB_FORE_L][0] = -12, p->ang[FB_FORE_R][0] = -12;
}

/* sitting on a chair seat 0.46 m high: the thighs forward, the shins down */
static void seat(float H, FigPose *p) {
  pose_clear(p);
  p->root[1] = (float)(0.46 + 0.045 * H / 1.75 - skeleton(H).hip_y);
  p->ang[FB_THIGH_L][0] = p->ang[FB_THIGH_R][0] = -90;
  p->ang[FB_SHIN_L][0] = p->ang[FB_SHIN_R][0] = 88;
}
#define SET(b, x, y, z) (p->ang[b][0] = (float)(x), p->ang[b][1] = (float)(y), p->ang[b][2] = (float)(z))

static const FigFace FACE_DEFAULT = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.3f, 0.2f };
/* a figure with the default looks: what figure.js gives when nothing is set */
static void spec_default(FigSpec *f, float H) {
  memset(f, 0, sizeof *f);
  f->height = H;
  f->shape = FACE_DEFAULT;
  memcpy(f->skin, (uint8_t[]){ 196, 152, 122 }, 3);
  memcpy(f->hair, (uint8_t[]){ 52, 40, 32 }, 3);
  memcpy(f->iris, (uint8_t[]){ 70, 58, 44 }, 3);
}

void fig_crew(int who, float H, float raise, FigSpec *f) {
  spec_default(f, H);
  FigPose *p = &f->pose;
  if (who == FC_OPERATOR) { /* chambray and dungarees and a sound-powered phone headset, seated, leaning in to his scope */
    f->kit = (FigKit){ FU_DUNGAREES, FD_NONE, false, false, true, true, false, false };
    memcpy(f->skin, (uint8_t[]){ 205, 160, 128 }, 3);
    memcpy(f->hair, (uint8_t[]){ 34, 26, 20 }, 3);
    memcpy(f->iris, (uint8_t[]){ 86, 62, 40 }, 3);
    f->shape = (FigFace){ 0.3f, 0.45f, 0.45f, 0.45f, 0.35f, 0.55f, 0.65f, 0.3f, 0.05f, 0.08f }; /* twenty-four, clean-shaven */
    seat(H, p);
    SET(FB_HIPS, 14, 0, 0), SET(FB_CHEST, 8, 0, 0), SET(FB_NECK, -12, 0, 0), SET(FB_HEAD, -8, 0, 0);
    SET(FB_UPPER_L, 0, -62, -58), SET(FB_UPPER_R, 0, 62, 58), SET(FB_FORE_L, 0, -48, 6), SET(FB_FORE_R, 0, 48, -6);
    SET(FB_HAND_L, 0, -10, -8), SET(FB_HAND_R, 0, 10, 8);
    p->curl[0] = 30, p->curl[1] = 34;
  } else if (who == FC_SUPERVISOR) { /* the chief in service khakis, standing, the clipboard held at his waist */
    f->kit = (FigKit){ FU_KHAKI, FD_ANCHOR, true, false, false, false, true, false };
    memcpy(f->skin, (uint8_t[]){ 190, 140, 110 }, 3);
    memcpy(f->hair, (uint8_t[]){ 46, 40, 36 }, 3);
    memcpy(f->iris, (uint8_t[]){ 60, 70, 80 }, 3);
    f->shape = (FigFace){ 0.75f, 0.55f, 0.8f, 0.6f, 0.7f, 0.65f, 0.4f, 0.6f, 0.5f, 0.55f }; /* forty-five, a day's stubble */
    pose_clear(p);
    SET(FB_UPPER_L, 0, -8, -82), SET(FB_FORE_L, 0, -86, 0), SET(FB_HAND_L, 0, 0, 16);
    SET(FB_UPPER_R, 0, 8, 82), SET(FB_FORE_R, 0, 12, 0), SET(FB_HEAD, 0, -6, 0);
    p->curl[0] = 60, p->curl[1] = 24;
  } else { /* the commander in khakis with his oak leaves and the gold on his cap's bill, in his chair */
    f->kit = (FigKit){ FU_KHAKI, FD_OAK, true, true, false, false, false, false };
    memcpy(f->skin, (uint8_t[]){ 196, 150, 120 }, 3);
    memcpy(f->hair, (uint8_t[]){ 140, 138, 134 }, 3);
    memcpy(f->iris, (uint8_t[]){ 76, 92, 104 }, 3);
    f->shape = (FigFace){ 0.6f, 0.75f, 0.55f, 0.65f, 0.8f, 0.45f, 0.3f, 0.8f, 0.85f, 0.25f }; /* fifty-five, deep-set eyes */
    seat(H, p);
    SET(FB_HIPS, -6, 0, 0), SET(FB_CHEST, -2, 0, 0), SET(FB_NECK, 4, 0, 0);
    SET(FB_UPPER_L, 0, 12, -84), SET(FB_FORE_L, 0, -88, 0), SET(FB_HAND_L, 0, -6, -10);
    SET(FB_UPPER_R, 0, -12 + 62 * raise, 84 - 40 * raise), SET(FB_FORE_R, 0, 88 - 20 * raise, 55 * raise), SET(FB_HAND_R, 0, 0, -15 * raise);
    p->curl[0] = 45, p->curl[1] = 45 - 38 * raise;
  }
}
#undef SET

/* ---- the canon and the measured mesh ---- */
void fig_canon(FigRatios *r) {
  memset(r, 0, sizeof *r);
  r->span = V_SPAN, r->head = V_HEAD, r->face = V_FACE;
  r->thirds[0] = r->thirds[1] = r->thirds[2] = V_FACE / 3;
  r->chest_top = V_CHEST_TOP, r->shoulders = V_SHOULDERS, r->elbow_tip = V_ELBOW_TIP, r->elbow_pit = V_ELBOW_PIT;
  r->hand = V_HAND, r->foot = V_FOOT, r->knee = V_KNEE, r->knee_crotch = V_KNEE, r->circle = 1;
}

typedef struct { double lo, hi; } Ext;
static Ext ext(const FigTri *t, int n, int axis, bool (*keep)(const FigTri *, const void *), const void *ctx) {
  Ext e = { INFINITY, -INFINITY };
  for (int i = 0; i < n; i++) {
    if (keep && !keep(&t[i], ctx)) continue;
    for (int k = 0; k < 3; k++) {
      double x = t[i].v[k][axis];
      if (x < e.lo) e.lo = x;
      if (x > e.hi) e.hi = x;
    }
  }
  return e;
}
static bool is_part(const FigTri *t, const void *c) { return t->part == *(const int *)c; }
static bool is_left_boot(const FigTri *t, const void *c) {
  (void)c;
  return t->role == FR_BOOT && t->v[0][0] > 0 && t->v[1][0] > 0 && t->v[2][0] > 0;
}
typedef struct { double y, h; } Band;
static bool in_band(const FigTri *t, const void *c) {
  const Band *b = c;
  if (t->part == FP_HEAD) return false;
  for (int k = 0; k < 3; k++)
    if (fabs(t->v[k][1] - b->y) >= b->h) return false;
  return true;
}
static double far_from(const FigTri *t, int n, V3 c, bool (*keep)(const FigTri *, const void *), const void *ctx) {
  double m = 0;
  for (int i = 0; i < n; i++) {
    if (!keep(&t[i], ctx)) continue;
    for (int k = 0; k < 3; k++) {
      double d = len3(sub(v3(t[i].v[k][0], t[i].v[k][1], t[i].v[k][2]), c));
      if (d > m) m = d;
    }
  }
  return m;
}
static bool is_boot(const FigTri *t, const void *c) {
  (void)c;
  return t->role == FR_BOOT;
}

bool fig_measure(double H, FigRatios *r) {
  static FigTri T[FIG_MAX_TRIS], S[FIG_MAX_TRIS], Vt[FIG_MAX_TRIS];
  FigSpec f;
  spec_default(&f, (float)H);
  int nt = fig_build(&f, T, FIG_MAX_TRIS);
  Skel K = skeleton(H);
  Joint W[FB_COUNT];
  solve(&K, &f.pose, W);
  FigSpec fs = f;
  fig_pose_stand(&fs.pose);
  int ns = fig_build(&fs, S, FIG_MAX_TRIS);
  FigSpec fv = f;
  fig_pose_vitruvian((float)H, &fv.pose);
  int nv = fig_build(&fv, Vt, FIG_MAX_TRIS);
  if (nt < 0 || ns < 0 || nv < 0) return false;
  Joint WV[FB_COUNT];
  solve(&K, &fv.pose, WV);
  Ext x = ext(T, nt, 0, NULL, NULL), y = ext(T, nt, 1, NULL, NULL);
  int ph = FP_HEAD, pl = FP_HAND_L;
  Ext hy = ext(T, nt, 1, is_part, &ph), hx = ext(T, nt, 0, is_part, &pl);
  Band band = { K.sh_y, 0.03 * H };
  Ext sx = ext(S, ns, 0, in_band, &band);
  Ext bz = ext(T, nt, 2, is_left_boot, NULL);
  V3 navel = v3(0, WV[FB_HIPS].at.v[1] + (K.navel - K.hip_y), 0);
  double r_tip = far_from(Vt, nv, navel, is_part, &pl), r_foot = far_from(Vt, nv, navel, is_boot, NULL);
  double h = y.hi - y.lo;
  memset(r, 0, sizeof *r);
  r->height = h;
  r->span = (x.hi - x.lo) / h;
  r->head = (y.hi - hy.lo) / h;
  r->hand = (hx.hi - W[FB_HAND_L].at.v[0]) / h;
  r->elbow_tip = (hx.hi - W[FB_FORE_L].at.v[0]) / h;
  /* Leonardo's armpit lies on the shoulder's outer line (see figure.js) */
  r->elbow_pit = (W[FB_FORE_L].at.v[0] - sx.hi) / h;
  r->shoulders = (sx.hi - sx.lo) / h;
  r->foot = (bz.hi - bz.lo) / h;
  r->knee = (W[FB_SHIN_L].at.v[1] - y.lo) / h;
  r->knee_crotch = (K.crotch - W[FB_SHIN_L].at.v[1]) / h;
  r->chest_top = (y.hi - K.chest_top) / h;
  r->face = (K.hairline - K.chin) / h;
  r->thirds[0] = (K.nose - K.chin) / h, r->thirds[1] = (K.brow - K.nose) / h, r->thirds[2] = (K.hairline - K.brow) / h;
  r->circle = r_foot / r_tip;
  return true;
}

/* ---- the face's rules, measured on the mesh: each 1 when the rule holds ---- */
bool fig_face_rules(int who, FigFaceRules *r) {
  static FigTri t[FIG_MAX_TRIS];
  FigSpec f;
  fig_crew(who, 1.75f, 0, &f);
  memset(&f.pose, 0, sizeof f.pose); /* standing straight: the head's axes are the world's */
  int n = fig_build(&f, t, FIG_MAX_TRIS);
  if (n < 0) return false;
  Skel K = skeleton(1.75);
  Land L = landmarks();
  double hH = K.H / 8, bottom = K.chin + 0.01 * K.H - 0.01 * K.H;
  /* the eyeballs (the white spheres: one eye wide each), the iris centres (the pupils), the nose's widest point, the
   * mouth's inside (its corners), the ears' extents; in head units */
  double ex0 = 1e9, ex1 = -1e9, ey = 0, ny = 0, nx = 0, mx = 0, px = 0, et = -1e9, eb = 1e9;
  int ne = 0, np = 0;
  for (int i = 0; i < n; i++) {
    const FigTri *q = &t[i];
    for (int k = 0; k < 3; k++) {
      double x = q->v[k][0] / hH, u = (q->v[k][1] - bottom) / hH;
      if (q->role == FR_EYE_WHITE && x > 0) ex0 = mind(ex0, x), ex1 = maxd(ex1, x), ey += u, ne++;
      if (q->role == FR_IRIS && x > 0 && k == 0) px += x, np++;
      if (q->part == FP_FACE && q->role == FR_SKIN && x > 0 && u < 0.29) nx = maxd(nx, x);
      if (q->role == FR_MOUTH && q->part == FP_FACE && q->shade == FS_OWN && q->col[0] < 60) mx = maxd(mx, fabs(x));
      if (q->role == FR_SKIN && q->part == FP_NONE && x > 0.3) et = maxd(et, u), eb = mind(eb, u);
    }
  }
  (void)ny;
  if (!ne || !np) return false;
  double eye_w = ex1 - ex0, gap = 2 * ex0, pupil = px / np;
  r->eye_height = ey / ne / EYE_U;
  r->eye_gap = gap / eye_w;
  r->nose_width = 2 * nx / gap;
  r->mouth_corners = mx / 0.95 / pupil;
  r->ear_top = et / L.earTop;
  r->ear_bottom = eb / L.earBottom;
  return true;
}

/* ---- speech (see figure.js: the same phonemes, shapes, pacing and blending) ---- */
enum { V_REST, V_MBP, V_FV, V_TH, V_AA, V_EE, V_IH, V_OO, V_OH, V_W, V_SH, V_TT, V_KK, V_SS, V_COUNT };
static const double VIS[V_COUNT][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0.08, 0.2, 0 }, { 0.15, 0.25, 0 }, { 0.75, 0.35, 0 }, { 0.3, 0.9, 0 },
                                        { 0.4, 0.55, 0 }, { 0.35, 0, 0.9 }, { 0.6, 0.1, 0.6 }, { 0.15, 0, 0.8 }, { 0.25, 0.1, 0.5 }, { 0.25, 0.4, 0 },
                                        { 0.35, 0.3, 0 }, { 0.12, 0.6, 0 } };
static const struct { const char *ph; int vis; bool vowel; } PHON[] = {
  { "AA", V_AA, 1 }, { "AE", V_AA, 1 }, { "AH", V_AA, 1 }, { "AO", V_OH, 1 }, { "AW", V_AA, 1 }, { "AY", V_AA, 1 }, { "EH", V_EE, 1 },
  { "ER", V_IH, 1 }, { "EY", V_EE, 1 }, { "IH", V_IH, 1 }, { "IY", V_EE, 1 }, { "OW", V_OH, 1 }, { "OY", V_OH, 1 }, { "UH", V_OO, 1 },
  { "UW", V_OO, 1 }, { "B", V_MBP, 0 }, { "P", V_MBP, 0 }, { "M", V_MBP, 0 }, { "F", V_FV, 0 }, { "V", V_FV, 0 }, { "TH", V_TH, 0 },
  { "DH", V_TH, 0 }, { "T", V_TT, 0 }, { "D", V_TT, 0 }, { "N", V_TT, 0 }, { "L", V_TT, 0 }, { "S", V_SS, 0 }, { "Z", V_SS, 0 },
  { "SH", V_SH, 0 }, { "TSH", V_SH, 0 }, { "JH", V_SH, 0 }, { "K", V_KK, 0 }, { "G", V_KK, 0 }, { "NG", V_KK, 0 }, { "HH", V_KK, 0 },
  { "R", V_W, 0 }, { "W", V_W, 0 }, { "Y", V_EE, 0 } };
static const struct { const char *w, *ph; } WORDLIST[] = {
  { "BOGEYS", "B OW G IY Z" }, { "TWO", "T UW" }, { "CONTACTS", "K AA N T AE K T S" }, { "BEARING", "B EH R IH NG" }, { "THREE", "TH R IY" },
  { "FIVE", "F AY V" }, { "ZERO", "Z IY R OW" }, { "CLOSING", "K L OW Z IH NG" }, { "FAST", "F AE S T" }, { "ALERT", "AH L ER T" },
  { "WE", "W IY" }, { "HAVE", "HH AE V" }, { "INBOUND", "IH N B AW N D" }, { "SEND", "S EH N D" }, { "THE", "DH AH" }, { "BIRDS", "B ER D Z" } };
static const char *const DIGIT[10] = { "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE" };
static const struct { const char *g, *ph; } RULES[] = {
  { "TION", "SH AH N" }, { "SH", "SH" }, { "TH", "TH" }, { "PH", "F" }, { "NG", "NG" }, { "CK", "K" }, { "EE", "IY" }, { "EA", "IY" },
  { "OO", "UW" }, { "OU", "AW" }, { "OW", "OW" }, { "AI", "EY" }, { "AY", "EY" }, { "OI", "OY" }, { "A", "AE" }, { "E", "EH" }, { "I", "IH" },
  { "O", "AA" }, { "U", "AH" }, { "Y", "IY" }, { "B", "B" }, { "C", "K" }, { "D", "D" }, { "F", "F" }, { "G", "G" }, { "H", "HH" }, { "J", "JH" },
  { "K", "K" }, { "L", "L" }, { "M", "M" }, { "N", "N" }, { "P", "P" }, { "Q", "K" }, { "R", "R" }, { "S", "S" }, { "T", "T" }, { "V", "V" },
  { "W", "W" }, { "X", "K S" }, { "Z", "Z" } };

/* a word's phonemes, as indexes into PHON; returns how many */
static int phonemes(const char *word, int *out, int max) {
  char buf[96] = "";
  bool found = false;
  for (size_t i = 0; i < sizeof WORDLIST / sizeof WORDLIST[0]; i++)
    if (!strcmp(WORDLIST[i].w, word)) {
      snprintf(buf, sizeof buf, "%s", WORDLIST[i].ph);
      found = true;
    }
  if (!found) {
    size_t len = strlen(word);
    for (size_t i = 0; i < len;) {
      size_t r = 0, best = sizeof RULES / sizeof RULES[0];
      for (; r < sizeof RULES / sizeof RULES[0]; r++)
        if (!strncmp(word + i, RULES[r].g, strlen(RULES[r].g))) {
          best = r;
          break;
        }
      if (best == sizeof RULES / sizeof RULES[0]) {
        i++;
        continue;
      }
      size_t at = strlen(buf);
      snprintf(buf + at, sizeof buf - at, "%s%s", at ? " " : "", RULES[best].ph);
      i += strlen(RULES[best].g);
    }
  }
  int n = 0;
  for (char *tok = buf; *tok && n < max;) {
    char ph[4] = "";
    int k = 0;
    while (*tok && *tok != ' ' && k < 3) ph[k++] = *tok++;
    ph[k] = 0;
    while (*tok == ' ') tok++;
    for (size_t p = 0; p < sizeof PHON / sizeof PHON[0]; p++)
      if (!strcmp(PHON[p].ph, ph)) {
        out[n++] = (int)p;
        break;
      }
  }
  return n;
}

static bool is_letter(char c) { return (c >= 'A' && c <= 'Z') || c == '\''; }
void fig_speech(const char *text, float t0f, float t1f, FigSpeech *S) {
  double t0 = t0f, t1 = t1f;
  memset(S, 0, sizeof *S);
  int ph[FIG_WORDS][24], nph[FIG_WORDS];
  double pause[FIG_WORDS];
  char up[256];
  snprintf(up, sizeof up, "%s", text);
  for (char *c = up; *c; c++)
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
  int nw = 0;
  for (const char *c = up; *c && nw < FIG_WORDS;) {
    while (*c && !is_letter(*c) && !(*c >= '0' && *c <= '9')) c++;
    if (!*c) break;
    char w[32] = "", sp[32] = "";
    int k = 0;
    if (*c >= '0' && *c <= '9') w[k++] = *c++;
    else
      while (is_letter(*c) && k < 31) w[k++] = *c++;
    w[k] = 0;
    snprintf(S->word[nw], sizeof S->word[nw], "%.15s", w);
    const char *say = w;
    if (w[0] >= '0' && w[0] <= '9') say = DIGIT[w[0] - '0'];
    else { /* drop apostrophes */
      int j = 0;
      for (int i = 0; w[i]; i++)
        if (w[i] != '\'') sp[j++] = w[i];
      sp[j] = 0;
      say = sp;
    }
    nph[nw] = phonemes(say, ph[nw], 24);
    const char *p = c;
    bool stop = false, comma = false;
    while (*p && !is_letter(*p) && !(*p >= '0' && *p <= '9')) {
      if (*p == '.' || *p == '!' || *p == '?') stop = true;
      if (*p == ',') comma = true;
      p++;
    }
    pause[nw] = stop ? 0.18 : comma ? 0.1 : 0;
    nw++;
  }
  S->nwords = nw;
  double lead = 0.06, weight = 0, pauses = 0;
  for (int i = 0; i < nw; i++) {
    for (int k = 0; k < nph[i]; k++) weight += PHON[ph[i][k]].vowel ? 1 : 0.55;
    pauses += pause[i];
  }
  double kk = (t1 - t0 - lead - pauses) / (weight != 0 ? weight : 1);
  if (kk < 0.01) kk = 0.01;
  int ns = 0;
  S->t0[ns] = (float)t0, S->t1[ns] = (float)(t0 + lead), S->vis[ns++] = V_REST;
  double t = t0 + lead;
  for (int i = 0; i < nw; i++) {
    S->start[i] = (float)t;
    for (int k = 0; k < nph[i] && ns < FIG_SEGS; k++) {
      double d = (PHON[ph[i][k]].vowel ? 1 : 0.55) * kk;
      S->t0[ns] = (float)t, S->t1[ns] = (float)(t + d), S->vis[ns++] = (uint8_t)PHON[ph[i][k]].vis;
      t += d;
    }
    if (pause[i] > 0 && ns < FIG_SEGS) {
      S->t0[ns] = (float)t, S->t1[ns] = (float)(t + pause[i]), S->vis[ns++] = V_REST;
      t += pause[i];
    }
  }
  S->t1[ns - 1] = (float)t1; /* the last stretch ends with the card */
  S->nseg = ns;
}

void fig_mouth(const FigSpeech *S, float tf, float *open, float *wide, float *round) {
  double t = tf, o = 0, w = 0, r = 0, ws = 0;
  for (int i = 0; i < S->nseg; i++) {
    double mid = ((double)S->t0[i] + S->t1[i]) / 2, half = maxd(0.05, ((double)S->t1[i] - S->t0[i]) * 0.9), d = fabs(t - mid);
    if (d >= half) continue;
    double k = 0.5 + 0.5 * cos(PI * d / half);
    o += VIS[S->vis[i]][0] * k, w += VIS[S->vis[i]][1] * k, r += VIS[S->vis[i]][2] * k, ws += k;
  }
  if (ws == 0) ws = 1, o = w = r = 0;
  *open = (float)(o / ws), *wide = (float)(w / ws), *round = (float)(r / ws);
}

int fig_words_shown(const FigSpeech *S, float t) {
  int n = 0;
  for (int i = 0; i < S->nwords; i++) n += S->start[i] <= t;
  return n;
}

/* the caption's text up to its nth spoken word, with the punctuation that follows that word */
void fig_spoken_upto(const char *text, int n, char *out, size_t size) {
  size_t i = 0, len = strlen(text), end = 0;
  int k = 0;
  if (n > 0) {
    end = len;
    while (i < len) {
      char c = text[i];
      bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'';
      if (!letter && !(c >= '0' && c <= '9')) {
        i++;
        continue;
      }
      if (c >= '0' && c <= '9') i++;
      else
        while (i < len && ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z') || text[i] == '\'')) i++;
      while (i < len && text[i] != ' ' && !((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z') || text[i] == '\'' || (text[i] >= '0' && text[i] <= '9'))) i++;
      if (++k == n) {
        end = i;
        break;
      }
    }
  }
  snprintf(out, size, "%.*s", (int)end, text);
}
