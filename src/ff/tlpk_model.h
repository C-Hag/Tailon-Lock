/* tlpk_model.h - TLPK's coding model, written once for both sides: compiled with TLPK_ENCODER in tools/ff/tlpk.c (a
 * bit is given and written) and without it in src/ff/tlpk.c (the same bit is read back), so the two can never
 * drift apart. Original code for this project.
 *
 * The stream is LZ77 tokens through a binary arithmetic coder:
 *   - the range coder is the carry-less one of the LZMA specification (Igor Pavlov, public domain), on 12-bit
 *     probabilities of a 1;
 *   - the token kinds, lengths and distances follow that specification's layout: a "match?" bit by state (the
 *     last two token kinds) and position; a new distance, or a repeat of one of the last four (rep0..rep3, the
 *     used one moving to the front), or a single byte at rep0 (the short repeat); lengths 2..273 in three bit
 *     trees; distances as a 6-bit slot by length plus reverse bit trees, direct bits and 4 aligned bits;
 *   - literals are our own: a context-mixing model (after Mahoney's PAQ, "Adaptive weighing of context models for
 *     lossless data compression", 2005) of an order-0, an order-1 and a hashed order-2 model and a match model (the
 *     byte at the last distance), mixed in the logistic domain by a small neural network whose weights are chosen
 *     by the bit's position and whether the match model still agrees.
 * Everything is integer arithmetic, so every system codes the same bits. */
#ifndef TL_TLPK_MODEL_H
#define TL_TLPK_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ff/ff_rc.h"

#define TLPK_MIN_MATCH 2
#define TLPK_MAX_MATCH 273

FF_SMALL static int tree(Rc *r, Prob *t, int nbits, int sym) { /* most significant bit first */
  int m = 1;
  for (int i = nbits - 1; i >= 0; i--) m = m << 1 | bit_ad(r, &t[m], (sym >> i) & 1);
  return m - (1 << nbits);
}
FF_SMALL static int tree_rev(Rc *r, Prob *t, int nbits, int sym) { /* least significant bit first */
  int m = 1, s = 0;
  for (int i = 0; i < nbits; i++) {
    int b = bit_ad(r, &t[m], (sym >> i) & 1);
    m = m << 1 | b;
    s |= b << i;
  }
  return s;
}
static int direct(Rc *r, int nbits, int v) {
  int s = 0;
  for (int i = nbits - 1; i >= 0; i--) s = s << 1 | rc_bit(r, 2048, (v >> i) & 1);
  return s;
}

/* ---- lengths ---- */
typedef struct { Prob choice, choice2, low[4][8], mid[4][8], high[256]; } LenModel;
FF_SMALL static int len_code(Rc *r, LenModel *m, int ps, int len) { /* len - 2: 0..271 */
  int v = len - TLPK_MIN_MATCH;
  if (!bit_ad(r, &m->choice, v >= 8)) return TLPK_MIN_MATCH + tree(r, m->low[ps], 3, v);
  if (!bit_ad(r, &m->choice2, v >= 16)) return TLPK_MIN_MATCH + 8 + tree(r, m->mid[ps], 3, v - 8);
  return TLPK_MIN_MATCH + 16 + tree(r, m->high, 8, v - 16);
}

/* ---- distances (1 .. 2^31) ---- */
static int dist_slot(uint32_t dv) { /* dv = distance - 1 */
  if (dv < 4) return (int)dv;
  int n = ff_bits(dv) - 1;
  return 2 * n + (int)((dv >> (n - 1)) & 1);
}
typedef struct { Prob slot[4][64], spec[14][32], align[16]; } DistModel;
static uint32_t dist_code(Rc *r, DistModel *m, int len, uint32_t dist) {
  uint32_t dv = dist - 1;
  int ls = len - TLPK_MIN_MATCH < 3 ? len - TLPK_MIN_MATCH : 3;
  int slot = tree(r, m->slot[ls], 6, dist_slot(dv));
  if (slot < 4) return (uint32_t)slot + 1;
  int nb = (slot >> 1) - 1;
  uint32_t base = (uint32_t)(2 | (slot & 1)) << nb, extra = dv - base;
  if (slot < 14) return base + (uint32_t)tree_rev(r, m->spec[slot], nb, (int)extra) + 1;
  uint32_t hi = (uint32_t)direct(r, nb - 4, (int)(extra >> 4));
  return base + (hi << 4 | (uint32_t)tree_rev(r, m->align, 4, (int)(extra & 15))) + 1;
}

/* ---- literals: four models mixed ---- */
static int16_t stretch_t[4096];
static int squash(int d) { /* 4096 / (1 + e^(-d/256)), d clamped to -2047..2047 */
  if (d > 2047) d = 2047;
  if (d < -2047) d = -2047;
  static const int t[33] = { 1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546, 2047, 2549, 2994,
                             3348, 3607, 3785, 3901, 3975, 4024, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094 };
  int u = d + 2048; /* 1..4095: the same steps as d's, with no shift of a negative number */
  int w = u & 127;
  d = u >> 7;
  return (t[d] * (128 - w) + t[d + 1] * w + 64) >> 7;
}
static void stretch_init(void) { /* the inverse of squash */
  int pi = 0;
  for (int x = -2047; x <= 2047; x++) {
    int v = squash(x);
    for (int i = pi; i <= v; i++) stretch_t[i] = (int16_t)x;
    pi = v + 1;
  }
  for (int i = pi; i < 4096; i++) stretch_t[i] = 2047;
}

#define NIN 5
typedef struct {
  Prob     o0[256], o1[65536], mm[64];
  Prob    *o2;
  uint32_t o2mask;
  int32_t  w[32][NIN];
} LitModel;

/* false (and nothing to free) when the order-2 table cannot be had */
static bool lit_init(LitModel *m, int o2bits) {
  for (int i = 0; i < 256; i++) m->o0[i] = PROB_INIT;
  for (int i = 0; i < 65536; i++) m->o1[i] = PROB_INIT;
  for (int i = 0; i < 64; i++) m->mm[i] = PROB_INIT;
  m->o2mask = (1u << o2bits) - 1;
  m->o2 = malloc(((size_t)m->o2mask + 1) * sizeof *m->o2);
  if (!m->o2) return false;
  for (uint32_t i = 0; i <= m->o2mask; i++) m->o2[i] = PROB_INIT;
  for (int s = 0; s < 32; s++)
    for (int i = 0; i < NIN; i++) m->w[s][i] = i == NIN - 1 ? 0 : (1 << 16) / 3;
  return true;
}
static void lit_free(LitModel *m) { free(m->o2); }

static void upd(Prob *p, int bit, int rate) {
  if (bit) *p = (Prob)(*p + ((65536 - *p) >> rate));
  else *p = (Prob)(*p - (*p >> rate));
}

/* one byte; prev1/prev2 the bytes before it; mb the byte at the last distance and mrun how many bytes in a row it
 * has been right (-1: no such byte) */
FF_SMALL static int lit_code(Rc *r, LitModel *m, int prev1, int prev2, int mb, int mrun, int byte) {
  uint32_t h2 = ((uint32_t)(prev2 << 8 | prev1) + 1) * 2654435761u;
  int c0 = 1;
  bool agree = mb >= 0;
  int mctx = (mrun > 15 ? 15 : mrun < 0 ? 0 : mrun) * 2;
  for (int i = 7; i >= 0; i--) {
    Prob *q0 = &m->o0[c0], *q1 = &m->o1[prev1 << 8 | c0];
    Prob *q2 = &m->o2[(h2 ^ ((uint32_t)c0 * 0x9E3779B1u)) & m->o2mask];
    int exp = agree ? (mb >> i) & 1 : 0;
    Prob *qm = &m->mm[mctx + exp];
    int st[NIN] = { stretch_t[p12(*q0)], stretch_t[p12(*q1)], stretch_t[p12(*q2)], 0, 256 };
    if (agree) st[3] = stretch_t[p12(*qm)];
    int32_t *w = m->w[(agree ? 16 : 0) + (7 - i) * 2 + (prev1 == mb)];
    int64_t dot = 0;
    for (int k = 0; k < NIN; k++) dot += (int64_t)w[k] * st[k];
    int p = squash((int)(dot >> 16));
    p = p < 1 ? 1 : p > 4095 ? 4095 : p;
    int bit = rc_bit(r, p, (byte >> i) & 1);
    int err = ((bit << 12) - p) * 6;
    for (int k = 0; k < NIN; k++) w[k] += (st[k] * err) >> 10;
    upd(q0, bit, 5), upd(q1, bit, 4), upd(q2, bit, 4);
    if (agree) {
      upd(qm, bit, 5);
      if (bit != exp) agree = false;
    }
    c0 = c0 << 1 | bit;
  }
  return c0 & 255;
}

/* ---- the whole token model ---- */
/* the token kinds */
enum { K_LIT, K_MATCH, K_REP, K_SHORT, NKINDS };
#define NSTATES (NKINDS * NKINDS)
typedef struct {
  Prob      is_match[NSTATES][4], is_rep[NSTATES], is_rep0[NSTATES], is_rep0_long[NSTATES][4];
  Prob      is_rep1[NSTATES], is_rep2[NSTATES];
  LenModel  len, rep_len;
  DistModel dist;
  LitModel  lit;
  int       state; /* the last two token kinds: kind2 * NKINDS + kind1 */
  uint32_t  rep[4];
  int       mrun;
} Model;
static int next_state(int s, int kind) { return (s % NKINDS) * NKINDS + kind; }
/* the repeat distance k used: it moves to the front */
static void rep_use(uint32_t *r, int k) {
  uint32_t d = r[k];
  for (int i = k; i > 0; i--) r[i] = r[i - 1];
  r[0] = d;
}
static void rep_push(uint32_t *r, uint32_t d) {
  r[3] = r[2], r[2] = r[1], r[1] = r[0], r[0] = d;
}

/* false (and nothing to free) when the literal model's table cannot be had */
static bool model_init(Model *m, int o2bits) {
  Prob *all[] = { &m->is_match[0][0], m->is_rep, m->is_rep0, &m->is_rep0_long[0][0], m->is_rep1, m->is_rep2 };
  size_t ns[] = { sizeof m->is_match / sizeof(Prob), NSTATES, NSTATES, sizeof m->is_rep0_long / sizeof(Prob), NSTATES,
                  NSTATES };
  for (int k = 0; k < 6; k++)
    for (size_t i = 0; i < ns[k]; i++) all[k][i] = PROB_INIT;
  Prob *lp = (Prob *)&m->len, *rp = (Prob *)&m->rep_len, *dp = (Prob *)&m->dist;
  for (size_t i = 0; i < sizeof m->len / sizeof(Prob); i++) lp[i] = rp[i] = PROB_INIT;
  for (size_t i = 0; i < sizeof m->dist / sizeof(Prob); i++) dp[i] = PROB_INIT;
  if (!lit_init(&m->lit, o2bits)) return false;
  m->state = 0, m->mrun = 0;
  for (int i = 0; i < 4; i++) m->rep[i] = 1;
  return true;
}

/* a token after "match?" was 1: *kind K_MATCH / K_REP / K_SHORT, *k the repeat (0..3), *len, *dist (the distance
 * used, for every kind). The encoder passes the token in; the decoder gets it back. */
static void token_code(Rc *r, Model *m, int ps, int *kind, int *k, uint32_t *len, uint32_t *dist) {
  int st = m->state;
  if (!bit_ad(r, &m->is_rep[st], *kind != K_MATCH)) {
    *len = (uint32_t)len_code(r, &m->len, ps, (int)*len);
    *dist = dist_code(r, &m->dist, (int)*len, *dist);
    rep_push(m->rep, *dist);
    *kind = K_MATCH, *k = 0;
    return;
  }
  if (!bit_ad(r, &m->is_rep0[st], *k != 0)) {
    if (!bit_ad(r, &m->is_rep0_long[st][ps], *kind != K_SHORT)) {
      *kind = K_SHORT, *k = 0, *len = 1, *dist = m->rep[0];
      return;
    }
    *k = 0;
  } else if (!bit_ad(r, &m->is_rep1[st], *k != 1)) {
    *k = 1;
  } else {
    *k = bit_ad(r, &m->is_rep2[st], *k != 2) ? 3 : 2;
  }
  *len = (uint32_t)len_code(r, &m->rep_len, ps, (int)*len);
  rep_use(m->rep, *k);
  *dist = m->rep[0];
  *kind = K_REP;
}

/* the size of the hashed order-2 table for a stream of n bytes (the packer picks it; the header carries it) */
#ifdef TLPK_ENCODER
static int o2_bits_for(uint32_t n) {
  int b = 16;
  while (b < 22 && (1u << (b - 2)) < n) b++;
  return b;
}
#endif

/* ---- the filters: make code and tables more alike before the model sees them ---- */
enum { TLPK_F_NONE, TLPK_F_X86, TLPK_F_ARM64, TLPK_F_DELTA };
/* enc 1: relative branch targets to absolute ones (the same call target becomes the same bytes); 0: back */
static void filter(uint8_t *b, size_t n, int kind, int param, int enc) {
  /* a build for one CPU leaves out the other's filter (TLPK_NO_X86, TLPK_NO_ARM64): its program is the only code
   * it unpacks, and the data files use no code filter */
  if (0) {
  }
#ifndef TLPK_NO_X86
  else if (kind == TLPK_F_X86) { /* CALL rel32 (E8) and JMP rel32 (E9) */
    for (size_t i = 0; i + 5 <= n; i++)
      if (b[i] == 0xE8 || b[i] == 0xE9) {
        uint32_t v = rd32(b + i + 1), at = (uint32_t)(i + 5);
        wr32(b + i + 1, enc ? v + at : v - at);
        i += 4;
      }
  }
#endif
#ifndef TLPK_NO_ARM64
  else if (kind == TLPK_F_ARM64) { /* BL imm26 and ADRP imm21, both relative to the instruction */
    for (size_t i = 0; i + 4 <= n; i += 4) {
      uint32_t w = rd32(b + i);
      if ((w & 0xFC000000u) == 0x94000000u) {
        uint32_t im = w & 0x03FFFFFFu, at = (uint32_t)(i >> 2);
        im = (enc ? im + at : im - at) & 0x03FFFFFFu;
        wr32(b + i, 0x94000000u | im);
      } else if ((w & 0x9F000000u) == 0x90000000u) {
        uint32_t im = ((w >> 5) & 0x7FFFFu) << 2 | ((w >> 29) & 3), at = (uint32_t)(i >> 12);
        im = (enc ? im + at : im - at) & 0x1FFFFFu;
        wr32(b + i, (w & 0x9F00001Fu) | (im & 3) << 29 | (im >> 2) << 5);
      }
    }
  }
#endif
  else if (kind == TLPK_F_DELTA && param > 0) { /* each byte less the byte param before it */
    if (enc)
      for (size_t i = n; i-- > (size_t)param;) b[i] = (uint8_t)(b[i] - b[i - (size_t)param]);
    else
      for (size_t i = (size_t)param; i < n; i++) b[i] = (uint8_t)(b[i] + b[i - (size_t)param]);
  }
}

/* the header: "TLPK", version, filter, its parameter, the order-2 table's bits, the size, the CRC-32 of the
 * unpacked bytes; then the coded stream */
#define TLPK_HEADER 16
#define TLPK_VERSION 1

#endif
