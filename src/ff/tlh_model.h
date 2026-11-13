/* tlh_model.h - TLH, the floppy forge's lossless codec for terrain heights (16-bit samples on a square grid), the
 * model written once for both sides (TLPK_ENCODER: given and written; without: read back). Original code for this
 * project, after the LOCO-I / JPEG-LS predictor and context modelling of Weinberger, Seroussi and Sapiro,
 * "The LOCO-I lossless image compression algorithm", IEEE Transactions on Image Processing 9(8), 2000:
 *   - each sample is predicted from its left (a), upper (b) and upper-left (c) neighbours by the median edge
 *     detector (min(a, b) when c is at least both, max(a, b) when at most both, else a + b - c), corrected by the mean
 *     error seen so far in its context;
 *   - the context is how rough the ground is there: the sum of |a - c|, |c - b| and |b - d| (d upper-right) in 16
 *     steps of about a doubling each, and the sign of the last error;
 *   - the error is coded bit by bit with adaptive chances: zero or not, its sign, the bit length of its size (one
 *     yes/no per length), then the bits below the top one (the top three with chances of their own).
 * The grid is coded row by row; outside it a neighbour is the nearest inside one. */
#ifndef TL_TLH_MODEL_H
#define TL_TLH_MODEL_H

#include "ff/ff_rc.h"

#define TLH_HEADER 16 /* "TLH1", width (u16), height (u16), the samples' CRC-32, 4 bytes kept */
#define TLH_CTX    32

typedef struct {
  Prob zero[TLH_CTX], sign[TLH_CTX], len[TLH_CTX][17], hi[TLH_CTX][17][8], lo[17][16];
  int32_t sum[TLH_CTX], cnt[TLH_CTX]; /* the bias correction: errors summed and counted, by context */
} TlhModel;

static inline void tlh_init(TlhModel *m) {
  Prob *p = &m->zero[0];
  size_t n = (sizeof m->zero + sizeof m->sign + sizeof m->len + sizeof m->hi + sizeof m->lo) / sizeof(Prob);
  for (size_t i = 0; i < n; i++) p[i] = PROB_INIT;
  memset(m->sum, 0, sizeof m->sum);
  memset(m->cnt, 0, sizeof m->cnt);
}

static inline int tlh_rough(int g) { /* 0..15, about one step per doubling */
  int q = 0;
  while (g > 0 && q < 15) g >>= 1, q++;
  return q;
}

/* one error e in context ctx: written, or read back (the encoder passes e, the decoder gets it) */
static inline int tlh_err(Rc *r, TlhModel *m, int ctx, int e) {
  if (!bit_ad(r, &m->zero[ctx], e != 0)) return 0;
  int neg = bit_ad(r, &m->sign[ctx], e < 0);
  uint32_t mag = (uint32_t)(e < 0 ? -e : e); /* 1 .. 65535 */
  int bits = ff_bits(mag | 1), n = 1;
  while (n < 16 && bit_ad(r, &m->len[ctx][n], bits > n)) n++; /* the length, one yes/no a step (at most 16 bits) */
  uint32_t v = 1;
  for (int i = n - 2; i >= 0; i--) { /* the bits below the top one */
    int k = n - 2 - i, b = (int)((mag >> i) & 1);
    b = k < 3 ? bit_ad(r, &m->hi[ctx][n][(int)(v & 7)], b) : bit_ad(r, &m->lo[n][k], b);
    v = v << 1 | (uint32_t)b;
  }
  return neg ? -(int)v : (int)v;
}

/* the whole grid: w x h samples h16 (in: the encoder's, out: the decoder's), coded through r */
static inline bool tlh_grid(Rc *r, int w, int h, int16_t *s) {
  TlhModel *m = malloc(sizeof *m);
  if (!m) return false;
  tlh_init(m);
  int last = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int a = x ? s[y * w + x - 1] : y ? s[(y - 1) * w] : 0;
      int b = y ? s[(y - 1) * w + x] : a;
      int c = x && y ? s[(y - 1) * w + x - 1] : b;
      int d = y && x + 1 < w ? s[(y - 1) * w + x + 1] : b;
      int mx = a > b ? a : b, mn = a < b ? a : b;
      int p = c >= mx ? mn : c <= mn ? mx : a + b - c;
      int g = abs(a - c) + abs(c - b) + abs(b - d);
      int ctx = tlh_rough(g) * 2 + (last < 0);
      if (m->cnt[ctx]) p += m->sum[ctx] / m->cnt[ctx]; /* the context's mean error */
      if (p < -32768) p = -32768;
      if (p > 32767) p = 32767;
      int e = tlh_err(r, m, ctx, s[y * w + x] - p);
      int v = p + e;
      if (v < -32768 || v > 32767) {
        free(m);
        return false;
      }
      s[y * w + x] = (int16_t)v;
      m->sum[ctx] += e, m->cnt[ctx]++;
      if (m->cnt[ctx] >= 64) m->sum[ctx] /= 2, m->cnt[ctx] /= 2; /* keep it recent */
      last = e;
    }
  free(m);
  return true;
}

#endif
