/* test_paint.c - the vector painter (src/paint.c) on its own: a plain square fills, and paths whose every point is
 * not a number fill nothing (their box stays empty and is never cast to int). Original code for this project. */
#include "paint.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void fb_text(Framebuffer *fb, const char *s, float x, float y, int scale, uint32_t c, int align, int shadow) {
  (void)fb, (void)s, (void)x, (void)y, (void)scale, (void)c, (void)align, (void)shadow; /* paint.c's only outside call */
}

static int fails;

static int drawn(const Framebuffer *fb) {
  int n = 0;
  for (int i = 0; i < fb->w * fb->h; i++) n += fb->px[i] != 0;
  return n;
}

static void check(const char *what, int got, int lo, int hi) {
  int bad = got < lo || got > hi;
  printf("  %-34s %5d pixels%s\n", what, got, bad ? "  OUT" : "");
  fails += bad;
}

int main(void) {
  static uint32_t px[64 * 64];
  Framebuffer fb = { px, NULL, 64, 64, 0, 0, 64, 64, 0, 0, 0, 0 };
  Painter *p = painter_new(&fb);
  if (!p) return 1;
  Paint white = p_rgba(1, 1, 1, 1);
  p_begin(p);
  p_rect(p, 8, 8, 16, 16);
  p_fill(p, &white);
  check("a 16 x 16 square", drawn(&fb), 240, 290);
  for (int i = 0; i < 64 * 64; i++) px[i] = 0;
  p_begin(p);
  p_move(p, NAN, NAN);
  p_line(p, NAN, NAN);
  p_line(p, NAN, NAN);
  p_close(p);
  p_fill(p, &white);
  check("a path of nothing but NaN", drawn(&fb), 0, 0);
  painter_free(p);
  printf("test_paint: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
