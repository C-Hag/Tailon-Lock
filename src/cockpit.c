/* cockpit.c - the instrument panel under each view. Every jet has its own
 * panel, after the eight cockpits of the hangar page (assets/jets): the
 * AF-40's panoramic glass, the LF-27's green MFDs and round gauges, the
 * ECF-90's three colour displays, the JA 46's Swedish analog panel, the
 * SY-26's wide LCD, the Vk-33's turquoise Soviet panel, the Ks-55's Russian
 * glass and the NF-24's 1970s Navy panel with its wing-sweep gauge. The
 * built-in jet keeps the original panel (from Ace4Combat, the game's old name).
 *
 * Everything is laid out on a 320x64 grid that is scaled to the panel. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/sim.h"
#include "core/jets.h"
#include "core/map.h"
#include "hud.h"
#include "units.h"

static inline uint32_t C(uint32_t c) { return hexc(c); }

typedef struct {
  Framebuffer *fb;
  float ox, oy, k;
  int   ts, ss;           /* text scale: normal and small */
  int   x, y, w, h;       /* the panel, to restore the clip */
} Pan;

/* What the instruments read, worked out once per panel. */
typedef struct {
  Game *g;
  Plane *p;
  bool  metric, blink;
  float spd, alt, vs;     /* in the jet's units: kt/ft/ft per min or km/h, m, m/s */
  float mach, hdg, gl, rpm, fuel, hull;
  bool  lock, warn, stall, lowfuel, lowhull, ab;
  int   nthreat;
  float threat[16];       /* bearings of hostiles within radar range, degrees */
  bool  mthreat[16];      /* that one is a missile guiding on us */
} Ins;

static float PX(const Pan *P, float x) { return P->ox + x * P->k; }
static float PY(const Pan *P, float y) { return P->oy + y * P->k; }
static float PL(const Pan *P, float l) { return l * P->k; }

static void prect(const Pan *P, float x, float y, float w, float h, uint32_t c) {
  int x0 = (int)roundf(PX(P, x)), y0 = (int)roundf(PY(P, y));
  int x1 = (int)roundf(PX(P, x + w)), y1 = (int)roundf(PY(P, y + h));
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;
  fb_rect(P->fb, x0, y0, x1 - x0, y1 - y0, c);
}
static void pframe(const Pan *P, float x, float y, float w, float h, uint32_t c) {
  fb_rect_stroke(P->fb, roundf(PX(P, x)), roundf(PY(P, y)), roundf(PL(P, w)), roundf(PL(P, h)), c, 1);
}
static void pline(const Pan *P, float x0, float y0, float x1, float y1, uint32_t c, float lw) {
  fb_line(P->fb, PX(P, x0), PY(P, y0), PX(P, x1), PY(P, y1), c, fmaxf(1, lw * P->k), 1);
}
static void ptri(const Pan *P, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
  float xy[6] = { PX(P, x0), PY(P, y0), PX(P, x1), PY(P, y1), PX(P, x2), PY(P, y2) };
  fb_poly(P->fb, xy, 3, c, 1, 0);
}
static void pclip(const Pan *P, float x, float y, float w, float h) {
  fb_clip(P->fb, (int)roundf(PX(P, x)), (int)roundf(PY(P, y)), (int)roundf(PL(P, w)), (int)roundf(PL(P, h)));
}
static void punclip(const Pan *P) { fb_clip(P->fb, P->x, P->y, P->w, P->h); }

void (*hud_text_cut_hook)(const char *full, const char *shown);
void (*hud_text_over_hook)(const char *s);
void (*hud_pips_hook)(float x, float y, float w, float h, float step, int n, int max, uint32_t on, uint32_t off, int kind);
void (*hud_dial_hook)(float cx, float cy, float r, float a0, float a1, float lo, float hi, float v, const char *title, float v2,
                      float len2);

static void ptext_sh(const Pan *P, const char *s, float x, float y, uint32_t c, int align, int scale, float maxw, int shadow);
/* a box for a number: w grid units, or as wide as the number needs at the panel's small text and a unit to
 * spare (a low-res panel); a number is never cut, so its box grows instead */
static float fit_w(const Pan *P, const char *s, float w) { return fmaxf(w, text_width(s, P->ss) / P->k + 1); }
/* Text no wider than maxw grid units (0: no limit), shrinking if it must. */
static void ptext_at(const Pan *P, const char *s, float x, float y, uint32_t c, int align, int scale, float maxw) {
  ptext_sh(P, s, x, y, c, align, scale, maxw, 0);
}
/* the same with fb_text's shadow (2: an outline) */
static void ptext_sh(const Pan *P, const char *s, float x, float y, uint32_t c, int align, int scale, float maxw, int shadow) {
  while (maxw > 0 && scale > 1 && text_width(s, scale) > PL(P, maxw)) scale--;
  char cut[64];
  /* still too wide on a low-res panel: drop characters from the end, but never from a number (447 cut to 44
   * reads as a wrong speed; a number a little wider than its box is better) */
  if (maxw > 0 && text_width(s, scale) > PL(P, maxw) && !strpbrk(s, "0123456789")) {
    snprintf(cut, sizeof cut, "%s", s);
    size_t n = strlen(cut);
    while (n > 1 && text_width(cut, scale) > PL(P, maxw)) {
      do n--;
      while (n > 0 && (cut[n] & 0xc0) == 0x80);
      cut[n] = 0;
    }
    if (hud_text_cut_hook) hud_text_cut_hook(s, cut);
    s = cut;
  }
  /* still wider than its box (a number, never cut): the tests want to know */
  if (maxw > 0 && hud_text_over_hook && text_width(s, scale) > PL(P, maxw) + 0.5f) hud_text_over_hook(s);
  fb_text(P->fb, s, PX(P, x), PY(P, y), scale, c, align, shadow);
}
static void ptext(const Pan *P, const char *s, float x, float y, uint32_t c, int align) {
  ptext_at(P, s, x, y, c, align, P->ts, 0);
}
static void stext(const Pan *P, const char *s, float x, float y, uint32_t c, int align, float maxw) {
  ptext_at(P, s, x, y, c, align, P->ss, maxw);
}

/* A panel face shading from top to bottom, with a glareshield lip. */
static void face(const Pan *P, uint32_t top, uint32_t bot, uint32_t lip) {
  Framebuffer *fb = P->fb;
  int r0 = (top >> 16) & 255, g0 = (top >> 8) & 255, b0 = top & 255;
  int r1 = (bot >> 16) & 255, g1 = (bot >> 8) & 255, b1 = bot & 255;
  for (int y = P->y; y < P->y + P->h; y++) {
    float t = (float)(y - P->y) / fmaxf(1, (float)P->h - 1);
    fb_rect(fb, P->x, y, P->w, 1,
            rgb((uint8_t)(r0 + (r1 - r0) * t), (uint8_t)(g0 + (g1 - g0) * t), (uint8_t)(b0 + (b1 - b0) * t)));
  }
  fb_rect(fb, P->x, P->y, P->w, (int)fmaxf(2, P->h / 22.0f), C(lip));
}

static void lamp(const Pan *P, const char *label, float x, float y, float w, float h, bool on, uint32_t col) {
  uint32_t off = C(0x262626);
  prect(P, x, y, w, h, on ? C(col) : off);
  /* lit, its frame a dark shade of its own colour: the dark legend touches the frame on a small panel and would
   * run into a black one */
  pframe(P, x, y, w, h, on ? C((col >> 2) & 0x3f3f3f) : C(0x0a0a0a));
  /* an unlit legend still reads, as an engraved one does (0x5a5a5a on 0x262626 was 2.3:1; this is 5.6:1) */
  uint32_t ink = on ? C(0x101010) : C(0x9a9a9a);
  const char *sp = strchr(label, ' ');
  /* too long for the lamp on a low-res panel but tall enough for two lines: split at the space, whole words */
  if (sp && text_width(label, P->ss) > PL(P, w - 1) && PL(P, h) >= 14 * P->ss + 1) {
    char l1[32];
    snprintf(l1, sizeof l1, "%.*s", (int)(sp - label), label);
    float lh = 4.0f * P->ss / P->k;
    stext(P, l1, x + w / 2, y + h / 2 - lh, ink, ALIGN_CENTER, w - 1);
    stext(P, sp + 1, x + w / 2, y + h / 2 + lh, ink, ALIGN_CENTER, w - 1);
    return;
  }
  stext(P, label, x + w / 2, y + h / 2, ink, ALIGN_CENTER, w - 1);
}

/* A row of lamps from x across w, each as wide as its word wants (a share by letters, so a long word gets the
 * room a short one does not need); their lefts and widths in xs, ws */
static void lamp_row(const char *const *label, int n, float x, float w, float gap, float *xs, float *ws) {
  float sum = 0, len[16];
  for (int i = 0; i < n; i++) {
    len[i] = 0.35f;
    for (const char *c = label[i]; *c; c++) len[i] += (*c & 0xc0) != 0x80;
    sum += len[i];
  }
  float unit = (w - (n - 1) * gap) / sum;
  for (int i = 0; i < n; i++) ws[i] = len[i] * unit, xs[i] = x, x += ws[i] + gap;
}

/* The gear lamp: off with the gear up, amber on its way, green down and locked, red down (or on its way) past the
 * speed it may be flown at */
static void gear_lamp(const Pan *P, const Ins *I, const char *label, float x, float y, float w, float h) {
  const Plane *p = I->p;
  bool up = p->gearT <= 0.01f, down = p->gearT >= 0.99f;
  uint32_t col = !up && p->speed > GEAR_VLE ? 0xff3030 : down ? 0x33ff33 : 0xffb020;
  lamp(P, label, x, y, w, h, !up, col);
}

static void hbar(const Pan *P, float x, float y, float w, float h, float v, uint32_t col, uint32_t back) {
  prect(P, x, y, w, h, back);
  float f = roundf(w * clampf(v, 0, 1) * 4) / 4;
  if (f > 0) prect(P, x, y, f, h, col);
}

static void lives(const Pan *P, const Plane *p, float x, float y, float s, uint32_t on, uint32_t off) {
  for (int i = 0; i < LIVES; i++) {
    float jx = x - i * s * 2.4f;
    ptri(P, jx, y - s, jx + s, y + s, jx - s, y + s, i < p->lives ? on : off);
  }
}

/* Every missile the jet carries as a mark, spent ones dimmed: its heat-seekers as rounds, then its radar
 * missiles (cyan) and its air-to-ground ones (squat, wider), as many of each as its loadout holds, closed up to
 * fit avail grid units */
static void stores_marks(const Pan *P, const Ins *I, float x, float y, float h, float avail, uint32_t ink, uint32_t dim) {
  const Plane *p = I->p;
  int full[4];
  game_full_load(I->g, p, full);
  const int have[3] = { p->missiles, p->radar, p->ground };
  const uint32_t on[3] = { ink, C(0x59d6ff), ink };
  int slots = 0, groups = 0;
  for (int k = 0; k < 3; k++) slots += full[k] * (k == 2 ? 2 : 1), groups += full[k] > 0;
  float w0 = fmaxf(1.4f, h * 0.3f), sp = fminf(w0 * 2, avail / fmaxf(1, slots + 0.6f * (groups - 1)));
  float w = sp * 0.5f;
  for (int k = 0; k < 3; k++) {
    if (full[k] <= 0) continue;
    float step = k == 2 ? sp * 2 : sp, pw = k == 2 ? sp * 1.4f : w;
    if (hud_pips_hook) hud_pips_hook(PX(P, x), PY(P, y + w0), PL(P, pw), PL(P, h - w0), PL(P, step), have[k], full[k], on[k], dim, k);
    for (int i = 0; i < full[k]; i++) {
      uint32_t c = i < have[k] ? on[k] : dim;
      float px = x + i * step;
      prect(P, px, y + w0, pw, h - w0, c);
      if (k < 2) ptri(P, px, y + w0, px + pw, y + w0, px + pw / 2, y, c);
    }
    x += full[k] * step + 0.6f * sp;
  }
}

/* ---- round gauges ------------------------------------------------------ */
typedef struct {
  float lo, hi, a0, a1;   /* value range and its sweep, degrees clockwise from 12 o'clock */
  int   ticks;            /* major divisions */
  uint32_t face, ink, rim, ncol, ncol2;
  const char *title, *unit, *readout;
  float red_lo, red_hi;   /* a red arc when red_hi > red_lo */
  float v, v2;            /* needles; v2 is drawn when ncol2 is set */
  float v2len;
} Dial;

static float dial_ang(const Dial *d, float v) {
  float t = clampf((v - d->lo) / (d->hi - d->lo), 0, 1);
  return (d->a0 + (d->a1 - d->a0) * t) * (PI_F / 180);
}
static void polar(float cx, float cy, float r, float a, float *x, float *y) {
  *x = cx + sinf(a) * r;
  *y = cy - cosf(a) * r;
}

/* A dial's label centred at (x, y), outlined dark: drawn over the ticks and the needle, which stop at each
 * letter's edge and still show between the letters */
static void face_text(const Pan *P, const Dial *d, const char *s, float x, float y, int sc, float maxw) {
  ptext_sh(P, s, x, y, d->ink, ALIGN_CENTER, sc, maxw, 2);
}

/* A dial's title above its hub, outlined so no tick runs through a letter. Too long for the face at
 * this size (a low-res panel), it goes on two lines, split between letters, rather than lose its end. */
static float dial_title(const Pan *P, const Dial *d, float x, float y, float R) {
  int sc = P->ss;
  float maxw = R * 1.6f;
  while (sc > 1 && text_width(d->title, sc) > PL(P, maxw)) sc--;
  char line[2][32];
  int nl = 1;
  snprintf(line[0], sizeof line[0], "%s", d->title);
  int n = 0;
  for (const char *c = d->title; *c; c++) n += (*c & 0xc0) != 0x80;
  if (text_width(d->title, sc) > PL(P, maxw) && n > 4) {
    int half = (n + 1) / 2, k = 0;
    const char *c = d->title;
    for (; *c && k < half; c++) k += (*(c + 1) & 0xc0) != 0x80;
    snprintf(line[0], sizeof line[0], "%.*s", (int)(c - d->title), d->title);
    snprintf(line[1], sizeof line[1], "%s", c);
    nl = 2;
  }
  float lh = 8.0f * sc / P->k; /* a line, in grid units */
  for (int i = 0; i < nl; i++) face_text(P, d, line[i], x, y - R * 0.3f + (nl == 2 ? (i - 0.5f) * lh : 0), sc, maxw);
  return y - R * 0.3f + (nl == 2 ? lh : lh / 2); /* where its last line ends */
}

static void dial(const Pan *P, float x, float y, float R, const Dial *d) {
  Framebuffer *fb = P->fb;
  float cx = PX(P, x), cy = PY(P, y), r = PL(P, R);
  fb_circle(fb, cx, cy, r, d->rim ? d->rim : C(0x2a2c2e));
  fb_circle(fb, cx, cy, r * 0.9f, d->face ? d->face : C(0x0c0c0c));
  float lw = fmaxf(1, P->k * 0.35f);
  if (d->red_hi > d->red_lo)
    for (int i = 0; i <= 12; i++) {
      float a = dial_ang(d, d->red_lo + (d->red_hi - d->red_lo) * i / 12), x0, y0, x1, y1;
      polar(cx, cy, r * 0.86f, a, &x0, &y0);
      polar(cx, cy, r * 0.72f, a, &x1, &y1);
      fb_line(fb, x0, y0, x1, y1, C(0xd02020), lw * 1.5f, 1);
    }
  for (int i = 0; i <= d->ticks * 2; i++) {
    float a = dial_ang(d, d->lo + (d->hi - d->lo) * i / (d->ticks * 2)), x0, y0, x1, y1;
    if (fabsf(d->a1 - d->a0) >= 359 && i == d->ticks * 2) break;
    polar(cx, cy, r * 0.86f, a, &x0, &y0);
    polar(cx, cy, r * (i % 2 ? 0.76f : 0.64f), a, &x1, &y1);
    fb_line(fb, x0, y0, x1, y1, d->ink, i % 2 ? lw : lw * 1.4f, 1);
  }

  float ww = d->readout ? fit_w(P, d->readout, R * 1.0f) : R, hh = R * 0.36f; /* the window as wide as its number */
  if (d->readout) prect(P, x - ww / 2, y + R * 0.22f, ww, hh, C(0x000000)); /* the number window, under the needle */
  float x1, y1, x0, y0, a = dial_ang(d, d->v);
  if (d->ncol2) {
    float a2 = dial_ang(d, d->v2);
    polar(cx, cy, r * (d->v2len > 0 ? d->v2len : 0.8f), a2, &x1, &y1);
    fb_line(fb, cx, cy, x1, y1, d->ncol2, fmaxf(1, P->k * 0.55f), 1);
  }
  polar(cx, cy, r * 0.8f, a, &x1, &y1);
  polar(cx, cy, -r * 0.15f, a, &x0, &y0);
  fb_line(fb, x0, y0, x1, y1, d->ncol ? d->ncol : C(0xf2f0e4), fmaxf(1, P->k * 0.6f), 1);
  /* the hub under the words (drawn after them it covered the middle letter of a title's second line and the foot
   * of a short one: "TAL" read "T?L", the G dial's "G" not at all) */
  fb_circle(fb, cx, cy, fmaxf(1, r * 0.1f), C(0x3a3a3a));
  /* the digits over the needle, outlined in the window's black: the needle shows through the window but never
   * across a digit */
  if (d->readout) ptext_sh(P, d->readout, x, y + R * 0.22f + hh / 2, d->ink, ALIGN_CENTER, P->ss, ww - 0.5f, 2);
  /* the title and the unit over the needle too, outlined: the needle is never drawn across a letter */
  float tb = d->title ? dial_title(P, d, x, y, R) : y;
  /* the unit below the title with a pixel between (under a title on two lines it ran into the second: "%" read
   * as part of "TAL") */
  if (!d->readout && d->unit) { /* (but inside the face: on a 320-pixel panel it went on to the dial below) */
    float half = 4.0f * P->ss / P->k;
    face_text(P, d, d->unit, x, y + fmaxf(R * 0.42f, fminf(tb - y + half + 2 / P->k, R * 0.9f - half)), P->ss, R * 1.3f);
  }
  if (hud_dial_hook)
    hud_dial_hook(cx, cy, r, d->a0, d->a1, d->lo, d->hi, d->v, d->title, d->ncol2 ? d->v2 : 0,
                  d->ncol2 ? (d->v2len > 0 ? d->v2len : 0.8f) : 0);
}

/* ---- attitude ------------------------------------------------------------ */
static void bank_pitch(const Plane *p, float *bank, float *pitch) {
  *bank = atan2f(-p->r.y, p->u.y);
  *pitch = asinf(clampf(p->f.y, -1, 1));
}

/* A round attitude ball. soviet: the horizon only pitches and the aircraft
 * symbol banks, as on the KPP. */
static void ball(const Pan *P, const Plane *p, float x, float y, float R, uint32_t sky, uint32_t gnd, uint32_t sym,
                 bool soviet) {
  Framebuffer *fb = P->fb;
  float cx = PX(P, x), cy = PY(P, y), r = PL(P, R), bank, pitch;
  bank_pitch(p, &bank, &pitch);
  float off = pitch / (PI_F / 2) * r * 1.3f;
  float cb = soviet ? 1 : cosf(bank), sb = soviet ? 0 : sinf(bank);
  fb_circle(fb, cx, cy, r + fmaxf(1, P->k * 0.6f), C(0x202020));
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  for (int yy = y0; yy <= y1; yy++) {
    if (yy < fb->cy0 || yy >= fb->cy1) continue;
    float dy = yy + 0.5f - cy;
    for (int xx = (int)floorf(cx - r); xx <= (int)ceilf(cx + r); xx++) {
      if (xx < fb->cx0 || xx >= fb->cx1) continue;
      float dx = xx + 0.5f - cx;
      if (dx * dx + dy * dy > r * r) continue;
      float by = sb * dx + cb * dy;
      fb->px[(size_t)yy * fb->w + xx] = fabsf(by - off) < 0.5f * fmaxf(1, P->k * 0.6f) ? C(0xffffff) : by > off ? gnd : sky;
    }
  }
  float lw = fmaxf(1, P->k * 0.5f), s = r * 0.55f;
  float c = soviet ? cosf(bank) : 1, n = soviet ? sinf(bank) : 0;
  /* wings (rotated by bank on the soviet ball), with a centre dot */
  fb_line(fb, cx - c * s, cy + n * s, cx - c * s * 0.3f, cy + n * s * 0.3f, sym, lw * 1.4f, 1);
  fb_line(fb, cx + c * s * 0.3f, cy - n * s * 0.3f, cx + c * s, cy - n * s, sym, lw * 1.4f, 1);
  fb_circle(fb, cx, cy, fmaxf(1, lw), sym);
  if (soviet) fb_line(fb, cx, cy, cx - n * s * 0.4f, cy - c * s * 0.4f, sym, lw, 1);
}

/* A glass primary flight display: horizon and pitch ladder filling a box,
 * speed on the left, altitude on the right, heading on top. */
static void pfd(const Pan *P, const Ins *I, float x, float y, float w, float h, uint32_t sky, uint32_t gnd, uint32_t ink) {
  Framebuffer *fb = P->fb;
  const Plane *p = I->p;
  float bank, pitch;
  bank_pitch(p, &bank, &pitch);
  float cx = PX(P, x + w / 2), cy = PY(P, y + h / 2), ppd = PL(P, h) / 50; /* px per degree: +-25 in view */
  float off = pitch * (180 / PI_F) * ppd, cb = cosf(bank), sb = sinf(bank);
  pclip(P, x, y, w, h);
  for (int yy = fb->cy0; yy < fb->cy1; yy++) {
    float dy = yy + 0.5f - cy;
    for (int xx = fb->cx0; xx < fb->cx1; xx++) {
      float by = sb * (xx + 0.5f - cx) + cb * dy;
      fb->px[(size_t)yy * fb->w + xx] = by > off ? gnd : sky;
    }
  }
  float lw = fmaxf(1, P->k * 0.3f);
#define BX(bx, by) (cx + cb * (bx) + sb * (by))
#define BY(bx, by) (cy - sb * (bx) + cb * (by))
  fb_line(fb, BX(-4 * w, off), BY(-4 * w, off), BX(4 * w, off), BY(4 * w, off), ink, lw, 1);
  for (int q = -60; q <= 60; q += 10) {
    if (!q) continue;
    float by = off - q * ppd, hw = PL(P, w) * (q % 20 ? 0.08f : 0.14f);
    fb_line(fb, BX(-hw, by), BY(-hw, by), BX(hw, by), BY(hw, by), ink, lw, 1);
  }
#undef BX
#undef BY
  uint32_t sym = C(0xffd23a);
  float s = PL(P, w) * 0.16f;
  fb_line(fb, cx - s, cy, cx - s * 0.35f, cy, sym, lw * 2, 1);
  fb_line(fb, cx + s * 0.35f, cy, cx + s, cy, sym, lw * 2, 1);
  fb_circle(fb, cx, cy, fmaxf(1, lw * 1.2f), sym);
  char b[16];
  float bw0 = fminf(16, w * 0.28f), bh = fminf(7, h * 0.18f), bw;
  snprintf(b, sizeof b, "%d", (int)roundf(I->spd));
  bw = fit_w(P, b, bw0);
  prect(P, x, y + h / 2 - bh / 2, bw, bh, C(0x000000));
  stext(P, b, x + bw / 2, y + h / 2, I->stall ? C(WARN) : ink, ALIGN_CENTER, bw - 0.5f);
  snprintf(b, sizeof b, "%d", (int)roundf(I->alt / 10) * 10);
  bw = fit_w(P, b, bw0);
  prect(P, x + w - bw, y + h / 2 - bh / 2, bw, bh, C(0x000000));
  stext(P, b, x + w - bw / 2, y + h / 2, ink, ALIGN_CENTER, bw - 0.5f);
  snprintf(b, sizeof b, "%03d", (int)roundf(I->hdg) % 360);
  bw = fit_w(P, b, bw0);
  prect(P, x + w / 2 - bw / 2, y, bw, bh * 0.9f, C(0x000000));
  stext(P, b, x + w / 2, y + bh * 0.45f, ink, ALIGN_CENTER, bw - 0.5f);
  punclip(P);
}

/* ---- radar --------------------------------------------------------------- */
typedef struct {
  bool fan;               /* a +-60 degree sector from the bottom edge, else a round scope */
  float range;            /* metres to the edge */
  uint32_t bg, grid, own, foe, friend_, tgt, msl;
  bool sweep, boundary;
} Scope;

/* world -> scope pixels; false when outside the scope (round scopes can
 * pin a contact to the rim instead) */
typedef struct { vec3 at; float ch, sn, sc, cx, cy, R; bool fan; } ScopeMap;
static bool scope_map(const ScopeMap *M, vec3 w, float *ox, float *oy, bool clampit) {
  float dx = w.x - M->at.x, dz = w.z - M->at.z;
  float rx = (dx * M->ch - dz * M->sn) * M->sc, ry = (dx * M->sn + dz * M->ch) * M->sc;
  float d = sqrtf(rx * rx + ry * ry);
  bool in = d <= M->R - 1 && (!M->fan || (ry > 0 && fabsf(rx) <= ry * 1.733f));
  if (!in && clampit && !M->fan && d > 0) {
    rx *= (M->R - 2) / d;
    ry *= (M->R - 2) / d;
    in = true;
  }
  *ox = M->cx + rx;
  *oy = M->cy - ry;
  return in;
}

static void scope(const Pan *P, const Ins *I, float x, float y, float w, float h, const Scope *S) {
  Framebuffer *fb = P->fb;
  Game *g = I->g;
  Plane *p = I->p;
  float cx, cy, R;
  if (S->fan) {
    R = fminf(PL(P, h) * 0.96f, PL(P, w) / 2 / 0.866f);
    cx = PX(P, x + w / 2);
    cy = PY(P, y) + (PL(P, h) + R) / 2;
    if (cy - R < PY(P, y)) cy = PY(P, y) + R;
  } else {
    R = fminf(PL(P, w), PL(P, h)) / 2;
    cx = PX(P, x + w / 2);
    cy = PY(P, y + h / 2);
  }
  if (S->fan) {
    float xy[2 * 16];
    xy[0] = cx;
    xy[1] = cy;
    for (int i = 0; i <= 12; i++) polar(cx, cy, R, (-60 + i * 10) * (PI_F / 180), &xy[2 + i * 2], &xy[3 + i * 2]);
    fb_poly(fb, xy, 14, S->bg, 1, 0);
    for (int k = 1; k <= 3; k++) {
      float px0, py0;
      polar(cx, cy, R * k / 3, -60 * (PI_F / 180), &px0, &py0);
      for (int i = 1; i <= 12; i++) {
        float px1, py1;
        polar(cx, cy, R * k / 3, (-60 + i * 10) * (PI_F / 180), &px1, &py1);
        fb_line(fb, px0, py0, px1, py1, S->grid, 1, 1);
        px0 = px1;
        py0 = py1;
      }
    }
    for (int a = -60; a <= 60; a += 30) {
      float ex, ey;
      polar(cx, cy, R, a * (PI_F / 180), &ex, &ey);
      fb_line(fb, cx, cy, ex, ey, S->grid, 1, 1);
    }
  } else {
    fb_circle(fb, cx, cy, R, S->bg);
    fb_ring(fb, cx, cy, R / 2, S->grid, 1);
    fb_line(fb, cx - R, cy, cx + R, cy, S->grid, 1, 1);
    fb_line(fb, cx, cy - R, cx, cy + R, S->grid, 1, 1);
  }
  if (S->sweep) {
    float a = S->fan ? sinf(g->time * 1.2f) * 60 * (PI_F / 180) : fmodf(g->time * 2.5f, 2 * PI_F), ex, ey;
    polar(cx, cy, R, a, &ex, &ey);
    fb_line(fb, cx, cy, ex, ey, S->own, fmaxf(1, P->k * 0.4f), 0.8f);
  }
  vec3 rpos = p->alive || !p->hasDeath ? p->pos : p->deathPos;
  float hh = atan2f(p->f.x, p->f.z);
  ScopeMap M = { rpos, cosf(hh), sinf(hh), R / S->range, cx, cy, R, S->fan };
  float s = fmaxf(1, P->k * 0.7f);
#define RP(w, ox, oy, clampit) scope_map(&M, (w), &(ox), &(oy), (clampit))
  float bx, by;
  if (S->boundary && !S->fan) {
    fb_clip_circle(fb, cx, cy, R);
    vec3 cn[4] = { v3(-ARENA_X, 0, -ARENA_Z), v3(ARENA_X, 0, -ARENA_Z), v3(ARENA_X, 0, ARENA_Z), v3(-ARENA_X, 0, ARENA_Z) };
    float qx[4], qy[4];
    for (int i = 0; i < 4; i++) (void)RP(cn[i], qx[i], qy[i], false);
    for (int i = 0; i < 4; i++) fb_line(fb, qx[i], qy[i], qx[(i + 1) % 4], qy[(i + 1) % 4], S->grid, 1, 1);
    fb_clip_circle_off(fb);
  }
  for (int t = 0; t < 2; t++)
    if (RP(v3(BASES[t].x, 0, BASES[t].z), bx, by, false))
      fb_rect_stroke(fb, bx - 2.5f * s, by - 2.5f * s, 5 * s, 5 * s, t == p->team ? S->friend_ : S->foe, 1);
  for (int i = 0; i < g->ntargets; i++) {
    const Target *t = &g->targets[i];
    if (t->alive && RP(t->pos, bx, by, false))
      fb_rect(fb, (int)(bx - 0.5f * s), (int)(by - 0.5f * s), (int)fmaxf(1, 1.5f * s), (int)fmaxf(1, 1.5f * s),
              t->team == p->team ? S->friend_ : S->tgt);
  }
  for (int t = 0; t < 2; t++) { /* the tankers: ours and theirs, a cross (theirs is a target too) */
    const Tanker *T = &g->tankers[t];
    if (T->gone || !RP(T->pos, bx, by, false)) continue;
    uint32_t c = T->team == p->team ? S->friend_ : S->tgt;
    fb_rect(fb, (int)(bx - 1.5f * s), (int)(by - 0.5f * s), (int)(4 * s), (int)fmaxf(1, 1.5f * s), c);
    fb_rect(fb, (int)(bx - 0.5f * s), (int)(by - 1.5f * s), (int)fmaxf(1, 1.5f * s), (int)(4 * s), c);
  }
  if (g->def.on && RP(g->def.pos, bx, by, false))
    fb_rect(fb, (int)(bx - 2 * s), (int)(by - 3 * s), (int)fmaxf(2, 4 * s), (int)fmaxf(3, 6 * s), S->friend_);
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    if (!ref_eq(m->target, ref_plane(p->idx)) && !m->ashm) continue;
    if (RP(m->pos, bx, by, false)) fb_rect(fb, (int)(bx - s), (int)(by - s), (int)(2 * s), (int)(2 * s), S->msl);
  }
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *e = &g->planes[i];
    if (e == p || !e->alive) continue;
    bool foe = e->team != p->team;
    if (foe && ((int)(g->time * 3)) % 3 == 0) continue;
    if (!RP(e->pos, bx, by, true)) continue;
    float z = (foe ? 2 : 1.5f) * s;
    fb_rect(fb, (int)(bx - z), (int)(by - z), (int)(z * 2), (int)(z * 2), foe ? S->foe : S->friend_);
  }
#undef RP
  float ty = S->fan ? cy - 1.5f * s : cy;
  float xy[6] = { cx, ty - 3 * s, cx + 2.5f * s, ty + 2.5f * s, cx - 2.5f * s, ty + 2.5f * s };
  fb_poly(fb, xy, 3, S->own, 1, 0);
}

/* Lights round a small aircraft for each eighth with a threat in it (the
 * SPO / RWR idea): amber for jets, red for missiles guiding on us. */
static void threat_ring(const Pan *P, const Ins *I, float x, float y, float R, uint32_t ink, bool lights) {
  float k = P->k;
  if (!lights) {
    fb_circle(P->fb, PX(P, x), PY(P, y), PL(P, R), C(0x05080a));
    fb_ring(P->fb, PX(P, x), PY(P, y), PL(P, R) * 0.5f, C(0x2a3036), 1);
    fb_ring(P->fb, PX(P, x), PY(P, y), PL(P, R), C(0x303438), 1);
  }
  pline(P, x, y - R * 0.35f, x, y + R * 0.35f, ink, 0.4f);
  pline(P, x - R * 0.3f, y - R * 0.05f, x + R * 0.3f, y - R * 0.05f, ink, 0.4f);
  pline(P, x - R * 0.15f, y + R * 0.3f, x + R * 0.15f, y + R * 0.3f, ink, 0.4f);
  for (int d = 0; d < 8; d++) {
    float a = (d * 45 + 22.5f) * (PI_F / 180), lx, ly;
    polar(PX(P, x), PY(P, y), PL(P, R) * 0.78f, a, &lx, &ly);
    int on = 0;
    for (int i = 0; i < I->nthreat; i++) {
      float rel = I->threat[i] - (d * 45 + 22.5f);
      rel = fmodf(rel + 540, 360) - 180;
      if (fabsf(rel) <= 22.5f) on = I->mthreat[i] ? 2 : on ? on : 1;
    }
    if (lights) fb_circle(P->fb, lx, ly, fmaxf(1, k * R * 0.12f), on == 2 ? C(0xff3a2a) : on ? C(0xffb020) : C(0x3a1a18));
    else if (on) {
      fb_rect(P->fb, (int)(lx - k), (int)(ly - k), (int)fmaxf(2, 2 * k), (int)fmaxf(2, 2 * k),
              on == 2 && I->blink ? C(0xff3a2a) : C(0xff9a3a));
    }
  }
}

/* A display bezel with a page title strip; returns nothing, the screen is
 * the rect inside. */
static void mfd(const Pan *P, float x, float y, float w, float h, uint32_t frame, uint32_t screen, uint32_t strip,
                const char *title, uint32_t ink) {
  prect(P, x, y, w, h, frame);
  prect(P, x + 1.5f, y + 1.5f, w - 3, h - 3, screen);
  if (title) {
    prect(P, x + 1.5f, y + 1.5f, w - 3, 4.5f, strip);
    stext(P, title, x + 3, y + 3.8f, ink, ALIGN_LEFT, w - 5);
  }
}

/* ---- the instruments' readings -------------------------------------------- */
static void read_ins(Ins *I, Game *g, Plane *p, bool metric) {
  memset(I, 0, sizeof *I);
  I->g = g;
  I->p = p;
  I->metric = metric;
  I->blink = ((int)(g->time * 4)) % 2 == 0;
  I->spd = metric ? p->speed * KMH_PER_MPS : p->speed * KT_PER_MPS;
  I->alt = fmaxf(0, metric ? p->pos.y : p->pos.y * FT_PER_M);
  I->vs = metric ? p->vel.y : p->vel.y * FPM_PER_MPS;
  I->mach = p->speed / SOUND_MPS;
  I->hdg = fmodf(atan2f(p->f.x, p->f.z) * (180 / PI_F) + 360, 360);
  I->gl = p->gmeter;
  I->rpm = 62 + p->throttle * 38;
  I->fuel = p->fuel;
  I->hull = fmaxf(0, p->health);
  I->lock = p->locked;
  I->warn = p->warn;
  I->stall = p->stall;
  I->lowfuel = p->fuel < 20;
  I->lowhull = p->health < 35;
  I->ab = p->throttle > 0.9f;
  float hh = atan2f(p->f.x, p->f.z), ch = cosf(hh), sn = sinf(hh);
  for (int i = 0; i < g->nplanes + g->nmissiles && I->nthreat < 16; i++) {
    vec3 at;
    bool msl = i >= g->nplanes;
    if (!msl) {
      const Plane *e = &g->planes[i];
      if (e == p || !e->alive || e->team == p->team) continue;
      at = e->pos;
    } else {
      const Missile *m = &g->missiles[i - g->nplanes];
      if (!ref_eq(m->target, ref_plane(p->idx))) continue;
      at = m->pos;
    }
    float dx = at.x - p->pos.x, dz = at.z - p->pos.z;
    if (dx * dx + dz * dz > 12000.0f * 12000.0f) continue;
    float rx = dx * ch - dz * sn, ry = dx * sn + dz * ch;
    I->threat[I->nthreat] = fmodf(atan2f(rx, ry) * (180 / PI_F) + 360, 360);
    I->mthreat[I->nthreat++] = msl;
  }
}

/* a number on a gauge, rounded: a macro, so each format string is a literal the compiler checks */
#define fmt(b, n, f, v) snprintf(b, n, f, (int)roundf((float)(v)))

/* ---- 0: the original panel (from Ace4Combat) -------------------------------- */
static void panel_classic(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char buf[48];
  fb_rect(P->fb, P->x, P->y, P->w, P->h, C(0x16122e));
  fb_rect(P->fb, P->x, P->y, P->w, (int)fmaxf(1, roundf(P->h / 40.0f)), C(TEAM[p->team].ui));
  Scope S = { false, 10000, C(0x071407), C(0x1f4a22), C(HUDC), C(TEAM[p->team ^ 1].ui), C(HUDC), C(0xff6a4a), C(0xffd25a), false, true };
  scope(P, I, 5, 7, 52, 52, &S);
  fb_ring(P->fb, PX(P, 31), PY(P, 33), PL(P, 26), C(0x3b335f), 1);
  ball(P, p, 80, 33, 20, C(0x3d5cc0), C(0x7a5a34), C(AMBER), false);
  const float L = 108;
  ptext(P, "ALT", L, 9, C(DIM), ALIGN_LEFT);
  snprintf(buf, sizeof buf, "%05d", (int)roundf(I->alt)); /* the same reading as every other panel */
  ptext(P, buf, 200, 9, C(INK), ALIGN_RIGHT);
  ptext(P, "KTS", L, 20, C(DIM), ALIGN_LEFT);
  snprintf(buf, sizeof buf, "%04d", (int)roundf(p->speed * KT_PER_MPS));
  ptext(P, buf, 200, 20, C(p->stall ? WARN : INK), ALIGN_RIGHT);
  const char *bl[3] = { "FUEL", "HULL", "THR" };
  float bv[3] = { p->fuel / 100, I->hull / 100, p->throttle };
  uint32_t bc[3] = { C(p->fuel < 20 ? WARN : AMBER), C(p->health < 35 ? WARN : HUDC), C(INK) };
  for (int i = 0; i < 3; i++) {
    ptext(P, bl[i], L, 31 + i * 11, C(DIM), ALIGN_LEFT);
    hbar(P, L + 32, 28 + i * 11, 60, 6, bv[i], bc[i], C(0x2a2350));
  }
  const float R = 208;
  uint32_t col = C(TEAM[p->team].ui);
  ptext(P, p->name, R, 9, col, ALIGN_LEFT);
  lives(P, p, 312, 9, 4, col, C(0x3b335f));
  snprintf(buf, sizeof buf, "KILLS %d  TGT %d/6", p->kills, p->tkills);
  ptext(P, buf, R, 20, C(INK), ALIGN_LEFT);
  ptext(P, "MSL", R, 31, C(DIM), ALIGN_LEFT);
  for (int i = 0; i < MAX_MSL; i++) prect(P, R + 26 + i * 6, 27, 3, 9, i < p->missiles ? C(INK) : C(0x3b335f));
  if (hud_pips_hook) hud_pips_hook(PX(P, R + 26), PY(P, 27), PL(P, 3), PL(P, 9), PL(P, 6), p->missiles, MAX_MSL, C(INK), C(0x3b335f), 0);
  if (plane_has_gun(p)) snprintf(buf, sizeof buf, "FLR %02d  GUN %03d", p->flares, (int)p->ammo);
  else snprintf(buf, sizeof buf, "FLR %02d", p->flares); /* missiles only: no gun, no rounds */
  ptext(P, buf, R, 42, C(INK), ALIGN_LEFT);
  const char *ll[3] = { "LOCK", "WARN", "FUEL" };
  bool lo[3] = { I->lock, I->warn && I->blink, I->lowfuel };
  uint32_t lc[3] = { C(WARN), C(WARN), C(AMBER) };
  for (int i = 0; i < 3; i++) {
    prect(P, R + i * 37, 50, 34, 11, lo[i] ? lc[i] : C(0x251f47));
    ptext(P, ll[i], R + i * 37 + 17, 56, lo[i] ? C(0x16122e) : C(0x4d4580), ALIGN_CENTER);
  }
}

/* Stores, gun, flares and score in a few short lines. */
/* Two "label number" pairs, "a  b" or "a b" when it fits w at the panel's small text; NULL: each on its own line
 * (a low-res panel) */
static const char *pair_fit(const Pan *P, float w, const char *a, const char *b, char *t, size_t n) {
  snprintf(t, n, "%s  %s", a, b);
  if (text_width(t, P->ss) <= PL(P, w)) return t;
  snprintf(t, n, "%s %s", a, b);
  return text_width(t, P->ss) <= PL(P, w) ? t : NULL;
}

/* The tanker call's lamp (key 4): lit green ready, amber inbound or on station, red lost; dark used, or in
 * sudden death. label: the panel's own short word. */
static void tanker_lamp(const Pan *P, const Ins *I, const char *label, float x, float y, float w, float h) {
  const char *st = sim_tanker_status(I->g, I->p);
  bool lost = strstr(st, "LOST") != NULL, ready = strstr(st, "READY") != NULL, up = strstr(st, "INBOUND") || strstr(st, "STATION");
  lamp(P, label, x, y, w, h, lost || ready || up, lost ? 0xff3030 : up ? 0xffb020 : 0x33ff33);
}

/* Stores, gun, flares and score in a few short lines; returns the height the block took (grid units from y to
 * the line after it), more on a low-res panel where a pair of numbers goes on two lines. The tanker lamp ends the
 * missiles' row. */
static float stores_block(const Pan *P, const Ins *I, float x, float y, float w, uint32_t ink, uint32_t dim,
                          const char *msl, const char *gun, const char *flr, const char *kills, const char *tgt, const char *tkr, bool split) {
  Plane *p = I->p;
  char a[2][48], b[2][48], t[2][200];
  /* the missiles' label as wide as its word needs (a long one on a low-res panel), the rounds after it */
  float lw = fmaxf(w * 0.4f, text_width(msl, P->ss) / P->k + 0.5f);
  /* the missiles' label halfway between the ink and the unlit marks' colour: drawn in the unlit colour itself it
   * read at 1.6-2.8:1 on the panels (the live test player's 3:1 bar; the views' panel text contrast) */
  uint32_t legend = 0xFF000000u;
  for (int k = 0; k < 24; k += 8) legend |= (((ink >> k & 255) + (dim >> k & 255)) / 2) << k;
  stext(P, msl, x, y + 2.5f, legend, ALIGN_LEFT, lw);
  float rx = x + fmaxf(w * 0.42f, lw + 1), tw = fit_w(P, tkr, 10);
  stores_marks(P, I, rx, y, 6, x + w - rx - tw - 1, ink, dim);
  tanker_lamp(P, I, tkr, x + w - tw, y, tw, 6);
  snprintf(a[0], sizeof a[0], "%s %03d", gun, (int)p->ammo);
  snprintf(b[0], sizeof b[0], "%s %02d", flr, p->flares);
  snprintf(a[1], sizeof a[1], "%s %d", kills, p->kills);
  snprintf(b[1], sizeof b[1], "%s %d/6", tgt, p->tkills);
  const char *one[2] = { pair_fit(P, w, a[0], b[0], t[0], sizeof t[0]), pair_fit(P, w, a[1], b[1], t[1], sizeof t[1]) };
  if (!plane_has_gun(p)) one[0] = b[0]; /* missiles only: no gun, no rounds, the flares alone */
  for (int i = 0; i < 2 && !split; i++) /* a panel with no room below for more lines: one line each, as wide as it is */
    if (!one[i]) one[i] = (snprintf(t[i], sizeof t[i], "%s  %s", a[i], b[i]), t[i]);
  /* split lines close up to a line of letters and a pixel */
  /* (never closer than the letters themselves, and the first line clear of the marks and the tanker lamp above
   * it: on a low-res panel the lamp ran into "AKAN") */
  float glyph = (7.0f * P->ss + 1) / P->k, pitch = fmaxf(one[0] && one[1] ? 6.5f : (8.0f * P->ss) / P->k, glyph);
  float ly = y + fmaxf(10, 6 + glyph / 2 + 1 / P->k);
  for (int i = 0; i < 2; i++) {
    if (one[i]) stext(P, one[i], x, ly, ink, ALIGN_LEFT, w), ly += pitch;
    else {
      stext(P, a[i], x, ly, ink, ALIGN_LEFT, w);
      stext(P, b[i], x, ly + pitch, ink, ALIGN_LEFT, w);
      ly += 2 * pitch;
    }
  }
  return ly - y;
}

/* ---- 1: AF-40 Nightshade, one panoramic touch display --------------------- */
static void panel_af40(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[40];
  face(P, 0x2b2e31, 0x141617, 0x0e0f10);
  lamp(P, "WARN", 4, 2, 26, 6, I->warn && I->blink, 0xff3030);
  lamp(P, "CAUTION", 32, 2, 32, 6, I->lowfuel || I->stall || I->lowhull, 0xffb020); /* room for its word at any size */
  gear_lamp(P, I, "GEAR", 66, 2, 28, 6);
  prect(P, 96, 1.5f, 128, 7, C(0x000000));
  snprintf(b, sizeof b, "%03d STPT 3  M %.2f  G %.1f", (int)I->hdg, I->mach, I->gl);
  stext(P, b, 160, 5, C(0x7cff8a), ALIGN_CENTER, 126);
  lamp(P, "LOCK", 262, 2, 26, 6, I->lock, 0xff3030);
  lamp(P, "A/B", 290, 2, 26, 6, I->ab, 0xffb020);
  prect(P, 2, 10, 316, 53, C(0x3b3e41));
  prect(P, 3, 11, 314, 51, C(0x0a0b0c));
  const char *tt[4] = { "PFD", "FCR", "SMS", "FUEL" };
  const float px[4] = { 4, 84, 164, 244 }, pw = 72;
  for (int i = 0; i < 4; i++) {
    prect(P, px[i], 12, pw, 4.5f, C(0x1a2a38));
    stext(P, tt[i], px[i] + 2, 14.3f, C(0x66ffff), ALIGN_LEFT, pw);
  }
  pfd(P, I, px[0], 17, pw, 44, C(0x2a64a8), C(0x7b5a35), C(0xffffff));
  Scope S = { true, 12000, C(0x06090c), C(0x1d4d55), C(0x66ffff), C(0xff4040), C(0x41c6ff), C(0xff8040), C(0xffd25a), true, false };
  scope(P, I, px[1], 17, pw, 44, &S);
  stext(P, "AESA RWS", px[1] + 2, 20, C(0x66ffff), ALIGN_LEFT, 36);
  /* the lines under the stores move down when they took more lines (a low-res panel) */
  float ny = fmaxf(42, 19 + stores_block(P, I, px[2] + 2, 19, pw - 4, C(0xffffff), C(0x5a6a78), "AAM", "GUN", "FLR", "KILLS", "TGT", "TK", true));
  stext(P, p->name, px[2] + 2, ny, C(TEAM[p->team].ui), ALIGN_LEFT, 40);
  lives(P, p, px[2] + pw - 4, ny, 2, C(TEAM[p->team].ui), C(0x2a3038));
  stext(P, "MASTER ARM", px[2] + pw / 2, ny > 42 ? ny + 6.5f : 52, p->missiles ? C(0x33ff33) : C(0x6e866e), ALIGN_CENTER, pw); /* off still readable (3:1) */
  const char *bl[3] = { "FUEL", "HULL", "THR" };
  float bv[3] = { I->fuel / 100, I->hull / 100, p->throttle };
  uint32_t bc[3] = { C(I->lowfuel ? 0xffb020 : 0x33ff66), C(I->lowhull ? 0xff4040 : 0x33ff66), C(0xffffff) };
  for (int i = 0; i < 3; i++) {
    stext(P, bl[i], px[3] + 2, 21 + i * 9, C(0x9ab0c0), ALIGN_LEFT, 18);
    hbar(P, px[3] + 20, 19 + i * 9, pw - 24, 4, bv[i], bc[i], C(0x1a2228));
  }
  fmt(b, sizeof b, "%05d LB", I->fuel / 100 * 18080);
  stext(P, b, px[3] + 2, 50, C(0xffffff), ALIGN_LEFT, pw - 4);
  fmt(b, sizeof b, "NOZ %d%%", I->rpm);
  stext(P, b, px[3] + 2, 57, C(0x9ab0c0), ALIGN_LEFT, pw - 4);
}

/* ---- 2: LF-27 Bobcat, two green MFDs and round gauges ---------------------- */
static void panel_lf27(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[40], r1[16], r2[16];
  face(P, 0x4b5054, 0x2c2f31, 0x1a1b1c);
  const uint32_t G5 = C(0x55ff55), GD = C(0x1f5a1f);
  mfd(P, 2, 3, 62, 60, C(0x2a2d30), C(0x000000), C(0x0a1a0a), "FCR  RWS", G5);
  Scope S = { true, 10000, C(0x020a02), C(0x174017), G5, G5, C(0x2a9a2a), G5, C(0xffffff), true, false };
  scope(P, I, 4, 10, 58, 51, &S);
  mfd(P, 256, 3, 62, 60, C(0x2a2d30), C(0x000000), C(0x0a1a0a), "SMS  AAM", G5);
  float ny = fmaxf(37, 12 + stores_block(P, I, 259, 12, 56, G5, GD, "AIM", "GUN", "FLR", "K", "TGT", "TK", true)); /* lower if it grew */
  stext(P, p->name, 259, ny, G5, ALIGN_LEFT, 30);
  lives(P, p, 312, ny, 2, G5, GD);
  stext(P, I->lock ? "LOCK" : "SRCH", 259, ny + 8, I->lock ? C(0xffffff) : G5, ALIGN_LEFT, 20);
  stext(P, "SWAP  DCLT", 287, 58, C(0x3aac3a), ALIGN_CENTER, 56); /* the soft keys' legend: GD read at 2.5:1 */
  /* up-front controls with the data entry display */
  prect(P, 92, 2, 136, 21, C(0x2a2d30));
  prect(P, 106, 3.5f, 108, 18, C(0x000000));
  snprintf(b, sizeof b, "STPT 4  M%.2f  G%.1f", I->mach, I->gl);
  stext(P, b, 108, 7, G5, ALIGN_LEFT, 104);
  fmt(b, sizeof b, "FUEL %5d  BINGO 2500", I->fuel / 100 * 7050);
  stext(P, b, 108, 12.5f, G5, ALIGN_LEFT, 104);
  snprintf(b, sizeof b, "C %03d   HULL %3d%%", (int)I->hdg, (int)I->hull);
  stext(P, b, 108, 18, I->lowhull ? C(0xffffff) : G5, ALIGN_LEFT, 104);
  for (int i = 0; i < 6; i++) {
    prect(P, 94 + (i % 2) * 5, 4 + (i / 2) * 6, 4, 5, C(0x3d4044));
    prect(P, 217 + (i % 2) * 5, 4 + (i / 2) * 6, 4, 5, C(0x3d4044));
  }
  /* the gap between the radar MFD and the up-front controls, all of it: MASTER CAUT on two lines when small */
  lamp(P, "MASTER CAUT", 64.25f, 3, 27.5f, 12, I->lowfuel || I->stall || I->lowhull, 0xffb020);
  lamp(P, "LOCK", 64.25f, 16, 27.5f, 6.5f, I->lock, 0xff3030);
  lamp(P, "WARN", 230, 3, 24, 6.5f, I->warn && I->blink, 0xff3030); /* three high, the gear's under them */
  lamp(P, "A/B", 230, 10.5f, 24, 6.5f, I->ab, 0x33ff33);
  gear_lamp(P, I, "GEAR", 230, 18, 24, 6.5f);
  /* round gauges */
  const uint32_t ink = C(0xeeeeee);
  fmt(r1, sizeof r1, "%d", I->spd);
  Dial asi = { 80, 850, 0, 340, 8, 0, ink, 0, 0, 0, "KTS", NULL, r1, 0, 0, I->spd, 0, 0 };
  dial(P, 79, 44, 12, &asi);
  ball(P, p, 107, 44, 14, C(0x3d6fc0), C(0x7a5a34), C(0xffd23a), false);
  fmt(r2, sizeof r2, "%05d", I->alt);
  Dial alt = { 0, 1000, 0, 360, 10, 0, ink, 0, 0, 0, "ALT", NULL, r2, 0, 0, fmodf(I->alt, 1000), 0, 0 };
  dial(P, 135, 44, 12, &alt);
  Dial gm = { -4, 10, -135, 135, 7, 0, ink, 0, 0, 0, "G", NULL, NULL, 9, 10, I->gl, 0, 0 };
  dial(P, 161, 44, 11, &gm);
  Dial rpm = { 0, 110, -135, 135, 11, 0, ink, 0, 0, 0, "RPM", "%", NULL, 0, 0, I->rpm, 0, 0 };
  dial(P, 186, 44, 11, &rpm);
  Dial fq = { 0, 100, -135, 135, 4, 0, ink, 0, 0, 0, "FUEL", NULL, NULL, 0, 20, I->fuel, 0, 0 };
  dial(P, 211, 44, 11, &fq);
  stext(P, "HULL", 241, 31, C(0xdddddd), ALIGN_CENTER, 18);
  prect(P, 237, 34, 8, 26, C(0x1c1c1c));
  float hf = 25 * I->hull / 100;
  if (hf > 0) prect(P, 238, 59 - hf, 6, hf, I->lowhull ? C(0xff3030) : C(0x33dd33));
}

/* ---- 3: ECF-90 Sirocco, three colour head-down displays ---------------------- */
static void panel_ecf90(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[40];
  face(P, 0x3d4246, 0x1f2224, 0x141516);
  const uint32_t ink = C(0xe8f2ff), cy = C(0x9fd8ff);
  /* get-u-home standby */
  /* wide enough for "ALT 49869" on a small panel */
  prect(P, 2, 3, 45, 59, C(0x1a1c1e));
  ball(P, p, 24.5f, 16, 10, C(0x3d6fc0), C(0x6a4a2a), C(0xffd23a), false);
  const char *gl[3] = { "IAS", "ALT", "HDG" };
  float gv[3] = { I->spd, I->alt, I->hdg };
  for (int i = 0; i < 3; i++) {
    prect(P, 3.5f, 29 + i * 8, 42, 6.5f, C(0x000000));
    snprintf(b, sizeof b, i == 2 ? "%s %03d" : "%s %d", gl[i], (int)roundf(gv[i]));
    stext(P, b, 24.5f, 32.3f + i * 8, C(0xffffff), ALIGN_CENTER, 41);
  }
  if (text_width("GET-U-HOME", P->ss) <= PL(P, 42)) stext(P, "GET-U-HOME", 24.5f, 58, C(0xaaaaaa), ALIGN_CENTER, 42);
  else { /* a small panel: on two lines */
    stext(P, "GET-U-", 24.5f, 54.5f, C(0xaaaaaa), ALIGN_CENTER, 42);
    stext(P, "HOME", 24.5f, 59.5f, C(0xaaaaaa), ALIGN_CENTER, 42);
  }
  /* the three displays (the two left ones a little narrower, for the standby and warning panels' words) */
  mfd(P, 49, 3, 68, 59, C(0x2b3034), C(0x04070b), C(0x0f1a24), "ATT  ENG", cy);
  pfd(P, I, 51, 9, 64, 51, C(0x2a64a8), C(0x7b5a35), C(0xffffff));
  mfd(P, 119, 3, 74, 59, C(0x2b3034), C(0x04070b), C(0x0f1a24), "PA  RDR TWS", cy);
  Scope S = { true, 12000, C(0x04070b), C(0x1a3d4d), C(0x7fe3ff), C(0xff5050), C(0x3fd1ff), C(0xff9050), C(0xffd25a), true, false };
  scope(P, I, 121, 9, 70, 52, &S);
  mfd(P, 195, 3, 78, 59, C(0x2b3034), C(0x04070b), C(0x0f1a24), "WPN  FUEL", cy);
  stores_block(P, I, 198, 11, 72, ink, C(0x4a5a6a), "MSL", "GUN", "FLR", "KILLS", "TGT", "TK", true);
  stext(P, p->name, 198, 33, C(TEAM[p->team].ui), ALIGN_LEFT, 40);
  lives(P, p, 268, 33, 2, C(TEAM[p->team].ui), C(0x2a3038));
  const char *bl[3] = { "FUEL", "HULL", "THR" };
  float bv[3] = { I->fuel / 100, I->hull / 100, p->throttle };
  uint32_t bc[3] = { C(I->lowfuel ? 0xffb020 : 0x38e1ff), C(I->lowhull ? 0xff5050 : 0x38e1ff), ink };
  for (int i = 0; i < 3; i++) {
    stext(P, bl[i], 198, 41 + i * 7, cy, ALIGN_LEFT, 18);
    hbar(P, 216, 39.5f + i * 7, 54, 3.5f, bv[i], bc[i], C(0x18222c));
  }
  /* dedicated warning panel: the short legends in pairs, the long ones across, so each word fits at any size */
  prect(P, 275, 3, 44, 59, C(0x1a1c1e));
  static const struct { const char *l; int row, col; } DL[10] = {
    { "FUEL", 0, 0 }, { "LOCK", 0, 1 }, { "LOW SPD", 1, -1 }, { "G LIM", 2, -1 }, { "REHEAT", 3, -1 },
    { "MSL", 4, 0 }, { "HULL", 4, 1 }, { "GUN", 5, 0 }, { "GEAR", 5, 1 }, { "BINGO", 6, -1 },
  };
  bool on[10] = { I->lowfuel, I->lock, I->stall, I->gl > 8, I->ab, I->warn && I->blink, I->lowhull, plane_has_gun(p) && p->ammo < 50,
                  false, I->fuel < 10 };
  uint32_t dc[10] = { 0xffb020, 0xff3030, 0xff3030, 0xff3030, 0x33ff33, 0xff3030, 0xff3030, 0xffb020, 0x33ff33, 0xffb020 };
  for (int i = 0; i < 10; i++) {
    float lx = DL[i].col < 0 ? 276 : 276 + DL[i].col * 21.25f, lw = DL[i].col < 0 ? 42 : 20.75f;
    if (i == 8) gear_lamp(P, I, DL[i].l, lx, 3.5f + DL[i].row * 7.3f, lw, 6.9f);
    else lamp(P, DL[i].l, lx, 3.5f + DL[i].row * 7.3f, lw, 6.9f, on[i], dc[i]); /* a line of letters and a pixel either side */
  }
  lamp(P, "ATTENTION", 276, 3.5f + 7 * 7.3f, 42, 6.9f, I->warn || I->lowfuel || I->stall || I->lowhull, 0xff3030);
}

/* ---- 4: JA 46 Gladan, 1970s Swedish analog, metric ------------------------ */
static void panel_ja46(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[40], r1[16], r2[16];
  face(P, 0x202224, 0x101112, 0x0b0b0c);
  const char *wl[8] = { "HUVUDVARN", "BRÄNSLE", "ÖVERSTEGR", "LÅSNING", "ROBOTVARN", "EBK", "SKADA", "STÄLL" };
  bool wo[7] = { I->warn || I->lowfuel || I->stall || I->lowhull, I->lowfuel, I->stall, I->lock, I->warn && I->blink, I->ab,
                 I->lowhull };
  uint32_t wc[7] = { 0xff3030, 0xffb020, 0xff3030, 0xffb020, 0xff3030, 0x33ff33, 0xff3030 };
  float lx[8], lw[8];
  lamp_row(wl, 8, 3, 315, 1.5f, lx, lw);
  for (int i = 0; i < 7; i++) lamp(P, wl[i], lx[i], 2, lw[i], 7, wo[i], wc[i]);
  gear_lamp(P, I, wl[7], lx[7], 2, lw[7], 7); /* the landing gear */
  const uint32_t ink = C(0xf2f0e4), fc = C(0x0b0b0b);
  /* a small panel (the retro picture at 320x180: 52 pixels high, a dial face 18 across, the right-hand column ten
   * letters wide) is laid out compact: fewer lines and two-letter dial titles. At full size the words did not fit:
   * the titles went on two lines under their units, the numbers ran out of their windows, and the column needed
   * eight lines of letters where five fit. From 1.2 up (640x360 and over) the full words, as before. */
  bool small = P->k < 1.2f;
  snprintf(r1, sizeof r1, "M%.2f", I->mach);
  fmt(r2, sizeof r2, "%d", I->alt);
  Dial fart = { 0, 1400, -150, 150, 7, fc, ink, 0, 0, 0, "FART", NULL, small ? NULL : r1, 0, 0, I->spd, 0, 0 };
  dial(P, 16, 37, 13, &fart);
  ball(P, p, 46, 37, 14, C(0x5a6f86), C(0x2a2a2a), C(0xff9a2a), false);
  Dial hojd = { 0, 1000, 0, 360, 10, fc, ink, 0, 0, 0, "HÖJD", NULL, small ? NULL : r2, 0, 0, fmodf(I->alt, 1000), 0, 0 };
  dial(P, 76, 37, 13, &hojd);
  if (small) { /* the numbers under their dials, where the units were, out to the panel's edges (a five-figure
                 * height is wider than its dial); the ball needs no word */
    stext(P, r1, 3, 58, C(0xcccccc), ALIGN_LEFT, 42);
    stext(P, r2, 90, 58, C(0xcccccc), ALIGN_RIGHT, 42);
  } else {
    stext(P, "ATTITYD", 46, 58, C(0xcccccc), ALIGN_CENTER, 31); /* the room between KM/H and M */
    stext(P, "KM/H", 16, 58, C(0xcccccc), ALIGN_CENTER, 28);
    stext(P, "M", 76, 58, C(0xcccccc), ALIGN_CENTER, 28);
  }
  /* the centre radar indicator (CI) */
  prect(P, 93, 11, 86, 52, C(0x151617));
  Scope S = { true, 10000, C(0x031208), C(0x15502a), C(0x78ff96), C(0x8fff8f), C(0x3a9a4a), C(0x8fff8f), C(0xffffff), true, false };
  scope(P, I, 95, 13, 82, 48, &S);
  stext(P, "CI 10 KM", 97, 15.5f, C(0x88ff88), ALIGN_LEFT, small ? 80 : 40); /* (small: its letters are wider than 40) */
  Dial varv = { 0, 110, -135, 135, 11, fc, ink, 0, 0, 0, small ? "VT" : "VARVTAL", small ? NULL : "%", NULL, 0, 0, I->rpm, 0, 0 };
  dial(P, 195, 26, 12, &varv);
  Dial br = { 0, 100, -135, 135, 4, fc, ink, 0, 0, 0, small ? "BR" : "BRÄNSLE", small ? NULL : "%", NULL, 0, 20, I->fuel, 0, 0 };
  dial(P, 222, 26, 12, &br);
  Dial gm = { -3, 9, -135, 135, 6, fc, ink, 0, 0, 0, "G", NULL, NULL, 7.5f, 9, I->gl, 0, 0 };
  dial(P, 195, 51, 11, &gm);
  Dial st = { -100, 100, 105, 435, 4, fc, ink, 0, 0, 0, small ? "ST" : "STIG", small ? NULL : "M/S", NULL, 0, 0, clampf(I->vs, -100, 100), 0, 0 };
  dial(P, 222, 51, 11, &st);
  /* weapons and score */
  prect(P, 238, 11, 80, 52, C(0x151617));
  if (small) {
    /* the stores and the score in the column's left part, one to a line; the planes left, then the hull
     * and the throttle as two upright bars, at its right (white and orange, as the full panel's) */
    stores_block(P, I, 241, 14, 66, C(0xf2f0e4), C(0x505050), "RB", "AKAN", "FACKL", "NEDSKJ", "MÅL", "TK", true);
    lives(P, p, 315.5f, 24, 1.2f, C(TEAM[p->team].ui), C(0x333333));
    float hull = clampf(I->hull / 100, 0, 1), thr = clampf(p->throttle, 0, 1);
    prect(P, 309, 28, 3.5f, 34, C(0x2a2a2a));
    prect(P, 309, 62 - 34 * hull, 3.5f, 34 * hull, I->lowhull ? C(0xff3030) : C(0xf2f0e4));
    prect(P, 314, 28, 3.5f, 34, C(0x2a2a2a));
    prect(P, 314, 62 - 34 * thr, 3.5f, 34 * thr, C(0xff9a2a));
    (void)b;
    return;
  }
  /* the lines under the stores move down when they took more lines, closer together (a low-res panel) */
  float ny = fmaxf(39, 14 + stores_block(P, I, 241, 14, 74, C(0xf2f0e4), C(0x505050), "RB", "AKAN", "FACKL", "NEDSKJ", "MÅL", "TK", true));
  float lh = ny > 39 ? 6.3f : 8;
  stext(P, p->name, 241, ny, C(TEAM[p->team].ui), ALIGN_LEFT, 40);
  lives(P, p, 313, ny, 2, C(TEAM[p->team].ui), C(0x333333));
  stext(P, "STRUKTUR", 241, ny + lh, C(0xcccccc), ALIGN_LEFT, 36); /* up to its bar */
  hbar(P, 277, ny + lh - 1.5f, 38, 3.5f, I->hull / 100, I->lowhull ? C(0xff3030) : C(0xf2f0e4), C(0x2a2a2a));
  stext(P, "PÅDRAG", 241, ny + 2 * lh, C(0xcccccc), ALIGN_LEFT, 34);
  hbar(P, 277, ny + 2 * lh - 1.5f, 38, 3.5f, p->throttle, C(0xff9a2a), C(0x2a2a2a));
  (void)b;
}

/* ---- 5: SY-26 Tianlong, one very wide colour LCD -------------------------- */
static void panel_sy26(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[48];
  face(P, 0x2a2c2f, 0x111213, 0x0b0c0d);
  const uint32_t ink = C(0xe6f7ff), cy = C(0x66ffff), gold = C(0xe8b84a);
  lamp(P, "WARN", 4, 1.5f, 24, 6, I->warn && I->blink, 0xff3030);
  lamp(P, "CAUT", 30, 1.5f, 24, 6, I->lowfuel || I->stall || I->lowhull, 0xffb020);
  gear_lamp(P, I, "GEAR", 56, 1.5f, 24, 6);
  prect(P, 96, 1, 128, 7, C(0x000000));
  fmt(b, sizeof b, "COM1 243.0  WPT 05  %d KM", fmodf(fabsf(p->pos.x) + fabsf(p->pos.z), 20000) / 1000);
  stext(P, b, 160, 4.5f, C(0x7fe3ff), ALIGN_CENTER, 126);
  lamp(P, "A/B", 266, 1.5f, 24, 6, I->ab, 0x33ff33);
  lamp(P, "LOCK", 292, 1.5f, 24, 6, I->lock, 0xff3030);
  prect(P, 2, 9, 316, 54, C(0x3b3e41));
  prect(P, 3, 10, 314, 52, C(0x080909));
  const char *tt[3] = { "PFD", "TAC  RDR", "WPN  ENG" };
  const float px[3] = { 4, 110, 214 }, pw[3] = { 104, 102, 102 };
  for (int i = 0; i < 3; i++) {
    prect(P, px[i], 11, pw[i], 4.5f, C(0x10202b));
    prect(P, px[i], 11, 1.5f, 4.5f, C(0xc8302a));
    stext(P, tt[i], px[i] + 3, 13.3f, cy, ALIGN_LEFT, pw[i]);
  }
  pfd(P, I, px[0], 16, pw[0], 45, C(0x245a96), C(0x6a4a2a), ink);
  Scope S = { false, 16000, C(0x050a0d), C(0x1a3a46), cy, C(0xff5a3c), C(0x3cd2ff), C(0xff8a5a), gold, true, true };
  scope(P, I, px[1], 16, pw[1], 45, &S);
  stext(P, "TWS", px[1] + 2, 19, cy, ALIGN_LEFT, 20);
  stext(P, "16 KM", px[1] + pw[1] - 2, 19, cy, ALIGN_RIGHT, 24);
  stores_block(P, I, px[2] + 3, 18, pw[2] - 6, ink, C(0x3a4a52), "LRM", "GUN", "FLR", "KILLS", "TGT", "TK", true);
  stext(P, p->name, px[2] + 3, 40, gold, ALIGN_LEFT, 40);
  lives(P, p, px[2] + pw[2] - 4, 40, 2, gold, C(0x2a3038));
  const char *bl[3] = { "FUEL", "HULL", "THRUST" };
  float bv[3] = { I->fuel / 100, I->hull / 100, p->throttle };
  uint32_t bc[3] = { C(I->lowfuel ? 0xffb020 : 0x3cd2ff), C(I->lowhull ? 0xff5a3c : 0x3cd2ff), gold };
  for (int i = 0; i < 3; i++) {
    stext(P, bl[i], px[2] + 3, 46.5f + i * 6.3f, cy, ALIGN_LEFT, 27); /* a line apart, so the labels never touch */
    hbar(P, px[2] + 30, 45 + i * 6.3f, pw[2] - 36, 3, bv[i], bc[i], C(0x18222c));
  }
  fmt(b, sizeof b, "%d KG", I->fuel / 100 * 11000);
  stext(P, b, px[2] + pw[2] - 3, 13.3f, ink, ALIGN_RIGHT, 40); /* in the header: between the stores' lines it hid them */
}

/* ---- 6: Vk-33 Strizh, Soviet analog on a turquoise panel ------------------ */
static void panel_vk33(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[48], r1[16];
  face(P, 0x4f9196, 0x2e6266, 0x1c2a2c);
  const uint32_t ink = C(0xf4f4ee), fc = C(0x0d0d0d), rim = C(0x2f3b3c), lbl = C(0xe9f5f5), yel = C(0xffcc33);
  /* radar and the SPO threat lights */
  prect(P, 2, 3, 86, 59, C(0x121a1b));
  Scope S = { false, 10000, C(0x041004), C(0x1a4a1a), C(0x8fff8f), C(0x8fff8f), C(0x3a9a3a), C(0x8fff8f), yel, true, false };
  scope(P, I, 4, 6, 44, 44, &S);
  stext(P, "РЛС", 26, 56, lbl, ALIGN_CENTER, 30);
  threat_ring(P, I, 68, 28, 17, C(0xcccccc), true);
  stext(P, "СПО", 68, 56, lbl, ALIGN_CENTER, 30);
  /* the gauges, labels under each as on the real panel */
  fmt(r1, sizeof r1, "%d", I->spd);
  Dial kus = { 0, 1600, -150, 150, 8, fc, ink, rim, 0, yel, "КМ/Ч", NULL, NULL, 0, 0, I->spd, I->spd * 1.08f + 30, 0.75f };
  dial(P, 101, 28, 12, &kus); /* the gauges four units closer, for the warning box beside them */
  stext(P, "КУС", 101, 46, lbl, ALIGN_CENTER, 24);
  Dial vd = { 0, 1000, 0, 360, 10, fc, ink, rim, 0, C(0xdddddd), "ВД-20", NULL, NULL, 0, 0, fmodf(I->alt, 1000),
              I->alt / 20, 0.45f };
  dial(P, 126.5f, 28, 12, &vd);
  stext(P, "ВЫСОТА", 126.5f, 46, lbl, ALIGN_CENTER, 27);
  ball(P, p, 154.5f, 28, 15, C(0x4c84b9), C(0x5b3d24), C(0xffe14a), true);
  stext(P, "КПП", 154.5f, 50, lbl, ALIGN_CENTER, 24);
  Dial uap = { -10, 40, -150, 60, 5, fc, ink, rim, 0, yel, "α  n", NULL, NULL, 26, 40,
               clampf(I->stall ? 28 : 6 + I->gl * 1.5f, -10, 40), clampf(I->gl, -2, 10) * 5 - 10 + 0.01f, 0.86f };
  dial(P, 182, 28, 12, &uap);
  stext(P, "УАП", 182, 46, lbl, ALIGN_CENTER, 24);
  Dial ob = { 0, 110, -135, 135, 11, fc, ink, rim, 0, yel, "%", NULL, NULL, 0, 0, I->rpm, I->rpm - 0.6f, 0.8f };
  dial(P, 207.5f, 28, 12, &ob);
  stext(P, "ОБОРОТЫ", 207.5f, 46, lbl, ALIGN_CENTER, 31);
  fmt(r1, sizeof r1, "%d", I->fuel / 100 * 3500);
  Dial tp = { 0, 4000, -135, 135, 4, fc, ink, rim, 0, 0, "КГ", NULL, r1, 0, 800, I->fuel / 100 * 3500, 0, 0 };
  dial(P, 233, 28, 12, &tp);
  /* a line lower than ОБОРОТЫ beside it, so the two long words never meet */
  stext(P, "ТОПЛИВО", 232, 52, lbl, ALIGN_CENTER, 31);
  fmt(b, sizeof b, "ВАР %+d М/С", I->vs);
  stext(P, b, 116, 57.5f, lbl, ALIGN_CENTER, 56); /* to "ВАР +299 М/С" */
  gear_lamp(P, I, "ШАССИ", 158, 55, 27, 6.5f); /* the gear and the afterburner, lamps with their words */
  lamp(P, "ФОРСАЖ", 186.5f, 55, 30, 6.5f, I->ab, 0x33ff33);
  /* warning lights and the weapons panel */
  prect(P, 248, 3, 70, 59, C(0x121a1b));
  const char *wl[6] = { "ЗАХВАТ", "ОПАСНО", "ОСТАТОК", "КРИТ α", "ПЕРЕГРУЗ", "ПОВРЕЖД" };
  bool wo[6] = { I->lock, I->warn && I->blink, I->lowfuel, I->stall, I->gl > 8, I->lowhull };
  uint32_t wc[6] = { 0xffb020, 0xff3030, 0xffb020, 0xff3030, 0xff3030, 0xff3030 };
  /* in pairs, the long ПЕРЕГРУЗ and ПОВРЕЖД sharing a row in lamps of their own widths, so each word fits */
  static const float LX[6] = { 248.5f, 283.5f, 248.5f, 283.5f, 248.5f, 286 }, LW[6] = { 34.5f, 34.5f, 34.5f, 34.5f, 37, 32 };
  for (int i = 0; i < 6; i++) lamp(P, wl[i], LX[i], 4 + (i / 2) * 6.5f, LW[i], 6, wo[i], wc[i]);
  float ny = fmaxf(55, 25 + stores_block(P, I, 251, 25, 66, C(0x99ff99), C(0x2a4a2a), "РАКЕТЫ", "ПУШКА", "ЛТЦ", "СБИТО", "ЦЕЛИ", "ЗП", true));
  stext(P, p->name, 251, ny, C(TEAM[p->team].ui), ALIGN_LEFT, 36);
  lives(P, p, 313, ny, 2, C(TEAM[p->team].ui), C(0x2a3a3a));
  for (int i = 0; i < 4; i++) {
    float sx = i % 2 ? 316 : 4, sy = i / 2 ? 60 : 5;
    fb_circle(P->fb, PX(P, sx), PY(P, sy), fmaxf(1, P->k * 0.9f), C(0x555555));
  }
}

/* ---- 7: Ks-55 Grifon, modern Russian glass ---------------------------------- */
static void panel_ks55(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[48];
  face(P, 0x2f3337, 0x15171a, 0x0d0e10);
  const uint32_t ink = C(0xe8ecf0), cy = C(0x99ddff);
  const char *wl[7] = { "ОПАСНО", "ТОПЛИВО", "ЗАХВАТ", "ФОРСАЖ", "КРИТ α", "ПОВРЕЖД", "ШАССИ" };
  bool wo[6] = { I->warn && I->blink, I->lowfuel, I->lock, I->ab, I->stall, I->lowhull };
  uint32_t wc[6] = { 0xff3030, 0xffb020, 0xffb020, 0x33ff33, 0xff3030, 0xff3030 };
  float lx[7], lw[7];
  lamp_row(wl, 7, 58, 210, 1.5f, lx, lw);
  for (int i = 0; i < 6; i++) lamp(P, wl[i], lx[i], 1.5f, lw[i], 6, wo[i], wc[i]);
  gear_lamp(P, I, wl[6], lx[6], 1.5f, lw[6], 6); /* the landing gear, seventh */
  /* standby display: wide enough for "V 2736 КМ/Ч"; M and N on lines of their own */
  prect(P, 2, 3, 54, 59, C(0x0a0b0c));
  ball(P, p, 29, 13, 9, C(0x3a74b8), C(0x6a4a2a), C(0xffd23a), false);
  fmt(b, sizeof b, "V %d КМ/Ч", I->spd);
  stext(P, b, 4, 26, ink, ALIGN_LEFT, 50);
  fmt(b, sizeof b, "H %d М", I->alt);
  stext(P, b, 4, 32, ink, ALIGN_LEFT, 50);
  snprintf(b, sizeof b, "M %.2f", I->mach);
  stext(P, b, 4, 38, ink, ALIGN_LEFT, 50);
  snprintf(b, sizeof b, "N %.1f", I->gl);
  stext(P, b, 4, 44, ink, ALIGN_LEFT, 50);
  stext(P, "ТЯГА", 4, 51, C(0x8a9aa8), ALIGN_LEFT, 27); /* the labels' room before the bars, for their words */
  hbar(P, 32, 49.5f, 22, 3, p->throttle, C(0x7cff6a), C(0x1a2228));
  stext(P, "ПЛАНЕР", 4, 57, C(0x8a9aa8), ALIGN_LEFT, 27);
  hbar(P, 32, 55.5f, 22, 3, I->hull / 100, I->lowhull ? C(0xff4a3a) : C(0x7cff6a), C(0x1a2228));
  /* the two big MFIs */
  mfd(P, 58, 9, 104, 54, C(0x25282b), C(0x04080b), C(0x0f1a24), "ПНИ  ДВИГ", cy);
  pfd(P, I, 60, 15, 100, 46, C(0x2a64a8), C(0x7b5a35), C(0xffffff));
  mfd(P, 164, 9, 104, 54, C(0x25282b), C(0x04080b), C(0x0f1a24), "ТАКТ  РЛС  ВООР", cy);
  Scope S = { true, 12000, C(0x04080b), C(0x1a4a1a), C(0x77ff77), C(0xff4a3a), C(0x4ad2ff), C(0xff8a5a), C(0xffd25a), true, false };
  scope(P, I, 166, 15, 100, 46, &S);
  char k1[32], k2[32], kt[72];
  snprintf(k1, sizeof k1, "СБИТО %d", p->kills);
  snprintf(k2, sizeof k2, "ЦЕЛИ %d/6", p->tkills);
  snprintf(kt, sizeof kt, "%s  %s", k1, k2);
  if (text_width(kt, P->ss) <= PL(P, 60)) stext(P, kt, 264, 18, ink, ALIGN_RIGHT, 60);
  else { /* a small panel: the two on lines of their own */
    stext(P, k1, 264, 18, ink, ALIGN_RIGHT, 60);
    stext(P, k2, 264, 24, ink, ALIGN_RIGHT, 60);
  }
  stext(P, "ОБЗОР", 168, 18, C(0x77ff77), ALIGN_LEFT, 30);
  /* right side display: threats, fuel and stores */
  prect(P, 270, 3, 48, 59, C(0x0a0b0c));
  threat_ring(P, I, 294, 16, 11, C(0xff9a5a), false);
  fmt(b, sizeof b, "ТОПЛ %d", I->fuel / 100 * 10300);
  stext(P, b, 272, 32, I->lowfuel ? C(0xffaa33) : ink, ALIGN_LEFT, 45);
  stext(P, "Р", 272, 38, C(0x8a9aa8), ALIGN_LEFT, 6);
  stores_marks(P, I, 277, 35.5f, 5, 26, ink, C(0x2a3038));
  tanker_lamp(P, I, "ЗП", 304, 35, fit_w(P, "ЗП", 12), 5); /* the tanker call, after the missiles' row */
  if (plane_has_gun(p)) {
    snprintf(b, sizeof b, "ПУШКА %03d", (int)p->ammo);
    stext(P, b, 272, 45, ink, ALIGN_LEFT, 45);
  }
  snprintf(b, sizeof b, "ЛТЦ %02d", p->flares);
  stext(P, b, 272, 51, ink, ALIGN_LEFT, 45);
  stext(P, p->name, 272, 57, C(TEAM[p->team].ui), ALIGN_LEFT, 24);
  lives(P, p, 314, 57, 1.6f, C(TEAM[p->team].ui), C(0x2a3038));
}

/* ---- 8: NF-24 Cormorant, 1970s US Navy analog ------------------------------ */
static void panel_nf24(const Pan *P, const Ins *I) {
  Plane *p = I->p;
  char b[48], r1[16], r2[16];
  face(P, 0x4b5156, 0x2b2f33, 0x17191b);
  const uint32_t ink = C(0xeeeeee), fc = C(0x0c0c0c), grn = C(0x99ff99);
  /* wing sweep: the computer schedules it to Mach, the wing follows */
  float cmd = clampf(20 + (I->mach - 0.4f) * 80, 20, 68), act = cmd - clampf(p->gload * 0.3f, -2, 2);
  Dial sw = { 20, 68, -100, 100, 6, fc, ink, 0, 0, C(0xffcc33), "SWEEP", "AUTO", NULL, 0, 0, act, cmd, 0.85f };
  dial(P, 17, 16, 13.5f, &sw);
  /* the lamps under it, tall enough for WING SWEEP and MASTER CAUT on two lines on a small panel */
  lamp(P, "WING SWEEP", 2, 30, 30, 12.5f, fabsf(act - cmd) > 1.5f, 0xffb020);
  lamp(P, "MASTER CAUT", 2, 43, 30, 12.5f, I->lowfuel || I->stall || I->lowhull, 0xffb020);
  lamp(P, "LOCK", 2, 56, 30, 6.5f, I->lock, 0xff3030);
  fmt(r1, sizeof r1, "%d", I->spd);
  Dial asi = { 80, 850, 0, 340, 8, fc, ink, 0, 0, 0, "KNOTS", NULL, r1, 0, 0, I->spd, 0, 0 };
  dial(P, 46, 16, 12, &asi);
  fmt(r2, sizeof r2, "%05d", I->alt);
  Dial alt = { 0, 1000, 0, 360, 10, fc, ink, 0, 0, 0, "ALT", NULL, r2, 0, 0, fmodf(I->alt, 1000), 0, 0 };
  dial(P, 46, 46, 12, &alt);
  Dial acc = { -4, 10, -135, 135, 7, fc, ink, 0, 0, 0, "ACCEL", NULL, NULL, 7.5f, 10, I->gl, 0, 0 };
  dial(P, 72, 16, 11, &acc);
  Dial vvi = { -6000, 6000, 100, 440, 6, fc, ink, 0, 0, 0, "VERT", NULL, NULL, 0, 0, clampf(I->vs, -6000, 6000), 0, 0 };
  dial(P, 72, 46, 11, &vvi);
  stext(P, "FT/MIN", 72, 60.5f, C(0xbbbbbb), ALIGN_CENTER, 27); /* under the dial: too wide for its face */
  /* VDI: a contact-analog attitude CRT */
  prect(P, 86, 2, 66, 60, C(0x1a1c1e));
  {
    Framebuffer *fb = P->fb;
    float bank, pitch;
    bank_pitch(p, &bank, &pitch);
    float x = 89, y = 5, w = 60, h = 51;
    prect(P, x, y, w, h, C(0x021006));
    pclip(P, x, y, w, h);
    float cx = PX(P, x + w / 2), cy = PY(P, y + h / 2), ppd = PL(P, h) / 60, cb = cosf(bank), sb = sinf(bank);
    float off = pitch * (180 / PI_F) * ppd;
#define BX(bx, by) (cx + cb * (bx) + sb * (by))
#define BY(bx, by) (cy - sb * (bx) + cb * (by))
    float W = PL(P, w) * 1.5f, lw = fmaxf(1, P->k * 0.35f);
    fb_line(fb, BX(-W, off), BY(-W, off), BX(W, off), BY(W, off), C(0x66ff66), lw * 1.5f, 1);
    for (int i = 1; i < 8; i++) {
      float yy = off + i * i * PL(P, 0.9f);
      fb_line(fb, BX(-W, yy), BY(-W, yy), BX(W, yy), BY(W, yy), C(0x2a8a3a), lw, 1);
    }
    for (int q = -60; q <= 60; q += 10) {
      if (!q) continue;
      float by = off - q * ppd, hw = PL(P, 5);
      fb_line(fb, BX(-hw, by), BY(-hw, by), BX(hw, by), BY(hw, by), C(0x66ff66), lw, 1);
    }
#undef BX
#undef BY
    float s = PL(P, 12);
    fb_line(fb, cx - s, cy, cx - s * 0.3f, cy, C(0x99ff99), lw * 2, 1);
    fb_line(fb, cx - s * 0.3f, cy, cx, cy + s * 0.2f, C(0x99ff99), lw * 2, 1);
    fb_line(fb, cx, cy + s * 0.2f, cx + s * 0.3f, cy, C(0x99ff99), lw * 2, 1);
    fb_line(fb, cx + s * 0.3f, cy, cx + s, cy, C(0x99ff99), lw * 2, 1);
    stext(P, r1, x + 2, y + 3, grn, ALIGN_LEFT, 20);
    fmt(b, sizeof b, "%d", I->alt / 10 * 10);
    stext(P, b, x + w - 2, y + 3, grn, ALIGN_RIGHT, 24);
    punclip(P);
  }
  stext(P, "VDI", 119, 59, C(0xd9d9d2), ALIGN_CENTER, 20);
  /* HSD: the round tactical display */
  prect(P, 154, 2, 62, 60, C(0x1a1c1e));
  Scope S = { false, 12000, C(0x021006), C(0x1a5a2a), grn, grn, C(0x4aaa5a), grn, C(0xffffff), false, true };
  scope(P, I, 156, 4, 58, 52, &S);
  stext(P, "HSD", 185, 59, C(0xd9d9d2), ALIGN_CENTER, 20);
  /* engines and fuel, a little smaller and closer, for the stores column's width */
  Dial rl = { 0, 110, -135, 135, 11, fc, ink, 0, 0, 0, "RPM L", NULL, NULL, 0, 0, I->rpm, 0, 0 };
  dial(P, 227.5f, 13, 10, &rl);
  Dial rr = { 0, 110, -135, 135, 11, fc, ink, 0, 0, 0, "RPM R", NULL, NULL, 0, 0, I->rpm - 0.5f, 0, 0 };
  dial(P, 249.5f, 13, 10, &rr);
  fmt(r2, sizeof r2, "%05d", I->fuel / 100 * 16200);
  Dial fq = { 0, 100, -135, 135, 4, fc, ink, 0, 0, C(0xffcc33), "FUEL", NULL, r2, 0, 15, I->fuel + 2, I->fuel - 2, 0.8f };
  dial(P, 238.5f, 45, 14, &fq);
  gear_lamp(P, I, "GEAR", 226, 24.5f, 25, 6); /* between the engines and the fuel */
  /* stores, score and lights: every row fits with its full words at any size; the lines under the stores
   * follow them down when a pair of numbers needed two lines (a low-res panel) */
  prect(P, 262, 2, 56, 60, C(0x2a2e32));
  float ny = fmaxf(28, 3 + stores_block(P, I, 264, 3, 53.5f, ink, C(0x4a4e52), "LRM", "GUN", "FLR", "K", "TGT", "TK", true));
  stext(P, p->name, 264, ny, C(TEAM[p->team].ui), ALIGN_LEFT, 26);
  lives(P, p, 314, ny, 1.6f, C(TEAM[p->team].ui), C(0x3a3e42));
  float hy = ny + (ny > 28 ? 6 : 6.5f), ly = hy + 4.5f;
  stext(P, "HULL", 264, hy, C(0xbbbbbb), ALIGN_LEFT, 18);
  hbar(P, 282, hy - 1.5f, 34, 3, I->hull / 100, I->lowhull ? C(0xff3030) : C(0x55dd55), C(0x1a1a1a));
  lamp(P, "A/B", 264, ly, 24, 6, I->ab, 0x33ff33);
  lamp(P, "BINGO", 289, ly, 28.5f, 6, I->lowfuel, 0xffb020);
  lamp(P, "MSL", 264, ly + 7.5f, 24, 6, I->warn && I->blink, 0xff3030);
  lamp(P, "STALL", 289, ly + 7.5f, 28.5f, 6, I->stall, 0xff3030);
  stext(P, "RIO BEHIND", 290, ly + 18, C(0x9a9a9a), ALIGN_CENTER, 52);
}

/* ---- classic panels, in the C64's sixteen colours ----------------- */
enum { K_BLACK = 0x000000, K_WHITE = 0xffffff, K_RED = 0x883932, K_CYAN = 0x67b6bd, K_PURPLE = 0x8b3f96,
       K_GREEN = 0x55a049, K_BLUE = 0x40318d, K_YELLOW = 0xbfce72, K_ORANGE = 0x8b5429, K_LRED = 0xb86962,
       K_DGREY = 0x505050, K_GREY = 0x787878, K_LGREEN = 0x94e089, K_LBLUE = 0x7869c4, K_LGREY = 0x9f9f9f };

static void kbox(const Pan *P, float x, float y, float w, float h, const char *label) {
  prect(P, x, y, w, h, C(K_BLACK));
  pframe(P, x, y, w, h, C(K_LBLUE));
  if (label) stext(P, label, x + w / 2, y + h + 3, C(K_LBLUE), ALIGN_CENTER, w + 6);
}

/* pitch: a ladder bar with the nose marker; roll: a tilting horizon line */
static void kpitch(const Pan *P, const Plane *p, float x, float y, float w, float h) {
  float bank, pitch;
  bank_pitch(p, &bank, &pitch);
  kbox(P, x, y, w, h, "PITCH");
  for (int i = 1; i < 6; i++) pline(P, x + 2, y + h * i / 6, x + w - 2, y + h * i / 6, C(K_DGREY), 0.3f);
  float my = y + h / 2 - clampf(pitch / (PI_F / 2), -1, 1) * h * 0.45f;
  prect(P, x + 1, my - 1, w - 2, 2, C(K_YELLOW));
}
static void kroll(const Pan *P, const Plane *p, float x, float y, float w, float h) {
  float bank, pitch;
  bank_pitch(p, &bank, &pitch);
  kbox(P, x, y, w, h, "ROLL");
  float cx = x + w / 2, cy = y + h / 2, L = w * 0.42f;
  pline(P, cx - cosf(bank) * L, cy - sinf(bank) * L, cx + cosf(bank) * L, cy + sinf(bank) * L, C(K_CYAN), 0.6f);
  ptri(P, cx, cy - 2, cx - 2, cy + 2, cx + 2, cy + 2, C(K_YELLOW));
}
static void kcompass(const Pan *P, const Ins *I, float x, float y, float w, float h) {
  kbox(P, x, y, w, h, "COMPASS");
  pclip(P, x + 1, y + 1, w - 2, h - 2);
  static const char *const PTS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  for (int k = -12; k <= 12; k++) {
    float d = roundf(I->hdg / 15) * 15 + k * 15, px = x + w / 2 + (d - I->hdg) * w / 120;
    int dd = ((int)d % 360 + 360) % 360;
    pline(P, px, y + h - 2, px, y + h - (dd % 45 ? 3 : 5), C(K_WHITE), 0.3f);
    if (dd % 45 == 0) stext(P, PTS[dd / 45], px, y + h * 0.38f, C(K_WHITE), ALIGN_CENTER, 8);
  }
  punclip(P);
  ptri(P, x + w / 2, y + h - 5, x + w / 2 - 2, y + h - 1, x + w / 2 + 2, y + h - 1, C(K_YELLOW));
}
static void kpower(const Pan *P, const Ins *I, float x, float y, float w, float h) {
  kbox(P, x, y, w, h, "POWER FUEL");
  float bw = w / 2 - 3;
  prect(P, x + 2, y + 2 + (h - 4) * (1 - I->p->throttle), bw, (h - 4) * I->p->throttle, C(I->ab ? K_ORANGE : K_GREEN));
  prect(P, x + w / 2 + 1, y + 2 + (h - 4) * (1 - I->fuel / 100), bw, (h - 4) * I->fuel / 100, C(I->lowfuel ? K_RED : K_YELLOW));
}
static void kradar(const Pan *P, const Ins *I, float x, float y, float s) {
  Scope S = { false, 12000, C(K_BLACK), C(K_BLUE), C(K_WHITE), C(K_LRED), C(K_LBLUE), C(K_RED), C(K_YELLOW), true, true };
  scope(P, I, x, y, s, s, &S);
  fb_ring(P->fb, PX(P, x + s / 2), PY(P, y + s / 2), PL(P, s / 2), C(K_LBLUE), 1);
  stext(P, "RADAR", x + s / 2, y + s + 3, C(K_LBLUE), ALIGN_CENTER, s + 6);
}

static const char *const WCODE[W_COUNT] = { "C", "H", "R", "S" };
static int wcount(const Plane *p, int w) {
  return w == W_CANNON ? (int)p->ammo : w == W_HEAT ? p->missiles : w == W_RADAR ? p->radar : p->ground;
}

static void panel_c64(const Pan *P, const Ins *I, bool two) {
  Plane *p = I->p;
  char b[48];
  fb_rect(P->fb, P->x, P->y, P->w, P->h, C(K_BLUE));
  fb_rect(P->fb, P->x, P->y, P->w, (int)fmaxf(2, P->h / 30.0f), C(K_LBLUE));
  if (!two) {
    /* player 1: messages, radar, pitch, roll, altitude/velocity, power/fuel, compass */
    kbox(P, 3, 4, 104, 26, "MESSAGES");
    const char *msg = p->evT > 0 ? p->ev : I->warn ? "MISSILE CHASING!" : I->lock ? "TARGET LOCKED" : "";
    stext(P, msg, 6, 10, I->warn ? C(K_LRED) : C(K_WHITE), ALIGN_LEFT, 98);
    snprintf(b, sizeof b, "%s %d", (const char *[]){ "CANNON", "HEAT", "RADAR", "GROUND" }[p->wsel], wcount(p, p->wsel));
    stext(P, b, 6, 18, C(K_YELLOW), ALIGN_LEFT, 98);
    if (!I->warn) {
      snprintf(b, sizeof b, "FLARES %d  CHAFF %d", p->flares, p->chaff);
      stext(P, b, 6, 25, C(K_LGREY), ALIGN_LEFT, 98);
    }
    kradar(P, I, 112, 4, 50);
    kpitch(P, p, 168, 4, 14, 50);
    kroll(P, p, 186, 4, 40, 22);
    kbox(P, 186, 32, 40, 22, "ALT VEL");
    fmt(b, sizeof b, "%05d", I->alt);
    stext(P, b, 206, 39, C(K_WHITE), ALIGN_CENTER, 38);
    fmt(b, sizeof b, "%d KT", I->spd);
    stext(P, b, 206, 47, I->stall ? C(K_LRED) : C(K_WHITE), ALIGN_CENTER, 38);
    kpower(P, I, 230, 4, 26, 50);
    kcompass(P, I, 260, 4, 56, 22);
    snprintf(b, sizeof b, "%s %d", p->name, plane_score(p));
    stext(P, b, 288, 37, C(TEAM[p->team].ui), ALIGN_CENTER, 56);
    lives(P, p, 300, 48, 2, C(TEAM[p->team].ui), C(K_DGREY));
    stext(P, I->stall ? "STALL" : "", 270, 48, C(K_LRED), ALIGN_CENTER, 24);
  } else {
    /* player 2: the C/H/R/S selector, altitude, velocity, the number of the
     * selected weapon, power/fuel, radar, pitch/roll and compass */
    for (int w = 0; w < W_COUNT; w++) {
      float x = 3 + w * 14;
      bool on = p->wsel == w;
      prect(P, x, 4, 12, 12, on ? C(K_YELLOW) : C(K_BLACK));
      pframe(P, x, 4, 12, 12, C(K_LBLUE));
      if (w != W_CANNON || plane_has_gun(p)) /* a jet without a gun: an empty box where the C would be */
        ptext_at(P, WCODE[w], x + 6, 10, on ? C(K_BLACK) : C(K_WHITE), ALIGN_CENTER, P->ts, 10);
    }
    stext(P, "WEAPON", 30, 19.5f, C(K_LBLUE), ALIGN_CENTER, 56);
    kbox(P, 3, 24, 26, 12, "ALT");
    fmt(b, sizeof b, "%d", I->alt / 100);
    stext(P, b, 16, 30, C(K_WHITE), ALIGN_CENTER, 24);
    kbox(P, 31, 24, 26, 12, "VEL");
    fmt(b, sizeof b, "%d", I->spd);
    stext(P, b, 44, 30, I->stall ? C(K_LRED) : C(K_WHITE), ALIGN_CENTER, 24);
    kbox(P, 3, 44, 54, 12, "NUMBER");
    snprintf(b, sizeof b, "%s %d", WCODE[p->wsel], wcount(p, p->wsel));
    stext(P, b, 30, 50, C(K_YELLOW), ALIGN_CENTER, 52);
    kpower(P, I, 62, 4, 26, 50);
    kradar(P, I, 94, 4, 50);
    kroll(P, p, 150, 4, 40, 22);
    kpitch(P, p, 194, 4, 14, 50);
    kcompass(P, I, 150, 32, 40, 22);
    kbox(P, 214, 4, 102, 26, NULL);
    const char *msg = p->evT > 0 ? p->ev : I->warn ? "MISSILE CHASING!" : I->lock ? "TARGET LOCKED" : p->name;
    stext(P, msg, 218, 11, I->warn ? C(K_LRED) : C(K_WHITE), ALIGN_LEFT, 96);
    snprintf(b, sizeof b, "FLARES %d  CHAFF %d", p->flares, p->chaff);
    stext(P, b, 218, 19, C(K_LGREY), ALIGN_LEFT, 96);
    snprintf(b, sizeof b, "SCORE %d  KILLS %d", plane_score(p), p->kills);
    stext(P, b, 218, 26, C(K_WHITE), ALIGN_LEFT, 96);
    lives(P, p, 310, 42, 2, C(TEAM[p->team].ui), C(K_DGREY));
    stext(P, I->stall ? "STALL" : "", 260, 42, C(K_LRED), ALIGN_CENTER, 30);
  }
}

/* The classic map view: the whole arena from above, north up. */
void hud_gauges(Game *g, Plane *p, Gauges *out) {
  Ins I;
  read_ins(&I, g, p, true);
  float bank, pitch;
  bank_pitch(p, &bank, &pitch);
  *out = (Gauges){ I.spd / 3.6f, I.alt, I.vs, I.hdg, I.gl, I.fuel, bank * (180 / PI_F), pitch * (180 / PI_F) };
}

void hud_map(Framebuffer *fb, Game *g, Plane *p, float vx, float vy, float vw, float vh) {
  fb_rect(fb, (int)vx, (int)vy, (int)vw, (int)vh, C(K_BLUE));
  float sc = fminf((vw - 20) / (2 * ARENA_X), (vh - 30) / (2 * ARENA_Z)), cx = vx + vw / 2, cy = vy + vh / 2 + 6;
#define MX(x) (cx + (x) * sc)
#define MY(z) (cy - (z) * sc)
  fb_rect(fb, (int)MX(-ARENA_X), (int)MY(ARENA_Z), (int)(2 * ARENA_X * sc), (int)(2 * ARENA_Z * sc), C(0x2a2370));
  if (MAP) {
    float cell = MAP->spacing, cs = fmaxf(1, cell * sc);
    for (int j = 0; j < MAP_CELLS; j++)
      for (int i = 0; i < MAP_CELLS; i++) {
        int mi = MAP->mat[j * MAP_CELLS + i];
        if (mi < MAP->npal && MAP->pal[mi].water) continue;
        float wx = MAP->origin_x + (i + 0.5f) * cell, wz = MAP->origin_z + (j + 0.5f) * cell;
        if (fabsf(wx) > ARENA_X || fabsf(wz) > ARENA_Z) continue;
        fb_rect(fb, (int)(MX(wx) - cs / 2), (int)(MY(wz) - cs / 2), (int)ceilf(cs), (int)ceilf(cs), C(K_GREEN));
      }
    for (int i = 0; i < MAP->ncarriers; i++)
      fb_rect(fb, (int)MX(MAP->carriers[i].x) - 3, (int)MY(MAP->carriers[i].z) - 6, 6, 12, C(TEAM[MAP->carriers[i].team].ui));
  }
  for (int t = 0; t < 2; t++) fb_rect_stroke(fb, MX(BASES[t].x) - 4, MY(BASES[t].z) - 8, 8, 16, C(TEAM[t].ui), 1);
  /* your home edge: fly off it below 1000 ft to land on the carrier */
  float ez = p->team == 0 ? -ARENA_Z : ARENA_Z;
  fb_line(fb, MX(-ARENA_X), MY(ez), MX(ARENA_X), MY(ez), C(K_YELLOW), 2, 1);
  fb_text(fb, "HOME - FLY OFF HERE BELOW 1000 FT", cx, MY(ez) + (p->team == 0 ? 8 : -8), 1, C(K_YELLOW), ALIGN_CENTER, 0);
  for (int i = 0; i < g->ntargets; i++)
    if (g->targets[i].alive)
      fb_rect(fb, (int)MX(g->targets[i].pos.x) - 2, (int)MY(g->targets[i].pos.z) - 2, 4, 4,
              C(g->targets[i].team == p->team ? K_LGREEN : K_RED));
  for (int i = 0; i < g->nmissiles; i++) fb_rect(fb, (int)MX(g->missiles[i].pos.x) - 1, (int)MY(g->missiles[i].pos.z) - 1, 2, 2, C(K_YELLOW));
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *e = &g->planes[i];
    if (!e->alive || e->inactive) continue;
    float h = atan2f(e->f.x, e->f.z), x = MX(e->pos.x), y = MY(e->pos.z), s = 6;
    float xy[6] = { x + sinf(h) * s, y - cosf(h) * s, x + sinf(h + 2.5f) * s, y - cosf(h + 2.5f) * s, x + sinf(h - 2.5f) * s, y - cosf(h - 2.5f) * s };
    fb_poly(fb, xy, 3, e == p ? C(K_WHITE) : C(TEAM[e->team].ui), 1, 0);
    if (e == p) fb_text(fb, "YOU", x + 8, y - 6, 1, C(K_WHITE), ALIGN_LEFT, 0);
  }
#undef MX
#undef MY
  char b[48];
  snprintf(b, sizeof b, "MAP VIEW   ALT %d FT", (int)roundf(fmaxf(0, p->pos.y * FT_PER_M)));
  fb_text(fb, b, vx + 8, vy + 10, 1, C(K_WHITE), ALIGN_LEFT, 0);
  fb_text(fb, p->hidx == 1 ? "B RETURNS" : "C RETURNS", vx + vw - 8, vy + 10, 1, C(K_LBLUE), ALIGN_RIGHT, 0);
}

void hud_panel(Framebuffer *fb, Game *g, Plane *p, float x, float y, float w, float h) {
  float k = fminf(w / 320, h / 64);
  Pan P = { fb, roundf(x + (w - 320 * k) / 2), roundf(y + (h - 64 * k) / 2), k, (int)fmaxf(1, floorf(k + 0.25f)),
            (int)fmaxf(1, floorf(k * 0.5f + 0.3f)), (int)x, (int)y, (int)w, (int)h };
  const char *id = p->jet >= 0 && p->jet < NJETS ? JETS[p->jet].id : "";
  static const struct { const char *id; void (*draw)(const Pan *, const Ins *); bool metric; } PANELS[] = {
    { "af40", panel_af40, false }, { "lf27", panel_lf27, false }, { "ecf90", panel_ecf90, false },
    { "ja46", panel_ja46, true },  { "sy26", panel_sy26, true },  { "vk33", panel_vk33, true },
    { "ks55", panel_ks55, true },  { "nf24", panel_nf24, false },
  };
  Ins I;
  if (g->classic) {
    read_ins(&I, g, p, false);
    panel_c64(&P, &I, p->hidx == 1);
    return;
  }
  for (size_t i = 0; i < sizeof PANELS / sizeof PANELS[0]; i++)
    if (!strcmp(id, PANELS[i].id)) {
      read_ins(&I, g, p, PANELS[i].metric);
      PANELS[i].draw(&P, &I);
      /* the panels fill the strip; the team colour runs along the top */
      fb_rect(fb, (int)x, (int)y, (int)w, (int)fmaxf(1, roundf(h / 60)), C(TEAM[p->team].ui));
      return;
    }
  read_ins(&I, g, p, false);
  panel_classic(&P, &I);
}
