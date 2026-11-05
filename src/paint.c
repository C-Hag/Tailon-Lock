/* paint.c - a small 2D vector painter on our framebuffer. Original code for
 * this project. See paint.h. */
#include "paint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 24

typedef struct {
  float    m[6]; /* x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5 */
  float    alpha;
  bool     add;
  uint8_t *clip; /* coverage 0-255 a pixel, or NULL for none */
  bool     own;  /* this level made it (and frees it) */
} PState;

struct Painter {
  Framebuffer *fb;
  PState       st[MAX_DEPTH];
  int          sp;
  /* the path, in device space: points, where each contour starts, whether it is closed */
  float       *pt;
  int          npt, cappt;
  int         *cs;
  bool        *cl;
  int          ncs, capcs;
  float        cx, cy; /* the current point (device) */
  float       *cov;    /* one row of coverage */
  int          capcov;
};

static PState *top(Painter *p) { return &p->st[p->sp]; }

Painter *painter_new(Framebuffer *fb) {
  Painter *p = calloc(1, sizeof *p);
  if (p) painter_begin(p, fb);
  return p;
}

void painter_free(Painter *p) {
  if (!p) return;
  while (p->sp > 0) p_restore(p);
  if (p->st[0].own) free(p->st[0].clip);
  free(p->pt);
  free(p->cs);
  free(p->cl);
  free(p->cov);
  free(p);
}

void painter_begin(Painter *p, Framebuffer *fb) {
  while (p->sp > 0) p_restore(p);
  if (p->st[0].own) free(p->st[0].clip);
  p->fb = fb;
  PState *s = top(p);
  memset(s, 0, sizeof *s);
  s->m[0] = s->m[3] = 1;
  s->alpha = 1;
  p->npt = p->ncs = 0;
}

void p_save(Painter *p) {
  if (p->sp + 1 >= MAX_DEPTH) return;
  p->st[p->sp + 1] = p->st[p->sp];
  p->st[++p->sp].own = false;
}

void p_restore(Painter *p) {
  if (p->sp == 0) return;
  if (top(p)->own) free(top(p)->clip);
  p->sp--;
}

void p_reset_transform(Painter *p) {
  float *m = top(p)->m;
  m[0] = m[3] = 1;
  m[1] = m[2] = m[4] = m[5] = 0;
}

void p_transform(Painter *p, float a, float b, float c, float d, float e, float f) {
  float *m = top(p)->m, n[6];
  n[0] = m[0] * a + m[2] * b;
  n[1] = m[1] * a + m[3] * b;
  n[2] = m[0] * c + m[2] * d;
  n[3] = m[1] * c + m[3] * d;
  n[4] = m[0] * e + m[2] * f + m[4];
  n[5] = m[1] * e + m[3] * f + m[5];
  memcpy(m, n, sizeof n);
}
void p_translate(Painter *p, float x, float y) { p_transform(p, 1, 0, 0, 1, x, y); }
void p_scale(Painter *p, float sx, float sy) { p_transform(p, sx, 0, 0, sy, 0, 0); }
void p_rotate(Painter *p, float a) {
  float c = cosf(a), s = sinf(a);
  p_transform(p, c, s, -s, c, 0, 0);
}
void p_alpha(Painter *p, float a) { top(p)->alpha = a; }
void p_additive(Painter *p, bool on) { top(p)->add = on; }

static void dev(const Painter *p, float x, float y, float *ox, float *oy) {
  const float *m = p->st[p->sp].m;
  *ox = m[0] * x + m[2] * y + m[4];
  *oy = m[1] * x + m[3] * y + m[5];
}
static float dev_scale(const Painter *p) {
  const float *m = p->st[p->sp].m;
  return sqrtf(fabsf(m[0] * m[3] - m[1] * m[2]));
}

/* ---- the path ---------------------------------------------------------------- */

static void push_pt(Painter *p, float x, float y) {
  if (p->npt == p->cappt) {
    int cap = p->cappt ? p->cappt * 2 : 1024;
    float *n = realloc(p->pt, (size_t)cap * 2 * sizeof *n);
    if (!n) return;
    p->pt = n;
    p->cappt = cap;
  }
  p->pt[2 * p->npt] = x;
  p->pt[2 * p->npt + 1] = y;
  p->npt++;
  p->cx = x;
  p->cy = y;
}

static void new_contour(Painter *p) {
  if (p->ncs == p->capcs) {
    int cap = p->capcs ? p->capcs * 2 : 64;
    int *n = realloc(p->cs, (size_t)cap * sizeof *n);
    bool *c = realloc(p->cl, (size_t)cap * sizeof *c);
    if (n) p->cs = n;
    if (c) p->cl = c;
    if (!n || !c) return;
    p->capcs = cap;
  }
  p->cs[p->ncs] = p->npt;
  p->cl[p->ncs] = false;
  p->ncs++;
}

void p_begin(Painter *p) { p->npt = p->ncs = 0; }

void p_move(Painter *p, float x, float y) {
  float dx, dy;
  dev(p, x, y, &dx, &dy);
  new_contour(p);
  push_pt(p, dx, dy);
}

void p_line(Painter *p, float x, float y) {
  float dx, dy;
  dev(p, x, y, &dx, &dy);
  if (!p->ncs) new_contour(p);
  push_pt(p, dx, dy);
}

/* how many straight pieces a curve with this control polygon needs (device pixels) */
static int pieces(float len) {
  int n = (int)(sqrtf(len) * 1.6f) + 2;
  return n > 64 ? 64 : n;
}

void p_quad(Painter *p, float cx, float cy, float x, float y) {
  float ax = p->cx, ay = p->cy, bx, by, ex, ey;
  dev(p, cx, cy, &bx, &by);
  dev(p, x, y, &ex, &ey);
  if (!p->ncs) {
    new_contour(p);
    push_pt(p, ax, ay);
  }
  int n = pieces(hypotf(bx - ax, by - ay) + hypotf(ex - bx, ey - by));
  for (int i = 1; i <= n; i++) {
    float t = (float)i / n, u = 1 - t;
    push_pt(p, u * u * ax + 2 * u * t * bx + t * t * ex, u * u * ay + 2 * u * t * by + t * t * ey);
  }
}

void p_cubic(Painter *p, float c1x, float c1y, float c2x, float c2y, float x, float y) {
  float ax = p->cx, ay = p->cy, bx, by, cx2, cy2, ex, ey;
  dev(p, c1x, c1y, &bx, &by);
  dev(p, c2x, c2y, &cx2, &cy2);
  dev(p, x, y, &ex, &ey);
  if (!p->ncs) {
    new_contour(p);
    push_pt(p, ax, ay);
  }
  int n = pieces(hypotf(bx - ax, by - ay) + hypotf(cx2 - bx, cy2 - by) + hypotf(ex - cx2, ey - cy2));
  for (int i = 1; i <= n; i++) {
    float t = (float)i / n, u = 1 - t;
    float k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
    push_pt(p, k0 * ax + k1 * bx + k2 * cx2 + k3 * ex, k0 * ay + k1 * by + k2 * cy2 + k3 * ey);
  }
}

void p_close(Painter *p) {
  if (p->ncs && p->cs[p->ncs - 1] < p->npt) { /* a contour with no point yet (its first one did not fit): nothing to close */
    p->cl[p->ncs - 1] = true;
    int s = p->cs[p->ncs - 1];
    p->cx = p->pt[2 * s];
    p->cy = p->pt[2 * s + 1];
  }
}

void p_rect(Painter *p, float x, float y, float w, float h) {
  p_move(p, x, y);
  p_line(p, x + w, y);
  p_line(p, x + w, y + h);
  p_line(p, x, y + h);
  p_close(p);
}

void p_ellipse(Painter *p, float cx, float cy, float rx, float ry, float rot, float a0, float a1) {
  while (a1 < a0) a1 += 6.2831853f; /* clockwise from a0 to a1, past the end if it is smaller (as a canvas does) */
  float r = (rx > ry ? rx : ry) * dev_scale(p);
  int n = (int)(fabsf(a1 - a0) / 6.2831853f * (sqrtf(r) * 6 + 12)) + 3;
  if (n > 160) n = 160;
  float cr = cosf(rot), sr = sinf(rot);
  for (int i = 0; i <= n; i++) {
    float a = a0 + (a1 - a0) * i / n, ex = rx * cosf(a), ey = ry * sinf(a);
    float x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
    if (i == 0) p_move(p, x, y);
    else p_line(p, x, y);
  }
  if (fabsf(a1 - a0) >= 6.2831f) p_close(p);
}

void p_curve(Painter *p, const float (*P)[2], int n, bool closed) {
  if (n < 2) return;
#define CPT(i) P[closed ? (((i) % n) + n) % n : ((i) < 0 ? 0 : (i) > n - 1 ? n - 1 : (i))]
  p_move(p, P[0][0], P[0][1]);
  int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; i++) {
    const float *p0 = CPT(i - 1), *p1 = CPT(i), *p2 = CPT(i + 1), *p3 = CPT(i + 2);
    p_cubic(p, p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6, p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6,
            p2[0], p2[1]);
  }
#undef CPT
  if (closed) p_close(p);
}

/* ---- paints --------------------------------------------------------------- */

Paint p_rgba(float r, float g, float b, float a) {
  Paint g0;
  memset(&g0, 0, sizeof g0);
  g0.kind = PAINT_SOLID;
  g0.c[0] = (PColor){ r, g, b, a };
  g0.n = 1;
  return g0;
}

static void inverse(const float *m, float *o) {
  float det = m[0] * m[3] - m[1] * m[2];
  if (fabsf(det) < 1e-12f) det = 1e-12f;
  float id = 1 / det;
  o[0] = m[3] * id;
  o[1] = -m[1] * id;
  o[2] = -m[2] * id;
  o[3] = m[0] * id;
  o[4] = -(o[0] * m[4] + o[2] * m[5]);
  o[5] = -(o[1] * m[4] + o[3] * m[5]);
}

Paint p_linear(const Painter *p, float x0, float y0, float x1, float y1) {
  Paint g;
  memset(&g, 0, sizeof g);
  g.kind = PAINT_LINEAR;
  g.x0 = x0, g.y0 = y0, g.x1 = x1, g.y1 = y1;
  inverse(p->st[p->sp].m, g.inv);
  return g;
}

Paint p_radial(const Painter *p, float x, float y, float r0, float r1) {
  Paint g;
  memset(&g, 0, sizeof g);
  g.kind = PAINT_RADIAL;
  g.x0 = x, g.y0 = y, g.r0 = r0, g.r1 = r1;
  inverse(p->st[p->sp].m, g.inv);
  return g;
}

void p_stop(Paint *g, float at, PColor c) {
  if (g->n >= PAINT_STOPS) return;
  g->at[g->n] = at;
  g->c[g->n] = c;
  g->n++;
}

/* the paint's colour at device point (x, y) */
static PColor paint_at(const Paint *g, float x, float y) {
  if (g->kind == PAINT_SOLID || g->n < 2) return g->c[0];
  float ux = g->inv[0] * x + g->inv[2] * y + g->inv[4], uy = g->inv[1] * x + g->inv[3] * y + g->inv[5], t;
  if (g->kind == PAINT_LINEAR) {
    float dx = g->x1 - g->x0, dy = g->y1 - g->y0, l2 = dx * dx + dy * dy;
    t = l2 > 0 ? ((ux - g->x0) * dx + (uy - g->y0) * dy) / l2 : 0;
  } else {
    float d = hypotf(ux - g->x0, uy - g->y0), dr = g->r1 - g->r0;
    t = dr != 0 ? (d - g->r0) / dr : 0;
  }
  if (t <= g->at[0]) return g->c[0];
  for (int i = 1; i < g->n; i++)
    if (t <= g->at[i]) {
      float k = (t - g->at[i - 1]) / fmaxf(1e-6f, g->at[i] - g->at[i - 1]);
      const PColor *a = &g->c[i - 1], *b = &g->c[i];
      return (PColor){ a->r + (b->r - a->r) * k, a->g + (b->g - a->g) * k, a->b + (b->b - a->b) * k, a->a + (b->a - a->a) * k };
    }
  return g->c[g->n - 1];
}

/* ---- filling ---------------------------------------------------------------- */

typedef struct {
  float x0, y0, x1, y1;
  int   dir;
} Edge;

/* coverage of the current path's area, one row at a time, handed to fn */
typedef void (*RowFn)(Painter *p, int y, int x0, int x1, const float *cov, void *ctx);

static void rasterize(Painter *p, const float *pt, const int *cs, int ncs, int npt, RowFn fn, void *ctx) {
  Framebuffer *fb = p->fb;
  if (!npt) return;
  int ne = 0;
  for (int c = 0; c < ncs; c++) {
    int s = cs[c], e = c + 1 < ncs ? cs[c + 1] : npt;
    ne += e - s;
  }
  Edge *E = malloc((size_t)(ne ? ne : 1) * sizeof *E);
  if (!E) return;
  float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
  int k = 0;
  for (int c = 0; c < ncs; c++) {
    int s = cs[c], e = c + 1 < ncs ? cs[c + 1] : npt;
    for (int i = s; i < e; i++) {
      int j = i + 1 < e ? i + 1 : s; /* every contour closes for filling */
      float x0 = pt[2 * i], y0 = pt[2 * i + 1], x1 = pt[2 * j], y1 = pt[2 * j + 1];
      minx = fminf(minx, x0), maxx = fmaxf(maxx, x0), miny = fminf(miny, y0), maxy = fmaxf(maxy, y0);
      if (y0 == y1) continue;
      E[k].dir = y1 > y0 ? 1 : -1;
      if (y1 < y0) {
        float t = x0;
        x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
      }
      E[k].x0 = x0, E[k].y0 = y0, E[k].x1 = x1, E[k].y1 = y1;
      k++;
    }
  }
  ne = k;
  if (!(minx <= maxx && miny <= maxy)) { /* no point is a number: nothing to fill (and 1e30 fits no int) */
    free(E);
    return;
  }
  int X0 = (int)floorf(minx), X1 = (int)ceilf(maxx) + 1, Y0 = (int)floorf(miny), Y1 = (int)ceilf(maxy) + 1;
  if (X0 < fb->cx0) X0 = fb->cx0;
  if (Y0 < fb->cy0) Y0 = fb->cy0;
  if (X1 > fb->cx1) X1 = fb->cx1;
  if (Y1 > fb->cy1) Y1 = fb->cy1;
  if (X0 >= X1 || Y0 >= Y1 || !ne) {
    free(E);
    return;
  }
  int w = X1 - X0 + 2;
  if (w > p->capcov) {
    float *n = realloc(p->cov, (size_t)w * sizeof *n);
    if (!n) {
      free(E);
      return;
    }
    p->cov = n;
    p->capcov = w;
  }
  float *xs = malloc((size_t)ne * sizeof *xs);
  int *ds = malloc((size_t)ne * sizeof *ds);
  if (!xs || !ds) {
    free(E), free(xs), free(ds);
    return;
  }
  for (int y = Y0; y < Y1; y++) {
    float *cov = p->cov;
    memset(cov, 0, (size_t)w * sizeof *cov);
    bool any = false;
    for (int sub = 0; sub < 4; sub++) {
      float sy = y + (sub + 0.5f) * 0.25f;
      int n = 0;
      for (int i = 0; i < ne; i++) {
        const Edge *e = &E[i];
        if (sy < e->y0 || sy >= e->y1) continue;
        float x = e->x0 + (e->x1 - e->x0) * (sy - e->y0) / (e->y1 - e->y0);
        int j = n++; /* insertion sort: few crossings a row */
        while (j > 0 && xs[j - 1] > x) {
          xs[j] = xs[j - 1];
          ds[j] = ds[j - 1];
          j--;
        }
        xs[j] = x;
        ds[j] = e->dir;
      }
      int wind = 0;
      for (int i = 0; i + 1 < n; i++) {
        wind += ds[i];
        if (!wind) continue;
        float a = fmaxf(xs[i], (float)X0), b = fminf(xs[i + 1], (float)X1);
        if (b <= a) continue;
        any = true;
        int ia = (int)floorf(a), ib = (int)floorf(b);
        if (ia == ib) cov[ia - X0] += (b - a) * 0.25f;
        else {
          cov[ia - X0] += (ia + 1 - a) * 0.25f;
          for (int x = ia + 1; x < ib; x++) cov[x - X0] += 0.25f;
          if (ib < X1) cov[ib - X0] += (b - ib) * 0.25f;
        }
      }
    }
    if (any) fn(p, y, X0, X1, cov, ctx);
  }
  free(E), free(xs), free(ds);
}

static void fill_row(Painter *p, int y, int x0, int x1, const float *cov, void *ctx) {
  const Paint *g = ctx;
  const PState *s = top(p);
  Framebuffer *fb = p->fb;
  uint32_t *row = fb->px + (size_t)y * fb->w;
  const uint8_t *clip = s->clip ? s->clip + (size_t)y * fb->w : NULL;
  bool solid = g->kind == PAINT_SOLID || g->n < 2;
  PColor sc = g->c[0];
  for (int x = x0; x < x1; x++) {
    float a = cov[x - x0];
    if (a <= 0.002f) continue;
    if (a > 1) a = 1;
    if (clip) {
      if (!clip[x]) continue;
      a *= clip[x] * (1 / 255.0f);
    }
    PColor c = solid ? sc : paint_at(g, x + 0.5f, y + 0.5f);
    a *= c.a * s->alpha;
    if (a <= 0.001f) continue;
    uint32_t d = row[x];
    float dr = (float)((d >> 16) & 255), dg = (float)((d >> 8) & 255), db = (float)(d & 255), r, gg, b;
    if (s->add) r = dr + c.r * a, gg = dg + c.g * a, b = db + c.b * a;
    else r = dr + (c.r - dr) * a, gg = dg + (c.g - dg) * a, b = db + (c.b - db) * a;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    row[x] = 0xFF000000u | (uint32_t)(r + 0.5f) << 16 | (uint32_t)(gg + 0.5f) << 8 | (uint32_t)(b + 0.5f);
  }
}

void p_fill(Painter *p, const Paint *paint) { rasterize(p, p->pt, p->cs, p->ncs, p->npt, fill_row, (void *)paint); }

void p_fill_rect(Painter *p, float x, float y, float w, float h, const Paint *paint) {
  p_begin(p);
  p_rect(p, x, y, w, h);
  p_fill(p, paint);
}

/* ---- clipping ---------------------------------------------------------------- */

static void clip_row(Painter *p, int y, int x0, int x1, const float *cov, void *ctx) {
  uint8_t *m = ctx;
  for (int x = x0; x < x1; x++) {
    float a = cov[x - x0];
    m[(size_t)y * p->fb->w + x] = (uint8_t)(a >= 1 ? 255 : a * 255);
  }
}

void p_clip(Painter *p) {
  Framebuffer *fb = p->fb;
  size_t n = (size_t)fb->w * fb->h;
  uint8_t *m = calloc(n, 1);
  if (!m) return;
  rasterize(p, p->pt, p->cs, p->ncs, p->npt, clip_row, m);
  PState *s = top(p);
  if (s->clip)
    for (size_t i = 0; i < n; i++) m[i] = (uint8_t)((m[i] * s->clip[i] + 127) / 255);
  if (s->own) free(s->clip);
  s->clip = m;
  s->own = true;
}

/* ---- stroking: every segment a quad and every corner a disc, filled as one (the non-zero union) ---- */

void p_stroke(Painter *p, const Paint *paint, float width) {
  float hw = 0.5f * width * dev_scale(p);
  if (hw < 0.35f) hw = 0.35f; /* hairlines still show, faintly (their coverage is their width) */
  int cap = 0;
  for (int c = 0; c < p->ncs; c++) {
    int s = p->cs[c], e = c + 1 < p->ncs ? p->cs[c + 1] : p->npt;
    cap += (e - s + 1) * (4 + 10);
  }
  float *pt = malloc((size_t)(cap ? cap : 1) * 2 * sizeof *pt);
  int *cs = malloc((size_t)(cap ? cap : 1) * sizeof *cs);
  if (!pt || !cs) {
    free(pt), free(cs);
    return;
  }
  int npt = 0, ncs = 0;
  bool joins = hw > 0.9f;
  for (int c = 0; c < p->ncs; c++) {
    int s = p->cs[c], e = c + 1 < p->ncs ? p->cs[c + 1] : p->npt, n = e - s;
    int segs = p->cl[c] ? n : n - 1;
    for (int i = 0; i < segs; i++) {
      int a = s + i, b = s + (i + 1) % n;
      float ax = p->pt[2 * a], ay = p->pt[2 * a + 1], bx = p->pt[2 * b], by = p->pt[2 * b + 1];
      float dx = bx - ax, dy = by - ay, l = hypotf(dx, dy);
      if (l < 1e-4f) continue;
      float nx = -dy / l * hw, ny = dx / l * hw; /* the quad, always wound the same way */
      cs[ncs++] = npt;
      float q[8] = { ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny };
      memcpy(pt + 2 * npt, q, sizeof q);
      npt += 4;
      if (joins && (i > 0 || p->cl[c])) { /* a disc at the corner, wound the same way */
        cs[ncs++] = npt;
        for (int k = 0; k < 10; k++) {
          float an = -k * 6.2831853f / 10;
          pt[2 * npt] = ax + hw * cosf(an);
          pt[2 * npt + 1] = ay + hw * sinf(an);
          npt++;
        }
      }
    }
  }
  rasterize(p, pt, cs, ncs, npt, fill_row, (void *)paint);
  free(pt), free(cs);
}

/* ---- text and blur ---------------------------------------------------------- */

void p_text(Painter *p, const char *s, float x, float y, float size, PColor c, int align) {
  float dx, dy;
  dev(p, x, y, &dx, &dy);
  float px = size * dev_scale(p);
  /* a font of size px has capitals about 0.7 px tall; ours are 7 dots tall */
  int scale = (int)lroundf(px / 10);
  if (scale < 1) {
    if (px < 3) return; /* too small to read: the web version shows a blur there too */
    scale = 1;
  }
  float a = c.a * top(p)->alpha;
  fb_text(p->fb, s, dx, dy, scale, rgb((uint8_t)(c.r * a), (uint8_t)(c.g * a), (uint8_t)(c.b * a)), align, 0);
}

void p_blur(Framebuffer *fb, int radius, float bright) {
  int W = fb->w, H = fb->h, n = W > H ? W : H;
  uint32_t *tmp = malloc((size_t)n * sizeof *tmp);
  if (!tmp || radius < 1) {
    free(tmp);
    if (bright != 1)
      for (size_t i = 0; i < (size_t)W * H; i++) {
        uint32_t c = fb->px[i];
        fb->px[i] = rgb((uint8_t)(((c >> 16) & 255) * bright), (uint8_t)(((c >> 8) & 255) * bright), (uint8_t)((c & 255) * bright));
      }
    return;
  }
  for (int pass = 0; pass < 3; pass++)
    for (int dir = 0; dir < 2; dir++) {
      int len = dir ? H : W, lines = dir ? W : H;
      for (int l = 0; l < lines; l++) {
        uint32_t *base = dir ? fb->px + l : fb->px + (size_t)l * W;
        size_t step = dir ? (size_t)W : 1;
        int sr = 0, sg = 0, sb = 0, cnt = 0;
        for (int i = -radius; i <= radius; i++) {
          int j = i < 0 ? 0 : i >= len ? len - 1 : i;
          uint32_t c = base[j * step];
          sr += (c >> 16) & 255, sg += (c >> 8) & 255, sb += c & 255, cnt++;
        }
        for (int i = 0; i < len; i++) {
          tmp[i] = rgb((uint8_t)(sr / cnt), (uint8_t)(sg / cnt), (uint8_t)(sb / cnt));
          int out = i - radius < 0 ? 0 : i - radius, in = i + radius + 1 >= len ? len - 1 : i + radius + 1;
          uint32_t co = base[out * step], ci = base[in * step];
          sr += (int)((ci >> 16) & 255) - (int)((co >> 16) & 255);
          sg += (int)((ci >> 8) & 255) - (int)((co >> 8) & 255);
          sb += (int)(ci & 255) - (int)(co & 255);
        }
        float k = pass == 2 && dir == 1 ? bright : 1;
        for (int i = 0; i < len; i++) {
          uint32_t c = tmp[i];
          base[i * step] = k == 1 ? c : rgb((uint8_t)(((c >> 16) & 255) * k), (uint8_t)(((c >> 8) & 255) * k), (uint8_t)((c & 255) * k));
        }
      }
    }
  free(tmp);
}
