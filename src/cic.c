/* cic.c - the carrier's combat information centre in the opening film,
 * 05:47, drawn entirely in code on our painter (paint.c): a dark, blue-lit
 * room with its wall plots and consoles, and three people lit by their
 * screens: the radar operator, the chief supervisor and the commander. One
 * slow take: tight on the two contacts on the scope, a pull back to the
 * operator, then a drift to the officers behind him. The same scene as the
 * browser game's (web/index.html, drawCIC), ported to C. Original code for
 * this project. */
#include "cic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/mathx.h"
#include "paint.h"

static Painter *P;
static Framebuffer bg; /* the room, drawn on its own and blurred, behind the people */
static Rng R;
static float T;        /* the film's clock */
static float S0, S1;   /* the shot's start and end */

static float rnd(void) { return (float)rng_f(&R); }
static float clampf_(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ease(float t) { return t * t * (3 - 2 * t); }
/* a contact's range on the scope, from the rim toward the ship over the shot */
static float range_at(float d0, float t) { return d0 + 0.08f - 0.42f * clampf_((t - S0) / (S1 - S0), 0, 1); }

typedef struct { float v[3]; } V3;
static V3 v3c(float r, float g, float b) { return (V3){ { r, g, b } }; }
static V3 mix(V3 a, V3 b, float k) { return v3c(a.v[0] + (b.v[0] - a.v[0]) * k, a.v[1] + (b.v[1] - a.v[1]) * k, a.v[2] + (b.v[2] - a.v[2]) * k); }
static V3 mul(V3 a, float k) { return v3c(a.v[0] * k, a.v[1] * k, a.v[2] * k); }
static PColor C(V3 c, float a) { return (PColor){ c.v[0], c.v[1], c.v[2], a }; }
static PColor RGBA(float r, float g, float b, float a) { return (PColor){ r, g, b, a }; }
static PColor HEX(uint32_t h) { return (PColor){ (float)(h >> 16 & 255), (float)(h >> 8 & 255), (float)(h & 255), 1 }; }

static Paint solid(PColor c) { return p_rgba(c.r, c.g, c.b, c.a); }
static void fill_c(PColor c) {
  Paint g = solid(c);
  p_fill(P, &g);
}
static void stroke_c(PColor c, float w) {
  Paint g = solid(c);
  p_stroke(P, &g, w);
}
static void rect_c(float x, float y, float w, float h, PColor c) {
  Paint g = solid(c);
  p_fill_rect(P, x, y, w, h, &g);
}
static void stroke_rect(float x, float y, float w, float h, PColor c, float lw) {
  p_begin(P);
  p_rect(P, x, y, w, h);
  stroke_c(c, lw);
}
/* gradients with up to four stops */
static Paint lg4(float x0, float y0, float x1, float y1, int n, const float *at, const PColor *c) {
  Paint g = p_linear(P, x0, y0, x1, y1);
  for (int i = 0; i < n; i++) p_stop(&g, at[i], c[i]);
  return g;
}
static Paint rg4(float x, float y, float r0, float r1, int n, const float *at, const PColor *c) {
  Paint g = p_radial(P, x, y, r0, r1);
  for (int i = 0; i < n; i++) p_stop(&g, at[i], c[i]);
  return g;
}
#define LG(x0, y0, x1, y1, ...)                                                                  \
  ({                                                                                             \
    struct { float k; PColor c; } st_[] = { __VA_ARGS__ };                                       \
    float at_[8];                                                                                \
    PColor cs_[8];                                                                               \
    int n_ = (int)(sizeof st_ / sizeof st_[0]);                                                  \
    for (int i_ = 0; i_ < n_; i_++) at_[i_] = st_[i_].k, cs_[i_] = st_[i_].c;                   \
    lg4(x0, y0, x1, y1, n_, at_, cs_);                                                           \
  })
#define RG(x, y, r0, r1, ...)                                                                    \
  ({                                                                                             \
    struct { float k; PColor c; } st_[] = { __VA_ARGS__ };                                       \
    float at_[8];                                                                                \
    PColor cs_[8];                                                                               \
    int n_ = (int)(sizeof st_ / sizeof st_[0]);                                                  \
    for (int i_ = 0; i_ < n_; i_++) at_[i_] = st_[i_].k, cs_[i_] = st_[i_].c;                   \
    rg4(x, y, r0, r1, n_, at_, cs_);                                                             \
  })
#define S_(k, c) { (k), (c) }

static void glow(float x, float y, float r, V3 col, float a) {
  Paint g = RG(x, y, 0, r, S_(0, C(col, a)), S_(1, C(col, 0)));
  p_fill_rect(P, x - r, y - r, r * 2, r * 2, &g);
}
static void mono(const char *s, float x, float y, float sz, PColor c, int align) { p_text(P, s, x, y, sz, c, align); }
static void curve(const float (*pts)[2], int n, bool closed) { p_curve(P, pts, n, closed); }

/* a line being spoken: the jaw opens per syllable and closes in the pauses */
static float speak(const float L[2], float len) {
  if (T < L[0] || T > L[1] - 0.15f) return 0;
  float k = (T - L[0]) / (L[1] - 0.15f - L[0]), syl = sinf(k * len * 0.55f * PI_F);
  return fmaxf(0, syl) * (0.55f + 0.45f * fabsf(sinf(T * 9.3f)));
}
static float blink_at(float seed) {
  float p = 2.8f + seed * 1.7f, ph = fmodf(T + seed * 5, p) / p;
  return ph > 0.965f ? 1 - fabsf(ph - 0.9825f) / 0.0175f : 0;
}

/* ---- a person: head in profile facing -x (dir flips), clothes, a key light from the screens and a rim light ---- */
enum { SUPERVISOR, OPERATOR, COMMANDER };
typedef struct {
  int   who;
  float x, y, s, dir;
  V3    skin, key, rim, iris, hair_col;
  float key_k, open, blink, age, look, brow_up, hair_a;
  bool  stubble;
  const float (*hair)[2];
  int   nhair;
} Person;

static float jaw_a, pvx = 0.06f, pvy = 0.6f;
static void J(float x, float y, float *ox, float *oy) {
  float dx = x - pvx, dy = y - pvy, c = cosf(jaw_a), s = sinf(jaw_a);
  *ox = pvx + dx * c - dy * s;
  *oy = pvy + dx * s + dy * c;
}

static void cloth(const float (*Pt)[2], int n, V3 base, V3 lit, V3 dark, float key_k, V3 rim) {
  (void)dark;
  p_begin(P);
  curve(Pt, n, true);
  fill_c(C(mul(base, 0.14f), 1));
  p_save(P);
  p_begin(P);
  curve(Pt, n, true);
  p_clip(P);
  Paint g = LG(-1.2f, 0, 1.0f, 0, S_(0, C(mul(mix(base, lit, 0.2f), 0.8f), 0.8f * key_k)), S_(0.45f, C(mul(base, 0.5f), 0.25f)),
               S_(1, C(base, 0)));
  p_fill_rect(P, -3, -1, 6, 6, &g);
  g = LG(0, 1.0f, 0, 1.7f, S_(0, C(rim, 0.16f)), S_(1, C(rim, 0)));
  p_fill_rect(P, -3, -1, 6, 6, &g);
  g = LG(0.3f, 0, 1.2f, 0, S_(0, C(rim, 0)), S_(1, C(rim, 0.5f)));
  p_begin(P);
  curve(Pt, n, true);
  p_stroke(P, &g, 0.03f);
  p_restore(P);
}
static void fold(float x0, float y0, float x1, float y1, float x2, float y2, float a) {
  p_begin(P);
  p_move(P, x0, y0);
  p_quad(P, x1, y1, x2, y2);
  stroke_c(RGBA(0, 0, 0, a), 0.018f);
}
static V3 blue_rim;
static float red_k, L1[2] = { 5.6f, 7.0f }, L2[2] = { 7.05f, 8.05f }, L3[2] = { 8.1f, 9.2f };

static void body(const Person *o, V3 lit, V3 dark);
static void extra(const Person *o, V3 lit, V3 dark);

static void person(const Person *o) {
  p_save(P);
  p_translate(P, o->x, o->y);
  p_scale(P, o->s * o->dir, o->s);
  V3 sk = o->skin, lit = mul(mix(sk, o->key, 0.35f), 0.75f + o->key_k * 0.5f);
  V3 dark = v3c(sk.v[0] * 0.16f + 4, sk.v[1] * 0.16f + 6, sk.v[2] * 0.16f + 10);
  jaw_a = -o->open * 0.16f;
  body(o, lit, dark);
  float head[25][2] = { { 0.02f, 0 }, { -0.2f, 0.035f }, { -0.33f, 0.14f }, { -0.378f, 0.27f }, { -0.398f, 0.355f }, { -0.378f, 0.425f },
                        { -0.425f, 0.515f }, { -0.49f, 0.6f }, { -0.47f, 0.635f }, { -0.405f, 0.655f }, { -0.412f, 0.7f }, { -0.387f, 0.733f },
                        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { -0.16f, 1.05f },
                        { -0.13f, 1.22f }, { 0.23f, 1.22f }, { 0.21f, 0.98f }, { 0.31f, 0.82f }, { 0.405f, 0.6f }, { 0.42f, 0.34f }, { 0.33f, 0.11f } };
  static const float JP[5][2] = { { -0.397f, 0.764f }, { -0.37f, 0.81f }, { -0.388f, 0.88f }, { -0.335f, 0.95f }, { -0.2f, 0.975f } };
  for (int i = 0; i < 5; i++) J(JP[i][0], JP[i][1], &head[12 + i][0], &head[12 + i][1]);
  const float(*H)[2] = (const float(*)[2])head;
  /* the skin, clipped to the head */
  p_begin(P);
  curve(H, 25, true);
  p_save(P);
  p_clip(P);
  rect_c(-1, -0.2f, 2, 1.8f, C(dark, 1));
  Paint g = LG(-0.5f, 0, 0.35f, 0, S_(0, C(lit, 1)), S_(0.35f, C(lit, 0.72f)), S_(0.75f, C(lit, 0.12f)), S_(1, C(lit, 0)));
  p_fill_rect(P, -1, -0.2f, 2, 1.8f, &g);
  /* modelling: forehead and cheekbone catch the light, the eye socket and under the jaw fall into shadow */
  g = RG(-0.3f, 0.22f, 0, 0.18f, S_(0, RGBA(255, 255, 255, 0.1f * o->key_k)), S_(1, RGBA(255, 255, 255, 0)));
  p_fill_rect(P, -1, -0.2f, 2, 1.8f, &g);
  g = RG(-0.26f, 0.56f, 0, 0.13f, S_(0, RGBA(255, 230, 220, 0.12f * o->key_k)), S_(1, RGBA(255, 230, 220, 0)));
  p_fill_rect(P, -1, -0.2f, 2, 1.8f, &g);
  g = RG(-0.3f, 0.44f, 0, 0.09f, S_(0, C(dark, 0.75f)), S_(1, C(dark, 0)));
  p_fill_rect(P, -1, -0.2f, 2, 1.8f, &g);
  g = LG(0, 0.88f, 0, 1.12f, S_(0, C(dark, 0)), S_(0.4f, C(dark, 0.85f)), S_(1, C(dark, 0.5f)));
  p_fill_rect(P, -1, 0.88f, 2, 0.5f, &g);
  g = RG(-0.25f, 0.7f, 0, 0.12f, S_(0, RGBA(160, 70, 60, 0.12f)), S_(1, RGBA(160, 70, 60, 0)));
  p_fill_rect(P, -1, -0.2f, 2, 1.8f, &g);
  if (o->stubble)
    for (int i = 0; i < 260; i++) {
      float a = rnd(), b = rnd(), x = -0.39f + a * 0.36f, y = 0.66f + b * 0.32f;
      if (x > -0.2f + (y - 0.66f) * 0.4f && rnd() < 0.6f) continue;
      rect_c(x, y, 0.004f, 0.004f, C(dark, 0.35f));
    }
  /* age: forehead lines and crow's feet */
  static const float FY[3] = { 0.2f, 0.245f, 0.29f }, CA[3] = { -0.3f, 0, 0.3f };
  for (int i = 0; i < 3; i++) {
    p_begin(P);
    p_move(P, -0.36f, FY[i]);
    p_quad(P, -0.28f, FY[i] - 0.012f, -0.18f, FY[i] + 0.004f);
    stroke_c(C(dark, 0.45f * o->age), 0.006f);
  }
  for (int i = 0; i < 3; i++) {
    p_begin(P);
    p_move(P, -0.27f, 0.45f);
    p_line(P, -0.23f, 0.45f + CA[i] * 0.05f);
    stroke_c(C(dark, 0.45f * o->age), 0.006f);
  }
  p_restore(P);
  /* rim light along the back of the head and neck from the deckhead lights */
  p_save(P);
  p_begin(P);
  curve(H, 25, true);
  p_clip(P);
  g = LG(0.0f, 0.9f, 0.42f, 0.2f, S_(0, C(o->rim, 0)), S_(1, C(o->rim, 0.7f)));
  p_begin(P);
  curve(H, 25, true);
  p_stroke(P, &g, 0.03f);
  g = LG(0.18f, 0, 0.45f, 0, S_(0, C(o->rim, 0)), S_(1, C(o->rim, 0.22f)));
  p_fill_rect(P, 0.1f, -0.1f, 0.4f, 1.5f, &g);
  p_restore(P);
  /* the face's edge catches the screen light: brow, nose, lips, chin */
  p_save(P);
  p_begin(P);
  curve(H, 25, true);
  p_clip(P);
  p_begin(P);
  curve(H + 2, 14, false);
  stroke_c(C(mix(lit, v3c(255, 255, 255), 0.3f), 0.6f * o->key_k), 0.014f);
  p_restore(P);
  /* the mouth: the dark gap when the jaw opens, the lip line, the corner */
  float llx, lly, inx, iny;
  J(-0.39f, 0.74f, &llx, &lly);
  J(-0.31f, 0.738f, &inx, &iny);
  p_begin(P);
  p_move(P, -0.39f - 0.005f, 0.727f);
  p_quad(P, -0.33f, 0.722f, -0.31f, 0.73f);
  p_line(P, inx, iny);
  p_quad(P, -0.35f, lly + 0.004f, llx - 0.005f, lly);
  fill_c(RGBA(20, 6, 8, 0.92f));
  p_begin(P);
  p_move(P, -0.392f, 0.731f);
  p_quad(P, -0.34f, 0.735f + o->open * 0.01f, -0.315f, 0.732f + o->open * 0.012f);
  stroke_c(C(dark, 0.9f), 0.008f);
  p_begin(P);
  p_ellipse(P, -0.395f, 0.712f, 0.016f, 0.012f, 0, 0, 2 * PI_F);
  fill_c(C(mix(lit, v3c(190, 90, 90), 0.25f), 0.35f));
  {
    float lx, ly;
    J(-0.388f, 0.757f, &lx, &ly);
    p_begin(P);
    p_ellipse(P, lx + 0.012f, ly, 0.018f, 0.008f, 0, 0, 2 * PI_F);
    fill_c(C(mix(lit, v3c(255, 255, 255), 0.3f), 0.28f * o->key_k));
  }
  /* the nose: the wing of the nostril and its shadow */
  p_begin(P);
  p_move(P, -0.405f, 0.648f);
  p_quad(P, -0.37f, 0.615f, -0.395f, 0.585f);
  stroke_c(C(dark, 0.85f), 0.009f);
  p_begin(P);
  p_ellipse(P, -0.43f, 0.642f, 0.016f, 0.007f, 0.25f, 0, 2 * PI_F);
  fill_c(C(dark, 0.9f));
  /* the fold from nose to mouth, deeper with age */
  p_begin(P);
  p_move(P, -0.375f, 0.61f);
  p_quad(P, -0.34f, 0.68f, -0.335f, 0.745f);
  stroke_c(C(dark, 0.35f + 0.35f * o->age), 0.007f);
  /* the eye: socket, eyeball, iris with a glint of the screen, lids; it blinks */
  float ex = -0.335f, ey = 0.447f, bl = o->blink;
  g = RG(ex + 0.015f, ey - 0.004f, 0.005f, 0.05f, S_(0, C(dark, 0.55f)), S_(1, C(dark, 0)));
  p_fill_rect(P, ex - 0.05f, ey - 0.06f, 0.12f, 0.12f, &g);
  p_begin(P);
  p_move(P, ex - 0.022f, ey);
  p_quad(P, ex + 0.01f, ey - 0.024f, ex + 0.04f, ey - 0.012f);
  p_line(P, ex + 0.038f, ey + 0.012f);
  p_quad(P, ex + 0.01f, ey + 0.016f, ex - 0.022f, ey);
  fill_c(C(mul(mix(v3c(200, 205, 210), o->key, 0.3f), 0.45f + 0.35f * o->key_k), 1));
  p_begin(P);
  p_ellipse(P, ex - 0.008f + o->look * 0.004f, ey - 0.001f, 0.009f, 0.014f, 0, 0, 2 * PI_F);
  fill_c(C(o->iris, 1));
  rect_c(ex - 0.014f, ey - 0.008f, 0.005f, 0.005f, C(mix(o->key, v3c(255, 255, 255), 0.5f), 0.9f * o->key_k));
  if (bl > 0) {
    p_begin(P);
    p_move(P, ex - 0.024f, ey - 0.002f);
    p_quad(P, ex + 0.01f, ey - 0.026f, ex + 0.042f, ey - 0.014f);
    p_line(P, ex + 0.042f, ey - 0.014f + 0.028f * bl);
    p_quad(P, ex + 0.01f, ey - 0.026f + 0.04f * bl, ex - 0.024f, ey - 0.002f + 0.002f * bl);
    fill_c(C(mul(lit, 0.6f), 1));
  }
  p_begin(P);
  p_move(P, ex - 0.026f, ey - 0.002f + 0.012f * bl);
  p_quad(P, ex + 0.01f, ey - 0.025f + 0.02f * bl, ex + 0.042f, ey - 0.013f);
  stroke_c(RGBA(10, 8, 8, 0.95f), 0.008f);
  p_begin(P);
  p_move(P, ex - 0.018f, ey - 0.026f);
  p_quad(P, ex + 0.012f, ey - 0.042f, ex + 0.045f, ey - 0.026f);
  stroke_c(C(dark, 0.7f), 0.005f);
  /* the brow */
  p_begin(P);
  p_move(P, -0.383f, 0.383f - o->brow_up * 0.01f);
  p_quad(P, -0.33f, 0.36f - o->brow_up * 0.015f, -0.27f, 0.372f);
  stroke_c(C(mul(o->hair_col, 0.7f), 0.95f), 0.022f);
  /* the hair: a cap over the skull with a hairline, sideburn and nape, lifted strands catching the rim light */
  p_alpha(P, o->hair_a > 0 ? o->hair_a : 0.95f);
  p_begin(P);
  curve(o->hair, o->nhair, true);
  fill_c(C(o->hair_col, 1));
  p_alpha(P, 1);
  for (int i = 0; i < o->nhair - 1; i++)
    for (int k = 0; k < 6; k++) {
      const float *a = o->hair[i], *b = o->hair[i + 1];
      float f = k / 6.0f, x = a[0] + (b[0] - a[0]) * f, y = a[1] + (b[1] - a[1]) * f;
      float dx = (rnd() - 0.5f) * 0.03f, dy = (rnd() - 0.2f) * 0.03f;
      p_begin(P);
      p_move(P, x, y);
      p_line(P, x + dx, y + dy);
      stroke_c(C(o->hair_col, 0.5f), 0.006f);
    }
  p_save(P);
  p_begin(P);
  curve(o->hair, o->nhair, true);
  p_clip(P);
  g = LG(-0.4f, 0, 0.45f, 0, S_(0, C(mul(mix(o->hair_col, o->key, 0.3f), 1.3f), 0.6f * o->key_k)), S_(0.5f, RGBA(0, 0, 0, 0)),
         S_(1, C(o->rim, 0.35f)));
  p_fill_rect(P, -1, -0.3f, 2, 1.4f, &g);
  for (int i = 0; i < 70; i++) {
    float a = -0.35f + rnd() * 0.8f, b = -0.05f + rnd() * 0.7f, len = 0.05f + rnd() * 0.1f;
    PColor sc = C(mix(o->hair_col, v3c(200, 210, 230), 0.25f + rnd() * 0.3f), 0.25f);
    p_begin(P);
    p_move(P, a, b);
    p_quad(P, a + len * 0.5f, b - 0.01f, a + len, b + 0.015f);
    stroke_c(sc, 0.004f);
  }
  p_restore(P);
  /* the ear: outer rim, the inner fold, warm light through the edge */
  p_save(P);
  p_translate(P, 0.12f, 0.53f);
  p_rotate(P, 0.18f);
  p_begin(P);
  p_ellipse(P, 0, 0, 0.068f, 0.112f, 0, 0, 2 * PI_F);
  fill_c(C(mix(dark, lit, 0.35f), 1));
  p_begin(P);
  p_ellipse(P, 0, 0, 0.062f, 0.106f, 0, PI_F * 0.9f, PI_F * 1.9f);
  stroke_c(C(mix(lit, v3c(255, 120, 90), 0.25f), 0.55f), 0.012f);
  p_begin(P);
  p_move(P, -0.01f, -0.06f);
  p_quad(P, 0.04f, -0.03f, 0.02f, 0.03f);
  p_quad(P, 0, 0.06f, -0.03f, 0.05f);
  stroke_c(C(dark, 0.9f), 0.012f);
  p_begin(P);
  p_ellipse(P, -0.02f, 0, 0.016f, 0.022f, 0, 0, 2 * PI_F);
  fill_c(C(dark, 1));
  p_restore(P);
  extra(o, lit, dark);
  p_restore(P);
}

/* ---- the figure engine: a skeleton with adult proportions, posed by joint angles, drawn as shaded, tapered limbs ----
 * Units are head heights (crown to chin = 1), the units the head is drawn in. The proportions are adult means from
 * published body measurements: shoulders 1.75 head heights across, upper arm 1.45, forearm 1.16, hand 0.83, the base
 * of the neck 1.25 below the crown. Body space: f forward (the face's side), y down, l toward us; the body is drawn
 * turned FIG_TURN toward us, the head in profile on top of it. The same as the browser game's figure(). */
#define FIG_NX 0.08f
#define FIG_NY 1.25f
#define FIG_SH 0.87f
#define FIG_SHY 0.12f
#define FIG_UPPER 1.45f
#define FIG_FORE 1.16f
#define FIG_PALM 0.46f
#define FIG_FING 0.4f
#define FIG_REL 0.2f
#define FIG_RWR 0.135f
#define FIG_TURN 0.32f

typedef struct { float v[3]; } F3;
static F3 f3(float a, float b, float c) { return (F3){ { a, b, c } }; }
static F3 fadd(F3 a, F3 b) { return f3(a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2]); }
static F3 fmul(F3 a, float k) { return f3(a.v[0] * k, a.v[1] * k, a.v[2] * k); }
static F3 fnorm(F3 a) {
  float l = sqrtf(a.v[0] * a.v[0] + a.v[1] * a.v[1] + a.v[2] * a.v[2]);
  if (l == 0) l = 1;
  return fmul(a, 1 / l);
}
static F3 fperp(F3 u, F3 w, F3 alt) {
  float d = u.v[0] * w.v[0] + u.v[1] * w.v[1] + u.v[2] * w.v[2];
  F3 p = f3(w.v[0] - u.v[0] * d, w.v[1] - u.v[1] * d, w.v[2] - u.v[2] * d);
  return sqrtf(p.v[0] * p.v[0] + p.v[1] * p.v[1] + p.v[2] * p.v[2]) > 1e-3f ? fnorm(p) : alt;
}
typedef struct { float x, y; } F2;
/* body space to the head's drawing */
static F2 to_head(F3 p) {
  return (F2){ FIG_NX - p.v[0] * cosf(FIG_TURN) + p.v[2] * sinf(FIG_TURN), FIG_NY + p.v[1] };
}

/* the outline of a capsule from a to b with radii ra, rb, as one closed path */
static void capsule_path(F2 a, F2 b, float ra, float rb) {
  float dx = b.x - a.x, dy = b.y - a.y, L = sqrtf(dx * dx + dy * dy);
  if (L < 1e-6f) L = 1e-6f;
  float nx = -dy / L, ny = dx / L, ang = atan2f(dy, dx);
  p_begin(P);
  p_move(P, a.x - nx * ra, a.y - ny * ra);
  p_line(P, b.x - nx * rb, b.y - ny * rb);
  for (int i = 1; i <= 12; i++) { /* round the end at b, through the far tip */
    float t = ang - PI_F / 2 + PI_F * i / 12;
    p_line(P, b.x + cosf(t) * rb, b.y + sinf(t) * rb);
  }
  p_line(P, a.x + nx * ra, a.y + ny * ra);
  for (int i = 1; i <= 12; i++) { /* and round the end at a */
    float t = ang + PI_F / 2 + PI_F * i / 12;
    p_line(P, a.x + cosf(t) * ra, a.y + sinf(t) * ra);
  }
  p_close(P);
}

/* one limb: a capsule lit from the face's side (the screens), with a rim on its back edge */
static void limb(F2 a, F2 b, float ra, float rb, V3 base, V3 lit, float key_k, V3 rim) {
  capsule_path(a, b, ra, rb);
  fill_c(C(mul(base, 0.22f), 1));
  float dx = b.x - a.x, dy = b.y - a.y, L = sqrtf(dx * dx + dy * dy);
  if (L < 1e-6f) L = 1e-6f;
  float nx = -dy / L, ny = dx / L, sg = nx < 0 ? 1.0f : -1.0f, r = ra > rb ? ra : rb, mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
  p_save(P);
  capsule_path(a, b, ra, rb);
  p_clip(P);
  Paint g = LG(mx + nx * r * sg, my + ny * r * sg, mx - nx * r * sg, my - ny * r * sg, S_(0, C(mix(base, lit, 0.3f), 0.95f * key_k)),
               S_(0.55f, C(mul(base, 0.55f), 0.6f)), S_(0.85f, C(mul(base, 0.3f), 0.2f)), S_(1, C(rim, 0.55f)));
  p_fill_rect(P, fminf(a.x, b.x) - r * 2, fminf(a.y, b.y) - r * 2, fabsf(dx) + r * 4, fabsf(dy) + r * 4, &g);
  p_restore(P);
}

typedef struct { float flex, abd, elbow, wrist, curl; } ArmPose;
typedef struct { F3 S, E, W, h, pw; float curl; } Arm;
typedef struct { F2 hand[2], wrist[2], elbow[2]; } Joints;

static F3 tilt(F3 q, float lean) {
  float cl = cosf(lean), sl = sinf(lean);
  return f3(q.v[0] * cl + q.v[1] * sl, q.v[1] * cl - q.v[0] * sl, q.v[2]);
}

static Arm arm_rig(const ArmPose *A, float side, float lean) {
  Arm a;
  a.S = tilt(f3(-0.04f, FIG_SHY, side * FIG_SH), lean);
  F3 u = fnorm(f3(sinf(A->flex) * cosf(A->abd), cosf(A->flex) * cosf(A->abd), side * sinf(A->abd)));
  a.E = fadd(a.S, fmul(u, FIG_UPPER));
  F3 pu = fperp(u, f3(1, -0.3f, 0), f3(0, -1, 0));
  F3 w = fnorm(fadd(fmul(u, cosf(A->elbow)), fmul(pu, sinf(A->elbow))));
  a.W = fadd(a.E, fmul(w, FIG_FORE));
  a.pw = fperp(w, f3(0, 1, 0), f3(1, 0, 0));
  a.h = fnorm(fadd(fmul(w, cosf(A->wrist)), fmul(a.pw, sinf(A->wrist))));
  a.curl = A->curl;
  return a;
}

static void crease(F2 p0, F2 p1, float k, float w, float a) {
  float mx = p0.x + (p1.x - p0.x) * k, my = p0.y + (p1.y - p0.y) * k, L = sqrtf((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
  if (L < 1e-6f) L = 1e-6f;
  float nx = -(p1.y - p0.y) / L * 0.16f, ny = (p1.x - p0.x) / L * 0.16f;
  p_begin(P);
  p_move(P, mx + nx, my + ny);
  p_quad(P, mx + (p1.x - p0.x) / L * 0.06f, my + (p1.y - p0.y) / L * 0.06f, mx - nx * 0.6f, my - ny * 0.6f);
  stroke_c(RGBA(0, 0, 0, a), w);
}

static void draw_arm(const Arm *A, V3 sleeve, V3 skin, V3 lit, V3 dark, float key_k, V3 rim) {
  F2 S = to_head(A->S), E = to_head(A->E), W = to_head(A->W), Wc = to_head(fadd(A->W, fmul(A->h, -0.06f)));
  /* the muscles under the sleeve: the shoulder's cap, the upper arm full at the middle, the forearm full near the
   * elbow and narrowing to the wrist */
  F2 M = to_head(fadd(A->S, fmul(fnorm(fadd(A->E, fmul(A->S, -1))), FIG_UPPER * 0.45f)));
  F2 Fm = to_head(fadd(A->E, fmul(fnorm(fadd(A->W, fmul(A->E, -1))), FIG_FORE * 0.3f)));
  limb(S, M, 0.3f, 0.27f, sleeve, lit, key_k, rim);
  limb(M, E, 0.27f, FIG_REL, sleeve, lit, key_k, rim);
  limb(E, Fm, FIG_REL, 0.215f, sleeve, lit, key_k, rim);
  limb(Fm, Wc, 0.215f, FIG_RWR + 0.02f, sleeve, lit, key_k, rim);
  /* the sleeve's creases: across the inside of the elbow, and loose folds down the upper arm and forearm */
  float ae = atan2f(S.y - E.y, S.x - E.x);
  p_begin(P);
  p_ellipse(P, E.x, E.y, FIG_REL * 0.75f, FIG_REL * 0.75f, 0, ae - 0.7f, ae + 0.7f);
  stroke_c(RGBA(0, 0, 0, 0.45f), 0.016f);
  crease(S, E, 0.35f, 0.012f, 0.28f);
  crease(S, E, 0.7f, 0.012f, 0.28f);
  crease(E, W, 0.4f, 0.012f, 0.28f);
  limb(Wc, to_head(fadd(A->W, fmul(A->h, 0.02f))), FIG_RWR + 0.03f, FIG_RWR + 0.03f, mul(sleeve, 0.8f), lit, key_k, rim); /* the cuff */
  /* the hand from the side: the thumb behind, the palm along h, the fingers together, curling in two joints */
  F3 K = fadd(A->W, fmul(A->h, FIG_PALM));
  F2 Kh = to_head(K);
  F3 T0 = fadd(A->W, fmul(A->h, 0.14f)), T1 = fadd(T0, fmul(fnorm(fadd(A->h, fmul(A->pw, -0.7f))), 0.3f));
  limb(to_head(T0), to_head(T1), 0.075f, 0.055f, skin, lit, key_k, rim);
  limb(W, Kh, FIG_RWR, 0.14f, skin, lit, key_k, rim);
  F3 d1 = fnorm(fadd(fmul(A->h, cosf(A->curl * 0.9f)), fmul(A->pw, sinf(A->curl * 0.9f)))), F1 = fadd(K, fmul(d1, FIG_FING * 0.55f));
  F3 d2 = fnorm(fadd(fmul(A->h, cosf(A->curl * 1.9f)), fmul(A->pw, sinf(A->curl * 1.9f)))), Fe = fadd(F1, fmul(d2, FIG_FING * 0.45f));
  limb(Kh, to_head(F1), 0.12f, 0.1f, skin, lit, key_k, rim);
  limb(to_head(F1), to_head(Fe), 0.1f, 0.075f, skin, lit, key_k, rim);
  /* the gaps between the fingers, and the knuckles catching the light */
  static const float GAP[3] = { -0.045f, 0, 0.045f };
  for (int i = 0; i < 3; i++) {
    F2 q0 = to_head(fadd(K, fmul(A->pw, GAP[i]))), q1 = to_head(fadd(F1, fmul(A->pw, GAP[i] * 0.9f))), q2 = to_head(fadd(Fe, fmul(A->pw, GAP[i] * 0.7f)));
    p_begin(P);
    p_move(P, q0.x, q0.y);
    p_quad(P, q1.x, q1.y, q2.x, q2.y);
    stroke_c(C(dark, 0.55f), 0.008f);
  }
  F2 k0 = to_head(fadd(A->W, fmul(A->h, 0.1f)));
  p_begin(P);
  p_move(P, k0.x, k0.y);
  p_line(P, Kh.x, Kh.y);
  stroke_c(C(mix(lit, v3c(255, 255, 255), 0.3f), 0.3f * key_k), 0.012f);
}

typedef void (*HeldFn)(const Joints *J, V3 lit);

/* draws the body of a person (in the head's units) for a pose, the uniform in colour cl; held draws what the near
 * hand holds, between the trunk and that hand */
static void figure(float lean, const ArmPose arms[2], V3 cl, V3 skin, V3 lit, V3 dark, float key_k, HeldFn held) {
  Arm A[2] = { arm_rig(&arms[0], -1, lean), arm_rig(&arms[1], 1, lean) };
  draw_arm(&A[0], cl, skin, lit, dark, key_k, blue_rim); /* the far arm behind the trunk */
  static const float TR[13][3] = { { 0.2f, -0.06f, 0 }, { 0.5f, 0.32f, 0 }, { 0.64f, 0.75f, 0 }, { 0.6f, 1.35f, 0 }, { 0.52f, 1.95f, 0 },
                                   { 0.5f, 2.6f, 0 }, { 0.5f, 2.6f, 0 }, { -0.4f, 2.6f, 0 }, { -0.44f, 1.8f, 0 }, { -0.46f, 0.9f, 0 },
                                   { -0.38f, 0.25f, 0 }, { -0.12f, 0.0f, FIG_SH * 0.9f }, { -0.18f, -0.06f, 0 } };
  float out[13][2];
  for (int i = 0; i < 13; i++) {
    F2 q = to_head(tilt(f3(TR[i][0], TR[i][1], TR[i][2]), lean));
    out[i][0] = q.x, out[i][1] = q.y;
  }
  cloth((const float(*)[2])out, 13, cl, lit, dark, key_k, blue_rim);
  limb((F2){ 0.02f, 0.98f }, (F2){ FIG_NX, FIG_NY }, 0.25f, 0.27f, skin, lit, key_k, blue_rim); /* the neck */
  Joints J;
  for (int i = 0; i < 2; i++) {
    J.hand[i] = to_head(fadd(A[i].W, fmul(A[i].h, FIG_PALM)));
    J.wrist[i] = to_head(A[i].W);
    J.elbow[i] = to_head(A[i].E);
  }
  if (held) held(&J, lit);
  draw_arm(&A[1], cl, skin, lit, dark, key_k, blue_rim);
}

/* the clipboard edge-on in the supervisor's near hand, paper catching the screen light, the steel clip */
static void clipboard(const Joints *J, V3 lit) {
  p_save(P);
  p_translate(P, J->hand[1].x - 0.1f, J->hand[1].y - 0.45f);
  p_rotate(P, -0.12f);
  rect_c(-0.16f, -0.5f, 0.3f, 1.02f, HEX(0x2a2016));
  rect_c(-0.13f, -0.46f, 0.24f, 0.94f, C(mul(mix(v3c(225, 225, 215), lit, 0.45f), 0.75f), 1));
  for (int i = 0; i < 9; i++) {
    p_begin(P);
    p_move(P, -0.11f, -0.34f + i * 0.09f);
    p_line(P, 0.09f, -0.34f + i * 0.09f);
    stroke_c(RGBA(40, 50, 60, 0.45f), 0.008f);
  }
  rect_c(-0.08f, -0.53f, 0.14f, 0.08f, HEX(0x8c949a));
  p_restore(P);
}

static void body(const Person *o, V3 lit, V3 dark) {
  if (o->who == SUPERVISOR) {
    /* standing, the far arm at his side, the near one holding the clipboard against his chest */
    static const ArmPose ARMS[2] = { { 0.08f, 0.1f, 0.3f, 0.1f, 0.5f }, { 0.22f, 0.22f, 1.95f, 0.25f, 0.95f } };
    figure(0.02f, ARMS, v3c(150, 125, 85), o->skin, lit, dark, 0.8f, clipboard);
    p_begin(P); /* the T-shirt at the open collar */
    p_move(P, -0.3f, 1.1f), p_line(P, -0.12f, 1.3f), p_line(P, 0.05f, 1.08f);
    fill_c(RGBA(225, 222, 212, 0.35f));
    p_begin(P);
    p_move(P, -0.42f, 1.12f), p_line(P, -0.14f, 1.42f), p_line(P, 0.12f, 1.06f);
    stroke_c(RGBA(0, 0, 0, 0.6f), 0.02f);
    p_begin(P); /* the chief's anchor */
    p_move(P, -0.4f, 1.22f), p_line(P, -0.34f, 1.33f), p_line(P, -0.28f, 1.22f), p_close(P);
    fill_c(HEX(0xd8b14a));
    fold(-0.3f, 1.9f, -0.2f, 2.4f, -0.3f, 2.9f, 0.5f);
  } else if (o->who == OPERATOR) {
    V3 blue = v3c(42, 60, 92);
    /* seated, leaning in to the scope, both hands forward on the keyboard and the trackball */
    static const ArmPose ARMS[2] = { { 1.05f, -0.08f, 0.7f, -0.15f, 0.5f }, { 0.95f, 0.22f, 0.85f, -0.25f, 0.65f } };
    figure(0.26f, ARMS, blue, o->skin, lit, dark, 1, NULL);
    static const float COL[7][2] = { { -0.24f, 1.0f }, { -0.05f, 1.13f }, { 0.24f, 1.1f }, { 0.32f, 0.97f }, { 0.36f, 1.12f }, { 0.12f, 1.24f }, { -0.26f, 1.17f } };
    cloth(COL, 7, mul(blue, 0.8f), lit, dark, 1, blue_rim); /* the collar */
    fold(0.4f, 1.7f, 0.5f, 2.2f, 0.42f, 2.8f, 0.4f);
    rect_c(0.3f, 1.5f, 0.18f, 0.13f, RGBA(15, 22, 34, 0.9f)); /* the shoulder patch */
    stroke_rect(0.3f, 1.5f, 0.18f, 0.13f, C(blue_rim, 0.35f), 0.01f);
  } else {
    float g = ease(clampf_((T - L3[0]) / 0.4f, 0, 1));
    /* in his chair, the far forearm on the armrest; giving the order, the near hand comes up and opens toward the deck */
    ArmPose arms[2] = { { 0.05f, 0.08f, 1.5f, 0.1f, 0.6f }, { -0.05f + 0.4f * g, 0.25f, 1.45f + 0.55f * g, 0.1f - 0.35f * g, 0.6f - 0.45f * g } };
    figure(-0.08f, arms, v3c(160, 135, 92), o->skin, lit, dark, 0.85f, NULL);
    p_begin(P);
    p_move(P, -0.3f, 1.08f), p_line(P, -0.1f, 1.3f), p_line(P, 0.08f, 1.05f);
    fill_c(RGBA(230, 226, 215, 0.4f));
    p_begin(P);
    p_move(P, -0.42f, 1.1f), p_line(P, -0.12f, 1.42f), p_line(P, 0.16f, 1.05f);
    stroke_c(RGBA(0, 0, 0, 0.6f), 0.02f);
    p_begin(P); /* the silver oak leaf on the collar point */
    p_ellipse(P, -0.36f, 1.25f, 0.05f, 0.028f, -0.6f, 0, 2 * PI_F);
    fill_c(HEX(0xc9ced4));
    p_begin(P);
    p_move(P, -0.4f, 1.28f), p_line(P, -0.32f, 1.22f);
    stroke_c(RGBA(60, 64, 70, 0.8f), 0.006f);
    stroke_rect(-0.5f, 1.75f, 0.3f, 0.3f, RGBA(0, 0, 0, 0.5f), 0.015f); /* the shirt pocket and a pen */
    rect_c(-0.46f, 1.62f, 0.028f, 0.16f, HEX(0x16181c));
    rect_c(-0.46f, 1.62f, 0.028f, 0.03f, HEX(0xb8bcc0));
  }
}

static void extra(const Person *o, V3 lit, V3 dark) {
  (void)dark;
  if (o->who == OPERATOR) {
    /* the headset: the band over the crown, the padded earcup, the boom mic to the corner of the mouth, the transmit light */
    p_begin(P);
    p_move(P, 0.12f, 0.47f), p_quad(P, 0.2f, -0.1f, 0.0f, -0.06f);
    stroke_c(HEX(0x15181b), 0.045f);
    p_begin(P);
    p_move(P, 0.16f, 0.3f), p_quad(P, 0.2f, -0.08f, 0.02f, -0.07f);
    stroke_c(C(blue_rim, 0.5f), 0.01f);
    Paint g = RG(0.11f, 0.53f, 0.01f, 0.12f, S_(0, HEX(0x2a3036)), S_(1, HEX(0x08090a)));
    p_begin(P);
    p_ellipse(P, 0.12f, 0.53f, 0.095f, 0.115f, 0.15f, 0, 2 * PI_F);
    p_fill(P, &g);
    p_begin(P);
    p_ellipse(P, 0.12f, 0.53f, 0.095f, 0.115f, 0.15f, PI_F * 0.8f, PI_F * 1.3f);
    stroke_c(C(lit, 0.35f), 0.01f);
    p_begin(P);
    p_move(P, 0.06f, 0.6f), p_quad(P, -0.15f, 0.76f, -0.35f, 0.75f);
    stroke_c(HEX(0x1b1f23), 0.018f);
    p_begin(P);
    p_move(P, 0.04f, 0.6f), p_quad(P, -0.15f, 0.745f, -0.34f, 0.74f);
    stroke_c(C(lit, 0.3f), 0.005f);
    p_begin(P);
    p_ellipse(P, -0.36f, 0.75f, 0.026f, 0.019f, 0, 0, 2 * PI_F);
    fill_c(HEX(0x0a0b0c));
    rect_c(0.05f, 0.6f, 0.02f, 0.02f, RGBA(255, 60, 40, o->open > 0.05f ? 0.9f : 0.25f));
  } else if (o->who == COMMANDER) {
    /* the ball cap: navy, the gold oak leaves on the bill, the ship's number in gold thread */
    static const float CAP[10][2] = { { -0.385f, 0.29f }, { -0.38f, 0.12f }, { -0.24f, -0.03f }, { 0.02f, -0.07f }, { 0.28f, -0.02f },
                                      { 0.43f, 0.13f }, { 0.455f, 0.33f }, { 0.38f, 0.35f }, { 0.05f, 0.33f }, { -0.25f, 0.31f } };
    p_begin(P);
    curve(CAP, 10, true);
    fill_c(HEX(0x0d1320));
    p_save(P);
    p_begin(P);
    curve(CAP, 10, true);
    p_clip(P);
    Paint g = LG(-0.4f, 0, 0.45f, 0, S_(0, C(mix(v3c(30, 44, 72), lit, 0.3f), 0.9f)), S_(0.6f, RGBA(20, 28, 44, 0.2f)), S_(1, C(blue_rim, 0.25f)));
    p_fill_rect(P, -1, -0.3f, 2, 1, &g);
    p_begin(P);
    p_move(P, 0.0f, -0.09f), p_quad(P, 0.08f, 0.1f, 0.06f, 0.31f);
    stroke_c(RGBA(0, 0, 0, 0.5f), 0.01f);
    /* the ship's number: the text is drawn mirrored back so it reads, whichever way the head faces */
    p_save(P);
    p_scale(P, o->dir, 1);
    p_text(P, "CVN-99", -0.2f * o->dir, 0.18f, 0.045f, RGBA(214, 170, 70, 0.9f), 1);
    p_restore(P);
    p_restore(P);
    p_begin(P);
    p_ellipse(P, 0.02f, -0.07f, 0.014f, 0.014f, 0, 0, 2 * PI_F);
    fill_c(HEX(0x1a2030));
    static const float BILL[6][2] = { { -0.36f, 0.25f }, { -0.52f, 0.27f }, { -0.68f, 0.32f }, { -0.66f, 0.35f }, { -0.46f, 0.335f }, { -0.34f, 0.32f } };
    g = LG(-0.68f, 0, -0.34f, 0, S_(0, C(mix(v3c(24, 34, 58), lit, 0.45f), 1)), S_(1, HEX(0x0c1220)));
    p_begin(P);
    curve(BILL, 6, true);
    p_fill(P, &g);
    p_begin(P);
    curve(BILL, 3, false);
    stroke_c(C(mix(lit, v3c(255, 255, 255), 0.2f), 0.55f * 0.85f), 0.01f);
    p_begin(P);
    for (int i = 0; i <= 8; i++) {
      float f = i / 8.0f, x = -0.38f - f * 0.26f, y = 0.29f + f * 0.035f + (i % 2 ? -0.01f : 0.01f);
      if (i) p_line(P, x, y);
      else p_move(P, x, y);
    }
    stroke_c(RGBA(214, 170, 70, 0.9f), 0.012f);
    g = LG(0, 0.26f, 0, 0.5f, S_(0, RGBA(0, 0, 0, 0.6f)), S_(1, RGBA(0, 0, 0, 0)));
    p_begin(P); /* the bill's shadow over his eyes */
    p_move(P, -0.52f, 0.33f), p_line(P, -0.25f, 0.32f), p_line(P, -0.25f, 0.5f), p_line(P, -0.47f, 0.5f);
    p_fill(P, &g);
  }
}

/* ---- the room ------------------------------------------------------------------ */

static void screen(float x, float y, float w, float h) {
  rect_c(x - 10, y - 10, w + 20, h + 20, HEX(0x0d1116));
  rect_c(x, y, w, h, HEX(0x01060c));
  glow(x + w / 2, y + h / 2, w * 0.9f, v3c(30, 80, 140), 0.1f);
}

static void room(float u, bool alert, float gq) {
  Paint g = LG(0, 0, 0, 900, S_(0, HEX(0x05080c)), S_(0.25f, HEX(0x0a1119)), S_(0.68f, HEX(0x0b1219)), S_(0.7f, HEX(0x06090c)), S_(1, HEX(0x020304)));
  p_fill_rect(P, -400, -200, 2400, 1300, &g);
  /* the deckhead: pipe runs, a cable tray and the blue overhead lights */
  static const struct { float y, r; float c[3]; } PIPES[4] = { { 34, 15, { 28, 34, 42 } }, { 66, 10, { 44, 40, 34 } }, { 92, 7, { 30, 38, 46 } }, { 112, 5, { 52, 24, 22 } } };
  for (int i = 0; i < 4; i++) {
    V3 col = v3c(PIPES[i].c[0], PIPES[i].c[1], PIPES[i].c[2]);
    float y = PIPES[i].y, r = PIPES[i].r;
    g = LG(0, y - r, 0, y + r, S_(0, C(mul(col, 0.5f), 1)), S_(0.35f, C(mul(col, 1.8f), 1)), S_(1, C(mul(col, 0.25f), 1)));
    p_fill_rect(P, -400, y - r, 2400, r * 2, &g);
    for (float x = -360; x < 2000; x += 230) rect_c(x, y - r - 2, 12, r * 2 + 4, HEX(0x0c1014));
  }
  rect_c(-400, 122, 2400, 16, HEX(0x0a0d10));
  for (float x = -390; x < 2000; x += 9) {
    float a = rnd(), b = rnd(), c2 = rnd();
    rect_c(x, 124, 7, 10, RGBA(14 + a * 10, 16 + b * 10, 20 + c2 * 12, 1));
  }
  static const float LX[5] = { -120, 340, 800, 1260, 1720 };
  for (int i = 0; i < 5; i++) {
    rect_c(LX[i] - 70, 140, 140, 9, HEX(0x16314f));
    glow(LX[i], 150, 230, v3c(40, 90, 170), 0.22f);
  }
  /* bulkhead panels with seams and rivets */
  for (float x = -390; x < 2000; x += 140) {
    p_begin(P);
    p_move(P, x, 160), p_line(P, x, 610);
    stroke_c(RGBA(0, 0, 0, 0.55f), 2);
  }
  for (float x = -385; x < 2000; x += 140)
    for (float y = 170; y < 610; y += 48) rect_c(x, y, 2, 2, RGBA(90, 120, 150, 0.12f));
  /* the tactical plot: sea, a coast, the grid, own ship, the CAP station and the two hostile tracks with speed leaders */
  float px = 300, py = 190, pw = 470, ph = 300;
  screen(px, py, pw, ph);
  p_save(P);
  p_begin(P);
  p_rect(P, px, py, pw, ph);
  p_clip(P);
  rect_c(px, py, pw, ph, HEX(0x031426));
  float coast[6][2] = { { px - 20, py + 40 }, { px + 90, py + 20 }, { px + 160, py + 55 }, { px + 230, py + 30 }, { px + 300, py - 10 }, { px - 20, py - 20 } };
  p_begin(P);
  curve((const float(*)[2])coast, 6, true);
  fill_c(HEX(0x0c2414));
  p_begin(P);
  curve((const float(*)[2])coast, 5, false);
  stroke_c(RGBA(70, 200, 120, 0.6f), 1.5f);
  for (int i = 1; i < 8; i++) {
    p_begin(P);
    p_move(P, px + pw * i / 8, py), p_line(P, px + pw * i / 8, py + ph);
    stroke_c(RGBA(90, 150, 220, 0.16f), 1);
  }
  for (int i = 1; i < 5; i++) {
    p_begin(P);
    p_move(P, px, py + ph * i / 5), p_line(P, px + pw, py + ph * i / 5);
    stroke_c(RGBA(90, 150, 220, 0.16f), 1);
  }
  float ox = px + pw * 0.56f, oy = py + ph * 0.7f;
  PColor own = RGBA(110, 170, 255, 0.9f);
  p_begin(P), p_ellipse(P, ox, oy, 7, 7, 0, 0, 2 * PI_F), stroke_c(own, 2);
  p_begin(P), p_ellipse(P, ox, oy, 1.5f, 1.5f, 0, 0, 2 * PI_F), stroke_c(own, 2);
  for (int k = 0; k < 48; k += 2) { /* the dashed range ring */
    p_begin(P);
    p_ellipse(P, ox, oy, 120, 120, 0, k * 2 * PI_F / 48, (k + 1) * 2 * PI_F / 48);
    stroke_c(own, 2);
  }
  p_begin(P), p_ellipse(P, ox + 90, oy - 120, 8, 8, 0, PI_F, 0), stroke_c(own, 2);
  mono("CAP 1", ox + 104, oy - 124, 11, own, ALIGN_LEFT);
  static const float TR[2][2] = { { -0.11f, 0 }, { -0.06f, 0.03f } };
  for (int i = 0; i < 2; i++) {
    float hx = ox + pw * TR[i][0] - 4, hy = py + ph * (0.14f + TR[i][1]) + u * ph * 0.3f;
    for (int j = 1; j <= 4; j++) /* the track's history: where it was plotted before */
      rect_c(hx - 1.5f, py + ph * (0.14f + TR[i][1]) + clampf_((T - j * 0.5f - S0) / (S1 - S0), 0, 1) * ph * 0.3f - 1.5f, 3, 3,
             RGBA(255, 70, 60, 0.35f * (1 - j / 5.0f)));
    bool on = !alert || gq > 0.25f;
    PColor hc = RGBA(255, 70, 60, on ? 0.95f : 0.4f);
    p_begin(P);
    p_move(P, hx - 8, hy + 3), p_line(P, hx, hy - 7), p_line(P, hx + 8, hy + 3);
    stroke_c(hc, 2);
    p_begin(P);
    p_move(P, hx, hy), p_line(P, hx + 7, hy + 38);
    stroke_c(hc, 2);
  }
  if (T > 6.0f) mono("7402  HOSTILE", ox - pw * 0.06f, py + ph * 0.08f, 11, RGBA(255, 110, 90, 0.95f), ALIGN_LEFT);
  p_restore(P);
  mono("TACTICAL PLOT   SCALE 64", px, py - 22, 12, RGBA(120, 170, 220, 0.7f), ALIGN_LEFT);
  /* the track list, its top rows going red once the tracks are classified */
  float tx = 820, ty = 190, tw = 330, th = 300;
  screen(tx, ty, tw, th);
  static const char *const ROWS[7][6] = { { "TN", "ID", "BRG", "RNG", "SPD", "ALT" }, { "7402", "HOS", "350", "62", "540", "2000" },
                                          { "7403", "HOS", "349", "64", "540", "2000" }, { "1101", "FRD", "045", "28", "310", "18000" },
                                          { "0212", "NEU", "180", "41", "12", "0" }, { "0214", "NEU", "210", "55", "18", "0" },
                                          { "3307", "UNK", "095", "88", "420", "31000" } };
  for (int i = 0; i < 7; i++) {
    float y = ty + 24 + i * 34;
    bool hot = i == 1 || i == 2;
    if (hot && T > 6.0f) rect_c(tx + 6, y - 13, tw - 12, 26, RGBA(200, 30, 30, alert ? 0.25f + 0.3f * gq : 0.25f));
    for (int j = 0; j < 6; j++)
      mono(ROWS[i][j], tx + 16 + j * 52, y, 12,
           i == 0 ? RGBA(120, 170, 220, 0.8f) : hot && T > 6.0f ? HEX(0xff8a72) : RGBA(170, 200, 220, 0.75f), ALIGN_LEFT);
  }
  mono("AIR TRACKS", tx, ty - 22, 12, RGBA(120, 170, 220, 0.7f), ALIGN_LEFT);
  /* the air plan / status board */
  float ax = 1200, ay = 190, aw = 300, ah = 300;
  screen(ax, ay, aw, ah);
  mono("AIR PLAN     05:47", ax + 16, ay + 26, 13, RGBA(120, 170, 220, 0.85f), ALIGN_LEFT);
  mono("ALERT 5   VIPER 1  NF-24", ax + 16, ay + 66, 12, alert ? HEX(0xffd25a) : RGBA(170, 200, 220, 0.7f), ALIGN_LEFT);
  mono("ALERT 5   VIPER 2  NF-24", ax + 16, ay + 94, 12, alert ? HEX(0xffd25a) : RGBA(170, 200, 220, 0.7f), ALIGN_LEFT);
  mono("ALERT 30  VIPER 3  NF-24", ax + 16, ay + 122, 12, RGBA(170, 200, 220, 0.5f), ALIGN_LEFT);
  mono("WIND 18 KT   DECK GREEN", ax + 16, ay + 162, 12, RGBA(120, 220, 150, 0.7f), ALIGN_LEFT);
  if (alert) {
    rect_c(ax + 8, ay + 196, aw - 16, 40, RGBA(210, 30, 25, 0.35f + 0.5f * gq));
    mono(T >= L3[0] ? "LAUNCH THE ALERT" : "GENERAL QUARTERS", ax + aw / 2, ay + 216, 15, HEX(0xfff2e8), ALIGN_CENTER);
  } else mono("CONDITION III", ax + 16, ay + 216, 13, RGBA(170, 200, 220, 0.6f), ALIGN_LEFT);
  /* the far row of consoles and two crew at work, backs to us */
  rect_c(-400, 560, 2400, 90, HEX(0x080b0e));
  for (float x = -300; x < 1900; x += 190) {
    rect_c(x, 540, 160, 30, HEX(0x10161c));
    float a = rnd(), b = rnd();
    rect_c(x + 14, 546, 132, 18, RGBA(20, 60 + a * 40, 90 + b * 50, 1));
    glow(x + 80, 556, 110, v3c(40, 110, 170), 0.12f);
  }
  static const float CX[2] = { 560, 1140 };
  for (int i = 0; i < 2; i++) {
    float x = CX[i];
    p_begin(P), p_ellipse(P, x, 548, 22, 26, 0, 0, 2 * PI_F), fill_c(HEX(0x05070a));
    p_begin(P), p_ellipse(P, x, 600, 46, 32, 0, PI_F, 0), fill_c(HEX(0x05070a));
    rect_c(x - 46, 600, 92, 60, HEX(0x05070a));
    p_begin(P), p_ellipse(P, x, 548, 22, 26, 0, PI_F * 1.1f, PI_F * 1.7f), stroke_c(RGBA(80, 150, 220, 0.35f), 2);
  }
  g = LG(0, 640, 0, 900, S_(0, HEX(0x07090b)), S_(1, HEX(0x010203)));
  p_fill_rect(P, -400, 640, 2400, 300, &g);
}

/* the operator's console: the radar repeater angled toward him, keyboard, trackball, the phone, a coffee cup */
static void console(float u) {
  Paint g = LG(0, 600, 0, 900, S_(0, HEX(0x1a2128)), S_(0.08f, HEX(0x0c1014)), S_(1, HEX(0x040506)));
  p_begin(P);
  p_move(P, -200, 600), p_line(P, 820, 620), p_line(P, 900, 900), p_line(P, -200, 900);
  p_fill(P, &g);
  p_begin(P);
  p_move(P, 60, 250), p_line(P, 640, 290), p_line(P, 660, 640), p_line(P, 40, 640), p_close(P);
  fill_c(HEX(0x11161b));
  stroke_c(RGBA(120, 150, 180, 0.15f), 2);
  /* the display, sheared so it faces him */
  p_save(P);
  p_transform(P, 1, 0.065f, -0.03f, 1, 0, 0);
  float dx = 100, dy = 285, dw = 500, dh = 330, rx = dx + dw * 0.42f, ry = dy + dh * 0.5f, rr = dh * 0.44f;
  rect_c(dx, dy, dw, dh, HEX(0x010805));
  p_save(P);
  p_begin(P);
  p_rect(P, dx, dy, dw, dh);
  p_clip(P);
  static const float RK[4] = { 0.25f, 0.5f, 0.75f, 1 };
  for (int i = 0; i < 4; i++) p_begin(P), p_ellipse(P, rx, ry, rr * RK[i], rr * RK[i], 0, 0, 2 * PI_F), stroke_c(RGBA(60, 255, 140, 0.25f), 1.2f);
  for (int i = 0; i < 72; i++) {
    float a = i * PI_F / 36, k = i % 6 ? 0.96f : 0.92f;
    p_begin(P);
    p_move(P, rx + sinf(a) * rr * k, ry - cosf(a) * rr * k), p_line(P, rx + sinf(a) * rr, ry - cosf(a) * rr);
    stroke_c(RGBA(60, 255, 140, 0.25f), 1.2f);
  }
  static const char *const BL[4] = { "000", "090", "180", "270" };
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * PI_F / 180;
    mono(BL[i], rx + sinf(a) * rr * 1.1f, ry - cosf(a) * rr * 1.1f, 11, RGBA(80, 220, 140, 0.7f), ALIGN_CENTER);
  }
  float sw = ((T - 2.2f) / 2.6f) * PI_F * 2;
  for (int i = 0; i < 30; i++) {
    float a = sw - i * 0.03f;
    p_begin(P);
    p_move(P, rx, ry), p_line(P, rx + sinf(a) * rr, ry - cosf(a) * rr);
    stroke_c(RGBA(80, 255, 150, 0.4f * (1 - i / 30.0f)), 3);
  }
  for (int i = 0; i < 40; i++) { /* sea clutter */
    float a = rnd() * PI_F * 2, d = 0.3f + rnd() * 0.6f;
    rect_c(rx + sinf(a) * rr * d, ry - cosf(a) * rr * d, 2, 2, RGBA(60, 200, 110, 0.18f));
  }
  PColor own = RGBA(110, 170, 255, 0.95f);
  p_begin(P), p_ellipse(P, rx, ry, 6, 6, 0, 0, 2 * PI_F), stroke_c(own, 2);
  p_begin(P), p_ellipse(P, rx + rr * 0.3f, ry - rr * 0.42f, 7, 7, 0, PI_F, 0), stroke_c(own, 2);
  /* the two contacts close from the rim toward the ship over the shot; each sweep leaves an echo where they were */
  static const float CT[2][2] = { { -10, 0.84f }, { -7, 0.9f } };
  for (int i = 0; i < 2; i++) {
    float a = CT[i][0] * PI_F / 180, d = range_at(CT[i][1], T), hx = rx + sinf(a) * rr * d, hy = ry - cosf(a) * rr * d;
    for (int j = 5; j >= 1; j--) {
      float dj = range_at(CT[i][1], T - j * 0.45f);
      p_begin(P);
      p_ellipse(P, rx + sinf(a) * rr * dj, ry - cosf(a) * rr * dj, 3, 3, 0, 0, 2 * PI_F);
      fill_c(RGBA(255, 96, 72, 0.4f * (1 - j / 6.0f)));
    }
    float since = fmodf(fmodf(sw - a, PI_F * 2) + PI_F * 2, PI_F * 2) / (PI_F * 2) * 2.6f, k = expf(-since * 1.2f);
    PColor hc = RGBA(255, 80, 64, 0.55f + 0.45f * k);
    p_begin(P), p_move(P, hx - 8, hy + 4), p_line(P, hx, hy - 7), p_line(P, hx + 8, hy + 4), stroke_c(hc, 2.5f);
    p_begin(P), p_move(P, hx, hy), p_line(P, hx + 3, hy + 30), stroke_c(hc, 2.5f);
  }
  if (T > 5.9f) {
    float a = -8.5f * PI_F / 180, d = range_at(0.87f, T), hx = rx + sinf(a) * rr * d, hy = ry - cosf(a) * rr * d, hs = 20;
    stroke_rect(hx - hs, hy - hs, hs * 2, hs * 2, RGBA(255, 220, 90, 0.9f), 1.5f);
    float bx = dx + dw * 0.79f, by = dy + 26;
    rect_c(bx - 8, by - 14, 112, 128, RGBA(0, 30, 15, 0.85f));
    static const char *const HK[7] = { "HOOK 7402", "HOSTILE AIR", "BRG  350", "RNG  62", "SPD  540", "ALT  2000", "CPA  4 MIN" };
    for (int i = 0; i < 7; i++) mono(HK[i], bx, by + i * 17, 11, i < 2 ? HEX(0xff8a72) : RGBA(120, 255, 170, 0.9f), ALIGN_LEFT);
  }
  p_restore(P);
  rect_c(dx, dy, dw, dh * 0.4f, RGBA(160, 255, 200, 0.03f)); /* the glass */
  p_restore(P);
  glow(360, 470, 520, v3c(50, 220, 140), 0.18f);
  /* the keyboard, the trackball, the phone handset in its cradle and a coffee cup */
  p_begin(P);
  p_move(P, 120, 648), p_line(P, 560, 652), p_line(P, 580, 700), p_line(P, 100, 696);
  fill_c(HEX(0x1a2026));
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 16; j++) rect_c(126 + j * 27 + i * 3, 655 + i * 10, 22, 7, RGBA(140, 200, 180, 0.12f));
  g = RG(632, 668, 2, 26, S_(0, HEX(0x7a8e96)), S_(0.4f, HEX(0x2c3a40)), S_(1, HEX(0x0b1013)));
  p_begin(P), p_ellipse(P, 632, 670, 24, 14, 0, 0, 2 * PI_F), p_fill(P, &g);
  p_begin(P), p_ellipse(P, 30, 640, 26, 70, 0.4f, 0, 2 * PI_F), fill_c(HEX(0x0e1215));
  p_begin(P), p_ellipse(P, 30, 640, 26, 70, 0.4f, PI_F * 1.2f, PI_F * 1.8f), stroke_c(RGBA(120, 200, 170, 0.25f), 2);
  g = LG(70, 0, 104, 0, S_(0, HEX(0x2a2f33)), S_(0.5f, HEX(0x4a5258)), S_(1, HEX(0x15191c)));
  p_fill_rect(P, 70, 600, 34, 46, &g);
  p_begin(P), p_ellipse(P, 87, 600, 17, 4, 0, 0, 2 * PI_F), fill_c(HEX(0x0d0f10));
  for (int i = 0; i < 3; i++) { /* the steam off the coffee */
    float k = fmodf(T * 0.25f + i / 3.0f, 1);
    p_begin(P);
    p_move(P, 85 + i * 4, 595 - k * 50);
    p_quad(P, 80 + sinf(T * 2 + i) * 8, 575 - k * 50, 88, 555 - k * 50);
    stroke_c(RGBA(180, 190, 200, 0.15f), 1.5f);
  }
}

void cic_draw(Framebuffer *out, float t, float t0, float t1) {
  T = t;
  S0 = t0, S1 = t1;
  float u = clampf_((t - t0) / (t1 - t0), 0, 1);
  bool alert = t >= L2[0];
  float gq = alert ? 0.5f + 0.5f * sinf((t - L2[0]) * PI_F * 2 / 1.1f) : 0;
  R.state = 4711;
  int W = out->w, H = out->h;
  /* it opens tight on the two contacts on the scope, then pulls out to the operator and the officers behind him */
  float zo = ease(clampf_((t - 6.25f) / 1.35f, 0, 1)), zw = 1.04f + 0.1f * ease(u), zc = 3.6f + 0.35f * (t - t0);
  float sc = H / 900.0f, z = expf(logf(zc) + (logf(zw) - logf(zc)) * zo);
  float wx = 760 + 260 * ease(u) + 120 * ease(clampf_((t - L3[0]) / 1.1f, 0, 1)), wy = 450 + 10 * sinf(t * 0.7f);
  float camx = 272 + (wx - 272) * zo, camy = 392 + (wy - 392) * zo;
  blue_rim = alert ? mix(v3c(90, 150, 230), v3c(255, 70, 60), gq * 0.5f) : v3c(90, 150, 230);
  red_k = alert ? gq * 0.25f : 0;
  /* the room on its own layer, softly out of focus behind the people */
  if (!fb_resize(&bg, W, H)) return;
  fb_clip_reset(&bg);
  fb_clear(&bg, hexc(0x030507));
  if (!P) P = painter_new(&bg);
  if (!P) return;
  painter_begin(P, &bg);
  p_translate(P, W / 2.0f, H / 2.0f);
  p_scale(P, sc * z, sc * z);
  p_translate(P, -camx, -camy);
  room(u, alert, gq);
  p_blur(&bg, (int)lroundf(1.3f * sc) > 0 ? (int)lroundf(1.3f * sc) : 1, 0.6f);
  memcpy(out->px, bg.px, sizeof(uint32_t) * (size_t)W * H);
  /* the people and the console, sharp, over it */
  painter_begin(P, out);
  p_translate(P, W / 2.0f, H / 2.0f);
  p_scale(P, sc * z, sc * z);
  p_translate(P, -camx, -camy);
  V3 key_red = v3c(255, 80, 60);
  /* the supervisor: a chief in khaki, standing behind the operator, turned toward the commander, clipboard in hand */
  {
    static const float HAIR[17][2] = { { -0.33f, 0.15f }, { -0.34f, 0.07f }, { -0.2f, -0.025f }, { 0.03f, -0.03f }, { 0.27f, 0.035f }, { 0.4f, 0.16f },
                                       { 0.435f, 0.36f }, { 0.415f, 0.56f }, { 0.36f, 0.66f }, { 0.25f, 0.62f }, { 0.19f, 0.52f }, { 0.12f, 0.42f },
                                       { 0.05f, 0.45f }, { 0.035f, 0.53f }, { 0.0f, 0.53f }, { -0.06f, 0.36f }, { -0.2f, 0.22f } };
    float step = ease(clampf_((t - L2[0]) / 0.4f, 0, 1));
    Person o = { SUPERVISOR, 1010 + 8 * step, 262 + 4 * sinf(t * 1.5f) - 3 * step, 118, -1, v3c(190, 140, 110), mix(v3c(140, 190, 255), key_red, red_k),
                 blue_rim, v3c(40, 30, 25), v3c(46, 40, 36), 0.75f, speak(L2, 22), blink_at(0.3f), 0.7f, 0, alert ? 1.0f : 0.0f, 0.8f, true,
                 HAIR, 17 };
    person(&o);
  }
  console(u);
  /* the air detector: young, headset on, seated, leaning in to his scope */
  {
    static const float HAIR[17][2] = { { -0.34f, 0.15f }, { -0.35f, 0.07f }, { -0.2f, -0.03f }, { 0.03f, -0.04f }, { 0.27f, 0.035f }, { 0.41f, 0.17f },
                                       { 0.445f, 0.37f }, { 0.425f, 0.58f }, { 0.35f, 0.72f }, { 0.27f, 0.72f }, { 0.2f, 0.58f }, { 0.12f, 0.42f },
                                       { 0.05f, 0.46f }, { 0.035f, 0.58f }, { 0.0f, 0.58f }, { -0.06f, 0.37f }, { -0.2f, 0.22f } };
    float lean = ease(clampf_((t - 5.3f) / 0.6f, 0, 1)) * 10 - ease(clampf_((t - L2[0]) / 0.5f, 0, 1)) * 6;
    Person o = { OPERATOR, 760 - lean, 318 + lean * 0.3f + 2 * sinf(t * 1.7f), 150, 1, v3c(205, 160, 128), mix(v3c(110, 255, 180), key_red, red_k),
                 blue_rim, v3c(60, 40, 25), v3c(34, 26, 20), 1, speak(L1, 46), blink_at(0.7f), 0.1f, 0, t > 5.5f && t < 7 ? 1.0f : 0.0f, 0, false,
                 HAIR, 17 };
    person(&o);
  }
  /* his chair back, between us and him */
  {
    Paint g = LG(905, 0, 995, 0, S_(0, HEX(0x0a0c0e)), S_(0.75f, HEX(0x171b20)), S_(1, HEX(0x060708)));
    static const float BACK[7][2] = { { 915, 900 }, { 905, 600 }, { 918, 545 }, { 950, 528 }, { 985, 545 }, { 995, 610 }, { 992, 900 } };
    p_begin(P);
    curve(BACK, 7, true);
    p_fill(P, &g);
    p_begin(P);
    curve(BACK + 3, 4, false);
    stroke_c(C(blue_rim, 0.35f), 3);
  }
  /* the commander: a senior officer in khaki and the ship's ball cap, in his chair close to us, listening, then giving the order */
  {
    float turn = ease(clampf_((t - 7.3f) / 0.7f, 0, 1));
    float nod = t > L3[0] + 0.55f ? sinf(clampf_((t - L3[0] - 0.55f) / 0.35f, 0, 1) * PI_F) * 4 : 0;
    Paint g = LG(1450, 0, 1640, 0, S_(0, HEX(0x08090b)), S_(0.5f, HEX(0x15181c)), S_(1, HEX(0x050607)));
    static const float CHR[5][2] = { { 1460, 430 }, { 1560, 410 }, { 1650, 450 }, { 1660, 900 }, { 1450, 900 } };
    p_begin(P);
    curve(CHR, 5, true);
    p_fill(P, &g);
    p_begin(P);
    curve(CHR, 4, false);
    stroke_c(C(blue_rim, 0.3f), 3);
    static const float HAIR[12][2] = { { -0.06f, 0.3f }, { 0.2f, 0.28f }, { 0.4f, 0.3f }, { 0.43f, 0.5f }, { 0.36f, 0.7f }, { 0.26f, 0.72f },
                                       { 0.2f, 0.55f }, { 0.12f, 0.42f }, { 0.05f, 0.45f }, { 0.035f, 0.55f }, { 0.0f, 0.55f }, { -0.06f, 0.38f } };
    Person o = { COMMANDER, 1395 - turn * 10, 246 + 3 * sinf(t * 1.3f) - turn * 4 + nod, 205, 1, v3c(196, 150, 120), mix(v3c(130, 180, 240), key_red, red_k),
                 blue_rim, v3c(70, 80, 90), v3c(140, 138, 134), 0.85f - 0.1f * turn, speak(L3, 14), blink_at(0.1f), 1, turn, 0, 0, true,
                 HAIR, 12 };
    person(&o);
  }
  /* light: bloom from the screens, the room's falloff, the alarm's pulse */
  p_additive(P, true);
  glow(535, 340, 420, v3c(40, 90, 170), 0.06f);
  glow(985, 340, 300, v3c(40, 90, 170), 0.05f);
  glow(1350, 340, 300, v3c(40, 90, 170), 0.05f);
  if (alert) glow(1350, 400, 380, v3c(255, 40, 30), 0.08f * gq);
  p_additive(P, false);
  p_reset_transform(P);
  Paint vig = RG(W / 2.0f, H / 2.0f, H * 0.4f, W * 0.75f, S_(0, RGBA(0, 0, 0, 0)), S_(1, RGBA(0, 0, 0, 0.7f)));
  p_fill_rect(P, 0, 0, (float)W, (float)H, &vig);
}

