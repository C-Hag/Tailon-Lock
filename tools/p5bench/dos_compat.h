/* dos_compat.h - what DJGPP's C library lacks of C99 maths, for the DOS build of p5bench only (build_dos.sh
 * includes it before every file). Test tool only. Original code for this project. */
#ifndef P5_DOS_COMPAT_H
#define P5_DOS_COMPAT_H
#include <math.h>
static inline float fminf(float a, float b) { return a < b ? a : b; }
static inline float fmaxf(float a, float b) { return a > b ? a : b; }
static inline double fmin(double a, double b) { return a < b ? a : b; }
static inline double fmax(double a, double b) { return a > b ? a : b; }
#endif
