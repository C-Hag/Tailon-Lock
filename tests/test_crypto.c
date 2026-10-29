/* test_crypto.c - checks for our own cryptography (src/core/crypto.c): the
 * official test vectors of RFC 7748, RFC 7693 and RFC 8439, RFC 7693's
 * self-test, the edge cases (all-zero points, wrong tags, empty messages,
 * block boundaries) and a million random AEAD round trips whose outputs must
 * hash to the digest that independent implementations give for the same
 * inputs (tools/crypto_crosscheck.py --bulk). Original code for this project.
 *
 * usage: test_crypto [--quick]   (--quick: 50,000 round trips, no fixed digest) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/crypto.h"

static int failures, checks;

#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) {                                                                    \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);        \
      failures++;                                                                     \
    }                                                                                 \
  } while (0)

static size_t unhex(const char *s, uint8_t *out) {
  size_t n = strlen(s) / 2;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    sscanf(s + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  return n;
}

static int is_hex(const uint8_t *b, size_t n, const char *want) {
  uint8_t w[256];
  size_t wn = unhex(want, w);
  return wn == n && memcmp(b, w, n) == 0;
}

/* ---- X25519, RFC 7748 sections 5.2 and 6.1 ------------------------------- */
static void test_x25519(void) {
  uint8_t k[32], u[32], out[32];
  unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
  unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
  cr_x25519(out, k, u);
  CHECK(is_hex(out, 32, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552"));
  unhex("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", k);
  unhex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", u);
  cr_x25519(out, k, u);
  CHECK(is_hex(out, 32, "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957"));
  /* iterated: k = u = 9, then k, u = X25519(k, u), k */
  memset(k, 0, 32), memset(u, 0, 32);
  k[0] = u[0] = 9;
  for (int i = 1; i <= 1000; i++) {
    cr_x25519(out, k, u);
    memcpy(u, k, 32);
    memcpy(k, out, 32);
    if (i == 1) CHECK(is_hex(k, 32, "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079"));
  }
  CHECK(is_hex(k, 32, "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51"));
  /* Diffie-Hellman (section 6.1) */
  uint8_t a[32], b[32], ap[32], bp[32], s1[32], s2[32], nine[32] = { 9 };
  unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
  unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
  cr_x25519(ap, a, nine);
  cr_x25519(bp, b, nine);
  CHECK(is_hex(ap, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
  CHECK(is_hex(bp, 32, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));
  cr_x25519(s1, a, bp);
  cr_x25519(s2, b, ap);
  CHECK(is_hex(s1, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742") && cr_same32(s1, s2));
  /* a point of small order gives all zeros (the caller refuses it) */
  static const uint8_t zero[32];
  cr_x25519(out, a, zero);
  CHECK(cr_same32(out, zero));
  /* the top bit of u is ignored; u values of p and more are reduced */
  uint8_t hi[32];
  memcpy(hi, bp, 32);
  hi[31] |= 0x80;
  cr_x25519(out, a, hi);
  CHECK(cr_same32(out, s1));
}

/* ---- BLAKE2b, RFC 7693 appendices A and E ---------------------------------- */
/* the deterministic sequence of the RFC's self-test (appendix E) */
static void selftest_seq(uint8_t *out, size_t len, uint32_t seed) {
  uint32_t a = 0xDEAD4BAD * seed, b = 1;
  for (size_t i = 0; i < len; i++) {
    uint32_t t = a + b;
    a = b;
    b = t;
    out[i] = (uint8_t)(t >> 24);
  }
}

static void test_blake2b(void) {
  uint8_t out[64];
  cr_blake2b(out, 64, NULL, 0, "abc", 3);
  CHECK(is_hex(out, 64, "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc95"
                        "18d38aa8dbf1925ab92386edd4009923"));
  /* the self-test: every output length and input length, keyed and not, into one BLAKE2b-256 */
  static const size_t OUTLEN[4] = { 20, 32, 48, 64 }, INLEN[6] = { 0, 3, 128, 129, 255, 1024 };
  CrBlake2b all;
  cr_blake2b_init(&all, 32, NULL, 0);
  uint8_t in[1024], key[64], md[64];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 6; j++) {
      size_t outlen = OUTLEN[i], inlen = INLEN[j];
      selftest_seq(in, inlen, (uint32_t)inlen);
      cr_blake2b(md, outlen, NULL, 0, in, inlen);
      cr_blake2b_update(&all, md, outlen);
      selftest_seq(key, outlen, (uint32_t)outlen);
      cr_blake2b(md, outlen, key, outlen, in, inlen);
      cr_blake2b_update(&all, md, outlen);
    }
  cr_blake2b_final(&all, out);
  CHECK(is_hex(out, 32, "c23a7800d98123bd10f506c61e29da5603d763b8bbad2e737f5e765a7bccd475"));
  /* fed in pieces: the same hash */
  uint8_t whole[64], parts[64];
  selftest_seq(in, 1000, 7);
  cr_blake2b(whole, 64, key, 17, in, 1000);
  CrBlake2b s;
  cr_blake2b_init(&s, 64, key, 17);
  for (size_t at = 0; at < 1000;) {
    size_t n = 1 + (at * 7 % 200);
    if (n > 1000 - at) n = 1000 - at;
    cr_blake2b_update(&s, in + at, n);
    at += n;
  }
  cr_blake2b_final(&s, parts);
  CHECK(memcmp(whole, parts, 64) == 0);
}

/* ---- ChaCha20, Poly1305 and the AEAD, RFC 8439 ------------------------------ */
static const char SUNSCREEN[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
                                "sunscreen would be it.";

static void test_chacha_poly(void) {
  uint8_t key[32], nonce[12], out[256];
  for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
  /* the block function (section 2.3.2) */
  unhex("000000090000004a00000000", nonce);
  cr_chacha20_block(out, key, 1, nonce);
  CHECK(is_hex(out, 64, "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2"
                        "b5129cd1de164eb9cbd083e8a2503c4e"));
  /* encryption (section 2.4.2): the start of the cipher text */
  unhex("000000000000004a00000000", nonce);
  size_t n = strlen(SUNSCREEN);
  cr_chacha20(out, (const uint8_t *)SUNSCREEN, n, key, 1, nonce);
  CHECK(is_hex(out, 32, "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"));
  /* Poly1305 (section 2.5.2) */
  uint8_t pk[32], tag[16];
  unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", pk);
  cr_poly1305(tag, (const uint8_t *)"Cryptographic Forum Research Group", 34, pk);
  CHECK(is_hex(tag, 16, "a8061dc1305136c6c22b8baf0c0127a9"));
  /* the AEAD (section 2.8.2) */
  uint8_t ak[32], an[12], ad[12], ct[256], back[256];
  for (int i = 0; i < 32; i++) ak[i] = (uint8_t)(0x80 + i);
  unhex("070000004041424344454647", an);
  unhex("50515253c0c1c2c3c4c5c6c7", ad);
  cr_aead_seal(ct, tag, ak, an, ad, 12, (const uint8_t *)SUNSCREEN, n);
  CHECK(is_hex(ct, n, "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de"
                      "0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a"
                      "9de576d26586cec64b6116"));
  CHECK(is_hex(tag, 16, "1ae10b594f09e26a7e902ecbd0600691"));
  CHECK(cr_aead_open(back, tag, ak, an, ad, 12, ct, n) && memcmp(back, SUNSCREEN, n) == 0);
  /* any changed bit is refused, and nothing is written */
  for (int which = 0; which < 4; which++) {
    uint8_t t2[16], c2[256], a2[12];
    memcpy(t2, tag, 16), memcpy(c2, ct, n), memcpy(a2, ad, 12);
    if (which == 0) t2[5] ^= 1;
    else if (which == 1) c2[60] ^= 0x80;
    else if (which == 2) a2[0] ^= 4;
    else an[3] ^= 1;
    memset(back, 0xAA, sizeof back);
    CHECK(!cr_aead_open(back, t2, ak, an, a2, 12, c2, n) && back[0] == 0xAA);
    if (which == 3) an[3] ^= 1;
  }
  /* empty message and empty data */
  cr_aead_seal(ct, tag, ak, an, NULL, 0, NULL, 0);
  CHECK(cr_aead_open(back, tag, ak, an, NULL, 0, ct, 0));
  /* Poly1305 at the 2^130 - 5 wrap: r = 0, s = 0 gives 0 for any message */
  uint8_t zk[32] = { 0 };
  cr_poly1305(tag, (const uint8_t *)SUNSCREEN, n, zk);
  static const uint8_t z16[16];
  CHECK(memcmp(tag, z16, 16) == 0);
}

/* ---- Elligator 2 -------------------------------------------------------------
 * Fixed outputs from the RFC 9380 model in tools/crypto_crosscheck.py (which
 * also compares 100,000 more in the bulk digest below), and the property the
 * handshake needs: two secrets on the mapped point agree, as Diffie-Hellman
 * should. Input 0 (and p, the same field element) maps to u = 0, a point of
 * small order: the handshake's input is a hash, and it refuses an all-zero
 * shared result anyway. */
static void test_elligator(void) {
  static const char *const IO[][2] = {
    { "0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000000" },
    { "0100000000000000000000000000000000000000000000000000000000000000", "9cdb525555555555555555555555555555555555555555555555555555555555" },
    { "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "5f3520001c6c9936a31206afe7c7ac224e8861619bf98872444915899d95f46e" },
    { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "1e5942dd97c756040d27755f1e5b11349cd47d796c45d07052f7e5b11541c349" },
    { "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", "0000000000000000000000000000000000000000000000000000000000000000" },
  };
  uint8_t in[32], u[32];
  for (size_t i = 0; i < sizeof IO / sizeof *IO; i++) {
    unhex(IO[i][0], in);
    cr_elligator2(u, in);
    CHECK(is_hex(u, 32, IO[i][1]));
  }
  static const uint8_t zero[32];
  int agree = 0, n = 64;
  for (int i = 0; i < n; i++) {
    uint8_t a[32], b[32], pa[32], pb[32], ab[32], ba[32];
    for (int k = 0; k < 32; k++) in[k] = (uint8_t)(i * 37 + k * 11 + 1), a[k] = (uint8_t)(k * 3 + i), b[k] = (uint8_t)(k * 5 + 2 * i);
    cr_elligator2(u, in);
    cr_x25519(pa, a, u);
    cr_x25519(pb, b, u);
    cr_x25519(ab, a, pb);
    cr_x25519(ba, b, pa);
    if (cr_same32(ab, ba) && !cr_same32(ab, zero)) agree++;
  }
  CHECK(agree == n);
}

/* ---- a million AEAD round trips, against fixed outputs ---------------------- */
static uint64_t sm_state;
static uint64_t sm_next(void) { /* splitmix64: the same stream as tools/crypto_crosscheck.py */
  uint64_t z = (sm_state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static void sm_bytes(uint8_t *b, size_t n) {
  uint8_t w[8];
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = sm_next();
    for (int k = 0; k < 8; k++) w[k] = (uint8_t)(v >> (8 * k));
    memcpy(b + i, w, n - i < 8 ? n - i : 8);
  }
}

static void test_bulk(int quick) {
  long n_aead = quick ? 50000 : 1000000, n_x = quick ? 50 : 1000, n_ell = quick ? 5000 : 100000, n_b2 = quick ? 5000 : 100000;
  sm_state = 1987;
  CrBlake2b h;
  cr_blake2b_init(&h, 32, NULL, 0);
  uint8_t key[32], nonce[12], ad[40], pt[100], ct[100 + 16], back[100];
  long round_trips = 0;
  for (long i = 0; i < n_aead; i++) {
    sm_bytes(key, 32);
    sm_bytes(nonce, 12);
    size_t adl = sm_next() % 33, ptl = sm_next() % 97;
    sm_bytes(ad, adl);
    sm_bytes(pt, ptl);
    cr_aead_seal(ct, ct + ptl, key, nonce, ad, adl, pt, ptl);
    cr_blake2b_update(&h, ct, ptl + 16);
    if (cr_aead_open(back, ct + ptl, key, nonce, ad, adl, ct, ptl) && memcmp(back, pt, ptl) == 0) round_trips++;
  }
  CHECK(round_trips == n_aead);
  for (long i = 0; i < n_x; i++) {
    uint8_t k[32], u[32], out[32];
    sm_bytes(k, 32);
    sm_bytes(u, 32);
    cr_x25519(out, k, u);
    cr_blake2b_update(&h, out, 32);
  }
  for (long i = 0; i < n_ell; i++) {
    uint8_t in[32], out[32];
    sm_bytes(in, 32);
    cr_elligator2(out, in);
    cr_blake2b_update(&h, out, 32);
  }
  for (long i = 0; i < n_b2; i++) {
    size_t ol = 1 + sm_next() % 64, kl = sm_next() % 65, ml = sm_next() % 300;
    uint8_t k[64], m[300], out[64];
    sm_bytes(k, kl);
    sm_bytes(m, ml);
    cr_blake2b(out, ol, kl ? k : NULL, kl, m, ml);
    cr_blake2b_update(&h, out, ol);
  }
  uint8_t digest[32];
  cr_blake2b_final(&h, digest);
  if (!quick) /* the digest independent implementations give for the same inputs (tools/crypto_crosscheck.py --bulk) */
    CHECK(is_hex(digest, 32, "c5611ce107222a2437ab7dc916e93be2f1baa4635e65f6f0dc715fabcefc216d"));
  printf("round trips: %ld AEAD, %ld X25519, %ld Elligator, %ld BLAKE2b%s\n", round_trips, n_x, n_ell, n_b2,
         quick ? " (quick: no fixed digest)" : ", digest as the independent implementations give");
}

int main(int argc, char **argv) {
  int quick = argc > 1 && !strcmp(argv[1], "--quick");
  test_x25519();
  test_blake2b();
  test_chacha_poly();
  test_elligator();
  test_bulk(quick);
  if (failures) {
    fprintf(stderr, "%d of %d crypto checks failed\n", failures, checks);
    return 1;
  }
  printf("crypto tests passed (%d checks)\n", checks);
  return 0;
}
