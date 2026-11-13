/* ff_rc.h - the floppy forge's binary arithmetic coding, shared by its codecs (TLPK, tlpk_model.h; terrain heights,
 * tlh_model.h): the carry-less range coder of the LZMA specification (Igor Pavlov, public domain) on 12-bit chances
 * of a 1, adaptive bits, and CRC-32. Each codec's model is written once for both sides: compiled with TLPK_ENCODER
 * a bit is given and written, without it the same bit is read back. Original code for this project. */
#ifndef TL_FF_RC_H
#define TL_FF_RC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* the unpacker the game carries is kept small: its coder's few hot calls stay calls (the packer inlines them) */
#if defined(TLPK_ENCODER)
#define FF_SMALL
#elif defined(__GNUC__) || defined(__clang__)
#define FF_SMALL __attribute__((noinline))
#elif defined(_MSC_VER)
#define FF_SMALL __declspec(noinline)
#else
#define FF_SMALL
#endif

/* the number of significant bits in v (0 for 0): portable C, the same on every compiler */
static inline int ff_bits(uint32_t v) {
  int n = 0;
  if (v >> 16) v >>= 16, n += 16;
  if (v >> 8) v >>= 8, n += 8;
  if (v >> 4) v >>= 4, n += 4;
  if (v >> 2) v >>= 2, n += 2;
  if (v >> 1) v >>= 1, n += 1;
  return n + (int)v;
}

/* ---- the range coder ---- */
typedef struct {
#ifdef TLPK_ENCODER
  uint64_t low;
  uint32_t range;
  uint8_t  cache;
  uint64_t cache_size;
  uint8_t *out;
  size_t   n, cap;
#else
  uint32_t range, code;
  const uint8_t *in;
  size_t   n, pos;
#endif
} Rc;

#ifdef TLPK_ENCODER
static inline void rc_put(Rc *r, uint8_t b) {
  if (r->n == r->cap) {
    r->cap = r->cap ? r->cap * 2 : 65536;
    r->out = realloc(r->out, r->cap);
  }
  r->out[r->n++] = b;
}
static inline void rc_shift(Rc *r) {
  if ((uint32_t)r->low < 0xFF000000u || (r->low >> 32) != 0) {
    uint8_t carry = (uint8_t)(r->low >> 32), t = r->cache;
    do {
      rc_put(r, (uint8_t)(t + carry));
      t = 0xFF;
    } while (--r->cache_size != 0);
    r->cache = (uint8_t)(r->low >> 24);
  }
  r->cache_size++;
  r->low = (uint32_t)r->low << 8;
}
static inline void rc_init(Rc *r) {
  memset(r, 0, sizeof *r);
  r->range = 0xFFFFFFFFu;
  r->cache_size = 1;
}
static inline void rc_flush(Rc *r) {
  for (int i = 0; i < 5; i++) rc_shift(r);
}
/* p1: the chance of a 1 in 4096ths (1..4095) */
static inline int rc_bit(Rc *r, int p1, int bit) {
  uint32_t bound = (r->range >> 12) * (uint32_t)p1;
  if (bit) r->range = bound;
  else r->low += bound, r->range -= bound;
  while (r->range < (1u << 24)) r->range <<= 8, rc_shift(r);
  return bit;
}
#else
static inline uint8_t rc_get(Rc *r) { return r->pos < r->n ? r->in[r->pos++] : 0; }
static inline void rc_init(Rc *r, const uint8_t *in, size_t n) {
  r->in = in, r->n = n, r->pos = 0, r->range = 0xFFFFFFFFu, r->code = 0;
  for (int i = 0; i < 5; i++) r->code = r->code << 8 | rc_get(r);
}
FF_SMALL static int rc_bit(Rc *r, int p1, int bit) {
  (void)bit;
  uint32_t bound = (r->range >> 12) * (uint32_t)p1;
  int b;
  if (r->code < bound) r->range = bound, b = 1;
  else r->code -= bound, r->range -= bound, b = 0;
  while (r->range < (1u << 24)) r->range <<= 8, r->code = r->code << 8 | rc_get(r);
  return b;
}
#endif

/* ---- adaptive bits and bit trees ---- */
typedef uint16_t Prob; /* the chance of a 1 in 65536ths */
#define PROB_INIT 32768
static inline int p12(Prob p) {
  int q = p >> 4;
  return q < 1 ? 1 : q > 4095 ? 4095 : q;
}
FF_SMALL static int bit_ad(Rc *r, Prob *p, int bit) {
  int b = rc_bit(r, p12(*p), bit);
  if (b) *p = (Prob)(*p + ((65536 - *p) >> 5));
  else *p = (Prob)(*p - (*p >> 5));
  return b;
}
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

/* CRC-32 (IEEE 802.3, the reflected polynomial 0xEDB88320) */
static inline uint32_t crc32_of(const uint8_t *b, size_t n) {
  static uint32_t t[256];
  if (!t[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = t[(c ^ b[i]) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

#endif
