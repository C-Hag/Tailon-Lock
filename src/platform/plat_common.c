/* plat_common.c - the parts of plat.h every platform does the same way, on
 * the C library: reading a whole file. Original code for this project. */
#include "platform/plat.h"

#include <stdio.h>
#include <stdlib.h>

void *plat_load_file(const char *path, size_t *size) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return NULL;
  char *buf = NULL;
  size_t len = 0, cap = 0;
  for (;;) { /* in chunks: a pipe or a special file has no size up front */
    if (len + 65536 + 1 > cap) {
      size_t ncap = cap ? cap * 2 : 65536 * 2;
      char *nb = realloc(buf, ncap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb, cap = ncap;
    }
    size_t got = fread(buf + len, 1, 65536, f);
    len += got;
    if (got < 65536) break;
  }
  bool bad = ferror(f);
  fclose(f);
  if (bad) {
    free(buf);
    return NULL;
  }
  buf[len] = 0;
  if (size) *size = len;
  return buf;
}
