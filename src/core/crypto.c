/* crypto.c - our own cryptography for online play. Original code, written
 * from RFC 7693 (BLAKE2b), RFC 7748 (X25519), RFC 8439 (ChaCha20, Poly1305,
 * AEAD) and RFC 9380 (Elligator 2); no other implementation was looked at.
 * See crypto.h. */
#include "core/crypto.h"

#include <string.h>

void cr_wipe(void *p, size_t n) {
  volatile uint8_t *v = p;
  while (n--) *v++ = 0;
}

bool cr_same32(const uint8_t a[32], const uint8_t b[32]) {
  uint32_t d = 0;
  for (int i = 0; i < 32; i++) d |= (uint32_t)(a[i] ^ b[i]);
  return ((d - 1) >> 31) & 1; /* 1 exactly when d == 0 */
}

static uint32_t ld32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t ld64(const uint8_t *p) { return (uint64_t)ld32(p) | (uint64_t)ld32(p + 4) << 32; }
static void st32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void st64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* ==== BLAKE2b (RFC 7693) ==================================================== */

/* the initialization vector: the same words as SHA-512's (section 2.6) */
static const uint64_t B2_IV[8] = {
  0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
  0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};

/* the message word schedule (section 2.7); round i uses row i mod 10 */
static const uint8_t B2_SIGMA[10][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
  { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 }, { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
  { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 }, { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
  { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 }, { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
  { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 }, { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

static uint64_t rotr64(uint64_t x, int n) { return x >> n | x << (64 - n); }

/* the mixing function G (section 3.1) */
#define B2_G(a, b, c, d, x, y)  \
  do {                          \
    v[a] = v[a] + v[b] + (x);   \
    v[d] = rotr64(v[d] ^ v[a], 32); \
    v[c] = v[c] + v[d];         \
    v[b] = rotr64(v[b] ^ v[c], 24); \
    v[a] = v[a] + v[b] + (y);   \
    v[d] = rotr64(v[d] ^ v[a], 16); \
    v[c] = v[c] + v[d];         \
    v[b] = rotr64(v[b] ^ v[c], 63); \
  } while (0)

/* the compression function F (section 3.2) */
static void b2_compress(CrBlake2b *s, const uint8_t block[128], bool last) {
  uint64_t v[16], m[16];
  for (int i = 0; i < 16; i++) m[i] = ld64(block + 8 * i);
  for (int i = 0; i < 8; i++) {
    v[i] = s->h[i];
    v[i + 8] = B2_IV[i];
  }
  v[12] ^= s->t[0];
  v[13] ^= s->t[1];
  if (last) v[14] = ~v[14];
  for (int r = 0; r < 12; r++) {
    const uint8_t *z = B2_SIGMA[r % 10];
    B2_G(0, 4, 8, 12, m[z[0]], m[z[1]]);
    B2_G(1, 5, 9, 13, m[z[2]], m[z[3]]);
    B2_G(2, 6, 10, 14, m[z[4]], m[z[5]]);
    B2_G(3, 7, 11, 15, m[z[6]], m[z[7]]);
    B2_G(0, 5, 10, 15, m[z[8]], m[z[9]]);
    B2_G(1, 6, 11, 12, m[z[10]], m[z[11]]);
    B2_G(2, 7, 8, 13, m[z[12]], m[z[13]]);
    B2_G(3, 4, 9, 14, m[z[14]], m[z[15]]);
  }
  for (int i = 0; i < 8; i++) s->h[i] ^= v[i] ^ v[i + 8];
  cr_wipe(v, sizeof v);
  cr_wipe(m, sizeof m);
}

static void b2_count(CrBlake2b *s, uint64_t n) {
  s->t[0] += n;
  if (s->t[0] < n) s->t[1]++;
}

void cr_blake2b_init(CrBlake2b *s, size_t outlen, const uint8_t *key, size_t keylen) {
  if (outlen < 1) outlen = 1;
  if (outlen > 64) outlen = 64;
  if (keylen > 64) keylen = 64;
  memcpy(s->h, B2_IV, sizeof s->h);
  s->h[0] ^= 0x01010000u ^ ((uint64_t)keylen << 8) ^ (uint64_t)outlen; /* the parameter block (section 2.5) */
  s->t[0] = s->t[1] = 0;
  s->fill = 0;
  s->outlen = outlen;
  memset(s->buf, 0, sizeof s->buf);
  if (keylen) { /* the key, padded to a whole block, is the first block */
    memcpy(s->buf, key, keylen);
    s->fill = 128;
  }
}

void cr_blake2b_update(CrBlake2b *s, const void *data, size_t n) {
  const uint8_t *p = data;
  while (n > 0) {
    if (s->fill == 128) { /* a full block is kept until more data comes: the last block is compressed in final */
      b2_count(s, 128);
      b2_compress(s, s->buf, false);
      s->fill = 0;
    }
    size_t k = 128 - s->fill < n ? 128 - s->fill : n;
    memcpy(s->buf + s->fill, p, k);
    s->fill += k;
    p += k;
    n -= k;
  }
}

void cr_blake2b_final(CrBlake2b *s, uint8_t *out) {
  b2_count(s, s->fill);
  memset(s->buf + s->fill, 0, 128 - s->fill);
  b2_compress(s, s->buf, true);
  uint8_t h[64];
  for (int i = 0; i < 8; i++) st64(h + 8 * i, s->h[i]);
  memcpy(out, h, s->outlen);
  cr_wipe(h, sizeof h);
  cr_wipe(s, sizeof *s);
}

void cr_blake2b(uint8_t *out, size_t outlen, const uint8_t *key, size_t keylen, const void *data, size_t n) {
  CrBlake2b s;
  cr_blake2b_init(&s, outlen, key, keylen);
  cr_blake2b_update(&s, data, n);
  cr_blake2b_final(&s, out);
}

/* ==== the field GF(2^255 - 19) ================================================
 * An element is 16 limbs of 16 bits (little-endian), held in int64 so sums
 * and products have room before they are carried. Nothing here branches on
 * the values. */
typedef int64_t fe[16];

static void fe_copy(fe o, const fe a) { memcpy(o, a, sizeof(fe)); }
static void fe_set(fe o, int64_t v) {
  memset(o, 0, sizeof(fe));
  o[0] = v;
}

/* every limb back to 16 bits; what passes 2^256 comes back as 38 (2^256 = 38 mod p) */
static void fe_carry(fe o) {
  for (int round = 0; round < 2; round++)
    for (int i = 0; i < 16; i++) {
      int64_t c = (o[i] - (o[i] & 0xffff)) / 65536; /* exact: floor division, defined for negatives too */
      o[i] -= c * 65536;
      if (i < 15) o[i + 1] += c;
      else o[0] += 38 * c;
    }
}

static void fe_add(fe o, const fe a, const fe b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}
static void fe_sub(fe o, const fe a, const fe b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void fe_mul(fe o, const fe a, const fe b) {
  int64_t t[31] = { 0 };
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16]; /* limb i+16 is 2^256 times limb i */
  for (int i = 0; i < 16; i++) o[i] = t[i];
  fe_carry(o);
}

static void fe_sq(fe o, const fe a) { fe_mul(o, a, a); }

/* a^e for a public exponent e given as 32 little-endian bytes */
static void fe_pow(fe o, const fe a, const uint8_t e[32]) {
  fe r, base;
  fe_set(r, 1);
  fe_copy(base, a);
  for (int bit = 254; bit >= 0; bit--) {
    fe_sq(r, r);
    if (e[bit >> 3] >> (bit & 7) & 1) fe_mul(r, r, base); /* the exponent is public: the branch shows nothing secret */
  }
  fe_copy(o, r);
}

/* p - 2 and (p - 1) / 2, little-endian */
static const uint8_t P_MINUS_2[32] = { 0xeb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f };
static const uint8_t P_HALF[32] = { 0xf6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f };

/* 1/a (and 0 for 0: the inv0 of RFC 9380), by Fermat: a^(p-2) */
static void fe_inv(fe o, const fe a) { fe_pow(o, a, P_MINUS_2); }

/* swaps a and b when bit is 1, without a branch */
static void fe_cswap(fe a, fe b, int64_t bit) {
  int64_t mask = -bit;
  for (int i = 0; i < 16; i++) {
    int64_t x = mask & (a[i] ^ b[i]);
    a[i] ^= x;
    b[i] ^= x;
  }
}

static void fe_unpack(fe o, const uint8_t in[32]) {
  for (int i = 0; i < 16; i++) o[i] = (int64_t)in[2 * i] | (int64_t)in[2 * i + 1] << 8;
  o[15] &= 0x7fff; /* the top bit is not part of the number (RFC 7748, section 5) */
}

/* the canonical 32 bytes: fully reduced below p */
static void fe_pack(uint8_t out[32], const fe a) {
  fe t, m;
  fe_copy(t, a);
  fe_carry(t);
  fe_carry(t);
  for (int round = 0; round < 2; round++) { /* t - p, kept when it does not go below zero */
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    m[14] &= 0xffff;
    int64_t borrow = (m[15] >> 16) & 1;
    m[15] &= 0xffff;
    fe_cswap(t, m, 1 - borrow);
  }
  for (int i = 0; i < 16; i++) {
    out[2 * i] = (uint8_t)(t[i] & 0xff);
    out[2 * i + 1] = (uint8_t)(t[i] >> 8);
  }
}

static bool fe_is_zero(const fe a) {
  uint8_t b[32];
  static const uint8_t zero[32];
  fe_pack(b, a);
  return cr_same32(b, zero);
}

/* ==== X25519 (RFC 7748) ===================================================== */

void cr_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]) {
  uint8_t k[32];
  memcpy(k, scalar, 32);
  k[0] &= 248; /* decodeScalar25519 (section 5) */
  k[31] &= 127;
  k[31] |= 64;
  fe x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t;
  fe_unpack(x1, u);
  fe_set(x2, 1);
  fe_set(z2, 0);
  fe_copy(x3, x1);
  fe_set(z3, 1);
  static const fe A24 = { 121665 }; /* (486662 - 2) / 4 */
  int64_t swap = 0;
  for (int bit = 254; bit >= 0; bit--) { /* the Montgomery ladder (section 5) */
    int64_t kt = (k[bit >> 3] >> (bit & 7)) & 1;
    swap ^= kt;
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);
    swap = kt;
    fe_add(a, x2, z2);
    fe_sq(aa, a);
    fe_sub(b, x2, z2);
    fe_sq(bb, b);
    fe_sub(e, aa, bb);
    fe_add(c, x3, z3);
    fe_sub(d, x3, z3);
    fe_mul(da, d, a);
    fe_mul(cb, c, b);
    fe_add(t, da, cb);
    fe_sq(x3, t);
    fe_sub(t, da, cb);
    fe_sq(t, t);
    fe_mul(z3, x1, t);
    fe_mul(x2, aa, bb);
    fe_mul(t, A24, e);
    fe_add(t, aa, t);
    fe_mul(z2, e, t);
  }
  fe_cswap(x2, x3, swap);
  fe_cswap(z2, z3, swap);
  fe_inv(z2, z2);
  fe_mul(x2, x2, z2);
  fe_pack(out, x2);
  cr_wipe(k, sizeof k);
  cr_wipe(x2, sizeof x2), cr_wipe(z2, sizeof z2), cr_wipe(x3, sizeof x3), cr_wipe(z3, sizeof z3);
  cr_wipe(a, sizeof a), cr_wipe(aa, sizeof aa), cr_wipe(b, sizeof b), cr_wipe(bb, sizeof bb), cr_wipe(e, sizeof e);
  cr_wipe(c, sizeof c), cr_wipe(d, sizeof d), cr_wipe(da, sizeof da), cr_wipe(cb, sizeof cb), cr_wipe(t, sizeof t);
}

/* ==== Elligator 2 for Curve25519 (RFC 9380, section 6.7.1) ======================
 * J = 486662, K = 1, Z = 2 (the suite's values, section 4 and appendix). */

void cr_elligator2(uint8_t out[32], const uint8_t in[32]) {
  fe u, tv, x1, x2, gx1, t, J, one;
  fe_unpack(u, in);
  fe_set(J, 486662);
  fe_set(one, 1);
  /* x1 = -J / (1 + Z u^2); x1 = -J when that divides by zero */
  fe_sq(tv, u);
  fe_add(tv, tv, tv);
  fe_add(tv, tv, one);
  fe_inv(tv, tv); /* inv0: 0 stays 0 */
  fe_mul(x1, J, tv);
  fe_set(t, 0);
  fe_sub(x1, t, x1);
  int64_t was_zero = fe_is_zero(tv);
  fe negJ;
  fe_sub(negJ, t, J);
  fe_cswap(x1, negJ, was_zero);
  /* gx1 = x1^3 + J x1^2 + x1 */
  fe_sq(t, x1);
  fe_mul(gx1, t, x1);
  fe_mul(t, t, J);
  fe_add(gx1, gx1, t);
  fe_add(gx1, gx1, x1);
  /* x2 = -x1 - J; exactly one of gx1 and gx2 is a square, so only gx1 is tested */
  fe_set(t, 0);
  fe_sub(x2, t, x1);
  fe_sub(x2, x2, J);
  /* x = x1 when gx1 is a square (gx1^((p-1)/2) is 0 or 1), else x2: chosen without a branch */
  fe_pow(t, gx1, P_HALF);
  uint8_t l[32];
  fe_pack(l, t);
  static const uint8_t ONE[32] = { 1 }, ZERO[32] = { 0 };
  int64_t square = (int64_t)cr_same32(l, ONE) | (int64_t)cr_same32(l, ZERO); /* both always run: no short circuit */
  fe_cswap(x1, x2, 1 - square);
  fe_pack(out, x1);
  cr_wipe(u, sizeof u), cr_wipe(tv, sizeof tv), cr_wipe(x1, sizeof x1), cr_wipe(x2, sizeof x2);
  cr_wipe(gx1, sizeof gx1), cr_wipe(t, sizeof t), cr_wipe(l, sizeof l);
}

/* ==== ChaCha20 (RFC 8439, sections 2.1-2.4) ======================================== */

static uint32_t rotl32(uint32_t x, int n) { return x << n | x >> (32 - n); }

#define CC_QR(a, b, c, d)         \
  do {                            \
    x[a] += x[b];                 \
    x[d] = rotl32(x[d] ^ x[a], 16); \
    x[c] += x[d];                 \
    x[b] = rotl32(x[b] ^ x[c], 12); \
    x[a] += x[b];                 \
    x[d] = rotl32(x[d] ^ x[a], 8);  \
    x[c] += x[d];                 \
    x[b] = rotl32(x[b] ^ x[c], 7);  \
  } while (0)

void cr_chacha20_block(uint8_t out[64], const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]) {
  uint32_t s[16], x[16];
  s[0] = 0x61707865; /* "expand 32-byte k" */
  s[1] = 0x3320646e;
  s[2] = 0x79622d32;
  s[3] = 0x6b206574;
  for (int i = 0; i < 8; i++) s[4 + i] = ld32(key + 4 * i);
  s[12] = counter;
  for (int i = 0; i < 3; i++) s[13 + i] = ld32(nonce + 4 * i);
  memcpy(x, s, sizeof x);
  for (int r = 0; r < 10; r++) { /* 20 rounds: a column round and a diagonal round, ten times */
    CC_QR(0, 4, 8, 12);
    CC_QR(1, 5, 9, 13);
    CC_QR(2, 6, 10, 14);
    CC_QR(3, 7, 11, 15);
    CC_QR(0, 5, 10, 15);
    CC_QR(1, 6, 11, 12);
    CC_QR(2, 7, 8, 13);
    CC_QR(3, 4, 9, 14);
  }
  for (int i = 0; i < 16; i++) st32(out + 4 * i, x[i] + s[i]);
  cr_wipe(s, sizeof s);
  cr_wipe(x, sizeof x);
}

void cr_chacha20(uint8_t *out, const uint8_t *in, size_t n, const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]) {
  uint8_t ks[64];
  while (n > 0) {
    cr_chacha20_block(ks, key, counter++, nonce);
    size_t k = n < 64 ? n : 64;
    for (size_t i = 0; i < k; i++) out[i] = in[i] ^ ks[i];
    out += k;
    in += k;
    n -= k;
  }
  cr_wipe(ks, sizeof ks);
}

/* ==== Poly1305 (RFC 8439, section 2.5) ===============================================
 * The accumulator and r are 5 limbs of 26 bits; products fit in 64 bits. */

typedef struct {
  uint32_t r[5], h[5], s[4];
} Poly;

static void poly_init(Poly *p, const uint8_t key[32]) {
  /* r, clamped: the top 4 bits of bytes 3, 7, 11, 15 and the low 2 of bytes 4, 8, 12 cleared */
  uint32_t t0 = ld32(key) & 0x0fffffff, t1 = ld32(key + 4) & 0x0ffffffc, t2 = ld32(key + 8) & 0x0ffffffc,
           t3 = ld32(key + 12) & 0x0ffffffc;
  p->r[0] = t0 & 0x3ffffff;
  p->r[1] = (t0 >> 26 | t1 << 6) & 0x3ffffff;
  p->r[2] = (t1 >> 20 | t2 << 12) & 0x3ffffff;
  p->r[3] = (t2 >> 14 | t3 << 18) & 0x3ffffff;
  p->r[4] = t3 >> 8;
  memset(p->h, 0, sizeof p->h);
  for (int i = 0; i < 4; i++) p->s[i] = ld32(key + 16 + 4 * i);
}

/* h = (h + block) * r mod 2^130 - 5; hibit is 2^128 for a whole block */
static void poly_block(Poly *p, const uint8_t b[16], uint32_t hibit) {
  uint32_t t0 = ld32(b), t1 = ld32(b + 4), t2 = ld32(b + 8), t3 = ld32(b + 12);
  uint64_t h0 = p->h[0] + (t0 & 0x3ffffff), h1 = p->h[1] + ((t0 >> 26 | t1 << 6) & 0x3ffffff),
           h2 = p->h[2] + ((t1 >> 20 | t2 << 12) & 0x3ffffff), h3 = p->h[3] + ((t2 >> 14 | t3 << 18) & 0x3ffffff),
           h4 = p->h[4] + (t3 >> 8 | hibit);
  uint64_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
  /* limbs past 2^130 come back times 5 (2^130 = 5 mod p) */
  uint64_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint64_t d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
  uint64_t d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
  uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
  uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
  uint64_t d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;
  uint64_t c = d0 >> 26;
  d0 &= 0x3ffffff;
  d1 += c, c = d1 >> 26, d1 &= 0x3ffffff;
  d2 += c, c = d2 >> 26, d2 &= 0x3ffffff;
  d3 += c, c = d3 >> 26, d3 &= 0x3ffffff;
  d4 += c, c = d4 >> 26, d4 &= 0x3ffffff;
  d0 += c * 5, c = d0 >> 26, d0 &= 0x3ffffff;
  d1 += c;
  p->h[0] = (uint32_t)d0, p->h[1] = (uint32_t)d1, p->h[2] = (uint32_t)d2, p->h[3] = (uint32_t)d3, p->h[4] = (uint32_t)d4;
}

static void poly_finish(Poly *p, uint8_t tag[16]) {
  uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4], c;
  /* carry fully */
  c = h1 >> 26, h1 &= 0x3ffffff, h2 += c;
  c = h2 >> 26, h2 &= 0x3ffffff, h3 += c;
  c = h3 >> 26, h3 &= 0x3ffffff, h4 += c;
  c = h4 >> 26, h4 &= 0x3ffffff, h0 += c * 5;
  c = h0 >> 26, h0 &= 0x3ffffff, h1 += c;
  /* g = h + 5 - 2^130: when it does not go below zero, h was at least p and g is the result */
  uint32_t g0 = h0 + 5;
  c = g0 >> 26, g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c;
  c = g1 >> 26, g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c;
  c = g2 >> 26, g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c;
  c = g3 >> 26, g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1u << 26);
  uint32_t keep_g = -((g4 >> 31) ^ 1); /* all ones when g4 did not wrap below zero */
  h0 = (h0 & ~keep_g) | (g0 & keep_g);
  h1 = (h1 & ~keep_g) | (g1 & keep_g);
  h2 = (h2 & ~keep_g) | (g2 & keep_g);
  h3 = (h3 & ~keep_g) | (g3 & keep_g);
  h4 = (h4 & ~keep_g) | (g4 & keep_g & 0x3ffffff);
  /* h + s mod 2^128 */
  uint64_t f0 = (uint64_t)(h0 | h1 << 26) + p->s[0];
  uint64_t f1 = (uint64_t)(h1 >> 6 | h2 << 20) + p->s[1] + (f0 >> 32);
  uint64_t f2 = (uint64_t)(h2 >> 12 | h3 << 14) + p->s[2] + (f1 >> 32);
  uint64_t f3 = (uint64_t)(h3 >> 18 | h4 << 8) + p->s[3] + (f2 >> 32);
  st32(tag, (uint32_t)f0);
  st32(tag + 4, (uint32_t)f1);
  st32(tag + 8, (uint32_t)f2);
  st32(tag + 12, (uint32_t)f3);
  cr_wipe(p, sizeof *p);
}

/* data in whole 16-byte blocks, the last one padded with zeros (as the AEAD
 * pads); pad16 false gives plain Poly1305's 1-byte end marker instead */
static void poly_data(Poly *p, const uint8_t *m, size_t n, bool pad16) {
  while (n >= 16) {
    poly_block(p, m, 1u << 24);
    m += 16;
    n -= 16;
  }
  if (n) {
    uint8_t b[16] = { 0 };
    memcpy(b, m, n);
    if (pad16) poly_block(p, b, 1u << 24);
    else {
      b[n] = 1;
      poly_block(p, b, 0);
    }
  }
}

void cr_poly1305(uint8_t tag[16], const uint8_t *msg, size_t n, const uint8_t key[32]) {
  Poly p;
  poly_init(&p, key);
  poly_data(&p, msg, n, false);
  poly_finish(&p, tag);
}

/* ==== the AEAD (RFC 8439, section 2.8) ============================================== */

static void aead_tag(uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t adlen,
                     const uint8_t *cipher, size_t n) {
  uint8_t block0[64];
  cr_chacha20_block(block0, key, 0, nonce); /* the one-time Poly1305 key: the first 32 bytes of block 0 (section 2.6) */
  Poly p;
  poly_init(&p, block0);
  poly_data(&p, ad, adlen, true);
  poly_data(&p, cipher, n, true);
  uint8_t lens[16];
  st64(lens, (uint64_t)adlen);
  st64(lens + 8, (uint64_t)n);
  poly_block(&p, lens, 1u << 24);
  poly_finish(&p, tag);
  cr_wipe(block0, sizeof block0);
}

void cr_aead_seal(uint8_t *cipher, uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad,
                  size_t adlen, const uint8_t *plain, size_t n) {
  cr_chacha20(cipher, plain, n, key, 1, nonce);
  aead_tag(tag, key, nonce, ad, adlen, cipher, n);
}

bool cr_aead_open(uint8_t *plain, const uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad,
                  size_t adlen, const uint8_t *cipher, size_t n) {
  uint8_t want[16];
  aead_tag(want, key, nonce, ad, adlen, cipher, n);
  uint32_t d = 0;
  for (int i = 0; i < 16; i++) d |= (uint32_t)(want[i] ^ tag[i]);
  cr_wipe(want, sizeof want);
  if (d) return false; /* the comparison above runs in constant time; only its result is public */
  cr_chacha20(plain, cipher, n, key, 1, nonce);
  return true;
}
