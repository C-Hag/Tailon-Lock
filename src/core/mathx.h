/* mathx.h - vec3 helpers and the game's seeded RNG.
 * From the JS math helpers and mulberry32 (the browser game, web/index.html).
 * Vectors are plain values on the stack; nothing here allocates. */
#ifndef TL_MATHX_H
#define TL_MATHX_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "core/fmath.h"

typedef struct { float x, y, z; } vec3;

static inline vec3  v3(float x, float y, float z)  { vec3 r = { x, y, z }; return r; }
static inline vec3  v3_add(vec3 a, vec3 b)         { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3  v3_sub(vec3 a, vec3 b)         { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3  v3_scale(vec3 a, float s)      { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b)         { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3  v3_cross(vec3 a, vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(vec3 a)                 { return sqrtf(v3_dot(a, a)); }
static inline vec3  v3_norm(vec3 a) {
  float l = v3_len(a);
  return l > 0.0f ? v3_scale(a, 1.0f / l) : a;
}

static inline float v3_dist(vec3 a, vec3 b)       { return v3_len(v3_sub(a, b)); }
static inline vec3  v3_lerp(vec3 a, vec3 b, float t) { return v3_add(a, v3_scale(v3_sub(b, a), t)); }

#define PI_F 3.14159265358979f

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t)  { return a + (b - a) * t; }
static inline float signf(float x)                    { return (float)((x > 0) - (x < 0)); }

/* JS rotPair: turns the pair (a, b) by ang in their shared plane. */
static inline void rot_pair(vec3 *a, vec3 *b, float ang) {
  float c = fm_cosf(ang), s = fm_sinf(ang);
  vec3 na = v3_add(v3_scale(*a, c), v3_scale(*b, s));
  vec3 nb = v3_sub(v3_scale(*b, c), v3_scale(*a, s));
  *a = na;
  *b = nb;
}
/* JS rotY: yaw about world up. */
static inline vec3 rot_y(vec3 v, float a) {
  float c = fm_cosf(a), s = fm_sinf(a);
  return v3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}
/* Distance from p to the segment a-b (bullet and missile hit tests). */
static inline float seg_dist(vec3 a, vec3 b, vec3 p) {
  vec3 ab = v3_sub(b, a);
  float l2 = v3_dot(ab, ab);
  if (l2 <= 0.0f) l2 = 1e-9f;
  float t = clampf(v3_dot(v3_sub(p, a), ab) / l2, 0.0f, 1.0f);
  return v3_len(v3_sub(p, v3_add(a, v3_scale(ab, t))));
}

/* mulberry32, bit-exact with the JS version. One Rng per game, so replays
 * and online sync can reproduce a match from its seed. */
typedef struct { uint32_t state; } Rng;

static inline uint32_t rng_u32(Rng *r) {
  uint32_t t = r->state += 0x6D2B79F5u;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return t ^ (t >> 14);
}
/* [0, 1), same as the JS rng() */
static inline double rng_f(Rng *r) { return rng_u32(r) / 4294967296.0; }
/* JS rand(a, b) */
static inline float rng_range(Rng *r, float a, float b) { return a + (float)rng_f(r) * (b - a); }

#endif
