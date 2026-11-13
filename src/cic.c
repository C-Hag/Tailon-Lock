/* cic.c - the carrier's combat information centre in the opening film,
 * 05:47, drawn entirely in code on our painter (paint.c): the room in 3D
 * (room.c) in the look of a refit-era gun ship's combat centre, its screens
 * drawn on their faces, and the people in 3D from the figure engine
 * (figure.c): the radar operator, the chief supervisor, the commander and a
 * plotter behind the boards. One
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
#include "room.h"


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
static Paint rg4(float x, float y, float r0, float r1, int n, const float *at, const PColor *c) {
  Paint g = p_radial(P, x, y, r0, r1);
  for (int i = 0; i < n; i++) p_stop(&g, at[i], c[i]);
  return g;
}
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

static float blink_at(float seed) {
  float p = 2.8f + seed * 1.7f, ph = fmodf(T + seed * 5, p) / p;
  return ph > 0.965f ? 1 - fabsf(ph - 0.9825f) / 0.0175f : 0;
}

static V3 blue_rim;
static float red_k, L1[2] = { 5.6f, 7.0f }, L2[2] = { 7.05f, 8.05f }, L3[2] = { 8.1f, 9.2f };

/* ---- the room in 3D (room.c), lit by its lamps, each screen drawn on its own face ----
 * A camera at eye height looks into the room; the same camera places the people. The same as the browser game's
 * drawCIC(). */
static const FigCam CAM = { { 0, 1.25f, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, 1100, 800, 450 };
static float seam; /* how far each facet is grown, in scene units, so no seams show between them */
static FigTri room_far[ROOM_MAX_TRIS], room_near[ROOM_MAX_TRIS], lit_far[ROOM_MAX_TRIS], lit_near[ROOM_MAX_TRIS];
static int n_far, n_near, n_scr = -1;
static RoomScreen scr[ROOM_MAX_SCREENS];
static float scr_p[ROOM_MAX_SCREENS][3][2]; /* each screen's corners on the film: o, o + ex, o + ey */
static RoomLights lamps;
static float zoom; /* the film's zoom: a head large on the screen is built finer (FigSpec.detail) */
static float view_sc, view_ox, view_oy; /* the scene to the film's pixels */
static Framebuffer *view_fb;
static bool alert;
static float gq;

static void proj(const float v[3], float out[2]) {
  out[0] = CAM.cx + v[0] / v[2] * CAM.F, out[1] = CAM.cy - (v[1] - CAM.pos[1]) / v[2] * CAM.F;
}
static bool room_ready(void) {
  if (n_scr >= 0) return true;
  static FigTri all[ROOM_MAX_TRIS];
  int ns = 0, n = room_build(all, ROOM_MAX_TRIS, scr, ROOM_MAX_SCREENS, &ns);
  if (n < 0) return false;
  for (int i = 0; i < n; i++) {
    if (all[i].part == RP_NEAR) room_near[n_near++] = all[i];
    else room_far[n_far++] = all[i];
  }
  for (int i = 0; i < ns; i++) {
    float a[3], b[3];
    for (int k = 0; k < 3; k++) a[k] = scr[i].o[k] + scr[i].ex[k], b[k] = scr[i].o[k] + scr[i].ey[k];
    proj(scr[i].o, scr_p[i][0]), proj(a, scr_p[i][1]), proj(b, scr_p[i][2]);
  }
  n_scr = ns;
  return true;
}

/* ---- what each screen shows, in its own units ---- */
static const char *const LABELS[11] = { "AIR SEARCH", "TRACK SUP", "AIR CONTROL", "SURFACE", "EW", "NAV PLOT", "STRIKE   TOMAHAWK",
                                        "ASCM   HARPOON", "DATA LINK", "AIR SEARCH 2", "COMMS" };
static const char *const ALPHA[7][5] = { { "TRK  BRG RNG", "1101 045  28", "0212 180  41", "0214 210  55", "3307 095  88" },
                                         { "LINK 11  NET", "PU 04  ACTIVE", "PU 07  ACTIVE", "R/C   4.2 S" },
                                         { "CAP 1  ON STA", "VIPER 1  A5", "VIPER 2  A5", "TANKER  T1" },
                                         { "SFC  BRG RNG", "0212 180  41", "0214 210  55" },
                                         { "ESM  BRG", "J-BAND 352", "I-BAND 349" },
                                         { "POSN 24-31N", "     052-12E", "CSE 090  SPD 18" },
                                         { "UHF 1  GUARD", "UHF 2  AIC", "HF  FLT BCST" } };
static void line_c(float x0, float y0, float x1, float y1, PColor c, float w) {
  p_begin(P);
  p_move(P, x0, y0), p_line(P, x1, y1);
  stroke_c(c, w);
}
static void ring_c(float x, float y, float r, PColor c, float w) {
  p_begin(P);
  p_ellipse(P, x, y, r, r, 0, 0, 2 * PI_F);
  stroke_c(c, w);
}
static void ppi(const RoomScreen *s) {
  float cx = 200, cy = 160, r = 140, sw = T * PI_F * 2 / 4 + s->bay;
  static const float RK[3] = { 0.33f, 0.66f, 1 };
  for (int i = 0; i < 3; i++) ring_c(cx, cy, r * RK[i], RGBA(60, 220, 120, 0.35f), 3);
  for (int i = 0; i < 24; i++) {
    float a = sw - i * 0.04f;
    line_c(cx, cy, cx + sinf(a) * r, cy - cosf(a) * r, RGBA(80, 255, 150, 0.5f * (1 - i / 24.0f)), 8);
  }
  Rng Rb = { (uint32_t)(17 + s->bay) };
  for (int i = 0; i < 9; i++) {
    float a = (float)rng_f(&Rb) * PI_F * 2, d = 0.2f + (float)rng_f(&Rb) * 0.75f;
    rect_c(cx + sinf(a) * r * d - 4, cy - cosf(a) * r * d - 4, 8, 8, RGBA(120, 255, 170, 0.8f));
  }
}
static void keys(const RoomScreen *s, bool weapon) {
  for (int r = 0; r < 3; r++)
    for (int j = 0; j < 8; j++) {
      int lit = (s->bay * 3 + r * 5 + j * 7) % 6;
      bool red = weapon && r == 0 && j > 4;
      rect_c(35 + j * 85, 30 + r * 80, 60, 55, red ? HEX(0xff4a3a) : lit == 0 ? HEX(0xffaa3c) : lit == 1 ? HEX(0x5aff8c) : RGBA(70, 76, 78, 1));
      if (red) stroke_rect(28 + j * 85, 23 + r * 80, 74, 69, RGBA(255, 90, 70, 0.9f), 6);
    }
}
/* the status boards: grease pencil on edge-lit plexiglass, ruled in white; the plotter writes from the far side,
 * backwards, so the watch reads it the right way round */
static void board(const RoomScreen *s, const char *title, int ncol, const char *const *head, const char *const (*rows)[6], int nrows) {
  float cw = (s->w - 80) / ncol;
  PColor ink = RGBA(255, 224, 90, 0.7f), hot = RGBA(255, 120, 70, 0.85f), rule = RGBA(230, 240, 255, 0.35f);
  line_c(40, 150, s->w - 40, 150, rule, 5);
  for (int j = 1; j < ncol; j++) line_c(40 + j * cw, 110, 40 + j * cw, s->h - 40, rule, 5);
  mono(title, 40, 60, 56, RGBA(235, 245, 255, 0.6f), ALIGN_LEFT);
  for (int j = 0; j < ncol; j++) mono(head[j], 60 + j * cw, 128, 40, RGBA(235, 245, 255, 0.5f), ALIGN_LEFT);
  for (int i = 0; i < nrows; i++)
    for (int j = 0; j < ncol; j++)
      mono(rows[i][j], 60 + j * cw, 200 + i * 100, 52, !strcmp(rows[i][1], "HOS") || !strcmp(rows[i][j], "LAUNCH") ? hot : ink, ALIGN_LEFT);
}
/* the operator's radar repeater: range rings and bearings, the sweep, sea clutter, own ship and the CAP station, the
 * two contacts closing with the echoes each sweep leaves, then the hook and its readout */
/* full: the scope fills the picture, so its sweep needs finer steps, its contacts room between them and a name, and
 * there is no glass to see */
static void scope(bool full) {
  float dw = 500, dh = 330, rx = dw * 0.42f, ry = dh * 0.5f, rr = dh * 0.44f;
  static const float RK[4] = { 0.25f, 0.5f, 0.75f, 1 };
  for (int i = 0; i < 4; i++) ring_c(rx, ry, rr * RK[i], RGBA(60, 255, 140, 0.25f), 1.2f);
  for (int i = 0; i < 72; i++) {
    float a = i * PI_F / 36, k = i % 6 ? 0.96f : 0.92f;
    line_c(rx + sinf(a) * rr * k, ry - cosf(a) * rr * k, rx + sinf(a) * rr, ry - cosf(a) * rr, RGBA(60, 255, 140, 0.25f), 1.2f);
  }
  static const char *const BL[4] = { "000", "090", "180", "270" };
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * PI_F / 180;
    mono(BL[i], rx + sinf(a) * rr * 1.1f, ry - cosf(a) * rr * 1.1f, 11, RGBA(80, 220, 140, 0.7f), ALIGN_CENTER);
  }
  float sw = ((T - 2.2f) / 2.6f) * PI_F * 2;
  int steps = full ? 120 : 30;
  for (int i = 0; i < steps; i++) {
    float a = sw - i * (0.9f / steps);
    line_c(rx, ry, rx + sinf(a) * rr, ry - cosf(a) * rr, RGBA(80, 255, 150, 0.4f * (30.0f / steps) * (1 - (float)i / steps)), full ? 1.5f : 3);
  }
  R.state = 4711;
  for (int i = 0; i < 40; i++) { /* sea clutter */
    float a = rnd() * PI_F * 2, d = 0.3f + rnd() * 0.6f;
    rect_c(rx + sinf(a) * rr * d, ry - cosf(a) * rr * d, 2, 2, RGBA(60, 200, 110, 0.18f));
  }
  PColor own = RGBA(110, 170, 255, 0.95f);
  ring_c(rx, ry, 6, own, 2);
  p_begin(P), p_ellipse(P, rx + rr * 0.3f, ry - rr * 0.42f, 7, 7, 0, PI_F, 0), stroke_c(own, 2);
  float CT[2][2] = { { full ? -24.0f : -10, 0.84f }, { full ? -9.0f : -7, 0.9f } };
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
    line_c(hx, hy, hx + 3, hy + 30, hc, 2.5f);
    if (full) mono(i ? "BOGEY 2" : "BOGEY 1", i ? hx + 26 : hx - 14, hy - 8, 10, hc, i ? ALIGN_LEFT : ALIGN_RIGHT); /* clear of the hook on bogey 2 */
  }
  if (T > 5.9f) {
    float a = (full ? -9.0f : -8.5f) * PI_F / 180, d = range_at(full ? 0.9f : 0.87f, T), hx = rx + sinf(a) * rr * d, hy = ry - cosf(a) * rr * d, hs = 20;
    stroke_rect(hx - hs, hy - hs, hs * 2, hs * 2, RGBA(255, 220, 90, 0.9f), 1.5f);
    float bx = dw * 0.79f, by = 26;
    rect_c(bx - 8, by - 14, 112, 128, RGBA(0, 30, 15, 0.85f));
    static const char *const HK[7] = { "HOOK 7402", "HOSTILE AIR", "BRG  350", "RNG  62", "SPD  540", "ALT  2000", "CPA  4 MIN" };
    for (int i = 0; i < 7; i++) mono(HK[i], bx, by + i * 17, 11, i < 2 ? HEX(0xff8a72) : RGBA(120, 255, 170, 0.9f), ALIGN_LEFT);
  }
  if (!full) rect_c(0, 0, dw, dh * 0.4f, RGBA(160, 255, 200, 0.03f)); /* the glass */
}
static void screen_content(const RoomScreen *s) {
  switch (s->kind) {
  case RS_SCOPE: scope(false); break;
  case RS_PPI: ppi(s); break;
  case RS_ALPHA: {
    const char *const *rows = ALPHA[s->bay % 7];
    int n = 0;
    while (n < 5 && rows[n]) n++;
    for (int i = 0; i < n; i++) mono(rows[i], 24, 40 + i * 52, 34, i ? RGBA(110, 255, 160, 0.85f) : RGBA(160, 255, 200, 0.95f), ALIGN_LEFT);
    if ((int)(T * 2) % 2) rect_c(24, 40 + n * 52 - 16, 22, 32, RGBA(110, 255, 160, 0.85f));
    break;
  }
  case RS_STRIKE: {
    static const char *const ROWS[5] = { "TOMAHAWK  WCS", "ENGAGE PLAN  --", "CELL  A1  RDY", "CELL  A2  RDY", "MODE  STBY" };
    for (int i = 0; i < 5; i++) mono(ROWS[i], 24, 40 + i * 54, 34, i ? RGBA(255, 180, 70, 0.85f) : RGBA(255, 210, 120, 0.95f), ALIGN_LEFT);
    break;
  }
  case RS_HARPOON: {
    const char *ROWS[5] = { "HARPOON  SWC", "TGT  NONE", "CANISTERS  8", alert ? "ALERT  ARMED" : "SAFE", "MODE  BOL" };
    for (int i = 0; i < 5; i++)
      mono(ROWS[i], 24, 40 + i * 54, 34, i == 3 && alert ? HEX(0xff6a50) : i ? RGBA(255, 180, 70, 0.85f) : RGBA(255, 210, 120, 0.95f), ALIGN_LEFT);
    break;
  }
  case RS_LABEL:
    if (s->bay >= 0 && s->bay < 11) mono(LABELS[s->bay], s->w / 2, s->h / 2, 40, RGBA(225, 225, 215, 0.9f), ALIGN_CENTER);
    break;
  case RS_KEYS: keys(s, false); break;
  case RS_GUARDS: keys(s, true); break;
  case RS_KEYBOARD:
    for (int r = 0; r < 4; r++)
      for (int j = 0; j < 12; j++) rect_c(14 + j * 38 + r * 4, 14 + r * 52, 31, 42, RGBA(66, 72, 74, 1));
    break;
  case RS_CLOCK: {
    float cx = s->w / 2, cy = s->h / 2, r = s->w * 0.46f, sec = 47 * 60 + 5 * 3600 + 12 + T;
    p_begin(P), p_ellipse(P, cx, cy, r, r, 0, 0, 2 * PI_F), fill_c(HEX(0xd8d8cc));
    for (int i = 0; i < 12; i++) {
      float a = i * PI_F / 6;
      line_c(cx + sinf(a) * r * 0.8f, cy - cosf(a) * r * 0.8f, cx + sinf(a) * r * 0.95f, cy - cosf(a) * r * 0.95f, HEX(0x202020), 6);
    }
    const float H3[3][3] = { { sec / 43200 * PI_F * 2, 0.5f, 10 }, { fmodf(sec, 3600) / 3600 * PI_F * 2, 0.75f, 7 }, { fmodf(sec, 60) / 60 * PI_F * 2, 0.85f, 3 } };
    for (int i = 0; i < 3; i++)
      line_c(cx, cy, cx + sinf(H3[i][0]) * r * H3[i][1], cy - cosf(H3[i][0]) * r * H3[i][1], i == 2 ? HEX(0xa02018) : HEX(0x151515), H3[i][2]);
    break;
  }
  case RS_GQ:
    if (alert) rect_c(0, 0, s->w, s->h, RGBA(230, 40, 30, 0.45f + 0.5f * gq));
    mono(T >= L3[0] ? "LAUNCH THE ALERT" : "GENERAL QUARTERS", s->w / 2, s->h / 2, 64, alert ? HEX(0xfff2e8) : RGBA(120, 40, 34, 0.8f), ALIGN_CENTER);
    break;
  case RS_BOARD_L: {
    static const char *const HEAD[6] = { "TN", "ID", "BRG", "RNG", "SPD", "ALT" };
    static const char *const ROWS[6][6] = { { "1101", "FRD", "045", "28", "310", "180" }, { "0212", "NEU", "180", "41", "12", "0" },
                                            { "0214", "NEU", "210", "55", "18", "0" },     { "3307", "UNK", "095", "88", "420", "310" },
                                            { "7402", "HOS", "350", "62", "540", "20" },   { "7403", "HOS", "349", "64", "540", "20" } };
    board(s, "AIR CONTACTS", 6, HEAD, ROWS, 4 + (T > 6.0f) + (T > 6.6f));
    break;
  }
  case RS_BOARD_R: {
    static const char *const HEAD[4] = { "STA", "A/C", "SIDE", "STATUS" };
    const char *const ROWS[5][6] = { { "ALERT 5", "VIPER 1", "24", alert ? "LAUNCH" : "MANNED" }, { "ALERT 5", "VIPER 2", "25", alert ? "LAUNCH" : "MANNED" },
                                     { "ALERT 30", "VIPER 3", "27", "READY" },                     { "CAP 1", "VIPER 7", "21", "ON STA" },
                                     { "WIND", "18 KT", "", "DECK GREEN" } };
    board(s, "AIR PLAN   05:47", 4, HEAD, ROWS, 5);
    break;
  }
  default: break;
  }
}
/* a screen's half: clipped to the triangle, its content mapped onto the face */
static void tri_path(const float pts[3][2]) {
  p_begin(P);
  p_move(P, pts[0][0], pts[0][1]), p_line(P, pts[1][0], pts[1][1]), p_line(P, pts[2][0], pts[2][1]), p_close(P);
}
static void screen_at(int i, const float pts[3][2], const float rgb[3]) {
  const RoomScreen *s = &scr[i];
  const float(*q)[2] = scr_p[i];
  p_save(P);
  tri_path(pts);
  fill_c(s->kind == RS_BOARD_L || s->kind == RS_BOARD_R ? RGBA(120, 170, 210, 0.06f) : RGBA(rgb[0], rgb[1], rgb[2], 1));
  tri_path(pts);
  p_clip(P);
  p_transform(P, (q[1][0] - q[0][0]) / s->w, (q[1][1] - q[0][1]) / s->w, (q[2][0] - q[0][0]) / s->h, (q[2][1] - q[0][1]) / s->h, q[0][0], q[0][1]);
  screen_content(s);
  p_restore(P);
}

static void fill_tri(const float pts[3][2], const float rgb[3], const FigGrad *gr, int tag, void *ud) {
  (void)ud;
  if (tag > 0 && tag <= n_scr) {
    screen_at(tag - 1, pts, rgb);
    return;
  }
  float cx = (pts[0][0] + pts[1][0] + pts[2][0]) / 3, cy = (pts[0][1] + pts[1][1] + pts[2][1]) / 3;
  p_begin(P);
  for (int i = 0; i < 3; i++) {
    float dx = pts[i][0] - cx, dy = pts[i][1] - cy, l = sqrtf(dx * dx + dy * dy);
    float k = l > 1e-6f ? (l + seam) / l : 1;
    if (i) p_line(P, cx + dx * k, cy + dy * k);
    else p_move(P, cx + dx * k, cy + dy * k);
  }
  p_close(P);
  if (gr) { /* a smooth facet: the colour runs across it */
    Paint g = p_linear(P, gr->x0, gr->y0, gr->x1, gr->y1);
    p_stop(&g, 0, RGBA(gr->c0[0], gr->c0[1], gr->c0[2], 1));
    p_stop(&g, 1, RGBA(gr->c1[0], gr->c1[1], gr->c1[2], 1));
    p_fill(P, &g);
  } else fill_c(RGBA(rgb[0], rgb[1], rgb[2], 1));
}

/* ---- the people ---- */
/* where a point of the scene lies in the room, at a depth (for where the crew look) */
static void room_at(float x, float y, float dz, float out[3]) {
  out[0] = (x - CAM.cx) * dz / CAM.F, out[1] = CAM.pos[1] - (y - CAM.cy) * dz / CAM.F, out[2] = dz;
}
/* where a man looks: from t on, at the point (x, y) of the scene, dz deep in the room */
typedef struct { float t, x, y, dz; } Look;
/* His eyes on the looks in turn, as eyes move: each new look a saccade of about 50 ms, a long one (over about 15
 * degrees) with a blink on it, and while he holds a look the small jumps of fixation, a few minutes of arc every
 * third of a second or so. Returns the blink that goes with the saccades. */
static float look_at(const Look *k, int n, float seed, float out[3]) {
  int i = 0;
  while (i + 1 < n && T >= k[i + 1].t) i++;
  const Look *a = &k[i > 0 ? i - 1 : 0], *b = &k[i];
  float s = ease(clampf_((T - b->t) / 0.05f, 0, 1));
  float x = a->x + (b->x - a->x) * s, y = a->y + (b->y - a->y) * s, dz = a->dz + (b->dz - a->dz) * s;
  float j = floorf(T * 3.1f + seed * 7);
  float h1 = fmodf(sinf(j * 12.9898f + seed * 78.233f) * 43758.547f, 1), h2 = fmodf(sinf(j * 39.346f + seed * 11.135f) * 24634.634f, 1);
  x += h1 * 3, y += h2 * 2; /* fixation: about 0.1 degree */
  room_at(x, y, dz, out);
  float jump = hypotf(b->x - a->x, b->y - a->y), since = T - b->t;
  return i > 0 && jump > 180 && since > 0 && since < 0.16f ? 1 - fabsf(since - 0.08f) / 0.08f : 0;
}
/* a man speaking his line: the mouth from the words, a blink as he finishes and with every long saccade, his eyes
 * on the looks */
static void voice(FigSpec *f, const char *text, const float L[2], float blink_seed, const Look *looks, int nlooks) {
  FigSpeech sp;
  fig_speech(text, L[0], L[1], &sp);
  fig_mouth(&sp, T, &f->face.open, &f->face.wide, &f->face.round);
  float after = T - L[1], b = after > 0.02f && after < 0.2f ? 1 - fabsf(after - 0.11f) / 0.09f : 0;
  float sac = look_at(looks, nlooks, blink_seed, f->face.gaze);
  f->face.blink = fmaxf(fmaxf(blink_at(blink_seed), b), sac);
  f->face.has_gaze = true;
}
/* the crew's eyes through the shot, on the dialogue: the radar operator's on the two contacts, from one to the other
 * as he calls them; the chief's down toward his clipboard until the operator's call, then up to the commander as he
 * raises the alert, his head turning after his eyes; the commander's on the operator while he reports, down to
 * the scope at the alert, and back on the operator for the order, turning toward him (each man's eyes are half his
 * head below his crown, at his head's depth, as crew3d places him; the chief and the commander stand behind and
 * beside each other, so neither can look at the other without turning his head) */
#define DZ_OP (1.75f / 8 * 1100 / 150)
#define DZ_SUP (1.75f / 8 * 1100 / 118)
#define DZ_CMD (1.75f / 8 * 1100 / 178)
static const Look LOOK_OP[] = { { 0, 300, 445, 1.25f }, { 5.62f, 285, 430, 1.25f }, { 6.05f, 318, 452, 1.25f }, { 6.55f, 292, 436, 1.25f }, { 7.25f, 310, 450, 1.25f } };
static const Look LOOK_SUP[] = { { 0, 1200, 400, DZ_SUP }, { 5.95f, 1175, 412, DZ_SUP }, { 6.98f, 1395, 335, DZ_CMD }, { 8.95f, 1200, 400, DZ_SUP } };
static const Look LOOK_CMD[] = { { 0, 760, 390, DZ_OP }, { 7.08f, 330, 450, 1.25f }, { 8.1f, 760, 390, DZ_OP } };
#define NLOOK(a) ((int)(sizeof a / sizeof a[0]))
static FigTri fig_tris[FIG_MAX_TRIS], chair_tris[ROOM_CHAIR_TRIS], chair_lit[ROOM_CHAIR_TRIS];
/* Places a man so the crown of his head lands where the scene wants it, the head s scene units tall, and draws him
 * with his chair under him (chair: 0 none, 1 a swivel chair, 2 the high chair). */
static void crew3d(FigSpec *f, float crown_x, float crown_y, float s, float yaw, V3 key, int chair) {
  const float H = 1.75f;
  f->pose.yaw = yaw;
  f->detail = s * zoom > 300; /* the close-up head */
  float dz = H / 8 * CAM.F / s;
  float target[3] = { (crown_x - CAM.cx) * dz / CAM.F, CAM.pos[1] - (crown_y - CAM.cy) * dz / CAM.F, dz };
  /* where the crown lands with the pose as it is: the top of a figure built there */
  int n = fig_build(f, fig_tris, FIG_MAX_TRIS);
  float top = -1e9f, tx = 0, tz = 0;
  for (int i = 0; i < n; i++)
    if (fig_tris[i].part == FP_HEAD)
      for (int k = 0; k < 3; k++)
        if (fig_tris[i].v[k][1] > top) top = fig_tris[i].v[k][1], tx = fig_tris[i].v[k][0], tz = fig_tris[i].v[k][2];
  f->pose.root[0] += target[0] - tx, f->pose.root[1] += target[1] - top, f->pose.root[2] += target[2] - tz;
  n = fig_build(f, fig_tris, FIG_MAX_TRIS);
  FigMesh m[2] = { { fig_tris, n > 0 ? n : 0, f }, { chair_lit, 0, NULL } };
  if (chair) {
    float hips[3], a = yaw * PI_F / 180;
    fig_joint(f, FB_HIPS, hips);
    float seat[3] = { hips[0] - sinf(a) * 0.06f, hips[1] - 0.06f, hips[2] - cosf(a) * 0.06f };
    int nc = room_chair(chair == 2, seat, yaw, chair_tris, ROOM_CHAIR_TRIS);
    if (nc > 0) room_light(chair_tris, chair_lit, nc, &lamps), m[1].n = nc;
  }
  FigLight L = { { -0.62f, 0.36f, -0.7f }, { key.v[0], key.v[1], key.v[2] }, { blue_rim.v[0], blue_rim.v[1], blue_rim.v[2] } };
  fig_raster(m, 2, &CAM, &L, view_sc, view_ox, view_oy, view_fb->px, view_fb->w, view_fb->h); /* the people: the close-up path */
}

void cic_draw(Framebuffer *out, float t, float t0, float t1) {
  /* the scene's own clock: its lines, looks and gestures are timed for a take from 5.2 to 9.0 s, so a take anywhere
   * else in the film plays them on that clock (without this they were all over before the take began) */
  if (t1 > t0) t = 5.2f + (t - t0) / (t1 - t0) * 3.8f;
  t0 = 5.2f, t1 = 9.0f;
  T = t;
  S0 = t0, S1 = t1;
  float u = clampf_((t - t0) / (t1 - t0), 0, 1);
  alert = t >= L2[0];
  gq = alert ? 0.5f + 0.5f * sinf((t - L2[0]) * PI_F * 2 / 1.1f) : 0;
  R.state = 4711;
  int W = out->w, H = out->h;
  /* it opens tight on the two contacts on the scope, then pulls out to the operator and the officers behind him */
  /* (no move out from the scope: the radar shot before it has the bogeys, and a second zoom on them was one too many) */
  float zo = 1, zw = 1.04f + 0.1f * ease(u), zc = 3.6f + 0.35f * (t - t0);
  float sc = H / 900.0f, z = expf(logf(zc) + (logf(zw) - logf(zc)) * zo);
  float wx = 760 + 260 * ease(u) + 120 * ease(clampf_((t - L3[0]) / 1.1f, 0, 1)), wy = 450 + 10 * sinf(t * 0.7f);
  float camx = 272 + (wx - 272) * zo, camy = 392 + (wy - 392) * zo;
  zoom = z;
  view_sc = sc * z, view_ox = W / 2.0f - camx * sc * z, view_oy = H / 2.0f - camy * sc * z, view_fb = out;
  blue_rim = alert ? mix(v3c(90, 150, 230), v3c(255, 70, 60), gq * 0.5f) : v3c(90, 150, 230);
  red_k = alert ? gq * 0.25f : 0;
  if (!room_ready()) return;
  room_lamps(scr, n_scr, alert, gq, &lamps);
  /* the room on its own layer, softly out of focus behind the people, with the plotter behind the air board writing
   * up the new contacts */
  /* it is soft and hardly moves: half the size and a third as often costs a sixth or so, and looks the same */
  static int bg_age;
  static float bg_t = -1;
  const int BQ = H > 1200 ? 2 : 1, bw = (W + BQ - 1) / BQ, bh = (H + BQ - 1) / BQ;
  bool new_bg = bg.w != bw || bg.h != bh || bg_age % 3 == 0 || t < bg_t || t > bg_t + 0.25f;
  bg_age++;
  if (new_bg) {
  bg_t = t;
  if (!fb_resize(&bg, bw, bh)) return;
  fb_clip_reset(&bg);
  fb_clear(&bg, hexc(0x030507));
  if (!P) P = painter_new(&bg);
  if (!P) return;
  painter_begin(P, &bg);
  p_translate(P, bw / 2.0f, bh / 2.0f);
  p_scale(P, sc * z / BQ, sc * z / BQ);
  p_translate(P, -camx, -camy);
  seam = 0.6f * BQ / (sc * z);
  room_light(room_far, lit_far, n_far, &lamps);
  {
    FigSpec pl, sup;
    fig_crew(FC_OPERATOR, 1.72f, 0, &pl);
    fig_crew(FC_SUPERVISOR, 1.75f, 0, &sup);
    pl.height = 1.72f;
    pl.shape = sup.shape;
    static const uint8_t SKIN[3] = { 150, 104, 78 }, HAIR[3] = { 20, 16, 14 }, IRIS[3] = { 50, 36, 26 };
    memcpy(pl.skin, SKIN, 3), memcpy(pl.hair, HAIR, 3), memcpy(pl.iris, IRIS, 3);
    memset(&pl.face, 0, sizeof pl.face);
    pl.face.blink = blink_at(0.5f);
    float write = t > 5.9f && t < 7.4f ? sinf((t - 5.9f) * 9) : 0;
    fig_pose_stand(&pl.pose);
    pl.pose.root[0] = 1.25f, pl.pose.root[1] = 0, pl.pose.root[2] = 3.42f, pl.pose.yaw = 180;
    pl.pose.ang[FB_UPPER_R][0] = 0, pl.pose.ang[FB_UPPER_R][1] = 45 + 8 * write, pl.pose.ang[FB_UPPER_R][2] = -50;
    pl.pose.ang[FB_FORE_R][0] = 0, pl.pose.ang[FB_FORE_R][1] = 50, pl.pose.ang[FB_FORE_R][2] = 0;
    pl.pose.curl[1] = 50;
    pl.pose.ang[FB_HEAD][0] = -6, pl.pose.ang[FB_HEAD][1] = -8, pl.pose.ang[FB_HEAD][2] = 0;
    int np = fig_build(&pl, fig_tris, FIG_MAX_TRIS);
    FigMesh m[2] = { { lit_far, n_far, NULL }, { fig_tris, np > 0 ? np : 0, &pl } };
    V3 k = mix(v3c(200, 180, 120), v3c(255, 80, 60), red_k);
    FigLight L = { { -0.62f, 0.36f, -0.7f }, { k.v[0], k.v[1], k.v[2] }, { blue_rim.v[0], blue_rim.v[1], blue_rim.v[2] } };
    fig_draw_meshes(m, 2, &CAM, &L, fill_tri, NULL);
  }
  p_blur(&bg, (int)lroundf(1.3f * sc / BQ) > 0 ? (int)lroundf(1.3f * sc / BQ) : 1, 0.85f);
  }
  fb_scale_up(out, &bg, BQ);
  /* the people and the console, sharp, over it */
  painter_begin(P, out);
  p_translate(P, W / 2.0f, H / 2.0f);
  p_scale(P, sc * z, sc * z);
  p_translate(P, -camx, -camy);
  V3 key_red = v3c(255, 80, 60);
  /* the supervisor: a chief in khaki, standing behind the operator, turned toward the commander, clipboard in hand */
  {
    float step = ease(clampf_((t - L2[0]) / 0.4f, 0, 1));
    FigSpec f;
    fig_crew(FC_SUPERVISOR, 1.75f, 0, &f);
    voice(&f, "ALERT, ALERT! WE HAVE INBOUND.", L2, 0.3f, LOOK_SUP, NLOOK(LOOK_SUP));
    f.face.brow_up = alert ? 0.7f : 0;
    float glance = ease(clampf_((t - 7.1f) / 0.35f, 0, 1)) - ease(clampf_((t - 9.05f) / 0.35f, 0, 1)); /* to the commander */
    f.pose.ang[FB_HEAD][0] = alert ? -4.0f : 0.0f, f.pose.ang[FB_HEAD][1] = -6 + 34 * glance, f.pose.ang[FB_HEAD][2] = 0;
    crew3d(&f, 1010 + 8 * step, 262 + 4 * sinf(t * 1.5f) - 3 * step, 118, 100, mul(mix(v3c(140, 190, 255), key_red, red_k), 0.9f), 0);
  }
  /* the operator's console: the radar repeater turned toward him, keyboard, trackball, the phone, his coffee */
  {
    room_light(room_near, lit_near, n_near, &lamps);
    FigMesh m = { lit_near, n_near, NULL };
    FigLight L = { { -0.62f, 0.36f, -0.7f }, { 255, 255, 255 }, { blue_rim.v[0], blue_rim.v[1], blue_rim.v[2] } };
    fig_draw_meshes(&m, 1, &CAM, &L, fill_tri, NULL);
    glow(360, 470, 520, v3c(50, 220, 140), 0.18f);
    for (int i = 0; i < n_scr; i++)
      if (scr[i].kind == RS_STEAM) {
        float mp[2];
        proj(scr[i].o, mp);
        for (int j = 0; j < 3; j++) { /* the steam off the coffee */
          float k = fmodf(t * 0.25f + j / 3.0f, 1);
          p_begin(P);
          p_move(P, mp[0] - 2 + j * 4, mp[1] - k * 50);
          p_quad(P, mp[0] - 7 + sinf(t * 2 + j) * 8, mp[1] - 20 - k * 50, mp[0] + 1, mp[1] - 40 - k * 50);
          stroke_c(RGBA(180, 190, 200, 0.15f), 1.5f);
        }
      }
  }
  /* the air detector: young, in chambray and dungarees and a sound-powered phone headset, on his swivel chair,
   * leaning in to his scope */
  {
    float lean = ease(clampf_((t - 5.3f) / 0.6f, 0, 1)) * 10 - ease(clampf_((t - L2[0]) / 0.5f, 0, 1)) * 6;
    FigSpec f;
    fig_crew(FC_OPERATOR, 1.75f, 0, &f);
    voice(&f, "BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.", L1, 0.7f, LOOK_OP, NLOOK(LOOK_OP));
    f.face.brow_up = t > 5.5f && t < 7 ? 0.5f : 0;
    f.pose.ang[FB_HIPS][0] = 14 + lean * 0.8f, f.pose.ang[FB_HIPS][1] = 0, f.pose.ang[FB_HIPS][2] = 0;
    crew3d(&f, 760 - lean, 318 + lean * 0.3f + 2 * sinf(t * 1.7f), 150, -108, mix(v3c(110, 255, 180), key_red, red_k), 1);
  }
  /* the commander: a senior officer in khaki and the ship's ball cap, in his high chair close to us, listening, then
   * giving the order */
  {
    float turn = ease(clampf_((t - 7.3f) / 0.7f, 0, 1));
    float nod = t > L3[0] + 0.55f ? sinf(clampf_((t - L3[0] - 0.55f) / 0.35f, 0, 1) * PI_F) * 4 : 0;
    FigSpec f;
    fig_crew(FC_COMMANDER, 1.75f, ease(clampf_((t - L3[0]) / 0.4f, 0, 1)), &f);
    voice(&f, "SEND THE BIRDS.", L3, 0.1f, LOOK_CMD, NLOOK(LOOK_CMD));
    f.face.frown = t < L3[0] ? 0.35f : 0;
    f.pose.ang[FB_HEAD][0] = nod * 2, f.pose.ang[FB_HEAD][1] = 0, f.pose.ang[FB_HEAD][2] = 0;
    crew3d(&f, 1395 - turn * 10, 246 + 3 * sinf(t * 1.3f) - turn * 4 + nod, 178, -100 + 14 * turn, mul(mix(v3c(130, 180, 240), key_red, red_k), 0.95f - 0.1f * turn), 2);
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

/* The radar scope alone, filling the picture: the sweep finds the two contacts closing (it once pushed in) as
 * the operator hooks one (the film, before the alert). */
void cic_radar_draw(Framebuffer *out, float t, float t0, float t1) {
  int W = out->w, H = out->h;
  float u = clampf_((t - t0) / (t1 - t0), 0, 1);
  /* the scope's own clock: the hook comes up about two thirds of the way through the shot */
  T = 5.9f + (u - 0.66f) * (t1 - t0);
  S0 = 5.9f - 0.66f * (t1 - t0), S1 = S0 + (t1 - t0);
  R.state = 4711;
  fb_clip_reset(out);
  fb_clear(out, hexc(0x020806));
  if (!P) P = painter_new(out);
  if (!P) return;
  painter_begin(P, out);
  /* inside the letterbox: the 500 x 330 screen, centred on the scope and moving to the contacts as it pushes in */
  /* (it holds still: the control room's own move out from the scope is the one zoom on the bogeys) */
  float k = 0 * ease(u), sc = H * 0.72f / 330 * (1 + 0.28f * k), cx = 250 - 40 * k, cy = 165 - 55 * k;
  p_translate(P, W / 2.0f, H / 2.0f);
  p_scale(P, sc, sc);
  p_translate(P, -cx, -cy);
  scope(true);
  p_additive(P, true);
  glow(210, 165, 240, v3c(40, 180, 90), 0.10f);
  p_additive(P, false);
  p_reset_transform(P);
  Paint vig = RG(W / 2.0f, H / 2.0f, H * 0.35f, W * 0.7f, S_(0, RGBA(0, 0, 0, 0)), S_(1, RGBA(0, 0, 0, 0.75f)));
  p_fill_rect(P, 0, 0, (float)W, (float)H, &vig);
}

/* ---- the menu: the same room, quiet after the launch, with the menu on its big tactical display ----
 * The watch carries on around it: the scopes sweep, the boards hold the air plan; nobody stands in the shot. The
 * room is soft and dim, the display sharp, its glow on the room. Out of focus the room needs no more than 640 x 360
 * and ten pictures a second; the bezel is drawn once for each size; each frame only copies the finished picture. */
static Framebuffer room_fb, room_lo, bezel_fb, menu_fb;
static int menu_key[6], menu_step = -1;

static void bezel_lamp(float x, float y, float r, PColor c) {
  p_begin(P), p_ellipse(P, x, y, r, r, 0, 0, 2 * PI_F), fill_c(c);
}

/* the film's camera on a picture of fb's height */
static void menu_view(Framebuffer *fb) {
  float sc = fb->h / 900.0f, z = 1.06f, camx = 800, camy = 440;
  zoom = z;
  view_sc = sc * z, view_ox = fb->w / 2.0f - camx * sc * z, view_oy = fb->h / 2.0f - camy * sc * z, view_fb = fb;
  painter_begin(P, fb);
  p_translate(P, fb->w / 2.0f, fb->h / 2.0f);
  p_scale(P, sc * z, sc * z);
  p_translate(P, -camx, -camy);
  seam = 0.6f / (sc * z);
}

/* the room: drawn at 1/d of out's size (its lettering in whole dots, the size the film has at 640 x 360), then
 * halved into room_lo and dimmed, which with the bilinear step back up is the film's blur; then the display's
 * light and the shadow it throws on the bulkhead behind it */
static void menu_room(int d, float t, float x, float y, float w, float h, float m0, float K) {
  T = 20 + t; /* after the film: the contacts closed, the alert away */
  S0 = 0, S1 = 1;
  alert = false, gq = 0, red_k = 0;
  blue_rim = v3c(90, 150, 230);
  R.state = 4711;
  room_lamps(scr, n_scr, false, 0, &lamps);
  fb_clip_reset(&room_fb);
  fb_clear(&room_fb, hexc(0x030507));
  menu_view(&room_fb);
  room_light(room_far, lit_far, n_far, &lamps);
  room_light(room_near, lit_near, n_near, &lamps);
  FigMesh m[2] = { { lit_far, n_far, NULL }, { lit_near, n_near, NULL } };
  FigLight L = { { -0.62f, 0.36f, -0.7f }, { 200, 180, 120 }, { blue_rim.v[0], blue_rim.v[1], blue_rim.v[2] } };
  fig_draw_meshes(m, 2, &CAM, &L, fill_tri, NULL);
  int sw = room_fb.w, lw = room_lo.w, lh = room_lo.h;
  for (int ly = 0; ly < lh; ly++) {
    const uint32_t *r0 = room_fb.px + (size_t)(2 * ly) * sw, *r1 = 2 * ly + 1 < room_fb.h ? r0 + sw : r0;
    for (int lx = 0; lx < lw; lx++) {
      int x0 = 2 * lx, x1 = x0 + 1 < sw ? x0 + 1 : x0;
      uint32_t v = 0xFF000000u;
      for (int s = 0; s < 24; s += 8) {
        int sum = (int)((r0[x0] >> s) & 255) + (int)((r0[x1] >> s) & 255) + (int)((r1[x0] >> s) & 255) + (int)((r1[x1] >> s) & 255);
        v |= (uint32_t)((sum * 159) >> 10) << s; /* the mean, times 0.62 */
      }
      room_lo.px[(size_t)ly * lw + lx] = v;
    }
  }
  fb_clip_reset(&room_lo);
  menu_view(&room_lo);
  p_additive(P, true);
  glow(535, 340, 420, v3c(40, 90, 170), 0.06f * 0.62f);
  glow(1350, 340, 300, v3c(40, 90, 170), 0.05f * 0.62f);
  painter_begin(P, &room_lo);
  p_scale(P, 1.0f / (2 * d), 1.0f / (2 * d));
  p_additive(P, true);
  glow(x + w / 2, y + h / 2, w * 0.95f, v3c(40, 120, 150), 0.22f);
  p_additive(P, false);
  rect_c(x - m0 + 3 * K, y - m0 + 4 * K, w + 2 * m0, h + 2 * m0, RGBA(0, 0, 0, 0.55f));
}

/* room_lo up to out's size (d times), bilinear; not under the bezel's box (bx0..bx1, by0..by1) */
static void menu_upscale(Framebuffer *out, int d, int bx0, int by0, int bx1, int by1) {
  int W = out->w, H = out->h, sw = room_lo.w, sh = room_lo.h;
  for (int y = 0; y < H; y++) {
    int fy = (int)(((y * 2 + 1) * 128) / d) - 128, y0 = fy < 0 ? 0 : fy >> 8, ky = fy < 0 ? 0 : fy & 255;
    int y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
    const uint32_t *r0 = room_lo.px + (size_t)y0 * sw, *r1 = room_lo.px + (size_t)y1 * sw;
    uint32_t *o = out->px + (size_t)y * W;
    for (int x = 0; x < W; x++) {
      if (x == bx0 && bx1 > bx0 && y >= by0 && y < by1) x = bx1;
      if (x >= W) break;
      int fx = (int)(((x * 2 + 1) * 128) / d) - 128, x0 = fx < 0 ? 0 : fx >> 8, kx = fx < 0 ? 0 : fx & 255;
      int x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
      uint32_t a = r0[x0], b = r0[x1], c = r1[x0], e = r1[x1], v = 0xFF000000u;
      for (int s = 0; s < 24; s += 8) {
        int top = (int)((a >> s) & 255) * (256 - kx) + (int)((b >> s) & 255) * kx;
        int bot = (int)((c >> s) & 255) * (256 - kx) + (int)((e >> s) & 255) * kx;
        v |= (uint32_t)((top * (256 - ky) + bot * ky) >> 16) << s;
      }
      o[x] = v;
    }
  }
}

/* the display: gunmetal bezel, plate and screen face, on bezel_fb at out's size */
static void menu_bezel(float x, float y, float w, float h, float m0, float m1, float K, int k) {
  int dot = k * 2 / 3 > 1 ? k * 2 / 3 : 1;
  float ts = 10.0f * dot; /* the plate's lettering, in whole dots; on the bezel between its rim and the screen */
  fb_clip_reset(&bezel_fb);
  painter_begin(P, &bezel_fb);
  /* the bezel: gunmetal, lit from above, with its rim catching the overheads */
  Paint gm = p_linear(P, 0, y - m0, 0, y + h + m0);
  p_stop(&gm, 0, RGBA(74, 82, 90, 1));
  p_stop(&gm, 0.5f, RGBA(42, 47, 54, 1));
  p_stop(&gm, 1, RGBA(24, 27, 32, 1));
  p_fill_rect(P, x - m0, y - m0, w + 2 * m0, h + 2 * m0, &gm);
  stroke_rect(x - m0 + 0.5f, y - m0 + 0.5f, w + 2 * m0 - 1, h + 2 * m0 - 1, RGBA(140, 160, 175, 0.5f), 1);
  for (int i = 0; i < 4; i++) { /* the screws in its corners */
    float sx = i & 1 ? x + w + m0 * 0.5f : x - m0 * 0.5f, sy = i & 2 ? y + h + m0 * 0.5f : y - m0 * 0.5f;
    bezel_lamp(sx, sy, 1.3f * K, RGBA(110, 118, 126, 1));
    line_c(sx - K, sy, sx + K, sy, RGBA(20, 22, 26, 1), 0.6f * K);
  }
  /* its plate: the ship's name, and the data link */
  if (7 * dot <= m0 - m1 - 1) {
    float fy = y - (m0 + m1) / 2, ty = y + h + (m0 + m1) / 2 - 1;
    mono("CVN-90  STORMHAVEN    CIC", x + 4 * K, fy, ts, RGBA(205, 210, 200, 0.75f), ALIGN_LEFT);
    mono("TACTICAL DISPLAY 1", x + w - 4 * K, fy, ts, RGBA(205, 210, 200, 0.75f), ALIGN_RIGHT);
    mono("SORTIE CONTROL", x + 4 * K, ty, ts, RGBA(205, 210, 200, 0.6f), ALIGN_LEFT);
    mono("LINK", x + w - 16 * K, ty, ts, RGBA(205, 210, 200, 0.6f), ALIGN_RIGHT);
  }
  /* the screen: a black lip, then the face, dark green-blue glass with its faint grid and scan lines */
  rect_c(x - m1, y - m1, w + 2 * m1, h + 2 * m1, RGBA(6, 7, 8, 1));
  Paint face = RG(x + w / 2, y + h * 0.45f, 0, w * 0.75f, S_(0, RGBA(10, 30, 38, 1)), S_(1, RGBA(4, 12, 16, 1)));
  p_fill_rect(P, x, y, w, h, &face);
  for (float gx = x + 20 * K; gx < x + w; gx += 20 * K) line_c(gx, y, gx, y + h, RGBA(60, 200, 160, 0.05f), 1);
  for (float gy = y + 20 * K; gy < y + h; gy += 20 * K) line_c(x, gy, x + w, gy, RGBA(60, 200, 160, 0.05f), 1);
  for (float sy = y; sy < y + h; sy += 3) fb_rect_a(&bezel_fb, (int)x, (int)sy, (int)w, 1, 0xFF000000u, 0.14f);
}

void cic_menu_draw(Framebuffer *out, float t, int bx, int by, int bw, int bh, int k) {
  int W = out->w, H = out->h, d = (H + 359) / 360, step = (int)floorf(t * 10), rw = (W + d - 1) / d, rh = (H + d - 1) / d;
  int key[6] = { W, H, bx, by, bw, bh };
  /* the bezel as wide as the margin round the box allows (8 k at the least) */
  float x = (float)bx, y = (float)by, w = (float)bw, h = (float)bh, K = (float)k;
  float m0 = fmaxf(fminf(10 * K, y), 3 * K), m1 = 2 * K;
  bool sized = !memcmp(key, menu_key, sizeof key);
  if (!sized || step != menu_step) {
    if (!room_ready() || !fb_resize(&room_fb, rw, rh) || !fb_resize(&room_lo, (rw + 1) / 2, (rh + 1) / 2) ||
        !fb_resize(&menu_fb, W, H))
      return;
    if (!P) P = painter_new(&room_fb);
    if (!P) return;
    if (!sized) {
      if (!fb_resize(&bezel_fb, W, H)) return;
      menu_bezel(x, y, w, h, m0, m1, K, k);
      memcpy(menu_key, key, sizeof key);
    }
    /* the display over the room: the bezel's box is opaque */
    int x0 = (int)floorf(x - m0), y0 = (int)floorf(y - m0), x1 = (int)ceilf(x + w + m0), y1 = (int)ceilf(y + h + m0);
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > W ? W : x1, y1 = y1 > H ? H : y1;
    menu_room(d, t, x, y, w, h, m0, K);
    menu_upscale(&menu_fb, 2 * d, x0, y0, x1, y1);
    for (int r = y0; r < y1 && x1 > x0; r++)
      memcpy(menu_fb.px + (size_t)r * W + x0, bezel_fb.px + (size_t)r * W + x0, sizeof(uint32_t) * (size_t)(x1 - x0));
    /* its lamps: power, and the data link up */
    painter_begin(P, &menu_fb);
    float ty = y + h + (m0 + m1) / 2 - 1;
    bezel_lamp(x + w - 6 * K, ty, 1.6f * K, RGBA(90, 255, 140, 0.95f));
    bezel_lamp(x + w - 12 * K, ty, 1.6f * K, (int)(t * 1.5f) % 2 ? RGBA(255, 180, 60, 0.95f) : RGBA(90, 60, 20, 0.9f));
    for (size_t i = 0, n = (size_t)W * H; i < n; i++) menu_fb.px[i] |= 0xFF000000u; /* opaque, over the GPU's picture too */
    menu_step = step;
  }
  memcpy(out->px, menu_fb.px, sizeof(uint32_t) * (size_t)W * H);
}
