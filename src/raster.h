/* raster.h - the software framebuffer and everything that draws into it.
 * Replaces the JS Canvas 2D fills and lines. No platform calls in here: pixels are
 * packed 0xAARRGGBB (shown as 0xXXRRGGBB by plat_present). Every
 * primitive clips to the current clip rectangle, and optionally to a circle
 * (the radar scope and attitude ball). */
#ifndef TL_RASTER_H
#define TL_RASTER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t *px;
  float    *zb; /* depth as 1/z per pixel; 0 is infinitely far */
  int w, h;
  int cx0, cy0, cx1, cy1;   /* clip rectangle, x1/y1 exclusive */
  int circ;                 /* circle clip on? */
  float ccx, ccy, cr2;
} Framebuffer;

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
/* 0xRRGGBB -> pixel */
static inline uint32_t hexc(uint32_t c) { return 0xFF000000u | c; }

/* (Re)allocates when the size changes. Returns 0 on failure. */
int  fb_resize(Framebuffer *fb, int w, int h);
/* src, k times smaller than dst, scaled up onto dst with bilinear filtering (opaque) */
void fb_scale_up(Framebuffer *dst, const Framebuffer *src, int k);
void fb_free(Framebuffer *fb);
void fb_clear(Framebuffer *fb, uint32_t c);

void fb_clip(Framebuffer *fb, int x, int y, int w, int h);
void fb_clip_reset(Framebuffer *fb);
void fb_clip_circle(Framebuffer *fb, float cx, float cy, float r);
void fb_clip_circle_off(Framebuffer *fb);

/* alpha in [0, 1] */
void fb_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t c);
void fb_rect_a(Framebuffer *fb, int x, int y, int w, int h, uint32_t c, float a);
void fb_rect_stroke(Framebuffer *fb, float x, float y, float w, float h, uint32_t c, float lw);
/* Convex polygon, xy holds n (x, y) pairs. grow widens the edges a little,
 * like the 0.6 px stroke the JS draws round each face. */
void fb_poly(Framebuffer *fb, const float *xy, int n, uint32_t c, float a, float grow);
void fb_line(Framebuffer *fb, float x0, float y0, float x1, float y1, uint32_t c, float w, float a);
void fb_circle(Framebuffer *fb, float cx, float cy, float r, uint32_t c);
void fb_ring(Framebuffer *fb, float cx, float cy, float r, uint32_t c, float lw);

/* ---- depth-tested drawing (w = 1/z, larger is nearer) ---------------- */
void fb_clear_depth(Framebuffer *fb, int x, int y, int w, int h);
/* Convex polygon with a 1/z per vertex; writes depth when zwrite. bias
 * scales w (above 1 pulls the polygon toward the camera, for decals). */
void fb_poly_z(Framebuffer *fb, const float *xy, const float *w, int n, uint32_t c, float a, float grow, int zwrite,
               float bias);
/* Square dot and line at one depth, tested but not written. */
void fb_rect_z(Framebuffer *fb, int x, int y, int w, int h, uint32_t c, float a, float wz);
void fb_line_z(Framebuffer *fb, float x0, float y0, float x1, float y1, uint32_t c, float lw, float a, float wz);
/* Filled disc at one depth (cloud puffs, glows), tested, not written. */
void fb_disc_z(Framebuffer *fb, float cx, float cy, float r, uint32_t c, float a, float wz);
/* The same with a soft edge (alpha 1 in the middle, 0.75 at 45 % out, 0 at the rim), like the GPU's puff sprite. */
void fb_disc_soft_z(Framebuffer *fb, float cx, float cy, float r, uint32_t c, float a, float wz);

/* One pixel, mixed with alpha in [0, 1]. */
void fb_blend_px(Framebuffer *fb, int x, int y, uint32_t c, float a);

/* Bitmap text: 5x7 glyphs in a 6x8 cell, times scale. Lower case is drawn
 * as upper case. y is the vertical middle, as the JS textBaseline. shadow: 1 a drop shadow, 2 a dark outline (the
 * HUD's, over a bright sky). */
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };
/* s cut after its first chars characters (UTF-8: a letter is never cut in half); size is s's buffer */
void utf8_cut(char *s, size_t size, int chars);
int  text_width(const char *s, int scale);
void fb_text(Framebuffer *fb, const char *s, float x, float y, int scale, uint32_t c, int align, int shadow);
/* Tests (the audit's views): told of every text drawn, its box (x1/y1 exclusive) and its ink. NULL: off. */
extern void (*fb_text_hook)(const Framebuffer *fb, const char *s, int x0, int y0, int x1, int y1, uint32_t ink);

/* Milestone 1 check, kept for --test-pattern. */
void fb_test_pattern(Framebuffer *fb, uint32_t tick);

#endif
