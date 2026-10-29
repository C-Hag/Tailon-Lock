/* cic.c - the carrier's combat information centre in the opening film,
 * 05:47, drawn entirely in code on our painter (paint.c): a dark, blue-lit
 * room with its wall plots and consoles, and three people in 3D from the
 * figure engine (figure.c), lit by their screens: the radar operator, the
 * chief supervisor and the commander. One
 * slow take: tight on the two contacts on the scope, a pull back to the
 * operator, then a drift to the officers behind him. The same scene as the
 * browser game's (web/index.html, drawCIC), ported to C. Original code for
 * this project. */
#include "cic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/mathx.h"
#include "figure.h"
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

static V3 blue_rim;
static float red_k, L1[2] = { 5.6f, 7.0f }, L2[2] = { 7.05f, 8.05f }, L3[2] = { 8.1f, 9.2f };

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

/* ---- the crew in 3D, from the figure engine (figure.c) ----
 * A camera at eye height looks into the room; each person is placed so the crown of his head lands where the scene
 * wants it, the head s scene units tall. The same as the browser game's crew3d(). */
static const FigCam CAM = { { 0, 1.25f, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, 1100, 800, 450 };
static float seam; /* how far each facet is grown, in scene units, so no seams show between them */

static void fill_tri(const float pts[3][2], const float rgb[3], void *ud) {
  (void)ud;
  float cx = (pts[0][0] + pts[1][0] + pts[2][0]) / 3, cy = (pts[0][1] + pts[1][1] + pts[2][1]) / 3;
  p_begin(P);
  for (int i = 0; i < 3; i++) {
    float dx = pts[i][0] - cx, dy = pts[i][1] - cy, l = sqrtf(dx * dx + dy * dy);
    float k = l > 1e-6f ? (l + seam) / l : 1;
    if (i) p_line(P, cx + dx * k, cy + dy * k);
    else p_move(P, cx + dx * k, cy + dy * k);
  }
  p_close(P);
  fill_c(RGBA(rgb[0], rgb[1], rgb[2], 1));
}

static void crew3d(FigSpec *f, float crown_x, float crown_y, float s, float yaw, V3 key) {
  const float H = 1.75f;
  f->pose.yaw = yaw;
  float dz = H / 8 * CAM.F / s;
  float target[3] = { (crown_x - CAM.cx) * dz / CAM.F, CAM.pos[1] - (crown_y - CAM.cy) * dz / CAM.F, dz };
  /* where the crown lands with the pose as it is: the top of a figure built there */
  static FigTri tris[4096];
  int n = fig_build(f, tris, 4096);
  float top = -1e9f, tx = 0, tz = 0;
  for (int i = 0; i < n; i++)
    if (tris[i].part == FP_HEAD)
      for (int k = 0; k < 3; k++)
        if (tris[i].v[k][1] > top) top = tris[i].v[k][1], tx = tris[i].v[k][0], tz = tris[i].v[k][2];
  f->pose.root[0] += target[0] - tx, f->pose.root[1] += target[1] - top, f->pose.root[2] += target[2] - tz;
  FigLight L = { { -0.62f, 0.36f, -0.7f }, { key.v[0], key.v[1], key.v[2] }, { blue_rim.v[0], blue_rim.v[1], blue_rim.v[2] } };
  fig_draw(f, 1, &CAM, &L, fill_tri, NULL);
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
  seam = 0.6f / (sc * z);
  /* the supervisor: a chief in khaki, standing behind the operator, turned toward the commander, clipboard in hand */
  {
    float step = ease(clampf_((t - L2[0]) / 0.4f, 0, 1));
    FigSpec f;
    fig_crew(FC_SUPERVISOR, 1.75f, 0, &f);
    f.open = speak(L2, 22), f.blink = blink_at(0.3f);
    f.pose.ang[FB_HEAD][0] = alert ? -4.0f : 0.0f, f.pose.ang[FB_HEAD][1] = -6, f.pose.ang[FB_HEAD][2] = 0;
    crew3d(&f, 1010 + 8 * step, 262 + 4 * sinf(t * 1.5f) - 3 * step, 118, 100, mul(mix(v3c(140, 190, 255), key_red, red_k), 0.9f));
  }
  console(u);
  /* the air detector: young, headset on, seated, leaning in to his scope */
  {
    float lean = ease(clampf_((t - 5.3f) / 0.6f, 0, 1)) * 10 - ease(clampf_((t - L2[0]) / 0.5f, 0, 1)) * 6;
    FigSpec f;
    fig_crew(FC_OPERATOR, 1.75f, 0, &f);
    f.open = speak(L1, 46), f.blink = blink_at(0.7f);
    f.pose.ang[FB_HIPS][0] = 14 + lean * 0.8f, f.pose.ang[FB_HIPS][1] = 0, f.pose.ang[FB_HIPS][2] = 0;
    crew3d(&f, 760 - lean, 318 + lean * 0.3f + 2 * sinf(t * 1.7f), 150, -108, mix(v3c(110, 255, 180), key_red, red_k));
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
    FigSpec f;
    fig_crew(FC_COMMANDER, 1.75f, ease(clampf_((t - L3[0]) / 0.4f, 0, 1)), &f);
    f.open = speak(L3, 14), f.blink = blink_at(0.1f);
    f.pose.ang[FB_HEAD][0] = nod * 2, f.pose.ang[FB_HEAD][1] = 0, f.pose.ang[FB_HEAD][2] = 0;
    crew3d(&f, 1395 - turn * 10, 246 + 3 * sinf(t * 1.3f) - turn * 4 + nod, 178, -100 - 14 * turn, mul(mix(v3c(130, 180, 240), key_red, red_k), 0.95f - 0.1f * turn));
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

