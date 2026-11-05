/* see.c - the live test player's eyes: reads text in the game's own 5x7 font, the horizon, dial needles, and
 * whether a jet, a missile or a round can be seen, from the picture's pixels alone. Original code for this
 * project. The font comes from the game itself (src/raster.c draws each glyph once into a small picture), so the
 * reader always knows the letters the game uses. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lp.h"
#include "raster.h"

/* ---- pixels ---- */
uint32_t px_at(const Frame *F, int x, int y) {
  if (!F->px || x < 0 || y < 0 || x >= F->w || y >= F->h) return 0; /* (a sheet can come without its picture) */
  return F->px[(size_t)y * F->w + x];
}
static inline int R_(uint32_t c) { return (int)(c >> 16 & 255); }
static inline int G_(uint32_t c) { return (int)(c >> 8 & 255); }
static inline int B_(uint32_t c) { return (int)(c & 255); }
static inline int cdist(uint32_t a, uint32_t b) {
  return abs(R_(a) - R_(b)) + abs(G_(a) - G_(b)) + abs(B_(a) - B_(b));
}
static inline bool near(uint32_t a, uint32_t b, int tol) {
  return abs(R_(a) - R_(b)) <= tol && abs(G_(a) - G_(b)) <= tol && abs(B_(a) - B_(b)) <= tol;
}
static inline float luma(uint32_t c) { return 0.299f * R_(c) + 0.587f * G_(c) + 0.114f * B_(c); }

/* ---- the font ---- */
#define MAXG 256 /* ASCII 32..95, then the letters beyond it the font has (Swedish, Cyrillic...), each read as '?' */
static uint8_t GL[MAXG][7]; /* rows, bit 4 = left column */
static char    GC[MAXG];    /* the character each reads as */
static int     glcmin[MAXG], glcmax[MAXG], nglyph;
static bool    glfull[MAXG]; /* uses the top and the bottom row */

static void utf8(unsigned cp, char *o) {
  if (cp < 0x80) o[0] = (char)cp, o[1] = 0;
  else if (cp < 0x800) o[0] = (char)(0xC0 | cp >> 6), o[1] = (char)(0x80 | (cp & 63)), o[2] = 0;
  else o[0] = (char)(0xE0 | cp >> 12), o[1] = (char)(0x80 | (cp >> 6 & 63)), o[2] = (char)(0x80 | (cp & 63)), o[3] = 0;
}

static void draw_glyph(Framebuffer *fb, const char *s, uint8_t rows[7]) {
  fb_clear(fb, 0xFF000000u);
  fb_text(fb, s, 0, 3.0f, 1, 0xFFFFFFFFu, ALIGN_LEFT, 0); /* y is the text's middle, rounded: rows 0..6 */
  for (int r = 0; r < 7; r++) {
    rows[r] = 0;
    for (int c = 0; c < 5; c++)
      if (fb->px[r * 8 + c] == 0xFFFFFFFFu) rows[r] |= (uint8_t)(0x10 >> c);
  }
}
static void add_glyph(const uint8_t rows[7], char ch) {
  int i = nglyph++;
  memcpy(GL[i], rows, 7);
  GC[i] = ch;
  glcmin[i] = 9, glcmax[i] = -1;
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 5; c++)
      if (rows[r] & (0x10 >> c)) {
        if (c < glcmin[i]) glcmin[i] = c;
        if (c > glcmax[i]) glcmax[i] = c;
      }
  glfull[i] = rows[0] && rows[6];
}

void see_init(void) {
  Framebuffer fb = { 0 };
  fb_resize(&fb, 8, 8);
  uint8_t rows[7];
  nglyph = 0;
  for (unsigned c = 32; c < 96; c++) {
    char t[2] = { (char)c, 0 };
    draw_glyph(&fb, t, rows);
    add_glyph(rows, (char)c);
  }
  /* beyond ASCII: Latin-1 and Latin Extended-A, then Cyrillic capitals; only shapes not already known (a
   * Cyrillic A is drawn as an A) and not the unknown '?' */
  /* (and Greek: the Soviet panels' alpha) */
  for (unsigned c = 0xB0; c < 0x430 && nglyph < MAXG; c = c == 0x17F ? 0x391 : c == 0x3C9 ? 0x410 : c + 1) {
    char t[4];
    utf8(c, t);
    draw_glyph(&fb, t, rows);
    bool known = false;
    for (int i = 0; i < nglyph && !known; i++) known = !memcmp(GL[i], rows, 7);
    if (!known) add_glyph(rows, '?');
  }
  fb_free(&fb);
}

/* one 6x7 cell at (gx, gy), scale s, ink c: the best glyph and its error (cells wrong) */
/* a dot of ink: its colour, with something plainly different within half a cell (the gap to the next dot, an
 * outline, the panel); a stretch of scenery that happens to be the ink's colour has no such edge */
static bool ink_dot(const Frame *F, int x, int y, int s, uint32_t c, int tol) {
  if (!near(px_at(F, x, y), c, tol)) return false;
  int d = s / 2 + 1;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy) && cdist(px_at(F, x + dx * d, y + dy * d), c) > 60) return true;
  return false;
}
static int cell_lit(const Frame *F, int gx, int gy, int s, uint32_t c, int tol, uint8_t rows[7]) {
  int n = 0;
  for (int r = 0; r < 7; r++) {
    rows[r] = 0;
    for (int k = 0; k < 5; k++)
      if (ink_dot(F, gx + k * s + s / 2, gy + r * s + s / 2, s, c, tol)) rows[r] |= (uint8_t)(0x10 >> k), n++;
  }
  return n;
}
static int best_glyph(const uint8_t rows[7], int *err) {
  int best = 0, be = 99;
  for (int i = 0; i < nglyph; i++) {
    int e = 0;
    for (int r = 0; r < 7; r++) e += __builtin_popcount((unsigned)(GL[i][r] ^ rows[r]));
    if (e < be) be = e, best = i;
  }
  *err = be;
  return best;
}

/* ---- text ---- */
typedef struct { int x0, y0, x1, y1, n; uint32_t c; } Comp;
static int   *lab;
static size_t labcap;
static int   *stk;
static Comp  *comps;
static size_t ccap;

#define TOL 14
#define MAXS 24 /* the biggest letters: the pause and page titles */
/* every patch of one colour small enough to be a letter */
static int find_comps(const Frame *F) {
  size_t N = (size_t)F->w * F->h;
  if (labcap < N) {
    free(lab), free(stk);
    lab = malloc(N * sizeof *lab), stk = malloc(N * sizeof *stk);
    labcap = N;
  }
  memset(lab, 0, N * sizeof *lab);
  int nc = 0;
  for (int y = 0; y < F->h; y++)
    for (int x = 0; x < F->w; x++) {
      size_t i = (size_t)y * F->w + x;
      if (lab[i]) continue;
      uint32_t c = F->px[i];
      Comp k = { x, y, x, y, 0, c };
      int sp = 0;
      stk[sp++] = (int)i;
      lab[i] = -1;
      bool big = false;
      while (sp) {
        int j = stk[--sp], jx = j % F->w, jy = j / F->w;
        k.n++;
        if (jx < k.x0) k.x0 = jx;
        if (jx > k.x1) k.x1 = jx;
        if (jy < k.y0) k.y0 = jy;
        if (jy > k.y1) k.y1 = jy;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = jx + dx, ny = jy + dy;
            if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= F->w || ny >= F->h) continue;
            size_t q = (size_t)ny * F->w + nx;
            if (lab[q] || !near(F->px[q], c, TOL)) continue;
            lab[q] = -1;
            stk[sp++] = (int)q;
          }
      }
      big = k.x1 - k.x0 > 7 * MAXS || k.y1 - k.y0 > 7 * MAXS || k.n > 35 * MAXS * MAXS; /* bigger than a letter */
      if (big) continue;
      if ((size_t)nc >= ccap) {
        size_t cap = ccap ? ccap * 2 : 4096;
        Comp *grown = realloc(comps, cap * sizeof *comps);
        if (!grown) return nc; /* out of memory: the shapes found so far */
        comps = grown;
        ccap = cap;
      }
      comps[nc++] = k;
    }
  return nc;
}

/* decode a line from a glyph at (gx, gy): walk both ways while the cells read as letters */
static int decode_line(const Frame *F, int gx, int gy, int s, uint32_t c, char *out, int max, int *lx0, int *lx1,
                       float *err) {
  char left[64], right[64];
  int nl = 0, nr = 0, errs = 0, glyphs = 0;
  uint8_t rows[7];
  /* right from the anchor (inclusive) */
  int blanks = 0, x;
  for (x = gx; nr < 63 && x < F->w; x += 6 * s) {
    int lit = cell_lit(F, x, gy, s, c, TOL + 10, rows), e;
    if (!lit) {
      if (++blanks > 2) break; /* two spaces part fields in a line */
      right[nr++] = ' ';
      continue;
    }
    int gi = best_glyph(rows, &e);
    if (e > 3) break;
    blanks = 0;
    right[nr++] = GC[gi], errs += e, glyphs++;
  }
  while (nr && right[nr - 1] == ' ') nr--;
  *lx1 = gx + nr * 6 * s - s;
  blanks = 0;
  int first = gx;
  for (x = gx - 6 * s; nl < 63 && x >= 0; x -= 6 * s) {
    int lit = cell_lit(F, x, gy, s, c, TOL + 10, rows), e;
    if (!lit) {
      if (++blanks > 2) break; /* two spaces part fields in a line */
      left[nl++] = ' ';
      continue;
    }
    int gi = best_glyph(rows, &e);
    if (e > 3) break;
    blanks = 0;
    left[nl++] = GC[gi], errs += e, glyphs++;
    first = x;
  }
  while (nl && left[nl - 1] == ' ') nl--;
  *lx0 = first;
  int n = 0;
  for (int i = nl - 1; i >= 0 && n < max - 1; i--) out[n++] = left[i];
  for (int i = 0; i < nr && n < max - 1; i++) out[n++] = right[i];
  out[n] = 0;
  *err = glyphs ? (float)errs / glyphs : 9;
  return glyphs;
}

/* every pixel around the box is far from the ink's colour (a green letter on blue sky: low in brightness
 * contrast, but plainly a letter) */
static bool stands_apart(const Frame *F, int x0, int y0, int x1, int y1, uint32_t col) {
  int n = 0, far = 0;
  for (int x = x0 - 2; x <= x1 + 1; x++)
    for (int k = 0; k < 2; k++) {
      int y = k ? y1 + 1 : y0 - 2;
      if (x < 0 || y < 0 || x >= F->w || y >= F->h) continue;
      n++, far += cdist(px_at(F, x, y), col) > 120;
    }
  return n && far * 10 >= n * 9; /* a tick or a mark may touch the ring */
}

int see_texts(const Frame *F, Read *out, int max) {
  int nc = find_comps(F), n = 0;
  for (int i = 0; i < nc && n < max; i++) {
    const Comp *k = &comps[i];
    int h = k->y1 - k->y0 + 1, w = k->x1 - k->x0 + 1;
    if (h % 7) continue;
    int s = h / 7;
    if (s < 1 || s > MAXS || w > 5 * s || w % s) continue;
    /* already part of a line read? */
    bool done = false;
    for (int j = 0; j < n && !done; j++)
      done = out[j].scale == s && k->y0 == out[j].y0 && k->x0 >= out[j].x0 && k->x1 <= out[j].x1 && near(out[j].col, k->c, TOL);
    if (done) continue;
    /* which full-height glyph fits this patch */
    int best = -1, be = 99, bgx = 0;
    uint8_t rows[7];
    for (int g = 1; g < nglyph; g++) {
      if (!glfull[g] || (glcmax[g] - glcmin[g] + 1) * s != w) continue;
      int gx = k->x0 - glcmin[g] * s, e;
      cell_lit(F, gx, k->y0, s, k->c, TOL + 10, rows);
      int gi = best_glyph(rows, &e);
      if (gi == g && e < be) be = e, best = g, bgx = gx;
    }
    if (best < 0 || be > 1) continue;
    Read r = { 0 };
    r.scale = s;
    r.col = k->c;
    r.y0 = k->y0, r.y1 = k->y0 + 7 * s;
    if (decode_line(F, bgx, k->y0, s, k->c, r.s, sizeof r.s, &r.x0, &r.x1, &r.err) < 1) continue;
    r.x1 += 1;
    /* one letter alone, like a stray L-shaped patch in the scenery: only when it reads perfectly and stands clear */
    if (strlen(r.s) == 1 && (r.err > 0 || (see_luma_contrast(F, r.x0, r.y0, r.x1, r.y1, r.col) < 2.5f &&
                                           !stands_apart(F, r.x0, r.y0, r.x1, r.y1, r.col))))
      continue;
    out[n++] = r;
  }
  return n;
}

/* the letters in a box where a line of n cells at scale s was drawn (picture pixels), read cell by cell in the box's
 * ink: for a letter alone (a '%' or a 'G' on a dial's face), which the line reader takes only when it stands clear
 * of everything (a stray L-shaped patch of scenery reads as an L) */
static uint32_t cells_ink;
uint32_t see_cells_ink(void) { return cells_ink; }
int see_cells(const Frame *F, int x0, int y0, int x1, int y1, char *out, int max) {
  int s = (int)lroundf((y1 - y0) / 7.0f), n = s > 0 ? (x1 - x0 + s) / (6 * s) : 0;
  out[0] = 0;
  if (s < 1 || n < 1 || n >= max) return 0;
  /* the ink: the commonest colour in the box plainly apart from the box's edge */
  static uint32_t col[4096];
  static int cnt[4096];
  int nc = 0;
  uint32_t edge = px_at(F, x0 - 1, y0 - 1);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      uint32_t c = px_at(F, x, y);
      if (cdist(c, edge) < 100) continue;
      int i;
      for (i = 0; i < nc && col[i] != c; i++) {}
      if (i == nc && nc < 4096) col[nc] = c, cnt[nc++] = 0;
      if (i < nc) cnt[i]++;
    }
  int bi = -1;
  for (int i = 0; i < nc; i++)
    if (bi < 0 || cnt[i] > cnt[bi]) bi = i;
  if (bi < 0) return 0;
  cells_ink = col[bi];
  int k = 0;
  for (int i = 0; i < n; i++) {
    uint8_t rows[7];
    int e, lit = cell_lit(F, x0 + i * 6 * s, y0, s, col[bi], TOL + 10, rows);
    int gi = lit ? best_glyph(rows, &e) : 0;
    out[k++] = !lit ? ' ' : e <= 1 ? GC[gi] : '\x01';
  }
  out[k] = 0;
  return k;
}

/* ---- contrast of a text against what is around it (WCAG ratio, 1..21) ---- */
static float lin(float c) {
  c /= 255;
  return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
static float rel_lum(uint32_t c) { return 0.2126f * lin((float)R_(c)) + 0.7152f * lin((float)G_(c)) + 0.0722f * lin((float)B_(c)); }
static int cmpf(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}
float see_luma_contrast(const Frame *F, int x0, int y0, int x1, int y1, uint32_t col) {
  /* the background the letters stand on: the pixels that touch the ink (a shadow or an outline counts, as it
   * does for the eye; with neither it is the sky or panel round the strokes) */
  static float L[8192];
  int n = 0;
  for (int y = y0 - 2; y < y1 + 2 && n < 8192; y++)
    for (int x = x0 - 2; x < x1 + 2 && n < 8192; x++) {
      if (x < 0 || y < 0 || x >= F->w || y >= F->h) continue;
      uint32_t c = px_at(F, x, y);
      if (near(c, col, TOL + 10)) continue;
      bool touches = false;
      for (int dy = -1; dy <= 1 && !touches; dy++)
        for (int dx = -1; dx <= 1 && !touches; dx++) touches = (dx || dy) && near(px_at(F, x + dx, y + dy), col, TOL + 10);
      if (touches) L[n++] = rel_lum(c);
    }
  if (!n) return 1;
  qsort(L, (size_t)n, sizeof *L, cmpf);
  float bg = L[n / 2], fg = rel_lum(col);
  float hi = fmaxf(bg, fg), lo = fminf(bg, fg);
  return (hi + 0.05f) / (lo + 0.05f);
}

/* ---- the horizon ---- */
bool see_horizon(const Frame *F, int x0, int y0, int x1, int y1, float *a, float *b, float *quality) {
  enum { COLS = 40 };
  float px[COLS], py[COLS];
  int np = 0, H = 6 * F->h / 360 + 2;
  for (int i = 0; i < COLS; i++) {
    int x = x0 + (x1 - x0) * (2 * i + 1) / (2 * COLS);
    float bestD = 0;
    int by = -1;
    for (int y = y0 + H; y < y1 - H; y++) {
      float up[3] = { 0 }, dn[3] = { 0 };
      for (int k = 1; k <= H; k++) {
        uint32_t u = px_at(F, x, y - k), d = px_at(F, x, y + k);
        up[0] += R_(u), up[1] += G_(u), up[2] += B_(u);
        dn[0] += R_(d), dn[1] += G_(d), dn[2] += B_(d);
      }
      float D = (fabsf(up[0] - dn[0]) + fabsf(up[1] - dn[1]) + fabsf(up[2] - dn[2])) / H;
      if (D > bestD) bestD = D, by = y;
    }
    if (by >= 0 && bestD > 18) px[np] = (float)x, py[np] = (float)by, np++;
  }
  if (np < 6) return false;
  /* the line most columns agree on */
  int bestIn = 0;
  float ba = 0, bb = 0, tol = 3.0f * F->h / 360 + 1;
  for (int i = 0; i < np; i++)
    for (int j = i + 1; j < np; j++) {
      if (px[j] == px[i]) continue;
      float sl = (py[j] - py[i]) / (px[j] - px[i]), ic = py[i] - sl * px[i];
      int in = 0;
      for (int k = 0; k < np; k++) in += fabsf(ic + sl * px[k] - py[k]) < tol;
      if (in > bestIn) bestIn = in, ba = ic, bb = sl;
    }
  /* least squares over its inliers */
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0;
  for (int k = 0; k < np; k++)
    if (fabsf(ba + bb * px[k] - py[k]) < tol) sx += px[k], sy += py[k], sxx += px[k] * px[k], sxy += px[k] * py[k], n++;
  if (n >= 2 && fabs(n * sxx - sx * sx) > 1e-6) {
    bb = (float)((n * sxy - sx * sy) / (n * sxx - sx * sx));
    ba = (float)((sy - bb * sx) / n);
  }
  *a = ba, *b = bb, *quality = (float)bestIn / COLS;
  return bestIn >= COLS * 0.4f;
}

/* the horizon near a guess: in each column the steps (the sky side, above when sky_up, no darker) within band pixels
 * of y = a0 + b0 x, several a column, then the line the most columns agree on within dang degrees of the guess (a
 * faint hazy horizon lines up across the view; the stronger edges of coasts and clouds do not) */
bool see_horizon_near(const Frame *F, int x0, int y0, int x1, int y1, float a0, float b0, float band, float dang, bool sky_up,
                      float *a, float *b, float *quality) {
  enum { COLS = 48, PER = 6 };
  static float D[4096];
  float px[COLS * PER], py[COLS * PER];
  int pc[COLS * PER], np = 0, H = 6 * F->h / 360 + 2;
  for (int i = 0; i < COLS; i++) {
    int x = x0 + (x1 - x0) * (2 * i + 1) / (2 * COLS);
    float g = a0 + b0 * x;
    int ya = (int)(g - band), yb = (int)(g + band);
    if (ya < y0 + H) ya = y0 + H;
    if (yb > y1 - H) yb = y1 - H;
    if (yb - ya > 4096) yb = ya + 4096;
    for (int y = ya; y < yb; y++) {
      float up[3] = { 0 }, dn[3] = { 0 }, lu = 0, ld = 0;
      for (int k = 1; k <= H; k++) {
        uint32_t u = px_at(F, x, y - k), d = px_at(F, x, y + k);
        up[0] += R_(u), up[1] += G_(u), up[2] += B_(u), lu += luma(u);
        dn[0] += R_(d), dn[1] += G_(d), dn[2] += B_(d), ld += luma(d);
      }
      float v = (fabsf(up[0] - dn[0]) + fabsf(up[1] - dn[1]) + fabsf(up[2] - dn[2])) / H;
      D[y - ya] = (sky_up ? lu < ld : ld < lu) ? 0 : v;
    }
    /* the strongest local peaks */
    int got = 0;
    for (int pass = 0; pass < PER; pass++) {
      int by = -1;
      float bd = 18;
      for (int y = ya; y < yb; y++) {
        float v = D[y - ya];
        if (v <= bd) continue;
        bool peak = true;
        for (int k = -H; k <= H && peak; k++)
          if (k && y + k >= ya && y + k < yb && D[y + k - ya] > v) peak = false;
        if (peak) bd = v, by = y;
      }
      if (by < 0) break;
      for (int k = -H; k <= H; k++)
        if (by + k >= ya && by + k < yb) D[by + k - ya] = 0; /* taken */
      px[np] = (float)x, py[np] = (float)by, pc[np] = i, np++, got++;
    }
    (void)got;
  }
  if (np < 4) return false;
  float ang0 = atanf(b0), lim = dang * 3.14159265f / 180, tol = 3.0f * F->h / 360 + 1;
  int bestIn = 0;
  float ba = a0, bb = b0;
  /* the lines found, so the one nearest the sky among the well supported can win: a coast far out may line up
   * across the view too, but below the horizon */
  static float LA[3000], LB[3000];
  static int   LN[3000];
  int nl = 0;
  float xm = (x0 + x1) / 2.0f;
  unsigned rs = 12345;
  for (int t = 0; t < 3000; t++) {
    rs = rs * 1103515245u + 12345u;
    int i = (int)((rs >> 8) % (unsigned)np);
    rs = rs * 1103515245u + 12345u;
    int j = (int)((rs >> 8) % (unsigned)np);
    if (pc[i] == pc[j]) continue;
    float sl = (py[j] - py[i]) / (px[j] - px[i]);
    if (fabsf(atanf(sl) - ang0) > lim) continue;
    float ic = py[i] - sl * px[i];
    int in = 0, last = -1;
    for (int k = 0; k < np; k++)
      if (pc[k] != last && fabsf(ic + sl * px[k] - py[k]) < tol) in++, last = pc[k]; /* one a column */
    if (in > bestIn) bestIn = in;
    LA[nl] = ic, LB[nl] = sl, LN[nl] = in, nl++;
  }
  int need = bestIn * 6 / 10 > COLS / 4 ? bestIn * 6 / 10 : COLS / 4, pick = -1;
  for (int k = 0; k < nl; k++) {
    if (LN[k] < need) continue;
    float y = LA[k] + LB[k] * xm, yp = pick < 0 ? 0 : LA[pick] + LB[pick] * xm;
    if (pick < 0 || (sky_up ? y < yp - tol : y > yp + tol) || (fabsf(y - yp) <= tol && LN[k] > LN[pick])) pick = k;
  }
  if (pick >= 0) ba = LA[pick], bb = LB[pick], bestIn = LN[pick];
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0, last = -1;
  for (int k = 0; k < np; k++)
    if (pc[k] != last && fabsf(ba + bb * px[k] - py[k]) < tol)
      sx += px[k], sy += py[k], sxx += px[k] * px[k], sxy += px[k] * py[k], n++, last = pc[k];
  if (n >= 3 && fabs(n * sxx - sx * sx) > 1e-6) {
    float nb = (float)((n * sxy - sx * sy) / (n * sxx - sx * sx));
    if (fabsf(atanf(nb) - ang0) <= lim) bb = nb, ba = (float)((sy - bb * sx) / n);
  }
  *a = ba, *b = bb, *quality = (float)bestIn / COLS;
  return bestIn >= COLS / 4;
}

/* ---- dial needles ---- */
/* a needle runs from the hub out toward the ticks: lit just outside the hub and on the stretch between the face's
 * writing and the ticks (0.58-0.76 r). The digits of a number window and the dial's title lie inside 0.66 r, and the
 * needle's short tail ends by 0.25 r: the old count over 0.42-0.6 r, where the writing is, read a number window and
 * the tail behind the hub as the needle, 180 degrees off */
static int needle_score(const Frame *F, float cx, float cy, float r, float face, int a) {
  float t = a * 3.14159265f / 180;
  bool hub = false;
  for (int k = 0; k < 5 && !hub; k++) {
    float rr = r * (0.13f + 0.025f * k);
    float l = luma(px_at(F, (int)floorf(cx + sinf(t) * rr), (int)floorf(cy - cosf(t) * rr)));
    hub = l > 110 && l - face > 60;
  }
  if (!hub) return 0;
  int s = 0;
  for (int k = 0; k < 8; k++) {
    float rr = r * (0.58f + 0.025f * k);
    float l = luma(px_at(F, (int)floorf(cx + sinf(t) * rr), (int)floorf(cy - cosf(t) * rr)));
    s += l > 110 && l - face > 60;
  }
  return s;
}

bool see_needle(const Frame *F, float cx, float cy, float r, float *deg, float *quality) {
  static float L[360 * 2];
  int n = 0;
  for (int a = 0; a < 360; a++) {
    float t = a * 3.14159265f / 180;
    L[n++] = luma(px_at(F, (int)(cx + sinf(t) * r * 0.52f), (int)(cy - cosf(t) * r * 0.52f)));
  }
  qsort(L, (size_t)n, sizeof *L, cmpf);
  float face = L[n / 2];
  static int sc[360];
  int best = -1, bs = 0;
  for (int a = 0; a < 360; a++) {
    sc[a] = needle_score(F, cx, cy, r, face, a);
    if (sc[a] > bs) bs = sc[a], best = a;
  }
  if (best < 0 || bs < 5) return false;
  /* the middle of the run of angles that score as well */
  int lo = best, hi = best;
  while (lo - 1 > best - 20 && sc[(lo - 1 + 360) % 360] >= bs) lo--;
  while (hi + 1 < best + 20 && sc[(hi + 1) % 360] >= bs) hi++;
  *deg = fmodf((lo + hi) / 2.0f + 360, 360);
  *quality = bs / 8.0f;
  return true;
}

/* ---- things in the world ---- */
static uint32_t ring_median(const Frame *F, float x, float y, float r0, float r1) {
  static int RR[512], GG[512], BB[512];
  int n = 0;
  for (int i = 0; i < 64; i++) {
    float t = i * 6.2831853f / 64;
    for (int k = 0; k < 4 && n < 512; k++) {
      float rr = r0 + (r1 - r0) * k / 3;
      int xx = (int)(x + cosf(t) * rr), yy = (int)(y + sinf(t) * rr);
      if (xx < 0 || yy < 0 || xx >= F->w || yy >= F->h) continue;
      uint32_t c = px_at(F, xx, yy);
      RR[n] = R_(c), GG[n] = G_(c), BB[n] = B_(c), n++;
    }
  }
  if (!n) return 0;
  /* per channel medians (nth by counting is enough for 0..255) */
  int out[3];
  int *ch[3] = { RR, GG, BB };
  for (int c = 0; c < 3; c++) {
    int hist[256] = { 0 }, acc = 0, v = 0;
    for (int i = 0; i < n; i++) hist[ch[c][i]]++;
    while (v < 255 && acc + hist[v] <= n / 2) acc += hist[v++];
    out[c] = v;
  }
  return 0xFF000000u | (uint32_t)out[0] << 16 | (uint32_t)out[1] << 8 | (uint32_t)out[2];
}

float see_contrast(const Frame *F, float x, float y, float r) {
  if (r < 1.5f) r = 1.5f;
  uint32_t bg = ring_median(F, x, y, r * 2.5f, r * 4);
  int n = 0, d = 0;
  for (int yy = (int)(y - r); yy <= (int)(y + r); yy++)
    for (int xx = (int)(x - r); xx <= (int)(x + r); xx++) {
      if ((xx - x) * (xx - x) + (yy - y) * (yy - y) > r * r || xx < 0 || yy < 0 || xx >= F->w || yy >= F->h) continue;
      n++;
      d += cdist(px_at(F, xx, yy), bg) > 60;
    }
  return n ? (float)d / n : 0;
}

float see_contrast_near(const Frame *F, float x, float y, float r, float near) {
  /* the background ring stays round the given point, past the search, so the object cannot be its own background */
  if (r < 1.5f) r = 1.5f;
  uint32_t bg = ring_median(F, x, y, r + near + 3, r * 2.5f + near + 6);
  float best = 0, st = fmaxf(1, r * 0.5f);
  for (float dy = -near; dy <= near; dy += st)
    for (float dx = -near; dx <= near; dx += st) {
      if (dx * dx + dy * dy > near * near) continue;
      int n = 0, d = 0;
      float cx = x + dx, cy = y + dy;
      for (int yy = (int)(cy - r); yy <= (int)(cy + r); yy++)
        for (int xx = (int)(cx - r); xx <= (int)(cx + r); xx++) {
          if ((xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) > r * r || xx < 0 || yy < 0 || xx >= F->w || yy >= F->h) continue;
          n++;
          d += cdist(px_at(F, xx, yy), bg) > 60;
        }
      if (n && (float)d / n > best) best = (float)d / n;
    }
  return best;
}

static bool fire_col(uint32_t c) { return R_(c) > 190 && G_(c) > 70 && B_(c) < 140 && R_(c) - B_(c) > 90; }

/* a flat colour shared with three or more of its eight neighbours: a HUD symbol or letter (the target arrow, the
 * lock diamond and LOCK are fire-coloured), not shaded fire */
static bool flat(const Frame *F, int x, int y) {
  uint32_t c = px_at(F, x, y);
  int same = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) same += (dx || dy) && px_at(F, x + dx, y + dy) == c;
  return same >= 3;
}

MissileLook see_missile(const Frame *F, float x, float y, float bx, float by, float finpx, float lenpx, float sizepx) {
  MissileLook L = { 0 };
  L.bodyr = fmaxf(2, sizepx);
  /* the body: a blob the size of its fins standing out, or its length along the tail (a thin line seen from the
   * side fills little of any round patch) standing out from the picture beside it */
  float fr = fmaxf(2, finpx);
  L.body = see_contrast_near(F, x, y, fr, 8.0f * F->h / 720) > 0.2f;
  float dx = bx - x, dy = by - y, len = sqrtf(dx * dx + dy * dy);
  if (!L.body && lenpx >= 4 && len >= 1) {
    float ux = dx / len, uy = dy / len, off = fmaxf(3, fr * 1.5f);
    int hit = 0, n = 0;
    for (int k = 0; k <= 8; k++) {
      float qx = x + ux * lenpx * k / 8, qy = y + uy * lenpx * k / 8;
      uint32_t c = px_at(F, (int)qx, (int)qy), s1 = px_at(F, (int)(qx - uy * off), (int)(qy + ux * off)),
               s2 = px_at(F, (int)(qx + uy * off), (int)(qy - ux * off));
      n++;
      hit += cdist(c, s1) > 40 && cdist(c, s2) > 40;
    }
    L.body = hit * 2 >= n;
  }
  /* the trail: points along the way back, each against the picture beside the path */
  L.trail = -1; /* not judged: the tail points at the eye, too short on screen to show a trail */
  if (len >= 12) {
    float nx = -dy / len, ny = dx / len, off = fmaxf(4, len * 0.35f);
    int hit = 0, n = 0;
    for (int k = 2; k <= 12; k++) {
      float t = k / 12.0f, qx = x + dx * t, qy = y + dy * t;
      uint32_t c = px_at(F, (int)qx, (int)qy), s1 = px_at(F, (int)(qx + nx * off), (int)(qy + ny * off)),
               s2 = px_at(F, (int)(qx - nx * off), (int)(qy - ny * off));
      n++;
      hit += cdist(c, s1) > 40 && cdist(c, s2) > 40;
    }
    L.trail = n ? (float)hit / n : 0;
  }
  /* a fireball: fire-coloured pixels around the head */
  int fire = 0;
  float R = fmaxf(12, L.bodyr * 8);
  for (int yy = (int)(y - R); yy <= (int)(y + R); yy++)
    for (int xx = (int)(x - R); xx <= (int)(x + R); xx++)
      if ((xx - x) * (xx - x) + (yy - y) * (yy - y) <= R * R && fire_col(px_at(F, xx, yy)) && !flat(F, xx, yy)) fire++;
  L.fireball = fire ? sqrtf(fire / 3.14159265f) : 0;
  return L;
}

bool see_blank(const Frame *F) {
  if (!F->px || F->w <= 0 || F->h <= 0) return true; /* no picture at all */
  uint32_t c0 = F->px[0];
  for (int i = 0; i < 4000; i++) {
    size_t k = (size_t)((uint64_t)i * 2654435761u % ((uint64_t)F->w * F->h));
    if (cdist(F->px[k], c0) > 24) return false;
  }
  return true;
}

double see_diff(const Frame *A, const Frame *B, int *bx0, int *by0, int *bx1, int *by1) {
  if (A->w != B->w || A->h != B->h) {
    *bx0 = *by0 = 0, *bx1 = A->w, *by1 = A->h;
    return 1;
  }
  size_t n = 0;
  int x0 = A->w, y0 = A->h, x1 = -1, y1 = -1;
  for (int y = 0; y < A->h; y++)
    for (int x = 0; x < A->w; x++) {
      size_t i = (size_t)y * A->w + x;
      if (cdist(A->px[i], B->px[i]) <= 12) continue;
      n++;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  *bx0 = x0, *by0 = y0, *bx1 = x1 + 1, *by1 = y1 + 1;
  return (double)n / ((double)A->w * A->h);
}

/* ---- pictures for the report ---- */
static void put32(FILE *f, uint32_t v) {
  unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
  fwrite(b, 1, 4, f);
}
void save_bmp_box(const char *path, const Frame *F, int x0, int y0, int x1, int y1, int zoom) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > F->w) x1 = F->w;
  if (y1 > F->h) y1 = F->h;
  if (zoom < 1) zoom = 1;
  int w = (x1 - x0) * zoom, h = (y1 - y0) * zoom;
  if (w <= 0 || h <= 0) return;
  FILE *f = fopen(path, "wb");
  if (!f) return;
  int stride = (w * 3 + 3) & ~3;
  fwrite("BM", 1, 2, f);
  put32(f, (uint32_t)(54 + stride * h));
  put32(f, 0);
  put32(f, 54);
  put32(f, 40);
  put32(f, (uint32_t)w);
  put32(f, (uint32_t)h);
  put32(f, 1 | 24u << 16);
  put32(f, 0);
  put32(f, (uint32_t)(stride * h));
  put32(f, 2835);
  put32(f, 2835);
  put32(f, 0);
  put32(f, 0);
  unsigned char *row = calloc(1, (size_t)stride);
  if (!row) {
    fclose(f);
    return;
  }
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      uint32_t c = px_at(F, x0 + x / zoom, y0 + y / zoom);
      row[x * 3] = (unsigned char)B_(c), row[x * 3 + 1] = (unsigned char)G_(c), row[x * 3 + 2] = (unsigned char)R_(c);
    }
    fwrite(row, 1, (size_t)stride, f);
  }
  free(row);
  fclose(f);
}
void save_bmp(const char *path, const Frame *F) { save_bmp_box(path, F, 0, 0, F->w, F->h, 1); }
