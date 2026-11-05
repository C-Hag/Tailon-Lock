/* unpack.c - tools/ff/forge's check: one packed asset back through the codecs the game's asset_load (src/assets.c)
 * uses, in the same order (TLPK, TLH, then TLJ to JSON text), to a file. Original code for this project. */
#include <stdio.h>
#include <stdlib.h>

#include "ff/tlh.h"
#include "ff/tlj.h"
#include "ff/tlpk.h"

int main(int argc, char **argv) {
  if (argc != 3) return fprintf(stderr, "usage: unpack IN OUT\n"), 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)n + 1);
  size_t len = b ? fread(b, 1, (size_t)n, f) : 0;
  fclose(f);
  if (b && tlpk_is(b, len)) {
    size_t u;
    uint8_t *x = tlpk_unpack(b, len, (size_t)64 << 20, &u);
    free(b), b = x, len = u;
  }
  if (b && tlh_is(b, len)) {
    size_t u;
    uint8_t *x = tlh_unpack(b, len, &u);
    free(b), b = x, len = u;
  }
  if (b && tlj_is(b, len)) {
    size_t u;
    char *x = tlj_to_text(b, len, &u);
    free(b), b = (uint8_t *)x, len = u;
  }
  if (!b) return fprintf(stderr, "unpack: %s is damaged\n", argv[1]), 1;
  FILE *o = fopen(argv[2], "wb");
  if (!o || fwrite(b, 1, len, o) != len) return 2;
  fclose(o);
  free(b);
  return 0;
}
