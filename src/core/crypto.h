/* crypto.h - our own cryptography for online play. Original code, written
 * from the RFCs; no other implementation was looked at:
 *   BLAKE2b                 RFC 7693
 *   X25519                  RFC 7748
 *   ChaCha20, Poly1305 and
 *   their AEAD construction RFC 8439
 *   Elligator 2 for
 *   Curve25519 (the map)    RFC 9380, section 6.7.1
 *
 * Everything that touches a secret runs in constant time: no branch and no
 * table index depends on secret data (the field arithmetic, the ladder's
 * swaps, Poly1305's reduction and the tag comparison are all branch-free;
 * BLAKE2b and ChaCha20 are data-independent by design). */
#ifndef A4C_CRYPTO_H
#define A4C_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Overwrites n bytes with zeros in a way the compiler keeps. */
void cr_wipe(void *p, size_t n);
/* true when the two 32-byte strings are equal; constant time. */
bool cr_same32(const uint8_t a[32], const uint8_t b[32]);

/* ---- BLAKE2b (RFC 7693) --------------------------------------------------- */
typedef struct {
  uint64_t h[8], t[2];
  uint8_t  buf[128];
  size_t   fill, outlen;
} CrBlake2b;

/* outlen 1-64 bytes; key 0-64 bytes (NULL and 0 for none) */
void cr_blake2b_init(CrBlake2b *s, size_t outlen, const uint8_t *key, size_t keylen);
void cr_blake2b_update(CrBlake2b *s, const void *data, size_t n);
void cr_blake2b_final(CrBlake2b *s, uint8_t *out);
void cr_blake2b(uint8_t *out, size_t outlen, const uint8_t *key, size_t keylen, const void *data, size_t n);

/* ---- X25519 (RFC 7748) ---------------------------------------------------- */
/* out = scalar * u, on Curve25519 (scalar clamped as the RFC says) */
void cr_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]);

/* ---- Elligator 2 (RFC 9380) ----------------------------------------------- */
/* A Curve25519 point (its u coordinate) from 32 bytes, as map_to_curve_elligator2
 * does from a field element: the bytes are read little-endian, their top bit
 * dropped, and reduced modulo 2^255 - 19. Any input gives a point; nobody
 * knows its discrete logarithm. */
void cr_elligator2(uint8_t u[32], const uint8_t in[32]);

/* ---- ChaCha20-Poly1305 (RFC 8439) ----------------------------------------- */
/* One 64-byte ChaCha20 block (section 2.3). */
void cr_chacha20_block(uint8_t out[64], const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]);
/* XORs n bytes of the ChaCha20 stream from block counter onto in (section 2.4). */
void cr_chacha20(uint8_t *out, const uint8_t *in, size_t n, const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]);
/* The Poly1305 tag of msg under a one-time key (section 2.5). */
void cr_poly1305(uint8_t tag[16], const uint8_t *msg, size_t n, const uint8_t key[32]);
/* AEAD (section 2.8): encrypts n bytes of plain into cipher and writes the
 * 16-byte tag over the additional data and the cipher text. */
void cr_aead_seal(uint8_t *cipher, uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad,
                  size_t adlen, const uint8_t *plain, size_t n);
/* Checks the tag (constant time) and only then decrypts; false and nothing
 * written when the tag is wrong. */
bool cr_aead_open(uint8_t *plain, const uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad,
                  size_t adlen, const uint8_t *cipher, size_t n);

#endif
