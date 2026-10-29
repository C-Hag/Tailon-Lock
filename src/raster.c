/* raster.c - framebuffer, clipped fills, convex polygons, lines, circles and
 * the bitmap font. Replaces the JS Canvas 2D calls. A depth buffer arrives
 * with the Blender models (milestone 6); until then the 3D view uses the
 * JS painter's sort. */
#include "raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int fb_resize(Framebuffer *fb, int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (fb->px && fb->w == w && fb->h == h) return 1;
  uint32_t *px = realloc(fb->px, (size_t)w * (size_t)h * sizeof *px);
  if (!px) return 0;
  fb->px = px;
  float *zb = realloc(fb->zb, (size_t)w * (size_t)h * sizeof *zb);
  if (!zb) return 0;
  fb->zb = zb;
  fb->w = w;
  fb->h = h;
  fb_clip_reset(fb);
  fb->circ = 0;
  return 1;
}

void fb_free(Framebuffer *fb) {
  free(fb->px);
  free(fb->zb);
  fb->px = NULL;
  fb->zb = NULL;
  fb->w = fb->h = 0;
}

void fb_clear(Framebuffer *fb, uint32_t c) {
  size_t n = (size_t)fb->w * (size_t)fb->h;
  for (size_t i = 0; i < n; i++) fb->px[i] = c;
}

void fb_clip(Framebuffer *fb, int x, int y, int w, int h) {
  fb->cx0 = x < 0 ? 0 : x;
  fb->cy0 = y < 0 ? 0 : y;
  fb->cx1 = x + w > fb->w ? fb->w : x + w;
  fb->cy1 = y + h > fb->h ? fb->h : y + h;
}
void fb_clip_reset(Framebuffer *fb) { fb_clip(fb, 0, 0, fb->w, fb->h); }
void fb_clip_circle(Framebuffer *fb, float cx, float cy, float r) {
  fb->circ = 1;
  fb->ccx = cx;
  fb->ccy = cy;
  fb->cr2 = r * r;
}
void fb_clip_circle_off(Framebuffer *fb) { fb->circ = 0; }

/* Source-over with premultiplied alpha: on an opaque framebuffer this is
 * the plain mix; on the GPU renderer's transparent HUD layer it keeps the
 * coverage, so the layer can be laid over the 3D picture. */
static inline uint32_t blend(uint32_t d, uint32_t s, unsigned a /* 0..256 */) {
  uint32_t rb = (((s & 0xFF00FFu) * a + (d & 0xFF00FFu) * (256 - a)) >> 8) & 0xFF00FFu;
  uint32_t g = (((s & 0x00FF00u) * a + (d & 0x00FF00u) * (256 - a)) >> 8) & 0x00FF00u;
  uint32_t da = d >> 24, oa = (a * 255 + da * (256 - a)) >> 8;
  return (oa > 255 ? 255u : oa) << 24 | rb | g;
}

void fb_blend_px(Framebuffer *fb, int x, int y, uint32_t c, float a) {
  if (x < 0 || y < 0 || x >= fb->w || y >= fb->h || a <= 0) return;
  uint32_t *p = &fb->px[(size_t)y * fb->w + x];
  *p = blend(*p, c, a >= 1 ? 256u : (unsigned)(a * 256));
}

/* One horizontal run [x0, x1) on row y, clipped to the rectangle and circle. */
static void span(Framebuffer *fb, int y, int x0, int x1, uint32_t c, unsigned a) {
  if (y < fb->cy0 || y >= fb->cy1) return;
  if (fb->circ) {
    float dy = y + 0.5f - fb->ccy, k = fb->cr2 - dy * dy;
    if (k <= 0) return;
    float hw = sqrtf(k);
    int l = (int)ceilf(fb->ccx - hw - 0.5f), r = (int)floorf(fb->ccx + hw - 0.5f) + 1;
    if (x0 < l) x0 = l;
    if (x1 > r) x1 = r;
  }
  if (x0 < fb->cx0) x0 = fb->cx0;
  if (x1 > fb->cx1) x1 = fb->cx1;
  if (x0 >= x1) return;
  uint32_t *row = fb->px + (size_t)y * fb->w;
  if (a >= 256) for (int i = x0; i < x1; i++) row[i] = c;
  else for (int i = x0; i < x1; i++) row[i] = blend(row[i], c, a);
}

/* A run with a depth plane: w at x is w0 + x * dw. mode 0 tests only, 1
 * tests and writes. */
static void span_z(Framebuffer *fb, int y, int x0, int x1, uint32_t c, unsigned a, float w0, float dw, int write) {
  if (y < fb->cy0 || y >= fb->cy1) return;
  if (fb->circ) {
    float dy = y + 0.5f - fb->ccy, k = fb->cr2 - dy * dy;
    if (k <= 0) return;
    float hw = sqrtf(k);
    int l = (int)ceilf(fb->ccx - hw - 0.5f), r = (int)floorf(fb->ccx + hw - 0.5f) + 1;
    if (x0 < l) x0 = l;
    if (x1 > r) x1 = r;
  }
  if (x0 < fb->cx0) x0 = fb->cx0;
  if (x1 > fb->cx1) x1 = fb->cx1;
  if (x0 >= x1) return;
  size_t o = (size_t)y * fb->w;
  uint32_t *row = fb->px + o;
  float *zr = fb->zb + o;
  float w = w0 + (x0 + 0.5f) * dw;
  for (int i = x0; i < x1; i++, w += dw) {
    if (w <= zr[i]) continue;
    if (write) zr[i] = w;
    row[i] = a >= 256 ? c : blend(row[i], c, a);
  }
}

static unsigned alpha256(float a) {
  if (a <= 0) return 0;
  if (a >= 1) return 256;
  return (unsigned)(a * 256 + 0.5f);
}

void fb_rect_a(Framebuffer *fb, int x, int y, int w, int h, uint32_t c, float a) {
  unsigned al = alpha256(a);
  if (!al) return;
  for (int j = y; j < y + h; j++) span(fb, j, x, x + w, c, al);
}
void fb_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c) { fb_rect_a(fb, x, y, w, h, c, 1); }

void fb_rect_stroke(Framebuffer *fb, float x, float y, float w, float h, uint32_t c, float lw) {
  int t = lw < 1 ? 1 : (int)(lw + 0.5f);
  int x0 = (int)floorf(x - lw / 2 + 0.5f), y0 = (int)floorf(y - lw / 2 + 0.5f);
  int x1 = (int)floorf(x + w - lw / 2 + 0.5f), y1 = (int)floorf(y + h - lw / 2 + 0.5f);
  fb_rect(fb, x0, y0, x1 - x0 + t, t, c);
  fb_rect(fb, x0, y1, x1 - x0 + t, t, c);
  fb_rect(fb, x0, y0, t, y1 - y0 + t, c);
  fb_rect(fb, x1, y0, t, y1 - y0 + t, c);
}

void fb_poly(Framebuffer *fb, const float *xy, int n, uint32_t c, float a, float grow) {
  if (n < 3) return;
  unsigned al = alpha256(a);
  if (!al) return;
  float ymin = xy[1], ymax = xy[1];
  for (int i = 1; i < n; i++) {
    if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
    if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
  }
  float ylo = ymin - grow, yhi = ymax + grow;
  int y0 = (int)ceilf(ylo - 0.5f), y1 = (int)floorf(yhi - 0.5f);
  if (y0 < fb->cy0) y0 = fb->cy0;
  if (y1 >= fb->cy1) y1 = fb->cy1 - 1;
  for (int y = y0; y <= y1; y++) {
    /* sample at the pixel centre, clamped into the polygon's span so thin
     * slivers still get their row */
    float sy = y + 0.5f;
    if (sy < ymin) sy = ymin;
    if (sy > ymax) sy = ymax;
    float xl = 1e30f, xr = -1e30f;
    for (int i = 0; i < n; i++) {
      float ax = xy[2 * i], ay = xy[2 * i + 1];
      int j = (i + 1) % n;
      float bx = xy[2 * j], by = xy[2 * j + 1];
      if ((ay <= sy && by >= sy) || (by <= sy && ay >= sy)) {
        float x;
        if (fabsf(by - ay) < 1e-6f) {
          if (ax < xl) xl = ax;
          if (ax > xr) xr = ax;
          x = bx;
        } else {
          x = ax + (sy - ay) / (by - ay) * (bx - ax);
        }
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    if (xl > xr) continue;
    xl -= grow;
    xr += grow;
    if (xl < -1e6f) xl = -1e6f;
    if (xr > 1e6f) xr = 1e6f;
    int ix0 = (int)ceilf(xl - 0.5f), ix1 = (int)floorf(xr - 0.5f) + 1;
    if (ix1 <= ix0) ix1 = ix0 + 1;
    span(fb, y, ix0, ix1, c, al);
  }
}

void fb_clear_depth(Framebuffer *fb, int x, int y, int w, int h) {
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > fb->w ? fb->w : x + w, y1 = y + h > fb->h ? fb->h : y + h;
  for (int j = y0; j < y1; j++) memset(fb->zb + (size_t)j * fb->w + x0, 0, sizeof(float) * (size_t)(x1 > x0 ? x1 - x0 : 0));
}

void fb_poly_z(Framebuffer *fb, const float *xy, const float *wv, int n, uint32_t c, float a, float grow, int zwrite,
               float bias) {
  if (n < 3) return;
  unsigned al = alpha256(a);
  if (!al) return;
  /* depth plane from the widest triangle of the polygon's fan */
  int bi = 1;
  float best = -1;
  for (int i = 1; i + 1 < n; i++) {
    float ar = fabsf((xy[2 * i] - xy[0]) * (xy[2 * i + 3] - xy[1]) - (xy[2 * i + 2] - xy[0]) * (xy[2 * i + 1] - xy[1]));
    if (ar > best) { best = ar; bi = i; }
  }
  float x0 = xy[0], y0 = xy[1], x1 = xy[2 * bi], y1 = xy[2 * bi + 1], x2 = xy[2 * bi + 2], y2 = xy[2 * bi + 3];
  float w0 = wv[0] * bias, w1 = wv[bi] * bias, w2 = wv[bi + 1] * bias;
  float det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  float dwdx = 0, dwdy = 0;
  if (fabsf(det) > 1e-6f) {
    dwdx = ((w1 - w0) * (y2 - y0) - (w2 - w0) * (y1 - y0)) / det;
    dwdy = ((w2 - w0) * (x1 - x0) - (w1 - w0) * (x2 - x0)) / det;
  } else {
    w0 = fmaxf(w0, fmaxf(w1, w2));
  }
  float ymin = xy[1], ymax = xy[1];
  for (int i = 1; i < n; i++) {
    if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
    if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
  }
  int ya = (int)ceilf(ymin - grow - 0.5f), yb = (int)floorf(ymax + grow - 0.5f);
  if (ya < fb->cy0) ya = fb->cy0;
  if (yb >= fb->cy1) yb = fb->cy1 - 1;
  for (int y = ya; y <= yb; y++) {
    float sy = y + 0.5f;
    if (sy < ymin) sy = ymin;
    if (sy > ymax) sy = ymax;
    float xl = 1e30f, xr = -1e30f;
    for (int i = 0; i < n; i++) {
      float ax = xy[2 * i], ay = xy[2 * i + 1];
      int j = (i + 1) % n;
      float bx = xy[2 * j], by = xy[2 * j + 1];
      if ((ay <= sy && by >= sy) || (by <= sy && ay >= sy)) {
        float x = fabsf(by - ay) < 1e-6f ? ax : ax + (sy - ay) / (by - ay) * (bx - ax);
        if (fabsf(by - ay) < 1e-6f) {
          if (bx < xl) xl = bx;
          if (bx > xr) xr = bx;
        }
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    if (xl > xr) continue;
    xl -= grow;
    xr += grow;
    if (xl < -1e6f) xl = -1e6f;
    if (xr > 1e6f) xr = 1e6f;
    int ix0 = (int)ceilf(xl - 0.5f), ix1 = (int)floorf(xr - 0.5f) + 1;
    if (ix1 <= ix0) ix1 = ix0 + 1;
    /* w at the row's x = 0 */
    float wrow = w0 + (y + 0.5f - y0) * dwdy - x0 * dwdx;
    span_z(fb, y, ix0, ix1, c, al, wrow, dwdx, zwrite);
  }
}

void fb_rect_z(Framebuffer *fb, int x, int y, int w, int h, uint32_t c, float a, float wz) {
  unsigned al = alpha256(a);
  if (!al) return;
  for (int j = y; j < y + h; j++) span_z(fb, j, x, x + w, c, al, wz, 0, 0);
}

void fb_disc_z(Framebuffer *fb, float cx, float cy, float r, uint32_t c, float a, float wz) {
  unsigned al = alpha256(a);
  if (!al) return;
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  if (y0 < fb->cy0) y0 = fb->cy0;
  if (y1 >= fb->cy1) y1 = fb->cy1 - 1;
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - cy, k = r * r - dy * dy;
    if (k < 0) continue;
    float hw = sqrtf(k);
    span_z(fb, y, (int)ceilf(cx - hw - 0.5f), (int)floorf(cx + hw - 0.5f) + 1, c, al, wz, 0, 0);
  }
}

/* Liang-Barsky against the clip rectangle (with a margin for line width). */
static int clip_line(const Framebuffer *fb, float m, float *x0, float *y0, float *x1, float *y1) {
  float xmin = fb->cx0 - m, ymin = fb->cy0 - m, xmax = fb->cx1 + m, ymax = fb->cy1 + m;
  float dx = *x1 - *x0, dy = *y1 - *y0, t0 = 0, t1 = 1;
  float p[4] = { -dx, dx, -dy, dy };
  float q[4] = { *x0 - xmin, xmax - *x0, *y0 - ymin, ymax - *y0 };
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return 0;
    } else {
      float t = q[i] / p[i];
      if (p[i] < 0) { if (t > t1) return 0; if (t > t0) t0 = t; }
      else { if (t < t0) return 0; if (t < t1) t1 = t; }
    }
  }
  float ox = *x0, oy = *y0;
  *x0 = ox + dx * t0;
  *y0 = oy + dy * t0;
  *x1 = ox + dx * t1;
  *y1 = oy + dy * t1;
  return 1;
}

void fb_line(Framebuffer *fb, float x0, float y0, float x1, float y1, uint32_t c, float w, float a) {
  unsigned al = alpha256(a);
  if (!al || !clip_line(fb, w + 1, &x0, &y0, &x1, &y1)) return;
  int t = w < 1.5f ? 1 : (int)(w + 0.5f);
  float dx = x1 - x0, dy = y1 - y0;
  int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
  if (steps < 1) steps = 1;
  float sx = dx / steps, sy = dy / steps, x = x0, y = y0;
  int px = 0x7fffffff, py = 0x7fffffff;
  for (int i = 0; i <= steps; i++, x += sx, y += sy) {
    int ix = (int)floorf(x - (t - 1) * 0.5f), iy = (int)floorf(y - (t - 1) * 0.5f);
    if (ix == px && iy == py) continue;
    px = ix;
    py = iy;
    for (int j = 0; j < t; j++) span(fb, iy + j, ix, ix + t, c, al);
  }
}

void fb_line_z(Framebuffer *fb, float x0, float y0, float x1, float y1, uint32_t c, float w, float a, float wz) {
  unsigned al = alpha256(a);
  if (!al || !clip_line(fb, w + 1, &x0, &y0, &x1, &y1)) return;
  int t = w < 1.5f ? 1 : (int)(w + 0.5f);
  float dx = x1 - x0, dy = y1 - y0;
  int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
  if (steps < 1) steps = 1;
  float sx = dx / steps, sy = dy / steps, x = x0, y = y0;
  for (int i = 0; i <= steps; i++, x += sx, y += sy) {
    int ix = (int)floorf(x - (t - 1) * 0.5f), iy = (int)floorf(y - (t - 1) * 0.5f);
    for (int j = 0; j < t; j++) span_z(fb, iy + j, ix, ix + t, c, al, wz, 0, 0);
  }
}

void fb_circle(Framebuffer *fb, float cx, float cy, float r, uint32_t c) {
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - cy, k = r * r - dy * dy;
    if (k < 0) continue;
    float hw = sqrtf(k);
    span(fb, y, (int)ceilf(cx - hw - 0.5f), (int)floorf(cx + hw - 0.5f) + 1, c, 256);
  }
}

void fb_ring(Framebuffer *fb, float cx, float cy, float r, uint32_t c, float lw) {
  int segs = (int)(r * 2) + 12;
  float px = cx + r, py = cy;
  for (int i = 1; i <= segs; i++) {
    float a = (float)i / segs * 6.2831853f, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
    fb_line(fb, px, py, x, y, c, lw, 1);
    px = x;
    py = y;
  }
}

/* 5x7 glyphs for ASCII 32-95, one byte per row, bit 4 = left column. */
static const uint8_t FONT5x7[64][7] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
  { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
  { 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* " */
  { 0x0a, 0x1f, 0x0a, 0x0a, 0x1f, 0x0a, 0x00 }, /* # */
  { 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04 }, /* $ */
  { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 }, /* % */
  { 0x0c, 0x12, 0x0c, 0x08, 0x15, 0x12, 0x0d }, /* & */
  { 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' */
  { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, /* ( */
  { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, /* ) */
  { 0x00, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x00 }, /* * */
  { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 }, /* + */
  { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 }, /* , */
  { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* - */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 }, /* . */
  { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, /* / */
  { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
  { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
  { 0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f }, /* 2 */
  { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e }, /* 3 */
  { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
  { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
  { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
  { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
  { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
  { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e }, /* 9 */
  { 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00 }, /* : */
  { 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08 }, /* ; */
  { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* < */
  { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 }, /* = */
  { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* > */
  { 0x0e, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04 }, /* ? */
  { 0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0e }, /* @ */
  { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
  { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
  { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
  { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
  { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
  { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
  { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
  { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
  { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
  { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
  { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
  { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
  { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
  { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
  { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
  { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
  { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
  { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
  { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 }, /* Y */
  { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
  { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e }, /* [ */
  { 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01 }, /* backslash */
  { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e }, /* ] */
  { 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00 }, /* ^ */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f }, /* _ */
};

/* Beyond ASCII: the Swedish letters, the degree sign and the Cyrillic
 * capitals that do not look like a Latin one, for the cockpit labels. */
static const struct { uint16_t cp; uint8_t g[7]; } FONT_EXT[] = {
  { 0x00b0, { 0x0c, 0x12, 0x12, 0x0c, 0x00, 0x00, 0x00 } }, /* deg */
  { 0x00c4, { 0x0a, 0x00, 0x0e, 0x11, 0x1f, 0x11, 0x11 } }, /* A diaeresis */
  { 0x00c5, { 0x04, 0x0a, 0x04, 0x0e, 0x11, 0x1f, 0x11 } }, /* A ring */
  { 0x00d6, { 0x0a, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e } }, /* O diaeresis */
  { 0x03b1, { 0x00, 0x00, 0x0d, 0x12, 0x12, 0x12, 0x0d } }, /* alpha */
  { 0x0411, { 0x1f, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x1e } }, /* Be */
  { 0x0413, { 0x1f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 } }, /* Ghe */
  { 0x0414, { 0x0e, 0x0a, 0x0a, 0x0a, 0x0a, 0x1f, 0x11 } }, /* De */
  { 0x0416, { 0x15, 0x15, 0x15, 0x0e, 0x15, 0x15, 0x15 } }, /* Zhe */
  { 0x0417, { 0x0e, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0e } }, /* Ze */
  { 0x0418, { 0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11 } }, /* I */
  { 0x0419, { 0x0a, 0x04, 0x11, 0x13, 0x15, 0x19, 0x11 } }, /* short I */
  { 0x041b, { 0x07, 0x09, 0x09, 0x09, 0x09, 0x09, 0x19 } }, /* El */
  { 0x041f, { 0x1f, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 } }, /* Pe */
  { 0x0423, { 0x11, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e } }, /* U */
  { 0x0424, { 0x04, 0x0e, 0x15, 0x15, 0x15, 0x0e, 0x04 } }, /* Ef */
  { 0x0426, { 0x12, 0x12, 0x12, 0x12, 0x12, 0x1f, 0x01 } }, /* Tse */
  { 0x0427, { 0x11, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x01 } }, /* Che */
  { 0x0428, { 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x1f } }, /* Sha */
  { 0x0429, { 0x15, 0x15, 0x15, 0x15, 0x15, 0x1f, 0x01 } }, /* Shcha */
  { 0x042a, { 0x18, 0x08, 0x08, 0x0e, 0x09, 0x09, 0x0e } }, /* hard sign */
  { 0x042b, { 0x11, 0x11, 0x11, 0x19, 0x15, 0x15, 0x19 } }, /* Yeru */
  { 0x042c, { 0x10, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x1e } }, /* soft sign */
  { 0x042d, { 0x0e, 0x11, 0x01, 0x07, 0x01, 0x11, 0x0e } }, /* E */
  { 0x042e, { 0x12, 0x15, 0x15, 0x1d, 0x15, 0x15, 0x12 } }, /* Yu */
  { 0x042f, { 0x0f, 0x11, 0x11, 0x0f, 0x05, 0x09, 0x11 } }, /* Ya */
};
/* Cyrillic capitals drawn with the Latin glyph they share, from U+0410 */
static const char CYR_LATIN[32] = { [0] = 'A', [2] = 'B', [5] = 'E', [10] = 'K', [12] = 'M', [13] = 'H',
                                    [14] = 'O', [16] = 'P', [17] = 'C', [18] = 'T', [21] = 'X' };

/* Reads one UTF-8 code point and advances *s past it. */
static uint32_t utf8_next(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  uint32_t c = p[0];
  int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
  if (n > 1) {
    c &= 0x3f >> (n - 1);
    for (int i = 1; i < n; i++) {
      if ((p[i] & 0xc0) != 0x80) { n = i; c = '?'; break; }
      c = (c << 6) | (p[i] & 0x3f);
    }
  }
  *s += n;
  return c;
}

static const uint8_t *glyph(uint32_t u) {
  if (u >= 'a' && u <= 'z') u -= 32;
  if (u >= 0xe0 && u <= 0xfe && u != 0xf7) u -= 0x20; /* Latin-1 lower case */
  if (u >= 0x430 && u <= 0x44f) u -= 0x20;           /* Cyrillic lower case */
  if (u == 0x401 || u == 0x451) u = 'E';
  if (u >= 0x410 && u <= 0x42f && CYR_LATIN[u - 0x410]) u = (uint32_t)CYR_LATIN[u - 0x410];
  if (u >= 32 && u <= 95) return FONT5x7[u - 32];
  for (size_t i = 0; i < sizeof FONT_EXT / sizeof FONT_EXT[0]; i++)
    if (FONT_EXT[i].cp == u) return FONT_EXT[i].g;
  return FONT5x7['?' - 32];
}

int text_width(const char *s, int scale) {
  int n = 0;
  while (*s) utf8_next(&s), n++;
  return n ? (n * 6 - 1) * scale : 0;
}

static void draw_glyphs(Framebuffer *fb, const char *s, int x, int y, int scale, uint32_t c) {
  for (; *s; x += 6 * scale) {
    uint32_t u = utf8_next(&s);
    if (u == ' ') continue;
    const uint8_t *g = glyph(u);
    for (int row = 0; row < 7; row++)
      for (int col = 0; col < 5; col++)
        if (g[row] & (0x10 >> col)) fb_rect(fb, x + col * scale, y + row * scale, scale, scale, c);
  }
}

void fb_text(Framebuffer *fb, const char *s, float x, float y, int scale, uint32_t c, int align, int shadow) {
  if (scale < 1) scale = 1;
  int w = text_width(s, scale);
  int ix = (int)floorf(x + 0.5f) - (align == ALIGN_CENTER ? w / 2 : align == ALIGN_RIGHT ? w : 0);
  int iy = (int)floorf(y + 0.5f) - (7 * scale) / 2;
  if (shadow) {
    /* JS: rgba(0,0,0,.65) offset by size/8 */
    int gx = ix + scale - 6 * scale, gy = iy + scale;
    for (const char *p = s; *p;) {
      uint32_t u = utf8_next(&p);
      gx += 6 * scale;
      if (u == ' ') continue;
      const uint8_t *g = glyph(u);
      for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
          if (g[row] & (0x10 >> col)) fb_rect_a(fb, gx + col * scale, gy + row * scale, scale, scale, 0xFF000000u, 0.65f);
    }
  }
  draw_glyphs(fb, s, ix, iy, scale, c);
}

void fb_test_pattern(Framebuffer *fb, uint32_t tick) {
  /* The JS sky and ground colours, then C64-ish bars. */
  static const uint32_t bars[] = {
    0xFF3D5CC0, 0xFFB6CCE6, 0xFF6B984B, 0xFF557D38,
    0xFFE6E9F0, 0xFF7A5A34, 0xFFC04040, 0xFFE0C040,
  };
  const int nbars = (int)(sizeof bars / sizeof bars[0]);
  int top = fb->h / 2;

  for (int i = 0; i < nbars; i++) {
    int x0 = fb->w * i / nbars, x1 = fb->w * (i + 1) / nbars;
    fb_rect(fb, x0, 0, x1 - x0, top, bars[i]);
  }
  /* 1-pixel checkerboard: any blur from scaling turns it grey. */
  for (int y = top; y < fb->h; y++) {
    uint32_t *row = fb->px + (size_t)y * fb->w;
    for (int x = 0; x < fb->w; x++)
      row[x] = ((x ^ y) & 1) ? 0xFF101010 : 0xFFF0F0F0;
  }
  /* A bar moving at one pixel per 60 Hz tick shows the fixed-step loop. */
  int bx = (int)(tick % (uint32_t)fb->w);
  fb_rect(fb, bx, top - 8, 8, 16, 0xFFFF00FF);
}
