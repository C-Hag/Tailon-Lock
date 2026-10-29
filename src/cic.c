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
static void scope(void) {
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
  for (int i = 0; i < 30; i++) {
    float a = sw - i * 0.03f;
    line_c(rx, ry, rx + sinf(a) * rr, ry - cosf(a) * rr, RGBA(80, 255, 150, 0.4f * (1 - i / 30.0f)), 3);
  }
  R.state = 4711;
  for (int i = 0; i < 40; i++) { /* sea clutter */
    float a = rnd() * PI_F * 2, d = 0.3f + rnd() * 0.6f;
    rect_c(rx + sinf(a) * rr * d, ry - cosf(a) * rr * d, 2, 2, RGBA(60, 200, 110, 0.18f));
  }
  PColor own = RGBA(110, 170, 255, 0.95f);
  ring_c(rx, ry, 6, own, 2);
  p_begin(P), p_ellipse(P, rx + rr * 0.3f, ry - rr * 0.42f, 7, 7, 0, PI_F, 0), stroke_c(own, 2);
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
    line_c(hx, hy, hx + 3, hy + 30, hc, 2.5f);
  }
  if (T > 5.9f) {
    float a = -8.5f * PI_F / 180, d = range_at(0.87f, T), hx = rx + sinf(a) * rr * d, hy = ry - cosf(a) * rr * d, hs = 20;
    stroke_rect(hx - hs, hy - hs, hs * 2, hs * 2, RGBA(255, 220, 90, 0.9f), 1.5f);
    float bx = dw * 0.79f, by = 26;
    rect_c(bx - 8, by - 14, 112, 128, RGBA(0, 30, 15, 0.85f));
    static const char *const HK[7] = { "HOOK 7402", "HOSTILE AIR", "BRG  350", "RNG  62", "SPD  540", "ALT  2000", "CPA  4 MIN" };
    for (int i = 0; i < 7; i++) mono(HK[i], bx, by + i * 17, 11, i < 2 ? HEX(0xff8a72) : RGBA(120, 255, 170, 0.9f), ALIGN_LEFT);
  }
  rect_c(0, 0, dw, dh * 0.4f, RGBA(160, 255, 200, 0.03f)); /* the glass */
}
static void screen_content(const RoomScreen *s) {
  switch (s->kind) {
  case RS_SCOPE: scope(); break;
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
/* a man speaking his line: the mouth from the words, a blink as he finishes, his eyes on gaze */
static void voice(FigSpec *f, const char *text, const float L[2], float blink_seed, const float gaze[3]) {
  FigSpeech sp;
  fig_speech(text, L[0], L[1], &sp);
  fig_mouth(&sp, T, &f->face.open, &f->face.wide, &f->face.round);
  float after = T - L[1], b = after > 0.02f && after < 0.2f ? 1 - fabsf(after - 0.11f) / 0.09f : 0;
  f->face.blink = fmaxf(blink_at(blink_seed), b);
  memcpy(f->face.gaze, gaze, sizeof f->face.gaze);
  f->face.has_gaze = true;
}
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
  T = t;
  S0 = t0, S1 = t1;
  float u = clampf_((t - t0) / (t1 - t0), 0, 1);
  alert = t >= L2[0];
  gq = alert ? 0.5f + 0.5f * sinf((t - L2[0]) * PI_F * 2 / 1.1f) : 0;
  R.state = 4711;
  int W = out->w, H = out->h;
  /* it opens tight on the two contacts on the scope, then pulls out to the operator and the officers behind him */
  float zo = ease(clampf_((t - 6.25f) / 1.35f, 0, 1)), zw = 1.04f + 0.1f * ease(u), zc = 3.6f + 0.35f * (t - t0);
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
  if (!fb_resize(&bg, W, H)) return;
  fb_clip_reset(&bg);
  fb_clear(&bg, hexc(0x030507));
  if (!P) P = painter_new(&bg);
  if (!P) return;
  painter_begin(P, &bg);
  p_translate(P, W / 2.0f, H / 2.0f);
  p_scale(P, sc * z, sc * z);
  p_translate(P, -camx, -camy);
  seam = 0.6f / (sc * z);
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
  p_blur(&bg, (int)lroundf(1.3f * sc) > 0 ? (int)lroundf(1.3f * sc) : 1, 0.85f);
  memcpy(out->px, bg.px, sizeof(uint32_t) * (size_t)W * H);
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
    float g[3];
    room_at(1395, 330, 1.75f / 8 * CAM.F / 178, g);
    voice(&f, "ALERT, ALERT! WE HAVE INBOUND.", L2, 0.3f, g);
    f.face.brow_up = alert ? 0.7f : 0;
    f.pose.ang[FB_HEAD][0] = alert ? -4.0f : 0.0f, f.pose.ang[FB_HEAD][1] = -6, f.pose.ang[FB_HEAD][2] = 0;
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
    float g[3];
    room_at(310, 450, 1.25f, g);
    voice(&f, "BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.", L1, 0.7f, g);
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
    float look[3];
    room_at(turn > 0.5f ? 1050 : 760, 300, 1.75f / 8 * CAM.F / 130, look);
    voice(&f, "SEND THE BIRDS.", L3, 0.1f, look);
    f.face.frown = t < L3[0] ? 0.35f : 0;
    f.pose.ang[FB_HEAD][0] = nod * 2, f.pose.ang[FB_HEAD][1] = 0, f.pose.ang[FB_HEAD][2] = 0;
    crew3d(&f, 1395 - turn * 10, 246 + 3 * sinf(t * 1.3f) - turn * 4 + nod, 178, -100 - 14 * turn, mul(mix(v3c(130, 180, 240), key_red, red_k), 0.95f - 0.1f * turn), 2);
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
