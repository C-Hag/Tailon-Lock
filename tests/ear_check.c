/* ear_check.c - listens to a song the way an ear does and flags what could
 * hurt or tire it. Original code for this project. The models are written
 * from their published descriptions: ITU-R BS.1770 (K-weighted loudness, true
 * peak) and EBU Tech 3342 (loudness range), IEC 61672 (A weighting),
 * Zwicker's critical bands, specific loudness and spreading (with
 * Schroeder's spreading function), DIN 45692 sharpness, Terhardt's threshold
 * of hearing, a simplified roughness after Aures, and the safe-listening
 * limits of the health authorities (docs/HEARING.md): the WHO / ITU-T H.870
 * and H.872 weekly dose (80 dB(A) for 40 hours, 75 dB(A) in the children's
 * mode), the EU limits for music players (85 dB(A) by default, 100 dB(A) at
 * most) and the peak limits (140 dB, 120 dB for children).
 *
 * usage:
 *   ear_check --selftest
 *   ear_check [--cal DB] [--loop A B | --loopfile F.loop] [--name NAME]
 *             [--series OUT.tsv] [--tsv SUMMARY.tsv] FILE.wav ...
 *
 *   --cal DB      the listening level: the sound level in dB SPL that a
 *                 full-scale sine plays at (default 95, loud headphones; a
 *                 song at -17 LUFS then averages about 78 dB SPL). Only the
 *                 absolute figures (dB SPL, dB(A), sone, the weekly dose)
 *                 depend on it; turning it down 10 dB lowers them 10 dB.
 *   --loop A B    the file loops from frame B back to frame A: the seam is
 *                 checked for a click and a level jump (--loopfile reads the
 *                 two numbers from the first line of a .loop file)
 *   --name NAME   the name in the report (default: the file name)
 *   --series OUT  every analysis frame of this file as a table (time, dB(A),
 *                 momentary LUFS, sone, acum, share above 4 kHz)
 *   --tsv OUT     appends one summary line per file (with a header when the
 *                 file is new)
 * --loop, --name and --series apply to the next file only.
 *
 * The WAV may be 16-, 24- or 32-bit PCM or 32-bit float, mono or stereo, at
 * any rate. Each finding is NOTE (worth a listen), WARN (likely to tire or
 * sting) or HURT (fix it). Exits 0 when every file was analysed, 2 when any
 * file has a HURT finding, 1 when a file is unreadable or a self-test fails.
 *
 * What it measures:
 *   level      sample peak, true peak (4x oversampled), clipped runs, DC, rumble below 20 Hz
 *   loudness   integrated, range, momentary maximum (BS.1770 / EBU 3342)
 *   ear        A-weighted Leq and maximum at the listening level, and the hours a week that is safe;
 *              loudness in sone (N5 = exceeded 5 % of the time), sharpness in acum, roughness in asper,
 *              the share of the loudness above 4 kHz
 *   findings   piercing tones (a sustained high tone that carries most of what is heard), sharp passages,
 *              startling jumps in level, clicks, a bad loop seam
 * Loudness, sharpness and roughness are calibrated at start against their
 * reference sounds (1 sone: a 1 kHz tone at 40 dB; 1 acum: a critical band of
 * noise at 1 kHz, 60 dB; 1 asper: a 1 kHz tone at 60 dB fully modulated at
 * 70 Hz), so the figures are on the standard scales. */
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/safe.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { SEV_OK, SEV_NOTE, SEV_WARN, SEV_HURT };
static const char *const SEV[] = { "OK", "NOTE", "WARN", "HURT" };

/* ---- the sound ------------------------------------------------------------------------------------------ */

typedef struct {
  float *l, *r;
  size_t n;
  int fs;
} Sig;

static unsigned char *load(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *b = len > 0 ? malloc((size_t)len) : NULL;
  if (b && fread(b, 1, (size_t)len, f) != (size_t)len) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *n = b ? (size_t)len : 0;
  return b;
}

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* a RIFF WAVE file: PCM 16/24/32 or float 32, the first two channels (a mono file is copied to both) */
static int wav_read(const char *path, Sig *s) {
  size_t n;
  unsigned char *b = load(path, &n);
  if (!b) return 0;
  int fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0;
  const unsigned char *data = NULL;
  size_t dlen = 0;
  if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4)) {
    size_t p = 12;
    while (p + 8 <= n) {
      uint32_t clen = le32(b + p + 4);
      const unsigned char *c = b + p + 8;
      size_t len = clen < n - p - 8 ? clen : n - p - 8;
      if (!memcmp(b + p, "fmt ", 4) && len >= 16) {
        fmt = le16(c);
        ch = le16(c + 2);
        rate = le32(c + 4);
        bits = le16(c + 14);
        if (fmt == 0xFFFE && len >= 26) fmt = le16(c + 24);
      } else if (!memcmp(b + p, "data", 4)) {
        data = c;
        dlen = len;
      }
      p += 8 + (size_t)clen + (clen & 1);
    }
  }
  int bps = bits / 8;
  if (!data || ch < 1 || rate < 8000 || rate > 384000 ||
      !((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
    free(b);
    return 0;
  }
  s->n = dlen / ((size_t)ch * (size_t)bps);
  s->fs = (int)rate;
  s->l = malloc((s->n + 1) * sizeof(float));
  s->r = malloc((s->n + 1) * sizeof(float));
  if (!s->l || !s->r) {
    free(b);
    return 0;
  }
  for (size_t i = 0; i < s->n; i++) {
    for (int c = 0; c < 2; c++) {
      const unsigned char *q = data + (i * (size_t)ch + (size_t)(c < ch ? c : 0)) * (size_t)bps;
      float v;
      if (fmt == 3) {
        uint32_t u = le32(q);
        memcpy(&v, &u, 4);
      } else if (bits == 16) v = (int16_t)le16(q) / 32768.0f;
      else if (bits == 24) v = (float)((int32_t)((uint32_t)q[0] << 8 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 24) >> 8) / 8388608.0f;
      else v = (float)((int32_t)le32(q) / 2147483648.0);
      (c ? s->r : s->l)[i] = v;
    }
  }
  free(b);
  return 1;
}

static Sig sig_new(size_t n, int fs) {
  Sig s = { calloc(n + 1, sizeof(float)), calloc(n + 1, sizeof(float)), n, fs };
  return s;
}

static void sig_free(Sig *s) {
  free(s->l);
  free(s->r);
  s->l = s->r = NULL;
}

/* ---- helpers --------------------------------------------------------------------------------------------- */

static void fmt_time(char *o, size_t on, double t) {
  if (t < 0) t = 0;
  int m = (int)(t / 60);
  snprintf(o, on, "%d:%04.1f", m, t - 60 * m);
}

static int cmp_d(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}

/* the p-th percentile (0-100) of n values, or 0 for none */
static double pct(const double *v, int n, double p) {
  if (n <= 0) return 0;
  double *c = malloc((size_t)n * sizeof *c);
  memcpy(c, v, (size_t)n * sizeof *c);
  qsort(c, (size_t)n, sizeof *c, cmp_d);
  int i = (int)(p / 100 * (n - 1) + 0.5);
  double r = c[i < 0 ? 0 : i >= n ? n - 1 : i];
  free(c);
  return r;
}

static double db10(double p) { return p > 1e-30 ? 10 * log10(p) : -300; }

static void note_name(char *o, size_t on, double f) {
  static const char *const NM[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  int m = (int)lround(69 + 12 * log2(f / 440));
  if (m < 0) m = 0;
  if (m > 131) m = 131;
  snprintf(o, on, "%s%d", NM[m % 12], m / 12 - 1);
}

/* Terhardt's threshold of hearing, dB SPL; the dip near 3.3 kHz is where the ear is most sensitive */
static double tq_db(double f) {
  double k = f / 1000;
  if (k < 0.02) k = 0.02;
  double t = 3.64 * pow(k, -0.8) - 6.5 * exp(-0.6 * (k - 3.3) * (k - 3.3)) + 1e-3 * k * k * k * k;
  return t > 120 ? 120 : t;
}

/* A weighting (IEC 61672) as a power factor */
static double a_weight(double f) {
  double f2 = f * f;
  double ra = 12194.0 * 12194.0 * f2 * f2 /
              ((f2 + 20.6 * 20.6) * sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
  return ra * ra * pow(10, 0.2);
}

typedef struct {
  double b0, b1, b2, a1, a2, z1, z2;
} Biquad;

static double bq(Biquad *q, double x) {
  double y = q->b0 * x + q->z1;
  q->z1 = q->b1 * x - q->a1 * y + q->z2;
  q->z2 = q->b2 * x - q->a2 * y;
  return y;
}

/* BS.1770 K weighting at any rate: the high shelf (head) and the high-pass (RLB) */
static void k_filters(int fs, Biquad *sh, Biquad *hp) {
  double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
  double K = tan(M_PI * f0 / fs), Vh = pow(10, G / 20), Vb = pow(Vh, 0.4996667741545416);
  double a0 = 1 + K / Q + K * K;
  *sh = (Biquad){ (Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0, 0, 0 };
  f0 = 38.13547087602444;
  Q = 0.5003270373238773;
  K = tan(M_PI * f0 / fs);
  a0 = 1 + K / Q + K * K;
  *hp = (Biquad){ 1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0, 0, 0 };
}

static Biquad bandpass(double fs, double fc, double q) {
  double w = 2 * M_PI * fc / fs, al = sin(w) / (2 * q), a0 = 1 + al;
  return (Biquad){ al / a0, 0, -al / a0, -2 * cos(w) / a0, (1 - al) / a0, 0, 0 };
}

static Biquad lowpass(double fs, double fc, double q) {
  double w = 2 * M_PI * fc / fs, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
  return (Biquad){ (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0, 0, 0 };
}

/* in-place radix-2 FFT; cs/sn hold cos and sin of 2 pi k / n for k < n/2 */
static void fft(double *re, double *im, int n, const double *cs, const double *sn) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i];
      re[i] = re[j];
      re[j] = t;
      t = im[i];
      im[i] = im[j];
      im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    int step = n / len, h = len / 2;
    for (int i = 0; i < n; i += len)
      for (int k = 0; k < h; k++) {
        double wr = cs[k * step], wi = -sn[k * step];
        int a = i + k, b = a + h;
        double xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr;
        im[b] = im[a] - xi;
        re[a] += xr;
        im[a] += xi;
      }
  }
}

static void twiddles(int n, double **cs, double **sn) {
  *cs = malloc((size_t)n / 2 * sizeof(double));
  *sn = malloc((size_t)n / 2 * sizeof(double));
  for (int k = 0; k < n / 2; k++) {
    (*cs)[k] = cos(2 * M_PI * k / n);
    (*sn)[k] = sin(2 * M_PI * k / n);
  }
}

/* ---- the ear model ---------------------------------------------------------------------------------------- */

/* Zwicker's critical bands (1 Bark each), from 20 Hz; the last one reaches 20 kHz */
#define NB 25
static const double EDGE[NB + 1] = { 20,   100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270, 1480,  1720, 2000,
                                     2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 20000 };

typedef struct {
  int fs, nfft, hop;
  double *win, wsum2, *cs, *sn;
  int b0[NB], b1[NB];
  double fc[NB], etq[NB], spread[NB][NB];
  double *aw; /* A weighting per bin */
  double cal; /* dB SPL of a full-scale sine */
  double kN, kS, kR;
} Model;

/* Schroeder's spreading function: how far a band's excitation reaches dz Bark above (dz > 0) or below it */
static double spread_db(double dz) { return 15.81 + 7.5 * (dz + 0.474) - 17.5 * sqrt(1 + (dz + 0.474) * (dz + 0.474)); }

static void model_init(Model *m, int fs, double cal) {
  memset(m, 0, sizeof *m);
  m->fs = fs;
  m->cal = cal;
  m->nfft = 1;
  while (m->nfft < 0.08 * fs) m->nfft <<= 1;
  m->hop = m->nfft / 4;
  m->win = malloc((size_t)m->nfft * sizeof(double));
  for (int i = 0; i < m->nfft; i++) {
    m->win[i] = 0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / m->nfft);
    m->wsum2 += m->win[i] * m->win[i];
  }
  twiddles(m->nfft, &m->cs, &m->sn);
  double bin = (double)fs / m->nfft;
  m->aw = malloc((size_t)(m->nfft / 2) * sizeof(double));
  for (int k = 0; k < m->nfft / 2; k++) m->aw[k] = k ? a_weight(k * bin) : 0;
  for (int b = 0; b < NB; b++) {
    int k0 = (int)ceil(EDGE[b] / bin), k1 = (int)ceil(EDGE[b + 1] / bin);
    if (k0 < 1) k0 = 1;
    if (k1 > m->nfft / 2) k1 = m->nfft / 2;
    m->b0[b] = k0;
    m->b1[b] = k1 > k0 ? k1 : k0;
    m->fc[b] = b ? sqrt(EDGE[b] * EDGE[b + 1]) : 60;
    m->etq[b] = pow(10, tq_db(m->fc[b]) / 10);
  }
  for (int i = 0; i < NB; i++)
    for (int j = 0; j < NB; j++) m->spread[i][j] = abs(i - j) <= 8 ? pow(10, spread_db(i - j) / 10) : 0;
  m->kN = m->kS = m->kR = 1;
}

static void model_free(Model *m) {
  free(m->win);
  free(m->cs);
  free(m->sn);
  free(m->aw);
}

/* Zwicker's specific loudness of one band, sone per Bark, from its excitation (intensity re 20 uPa) */
static double spec_loud(double e, double etq) {
  if (e <= etq) return 0;
  return 0.08 * pow(etq, 0.23) * (pow(0.5 + 0.5 * e / etq, 0.23) - 1);
}

/* DIN 45692 weighting of the sharpness integral */
static double sharp_g(double z) { return z <= 15.8 ? 1 : 0.066 * exp(0.171 * z); }

/* ---- the analysis ----------------------------------------------------------------------------------------- */

typedef struct {
  int sev;
  char text[240];
} Finding;

typedef struct {
  double f, t0, t1, spl, dom;
  int miss;
} Tone;

typedef struct {
  double secs, peak, tpeak, dc[2], sub_share;
  long clips;
  double I, LRA, Mmax, Mmax_t, Mmed; /* Mmed: the median momentary loudness, the song's usual level */
  double LAeq, LAmax, LAmax_t, hours, hours_child, peak_spl;
  double N5, Nmed, Smed, S95, Smax, Rmed, R95, hi;
  int tones_warn, tones_note, sharp, startles, clicks;
  double click_t[8], startle_t[8], tone_f, seam, seam_db, trimmed;
  long loop_a, loop_b;
  int nf, sev;
  Finding f[48];
  /* per analysis frame */
  int nfr;
  double *t, *la, *N, *S, *hs;
  int nr;
  double *R, rhop;
} Result;

A4C_PRINTF(3, 4) static void add(Result *r, int sev, const char *fmt, ...) {
  if (sev > r->sev) r->sev = sev;
  if (r->nf >= (int)(sizeof r->f / sizeof r->f[0])) return;
  Finding *f = &r->f[r->nf++];
  f->sev = sev;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(f->text, sizeof f->text, fmt, ap);
  va_end(ap);
}

static void result_free(Result *r) {
  free(r->t);
  free(r->la);
  free(r->N);
  free(r->S);
  free(r->hs);
  free(r->R);
}

/* samples: peak, true peak, clipped runs, DC, clicks */
static void pass_samples(const Sig *s, Result *r) {
  /* true peak: 4x oversampling by a Hann-windowed sinc, 12 taps each side */
  enum { TAPS = 12 };
  double h[3][2 * TAPS];
  for (int p = 1; p <= 3; p++)
    for (int j = -TAPS + 1; j <= TAPS; j++) {
      double t = p / 4.0 - j, sc = sin(M_PI * t) / (M_PI * t), w = 0.5 + 0.5 * cos(M_PI * t / (TAPS + 0.5));
      h[p - 1][j + TAPS - 1] = sc * w;
    }
  double pk = 0, tp = 0;
  for (int c = 0; c < 2; c++) {
    const float *x = c ? s->r : s->l;
    double sum = 0;
    long run = 0;
    for (size_t i = 0; i < s->n; i++) {
      double a = fabs(x[i]);
      sum += x[i];
      if (a > pk) pk = a;
      if (a >= 0.999) {
        if (++run == 3) r->clips++;
      } else run = 0;
      if (i + TAPS < s->n && i >= TAPS) {
        for (int p = 0; p < 3; p++) {
          double y = 0;
          for (int j = -TAPS + 1; j <= TAPS; j++) y += x[i + (size_t)j] * h[p][j + TAPS - 1];
          if (fabs(y) > tp) tp = fabs(y);
        }
      }
    }
    r->dc[c] = s->n ? sum / s->n : 0;
  }
  r->peak = pk;
  r->tpeak = tp > pk ? tp : pk;

  /* clicks: a jump in the second difference that stands 4x above everything within 20 ms of it (periodic
   * edges of a pulse wave have equal neighbours and are not clicks) */
  int blk = s->fs / 1000;
  size_t nb = s->n / (size_t)blk;
  double *bm = calloc(nb + 1, sizeof *bm);
  double last = -1;
  for (int c = 0; c < 2 && nb > 44; c++) {
    const float *x = c ? s->r : s->l;
    memset(bm, 0, (nb + 1) * sizeof *bm);
    for (size_t i = 2; i < nb * (size_t)blk; i++) {
      double d = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
      if (d > bm[i / (size_t)blk]) bm[i / (size_t)blk] = d;
    }
    for (size_t b = 21; b + 21 < nb; b++) {
      if (bm[b] < 0.02) continue;
      double nmax = 0;
      for (size_t j = b - 20; j <= b + 20; j++)
        if ((j + 1 < b || j > b + 1) && bm[j] > nmax) nmax = bm[j];
      if (bm[b] > 4 * nmax && bm[b] >= bm[b - 1] && bm[b] >= bm[b + 1]) {
        double t = (b + 0.5) * blk / (double)s->fs;
        if (fabs(t - last) < 0.005) continue;
        last = t;
        if (r->clicks < 8) r->click_t[r->clicks] = t;
        r->clicks++;
      }
    }
  }
  free(bm);
}

/* the seam of a loop: playing frame b-1 then frame a */
static double seam_ratio(const Sig *s, size_t a, size_t b) {
  if (a < 2 || b < 2 || a + 2 > s->n || b > s->n) return 0;
  int w = s->fs / 50; /* 20 ms each side */
  double worst = 0;
  for (int c = 0; c < 2; c++) {
    const float *x = c ? s->r : s->l;
    double d = fabs((double)x[a] - 2.0 * x[b - 1] + x[b - 2]);
    double d1 = fabs((double)x[a + 1] - 2.0 * x[a] + x[b - 1]);
    if (d1 > d) d = d1;
    double nmax = 1e-6;
    for (int i = 2; i < w; i++) {
      size_t ia = a + (size_t)i, ib = b - (size_t)i;
      if (ia + 1 < s->n) {
        double v = fabs((double)x[ia] - 2.0 * x[ia - 1] + x[ia - 2]);
        if (v > nmax) nmax = v;
      }
      if (ib >= 2) {
        double v = fabs((double)x[ib] - 2.0 * x[ib - 1] + x[ib - 2]);
        if (v > nmax) nmax = v;
      }
    }
    if (d > 0.02 && d / nmax > worst) worst = d / nmax;
  }
  return worst;
}

/* the level step the seam adds, in dB: frame a is reached from the 200 ms before frame b instead of the 200 ms
   before a that lead into it in the song, so a downbeat after the seam is only a jump if it is one in the song */
static double seam_level(const Sig *s, size_t a, size_t b) {
  size_t w = (size_t)s->fs / 5;
  if (b < w || a < w) return 0;
  double ea = 1e-12, eb = 1e-12;
  for (size_t i = 0; i < w; i++) {
    eb += (double)s->l[b - w + i] * s->l[b - w + i] + (double)s->r[b - w + i] * s->r[b - w + i];
    ea += (double)s->l[a - w + i] * s->l[a - w + i] + (double)s->r[a - w + i] * s->r[a - w + i];
  }
  return db10(ea / eb);
}

/* K-weighted loudness: integrated, range, momentary maximum, and startling jumps over 10 ms */
static void pass_loudness(const Sig *s, Result *r) {
  int h100 = s->fs / 10, h10 = s->fs / 100;
  size_t n100 = s->n / (size_t)h100, n10 = s->n / (size_t)h10;
  double *p100 = calloc(n100 + 1, sizeof(double)), *p10 = calloc(n10 + 1, sizeof(double));
  Biquad sh[2], hp[2];
  for (int c = 0; c < 2; c++) k_filters(s->fs, &sh[c], &hp[c]);
  for (size_t i = 0; i < n10 * (size_t)h10; i++) {
    double yl = bq(&hp[0], bq(&sh[0], s->l[i])), yr = bq(&hp[1], bq(&sh[1], s->r[i]));
    double e = yl * yl + yr * yr;
    p10[i / (size_t)h10] += e / h10;
    if (i / (size_t)h100 < n100) p100[i / (size_t)h100] += e / h100;
  }
  /* integrated: 400 ms blocks every 100 ms, gated at -70 LUFS and 10 LU under the ungated mean */
  r->I = r->Mmax = -300;
  if (n100 >= 4) {
    size_t nm = n100 - 3;
    double *z = malloc(nm * sizeof *z);
    double sum = 0;
    int kept = 0;
    for (size_t j = 0; j < nm; j++) {
      z[j] = (p100[j] + p100[j + 1] + p100[j + 2] + p100[j + 3]) / 4;
      double l = -0.691 + db10(z[j]);
      if (l > r->Mmax) {
        r->Mmax = l;
        r->Mmax_t = j * 0.1 + 0.2;
      }
      if (l > -70) sum += z[j], kept++;
    }
    double *lv = malloc(nm * sizeof *lv);
    int nl = 0;
    for (size_t j = 0; j < nm; j++)
      if (-0.691 + db10(z[j]) > -70) lv[nl++] = -0.691 + db10(z[j]);
    r->Mmed = nl ? pct(lv, nl, 50) : -300;
    free(lv);
    if (kept) {
      double rel = -0.691 + db10(sum / kept) - 10, s2 = 0;
      int k2 = 0;
      for (size_t j = 0; j < nm; j++) {
        double l = -0.691 + db10(z[j]);
        if (l > -70 && l > rel) s2 += z[j], k2++;
      }
      if (k2) r->I = -0.691 + db10(s2 / k2);
    }
    free(z);
  }
  /* range: 3 s blocks every 100 ms, gated at -70 and 20 LU under their mean, 10th to 95th percentile */
  r->LRA = 0;
  if (n100 >= 30) {
    size_t ns = n100 - 29;
    double *z = malloc(ns * sizeof *z), *l = malloc(ns * sizeof *l), sum = 0;
    int kept = 0;
    for (size_t j = 0; j < ns; j++) {
      z[j] = 0;
      for (int k = 0; k < 30; k++) z[j] += p100[j + (size_t)k] / 30;
      if (-0.691 + db10(z[j]) > -70) sum += z[j], kept++;
    }
    if (kept) {
      double rel = -0.691 + db10(sum / kept) - 20;
      int nl = 0;
      for (size_t j = 0; j < ns; j++) {
        double v = -0.691 + db10(z[j]);
        if (v > -70 && v > rel) l[nl++] = v;
      }
      r->LRA = pct(l, nl, 95) - pct(l, nl, 10);
    }
    free(z);
    free(l);
  }
  /* startling jumps: 10 ms far above the second before it (up to 50 ms ago) and well above the song's usual level */
  double *cum = malloc((n10 + 1) * sizeof *cum);
  cum[0] = 0;
  for (size_t i = 0; i < n10; i++) cum[i + 1] = cum[i] + p10[i];
  double floor_p = pow(10, (-70 + 0.691) / 10), last = -10;
  for (size_t i = 50; i < n10; i++) {
    size_t a = i >= 100 ? i - 100 : 0, b = i - 5;
    double bg = (cum[b] - cum[a]) / (double)(b - a);
    if (bg < floor_p) bg = floor_p;
    double jump = db10(p10[i] / bg), lv = -0.691 + db10(p10[i]);
    double t = i * 0.01;
    if (jump >= 15 && lv >= r->Mmed + 8 && t - last > 0.5) {
      last = t;
      if (r->startles < 8) r->startle_t[r->startles] = t;
      r->startles++;
      char ts[16];
      fmt_time(ts, sizeof ts, t);
      bool hard = jump >= 20 && lv >= r->Mmed + 10;
      if (r->startles <= 6)
        add(r, hard ? SEV_WARN : SEV_NOTE, "sudden jump at %s: %.0f dB above the second before, %.1f LU over the song's usual level",
            ts, jump, lv - r->Mmed);
    }
  }
  if (r->startles > 6) add(r, SEV_NOTE, "%d more sudden jumps", r->startles - 6);
  free(cum);
  free(p100);
  free(p10);
}

/* the spectrum frame by frame: A level, loudness, sharpness, the highs, rumble, and the tone tracker */
static void pass_spectrum(const Sig *s, const Model *m, Result *r, Tone **evs, int *nev) {
  int N = m->nfft, H = m->hop;
  size_t nfr = s->n >= (size_t)N ? (s->n - (size_t)N) / (size_t)H + 1 : 1;
  r->nfr = (int)nfr;
  r->t = malloc(nfr * sizeof(double));
  r->la = malloc(nfr * sizeof(double));
  r->N = malloc(nfr * sizeof(double));
  r->S = malloc(nfr * sizeof(double));
  r->hs = malloc(nfr * sizeof(double));
  double *re = malloc((size_t)N * sizeof(double)), *im = malloc((size_t)N * sizeof(double));
  double *P = malloc((size_t)N / 2 * sizeof(double));
  double bin = (double)m->fs / N, norm = 2.0 / (N * m->wsum2), ecal = pow(10, m->cal / 10) * 2;
  double sub = 0, all = 0, aeq = 0;
  r->LAmax = -300;
  enum { MAXT = 96, MAXC = 16 };
  Tone act[MAXT];
  int nact = 0, cap = 256;
  *evs = malloc((size_t)cap * sizeof(Tone));
  *nev = 0;
  for (size_t f = 0; f < nfr; f++) {
    size_t st = f * (size_t)H;
    for (int i = 0; i < N; i++) {
      size_t k = st + (size_t)i;
      re[i] = k < s->n ? s->l[k] * m->win[i] : 0;
      im[i] = k < s->n ? s->r[k] * m->win[i] : 0;
    }
    fft(re, im, N, m->cs, m->sn);
    double pa = 0, pt = 0;
    P[0] = 0;
    for (int k = 1; k < N / 2; k++) {
      double zr = re[k], zi = im[k], yr = re[N - k], yi = im[N - k];
      double lr = (zr + yr) / 2, li = (zi - yi) / 2, rr = (zi + yi) / 2, ri = (yr - zr) / 2;
      P[k] = (lr * lr + li * li + rr * rr + ri * ri) / 2 * norm; /* mean square per bin, the two ears averaged */
      pa += P[k] * m->aw[k];
      pt += P[k];
      if (k * bin < 20) sub += P[k];
    }
    all += pt;
    double t = (st + N / 2.0) / m->fs;
    double la = m->cal + db10(2 * pa);
    aeq += pa;
    if (la > r->LAmax) r->LAmax = la, r->LAmax_t = t;
    /* loudness: band excitation, spread along the basilar membrane, Zwicker's law */
    double E[NB], Es[NB], Nz[NB], Nt = 0, Sw = 0, hi = 0;
    for (int b = 0; b < NB; b++) {
      double e = 0;
      for (int k = m->b0[b]; k < m->b1[b]; k++) e += P[k];
      E[b] = e * ecal;
    }
    for (int i = 0; i < NB; i++) {
      Es[i] = 0;
      for (int j = 0; j < NB; j++) Es[i] += E[j] * m->spread[i][j];
      Nz[i] = spec_loud(Es[i], m->etq[i]) * m->kN;
      Nt += Nz[i];
      Sw += Nz[i] * sharp_g(i + 0.5) * (i + 0.5);
      if (m->fc[i] >= 4000) hi += Nz[i];
    }
    r->t[f] = t;
    r->la[f] = la;
    r->N[f] = Nt;
    r->S[f] = Nt > 0.05 ? 0.11 * Sw / Nt * m->kS : 0;
    r->hs[f] = Nt > 0.05 ? hi / Nt : 0;

    /* tones: sharp spectral peaks above 1.5 kHz, 15 dB over their neighbourhood */
    Tone cand[MAXC];
    int nc = 0;
    int k0 = (int)(1500 / bin), k1 = (int)(fmin(18000, m->fs * 0.45) / bin);
    for (int k = k0 > 3 ? k0 : 3; k < k1 && k + 3 < N / 2; k++) {
      if (!(P[k] > P[k - 1] && P[k] >= P[k + 1] && P[k] > P[k - 2] && P[k] >= P[k + 2])) continue;
      double pk = P[k - 1] + P[k] + P[k + 1];
      int w = (int)(k * 0.08);
      if (w < 8) w = 8;
      double bg = 0;
      int nbg = 0;
      for (int j = 4; j <= w; j++) {
        if (k - j >= 1) bg += P[k - j], nbg++;
        if (k + j < N / 2) bg += P[k + j], nbg++;
      }
      bg = nbg ? bg / nbg : 0;
      if (pk < 3 * bg * 31.6) continue; /* 15 dB */
      double la_ = log(P[k - 1] + 1e-30), lb = log(P[k] + 1e-30), lc = log(P[k + 1] + 1e-30);
      double den = la_ - 2 * lb + lc, dk = den < 0 ? 0.5 * (la_ - lc) / den : 0;
      double fr = (k + dk) * bin;
      double spl = m->cal + db10(2 * pk), dom = db10(pk * a_weight(fr) / (pa > 0 ? pa : 1e-30));
      if (spl - tq_db(fr) < 20 || dom < -15) continue;
      Tone c = { fr, t, t, spl, dom, 0 };
      if (nc < MAXC) cand[nc++] = c;
      else {
        int lo = 0;
        for (int i = 1; i < MAXC; i++)
          if (cand[i].dom < cand[lo].dom) lo = i;
        if (c.dom > cand[lo].dom) cand[lo] = c;
      }
      k += 2;
    }
    bool used[MAXC] = { 0 };
    for (int a = 0; a < nact; a++) {
      int best = -1;
      double bd = 0.015;
      for (int c = 0; c < nc; c++) {
        double d = fabs(cand[c].f - act[a].f) / act[a].f;
        if (!used[c] && d < bd) bd = d, best = c;
      }
      if (best >= 0) {
        used[best] = true;
        act[a].f = 0.8 * act[a].f + 0.2 * cand[best].f;
        act[a].t1 = t;
        act[a].miss = 0;
        if (cand[best].spl > act[a].spl) act[a].spl = cand[best].spl;
        if (cand[best].dom > act[a].dom) act[a].dom = cand[best].dom;
      } else act[a].miss++;
    }
    for (int a = 0; a < nact;) {
      if (act[a].miss > 1 || f + 1 == nfr) {
        if (act[a].t1 - act[a].t0 + (double)H / m->fs >= 0.25) {
          if (*nev == cap) *evs = realloc(*evs, (size_t)(cap *= 2) * sizeof(Tone));
          (*evs)[(*nev)++] = act[a];
        }
        act[a] = act[--nact];
      } else a++;
    }
    for (int c = 0; c < nc && nact < MAXT; c++)
      if (!used[c]) act[nact++] = cand[c];
  }
  r->sub_share = all > 0 ? sub / all : 0;
  r->LAeq = m->cal + db10(2 * aeq / (double)nfr);
  r->hours = 40 * pow(10, (80 - r->LAeq) / 10);
  r->hours_child = 40 * pow(10, (75 - r->LAeq) / 10);
  free(re);
  free(im);
  free(P);
}

/* roughness: in each critical band from 400 Hz up, how deeply the envelope is modulated at 20-300 Hz,
 * weighted by the ear's roughness curve (strongest near 70 Hz) and by how loud the band is */
static void pass_roughness(const Sig *s, const Model *m, Result *r) {
  int D = (int)lround(s->fs / 2000.0);
  if (D < 1) D = 1;
  double er = (double)s->fs / D;
  enum { W = 256 };
  size_t ne = s->n / (size_t)D, nw = ne >= W ? (ne - W) / (W / 2) + 1 : 0;
  r->nr = (int)nw;
  r->rhop = (W / 2) * (double)D / s->fs;
  r->R = calloc(nw + 1, sizeof(double));
  if (!nw) return;
  double *env = malloc(ne * sizeof(double)), *ms = malloc(ne * sizeof(double));
  double *cs, *sn, win[W], ws2 = 0, re[W], im[W];
  twiddles(W, &cs, &sn);
  for (int i = 0; i < W; i++) win[i] = 0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / W), ws2 += win[i] * win[i];
  double ecal = pow(10, m->cal / 10) * 2;
  for (int b = 4; b < NB; b++) {
    if (EDGE[b + 1] > s->fs * 0.45) break;
    double fc = m->fc[b], bw = EDGE[b + 1] - EDGE[b];
    Biquad bp1 = bandpass(s->fs, fc, fc / bw * 0.644), bp2 = bp1;
    Biquad lp1 = lowpass(s->fs, 400, 0.7071), lp2 = lp1;
    double fp = fmin(100, fmax(30, 70 * pow(fc / 1000, 0.35))); /* where this band is roughest */
    double accm = 0;
    size_t j = 0;
    int c = 0;
    for (size_t i = 0; i < ne * (size_t)D; i++) {
      double x = 0.5 * (s->l[i] + s->r[i]);
      double y = bq(&bp2, bq(&bp1, x));
      double e = bq(&lp2, bq(&lp1, fabs(y)));
      accm += y * y;
      if (++c == D) {
        env[j] = e;
        ms[j] = accm / D;
        j++;
        c = 0;
        accm = 0;
      }
    }
    for (size_t w = 0; w < nw; w++) {
      size_t o = w * (W / 2);
      double mean = 0, msw = 0;
      for (int i = 0; i < W; i++) mean += env[o + (size_t)i], msw += ms[o + (size_t)i];
      mean /= W;
      msw /= W;
      if (mean <= 1e-9) continue;
      for (int i = 0; i < W; i++) re[i] = (env[o + (size_t)i] - mean) * win[i], im[i] = 0;
      fft(re, im, W, cs, sn);
      double m2 = 0;
      for (int k = 1; k < W / 2; k++) {
        double fm = k * er / W;
        if (fm < 20 || fm > 300 || fm > fc / 2) continue;
        double g = exp(-0.5 * pow(log2(fm / fp) / 0.85, 2));
        m2 += g * g * 4 * (re[k] * re[k] + im[k] * im[k]) / (W * ws2 * mean * mean);
      }
      double meff = sqrt(m2);
      if (meff > 1) meff = 1;
      double nz = spec_loud(msw * ecal, m->etq[b]);
      r->R[w] += pow(meff, 1.6) * sqrt(nz) * m->kR;
    }
  }
  free(env);
  free(ms);
  free(cs);
  free(sn);
}

/* everything, with the findings */
static void analyse(const Sig *s, const Model *m, Result *r, long loop_a, long loop_b) {
  memset(r, 0, sizeof *r);
  r->secs = (double)s->n / s->fs;
  pass_samples(s, r);
  pass_loudness(s, r);
  Tone *ev;
  int nev;
  pass_spectrum(s, m, r, &ev, &nev);
  pass_roughness(s, m, r);

  double *v = malloc(((size_t)(r->nfr > r->nr ? r->nfr : r->nr) + 1) * sizeof(double));
  int nv = 0;
  for (int i = 0; i < r->nfr; i++)
    if (r->N[i] > 0.05) v[nv++] = r->N[i];
  r->N5 = pct(v, nv, 95);
  r->Nmed = pct(v, nv, 50);
  nv = 0;
  double hs = 0, nsum = 0;
  for (int i = 0; i < r->nfr; i++)
    if (r->N[i] >= 1) {
      v[nv++] = r->S[i];
      hs += r->hs[i] * r->N[i];
      nsum += r->N[i];
      if (r->S[i] > r->Smax) r->Smax = r->S[i];
    }
  r->Smed = pct(v, nv, 50);
  r->S95 = pct(v, nv, 95);
  r->hi = nsum > 0 ? hs / nsum : 0;
  nv = 0;
  for (int i = 0; i < r->nr; i++) v[nv++] = r->R[i];
  r->Rmed = pct(v, nv, 50);
  r->R95 = pct(v, nv, 95);
  free(v);

  char ts[16], te[16], nn[8];
  /* level */
  double tp = 20 * log10(r->tpeak > 1e-9 ? r->tpeak : 1e-9);
  if (r->clips) add(r, r->clips > 10 ? SEV_HURT : SEV_WARN, "%ld clipped runs (3+ samples at full scale): harsh distortion", r->clips);
  if (tp > 0) add(r, SEV_WARN, "true peak %+.1f dBTP: over full scale between the samples, a player may clip it", tp);
  else if (tp > -1) add(r, SEV_NOTE, "true peak %+.1f dBTP: less than 1 dB of headroom", tp);
  for (int c = 0; c < 2; c++)
    if (fabs(r->dc[c]) > 0.002) add(r, SEV_NOTE, "DC offset %.4f on the %s channel", r->dc[c], c ? "right" : "left");
  if (r->sub_share > 0.02) add(r, SEV_NOTE, "%.0f %% of the energy is below 20 Hz (inaudible rumble that strains speakers)", 100 * r->sub_share);
  /* loudness */
  if (r->I > -14) add(r, SEV_WARN, "integrated %.1f LUFS: a loud master (streaming services turn music down to -14)", r->I);
  if (r->Mmax > r->I + 12) {
    fmt_time(ts, sizeof ts, r->Mmax_t);
    add(r, SEV_NOTE, "loudest moment %s at %.1f LUFS, %.1f LU over the song", ts, r->Mmax, r->Mmax - r->I);
  }
  /* the health limits, at this listening level */
  r->peak_spl = m->cal + 3.01 + tp; /* a full-scale sine is cal dB SPL, its crest 3 dB above that */
  if (r->LAeq > 85)
    add(r, SEV_WARN, "%.0f dB(A) on average: over the EU default for music players (85), safe for only %.1f hours a week", r->LAeq,
        r->hours);
  else if (r->LAeq > 80)
    add(r, SEV_NOTE, "%.0f dB(A) on average: over the WHO reference level (80), safe for %.1f hours a week (%.1f for a child)",
        r->LAeq, r->hours, r->hours_child);
  if (r->LAmax > 100) {
    fmt_time(ts, sizeof ts, r->LAmax_t);
    add(r, SEV_WARN, "%.0f dB(A) at %s: over the EU maximum for music players (100)", r->LAmax, ts);
  }
  if (r->peak_spl > 140) add(r, SEV_HURT, "peaks of %.0f dB SPL: over the 140 dB limit for instant damage", r->peak_spl);
  else if (r->peak_spl > 120) add(r, SEV_WARN, "peaks of %.0f dB SPL: over the 120 dB limit for children", r->peak_spl);
  /* the highs */
  if (r->hi > 0.25) add(r, SEV_WARN, "%.0f %% of the loudness is above 4 kHz: a bright, tiring mix", 100 * r->hi);
  else if (r->hi > 0.15) add(r, SEV_NOTE, "%.0f %% of the loudness is above 4 kHz", 100 * r->hi);
  /* sharp passages: sharpness of 2 acum or more for half a second (gaps of 2 frames bridged), while the song
     is at least a quarter as loud as usual (about 20 phon under its median at most: a quiet tail is not shrill) */
  {
    double nmin = r->Nmed / 4 > 1 ? r->Nmed / 4 : 1;
    int i = 0, shown = 0;
    while (i < r->nfr) {
      if (!(r->S[i] >= 2.0 && r->N[i] >= nmin)) {
        i++;
        continue;
      }
      int j = i, gap = 0, e = i;
      double mx = 0;
      for (; j < r->nfr && gap <= 2; j++) {
        if (r->S[j] >= 2.0 && r->N[j] >= nmin) {
          gap = 0;
          e = j;
          if (r->S[j] > mx) mx = r->S[j];
        } else gap++;
      }
      double t0 = r->t[i] - (double)m->hop / m->fs, t1 = r->t[e] + (double)m->hop / m->fs;
      if (t1 - t0 >= 0.5) {
        r->sharp++;
        if (shown++ < 6) {
          fmt_time(ts, sizeof ts, t0);
          fmt_time(te, sizeof te, t1);
          add(r, mx >= 2.5 ? SEV_WARN : SEV_NOTE, "sharp passage %s-%s, up to %.2f acum (shrill)", ts, te, mx);
        }
      }
      i = e + 1;
    }
    if (r->sharp > 6) add(r, SEV_NOTE, "%d more sharp passages", r->sharp - 6);
  }
  if (r->R95 > 1.0) add(r, SEV_NOTE, "rough (buzzy) for 5 %% of the time at %.2f asper or more", r->R95);
  /* piercing tones, grouped by the nearest semitone */
  {
    typedef struct {
      int note, count;
      double dur, dom, t, spl, f;
    } Group;
    Group g[64];
    int ng = 0;
    for (int e = 0; e < nev; e++) {
      Tone *t = &ev[e];
      if (t->f < 2000 || t->dom < -10) continue;
      int note = (int)lround(69 + 12 * log2(t->f / 440));
      int k = 0;
      while (k < ng && g[k].note != note) k++;
      if (k == ng) {
        if (ng == 64) continue;
        g[ng++] = (Group){ note, 0, 0, -300, 0, 0, t->f };
      }
      g[k].count++;
      g[k].dur += t->t1 - t->t0 + (double)m->hop / m->fs;
      if (t->dom > g[k].dom) g[k].dom = t->dom, g[k].t = t->t0, g[k].spl = t->spl, g[k].f = t->f;
    }
    for (int a = 0; a < ng; a++)
      for (int b = a + 1; b < ng; b++)
        if (g[b].dom > g[a].dom) {
          Group x = g[a];
          g[a] = g[b];
          g[b] = x;
        }
    for (int k = 0; k < ng; k++) {
      int sev = g[k].dom >= -6 ? SEV_WARN : SEV_NOTE;
      if (sev == SEV_WARN) r->tones_warn++;
      else r->tones_note++;
      if (k == 0) r->tone_f = g[k].f;
      if (k < 8) {
        fmt_time(ts, sizeof ts, g[k].t);
        note_name(nn, sizeof nn, g[k].f);
        add(r, sev, "%s high tone %.0f Hz (%s): %d times, %.1f s in all, at %s it is %.0f dB under the whole mix (%.0f dB SPL)",
            sev == SEV_WARN ? "piercing" : "strong", g[k].f, nn, g[k].count, g[k].dur, ts, -g[k].dom, g[k].spl);
      }
    }
    if (ng > 8) add(r, SEV_NOTE, "%d more high tones", ng - 8);
  }
  free(ev);
  /* clicks and the loop seam */
  if (r->clicks) {
    char list[120] = "";
    for (int i = 0; i < r->clicks && i < 6; i++) {
      fmt_time(ts, sizeof ts, r->click_t[i]);
      snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", i ? ", " : "", ts);
    }
    add(r, SEV_NOTE, "%d click%s: %s%s", r->clicks, r->clicks > 1 ? "s" : "", list, r->clicks > 6 ? ", ..." : "");
  }
  if (loop_b > 0) {
    r->loop_a = loop_a;
    r->loop_b = loop_b;
    r->seam = seam_ratio(s, (size_t)loop_a, (size_t)loop_b);
    r->seam_db = seam_level(s, (size_t)loop_a, (size_t)loop_b);
    if (r->seam > 4) add(r, SEV_WARN, "the loop seam clicks: a jump %.0fx the largest step around it", r->seam);
    if (fabs(r->seam_db) > 6) add(r, SEV_WARN, "the loop seam jumps %+.1f dB in level", r->seam_db);
  }
}

/* ---- the report ------------------------------------------------------------------------------------------- */

static void report(const char *name, const Sig *s, const Model *m, const Result *r) {
  char ts[16], tl[16];
  printf("== %s  (%.1f s, %d Hz)  listening level: a full-scale sine at %.0f dB SPL\n", name, r->secs, s->fs, m->cal);
  printf("level      peak %+.1f dBFS  true peak %+.1f dBTP  clipped runs %ld  DC %.4f/%.4f  below 20 Hz %.1f %%\n",
         20 * log10(r->peak > 1e-9 ? r->peak : 1e-9), 20 * log10(r->tpeak > 1e-9 ? r->tpeak : 1e-9), r->clips, r->dc[0],
         r->dc[1], 100 * r->sub_share);
  fmt_time(ts, sizeof ts, r->Mmax_t);
  printf("loudness   integrated %.1f LUFS  range %.1f LU  momentary max %.1f LUFS at %s\n", r->I, r->LRA, r->Mmax, ts);
  fmt_time(tl, sizeof tl, r->LAmax_t);
  printf("ear        %.1f dB(A) average, %.1f dB(A) max at %s, peaks %.0f dB SPL; safe %.1f hours a week (%.1f for a child)\n",
         r->LAeq, r->LAmax, tl, r->peak_spl, r->hours, r->hours_child);
  printf("           loudness %.1f sone median, N5 %.1f sone; sharpness %.2f acum median, %.2f p95, %.2f max\n", r->Nmed,
         r->N5, r->Smed, r->S95, r->Smax);
  printf("           roughness %.2f asper median, %.2f p95; %.0f %% of the loudness above 4 kHz\n", r->Rmed, r->R95,
         100 * r->hi);
  if (r->loop_b > 0)
    printf("seam       loop %ld -> %ld (%.3f s -> %.3f s): step %.1fx the largest around it, level %+.1f dB\n", r->loop_b,
           r->loop_a, (double)r->loop_b / s->fs, (double)r->loop_a / s->fs, r->seam, r->seam_db);
  if (r->trimmed > 0) printf("silence    %.1f s of digital silence at the end left out\n", r->trimmed);
  printf("findings:\n");
  if (!r->nf) printf("  none\n");
  for (int i = 0; i < r->nf; i++) printf("  %-4s  %s\n", SEV[r->f[i].sev], r->f[i].text);
  printf("verdict: %s\n", SEV[r->sev]);
  /* a timeline in 10 s steps */
  printf("timeline   from    dB(A)  sone  acum  asper  >4kHz\n");
  for (double t0 = 0; t0 < r->secs; t0 += 10) {
    double pa = 0, n = 0, sw = 0, hw = 0, rs = 0;
    int k = 0, kr = 0;
    for (int i = 0; i < r->nfr; i++)
      if (r->t[i] >= t0 && r->t[i] < t0 + 10) {
        pa += pow(10, r->la[i] / 10);
        n += r->N[i];
        sw += r->S[i] * r->N[i];
        hw += r->hs[i] * r->N[i];
        k++;
      }
    for (int i = 0; i < r->nr; i++) {
      double t = i * r->rhop + 0.064;
      if (t >= t0 && t < t0 + 10) rs += r->R[i], kr++;
    }
    if (!k) continue;
    fmt_time(ts, sizeof ts, t0);
    printf("           %-7s %5.1f  %5.1f  %4.2f  %5.2f  %4.0f %%\n", ts, 10 * log10(pa / k), n / k, n > 0 ? sw / n : 0,
           kr ? rs / kr : 0, n > 0 ? 100 * hw / n : 0);
  }
  printf("\n");
}

static void write_tsv(const char *path, const char *name, const Result *r) {
  FILE *f = fopen(path, "r");
  bool fresh = !f;
  if (f) {
    fseek(f, 0, SEEK_END);
    fresh = ftell(f) == 0;
    fclose(f);
  }
  f = fopen(path, "a");
  if (!f) return;
  if (fresh)
    fprintf(f, "song\tsecs\tpeak_dbfs\ttrue_peak_dbtp\tclipped\tlufs\tlra\tmomentary_max\tlaeq\tlamax\tpeak_spl\tsafe_hours_week\tsafe_hours_child\t"
               "sone_med\tsone_n5\tacum_med\tacum_p95\tacum_max\tasper_med\tasper_p95\tshare_above_4k\tpiercing_tones\t"
               "strong_tones\tsharp_passages\tsudden_jumps\tclicks\tseam\tseam_db\tverdict\n");
  fprintf(f, "%s\t%.1f\t%.2f\t%.2f\t%ld\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%s\n",
          name, r->secs, 20 * log10(r->peak > 1e-9 ? r->peak : 1e-9), 20 * log10(r->tpeak > 1e-9 ? r->tpeak : 1e-9), r->clips,
          r->I, r->LRA, r->Mmax, r->LAeq, r->LAmax, r->peak_spl, r->hours, r->hours_child, r->Nmed, r->N5, r->Smed, r->S95, r->Smax, r->Rmed, r->R95,
          r->hi, r->tones_warn, r->tones_note, r->sharp, r->startles, r->clicks, r->seam, r->seam_db, SEV[r->sev]);
  fclose(f);
}

static void write_series(const char *path, const Result *r) {
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "time\tdba\tsone\tacum\tshare_above_4k\n");
  for (int i = 0; i < r->nfr; i++) fprintf(f, "%.3f\t%.1f\t%.2f\t%.3f\t%.3f\n", r->t[i], r->la[i], r->N[i], r->S[i], r->hs[i]);
  fclose(f);
}

/* ---- calibration ------------------------------------------------------------------------------------------ */

static double amp_for(double spl, double cal) { return sqrt(2.0) * sqrt(0.5 * pow(10, (spl - cal) / 10)); }

static Sig gen_tone(int fs, double secs, double f, double spl, double cal, double am_f, double am_m) {
  Sig s = sig_new((size_t)(secs * fs), fs);
  double a = amp_for(spl, cal) / sqrt(1 + am_m * am_m / 2);
  for (size_t i = 0; i < s.n; i++) {
    double t = (double)i / fs;
    s.l[i] = s.r[i] = (float)(a * (1 + am_m * cos(2 * M_PI * am_f * t)) * sin(2 * M_PI * f * t));
  }
  return s;
}

/* noise from f0 to f1 at a given level: many sines with random phases, 1 Hz apart */
static Sig gen_band(int fs, double secs, double f0, double f1, double spl, double cal, uint32_t seed) {
  Sig s = sig_new((size_t)(secs * fs), fs);
  int nn = (int)(f1 - f0);
  double a = amp_for(spl, cal) / sqrt((double)nn);
  for (int k = 0; k < nn; k++) {
    seed = seed * 1664525u + 1013904223u;
    double ph = seed / 4294967296.0 * 2 * M_PI, w = 2 * M_PI * (f0 + k + 0.5) / fs;
    double c = cos(w), sn = sin(w), yr = cos(ph), yi = sin(ph);
    for (size_t i = 0; i < s.n; i++) {
      s.l[i] += (float)(a * yi);
      double nr = yr * c - yi * sn;
      yi = yr * sn + yi * c;
      yr = nr;
    }
  }
  memcpy(s.r, s.l, s.n * sizeof(float));
  return s;
}

static double mid_mean(const double *v, int n) {
  double sum = 0;
  int k = 0;
  for (int i = n / 4; i < n - n / 4; i++) sum += v[i], k++;
  return k ? sum / k : 0;
}

/* sets kN, kS and kR from the reference sounds at this rate */
static void calibrate(Model *m) {
  Result r;
  Tone *ev;
  int nev;
  Sig s = gen_tone(m->fs, 2, 1000, 40, m->cal, 0, 0);
  memset(&r, 0, sizeof r);
  pass_spectrum(&s, m, &r, &ev, &nev);
  m->kN = 1 / mid_mean(r.N, r.nfr);
  free(ev);
  result_free(&r);
  sig_free(&s);
  s = gen_band(m->fs, 2, 920, 1080, 60, m->cal, 7);
  memset(&r, 0, sizeof r);
  pass_spectrum(&s, m, &r, &ev, &nev);
  m->kS = 1 / mid_mean(r.S, r.nfr);
  free(ev);
  result_free(&r);
  sig_free(&s);
  s = gen_tone(m->fs, 2, 1000, 60, m->cal, 70, 1);
  memset(&r, 0, sizeof r);
  pass_roughness(&s, m, &r);
  m->kR = 1 / mid_mean(r.R, r.nr);
  result_free(&r);
  sig_free(&s);
}

/* ---- self-test -------------------------------------------------------------------------------------------- */

static int fails;

static void check(bool ok, const char *what, double got) {
  printf("  %s  %-58s %.4f\n", ok ? "PASS" : "FAIL", what, got);
  if (!ok) fails++;
}

static double tone_loud(Model *m, double f, double spl) {
  Sig s = gen_tone(m->fs, 1.5, f, spl, m->cal, 0, 0);
  Result r;
  Tone *ev;
  int nev;
  memset(&r, 0, sizeof r);
  pass_spectrum(&s, m, &r, &ev, &nev);
  double n = mid_mean(r.N, r.nfr);
  free(ev);
  result_free(&r);
  sig_free(&s);
  return n;
}

static int selftest(void) {
  int fs = 48000;
  Model m;
  model_init(&m, fs, 95);
  calibrate(&m);
  printf("calibration at %d Hz: kN %.4g kS %.4g kR %.4g\n", fs, m.kN, m.kS, m.kR);

  Biquad sh, hp;
  k_filters(48000, &sh, &hp);
  check(fabs(sh.b0 - 1.53512485958697) < 1e-6 && fabs(sh.b1 + 2.69169618940638) < 1e-6 && fabs(sh.b2 - 1.19839281085285) < 1e-6 &&
            fabs(sh.a1 + 1.69065929318241) < 1e-6 && fabs(sh.a2 - 0.73248077421585) < 1e-6,
        "K weighting shelf = the BS.1770 coefficients at 48 kHz", sh.b0);
  check(fabs(hp.a1 + 1.99004745483398) < 1e-6 && fabs(hp.a2 - 0.99007225036621) < 1e-6,
        "K weighting high-pass = the BS.1770 coefficients at 48 kHz", hp.a1);

  Result r;
  Sig s = gen_tone(fs, 10, 997, m.cal, m.cal, 0, 0); /* a full-scale sine in both channels */
  analyse(&s, &m, &r, 0, 0);
  check(fabs(r.I - 0.0) < 0.1, "997 Hz at 0 dBFS in both channels = 0 LUFS", r.I);
  check(fabs(r.LAeq - m.cal) < 0.3, "...and plays at the listening level in dB(A)", r.LAeq);
  check(r.clicks == 0 && r.startles == 0, "...no clicks, no sudden jumps", r.clicks + r.startles);
  result_free(&r);
  sig_free(&s);

  check(fabs(tone_loud(&m, 1000, 40) - 1) < 0.05, "1 kHz at 40 dB SPL = 1 sone", tone_loud(&m, 1000, 40));
  double n60 = tone_loud(&m, 1000, 60);
  check(n60 > 2.5 && n60 < 4.5, "1 kHz at 60 dB SPL = 2.5-4.5 sone (standard: 4)", n60);
  check(tone_loud(&m, 3200, 50) > tone_loud(&m, 100, 50), "3.2 kHz sounds louder than 100 Hz at the same level",
        tone_loud(&m, 3200, 50) / tone_loud(&m, 100, 50));

  Result rr;
  Tone *ev;
  int nev;
  s = gen_band(fs, 2, 920, 1080, 60, m.cal, 7);
  memset(&rr, 0, sizeof rr);
  pass_spectrum(&s, &m, &rr, &ev, &nev);
  check(fabs(mid_mean(rr.S, rr.nfr) - 1) < 0.05, "1 kHz critical band of noise at 60 dB = 1 acum", mid_mean(rr.S, rr.nfr));
  free(ev);
  result_free(&rr);
  sig_free(&s);
  s = gen_band(fs, 2, 3700, 4400, 60, m.cal, 9);
  memset(&rr, 0, sizeof rr);
  pass_spectrum(&s, &m, &rr, &ev, &nev);
  check(mid_mean(rr.S, rr.nfr) > 1.4, "4 kHz band of noise is sharper (> 1.4 acum)", mid_mean(rr.S, rr.nfr));
  free(ev);
  result_free(&rr);
  sig_free(&s);

  s = gen_tone(fs, 2, 1000, 60, m.cal, 70, 1);
  memset(&rr, 0, sizeof rr);
  pass_roughness(&s, &m, &rr);
  check(fabs(mid_mean(rr.R, rr.nr) - 1) < 0.05, "1 kHz at 60 dB, 70 Hz full modulation = 1 asper", mid_mean(rr.R, rr.nr));
  result_free(&rr);
  sig_free(&s);
  s = gen_tone(fs, 2, 1000, 60, m.cal, 0, 0);
  memset(&rr, 0, sizeof rr);
  pass_roughness(&s, &m, &rr);
  check(mid_mean(rr.R, rr.nr) < 0.1, "the same tone unmodulated is smooth (< 0.1 asper)", mid_mean(rr.R, rr.nr));
  result_free(&rr);
  sig_free(&s);
  s = gen_tone(fs, 2, 1000, 60, m.cal, 4, 1);
  memset(&rr, 0, sizeof rr);
  pass_roughness(&s, &m, &rr);
  check(mid_mean(rr.R, rr.nr) < 0.3, "...modulated at 4 Hz it fluctuates but is not rough (< 0.3)", mid_mean(rr.R, rr.nr));
  result_free(&rr);
  sig_free(&s);

  /* a piercing 4 kHz tone over quiet noise; a low tone is not flagged */
  s = gen_band(fs, 3, 100, 8000, 60, m.cal, 3);
  for (size_t i = fs / 2; i < s.n - fs / 2; i++) {
    float v = (float)(0.25 * sin(2 * M_PI * 4000.0 * i / fs));
    s.l[i] += v;
    s.r[i] += v;
  }
  analyse(&s, &m, &r, 0, 0);
  check(r.tones_warn >= 1 && fabs(r.tone_f - 4000) < 40, "a loud 4 kHz tone is found as piercing", r.tone_f);
  result_free(&r);
  sig_free(&s);
  s = gen_tone(fs, 3, 500, 80, m.cal, 0, 0);
  analyse(&s, &m, &r, 0, 0);
  check(r.tones_warn == 0 && r.tones_note == 0, "a 500 Hz tone is not a high tone", r.tones_warn + r.tones_note);
  result_free(&r);
  sig_free(&s);

  /* a sudden burst after quiet noise */
  s = gen_band(fs, 4, 100, 8000, 50, m.cal, 5);
  for (size_t i = 2 * (size_t)fs; i < 2 * (size_t)fs + fs / 10; i++) s.l[i] *= 300, s.r[i] *= 300;
  analyse(&s, &m, &r, 0, 0);
  check(r.startles >= 1 && fabs(r.startle_t[0] - 2.0) < 0.05, "a burst 50 dB over quiet noise at 2.0 s is a sudden jump",
        r.startles ? r.startle_t[0] : -1);
  result_free(&r);
  sig_free(&s);

  /* a click on a sine; a raw pulse wave's edges are not clicks */
  s = gen_tone(fs, 2, 440, 80, m.cal, 0, 0);
  s.l[fs] += 0.3f;
  analyse(&s, &m, &r, 0, 0);
  check(r.clicks == 1 && fabs(r.click_t[0] - 1.0) < 0.002, "a one-sample click at 1.000 s is found", r.clicks ? r.click_t[0] : -1);
  result_free(&r);
  sig_free(&s);
  s = sig_new(2 * (size_t)fs, fs);
  for (size_t i = 0; i < s.n; i++) s.l[i] = s.r[i] = fmod(i * 100.0 / fs, 1) < 0.25 ? 0.3f : -0.3f;
  analyse(&s, &m, &r, 0, 0);
  check(r.clicks == 0, "the edges of a 100 Hz pulse wave are not clicks", r.clicks);
  result_free(&r);
  sig_free(&s);

  /* true peak: a quarter-rate sine sampled between its crests */
  s = sig_new((size_t)fs, fs);
  for (size_t i = 0; i < s.n; i++) s.l[i] = s.r[i] = (float)sin(M_PI / 2 * i + M_PI / 4);
  analyse(&s, &m, &r, 0, 0);
  check(fabs(20 * log10(r.peak) + 3.01) < 0.05 && fabs(20 * log10(r.tpeak)) < 0.3,
        "a sine at fs/4 sampled at 45 degrees: peak -3 dBFS, true peak 0 dBTP", 20 * log10(r.tpeak));
  result_free(&r);
  sig_free(&s);

  /* a loop whose seam jumps */
  s = gen_tone(fs, 2, 220, 70, m.cal, 0, 0);
  analyse(&s, &m, &r, fs / 2 + 37, fs + fs / 2);
  check(r.seam > 4, "a loop seam cut mid-wave is found", r.seam);
  result_free(&r);
  sig_free(&s);

  /* a loop that comes back 12 dB louder */
  s = gen_tone(fs, 2, 220, 70, m.cal, 0, 0);
  for (size_t i = (size_t)fs / 2; i < s.n; i++) s.l[i] *= 0.25f, s.r[i] *= 0.25f;
  analyse(&s, &m, &r, fs / 4, fs + fs / 2);
  check(fabs(r.seam_db - 12) < 0.5, "a loop that comes back 12 dB louder is found", r.seam_db);
  result_free(&r);
  sig_free(&s);

  model_free(&m);
  printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}

/* ---- main ------------------------------------------------------------------------------------------------- */

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
  double cal = 95;
  long la = 0, lb = 0;
  const char *name = NULL, *tsv = NULL, *series = NULL;
  int rc = 0, files = 0;
  Model m;
  int mfs = 0;
  double mcal = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cal") && i + 1 < argc) cal = safe_num(argv[++i], 0);
    else if (!strcmp(argv[i], "--loop") && i + 2 < argc) la = safe_int(argv[++i], 0, LONG_MIN, LONG_MAX), lb = safe_int(argv[++i], 0, LONG_MIN, LONG_MAX);
    else if (!strcmp(argv[i], "--loopfile") && i + 1 < argc) {
      FILE *f = fopen(argv[++i], "r");
      if (!f || fscanf(f, "%ld %ld", &la, &lb) != 2) la = lb = 0;
      if (f) fclose(f);
    } else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
    else if (!strcmp(argv[i], "--tsv") && i + 1 < argc) tsv = argv[++i];
    else if (!strcmp(argv[i], "--series") && i + 1 < argc) series = argv[++i];
    else {
      Sig s;
      files++;
      if (!wav_read(argv[i], &s)) {
        fprintf(stderr, "ear_check: cannot read %s (a PCM or float WAV)\n", argv[i]);
        rc = 1;
        continue;
      }
      if (s.fs != mfs || cal != mcal) {
        if (mfs) model_free(&m);
        model_init(&m, s.fs, cal);
        calibrate(&m);
        mfs = s.fs;
        mcal = cal;
      }
      const char *nm = name ? name : strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
      /* digital silence at the end (a one-shot rendered longer than it plays) would water the averages down */
      size_t full = s.n, keep = s.n;
      while (keep > (size_t)s.fs && fabsf(s.l[keep - 1]) < 1e-5f && fabsf(s.r[keep - 1]) < 1e-5f) keep--;
      if (lb > 0 && keep < (size_t)lb) keep = (size_t)lb;
      if (full - keep < (size_t)s.fs) keep = full;
      s.n = keep;
      Result r;
      analyse(&s, &m, &r, la, lb);
      r.trimmed = (double)(full - keep) / s.fs;
      report(nm, &s, &m, &r);
      if (tsv) write_tsv(tsv, nm, &r);
      if (series) write_series(series, &r);
      if (r.sev == SEV_HURT && rc == 0) rc = 2;
      result_free(&r);
      sig_free(&s);
      la = lb = 0;
      name = series = NULL;
    }
  }
  if (mfs) model_free(&m);
  if (!files) {
    fprintf(stderr, "usage: ear_check --selftest | ear_check [--cal DB] [--loop A B | --loopfile F] [--name N] [--series OUT] [--tsv OUT] FILE.wav ...\n");
    return 1;
  }
  return rc;
}
