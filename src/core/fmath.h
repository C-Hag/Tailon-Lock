/* fmath.h - the simulation's own sin, cos, tan, atan2, asin, acos, exp and pow.
 * The system maths libraries (glibc, Apple's, MinGW's) round some results
 * differently in the last bit, and a match is thousands of ticks of
 * feedback, so one bit apart grows into a different match. These use only
 * + - * / and sqrt, which IEEE 754 rounds exactly the same everywhere, and
 * are built with -ffp-contract=off (CMakeLists.txt), so every computer
 * computes the same bits. The sim, the AI, the world and the sound (synth.c,
 * sfx.c: the music render is checked bit for bit) use these; the renderer and
 * the menus may keep the system's. */
#ifndef TL_FMATH_H
#define TL_FMATH_H

float fm_sinf(float x);
float fm_cosf(float x);
void  fm_sincosf(float x, float *s, float *c);
float fm_atan2f(float y, float x);
float fm_asinf(float x);
float fm_acosf(float x);
float fm_expf(float x);
float fm_powf(float x, float y); /* x > 0 (0 for x <= 0); for the sound: frequency ratios, 2^n, 10^n */
float fm_tanf(float x);

#endif
