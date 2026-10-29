/* figure.c - the figure engine (see figure.h), the same as the browser game's assets/figure/figure.js: a skeleton
 * sized by Leonardo's canon, forward kinematics, lofted rings along the bones, a head built on the face's thirds, the
 * Navy kit, flat-shaded drawing and the canon measured on the mesh. The arithmetic is in double, as JavaScript's is,
 * so the two make the same triangles (tests/test_figure.c compares them). Original code for this project. */
#include "figure.h"

#include <math.h>
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
static const double HEAD[18][4] = {
  { 0.0, 0.14, 0.30, -0.05 }, { 0.04, 0.2, 0.40, -0.1 }, { 0.1, 0.28, 0.41, -0.2 }, { 0.16, 0.33, 0.43, -0.3 },
  { 0.2, 0.35, 0.44, -0.34 }, { 0.24, 0.37, 0.45, -0.38 }, { 0.267, 0.39, 0.44, -0.4 }, { 0.33, 0.42, 0.43, -0.43 },
  { 0.4, 0.44, 0.44, -0.46 }, { 0.47, 0.46, 0.43, -0.48 }, { 0.533, 0.46, 0.465, -0.49 }, { 0.6, 0.46, 0.46, -0.5 },
  { 0.7, 0.46, 0.445, -0.5 }, { 0.8, 0.45, 0.41, -0.49 }, { 0.88, 0.41, 0.34, -0.44 }, { 0.94, 0.32, 0.24, -0.34 },
  { 0.98, 0.18, 0.12, -0.18 }, { 1.0, 0.04, 0.02, -0.03 } };
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

typedef struct { const Skel *K; Frame F; V3 bottom; double hH, open; } HeadCtx;
static double jaw(const HeadCtx *h, double u) {
  if (u >= 0.205) return 0;
  double k = (0.205 - u) / 0.06;
  return h->open * 0.07 * (k < 1 ? k : 1);
}
static V3 HP(const HeadCtx *h, double u, double x, double z) {
  return add(h->bottom, add(mul(h->F.along, (u - jaw(h, u)) * h->hH), add(mul(h->F.across, x * h->hH), mul(h->F.fwd, z * h->hH))));
}
static V3 HS(const HeadCtx *h, double u, double a, double lift) {
  double x, z;
  surf(u, a, &x, &z);
  double n = sqrt(x * x + z * z);
  if (n == 0) n = 1;
  return HP(h, u, x + x / n * lift, z + z / n * lift);
}
static void quad(Out *o, V3 a, V3 b, V3 c, V3 d, int role, int part) {
  put(o, a, b, c, role, part, true);
  put(o, a, c, d, role, part, true);
}
static bool hair_at(double u, double x, double z) { return u >= 0.8 || (z < -0.05 && u > 0.4) || (u > 0.6 && fabs(x) > 0.3 && z < 0.25); }

static void head(const Skel *K, const Joint *w, const FigSpec *fig, Out *o) {
  HeadCtx h;
  h.K = K;
  h.F = frame(w);
  h.hH = K->H / 8;
  h.bottom = add(w->at, mul(h.F.along, -0.01 * K->H));
  h.open = fig->open;
  double blink = fig->blink;
  static V3 rp[NHEAD][HN];
  static double rx_[NHEAD][HN], rz_[NHEAD][HN];
  for (int k = 0; k < NHEAD; k++)
    for (int i = 0; i < HN; i++) {
      double a = (double)i / HN * PI * 2, x, z;
      surf(HEAD[k][0], a, &x, &z);
      rp[k][i] = HP(&h, HEAD[k][0], x, z);
      rx_[k][i] = x, rz_[k][i] = z;
    }
  for (int k = 0; k + 1 < NHEAD; k++)
    for (int i = 0; i < HN; i++) {
      int j = (i + 1) % HN;
      bool hr = hair_at(HEAD[k][0], rx_[k][i], rz_[k][i]) && hair_at(HEAD[k + 1][0], rx_[k + 1][j], rz_[k + 1][j]) &&
                hair_at(HEAD[k][0], rx_[k][j], rz_[k][j]);
      int role = hr ? FR_HAIR : FR_SKIN;
      put(o, rp[k][i], rp[k + 1][j], rp[k][j], role, FP_HEAD, false);
      put(o, rp[k][i], rp[k + 1][i], rp[k + 1][j], role, FP_HEAD, false);
    }
  V3 c0 = HP(&h, 0, 0, 0.12);
  for (int i = 0; i < HN; i++) put(o, rp[0][i], rp[0][(i + 1) % HN], c0, FR_SKIN, FP_HEAD, false);
  /* the nose: the bridge between the eyes, the tip forward of the face, the wings and the base */
  V3 bridge = HP(&h, 0.52, 0, 0.47), bl = HP(&h, 0.43, 0.05, 0.452), br = HP(&h, 0.43, -0.05, 0.452), tip = HP(&h, 0.31, 0, 0.555);
  V3 wl = HP(&h, 0.28, 0.075, 0.445), wr = HP(&h, 0.28, -0.075, 0.445), base = HP(&h, 0.265, 0, 0.47);
  put(o, bridge, bl, tip, FR_SKIN, FP_FACE, true);
  put(o, bridge, tip, br, FR_SKIN, FP_FACE, true);
  put(o, bl, wl, tip, FR_SKIN, FP_FACE, true);
  put(o, br, tip, wr, FR_SKIN, FP_FACE, true);
  put(o, wl, base, tip, FR_SKIN_SHADE, FP_FACE, true);
  put(o, tip, base, wr, FR_SKIN_SHADE, FP_FACE, true);
  for (int e = 0; e < 2; e++) {
    double sx = e ? -1 : 1, ac = angle_at(0.5, 0.2 * sx), eu0 = 0.475, eu1 = 0.525, top = eu1 - (eu1 - eu0) * blink;
#define E(u, da, lift) HS(&h, (u), ac + (da) * sx, (lift))
    /* the eye: the white, the iris; the upper lid comes down to blink */
    put(o, E(0.5, -0.13, 0.008), E(eu0, 0, 0.008), E(0.5, 0.13, 0.008), FR_EYE_WHITE, FP_NONE, true);
    if (blink < 0.6) put(o, E(0.5, -0.13, 0.008), E(0.5, 0.13, 0.008), E(top, 0, 0.008), FR_EYE_WHITE, FP_NONE, true);
    double ti = top < 0.518 ? top : 0.518;
    if (blink < 0.6) quad(o, E(0.482, -0.04, 0.011), E(0.482, 0.04, 0.011), E(ti, 0.04, 0.011), E(ti, -0.04, 0.011), FR_IRIS, FP_FACE);
    quad(o, E(top - 0.004, -0.14, 0.012), E(top - 0.004, 0.14, 0.012), E(top + 0.01, 0.12, 0.012), E(top + 0.01, -0.12, 0.012), FR_LID, FP_FACE);
    /* the brow along the brow ridge */
    quad(o, E(0.56, -0.2, 0.012), E(0.565, 0.18, 0.012), E(0.595, 0.16, 0.012), E(0.59, -0.2, 0.012), FR_BROW, FP_FACE);
#undef E
    /* the ear, as long as one third of the face, between the brows and the nose's base, flat against the head */
    V3 ec = HP(&h, 0.4, 0.455 * sx, -0.06);
    V3 e0 = add(ec, mul(h.F.along, -h.hH * 0.133)), e1 = add(ec, mul(h.F.along, h.hH * 0.02)), e2 = add(ec, mul(h.F.along, h.hH * 0.133));
    tube(K, e0, e1, h.F.fwd, 0.006, 0.003, 0.012, 0.0035, FR_SKIN, FP_NONE, o);
    tube(K, e1, e2, h.F.fwd, 0.012, 0.0035, 0.007, 0.003, FR_SKIN, FP_NONE, o);
  }
  /* the lips round the front of the face, the mouth's line between them */
  static const double LIPS[3][3] = { { 0.212, 0.238, 0.006 }, { 0.17, 0.196, 0.008 }, { 0, 0.212, 0.009 } };
  for (int l = 0; l < 3; l++) {
    double u0 = l == 2 ? 0.198 - h.open * 0.05 : LIPS[l][0], u1 = LIPS[l][1], lift = LIPS[l][2];
    int role = l == 2 ? FR_MOUTH : FR_LIP;
    for (int k = -3; k < 3; k++) {
      double a0 = PI / 2 + k * 0.09, a1 = a0 + 0.09;
      quad(o, HS(&h, u0, a0, lift), HS(&h, u0, a1, lift), HS(&h, u1, a1, lift), HS(&h, u1, a0, lift), role, FP_FACE);
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
    quad(o, HP(&h, 0.725, -0.42, 0.44), HP(&h, 0.725, 0.42, 0.44), HP(&h, 0.69, 0.34, 0.86), HP(&h, 0.69, -0.34, 0.86), FR_CAP, FP_NONE);
    if (C->gold) /* the gold oak leaves along the bill's front edge */
      quad(o, HP(&h, 0.712, -0.38, 0.6), HP(&h, 0.712, 0.38, 0.6), HP(&h, 0.692, 0.33, 0.84), HP(&h, 0.692, -0.33, 0.84), FR_GOLD, FP_NONE);
    quad(o, HP(&h, 0.79, -0.22, 0.46), HP(&h, 0.79, 0.22, 0.46), HP(&h, 0.88, 0.19, 0.4), HP(&h, 0.88, -0.19, 0.4), FR_GOLD, FP_NONE);
  }
  if (C->headset) { /* a band over the crown, cups over the ears, a boom to the corner of the mouth */
    V3 band[9];
    for (int i = 0; i <= 8; i++) {
      double a = PI * i / 8;
      band[i] = HP(&h, 0.4 + sin(a) * 0.62, cos(a) * 0.5, -0.03);
    }
    for (int i = 0; i + 1 < 9; i++) tube(K, band[i], band[i + 1], h.F.fwd, 0.004, 0.003, 0.004, 0.003, FR_KIT, FP_NONE, o);
    for (int e = 0; e < 2; e++) {
      double sx = e ? -1 : 1;
      V3 c = HP(&h, 0.4, 0.5 * sx, -0.04);
      tube(K, sub(c, mul(h.F.across, 0.04 * sx * h.hH)), add(c, mul(h.F.across, 0.04 * sx * h.hH)), h.F.fwd, 0.011, 0.012, 0.011, 0.012, FR_KIT, FP_NONE, o);
    }
    tube(K, HP(&h, 0.38, 0.5, 0.0), HP(&h, 0.22, 0.25, 0.45), h.F.across, 0.0015, 0.0015, 0.0015, 0.0015, FR_KIT, FP_NONE, o);
    tube(K, HP(&h, 0.22, 0.25, 0.45), HP(&h, 0.2, 0.1, 0.5), h.F.across, 0.004, 0.004, 0.004, 0.004, FR_KIT, FP_NONE, o);
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
      const uint8_t *u = (f->kit.uniform == FU_KHAKI ? PAL_KHAKI : PAL_COVERALLS)[role];
      if (role == FR_SHIRT || role == FR_SLEEVE || role == FR_TROUSERS || role == FR_BELT || role == FR_COLLAR || role == FR_POCKET ||
          role == FR_TAPE || role == FR_TEE)
        c[i] = u[i];
      else c[i] = PAL_SHARED[role][i];
    }
    }
  }
}

/* ---- drawing ---- */
typedef struct { float z; int idx; float pts[3][2], col[3]; } Shaded;
static int by_depth(const void *a, const void *b) {
  const Shaded *x = a, *y = b;
  if (x->z != y->z) return x->z < y->z ? 1 : -1; /* far first */
  return x->idx - y->idx;
}

#define FIG_MAX_TRIS 4096
void fig_draw(const FigSpec *figs, int n, const FigCam *cam, const FigLight *light, FigFill fill, void *ud) {
  static FigTri tris[FIG_MAX_TRIS];
  Shaded *all = malloc(sizeof *all * (size_t)FIG_MAX_TRIS * (size_t)(n > 0 ? n : 1));
  if (!all) return;
  int na = 0;
  V3 cp = v3(cam->pos[0], cam->pos[1], cam->pos[2]), cf = v3(cam->f[0], cam->f[1], cam->f[2]);
  V3 cu = v3(cam->u[0], cam->u[1], cam->u[2]), cr = v3(cam->r[0], cam->r[1], cam->r[2]), ld = v3(light->dir[0], light->dir[1], light->dir[2]);
  for (int k = 0; k < n; k++) {
    int nt = fig_build(&figs[k], tris, FIG_MAX_TRIS);
    for (int i = 0; i < nt; i++) {
      const FigTri *t = &tris[i];
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
      for (int q = 0; q < 3; q++) {
        V3 p = q == 0 ? a : q == 1 ? b : c, qd = sub(p, cp);
        double zz = dot(qd, cf);
        if (zz < 0.05) zz = 0.05;
        s->pts[q][0] = (float)(cam->cx + dot(qd, cr) / zz * cam->F);
        s->pts[q][1] = (float)(cam->cy - dot(qd, cu) / zz * cam->F);
      }
      float base[3];
      fig_colour(&figs[k], t->role, base);
      double kl = dot(nn, ld), vv = -dot(nn, norm(d));
      if (kl < 0) kl = 0;
      if (vv < 0) vv = 0;
      double rim = pow(1 - vv, 3);
      for (int q = 0; q < 3; q++) {
        double v = base[q] * (0.24 + 0.85 * kl * light->key[q] / 255) + 255 * rim * 0.2 * light->rim[q] / 255;
        s->col[q] = (float)(v > 255 ? 255 : v);
      }
      na++;
    }
  }
  qsort(all, (size_t)na, sizeof *all, by_depth);
  for (int i = 0; i < na; i++) fill((const float(*)[2])all[i].pts, all[i].col, ud);
  free(all);
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

void fig_crew(int who, float H, float raise, FigSpec *f) {
  memset(f, 0, sizeof *f);
  f->height = H;
  FigPose *p = &f->pose;
  if (who == FC_OPERATOR) { /* the ship's flame-resistant coveralls and a headset, seated, leaning in to his scope */
    f->kit = (FigKit){ FU_COVERALLS, FD_NONE, false, false, true, true, false, false };
    memcpy(f->skin, (uint8_t[]){ 205, 160, 128 }, 3);
    memcpy(f->hair, (uint8_t[]){ 34, 26, 20 }, 3);
    seat(H, p);
    SET(FB_HIPS, 14, 0, 0), SET(FB_CHEST, 8, 0, 0), SET(FB_NECK, -12, 0, 0), SET(FB_HEAD, -8, 0, 0);
    SET(FB_UPPER_L, 0, -62, -58), SET(FB_UPPER_R, 0, 62, 58), SET(FB_FORE_L, 0, -48, 6), SET(FB_FORE_R, 0, 48, -6);
    SET(FB_HAND_L, 0, -10, -8), SET(FB_HAND_R, 0, 10, 8);
    p->curl[0] = 30, p->curl[1] = 34;
  } else if (who == FC_SUPERVISOR) { /* the chief in service khakis, standing, the clipboard held at his waist */
    f->kit = (FigKit){ FU_KHAKI, FD_ANCHOR, true, false, false, false, true, false };
    memcpy(f->skin, (uint8_t[]){ 190, 140, 110 }, 3);
    memcpy(f->hair, (uint8_t[]){ 46, 40, 36 }, 3);
    pose_clear(p);
    SET(FB_UPPER_L, 0, -8, -82), SET(FB_FORE_L, 0, -86, 0), SET(FB_HAND_L, 0, 0, 16);
    SET(FB_UPPER_R, 0, 8, 82), SET(FB_FORE_R, 0, 12, 0), SET(FB_HEAD, 0, -6, 0);
    p->curl[0] = 60, p->curl[1] = 24;
  } else { /* the commander in khakis with his oak leaves and the gold on his cap's bill, in his chair */
    f->kit = (FigKit){ FU_KHAKI, FD_OAK, true, true, false, false, false, false };
    memcpy(f->skin, (uint8_t[]){ 196, 150, 120 }, 3);
    memcpy(f->hair, (uint8_t[]){ 140, 138, 134 }, 3);
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
  memset(&f, 0, sizeof f);
  f.height = (float)H;
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
