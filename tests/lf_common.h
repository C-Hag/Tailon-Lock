/* lf_common.h - what the libFuzzer targets (tests/lf_*.c) share: a folder of
 * their own for the files they write (and TL_CONFIG_DIR pointing into it, so
 * a run never reads or writes the player's settings), a file written whole,
 * and, unless the target links the real plat_core.c (LF_PLAT_CORE), a
 * stand-in for its log and clock. Original code for this project. */
#ifndef TL_LF_COMMON_H
#define TL_LF_COMMON_H

#define _POSIX_C_SOURCE 200809L /* mkdtemp, setenv */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* macOS declares mkdtemp only with its own calls */
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char lf_dir[512];

/* Makes the folder (once) with the given subfolders, and points TL_CONFIG_DIR at it. */
static inline const char *lf_folder(const char *const *subs) {
  if (lf_dir[0]) return lf_dir;
  const char *t = getenv("TMPDIR");
  snprintf(lf_dir, sizeof lf_dir, "%s/a4c-lf-XXXXXX", t && *t ? t : "/tmp");
  if (!mkdtemp(lf_dir)) {
    perror("lf: mkdtemp");
    abort();
  }
  setenv("TL_CONFIG_DIR", lf_dir, 1);
  for (int i = 0; subs && subs[i]; i++) {
    char p[600];
    snprintf(p, sizeof p, "%s/%s", lf_dir, subs[i]);
    if (mkdir(p, 0700) != 0) {
      perror("lf: mkdir");
      abort();
    }
  }
  return lf_dir;
}

/* rel in the folder, written with exactly these bytes */
static inline void lf_write(const char *rel, const void *data, size_t n) {
  char p[700];
  snprintf(p, sizeof p, "%s/%s", lf_dir, rel);
  FILE *f = fopen(p, "wb");
  if (!f || (n && fwrite(data, 1, n, f) != n) || fclose(f) != 0) {
    perror("lf: write");
    abort();
  }
}

#ifndef LF_PLAT_CORE
#include "platform/plat.h"
static uint64_t lf_clock;
void plat_logv(const char *fmt, va_list ap) {
  char line[512];
  vsnprintf(line, sizeof line, fmt, ap); /* formatted (a bad argument shows), then dropped */
}
void plat_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  plat_logv(fmt, ap);
  va_end(ap);
}
uint64_t plat_ms(void) { return lf_clock += 16; }
const char *plat_error(void) { return ""; }
#endif

#endif
