/* tlj_dec.c - tools/ff/tlj's check: a TLJ file back to JSON text on stdout, through the game's own decoder
 * (src/ff/tlj.c). Original code for this project. */
#include <stdio.h>
#include <stdlib.h>

#include "ff/tlj.h"

int main(int argc, char **argv) {
  if (argc != 2) return fprintf(stderr, "usage: tlj_dec FILE.tlj\n"), 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return fprintf(stderr, "tlj_dec: cannot read %s\n", argv[1]), 2;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc((size_t)n);
  size_t got = b ? fread(b, 1, (size_t)n, f) : 0;
  fclose(f);
  size_t len;
  char *t = got == (size_t)n ? tlj_to_text(b, (size_t)n, &len) : NULL;
  if (!t) return fprintf(stderr, "tlj_dec: %s is not a sound TLJ file\n", argv[1]), 1;
  fwrite(t, 1, len, stdout);
  free(t), free(b);
  return 0;
}
