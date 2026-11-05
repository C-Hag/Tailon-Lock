/* tlj.c - TLJ (see tools/ff/tlj for the format) back into JSON text. Numbers come out as the text that parses to the
 * very double the encoder checked: whole numbers as they are, decimals as the mantissa with its point put back (the
 * game's strtod rounds "m with k decimals" exactly as the encoder's m / 10^k did), the rest with 17 digits. Every read
 * is checked against the end and nesting is held to the JSON parser's own limit, so damaged bytes give NULL.
 * Original code for this project. */
#include "ff/tlj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const uint8_t *p, *end;
  const uint8_t **str; /* the dictionary: each string's bytes */
  uint32_t *slen, nstr;
  char *out;
  size_t n, cap;
  bool bad;
} D;

static void put(D *d, const char *s, size_t n) {
  if (d->bad) return;
  if (d->n + n + 1 > d->cap) {
    size_t c = d->cap ? d->cap : 65536;
    while (d->n + n + 1 > c) c *= 2;
    char *o = realloc(d->out, c);
    if (!o) {
      d->bad = true;
      return;
    }
    d->out = o, d->cap = c;
  }
  memcpy(d->out + d->n, s, n);
  d->n += n;
}
static void putc_(D *d, char c) { put(d, &c, 1); }

static uint64_t uvar(D *d) {
  uint64_t v = 0;
  for (int s = 0; s < 64; s += 7) {
    if (d->p >= d->end) {
      d->bad = true;
      return 0;
    }
    uint8_t b = *d->p++;
    v |= (uint64_t)(b & 0x7F) << s;
    if (!(b & 0x80)) return v;
  }
  d->bad = true;
  return 0;
}
static int64_t svar(D *d) {
  uint64_t u = uvar(d);
  return (u & 1) ? -(int64_t)(u >> 1) - 1 : (int64_t)(u >> 1);
}
static int byte(D *d) {
  if (d->p >= d->end) {
    d->bad = true;
    return 0;
  }
  return *d->p++;
}

/* m with k decimals, as text: 12345 and 3 give 12.345, 5 and 3 give 0.005 */
static void number(D *d, int64_t m, int k) {
  char dig[24], buf[48];
  uint64_t a = m < 0 ? (uint64_t)(-(m + 1)) + 1 : (uint64_t)m;
  int nd = snprintf(dig, sizeof dig, "%llu", (unsigned long long)a), o = 0;
  if (m < 0) buf[o++] = '-';
  if (k <= 0 || k > 17) {
    memcpy(buf + o, dig, (size_t)nd), o += nd;
  } else if (nd <= k) {
    buf[o++] = '0', buf[o++] = '.';
    for (int i = 0; i < k - nd; i++) buf[o++] = '0';
    memcpy(buf + o, dig, (size_t)nd), o += nd;
  } else {
    memcpy(buf + o, dig, (size_t)(nd - k)), o += nd - k;
    buf[o++] = '.';
    memcpy(buf + o, dig + nd - k, (size_t)k), o += k;
  }
  put(d, buf, (size_t)o);
}

/* Is s[0..n) UTF-8 (RFC 3629: no overlong forms, no surrogates, nothing past U+10FFFF), as JSON text must be? */
static bool utf8_ok(const uint8_t *s, size_t n) {
  for (size_t i = 0; i < n;) {
    uint8_t c = s[i];
    size_t len = c < 0x80 ? 1 : c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
    if (!len || len > n - i) return false;
    for (size_t k = 1; k < len; k++)
      if ((s[i + k] & 0xC0) != 0x80) return false;
    if ((c == 0xE0 && s[i + 1] < 0xA0) || (c == 0xED && s[i + 1] > 0x9F) || (c == 0xF0 && s[i + 1] < 0x90) ||
        (c == 0xF4 && s[i + 1] > 0x8F))
      return false; /* overlong, a surrogate, or past U+10FFFF */
    i += len;
  }
  return true;
}

static void string(D *d, uint64_t i) {
  if (i >= d->nstr) {
    d->bad = true;
    return;
  }
  const uint8_t *s = d->str[i];
  putc_(d, '"');
  for (uint32_t j = 0; j < d->slen[i]; j++) {
    uint8_t c = s[j];
    if (c == '"' || c == '\\') {
      char e[2] = { '\\', (char)c };
      put(d, e, 2);
    } else if (c < 0x20) {
      char e[8];
      snprintf(e, sizeof e, "\\u%04x", c);
      put(d, e, 6);
    } else {
      putc_(d, (char)c);
    }
  }
  putc_(d, '"');
}

static void column(D *d, uint64_t n, int64_t *out) {
  int k = byte(d), mode = byte(d);
  uint64_t prev = 0; /* the running sum in unsigned arithmetic: a damaged file's deltas wrap instead of overflowing */
  for (uint64_t i = 0; i < n && !d->bad; i++) {
    int64_t v = svar(d);
    out[i] = mode ? (int64_t)(prev += (uint64_t)v) : v;
  }
  out[n] = k; /* the decimals ride after the values */
}

static void value(D *d, int depth) {
  if (d->bad || depth > 1000) {
    d->bad = true;
    return;
  }
  int t = byte(d);
  switch (t) {
  case 0: put(d, "null", 4); break;
  case 1: put(d, "false", 5); break;
  case 2: put(d, "true", 4); break;
  case 3: number(d, svar(d), 0); break;
  case 4: {
    int k = byte(d);
    number(d, svar(d), k);
    break;
  }
  case 5: {
    if (d->end - d->p < 8) {
      d->bad = true;
      return;
    }
    double v;
    uint8_t le[8];
    memcpy(le, d->p, 8);
    d->p += 8;
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) u = u << 8 | le[i];
    memcpy(&v, &u, 8);
    char buf[40];
    int n = snprintf(buf, sizeof buf, "%.17g", v);
    if (!strpbrk(buf, ".eEn")) buf[n++] = '.', buf[n++] = '0'; /* a double stays one: -0 is -0.0 */
    put(d, buf, (size_t)n);
    break;
  }
  case 6: string(d, uvar(d)); break;
  case 7:
  case 8: {
    uint64_t n = uvar(d);
    if (n > (uint64_t)(d->end - d->p)) { /* every member takes at least a byte */
      d->bad = true;
      return;
    }
    putc_(d, t == 7 ? '[' : '{');
    for (uint64_t i = 0; i < n && !d->bad; i++) {
      if (i) putc_(d, ',');
      if (t == 8) {
        string(d, uvar(d));
        putc_(d, ':');
      }
      value(d, depth + 1);
    }
    putc_(d, t == 7 ? ']' : '}');
    break;
  }
  case 9:
  case 10: {
    uint64_t n = uvar(d), r = t == 10 ? uvar(d) : 1;
    if (n > (uint64_t)(d->end - d->p) || r == 0 || r > 64 || (n && n * r > (uint64_t)(d->end - d->p))) {
      d->bad = true;
      return;
    }
    int64_t *cols = calloc((size_t)(n + 1) * (size_t)r, sizeof(int64_t)); /* zeroed: a column cut short is never read unset */
    if (!cols) {
      d->bad = true;
      return;
    }
    for (uint64_t c = 0; c < r && !d->bad; c++) column(d, n, cols + c * (n + 1));
    putc_(d, '[');
    for (uint64_t i = 0; i < n && !d->bad; i++) {
      if (i) putc_(d, ',');
      if (t == 10) putc_(d, '[');
      for (uint64_t c = 0; c < r; c++) {
        if (c) putc_(d, ',');
        number(d, cols[c * (n + 1) + i], (int)cols[c * (n + 1) + n]);
      }
      if (t == 10) putc_(d, ']');
    }
    putc_(d, ']');
    free(cols);
    break;
  }
  default: d->bad = true;
  }
}

bool tlj_is(const uint8_t *in, size_t n) { return n >= 4 && !memcmp(in, "TLJ1", 4); }

char *tlj_to_text(const uint8_t *in, size_t n, size_t *size) {
  if (!tlj_is(in, n)) return NULL;
  D d = { in + 4, in + n, NULL, NULL, 0, NULL, 0, 0, false };
  uint64_t ns = uvar(&d);
  if (d.bad || ns > n) return NULL;
  d.str = (const uint8_t **)malloc(sizeof *d.str * (size_t)(ns + 1));
  d.slen = malloc(sizeof *d.slen * (size_t)(ns + 1));
  d.nstr = (uint32_t)ns;
  for (uint64_t i = 0; i < ns && d.str && d.slen; i++) {
    uint64_t l = uvar(&d);
    if (d.bad || l > (uint64_t)(d.end - d.p)) {
      d.bad = true;
      break;
    }
    if (!utf8_ok(d.p, (size_t)l)) { /* the forge writes only JSON's strings: anything else is damage */
      d.bad = true;
      break;
    }
    d.str[i] = d.p, d.slen[i] = (uint32_t)l;
    d.p += l;
  }
  if (!d.str || !d.slen) d.bad = true;
  if (!d.bad) value(&d, 0);
  if (!d.bad && d.p != d.end) d.bad = true; /* nothing may follow the value */
  free((void *)d.str), free(d.slen);
  if (d.bad) {
    free(d.out);
    return NULL;
  }
  put(&d, "", 0);
  if (d.bad) {
    free(d.out);
    return NULL;
  }
  d.out[d.n] = 0;
  if (size) *size = d.n;
  return d.out;
}
