/* safe.h - the Safe C helpers (docs/SAFE_C.md): numbers read from text with every failure seen, a tokenizer that
 * keeps no hidden state, and size arithmetic that says when it overflows. C11 only, header only. Original code for
 * this project. */
#ifndef TL_SAFE_H
#define TL_SAFE_H

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The number at the start of s (base 10, leading blanks and a sign as strtol takes them), held to lo..hi: what
 * atoi read, but text with no number gives def instead of 0 by accident, and a number too big for a long is held
 * to the range instead of being undefined. What follows the number is ignored, as atoi does. */
static inline long safe_int(const char *s, long def, long lo, long hi) {
  if (!s) return def;
  char *end;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (end == s) return def;
  if (errno == ERANGE) v = v < 0 ? lo : hi;
  return v < lo ? lo : v > hi ? hi : v;
}

/* The same for a decimal number (strtod): def when there is no number, and never infinity or NaN. */
static inline double safe_num(const char *s, double def) {
  if (!s) return def;
  char *end;
  errno = 0;
  double v = strtod(s, &end);
  if (end == s || !isfinite(v)) return def;
  return v;
}

/* The next token of *cursor, split at any of delims, or NULL at the end. The string is changed in place (each
 * token ends with a 0) and *cursor moves on; unlike strtok there is no state shared between calls or threads.
 * Empty tokens between two delimiters are skipped, as strtok skips them. */
static inline char *safe_token(char **cursor, const char *delims) {
  char *s = *cursor;
  if (!s) return NULL;
  s += strspn(s, delims);
  if (!*s) {
    *cursor = NULL;
    return NULL;
  }
  char *e = s + strcspn(s, delims);
  if (*e) *e++ = 0;
  *cursor = e;
  return s;
}

/* printf-style checking of a function's format string and its arguments (f: the format's place, a: the first
 * argument's, 0 for a va_list), and a switch case that falls through on purpose */
#if defined(__GNUC__) || defined(__clang__)
#define TL_PRINTF(f, a) __attribute__((format(printf, f, a)))
#define TL_FALLTHROUGH __attribute__((fallthrough))
#else
#define TL_PRINTF(f, a)
#define TL_FALLTHROUGH ((void)0)
#endif

/* a + b and a * b on sizes: false (and *out untouched) when the result would not fit */
static inline bool safe_add(size_t a, size_t b, size_t *out) {
  if (a > SIZE_MAX - b) return false;
  *out = a + b;
  return true;
}
static inline bool safe_mul(size_t a, size_t b, size_t *out) {
  if (a && b > SIZE_MAX / a) return false;
  *out = a * b;
  return true;
}

/* src copied into dst (size bytes, size > 0), always ended with 0; text too long for it is cut before the
 * character that does not fit whole, never inside one (half a UTF-8 character is text no reader takes back) */
static inline void safe_text(char *dst, size_t size, const char *src) {
  size_t n = 0;
  while (n < size - 1 && src[n]) n++;
  if (src[n]) { /* cut: step back to the first byte of the last character and keep it only if it is whole */
    size_t i = n;
    while (i > 0 && ((unsigned char)src[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) {
      unsigned char lead = (unsigned char)src[i - 1];
      size_t len = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      if (n - (i - 1) < len) n = i - 1;
    } else n = 0;
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

#endif
