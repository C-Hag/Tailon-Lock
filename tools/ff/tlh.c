/* tlh.c - the floppy forge's terrain heights packer (TLH, src/ff/tlh_model.h), and its tests. Original code for this
 * project; C99 and the standard library.
 *
 *   tlh c W H IN.h16 OUT      pack a W x H grid of 16-bit samples
 *   tlh t W H FILE...         pack, unpack with the game's code, compare; one line each
 *   tlh fuzz N [SEED]         made-up grids (smooth hills, cliffs, noise, flat, extremes) round trip, and damaged
 *                             files must fail cleanly */
#define TLPK_ENCODER
#include "ff/tlh_model.h"

#include <stdio.h>

#include "ff/tlh.h"

static uint8_t *pack(const int16_t *src, int w, int h, size_t *outn) {
  size_t ns = (size_t)w * (size_t)h;
  int16_t *s = malloc(ns * sizeof *s);
  memcpy(s, src, ns * sizeof *s);
  uint8_t *le = malloc(ns * 2);
  for (size_t i = 0; i < ns; i++) le[2 * i] = (uint8_t)(src[i] & 255), le[2 * i + 1] = (uint8_t)((uint16_t)src[i] >> 8);
  Rc r;
  rc_init(&r);
  for (int i = 0; i < TLH_HEADER; i++) rc_put(&r, 0);
  tlh_grid(&r, w, h, s);
  rc_flush(&r);
  memcpy(r.out, "TLH1", 4);
  r.out[4] = (uint8_t)w, r.out[5] = (uint8_t)(w >> 8), r.out[6] = (uint8_t)h, r.out[7] = (uint8_t)(h >> 8);
  wr32(r.out + 8, crc32_of(le, ns * 2));
  free(s), free(le);
  *outn = r.n;
  return r.out;
}

static int16_t *load_h16(const char *path, int w, int h) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t ns = (size_t)w * (size_t)h;
  uint8_t *b = malloc(ns * 2);
  int16_t *s = malloc(ns * sizeof *s);
  bool ok = fread(b, 1, ns * 2, f) == ns * 2 && fgetc(f) == EOF;
  fclose(f);
  for (size_t i = 0; ok && i < ns; i++) s[i] = (int16_t)(b[2 * i] | b[2 * i + 1] << 8);
  free(b);
  if (!ok) free(s);
  return ok ? s : NULL;
}

static uint32_t rs = 1;
static uint32_t rnd(void) { return rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5; }

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "fuzz")) {
    long iters = argc > 2 ? strtol(argv[2], NULL, 10) : 100, bad = 0, dam = 0, caught = 0;
    rs = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) | 1 : 99;
    for (long it = 0; it < iters; it++) {
      int w = 1 + (int)(rnd() % 160), h = 1 + (int)(rnd() % 160), kind = (int)(rnd() % 5);
      int16_t *s = malloc(sizeof *s * (size_t)w * (size_t)h);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          int v;
          switch (kind) {
          case 0: v = (int)(800 * __builtin_sin(x * 0.07) * __builtin_cos(y * 0.05)) + (int)(rnd() % 7); break;
          case 1: v = (x + y) % 37 < 18 ? 3000 : -200; break;
          case 2: v = (int)(rnd() & 0xFFFF) - 32768; break;
          case 3: v = 42; break;
          default: v = (x ^ y) & 1 ? 32767 : -32768; break;
          }
          s[y * w + x] = (int16_t)v;
        }
      size_t pn, un = 0;
      uint8_t *p = pack(s, w, h, &pn), *u = tlh_unpack(p, pn, &un);
      bool ok = u && un == (size_t)w * h * 2;
      for (int i = 0; ok && i < w * h; i++) ok = (int16_t)(u[2 * i] | u[2 * i + 1] << 8) == s[i];
      bad += !ok;
      free(u);
      for (int t = 0; t < 3 && pn > TLH_HEADER + 1; t++) {
        uint8_t *q = malloc(pn);
        memcpy(q, p, pn);
        size_t qn = t == 2 ? TLH_HEADER + rnd() % (pn - TLH_HEADER) : pn;
        if (t < 2) q[TLH_HEADER + rnd() % (pn - TLH_HEADER)] ^= (uint8_t)(1 + rnd() % 255);
        size_t dn;
        uint8_t *d = tlh_unpack(q, qn, &dn);
        bool same = d && dn == (size_t)w * h * 2;
        for (int i = 0; same && i < w * h; i++) same = (int16_t)(d[2 * i] | d[2 * i + 1] << 8) == s[i];
        dam++, caught += !d || same;
        free(d), free(q);
      }
      free(p), free(s);
    }
    printf("fuzz: %ld grids, %ld round trips failed; %ld damaged files, %ld failed cleanly or came back right\n", iters, bad,
           dam, caught);
    return bad || caught != dam;
  }
  if (argc >= 5 && (!strcmp(argv[1], "c") || !strcmp(argv[1], "t"))) {
    int w = atoi(argv[2]), h = atoi(argv[3]), fails = 0;
    for (int i = 4; i < argc - (argv[1][0] == 'c'); i++) {
      int16_t *s = load_h16(argv[i], w, h);
      if (!s) {
        fprintf(stderr, "tlh: %s is not a %d x %d grid of 16-bit samples\n", argv[i], w, h);
        return 2;
      }
      size_t pn, un = 0;
      uint8_t *p = pack(s, w, h, &pn), *u = tlh_unpack(p, pn, &un);
      bool ok = u && un == (size_t)w * h * 2;
      for (int k = 0; ok && k < w * h; k++) ok = (int16_t)(u[2 * k] | u[2 * k + 1] << 8) == s[k];
      fails += !ok;
      if (argv[1][0] == 'c') {
        FILE *f = fopen(argv[argc - 1], "wb");
        if (!f || fwrite(p, 1, pn, f) != pn) return 2;
        fclose(f);
      }
      printf("%s %zu %zu %s\n", ok ? "ok" : "FAIL", (size_t)w * h * 2, pn, argv[i]);
      free(s), free(p), free(u);
    }
    return fails ? 1 : 0;
  }
  fprintf(stderr, "usage: tlh c W H IN.h16 OUT | t W H FILE... | fuzz N [SEED]\n");
  return 2;
}
