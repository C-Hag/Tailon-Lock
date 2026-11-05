/* room.c - the control room in 3D (see room.h), the same as the browser game's (assets/figure/figure.js: room, chair,
 * roomLamps, lightRoom), step for step, in double as JavaScript's arithmetic is, so both make the same triangles
 * (tests/test_figure.c --room, assets/figure/parity.js). Original code for this project. */
#include "room.h"

#include <math.h>
#include <string.h>

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

/* the paints */
typedef struct { double c[3]; } Paint3;
#define PT(r, g, b) ((Paint3){ { r, g, b } })
static const Paint3 P_PLATE = PT(150, 154, 150), P_RIVET = PT(176, 180, 174), P_WELD = PT(70, 72, 70), P_FRAME = PT(132, 136, 132),
                    P_DECK = PT(70, 72, 74), P_DECKHEAD = PT(120, 124, 122), P_MAT = PT(26, 26, 28), P_RIB = PT(40, 40, 42),
                    P_PIPE = PT(184, 184, 178), P_PIPE_GREY = PT(110, 116, 120), P_FIRE = PT(150, 34, 28), P_CABLE = PT(32, 32, 34),
                    P_TRAY = PT(96, 100, 100), P_DUCT = PT(140, 144, 142), P_CONSOLE = PT(120, 126, 130), P_CONSOLE_DARK = PT(74, 80, 84),
                    P_BEZEL = PT(26, 28, 30), P_GLASS = PT(6, 12, 9), P_LABEL = PT(30, 32, 34), P_PHONE = PT(104, 108, 104),
                    P_BLACK = PT(22, 22, 24), P_ALU = PT(170, 176, 182), P_EDGE = PT(255, 214, 140), P_LENS_BLUE = PT(130, 160, 230),
                    P_LENS_RED = PT(235, 50, 36), P_MUG = PT(200, 200, 192), P_COFFEE = PT(40, 24, 14), P_VINYL = PT(60, 70, 96),
                    P_STEEL = PT(92, 96, 100), P_BALL = PT(120, 132, 138), P_GQ = PT(30, 8, 6), P_BOARD = PT(20, 26, 30);

/* the builder: where the triangles and screens go, and the state each new triangle takes */
typedef struct {
  FigTri *t;
  int n, max;
  RoomScreen *scr;
  int ns, maxs;
  bool full;
  int part, emit, tag;
  bool two;
} B;

static void tri(B *b, V3 p, V3 q, V3 r, Paint3 col) {
  if (b->n >= b->max) {
    b->full = true;
    return;
  }
  FigTri *t = &b->t[b->n++];
  memset(t, 0, sizeof *t);
  for (int k = 0; k < 3; k++) t->v[0][k] = (float)p.v[k], t->v[1][k] = (float)q.v[k], t->v[2][k] = (float)r.v[k], t->col[k] = (float)col.c[k];
  t->shade = FS_OWN;
  t->part = (uint8_t)b->part;
  t->emit = (uint8_t)b->emit;
  t->tag = (uint8_t)b->tag;
  t->two = b->two;
}
/* a quad o, o+a, o+a+b, o+b, facing along a x b */
static void quad(B *b, V3 o, V3 a, V3 c, Paint3 col) {
  V3 p1 = add(o, a), p2 = add(p1, c), p3 = add(o, c);
  tri(b, o, p1, p2, col);
  tri(b, o, p2, p3, col);
}
static void tile(B *b, V3 o, V3 a, V3 c, int na, int nb, Paint3 col) {
  for (int i = 0; i < na; i++)
    for (int j = 0; j < nb; j++) quad(b, add(o, add(mul(a, (double)i / na), mul(c, (double)j / nb))), mul(a, 1.0 / na), mul(c, 1.0 / nb), col);
}
/* a box from corner o along X, Y and Z (X x Y = Z); faces: 1 front (-Z), 2 back, 4 left (-X), 8 right, 16 top, 32 bottom */
static void box(B *b, V3 o, V3 X, V3 Y, V3 Z, Paint3 col, int faces) {
  if (faces & 1) quad(b, o, Y, X, col);
  if (faces & 2) quad(b, add(o, Z), X, Y, col);
  if (faces & 4) quad(b, o, Z, Y, col);
  if (faces & 8) quad(b, add(o, X), Y, Z, col);
  if (faces & 16) quad(b, add(o, Y), Z, X, col);
  if (faces & 32) quad(b, o, X, Z, col);
}
static void abox(B *b, double x0, double y0, double z0, double x1, double y1, double z1, Paint3 col, int faces) {
  box(b, v3(x0, y0, z0), v3(x1 - x0, 0, 0), v3(0, y1 - y0, 0), v3(0, 0, z1 - z0), col, faces);
}
static V3 ring_at(V3 c, V3 p1, V3 p2, double r, int i, int n) {
  return add(c, add(mul(p1, r * cos(2 * PI * i / n)), mul(p2, r * sin(2 * PI * i / n))));
}
/* a cylinder from a to e, n sides, capped at both ends if caps */
static void cyl(B *b, V3 a, V3 e, double r, int n, Paint3 col, bool caps) {
  V3 u = norm(sub(e, a)), h = fabs(u.v[1]) < 0.9 ? v3(0, 1, 0) : v3(1, 0, 0), p1 = norm(cross(h, u)), p2 = cross(u, p1), L = sub(e, a);
  for (int i = 0; i < n; i++) {
    V3 A = ring_at(a, p1, p2, r, i, n);
    quad(b, A, sub(ring_at(a, p1, p2, r, i + 1, n), A), L, col);
  }
  if (caps)
    for (int i = 0; i < n; i++) {
      tri(b, e, ring_at(e, p1, p2, r, i, n), ring_at(e, p1, p2, r, i + 1, n), col);
      tri(b, a, ring_at(a, p1, p2, r, i + 1, n), ring_at(a, p1, p2, r, i, n), col);
    }
}
/* a panel o + ex u + ey v (u, v 0-1) with a hole (u0, v0)-(u1, v1) for what sits in it, so nothing lies on top of
 * anything else and the painter's sort has nothing to get wrong */
static V3 fat(V3 o, V3 ex, V3 ey, double u, double v) { return add(o, add(mul(ex, u), mul(ey, v))); }
static void fq(B *b, V3 o, V3 ex, V3 ey, double ua, double va, double ub, double vb, Paint3 col) {
  quad(b, fat(o, ex, ey, ua, va), mul(ex, ub - ua), mul(ey, vb - va), col);
}
static void frame(B *b, V3 o, V3 ex, V3 ey, double u0, double v0, double u1, double v1, Paint3 col) {
  fq(b, o, ex, ey, 0, 0, 1, v0, col);
  fq(b, o, ex, ey, 0, v1, 1, 1, col);
  fq(b, o, ex, ey, 0, v0, u0, v1, col);
  fq(b, o, ex, ey, u1, v0, 1, v1, col);
}
static void screen(B *b, int kind, V3 o, V3 ex, V3 ey, double w, double h, int bay, const Paint3 *col) {
  if (b->ns >= b->maxs) {
    b->full = true;
    return;
  }
  RoomScreen *s = &b->scr[b->ns++];
  s->kind = kind, s->bay = bay, s->w = (float)w, s->h = (float)h;
  for (int k = 0; k < 3; k++) s->o[k] = (float)o.v[k], s->ex[k] = (float)ex.v[k], s->ey[k] = (float)ey.v[k];
  int e = b->emit, g = b->tag;
  b->emit = RE_SELF, b->tag = b->ns;
  if (col) quad(b, o, ex, ey, *col);
  b->emit = e, b->tag = g;
}
static Paint3 shift(Paint3 c, double k) { return PT(c.c[0] + k, c.c[1] + k, c.c[2] + k); }

/* the operator's console: a frame turned toward his chair, in its own axes */
typedef struct { V3 c, X, Y, Z; double L; } Near;
static V3 nat(const Near *N, double u, double v, double w) {
  return add(add(add(N->c, mul(N->X, u - N->L - 0.13)), mul(N->Y, v - 1.25)), mul(N->Z, w));
}
static void lbox(B *b, const Near *N, double u0, double v0, double w0, double u1, double v1, double w1, Paint3 col, int faces) {
  box(b, nat(N, u0, v0, w0), mul(N->X, u1 - u0), mul(N->Y, v1 - v0), mul(N->Z, w1 - w0), col, faces);
}

int room_build(FigTri *out, int max, RoomScreen *scr, int maxscr, int *nscr) {
  B b0 = { out, 0, max, scr, 0, maxscr, false, RP_FAR, RE_LIT, 0, false }, *b = &b0;
  const double ZW = 4.4, X0 = -4.2, X1 = 5.4, YC = 2.55;
  /* the after bulkhead: three strakes of plates, rivet rows along their butts and seams, the welds, the frames */
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 8; k++) {
      double x = X0 + k * 1.2, y = r * 0.85;
      tile(b, v3(x, y + 0.85, ZW), v3(1.2, 0, 0), v3(0, -0.85, 0), 2, 2, shift(P_PLATE, ((k * 7 + r * 3) % 5 - 2) * 3));
      for (int i = 0; i < 6; i++) quad(b, v3(x + 0.03, y + 0.1 + i * 0.13 + 0.012, ZW - 0.004), v3(0.024, 0, 0), v3(0, -0.024, 0), P_RIVET);
      for (int i = 0; i < 8; i++) quad(b, v3(x + 0.1 + i * 0.14, y + 0.85 - 0.028, ZW - 0.004), v3(0.024, 0, 0), v3(0, -0.024, 0), P_RIVET);
    }
  static const double SEAMS[2] = { 0.85, 1.7 };
  for (int s = 0; s < 2; s++)
    for (int k = 0; k < 8; k++) quad(b, v3(X0 + k * 1.2, SEAMS[s] + 0.007, ZW - 0.002), v3(1.2, 0, 0), v3(0, -0.014, 0), P_WELD);
  for (int k = 0; k <= 4; k++)
    for (int j = 0; j < 3; j++)
      abox(b, X0 + 0.6 + k * 2.4 - 0.04, j * 0.85, ZW - 0.14, X0 + 0.6 + k * 2.4 + 0.04, (j + 1) * 0.85, ZW, P_FRAME, 1 | 4 | 8);
  /* the deck, and the ribbed rubber mats in front of the consoles */
  tile(b, v3(X0, 0, 0.2), v3(0, 0, ZW - 0.2), v3(X1 - X0, 0, 0), 7, 12, P_DECK);
  static const double MATS[2][2] = { { -3.9, -0.3 }, { 0.5, 4.6 } };
  for (int m = 0; m < 2; m++) {
    double m0 = MATS[m][0], m1 = MATS[m][1];
    tile(b, v3(m0, 0.006, 3.0), v3(0, 0, 0.64), v3(m1 - m0, 0, 0), 1, 4, P_MAT);
    for (int i = 0; i < 7; i++) quad(b, v3(m0 + 0.04, 0.01, 3.04 + i * 0.09), v3(0, 0, 0.022), v3(m1 - m0 - 0.08, 0, 0), P_RIB);
  }
  /* the deckhead: its beams, the pipe runs with their flanges and hangers, the fire main and its valve, a cable tray,
   * the trunk of ducting, and the pipes coming down the bulkhead */
  tile(b, v3(X0, YC, 2.2), v3(X1 - X0, 0, 0), v3(0, 0, ZW - 2.2), 12, 3, P_DECKHEAD);
  static const double BEAMS[2] = { 2.75, 3.6 };
  for (int s = 0; s < 2; s++)
    for (int k = 0; k < 8; k++) abox(b, X0 + k * 1.2, YC - 0.15, BEAMS[s] - 0.03, X0 + (k + 1) * 1.2, YC, BEAMS[s] + 0.03, P_FRAME, 1 | 2 | 32);
  static const double PIPES[5][3] = { { 2.4, 2.95, 0.06 }, { 2.29, 3.2, 0.035 }, { 2.42, 3.4, 0.05 }, { 2.36, 3.85, 0.075 }, { 2.28, 4.08, 0.04 } };
  const Paint3 PIPE_COL[5] = { P_PIPE, P_PIPE_GREY, P_FIRE, P_PIPE, P_PIPE_GREY };
  for (int p = 0; p < 5; p++) {
    double y = PIPES[p][0], z = PIPES[p][1], r = PIPES[p][2];
    for (int k = 0; k < 6; k++) {
      double x = X0 + k * 1.6;
      cyl(b, v3(x, y, z), v3(x + 1.6, y, z), r, 8, PIPE_COL[p], false);
      cyl(b, v3(x + 1.585, y, z), v3(x + 1.615, y, z), r * 1.4, 8, PIPE_COL[p], true);
      abox(b, x + 0.8 - 0.012, y + r, z - 0.012, x + 0.8 + 0.012, YC, z + 0.012, P_STEEL, 1 | 4 | 8);
    }
  }
  cyl(b, v3(1.2, 2.42, 3.33), v3(1.2, 2.42, 3.29), 0.1, 10, P_FIRE, true);
  cyl(b, v3(1.2, 2.42, 3.4), v3(1.2, 2.42, 3.33), 0.012, 5, P_STEEL, false);
  for (int k = 0; k < 6; k++) {
    double x = X0 + k * 1.6;
    abox(b, x, 2.15, 3.55, x + 1.6, 2.17, 3.86, P_TRAY, 1 | 16 | 32);
    abox(b, x, 2.17, 3.55, x + 1.6, 2.22, 3.56, P_TRAY, 1 | 2 | 16);
    for (int i = 0; i < 4; i++)
      cyl(b, v3(x, 2.19 + (i & 1) * 0.012, 3.6 + i * 0.065), v3(x + 1.6, 2.19 + (i & 1) * 0.012, 3.6 + i * 0.065), 0.016, 5, i == 2 ? P_PIPE_GREY : P_CABLE,
          false);
    abox(b, x, 2.08, 4.12, x + 1.6, 2.4, 4.38, P_DUCT, 1 | 16 | 32);
    abox(b, x + 1.56, 2.06, 4.1, x + 1.6, 2.42, 4.38, P_FRAME, 1 | 4 | 8 | 32);
  }
  static const double DOWN[3] = { -3.05, 0.75, 4.0 };
  for (int p = 0; p < 3; p++)
    for (int j = 0; j < 3; j++) cyl(b, v3(DOWN[p], j * 0.7, ZW - 0.06), v3(DOWN[p], (j + 1) * 0.7, ZW - 0.06), 0.035, 8, P_PIPE_GREY, false);
  /* the lamps: dimmed blue fixtures overhead, red battle lanterns on the bulkhead */
  static const double BLUE[3] = { -2.4, 0.4, 3.0 }, RED[3] = { -1.6, 1.95, 4.6 };
  for (int i = 0; i < 3; i++) {
    double x = BLUE[i];
    abox(b, x - 0.35, YC - 0.07, 3.0, x + 0.35, YC, 3.16, P_FRAME, 1 | 2 | 4 | 8);
    b->emit = RE_BLUE;
    quad(b, v3(x - 0.33, YC - 0.071, 3.02), v3(0.66, 0, 0), v3(0, 0, 0.12), P_LENS_BLUE);
    b->emit = RE_LIT;
  }
  for (int i = 0; i < 3; i++) {
    double x = RED[i];
    abox(b, x - 0.07, 2.02, ZW - 0.1, x + 0.07, 2.18, ZW, P_BLACK, 1 | 4 | 8 | 16);
    b->emit = RE_RED;
    quad(b, v3(x - 0.05, 2.16, ZW - 0.102), v3(0.1, 0, 0), v3(0, -0.12, 0), P_LENS_RED);
    b->emit = RE_LIT;
  }
  /* the console row: eleven bays, each a cabinet, a writing shelf, a sloped key panel, the display unit with its CRT
   * under a hood and its name plate; two of them are the strike-weapon consoles */
  static const int KINDS[11] = { RS_PPI, RS_ALPHA, RS_ALPHA, RS_PPI, RS_ALPHA, RS_PPI, RS_STRIKE, RS_HARPOON, RS_ALPHA, RS_PPI, RS_ALPHA };
  for (int i = 0; i < 11; i++) {
    double x = -3.9 + i * 0.8, cx = x + 0.39;
    bool weapon = KINDS[i] == RS_STRIKE || KINDS[i] == RS_HARPOON;
    abox(b, x, 0, 3.82, x + 0.78, 0.76, 4.3, P_CONSOLE, 4 | 8);
    tile(b, v3(x, 0.76, 3.82), v3(0.78, 0, 0), v3(0, -0.76, 0), 1, 2, P_CONSOLE);
    abox(b, x, 0.76, 3.6, x + 0.78, 0.8, 4.3, P_CONSOLE_DARK, 1 | 4 | 8 | 16);
    screen(b, weapon ? RS_GUARDS : RS_KEYS, v3(x + 0.04, 0.92, 3.88), v3(0.7, 0, 0), v3(0, -0.12, -0.24), 700, 270, i, &P_CONSOLE_DARK);
    abox(b, x, 0.8, 3.9, x + 0.78, 1.46, 4.3, P_CONSOLE, 4 | 8 | 16);
    frame(b, v3(x, 1.46, 3.9), v3(0.78, 0, 0), v3(0, -0.66, 0), 0.11 / 0.78, 0.01 / 0.66, 0.67 / 0.78, 0.51 / 0.66, P_CONSOLE);
    frame(b, v3(x + 0.11, 1.37, 3.9), v3(0.56, 0, 0), v3(0, -0.42, 0), 0.08 / 0.56, 0.04 / 0.42, 0.48 / 0.56, 0.36 / 0.42, P_BEZEL);
    abox(b, cx - 0.27, 1.33, 3.8, cx + 0.27, 1.355, 3.9, P_CONSOLE_DARK, 1 | 16 | 32);
    screen(b, KINDS[i], v3(cx - 0.2, 1.33, 3.9), v3(0.4, 0, 0), v3(0, -0.32, 0), 400, 320, i, &P_GLASS);
    screen(b, RS_LABEL, v3(x + 0.11, 1.45, 3.9), v3(0.56, 0, 0), v3(0, -0.08, 0), 560, 80, i, &P_LABEL);
  }
  /* on the bulkhead above them: the sound-powered phone boxes with a handset on its hook and a headset hung beside,
   * the clock and the general-quarters light */
  static const int PHONES[4] = { 1, 4, 7, 10 };
  for (int p = 0; p < 4; p++) {
    double cx = -3.9 + PHONES[p] * 0.8 + 0.39;
    abox(b, cx - 0.08, 1.6, ZW - 0.07, cx + 0.08, 1.8, ZW, P_PHONE, 1 | 4 | 8 | 16);
    abox(b, cx + 0.09, 1.6, ZW - 0.06, cx + 0.13, 1.8, ZW - 0.02, P_BLACK, 1 | 4 | 8 | 16);
    for (int s = -1; s <= 1; s += 2) cyl(b, v3(cx - 0.24 + s * 0.065, 1.58, ZW - 0.05), v3(cx - 0.24 + s * 0.065, 1.58, ZW - 0.02), 0.035, 8, P_BLACK, true);
    abox(b, cx - 0.31, 1.64, ZW - 0.04, cx - 0.17, 1.66, ZW - 0.02, P_BLACK, 1 | 16);
  }
  cyl(b, v3(0.3, 1.95, ZW - 0.05), v3(0.3, 1.95, ZW), 0.14, 14, P_BLACK, false);
  b->two = false;
  screen(b, RS_CLOCK, v3(0.18, 2.07, ZW - 0.052), v3(0.24, 0, 0), v3(0, -0.24, 0), 240, 240, -1, NULL);
  abox(b, 2.0, 2.14, 3.92, 2.7, 2.3, 4.0, P_BLACK, 1 | 4 | 8 | 32);
  abox(b, 2.33, 2.3, 3.95, 2.37, 2.4, 3.97, P_STEEL, 1 | 4 | 8);
  screen(b, RS_GQ, v3(2.02, 2.28, 3.918), v3(0.66, 0, 0), v3(0, -0.12, 0), 660, 120, -1, &P_GQ);
  /* the plexiglass status boards on their posts, lit from their top edge */
  static const double BX[2] = { 0.15, 2.25 };
  for (int q = 0; q < 2; q++) {
    double x0 = BX[q], XS[2] = { x0 - 0.03, x0 + 1.95 + 0.03 };
    for (int p = 0; p < 2; p++)
      for (int j = 0; j < 3; j++) abox(b, XS[p] - 0.02, j * 0.85, 3.13, XS[p] + 0.02, (j + 1) * 0.85, 3.17, P_ALU, 1 | 4 | 8);
    abox(b, x0, 1.95, 3.13, x0 + 1.95, 1.99, 3.17, P_ALU, 1 | 32);
    abox(b, x0, 0.97, 3.13, x0 + 1.95, 1.0, 3.17, P_ALU, 1 | 16);
    b->emit = RE_SELF;
    quad(b, v3(x0, 1.95, 3.128), v3(1.95, 0, 0), v3(0, -0.008, 0), P_EDGE);
    b->emit = RE_LIT;
    b->two = true;
    screen(b, q ? RS_BOARD_R : RS_BOARD_L, v3(x0, 1.95, 3.15), v3(1.95, 0, 0), v3(0, -0.95, 0), 1950, 950, -1, &P_BOARD);
    b->two = false;
  }
  /* ---- near: the radar operator's console, turned toward his chair ---- */
  b->part = RP_NEAR;
  double th = -25 * D, sw = 0.57, sh = 0.376;
  Near N = { v3(-0.557, 1.25, 1.25), v3(cos(th), 0, -sin(th)), v3(0, 1, 0), v3(sin(th), 0, cos(th)), 0.42 * 0.57 };
  const Near *n = &N;
  double W = sw + 0.26;
  lbox(b, n, 0, 0, 0, W, 0.9, 0.55, P_CONSOLE, 1 | 4 | 8);
  lbox(b, n, 0, 0.9, -0.32, W, 0.94, 0.55, P_CONSOLE_DARK, 1 | 4 | 8 | 16);
  lbox(b, n, 0.1, 0.94, -0.28, 0.58, 0.968, -0.05, P_BLACK, 1 | 4 | 8);
  screen(b, RS_KEYBOARD, nat(n, 0.1, 0.968, -0.05), mul(N.X, 0.48), mul(N.Z, -0.23), 480, 230, -1, &P_BLACK);
  lbox(b, n, 0.61, 0.94, -0.25, 0.71, 0.965, -0.1, P_BLACK, 1 | 4 | 8 | 16);
  cyl(b, nat(n, 0.66, 0.965, -0.175), nat(n, 0.66, 0.985, -0.175), 0.032, 10, P_BALL, true);
  lbox(b, n, 0, 0.94, 0.04, W, 1.0, 0.55, P_CONSOLE_DARK, 1 | 4 | 8);
  lbox(b, n, 0, 1.0, 0, W, 1.62, 0.55, P_CONSOLE, 4 | 8 | 16);
  double bt = 1.25 + sh / 2 + 0.035, bh = sh + 0.07, bw = sw + 0.06;
  frame(b, nat(n, 0, 1.62, 0), mul(N.X, W), mul(N.Y, -0.62), 0.1 / W, (1.62 - bt) / 0.62, (0.1 + bw) / W, (1.62 - bt + bh) / 0.62, P_CONSOLE);
  frame(b, nat(n, 0.1, bt, 0), mul(N.X, bw), mul(N.Y, -bh), 0.03 / bw, 0.035 / bh, (0.03 + sw) / bw, (0.035 + sh) / bh, P_BEZEL);
  lbox(b, n, 0.08, bt + 0.01, -0.12, W - 0.08, bt + 0.035, 0, P_CONSOLE_DARK, 1 | 16 | 32);
  screen(b, RS_SCOPE, nat(n, 0.13, 1.25 + sh / 2, 0), mul(N.X, sw), mul(N.Y, -sh), 500, 330, -1, &P_GLASS);
  /* the sound-powered phone's handset hung on the console's side, its cord's jack box, and a mug of coffee */
  lbox(b, n, -0.05, 0.98, 0.08, -0.005, 1.2, 0.14, P_BLACK, 1 | 4 | 16 | 32);
  cyl(b, nat(n, -0.03, 1.2, 0.11), nat(n, -0.03, 1.23, 0.11), 0.03, 8, P_BLACK, true);
  cyl(b, nat(n, -0.03, 0.95, 0.11), nat(n, -0.03, 0.98, 0.11), 0.028, 8, P_BLACK, true);
  V3 m = nat(n, 0.78, 0.94, -0.17);
  cyl(b, m, add(m, v3(0, 0.095, 0)), 0.04, 10, P_MUG, false);
  for (int i = 0; i < 10; i++) {
    double a0 = 2 * PI * i / 10, a1 = 2 * PI * (i + 1) / 10;
    V3 top = add(m, v3(0, 0.088, 0));
    tri(b, top, add(top, v3(0.036 * cos(a0), 0, -0.036 * sin(a0))), add(top, v3(0.036 * cos(a1), 0, -0.036 * sin(a1))), P_COFFEE);
  }
  screen(b, RS_STEAM, add(m, v3(0, 0.1, 0)), v3(0.04, 0, 0), v3(0, -0.04, 0), 40, 40, -1, NULL);
  *nscr = b->ns;
  return b->full ? -1 : b->n;
}

int room_chair(bool high, const float seatf[3], float yaw, FigTri *out, int max) {
  B b0 = { out, 0, max, NULL, 0, 0, false, RP_NEAR, RE_LIT, 0, false }, *b = &b0;
  double a = yaw * D, c = cos(a), s = sin(a);
  V3 seat = v3(seatf[0], seatf[1], seatf[2]), Xv = v3(c, 0, -s), Yv = v3(0, 1, 0), Zv = v3(s, 0, c);
#define AT(x, y, z) add(add(add(mul(Xv, x), mul(Yv, y)), mul(Zv, z)), seat)
#define LBOX(x0, y0, z0, x1, y1, z1, col) box(b, AT(x0, y0, z0), mul(Xv, (x1) - (x0)), mul(Yv, (y1) - (y0)), mul(Zv, (z1) - (z0)), col, 63)
  V3 foot = v3(seat.v[0], 0, seat.v[2]);
  cyl(b, foot, v3(seat.v[0], 0.02, seat.v[2]), 0.17, 12, P_STEEL, true);
  for (int i = 0; i < 4; i++) {
    double ab = (45 + 90 * i) * D;
    V3 bb = add(foot, v3(0.13 * cos(ab), 0, 0.13 * sin(ab)));
    cyl(b, bb, add(bb, v3(0, 0.032, 0)), 0.014, 6, P_RIVET, true);
  }
  cyl(b, v3(seat.v[0], 0.02, seat.v[2]), v3(seat.v[0], seat.v[1] - 0.07, seat.v[2]), 0.035, 8, P_STEEL, false);
  LBOX(-0.24, -0.07, -0.22, 0.24, 0, 0.24, P_VINYL);
  LBOX(-0.03, -0.05, -0.28, 0.03, high ? 0.3 : 0.2, -0.24, P_STEEL);
  LBOX(high ? -0.22 : -0.17, 0.12, -0.31, high ? 0.22 : 0.17, high ? 0.62 : 0.4, -0.25, P_VINYL);
  if (high) {
    for (int sx = -1; sx <= 1; sx += 2) {
      LBOX(sx * 0.25 - 0.025, 0.17, -0.2, sx * 0.25 + 0.025, 0.2, 0.16, P_VINYL);
      LBOX(sx * 0.25 - 0.012, 0, -0.08, sx * 0.25 + 0.012, 0.17, -0.05, P_STEEL);
    }
    double ry0 = 0.38 - seat.v[1];
    for (int i = 0; i < 8; i++) {
      double a0 = 2 * PI * i / 8, a1 = 2 * PI * (i + 1) / 8;
      cyl(b, AT(0.2 * cos(a0), ry0, 0.2 * sin(a0)), AT(0.2 * cos(a1), ry0, 0.2 * sin(a1)), 0.01, 5, P_STEEL, false);
    }
  }
#undef AT
#undef LBOX
  return b->full ? -1 : b->n;
}

void room_lamps(const RoomScreen *scr, int n, bool alert, float gqf, RoomLights *L) {
  double gq = gqf, red = alert ? 0.55 + 0.45 * gq : 0.22, blue = alert ? 0.7 : 1;
  double amb[3] = { alert ? 0.07 + 0.06 * gq : 0.045, alert ? 0.045 : 0.065, alert ? 0.07 : 0.11 };
  for (int i = 0; i < 3; i++) L->amb[i] = (float)amb[i];
  L->red = (float)red, L->blue = (float)blue, L->n = 0;
  static const double BLUE[3] = { -2.4, 0.4, 3.0 }, RED[3] = { -1.6, 1.95, 4.6 };
  for (int i = 0; i < 3; i++) L->lamp[L->n++] = (RoomLamp){ { (float)BLUE[i], 2.4f, 3.08f }, { (float)(0.2 * blue), (float)(0.3 * blue), (float)(0.6 * blue) }, 1.4f };
  for (int i = 0; i < 3; i++) L->lamp[L->n++] = (RoomLamp){ { (float)RED[i], 2.1f, 4.2f }, { (float)(0.8 * red), (float)(0.1 * red), (float)(0.07 * red) }, 1.0f };
  /* the glow of each kind of screen: its colour and reach */
  static const double GLOW[8][4] = { { 0 }, { 0.08, 0.4, 0.22, 0.8 }, { 0.05, 0.35, 0.16, 0.7 }, { 0.06, 0.32, 0.18, 0.7 }, { 0.4, 0.25, 0.05, 0.7 },
                                     { 0.4, 0.25, 0.05, 0.7 }, { 0.35, 0.28, 0.12, 1.2 }, { 0.35, 0.28, 0.12, 1.2 } };
  for (int i = 0; i < n && L->n < (int)(sizeof L->lamp / sizeof L->lamp[0]); i++) {
    const RoomScreen *s = &scr[i];
    if (s->kind < 1 || s->kind > 7) continue;
    V3 o = v3(s->o[0], s->o[1], s->o[2]), ex = v3(s->ex[0], s->ex[1], s->ex[2]), ey = v3(s->ey[0], s->ey[1], s->ey[2]);
    V3 nn = norm(cross(ex, ey)), p = add(add(o, mul(add(ex, ey), 0.5)), mul(nn, 0.25));
    RoomLamp *l = &L->lamp[L->n++];
    for (int k = 0; k < 3; k++) l->p[k] = (float)p.v[k], l->c[k] = (float)GLOW[s->kind][k];
    l->r = (float)GLOW[s->kind][3];
  }
}

void room_light(const FigTri *in, FigTri *out, int n, const RoomLights *L) {
  for (int i = 0; i < n; i++) {
    const FigTri *t = &in[i];
    double col[3];
    if (t->emit == RE_SELF)
      for (int q = 0; q < 3; q++) col[q] = t->col[q];
    else if (t->emit)
      for (int q = 0; q < 3; q++) col[q] = t->col[q] * (t->emit == RE_RED ? L->red : L->blue);
    else {
      V3 a = v3(t->v[0][0], t->v[0][1], t->v[0][2]), b = v3(t->v[1][0], t->v[1][1], t->v[1][2]), c = v3(t->v[2][0], t->v[2][1], t->v[2][2]);
      V3 nn = norm(cross(sub(b, a), sub(c, a))), ce = mul(add(add(a, b), c), 1.0 / 3);
      double k[3] = { L->amb[0], L->amb[1], L->amb[2] };
      for (int j = 0; j < L->n; j++) {
        const RoomLamp *lp = &L->lamp[j];
        V3 d = sub(v3(lp->p[0], lp->p[1], lp->p[2]), ce);
        double dl = len3(d), w = 1 / (1 + (dl / lp->r) * (dl / lp->r)), nd = dot(nn, d) / (dl != 0 ? dl : 1);
        if (t->two) nd = fabs(nd);
        double f = (0.3 + 0.7 * (nd > 0 ? nd : 0)) * w;
        for (int q = 0; q < 3; q++) k[q] += lp->c[q] * f;
      }
      for (int q = 0; q < 3; q++) col[q] = t->col[q] * k[q];
    }
    FigTri o = *t;
    for (int q = 0; q < 3; q++) o.col[q] = (float)(col[q] < 255 ? col[q] : 255);
    o.shade = FS_LIT;
    out[i] = o;
  }
}
