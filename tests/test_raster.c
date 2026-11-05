/* test_raster.c - the software rasteriser's shapes with sizes from a broken view: a circle or ring of a huge, an
 * infinite or a NaN radius comes back at once (its loops are held to the picture), and a normal one draws as
 * before. Original code for this project. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "raster.h"

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_raster:%d: %s\n", __LINE__, #c);           \
      fails++;                                                         \
    }                                                                  \
  } while (0)

int main(void) {
  static Framebuffer fb;
  CHECK(fb_resize(&fb, 320, 200));
  fb_clear(&fb, 0);
  fb_circle(&fb, 160, 100, 10, 0xffffff);
  int lit = 0;
  for (int i = 0; i < 320 * 200; i++) lit += (fb.px[i] & 0xffffff) != 0;
  CHECK(lit > 280 && lit < 350); /* about pi r^2 */
  clock_t t0 = clock();
  const float R[] = { 1e9f, 1e30f, INFINITY, NAN, -1e30f };
  for (int i = 0; i < 5; i++) {
    fb_circle(&fb, 160, 100, R[i], 0xff0000);
    fb_ring(&fb, 160, 100, R[i], 0x00ff00, 2);
  }
  double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
  CHECK(secs < 1);
  fb_free(&fb);
  /* the typewriter's cut: characters, not bytes */
  char t[32];
  snprintf(t, sizeof t, "%s", "BL\xc3\x85 \xce\xb1" "B");
  utf8_cut(t, sizeof t, 3);
  CHECK(!strcmp(t, "BL\xc3\x85"));
  snprintf(t, sizeof t, "%s", "AB");
  utf8_cut(t, sizeof t, 9);
  CHECK(!strcmp(t, "AB"));
  utf8_cut(t, sizeof t, 0);
  CHECK(t[0] == 0);
  if (fails) fprintf(stderr, "test_raster: %d failed\n", fails);
  else printf("test_raster: ok (%.3f s)\n", secs);
  return fails != 0;
}
