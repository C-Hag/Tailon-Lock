/* crypto_tool.c - runs our own cryptography (src/core/crypto.c) on inputs
 * read from stdin, one request a line, for cross-checks against independent
 * implementations (tools/crypto_crosscheck.py) and for golden outputs.
 * Original code for this project.
 *
 * Every line is a command and hex arguments ("-" for an empty one); the
 * answer is one line of hex:
 *   x25519 SCALAR U            -> scalar * u
 *   blake2b OUTLEN KEY MSG     -> the hash (OUTLEN in decimal)
 *   poly1305 KEY MSG           -> the tag
 *   chacha KEY COUNTER NONCE   -> one block (COUNTER in decimal)
 *   seal KEY NONCE AD PLAIN    -> cipher text then tag
 *   open KEY NONCE AD CT+TAG   -> the plain text, or "refused"
 *   elligator IN               -> the point's u coordinate */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/crypto.h"
#include "core/safe.h"

static size_t unhex(const char *s, uint8_t *out, size_t cap) {
  if (!strcmp(s, "-")) return 0;
  size_t n = strlen(s) / 2;
  if (n > cap) n = cap;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
    out[i] = (uint8_t)v;
  }
  return n;
}

static void hex(const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
}

int main(void) {
  static char line[1 << 17];
  static uint8_t a[1 << 15], b[1 << 15], c[1 << 15], d[1 << 15], out[1 << 15];
  while (fgets(line, sizeof line, stdin)) {
    char cmd[16], s1[1 << 16], s2[1 << 16], s3[1 << 16], s4[1 << 16];
    s1[0] = s2[0] = s3[0] = s4[0] = 0;
    int k = sscanf(line, "%15s %65535s %65535s %65535s %65535s", cmd, s1, s2, s3, s4);
    if (k < 2) continue;
    if (!strcmp(cmd, "x25519")) {
      unhex(s1, a, 32);
      unhex(s2, b, 32);
      cr_x25519(out, a, b);
      hex(out, 32);
    } else if (!strcmp(cmd, "blake2b")) {
      size_t ol = (size_t)(int)safe_int(s1, 0, INT_MIN, INT_MAX), kl = unhex(s2, a, 64), ml = unhex(s3, b, sizeof b);
      cr_blake2b(out, ol, kl ? a : NULL, kl, b, ml);
      hex(out, ol);
    } else if (!strcmp(cmd, "poly1305")) {
      unhex(s1, a, 32);
      size_t ml = unhex(s2, b, sizeof b);
      cr_poly1305(out, b, ml, a);
      hex(out, 16);
    } else if (!strcmp(cmd, "chacha")) {
      unhex(s1, a, 32);
      unhex(s3, b, 12);
      cr_chacha20_block(out, a, (uint32_t)strtoul(s2, NULL, 10), b);
      hex(out, 64);
    } else if (!strcmp(cmd, "seal")) {
      unhex(s1, a, 32);
      unhex(s2, b, 12);
      size_t al = unhex(s3, c, sizeof c), pl = unhex(s4, d, sizeof d);
      cr_aead_seal(out, out + pl, a, b, c, al, d, pl);
      hex(out, pl + 16);
    } else if (!strcmp(cmd, "open")) {
      unhex(s1, a, 32);
      unhex(s2, b, 12);
      size_t al = unhex(s3, c, sizeof c), cl = unhex(s4, d, sizeof d);
      if (cl < 16 || !cr_aead_open(out, d + cl - 16, a, b, c, al, d, cl - 16)) printf("refused");
      else hex(out, cl - 16);
    } else if (!strcmp(cmd, "elligator")) {
      unhex(s1, a, 32);
      cr_elligator2(out, a);
      hex(out, 32);
    } else printf("unknown");
    printf("\n");
  }
  return 0;
}
