/* fmath.c - see fmath.h. Every function works in double and rounds once to
 * float at the end, so float accuracy needs no tricks: plain argument
 * reduction and Taylor series, which are textbook maths (the series of
 * sin, cos, atan and exp), cut where the next term is below 1e-11. Only
 * + - * /, sqrt and floor (exact everywhere) are used, and no constant
 * depends on the compiler: each is a decimal literal or a quotient of two. */
#include "core/fmath.h"

#include <math.h>

#define PI_D    3.14159265358979323846
#define PIO2_D  1.57079632679489661923
/* pi/2 split in two (Cody and Waite): k * PIO2_HI is exact for the k the
 * game sees (its 33 bits leave 20 for k), so x - k * pi/2 keeps its digits */
#define PIO2_HI 1.57079632673412561417e+00 /* the top 33 bits of pi/2 */
#define PIO2_LO 6.07710050650619224932e-11 /* pi/2 - PIO2_HI */
#define LN2_HI  6.93147180369123816490e-01 /* ln 2, the same split */
#define LN2_LO  1.90821492927058770002e-10

/* sin and cos of r in [-pi/4, pi/4]: the Taylor series to r^13 and r^14
 * (the first term left out is below 1e-11 there) */
static double sin_kernel(double r) {
  double z = r * r;
  return r * (1 + z * (-1.0 / 6 + z * (1.0 / 120 + z * (-1.0 / 5040 + z * (1.0 / 362880 +
              z * (-1.0 / 39916800 + z * (1.0 / 6227020800.0)))))));
}
static double cos_kernel(double r) {
  double z = r * r;
  return 1 + z * (-1.0 / 2 + z * (1.0 / 24 + z * (-1.0 / 720 + z * (1.0 / 40320 + z * (-1.0 / 3628800 +
             z * (1.0 / 479001600 + z * (-1.0 / 87178291200.0)))))));
}

/* x as k quarter turns plus r in [-pi/4, pi/4]; k mod 4 says which kernel and sign */
static int reduce(double x, double *r) {
  double k = floor(x / PIO2_D + 0.5);
  *r = (x - k * PIO2_HI) - k * PIO2_LO;
  double q = k - 4 * floor(k / 4);
  return (int)q;
}

void fm_sincosf(float x, float *s, float *c) {
  if (!(x - x == 0)) { /* inf or nan */
    *s = *c = x - x;
    return;
  }
  double r;
  int q = reduce(x, &r);
  double sr = sin_kernel(r), cr = cos_kernel(r);
  switch (q) {
  case 0: *s = (float)sr; *c = (float)cr; break;
  case 1: *s = (float)cr; *c = (float)-sr; break;
  case 2: *s = (float)-sr; *c = (float)-cr; break;
  default: *s = (float)-cr; *c = (float)sr; break;
  }
}

float fm_sinf(float x) {
  float s, c;
  fm_sincosf(x, &s, &c);
  return s;
}

float fm_cosf(float x) {
  float s, c;
  fm_sincosf(x, &s, &c);
  return c;
}

/* atan of t in [0, 1]: halve the angle three times (tan(a/2) = t / (1 + sqrt(1 + t^2)), so
 * t ends below 0.2), then the Taylor series to t^19 (the next term is below 1e-14) */
static double atan01(double t) {
  for (int i = 0; i < 3; i++) t = t / (1 + sqrt(1 + t * t));
  double z = t * t;
  double s = t * (1 + z * (-1.0 / 3 + z * (1.0 / 5 + z * (-1.0 / 7 + z * (1.0 / 9 + z * (-1.0 / 11 +
             z * (1.0 / 13 + z * (-1.0 / 15 + z * (1.0 / 17 + z * (-1.0 / 19))))))))));
  return 8 * s;
}

float fm_atan2f(float y, float x) {
  if (y != y || x != x) return x + y;
  double ax = fabs((double)x), ay = fabs((double)y), a;
  if (ax == 0 && ay == 0) a = 0;
  else if (ay <= ax) a = ax - ax == 0 ? atan01(ay / ax) : (ay - ay == 0 ? 0 : PI_D / 4);
  else a = ay - ay == 0 ? PIO2_D - atan01(ax / ay) : (ax - ax == 0 ? PIO2_D : PI_D / 4);
  if (x < 0 || (x == 0 && 1 / (double)x < 0)) a = PI_D - a;
  return (float)(y < 0 || (y == 0 && 1 / (double)y < 0) ? -a : a);
}

float fm_asinf(float x) {
  double d = x;
  if (!(d >= -1 && d <= 1)) return (x - x) / (x - x); /* outside [-1, 1]: nan, as asinf */
  return fm_atan2f(x, (float)sqrt(1 - d * d));
}

float fm_acosf(float x) {
  double d = x;
  if (!(d >= -1 && d <= 1)) return (x - x) / (x - x);
  return fm_atan2f((float)sqrt(1 - d * d), x);
}

/* e^x in double for |x| up to 104 (the caller has dealt with NaN and the ends): x as k ln 2 plus r in
 * [-ln 2 / 2, ln 2 / 2], the Taylor series of e^r to r^13, then 2^k */
static double exp_d(double x) {
  double k = floor(x / 0.69314718055994530942 + 0.5);
  double r = (x - k * LN2_HI) - k * LN2_LO;
  double e = 1 + r * (1 + r * (1.0 / 2 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 +
             r * (1.0 / 5040 + r * (1.0 / 40320 + r * (1.0 / 362880 + r * (1.0 / 3628800 +
             r * (1.0 / 39916800 + r * (1.0 / 479001600 + r * (1.0 / 6227020800.0)))))))))))));
  return ldexp(e, (int)k);
}

float fm_expf(float x) {
  if (x != x) return x;
  if (x > 88.8f) return (float)HUGE_VAL;
  if (x < -104.0f) return 0;
  return (float)exp_d(x);
}

/* ln x for a finite x > 0: x = m 2^e (frexp, exact) with m in [sqrt 1/2, sqrt 2), then ln m = 2 atanh s,
 * s = (m - 1) / (m + 1), |s| <= 0.1716, by its series to s^21 */
static double log_d(double x) {
  int e;
  double m = frexp(x, &e);
  if (m < 0.70710678118654752440) m *= 2, e--;
  double s = (m - 1) / (m + 1), z = s * s;
  double t = s * (2 + z * (2.0 / 3 + z * (2.0 / 5 + z * (2.0 / 7 + z * (2.0 / 9 + z * (2.0 / 11 +
             z * (2.0 / 13 + z * (2.0 / 15 + z * (2.0 / 17 + z * (2.0 / 19 + z * (2.0 / 21)))))))))));
  return (e * LN2_HI + t) + e * LN2_LO;
}

/* x^y for x > 0 as e^(y ln x); 0 for x = 0 with y > 0, 1 for y = 0. The sound only raises positive numbers (a
 * frequency ratio, 2 or 10), so a negative base, which has no real power in general, also gives 0 */
float fm_powf(float x, float y) {
  if (x != x || y != y) return x + y;
  if (y == 0) return 1;
  if (x <= 0) return 0;
  if (x == (float)HUGE_VAL) return y > 0 ? (float)HUGE_VAL : 0;
  double t = (double)y * log_d(x);
  if (t > 88.8) return (float)HUGE_VAL;
  if (t < -104.0) return 0;
  return (float)exp_d(t);
}

/* tan x as sin x / cos x, both from fm_sincosf */
float fm_tanf(float x) {
  float s, c;
  fm_sincosf(x, &s, &c);
  return s / c;
}
