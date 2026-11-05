/* livepilot.c - the live test player: a player that sees only the picture the game draws and plays with the
 * keyboard and mouse, running the hands-on checklist (docs/TEST_GUIDE.md section 5) from the screen, then the
 * cockpit items: every key, the weapons, the HUD, the panel's numbers and needles, the horizon, and what can be
 * seen of jets, missiles and rounds. Original code for this project; C11 and POSIX, no other code.
 *
 * usage: livepilot --game EXE [--out DIR] [--only LIST] [--window WxH] [--renderer modern|retro]
 *                  [--jets all|ID,...] [--seconds S] [--net] [--seed N]
 *
 * Every item ends PASS, FAIL, SKIP or NOTE with what was seen; pictures of every failure go to DIR/shots and the
 * report to DIR/report.md. Exits 1 when an item failed. See docs/LIVE_PILOT.md. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lp.h"
#include "core/safe.h"

char        OUT[512] = "build/livepilot";
static Run  RUN;
static int  WIN_W = 1280, WIN_H = 720, fails, shots;
static FILE *rep;
static unsigned seed = 7;
static double seconds_per_jet = 150;

static const char *JET_IDS[] = { "af40", "lf27", "ecf90", "ja46", "sy26", "vk33", "ks55", "nf24" };
#define NJ 8

/* ---- the report ---- */
static char  body[1 << 20];
static size_t bodyn;
static char  summary[1 << 16];
static size_t sumn;

void report_line(const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  int n = vsnprintf(body + bodyn, sizeof body - bodyn, fmt, a);
  va_end(a);
  if (n > 0 && bodyn + (size_t)n < sizeof body) bodyn += (size_t)n;
}

void report_item(const char *id, const char *title, Verdict v, const char *fmt, ...) {
  static const char *const VN[] = { "PASS", "FAIL", "SKIP", "NOTE" };
  static char ev[1 << 15]; /* a key table runs long */
  va_list a;
  va_start(a, fmt);
  vsnprintf(ev, sizeof ev, fmt, a);
  va_end(a);
  if (v == V_FAIL) fails++;
  int n = snprintf(summary + sumn, sizeof summary - sumn, "| %s | %s | **%s** |\n", id, title, VN[v]);
  if (n > 0 && sumn + (size_t)n < sizeof summary) sumn += (size_t)n;
  report_line("\n### %s %s: %s\n\n%s\n", id, title, VN[v], ev);
  printf("%-5s %-4s %s\n", id, VN[v], title);
  fflush(stdout);
}

static const char *shot(const Frame *F, const char *what, int x0, int y0, int x1, int y1) {
  static char rel[160];
  char path[700];
  if (shots >= 400) return "(no more pictures saved)";
  snprintf(rel, sizeof rel, "shots/%03d-%s.bmp", ++shots, what);
  snprintf(path, sizeof path, "%s/%s", OUT, rel);
  if (x1 <= x0 || y1 <= y0) save_bmp(path, F);
  else {
    int m = 24, zoom = (x1 - x0) < 200 ? 3 : 1;
    save_bmp_box(path, F, x0 - m, y0 - m, x1 + m, y1 + m, zoom);
  }
  return rel;
}

/* ---- small helpers ---- */
/* '#' a digit, '?' any one character, '*' any run */
static bool match(const char *s, const char *p) {
  if (!*p) return !*s;
  if (*p == '*') return match(s, p + 1) || (*s && match(s + 1, p));
  if (!*s) return false;
  if (*p == '#') return isdigit((unsigned char)*s) && match(s + 1, p + 1);
  if (*p == '?' || *p == *s) return match(s + 1, p + 1);
  return false;
}
static void norm(const char *in, char *out, size_t n) { /* as the font draws it: upper case, unknowns '?' */
  size_t k = 0;
  for (const unsigned char *s = (const unsigned char *)in; *s && k + 1 < n; s++) {
    unsigned c = *s;
    if (c >= 0x80) {
      while ((s[1] & 0xC0) == 0x80) s++;
      c = '?';
    }
    out[k++] = (char)toupper((int)c);
  }
  out[k] = 0;
}
static void pattern_of(const char *s, char *out, size_t n) { /* numbers as '#' */
  size_t k = 0;
  for (; *s && k + 1 < n; s++) out[k++] = isdigit((unsigned char)*s) ? '#' : *s;
  out[k] = 0;
}
static bool contains_words(const char *hay, const char *needle) { /* needle inside hay, spaces ignored */
  char a[128], b[128];
  size_t i = 0, j = 0;
  for (; *hay && i < 127; hay++)
    if (*hay != ' ') a[i++] = *hay;
  for (; *needle && j < 127; needle++)
    if (*needle != ' ') b[j++] = *needle;
  a[i] = b[j] = 0;
  if (!j) return false;
  for (size_t k = 0; k + j <= i; k++) {
    size_t m = 0;
    while (m < j && (b[m] == '?' || b[m] == a[k + m])) m++;
    if (m == j) return true;
  }
  return false;
}
static float wrap180(float d) {
  d = fmodf(d + 540, 360) - 180;
  return d;
}

static Read RD[2048];
static int  NRD;
static void read_screen(const Frame *F) { NRD = see_texts(F, RD, 2048); }
static const Read *find_read(const char *pat) {
  for (int i = 0; i < NRD; i++)
    if (match(RD[i].s, pat)) return &RD[i];
  return NULL;
}
static const Read *find_read_has(const char *word) {
  for (int i = 0; i < NRD; i++)
    if (contains_words(RD[i].s, word)) return &RD[i];
  return NULL;
}
/* picture pixels to the window's, for clicks */
static float wx(const Frame *F, float x) { return x * WIN_W / F->w; }
static float wy(const Frame *F, float y) { return y * WIN_H / F->h; }

static void args_add(const char **a, int *n, const char *x) { a[(*n)++] = x, a[*n] = NULL; }

/* steps the game n pictures, reading each; false if it went away */
static bool step(Game *G, Frame *F, int n) {
  for (int i = 0; i < n; i++) {
    game_go(G);
    if (!game_read(G, F, 30000)) return false;
  }
  return true;
}

/* ---- the text audit: everything drawn should read back from the picture ---- */
typedef struct {
  char  pat[48];
  int   drawn, read, tiny, overlap, offpic, lowc;
  float minc;
  char  ex[160], exread[64];
} TextStat;
static TextStat TS[400];
static int      NTS;
static int      phantom;
static char     phantom_ex[1024]; /* a few of them, for the report */

static TextStat *ts_of(const char *s) {
  char p[48];
  pattern_of(s, p, sizeof p);
  for (int i = 0; i < NTS; i++)
    if (!strcmp(TS[i].pat, p)) return &TS[i];
  if (NTS >= 400) return NULL;
  TextStat *t = &TS[NTS++];
  memset(t, 0, sizeof *t);
  snprintf(t->pat, sizeof t->pat, "%s", p);
  t->minc = 99;
  return t;
}

/* a drawn text (picture pixels) read back: a line read over the same place that holds the same letters, or the
 * lines read along it put together (a long line with wide gaps reads as several); best: the line read there */
static bool read_back(const Frame *F, int x0, int y0, int x1, int y1, const char *s, const Read **bestp) {
  const Read *best = NULL;
  for (int k = 0; k < NRD; k++) {
    const Read *r = &RD[k];
    if (r->x1 < x0 - 2 || r->x0 > x1 + 2 || r->y1 < y0 - 2 || r->y0 > y1 + 2) continue;
    best = r;
    if (contains_words(r->s, s)) {
      *bestp = r;
      return true;
    }
  }
  *bestp = best;
  /* a letter or two alone: read where it was drawn, cell by cell (the line reader takes a lone letter only when it
   * stands clear of everything, which a '%' on a dial's face never does) */
  if (strlen(s) <= 2) {
    static Read cellread; /* the box as read, for its contrast */
    char got[8];
    if (see_cells(F, x0, y0, x1, y1, got, sizeof got) && contains_words(got, s)) {
      memset(&cellread, 0, sizeof cellread);
      cellread.x0 = x0, cellread.y0 = y0, cellread.x1 = x1, cellread.y1 = y1, cellread.scale = (int)lroundf((y1 - y0) / 7.0f);
      cellread.col = see_cells_ink();
      snprintf(cellread.s, sizeof cellread.s, "%s", got);
      *bestp = &cellread;
      return true;
    }
  }
  if (!best) return false;
  /* the lines read along it put together, for each line of letters found over the box (the last one found was the
   * only one tried: a speck of scenery read as "H ?" in small letters over the box hid the line itself) */
  for (int a = 0; a < NRD; a++) {
    const Read *anchor = &RD[a];
    if (anchor->x1 < x0 - 2 || anchor->x0 > x1 + 2 || anchor->y1 < y0 - 2 || anchor->y0 > y1 + 2) continue;
    char joined[256] = "";
    int at = -100000;
    for (int pass = 0; pass < 64; pass++) {
      const Read *next = NULL;
      for (int k = 0; k < NRD; k++) {
        const Read *r = &RD[k];
        if (r->x1 < x0 - 2 || r->x0 > x1 + 2 || r->y1 < y0 - 2 || r->y0 > y1 + 2 || abs(r->y0 - anchor->y0) > 1 ||
            r->scale != anchor->scale || r->x0 <= at)
          continue;
        if (!next || r->x0 < next->x0) next = r;
      }
      if (!next) break;
      at = next->x0;
      size_t l = strlen(joined);
      snprintf(joined + l, sizeof joined - l, "%s%s", l ? " " : "", next->s);
    }
    if (contains_words(joined, s)) {
      *bestp = anchor;
      return true;
    }
  }
  return false;
}

/* a dial's needle across the text's box (fb pixels) this frame: a title on the face under the needle, as on a real
 * gauge; not a text the game failed to draw */
static int under_needle;
static bool needle_over(const Frame *F, const SheetText *t) {
  for (int i = 0; i < F->nd; i++) {
    const SheetDial *d = &F->d[i];
    float v = d->hi > d->lo ? (d->v - d->lo) / (d->hi - d->lo) : 0;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    float a = (d->a0 + (d->a1 - d->a0) * v) * 3.14159265f / 180;
    /* the needle from the end of its tail behind the hub (a quarter of the radius) to its tip */
    for (int k = -5; k <= 20; k++) {
      float rr = d->r * k / 20.0f, x = d->cx + sinf(a) * rr, y = d->cy - cosf(a) * rr;
      if (x >= t->x0 - 1 && x <= t->x1 + 1 && y >= t->y0 - 1 && y <= t->y1 + 1) return true;
    }
    /* and a second needle (the Soviet altimeter's thousands: it showed through the "-" of "ВД-20"), from the hub */
    if (d->len2 > 0) {
      float v2 = d->hi > d->lo ? (d->v2 - d->lo) / (d->hi - d->lo) : 0;
      v2 = v2 < 0 ? 0 : v2 > 1 ? 1 : v2;
      float a2 = (d->a0 + (d->a1 - d->a0) * v2) * 3.14159265f / 180;
      for (int k = 0; k <= 20; k++) {
        float rr = d->r * d->len2 * k / 20.0f, x = d->cx + sinf(a2) * rr, y = d->cy - cosf(a2) * rr;
        if (x >= t->x0 - 1 && x <= t->x1 + 1 && y >= t->y0 - 1 && y <= t->y1 + 1) return true;
      }
    }
  }
  return false;
}

static void audit_texts(const Frame *F, const char *where) {
  /* on the pause page the match behind is dimmed on purpose: only the page's own texts, drawn after its title */
  int first = 0;
  if (!strcmp(where, "pause"))
    for (int i = 0; i < F->nt; i++)
      if (!strcmp(F->t[i].s, "PAUSED")) first = i;
  for (int i = first; i < F->nt; i++) {
    const SheetText *t = &F->t[i];
    char s[64];
    norm(t->s, s, sizeof s);
    if (!s[0] || !strcmp(s, " ")) continue;
    TextStat *st = ts_of(s);
    if (!st) continue;
    if (needle_over(F, t)) {
      under_needle++;
      continue;
    }
    st->drawn++;
    int x0 = (int)fx(F, (float)t->x0), y0 = (int)fy(F, (float)t->y0), x1 = (int)fx(F, (float)t->x1), y1 = (int)fy(F, (float)t->y1);
    float screen_h = (float)(t->y1 - t->y0) * WIN_H / F->fbh;
    if (screen_h < 6.5f) st->tiny++;
    if (x0 < 0 || y0 < 0 || x1 > F->w || y1 > F->h) st->offpic++;
    const Read *best = NULL;
    bool ok = read_back(F, x0, y0, x1, y1, s, &best);
    if (ok) {
      float c = see_luma_contrast(F, best->x0, best->y0, best->x1, best->y1, best->col);
      if (c < st->minc) st->minc = c;
      if (c < 3) st->lowc++;
    }
    if (ok) st->read++;
    else if (!st->ex[0]) {
      char what[40];
      snprintf(what, sizeof what, "text-%s", where);
      snprintf(st->ex, sizeof st->ex, "%s", shot(F, what, x0, y0, x1, y1));
      snprintf(st->exread, sizeof st->exread, "%s", best ? best->s : "");
    }
    for (int j = i + 1; j < F->nt; j++) {
      const SheetText *u = &F->t[j];
      int ox = (t->x1 < u->x1 ? t->x1 : u->x1) - (t->x0 > u->x0 ? t->x0 : u->x0);
      int oy = (t->y1 < u->y1 ? t->y1 : u->y1) - (t->y0 > u->y0 ? t->y0 : u->y0);
      if (ox > 1 && oy > 1 && strcmp(t->s, u->s)) {
        int a = (t->x1 - t->x0) * (t->y1 - t->y0), b = (u->x1 - u->x0) * (u->y1 - u->y0);
        if (ox * oy * 5 > (a < b ? a : b)) st->overlap++;
      }
    }
  }
  /* lines read that the game did not write: something in the picture that looks like text */
  for (int k = 0; k < NRD; k++) {
    bool drawn = false;
    for (int i = 0; i < F->nt && !drawn; i++) {
      const SheetText *t = &F->t[i];
      drawn = !(RD[k].x1 < fx(F, (float)t->x0) - 2 || RD[k].x0 > fx(F, (float)t->x1) + 2 || RD[k].y1 < fy(F, (float)t->y0) - 2 ||
                RD[k].y0 > fy(F, (float)t->y1) + 2);
    }
    if (!drawn && strlen(RD[k].s) >= 3) {
      phantom++;
      char e[128];
      snprintf(e, sizeof e, "`%.63s` (%.40s)", RD[k].s, where);
      if (!strstr(phantom_ex, e) && strlen(phantom_ex) + strlen(e) + 3 < sizeof phantom_ex)
        snprintf(phantom_ex + strlen(phantom_ex), sizeof phantom_ex - strlen(phantom_ex), "%s%s", phantom_ex[0] ? ", " : "", e);
    }
  }
}

static void report_texts(const char *id, const char *title, float min_contrast) {
  int bad = 0, total = 0;
  char b[1 << 15];
  size_t n = 0;
  n += (size_t)snprintf(b + n, sizeof b - n, "| text | drawn | read back | under 7 px | overlapped | contrast below %.0f:1 | lowest | example |\n|---|---|---|---|---|---|---|---|\n", min_contrast);
  for (int i = 0; i < NTS; i++) {
    TextStat *t = &TS[i];
    total++;
    bool weak = t->read < t->drawn * 0.9 || t->tiny || t->offpic || t->overlap > t->drawn / 10 || t->lowc > t->read / 5;
    if (!weak) continue;
    bad++;
    if (n < sizeof b - 400)
      n += (size_t)snprintf(b + n, sizeof b - n, "| `%s` | %d | %d | %d | %d | %d | %.1f | %s%s%s%s%s%s |\n", t->pat, t->drawn,
                            t->read, t->tiny, t->overlap, t->lowc, t->minc > 50 ? 0 : t->minc, t->ex[0] ? "[picture](" : "",
                            t->ex, t->ex[0] ? ")" : "", t->exread[0] ? " read as `" : "", t->exread, t->exread[0] ? "`" : "");
  }
  report_item(id, title, bad ? V_FAIL : V_PASS,
              "%d different texts on screen (numbers counted as one), %d of them with a problem; %d other lines read "
              "that the game did not write%s%s; %d texts on a dial's face with its needle across them not counted.\n\n%s",
              total, bad, phantom, phantom ? ", such as " : "", phantom_ex, under_needle,
              bad ? b : "Every text read back from the picture, at 7 px or more, clear of the others.");
  NTS = 0;
  under_needle = 0;
  phantom = 0;
  phantom_ex[0] = 0;
}

static const char *pct(int a, int b);

/* ---- the stores: rounds, missiles, flares and chaff as the panel shows them, against what is left ---- */
typedef struct {
  int  n, ok, unread; char ex[200];
  char what[8][32]; int wn[8], wok[8], nw;
  int  pip_n, pip_ok; char pip_ex[200];
} Stores;

static void stores_count(Stores *S, const char *what, bool ok) {
  int i;
  for (i = 0; i < S->nw && strcmp(S->what[i], what); i++) {}
  if (i == S->nw && S->nw < 8) snprintf(S->what[S->nw++], sizeof S->what[0], "%s", what);
  if (i < 8) S->wn[i]++, S->wok[i] += ok;
}

/* which store a label in the panel names (the game's own words, in every cockpit's language) */
static int store_of(const char *label, const Frame *F, const char **name) {
  static const struct { const char *l; int q; } L[] = {
    { "GUN", 0 }, { "AKAN", 0 }, { "ПУШКА", 0 }, { "CANNON", 0 }, { "C", 0 }, { "FLR", 4 }, { "FACKL", 4 }, { "ЛТЦ", 4 },
    { "FLARES", 4 }, { "CHAFF", 5 }, { "HEAT", 1 }, { "H", 1 }, { "RADAR", 2 }, { "R", 2 }, { "GROUND", 3 }, { "S", 3 },
  };
  static const char *const N[] = { "rounds", "heat missiles", "radar missiles", "ground missiles", "flares", "chaff" };
  for (size_t i = 0; i < sizeof L / sizeof L[0]; i++)
    if (!strcmp(label, L[i].l) && (F->classic || strlen(L[i].l) > 1)) { /* C, H, R, S: the C64 panel's only (C is a course elsewhere) */
      *name = N[L[i].q];
      return L[i].q;
    }
  return -1;
}
static int store_value(const Frame *F, int q) {
  return q == 0 ? F->q_ammo : q == 1 ? F->q_heat : q == 2 ? F->q_radar : q == 3 ? F->q_ground : q == 4 ? F->q_flares : F->q_chaff;
}

static void check_stores(const Frame *F, Stores *S) {
  if (!F->has_q || !F->has_p || !F->alive || strcmp(F->state, "play")) return;
  float panel_top = F->has_v ? F->vy + F->sh - 1 : 0;
  for (int i = 0; i < F->nt; i++) {
    const SheetText *t = &F->t[i];
    if (t->y0 < panel_top) continue;
    if (F->has_v && (t->x1 <= F->vx || t->x0 >= F->vx + F->vw)) continue; /* the other player's panel, side by side */
    /* label-number pairs in the drawn text: which store each number in it stands for */
    char buf[64];
    snprintf(buf, sizeof buf, "%s", t->s);
    int want[8], nwant = 0, ord = 0, pos[8];
    char *save = NULL, *prev = NULL;
    for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
      bool num = isdigit((unsigned char)tok[0]);
      if (num) {
        const char *nm;
        int q = prev ? store_of(prev, F, &nm) : -1;
        if (q >= 0 && nwant < 8) want[nwant] = q, pos[nwant] = ord, nwant++;
        ord++;
      }
      prev = tok;
    }
    if (!nwant) continue;
    /* the numbers the picture shows there */
    int x0 = (int)fx(F, (float)t->x0), y0 = (int)fy(F, (float)t->y0), x1 = (int)fx(F, (float)t->x1), y1 = (int)fy(F, (float)t->y1);
    /* the line read there: the one overlapping the drawn text most (on a small panel the next line is close) */
    const Read *r = NULL;
    int best = 0;
    for (int k = 0; k < NRD; k++) {
      int ox = (RD[k].x1 < x1 ? RD[k].x1 : x1) - (RD[k].x0 > x0 ? RD[k].x0 : x0);
      int oy = (RD[k].y1 < y1 ? RD[k].y1 : y1) - (RD[k].y0 > y0 ? RD[k].y0 : y0);
      if (ox > 0 && oy > 0 && ox * oy > best) best = ox * oy, r = &RD[k];
    }
    int seen[8], ns = 0;
    if (r)
      for (const char *c = r->s; *c && ns < 8;) {
        if (isdigit((unsigned char)*c)) {
          seen[ns++] = (int)safe_int(c, 0, INT_MIN, INT_MAX);
          while (isdigit((unsigned char)*c)) c++;
        } else c++;
      }
    for (int w = 0; w < nwant; w++) {
      const char *nm;
      char lab[16] = "";
      static const char *const N[] = { "rounds", "heat missiles", "radar missiles", "ground missiles", "flares", "chaff" };
      nm = N[want[w]];
      (void)lab;
      int real = store_value(F, want[w]);
      S->n++;
      if (!r || pos[w] >= ns) {
        S->unread++;
        stores_count(S, nm, false);
        continue;
      }
      bool ok = seen[pos[w]] == real;
      S->ok += ok;
      stores_count(S, nm, ok);
      if (!ok && !S->ex[0])
        snprintf(S->ex, sizeof S->ex, "%s (%s: the panel reads %d, %d left)", shot(F, "stores", x0, y0, x1, y1), nm, seen[pos[w]], real);
    }
  }
  /* the missile pips: lit ones counted from the picture */
  for (int i = 0; i < F->nk; i++) {
    const SheetPips *k = &F->k[i];
    if (F->has_v && (k->x + k->max * k->step <= F->vx || k->x >= F->vx + F->vw)) continue; /* the other player's */
    int lit = 0;
    for (int j = 0; j < k->max; j++) {
      uint32_t c = px_at(F, (int)fx(F, k->x + j * k->step + k->w / 2), (int)fy(F, k->y + k->h / 2));
      int don = abs((int)(c >> 16 & 255) - (int)(k->on >> 16 & 255)) + abs((int)(c >> 8 & 255) - (int)(k->on >> 8 & 255)) +
                abs((int)(c & 255) - (int)(k->on & 255));
      int doff = abs((int)(c >> 16 & 255) - (int)(k->off >> 16 & 255)) + abs((int)(c >> 8 & 255) - (int)(k->off >> 8 & 255)) +
                 abs((int)(c & 255) - (int)(k->off & 255));
      lit += don < doff;
    }
    /* each kind's marks against what is left of that kind */
    static const char *const KN[3] = { "heat missile marks", "radar missile marks", "ground missile marks" };
    int kind = k->kind < 0 || k->kind > 2 ? 0 : k->kind, left = kind == 0 ? F->q_heat : kind == 1 ? F->q_radar : F->q_ground;
    S->pip_n++;
    bool ok = lit == left;
    S->pip_ok += ok;
    stores_count(S, KN[kind], ok);
    if (!ok && !S->pip_ex[0])
      snprintf(S->pip_ex, sizeof S->pip_ex, "%s (%d %s lit, %d left)",
               shot(F, "missile-marks", (int)fx(F, k->x), (int)fy(F, k->y), (int)fx(F, k->x + k->max * k->step), (int)fy(F, k->y + k->h)),
               lit, KN[kind], left);
  }
}

static void stores_table(char *b, size_t n, const Stores *S) {
  size_t u = 0;
  u += (size_t)snprintf(b + u, n - u, "| store shown | right | |\n|---|---|---|\n");
  for (int i = 0; i < S->nw && u < n - 200; i++)
    u += (size_t)snprintf(b + u, n - u, "| %s%s | %s | |\n", S->what[i], strstr(S->what[i], "marks") ? " lit" : " in the panel's text",
                          pct(S->wok[i], S->wn[i]));
  if (u < n - 400)
    u += (size_t)snprintf(b + u, n - u, "| all store numbers | %s, %d not readable | %s |\n| missile marks lit | %s | %s |\n",
                          pct(S->ok, S->n), S->unread, S->ex, pct(S->pip_ok, S->pip_n), S->pip_ex);
}
static bool stores_bad(const Stores *S) { return (S->n && S->ok < S->n) || (S->pip_n && S->pip_ok < S->pip_n); }

/* ---- the cockpit checks over one flight ---- */
typedef struct {
  long pics, cockpit;
  int  hdg_n, hdg_ok; float hdg_worst; char hdg_ex[160];
  int  g_n, g_ok; float g_worst; char g_ex[160];
  int  km_n, km_ok; char km_ex[160];
  int  hor_n, hor_found, hor_ok; float hor_worst; char hor_ex[160];
  int  dial_n, dial_found, dial_ok, dial_hidden; float dial_worst; char dial_ex[160], dial_title[16];
  int  foe_n, foe_seen, foe_cloud, box_n, box_label; char foe_ex[160];
  int  msl_n, msl_body, msl_trail, msl_trail_n, msl_fire, msl_hidden; char msl_ex[160], fire_ex[160], trail_ex[200];
  int  launches, launch_seen;
  int  rnd_n, rnd_seen;
  int  lock_n, lock_read; char lock_ex[160];
  int  spd_n, spd_ok, alt_n, alt_ok; char spd_ex[200], alt_ex[200];
  int  presses[8], fired[8];
  Stores st;
} Cock;

static struct { unsigned id; bool seen; int pics; } LAUNCH[256];
static int NL;

/* how far from where the sheet puts a jet, missile or round it may be drawn: the picture shows the world a
 * moment apart from the state the sheet gives (about 8 px at 720 lines) */
static float near_px(const Frame *F) { return 8.0f * F->h / 720; }

/* blacked out round (x, y), the G vignette's tunnel: no eye sees anything there, nothing to judge */
static bool blacked(const Frame *F, float x, float y) {
  int dark = 0;
  float r = 14.0f * F->h / 720;
  for (int k = 0; k < 8; k++) {
    uint32_t c = px_at(F, (int)(x + r * cosf(k * 0.785f)), (int)(y + r * sinf(k * 0.785f)));
    dark += ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) < 90;
  }
  return dark >= 6;
}

/* the segment from (x, y) toward (bx, by) cut where it leaves the box: the part of a missile's tail on screen */
static void clip_tail(float x, float y, float *bx, float *by, float x0, float y0, float x1, float y1) {
  float t = 1, dx = *bx - x, dy = *by - y;
  if (dx < 0 && x + dx < x0) t = fminf(t, (x0 - x) / dx);
  if (dx > 0 && x + dx > x1) t = fminf(t, (x1 - x) / dx);
  if (dy < 0 && y + dy < y0) t = fminf(t, (y0 - y) / dy);
  if (dy > 0 && y + dy > y1) t = fminf(t, (y1 - y) / dy);
  t = fmaxf(0, t);
  *bx = x + dx * t, *by = y + dy * t;
}

static void check_cockpit(const Frame *F, Cock *C) {
  C->pics++;
  if (!F->has_p || !F->has_v || !F->alive || F->gloc || strcmp(F->state, "play")) return;
  if (!strcmp(F->view, "cockpit")) check_stores(F, &C->st);
  bool cockpit = !strcmp(F->view, "cockpit");
  if (cockpit) C->cockpit++;
  float vx0 = fx(F, F->vx), vy0 = fy(F, F->vy), vx1 = fx(F, F->vx + F->vw), vy1 = fy(F, F->vy + F->sh);
  float cx = fx(F, F->cx);
  /* the heading under the tape: three digits, high in the middle */
  for (int i = 0; i < NRD; i++) {
    const Read *r = &RD[i];
    if (!match(r->s, "###") || fabsf((r->x0 + r->x1) / 2.0f - cx) > 12 || r->y0 > vy0 + (vy1 - vy0) * 0.25f) continue;
    float d = fabsf(wrap180((float)safe_num(r->s, 0) - F->hdg));
    C->hdg_n++;
    if (d <= 1.5f) C->hdg_ok++;
    else if (d > C->hdg_worst) {
      C->hdg_worst = d;
      snprintf(C->hdg_ex, sizeof C->hdg_ex, "%s (read %s, flying %.1f)", shot(F, "heading", r->x0, r->y0, r->x1, r->y1), r->s, F->hdg);
    }
    break;
  }
  /* the G meter */
  const Read *g = find_read("G +#.#");
  if (!g) g = find_read("G -#.#");
  if (!g) g = find_read("G +##.#");
  if (g) {
    float v = (float)safe_num(g->s + 2, 0), d = fabsf(v - F->gmeter);
    C->g_n++;
    if (d <= 0.11f) C->g_ok++;
    else if (d > C->g_worst) {
      C->g_worst = d;
      snprintf(C->g_ex, sizeof C->g_ex, "%s (read %s, gauge %.2f)", shot(F, "gmeter", g->x0, g->y0, g->x1, g->y1), g->s, F->gmeter);
    }
  }
  /* speed and height somewhere in the panel, in any of the units the panels use */
  float spd_units[3] = { F->spd * 1.943844f, F->spd * 3.6f, F->spd }, alt_units[2] = { F->alt * 3.28084f, F->alt };
  bool spd_seen = false, alt_seen = false;
  for (int i = 0; i < NRD; i++) {
    const char *s = RD[i].s;
    while (*s && !isdigit((unsigned char)*s)) s++;
    if (!*s || RD[i].y0 < vy1 - 2) continue; /* the panel is below the view */
    float v = (float)safe_num(s, 0);
    for (int k = 0; k < 3; k++) spd_seen = spd_seen || (v > 30 && fabsf(v - spd_units[k]) <= fmaxf(3, spd_units[k] * 0.02f));
    for (int k = 0; k < 2; k++) alt_seen = alt_seen || (v > 30 && fabsf(v - alt_units[k]) <= fmaxf(12, alt_units[k] * 0.02f));
  }
  /* judged only where the panel writes them as numbers (some show them on needles alone, or speed as a Mach) */
  bool spd_drawn = false, alt_drawn = false;
  int spd_t = -1, alt_t = -1;
  for (int i = 0; i < F->nt; i++) {
    bool s0 = spd_drawn, a0 = alt_drawn;
    const char *s = F->t[i].s;
    if (fy(F, (float)F->t[i].y0) < vy1 - 2) continue;
    for (; *s; s++) {
      if (!isdigit((unsigned char)*s) || (s > F->t[i].s && (isdigit((unsigned char)s[-1]) || s[-1] == '.'))) continue;
      float v = (float)safe_num(s, 0);
      for (int k = 0; k < 3; k++) spd_drawn = spd_drawn || (v > 30 && fabsf(v - spd_units[k]) <= fmaxf(3, spd_units[k] * 0.02f));
      for (int k = 0; k < 2; k++) alt_drawn = alt_drawn || (v > 30 && fabsf(v - alt_units[k]) <= fmaxf(12, alt_units[k] * 0.02f));
    }
    if (spd_drawn && !s0) spd_t = i;
    if (alt_drawn && !a0) alt_t = i;
  }
  for (int w = 0; w < 2 && cockpit; w++) {
    int ti = w ? alt_t : spd_t;
    char *ex = w ? C->alt_ex : C->spd_ex;
    if (ti < 0 || (w ? alt_seen : spd_seen) || ex[0]) continue;
    const SheetText *t = &F->t[ti];
    const Read *best;
    char nt[64];
    norm(t->s, nt, sizeof nt);
    int x0 = (int)fx(F, (float)t->x0), y0 = (int)fy(F, (float)t->y0), x1 = (int)fx(F, (float)t->x1), y1 = (int)fy(F, (float)t->y1);
    read_back(F, x0, y0, x1, y1, nt, &best);
    snprintf(ex, 200, "%s (drawn `%.40s`, read `%.40s`)", shot(F, w ? "altitude" : "airspeed", x0, y0, x1, y1), t->s, best ? best->s : "");
  }
  if (cockpit) {
    C->spd_n += spd_drawn, C->alt_n += alt_drawn;
    C->spd_ok += spd_drawn && spd_seen, C->alt_ok += alt_drawn && alt_seen;
  }
  /* the horizon in the windscreen against the jet's attitude */
  if (cockpit && fabsf(F->u[1]) > 0.35f) {
    float sx0 = F->vx, sx1 = F->vx + F->vw;
    float ey0 = F->cy + (F->r[1] * (sx0 - F->cx) + F->f[1] * F->F) / F->u[1];
    float ey1 = F->cy + (F->r[1] * (sx1 - F->cx) + F->f[1] * F->F) / F->u[1];
    float ymid = (ey0 + ey1) / 2;
    /* calm flight only: steep bank, inverted, or the G vignette leave a hazy horizon no reader can place */
    /* (the g as flown and shown, gmeter, and no grey-out: gload is the airframe's, and in arcade handling the jet
     * turned at 6 g with gload under 3, the view greyed and narrowed by hud.c's g_vision) */
    bool calm = fabsf(F->bank) < 60 && fabsf(F->pitch) < 20 && F->gload < 3 && F->gmeter < 3 && F->gstress <= 1;
    if (calm && ymid > F->vy + F->sh * 0.1f && ymid < F->vy + F->sh * 0.9f && F->alt > 300) {
      C->hor_n++;
      float a, b, q;
      /* near where the attitude puts it (a quarter of the view and 10 degrees each way): coastlines, cloud edges
       * and the G vignette make stronger edges elsewhere; a horizon drawn in the wrong place still shows as off */
      float gb = (fy(F, ey1) - fy(F, ey0)) / (fx(F, sx1) - fx(F, sx0)), ga = fy(F, ey0) - gb * fx(F, sx0);
      if (see_horizon_near(F, (int)vx0, (int)vy0, (int)vx1, (int)vy1, ga, gb, (vy1 - vy0) * 0.25f, 10, F->u[1] > 0, &a, &b, &q) &&
          q >= 0.34f) { /* a third of the columns: clearly a horizon */
        C->hor_found++;
        float eb = (fy(F, ey1) - fy(F, ey0)) / (fx(F, sx1) - fx(F, sx0)), ea = fy(F, ey0) - eb * fx(F, sx0);
        float mx = (vx0 + vx1) / 2, off = fabsf((a + b * mx) - (ea + eb * mx)) / (vy1 - vy0);
        float ang = fabsf(atanf(b) - atanf(eb)) * 57.29578f;
        if (ang <= 3 && off <= 0.04f) C->hor_ok++;
        else if (ang + off * 100 > C->hor_worst) {
          C->hor_worst = ang + off * 100;
          snprintf(C->hor_ex, sizeof C->hor_ex, "%s (%.1f degrees and %.0f %% of the view off; bank %.0f, pitch %.0f)",
                   shot(F, "horizon", 0, 0, 0, 0), ang, off * 100, F->bank, F->pitch);
        }
      }
    }
  }
  /* the dials' needles */
  for (int i = 0; i < F->nd; i++) {
    const SheetDial *d = &F->d[i];
    float r = fx(F, d->r);
    if (r < 8) continue; /* too small to read a needle */
    /* a needle passing under the dial's number window or across its title is hidden in part there, as on a real
     * gauge: judged only where it runs clear of the face's writing (the texts the game drew on the face) */
    {
      float tt = d->hi > d->lo ? (d->v - d->lo) / (d->hi - d->lo) : 0;
      tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
      float a = (d->a0 + (d->a1 - d->a0) * tt) * 3.14159265f / 180;
      bool under = false;
      for (int k = 2; k <= 18 && !under; k++) {
        float rr = d->r * k / 20.0f, x = d->cx + sinf(a) * rr, y = d->cy - cosf(a) * rr;
        for (int q = 0; q < F->nt && !under; q++)
          under = x >= F->t[q].x0 - 1 && x <= F->t[q].x1 + 1 && y >= F->t[q].y0 - 1 && y <= F->t[q].y1 + 1;
      }
      if (under) {
        C->dial_hidden++;
        continue;
      }
    }
    C->dial_n++;
    float deg, q;
    if (!see_needle(F, fx(F, d->cx), fy(F, d->cy), r, &deg, &q)) continue;
    C->dial_found++;
    float t = d->hi > d->lo ? (d->v - d->lo) / (d->hi - d->lo) : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float want = d->a0 + (d->a1 - d->a0) * t, e = fabsf(wrap180(deg - want));
    if (e <= 7) C->dial_ok++;
    else if (e > C->dial_worst) {
      C->dial_worst = e;
      snprintf(C->dial_title, sizeof C->dial_title, "%s", d->title);
      snprintf(C->dial_ex, sizeof C->dial_ex, "%s (%s: needle read at %.0f degrees, should show %.1f at %.0f)",
               shot(F, "dial", (int)(fx(F, d->cx) - r), (int)(fy(F, d->cy) - r), (int)(fx(F, d->cx) + r), (int)(fy(F, d->cy) + r)),
               d->title, deg, d->v, fmodf(want + 720, 360));
    }
  }
  /* jets: seen where they are, boxed with their range */
  for (int i = 0; i < F->ne; i++) {
    const SheetJet *e = &F->e[i];
    if (!e->foe || !e->on || e->z < 60 || e->z > 3500) continue;
    float x = fx(F, e->x), y = fy(F, e->y);
    if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;
    /* blacked out round it (the G vignette): no eye sees a jet there, nothing to judge (the ring was 14 px at any
     * size; now 14 px at 720 lines, as the vignette scales with the picture) */
    if (blacked(F, x, y)) continue;
    /* behind a cloud (the sheet says the line of sight passes through a puff): rightly unseen */
    if (e->cloud) {
      C->foe_cloud++;
      continue;
    }
    C->foe_n++;
    float rpx = fmaxf(2, fx(F, 6 / e->z * F->F));
    if (see_contrast_near(F, x, y, rpx, near_px(F)) > 0.15f) C->foe_seen++;
    else if (!C->foe_ex[0])
      snprintf(C->foe_ex, sizeof C->foe_ex, "%s (an enemy %.1f km away, nothing stands out where it is)",
               shot(F, "enemy-unseen", (int)(x - 30), (int)(y - 30), (int)(x + 30), (int)(y + 30)), e->km);
  }
  for (int i = 0; i < F->nb; i++) {
    float x = fx(F, F->b[i].x), y = fy(F, F->b[i].y);
    C->box_n++;
    bool lab = false;
    for (int k = 0; k < NRD && !lab; k++) {
      const Read *r = &RD[k];
      if (!match(r->s, "#.#KM") && !match(r->s, "##.#KM")) continue;
      float lx = (r->x0 + r->x1) / 2.0f;
      if (fabsf(lx - x) < 20 * F->h / 720.0f && r->y0 > y && r->y0 < y + 120 * F->h / 720.0f) lab = fabsf((float)safe_num(r->s, 0) - F->b[i].km) <= 0.11f;
    }
    if (lab) C->box_label++;
    else if (!C->km_ex[0])
      snprintf(C->km_ex, sizeof C->km_ex, "%s (an enemy box at %.1f km, its range not read under it)",
               shot(F, "range", (int)(x - 40), (int)(y - 40), (int)(x + 40), (int)(y + 80)), F->b[i].km);
  }
  /* the lock */
  if (F->locked && F->lock) {
    float u = F->h / 720.0f; /* the window in pixels at 720 lines: the HUD's letters grow with the picture */
    C->lock_n++;
    float x = fx(F, F->lx), y = fy(F, F->ly);
    bool seen = false;
    for (int k = 0; k < NRD && !seen; k++)
      seen = !strcmp(RD[k].s, "LOCK") && fabsf((RD[k].x0 + RD[k].x1) / 2.0f - x) < 30 * u && RD[k].y1 <= y + 4 * u && RD[k].y0 > y - 80 * u;
    C->lock_read += seen;
    if (!seen && !C->lock_ex[0])
      snprintf(C->lock_ex, sizeof C->lock_ex, "%s", shot(F, "lock", (int)x - 50, (int)y - 80, (int)x + 50, (int)y + 40));
  }
  /* own missiles: a body with a smoke trail, not a ball of fire */
  for (int i = 0; i < F->nm; i++) {
    const SheetMissile *m = &F->m[i];
    int li;
    for (li = 0; li < NL && LAUNCH[li].id != m->id; li++) {}
    if (li == NL && NL < 256) LAUNCH[NL].id = m->id, LAUNCH[NL].seen = false, LAUNCH[NL].pics = 0, NL++;
    if (!m->on || m->z < 12 || m->z > 2500) continue;
    float x = fx(F, m->x), y = fy(F, m->y);
    if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;
    /* behind a cloud, in the G vignette's black or the grey-out (the picture desaturated and darkened above 1,
     * hud.c's g_vision): rightly hard to see */
    if (m->cloud || blacked(F, x, y) || F->gstress > 1) {
      C->msl_hidden++;
      continue;
    }
    C->msl_n++;
    /* a launch is judged by the pictures of it leaving: on screen and clear within 800 m (a missile first in
     * sight 1.3 km out, a speck against a cloud's edge, was counted as a launch not seen) */
    if (li < NL && m->z < 800 && !LAUNCH[li].pics++) C->launches++;
    /* its tail cut at the view's edge (it runs into the panel or off the screen just after launch: the part on screen
     * is judged; before, a tail running out of the view was not judged at all, and a missile 48 m out with its smoke
     * plain below it counted as unseen) */
    float tbx = fx(F, m->bx), tby = fy(F, m->by);
    clip_tail(x, y, &tbx, &tby, vx0, vy0, vx1, vy1);
    /* the body: its fins' size, 0.4 m, and its length along the tail (the whole 3.5 m as a round blob was a disc
     * many times the missile's own area seen from behind) */
    float full = fx(F, 3.5f / m->z * F->F), back = hypotf(fx(F, m->bx) - x, fy(F, m->by) - y);
    MissileLook L = see_missile(F, x, y, tbx, tby, fx(F, 0.4f / m->z * F->F), fminf(full, back * 3.5f / 40), full);
    C->msl_body += L.body;
    C->msl_trail_n += L.trail >= 0;
    C->msl_trail += L.trail >= 0.4f;
    if (L.trail >= 0 && L.trail < 0.4f && !C->trail_ex[0]) {
      float bx = fx(F, m->bx), by = fy(F, m->by);
      snprintf(C->trail_ex, sizeof C->trail_ex, "%s (%.0f m away, its last 40 m %.0f px long on screen, %.0f %% of it marked)",
               shot(F, "missile-notrail", (int)fminf(x, bx) - 20, (int)fminf(y, by) - 20, (int)fmaxf(x, bx) + 20, (int)fmaxf(y, by) + 20),
               m->z, sqrtf((bx - x) * (bx - x) + (by - y) * (by - y)), L.trail * 100);
    }
    bool fire = L.fireball > fmaxf(6, L.bodyr * 3);
    C->msl_fire += fire;
    if (li < NL && m->z < 800 && (L.body || L.trail >= 0.4f) && !LAUNCH[li].seen) LAUNCH[li].seen = true, C->launch_seen++;
    if (fire && !C->fire_ex[0])
      snprintf(C->fire_ex, sizeof C->fire_ex, "%s (%.0f m away: a ball of fire %.0f px across where a body of %.1f px should be)",
               shot(F, "missile-fireball", (int)x - 40, (int)y - 40, (int)x + 40, (int)y + 40), m->z, L.fireball * 2, L.bodyr * 2);
    if (!L.body && L.trail < 0.4f && !C->msl_ex[0])
      snprintf(C->msl_ex, sizeof C->msl_ex, "%s (%.0f m away, nothing seen)",
               shot(F, "missile-unseen", (int)x - 40, (int)y - 40, (int)x + 40, (int)y + 40), m->z);
  }
  for (int i = 0; i < F->nr; i++) {
    const SheetRound *r = &F->rd[i];
    if (r->z < 20 || r->z > 1200) continue;
    float x = fx(F, r->x), y = fy(F, r->y);
    if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;
    C->rnd_n++;
    C->rnd_seen += see_contrast_near(F, x, y, 1.5f, near_px(F)) > 0.2f;
  }
}

static const char *pct(int a, int b) {
  static char s[32][32]; /* more than one report line uses at once (the flight's uses 15) */
  static int k;
  k = (k + 1) % 32;
  if (!b) snprintf(s[k], sizeof s[k], "none to check");
  else snprintf(s[k], sizeof s[k], "%d of %d (%.0f %%)", a, b, 100.0 * a / b);
  return s[k];
}

/* ---- the player's hands and head: flies from what it reads ---- */
typedef struct {
  long  t;          /* pictures */
  long  next_msl, next_flr, next_look;
  int   presses[4]; /* gun, missile, flares, throttle */
} Brain;

static void hands(Game *G, const Frame *F, Brain *B, Cock *C) {
  B->t++;
  if (strcmp(F->state, "play")) return;
  bool alive = F->has_p && F->alive;
  float cx = F->has_v ? fx(F, F->cx) : F->w / 2.0f, cy = F->has_v ? fy(F, F->cy) : F->h / 2.0f;
  /* the nearest enemy by the ranges written under the boxes and the edge arrow */
  const Read *tgt = NULL;
  float best = 1e9f;
  for (int i = 0; i < NRD; i++)
    if (match(RD[i].s, "#.#KM") || match(RD[i].s, "##.#KM") || match(RD[i].s, "###.#KM")) {
      float km = (float)safe_num(RD[i].s, 0);
      if (km < best) best = km, tgt = &RD[i];
    }
  bool lock = find_read("LOCK") != NULL, warn = find_read("MISSILE!") != NULL, pullup = find_read_has("PULL UP") != NULL;
  bool pull = false, push = false, left = false, right = false, gun = false;
  if (!alive) {
  } else if (pullup) {
    pull = true;
  } else if (tgt) {
    float tx = (tgt->x0 + tgt->x1) / 2.0f, ty = tgt->y0 - 14.0f * tgt->scale;
    float dx = tx - cx, dy = ty - cy, ang = atan2f(dx, -dy) * 57.29578f, dist = sqrtf(dx * dx + dy * dy);
    if (dist > 18 * tgt->scale) {
      if (ang > 25) right = true;
      else if (ang < -25) left = true;
      pull = fabsf(ang) < 100;
    } else {
      pull = dy < -2;
      push = dy > 6;
    }
    gun = dist < 20.0f * tgt->scale && best < 1.3f;
  } else {
    pull = (B->t / 30) % 2 == 0; /* look for them: a slow turn */
    right = (B->t / 60) % 3 == 0;
  }
  game_key(G, "s", pull);
  game_key(G, "w", push);
  game_key(G, "a", left);
  game_key(G, "d", right);
  game_key(G, F->classic ? "r" : "space", gun);
  if (gun) C->presses[0]++;
  game_key(G, "r", !F->classic && F->has_p && F->thr < 0.95f && B->t < 200);
  if (lock && B->t >= B->next_msl && !F->classic) {
    game_say(G, "down lshift");
    game_go(G);
    game_say(G, "up lshift");
    C->presses[1]++;
    B->next_msl = B->t + 45;
  }
  if (warn && B->t >= B->next_flr) {
    game_say(G, "down %s", F->classic ? "f" : "q");
    game_go(G);
    game_say(G, "up %s", F->classic ? "f" : "q");
    C->presses[2]++;
    B->next_flr = B->t + 12;
  }
}

/* ---- one flight: a match in a jet, the player at the keys ---- */
static void fly(const char *jet, const char *extra_settings, double seconds, Cock *C) {
  const char *args[16];
  int n = 0;
  char sd[16];
  snprintf(sd, sizeof sd, "%u", seed++);
  args[0] = NULL;
  args_add(args, &n, "--play");
  args_add(args, &n, "1p");
  args_add(args, &n, "--seed");
  args_add(args, &n, sd);
  if (jet) args_add(args, &n, "--jet"), args_add(args, &n, jet);
  Game G;
  Frame F = { 0 };
  memset(C, 0, sizeof *C);
  NL = 0;
  if (!game_start(&G, &RUN, args, extra_settings ? extra_settings : "")) return;
  Brain B = { 0 };
  long limit = (long)(seconds * 60 / RUN.every);
  while (game_read(&G, &F, 30000)) {
    read_screen(&F);
    check_cockpit(&F, C);
    if (C->pics % 3 == 0) audit_texts(&F, "flight");
    hands(&G, &F, &B, C);
    if (!strcmp(F.state, "over") || C->pics >= limit) break;
    game_go(&G);
  }
  game_end(&G);
  frame_free(&F);
}

static void report_cockpit(const char *id, const char *jet, const Cock *C) {
  char title[64];
  snprintf(title, sizeof title, "flying the %s from the cockpit", jet);
  bool bad = (C->hdg_n && C->hdg_ok < C->hdg_n * 0.97) || (C->g_n && C->g_ok < C->g_n * 0.95) ||
             (C->hor_found >= 20 && C->hor_ok < C->hor_found * 0.6) /* clouds, hills and the G vignette hide it often */ || (C->dial_n && C->dial_ok < C->dial_found * 0.9) ||
             (C->foe_n && C->foe_seen < C->foe_n * 0.8) || (C->box_n && C->box_label < C->box_n * 0.9) ||
             (C->lock_n && C->lock_read < C->lock_n * 0.9) || C->msl_fire > 0 || (C->launches && C->launch_seen < C->launches) ||
             (C->spd_n && C->spd_ok < C->spd_n * 0.9) || (C->alt_n && C->alt_ok < C->alt_n * 0.9) || stores_bad(&C->st);
  char stb[4096];
  stores_table(stb, sizeof stb, &C->st);
  report_item(id, title, C->pics < 10 ? V_FAIL : bad ? V_FAIL : V_PASS,
              "%ld pictures, %ld in the cockpit; I pressed the gun %d times, missiles %d, flares %d.\n\n"
              "| reading | right | worst | example |\n|---|---|---|---|\n"
              "| heading under the tape (to 1.5 degrees) | %s | %.1f deg | %s |\n"
              "| G meter (to 0.1 g) | %s | %.2f g | %s |\n"
              "| airspeed in the panel, where it is written (in kt, km/h or m/s, 2 %%) | %s | | %s |\n"
              "| altitude in the panel, where it is written (in ft or m, 2 %%) | %s | | %s |\n"
              "| horizon in the windscreen against the attitude (calm flight; 3 degrees, 4 %% of the view) | %s, found in %s | | %s |\n"
              "| dial needles against the value they show (7 degrees; %d more under the face's writing not judged) | %s, needle found in %s | %.0f deg %s | %s |\n"
              "| enemy jets standing out where they are (inside 3.5 km; %d more behind cloud not judged) | %s | | %s |\n"
              "| range read under each enemy box (to 0.1 km) | %s | | %s |\n"
              "| LOCK read over the lock diamond | %s | | %s |\n"
              "| my missiles launched and seen leaving (in sight and clear within 800 m) | %s | | %s |\n"
              "| missile pictures with a body (%d more behind cloud or in the G black not judged) | %s | | |\n"
              "| missile pictures with a smoke trail (where its tail is 12 px or more on screen) | %s | | %s |\n"
              "| missile pictures that look like a ball of fire | %s | | %s |\n"
              "| my rounds seen (inside 1.2 km) | %s | | |\n\nThe stores, every cockpit picture:\n\n%s",
              C->pics, C->cockpit, C->presses[0], C->presses[1], C->presses[2], pct(C->hdg_ok, C->hdg_n), C->hdg_worst,
              C->hdg_ex, pct(C->g_ok, C->g_n), C->g_worst, C->g_ex, pct(C->spd_ok, C->spd_n), C->spd_ex, pct(C->alt_ok, C->alt_n), C->alt_ex,
              pct(C->hor_ok, C->hor_found), pct(C->hor_found, C->hor_n), C->hor_ex, C->dial_hidden, pct(C->dial_ok, C->dial_found),
              pct(C->dial_found, C->dial_n), C->dial_worst, C->dial_title, C->dial_ex, C->foe_cloud, pct(C->foe_seen, C->foe_n), C->foe_ex,
              pct(C->box_label, C->box_n), C->km_ex, pct(C->lock_read, C->lock_n), C->lock_ex,
              pct(C->launch_seen, C->launches), C->msl_ex, C->msl_hidden, pct(C->msl_body, C->msl_n), pct(C->msl_trail, C->msl_trail_n), C->trail_ex,
              pct(C->msl_fire, C->msl_n), C->fire_ex, pct(C->rnd_seen, C->rnd_n), stb);
}

/* ---- every key: the same match twice, with and without the key, picture against picture ---- */
#define KEY_AT    150  /* frame: in the air, past the start */
#define KEY_SPAN  120  /* frames watched after the press */
typedef struct { Frame f[64]; int n; } Film;

static void film_free(Film *M) {
  for (int i = 0; i < M->n; i++) frame_free(&M->f[i]);
  M->n = 0;
}

/* runs to KEY_AT + KEY_SPAN, pressing key (NULL: none) at KEY_AT for hold frames; keeps the pictures after it */
static bool film(Film *M, const char *const *args, const char *settings, const char *key, int hold) {
  Game G;
  memset(M, 0, sizeof *M);
  Run R = RUN;
  R.every = 3;
  if (!game_start(&G, &R, args, settings)) return false;
  Frame F = { 0 };
  bool down = false;
  while (game_read(&G, &F, 30000)) {
    if (key && !down && F.frame >= KEY_AT) game_say(&G, "down %s", key), down = true;
    if (key && down && F.frame >= KEY_AT + hold && hold >= 0) game_say(&G, "up %s", key), hold = -1;
    if (F.frame >= KEY_AT && M->n < 64) {
      M->f[M->n] = F;          /* keep this picture */
      memset(&F, 0, sizeof F); /* and read the next into fresh memory */
      M->n++;
    }
    if (M->n && M->f[M->n - 1].frame >= KEY_AT + KEY_SPAN) break;
    game_go(&G);
  }
  game_end(&G);
  frame_free(&F);
  return M->n > 0;
}

static void texts_of(const Frame *F, char *out, size_t n) {
  out[0] = 0;
  Read r[512];
  int k = see_texts(F, r, 512);
  size_t u = 0;
  for (int i = 0; i < k && u + 70 < n; i++) u += (size_t)snprintf(out + u, n - u, "|%s", r[i].s);
}
/* letters to change, add or drop to turn a into b */
static int edits(const char *a, const char *b) {
  int la = (int)strlen(a), lb = (int)strlen(b), row[80] = { 0 };
  if (la > 78 || lb > 78) return 99;
  for (int j = 0; j <= lb; j++) row[j] = j;
  for (int i = 1; i <= la; i++) {
    int diag = row[0];
    row[0] = i;
    for (int j = 1; j <= lb; j++) {
      int up = row[j], c = diag + (a[i - 1] != b[j - 1]);
      if (up + 1 < c) c = up + 1;
      if (row[j - 1] + 1 < c) c = row[j - 1] + 1;
      diag = up, row[j] = c;
    }
  }
  return row[lb];
}
/* b is there in a, read a little differently (see-through text over a moving scene loses a cell now and then),
 * or as part of a longer line read there */
static bool seen_like(const char *ta, const char *p) {
  char buf[8192];
  snprintf(buf, sizeof buf, "%s", ta);
  int allow = (int)strlen(p) / 5;
  if (allow < 1) allow = strlen(p) >= 4;
  if (strpbrk(p, "0123456789")) allow = 0; /* a readout that changed is news */
  for (char *save = NULL, *q = strtok_r(buf, "|", &save); q; q = strtok_r(NULL, "|", &save))
    if (edits(q, p) <= allow || (strlen(p) >= 3 && !strpbrk(p, "0123456789") && strstr(q, p)))
      return true;
  return false;
}
/* a text in b that a does not have */
static void new_texts(const Frame *A, const Frame *B, char *out, size_t n) {
  char ta[8192], tb[8192];
  texts_of(A, ta, sizeof ta);
  texts_of(B, tb, sizeof tb);
  out[0] = 0;
  size_t u = 0;
  char *cur = tb;
  for (char *p = safe_token(&cur, "|"); p && u + 70 < n; p = safe_token(&cur, "|")) {
    char needle[80];
    snprintf(needle, sizeof needle, "|%s", p);
    char pa[80];
    pattern_of(p, pa, sizeof pa);
    if (!strstr(ta, needle) && !seen_like(ta, p)) u += (size_t)snprintf(out + u, n - u, "%s`%s`", u ? ", " : "", p);
  }
}

typedef struct { const char *key, *what; int hold; } KeyCase;

static void key_cases(const char *id, const char *title, const char *const *args, const char *settings, const KeyCase *K, int nk) {
  Film base = { 0 }, again = { 0 }; /* film_free must find again empty when base already failed */
  if (!film(&base, args, settings, NULL, 0) || !film(&again, args, settings, NULL, 0)) {
    report_item(id, title, V_FAIL, "The game did not run to frame %d (log in %s/logs).", KEY_AT + KEY_SPAN, OUT);
    film_free(&base), film_free(&again);
    return;
  }
  /* the same match twice must give the same pictures, or a difference proves nothing */
  int x0, y0, x1, y1;
  double noise = 0;
  for (int i = 0; i < base.n && i < again.n; i++) {
    double d = see_diff(&base.f[i], &again.f[i], &x0, &y0, &x1, &y1);
    if (d > noise) noise = d;
  }
  film_free(&again);
  char b[1 << 15];
  size_t u = 0;
  int bad = 0;
  Stores KS;
  memset(&KS, 0, sizeof KS);
  u += (size_t)snprintf(b + u, sizeof b - u,
                        "Each key pressed at frame %d in the same match as a run without it; the pictures after it compared. "
                        "Two runs without a key differed by %.4f %% of the picture at most.\n\n"
                        "| key | for | first change on screen | where | new on screen | what the game did | verdict |\n|---|---|---|---|---|---|---|\n",
                        KEY_AT, noise * 100);
  for (int k = 0; k < nk; k++) {
    Film test;
    if (!film(&test, args, settings, K[k].key, K[k].hold)) {
      u += (size_t)snprintf(b + u, sizeof b - u, "| %s | %s | the game stopped | | | | FAIL |\n", K[k].key, K[k].what);
      bad++;
      continue;
    }
    for (int i = 0; i < test.n; i++) {
      read_screen(&test.f[i]);
      check_stores(&test.f[i], &KS);
    }
    int first = -1, fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    double most = 0;
    int mi = 0;
    for (int i = 0; i < base.n && i < test.n; i++) {
      double d = see_diff(&base.f[i], &test.f[i], &x0, &y0, &x1, &y1);
      if (d > noise + 0.00002 && first < 0) first = i, fx0 = x0, fy0 = y0, fx1 = x1, fy1 = y1;
      if (d > most) most = d, mi = i;
    }
    const Frame *A = &base.f[mi < base.n ? mi : 0], *T = &test.f[mi < test.n ? mi : 0];
    const Frame *A0 = &base.f[first >= 0 ? first : 0], *T0 = &test.f[first >= 0 ? first : 0];
    char gained[600];
    new_texts(A, T, gained, sizeof gained);
    /* what the game did, from the answer sheet at the same moment */
    char did[300];
    size_t dn = 0;
    did[0] = 0;
    const Frame *Ae = &base.f[base.n - 1], *Te = &test.f[test.n - 1];
#define DID(...) (dn += (size_t)snprintf(did + dn, sizeof did - dn, __VA_ARGS__))
    if (strcmp(Ae->state, Te->state)) DID("screen %s to %s; ", Ae->state, Te->state);
    if (Ae->has_p && Te->has_p) {
      if (Te->ammo != Ae->ammo) DID("rounds %d to %d; ", Ae->ammo, Te->ammo);
      if (Te->missiles != Ae->missiles) DID("missiles %d to %d; ", Ae->missiles, Te->missiles);
      if (Te->flares != Ae->flares) DID("flares %d to %d; ", Ae->flares, Te->flares);
      if (Te->wsel != Ae->wsel) DID("weapon %d to %d; ", Ae->wsel, Te->wsel);
      if (strcmp(Te->view, Ae->view)) DID("view %s to %s; ", Ae->view, Te->view);
      if (fabsf(Te->thr - Ae->thr) > 0.02f) DID("throttle %.0f%% to %.0f%%; ", Ae->thr * 100, Te->thr * 100);
      if (fabsf(Te->pitch - Ae->pitch) > 1) DID("pitch %+.0f deg; ", Te->pitch - Ae->pitch);
      if (fabsf(wrap180(Te->bank - Ae->bank)) > 2) DID("bank %+.0f deg; ", wrap180(Te->bank - Ae->bank));
      if (fabsf(wrap180(Te->hdg - Ae->hdg)) > 1) DID("heading %+.0f deg; ", wrap180(Te->hdg - Ae->hdg));
      int ml = 0;
      for (int i = 0; i < test.n; i++) ml = test.f[i].nm > ml ? test.f[i].nm : ml;
      int mb = 0;
      for (int i = 0; i < base.n; i++) mb = base.f[i].nm > mb ? base.f[i].nm : mb;
      if (ml > mb) DID("%d more of my missiles in the air; ", ml - mb);
    }
#undef DID
    if (!did[0]) snprintf(did, sizeof did, "nothing");
    const char *where = "";
    if (first >= 0) {
      bool panel = T0->has_v && fy0 >= fy(T0, T0->vy + T0->sh) - 1;
      bool hud = gained[0] != 0;
      where = panel ? "the panel" : hud ? "the view and its texts" : "the view";
    }
    bool silent = first < 0;
    bool unshown = !silent && strcmp(did, "nothing") && most < 0.0004 && !gained[0];
    const char *v = silent ? "**FAIL**: nothing on screen" : unshown ? "**FAIL**: hardly shows" : "PASS";
    if (silent || unshown) bad++;
    char when[200];
    if (silent) snprintf(when, sizeof when, "none ([after](%s))", shot(T, "key-silent", 0, 0, 0, 0));
    else snprintf(when, sizeof when, "after %.2f s ([picture](%s))", (T0->frame - KEY_AT) / 60.0, shot(T0, "key", fx0, fy0, fx1, fy1));
    (void)A0;
    if (u < sizeof b - 1500)
      u += (size_t)snprintf(b + u, sizeof b - u, "| `%s` | %s | %s | %s | %s | %s | %s |\n", K[k].key, K[k].what, when,
                            silent ? "-" : where, gained[0] ? gained : "-", did, v);
    film_free(&test);
  }
  film_free(&base);
  if (KS.n || KS.pip_n) {
    char stb[4096];
    stores_table(stb, sizeof stb, &KS);
    if (u < sizeof b - strlen(stb) - 100) u += (size_t)snprintf(b + u, sizeof b - u, "\nThe stores in the panel after each key, against what is left:\n\n%s", stb);
    if (stores_bad(&KS)) bad++;
  }
  report_item(id, title, bad ? V_FAIL : V_PASS, "%s", b);
}

/* ---- the hands-on checklist, item by item ---- */
static bool want(const char *only, const char *id) {
  if (!only) return true;
  char b[256];
  snprintf(b, sizeof b, ",%s,", only);
  char k[16];
  snprintf(k, sizeof k, ",%s,", id);
  return strstr(b, k) != NULL;
}

/* runs the game to its menu and reads it; returns with G running, or with it ended when the menu never came */
static bool to_menu(Game *G, Frame *F, const char *settings, const char *const *args) {
  if (!game_start(G, &RUN, args, settings)) return false;
  for (int i = 0; i < 400; i++) {
    if (!game_read(G, F, 30000)) {
      report_line("\nThe game stopped sending pictures before its menu (log: %s).\n", G->log);
      game_end(G);
      return false;
    }
    if (!strcmp(F->state, "menu") && i > 20) {
      read_screen(F);
      if (find_read("START MISSION")) return true;
    }
    game_go(G);
  }
  /* leave what it saw for the report */
  read_screen(F);
  char b[1024];
  size_t u = 0;
  for (int i = 0; i < NRD && u < sizeof b - 80; i++) u += (size_t)snprintf(b + u, sizeof b - u, "%s\"%s\"", i ? ", " : "", RD[i].s);
  report_line("\nNo menu after 400 pictures (state %s, page %s); read: %s. [last picture](%s)\n", F->state, F->page,
              u ? b : "nothing", shot(F, "nomenu", 0, 0, 0, 0));
  game_end(G);
  return false;
}

/* clicks on the middle of a text read on the screen; false if not on screen */
static bool click_text(Game *G, Frame *F, const char *word) {
  read_screen(F);
  const Read *r = find_read_has(word);
  if (!r) return false;
  game_say(G, "click %.0f %.0f", wx(F, (r->x0 + r->x1) / 2.0f), wy(F, (r->y0 + r->y1) / 2.0f));
  return true;
}

static void item1(void) {
  Game G;
  Frame F = { 0 };
  const char *a[] = { NULL };
  if (!to_menu(&G, &F, "", a)) {
    report_item("1", "window size, sharpness, fullscreen", V_FAIL, "The menu never came up.");
    return;
  }
  int w0 = F.w, h0 = F.h;
  read_screen(&F);
  float err = 0;
  for (int i = 0; i < NRD; i++) err += RD[i].err;
  err = NRD ? err / NRD : 9;
  game_say(&G, "down f11");
  step(&G, &F, 1);
  game_say(&G, "up f11");
  step(&G, &F, 20);
  int w1 = F.w, h1 = F.h;
  game_say(&G, "down alt+enter");
  step(&G, &F, 1);
  game_say(&G, "up alt+enter");
  step(&G, &F, 20);
  int w2 = F.w, h2 = F.h;
  const char *pic = shot(&F, "menu", 0, 0, 0, 0);
  game_end(&G);
  bool sharp = err < 0.25f && NRD > 5;
  report_item("1", "window size, sharpness, fullscreen", sharp && w0 > 0 ? V_PASS : V_FAIL,
              "Window %dx%d; the picture comes as %dx%d (%s, %s). %d lines of text read on the menu with %.2f wrong cells "
              "a letter on average: %s. F11 gave %dx%d, Alt+Enter %dx%d%s. [menu](%s)",
              WIN_W, WIN_H, w0, h0, F.gpu ? "GPU, full size" : "software, drawn at this size and scaled up",
              F.fbw == w0 ? "one picture pixel per drawn pixel" : "scaled", NRD, err, sharp ? "sharp" : "blurred or broken", w1, h1,
              w2, h2, w1 == w0 && h1 == h0 ? " (the test window cannot go fullscreen without a screen: run with LP_DISPLAY=1 on a desktop)" : "",
              pic);
}

static void item2(void) {
  /* every button read on the main menu is clicked where it was read; the screen must answer */
  static const struct { const char *label, *page, *expect; } B[] = {
    { "HANGAR", "hangar", NULL }, { "SETTINGS", NULL, "RENDERER" }, { "NETWORK", "network", NULL }, /* settings has no title */
  };
  char b[4096];
  size_t u = 0;
  int bad = 0;
  for (size_t i = 0; i < sizeof B / sizeof B[0]; i++) {
    Game G;
    Frame F = { 0 };
    const char *a[] = { NULL };
    if (!to_menu(&G, &F, "", a)) {
      bad++;
      u += (size_t)snprintf(b + u, sizeof b - u, "- %s: the menu never came up\n", B[i].label);
      continue;
    }
    if (!click_text(&G, &F, B[i].label)) {
      bad++;
      u += (size_t)snprintf(b + u, sizeof b - u, "- `%s` is not on the menu ([menu](%s))\n", B[i].label, shot(&F, "menu", 0, 0, 0, 0));
      game_end(&G);
      continue;
    }
    step(&G, &F, 10);
    read_screen(&F);
    bool ok = (B[i].page && !strcmp(F.page, B[i].page)) || (B[i].expect && find_read(B[i].expect) && !find_read("START MISSION"));
    if (!ok) bad++;
    u += (size_t)snprintf(b + u, sizeof b - u, "- clicked `%s` where it was read: %s ([after](%s))\n", B[i].label,
                          ok ? "its page opened" : "the page did not change", shot(&F, "click", 0, 0, 0, 0));
    /* and back with Esc */
    game_say(&G, "down esc");
    step(&G, &F, 1);
    game_say(&G, "up esc");
    step(&G, &F, 8);
    read_screen(&F);
    /* the main menu itself, not the hangar's own START MISSION button */
    bool back = find_read("START MISSION") && find_read("EXIT") && find_read("SETTINGS");
    if (!back) bad++;
    if (back) u += (size_t)snprintf(b + u, sizeof b - u, "  - Esc: back on the main menu\n");
    else u += (size_t)snprintf(b + u, sizeof b - u, "  - Esc: did not come back ([after Esc](%s))\n", shot(&F, "esc", 0, 0, 0, 0));
    game_end(&G);
    frame_free(&F);
  }
  report_item("2", "mouse clicks land where the pointer is", bad ? V_FAIL : V_PASS, "%s", b);
}

static void item3(void) {
  const char *a1[] = { "--play", "1p", "--seed", "11", NULL };
  static const KeyCase P1[] = {
    { "w", "push (nose down)", 40 }, { "s", "pull (nose up)", 40 }, { "a", "roll left", 30 }, { "d", "roll right", 30 },
    { "r", "throttle up", 60 }, { "f", "throttle down", 60 }, { "space", "gun", 30 }, { "lshift", "missile", 4 },
    { "q", "flares", 4 }, { "g", "eyes on the enemy (padlock)", 60 }, { "t", "autopilot", 4 }, { "tab", "chase view", 4 },
    { "m", "sound on and off", 4 }, { "esc", "pause", 4 }, { "f1", "pause", 4 },
  };
  key_cases("3a", "keyboard, player 1 keys, one player", a1, "", P1, (int)(sizeof P1 / sizeof P1[0]));
  const char *a2[] = { "--play", "2p", "--seed", "12", NULL };
  static const KeyCase P2[] = {
    { "w", "player 1 push", 40 }, { "up", "player 2 push", 40 }, { "down", "player 2 pull", 40 }, { "left", "player 2 roll left", 30 },
    { "right", "player 2 roll right", 30 }, { "p", "player 2 throttle up", 60 }, { "semicolon", "player 2 throttle down", 60 },
    { "enter", "player 2 gun", 30 }, { "rshift", "player 2 missile", 4 }, { "apostrophe", "player 2 flares", 4 },
  };
  key_cases("3b", "keyboard, both key sets in two-player", a2, "mode=2p\n", P2, (int)(sizeof P2 / sizeof P2[0]));
  static const KeyCase CL[] = {
    { "q", "dive", 40 }, { "a", "climb", 40 }, { "z", "bank left", 30 }, { "x", "bank right", 30 }, { "d", "power up", 60 },
    { "s", "power down", 60 }, { "f", "fire the selected weapon", 30 }, { "w", "select weapon", 4 }, { "c", "map view", 4 },
    { "r", "flares or chaff", 4 },
  };
  key_cases("3c", "keyboard, the classic keys", a1, "rules=classic\n", CL, (int)(sizeof CL / sizeof CL[0]));
}

/* the missile key with every kind of loadout, in every jet: it must show something each time */
static void item_weapons(const char *jets_only) {
  /* loadouts the game takes (game_load_fits: radar or ground missiles, not both; 8 places, a ground one takes two);
   * one it refuses falls back to the default without a word */
  static const char *LOADS[] = { "6,0,1", "2,6,0", "0,6,0", "0,0,1", "4,0,2" };
  static const KeyCase K[] = { { "lshift", "missile", 4 }, { "space", "gun", 30 }, { "q", "flares", 4 } };
  for (size_t l = 0; l < sizeof LOADS / sizeof LOADS[0]; l++)
    for (int j = 0; j < NJ; j++) {
      if (jets_only && !strstr(jets_only, JET_IDS[j])) continue;
      if (l > 0 && j > 0 && !jets_only) continue; /* every loadout in the first jet, every jet with the default */
      const char *a[] = { "--play", "1p", "--seed", "13", "--jet", JET_IDS[j], NULL };
      char set[64], id[16], title[96];
      snprintf(set, sizeof set, "load1=%s\n", LOADS[l]);
      snprintf(id, sizeof id, "W%zu%d", l + 1, j + 1);
      snprintf(title, sizeof title, "weapons in the %s, loadout %s (heat, radar, ground)", JET_IDS[j], LOADS[l]);
      key_cases(id, title, a, set, K, 3);
    }
  /* classic: select each weapon and fire it */
  static const KeyCase C[] = { { "w", "select weapon", 4 }, { "f", "fire", 20 } };
  const char *a[] = { "--play", "1p", "--seed", "14", NULL };
  key_cases("W9", "classic rules: select and fire", a, "rules=classic\nload1=2,6,0\n", C, 2);
}

/* ---- what the jet carries and how its engines burn, seen from outside (chase view) ---- */
static float luma_of(uint32_t c) { return 0.3f * (c >> 16 & 255) + 0.59f * (c >> 8 & 255) + 0.11f * (c & 255); }
static int sat_of(uint32_t c) {
  int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255, hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
  return hi - lo;
}
/* the mark of each kind's model, as stores.c paints it: the heat-seeker's dark seeker dome, the radar missile's pale
 * radome, the air-to-ground missile's olive body */
static bool kind_mark(int cls, uint32_t c) {
  int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  if (cls == 1) return luma_of(c) > 175 && sat_of(c) < 40;
  if (cls == 2) return g > b + 10 && r > b && abs(r - g) < 30 && luma_of(c) > 60 && luma_of(c) < 180;
  return luma_of(c) < 75 && sat_of(c) < 30;
}
/* pixels of the mark within a box half h about (x, y) (picture pixels), and the box's mean colour */
static int marks_near(const Frame *F, float x, float y, int h, int cls) {
  int n = 0;
  for (int yy = (int)y - h; yy <= (int)y + h; yy++)
    for (int xx = (int)x - h; xx <= (int)x + h; xx++)
      if (xx >= 0 && yy >= 0 && xx < F->w && yy < F->h && kind_mark(cls, px_at(F, xx, yy))) n++;
  return n;
}
/* pixels that changed clearly (a channel by more than 40) in a box half h about (x, y) */
static int patch_changed(const Frame *A, const Frame *B, float x, float y, int h) {
  int n = 0;
  for (int yy = (int)y - h; yy <= (int)y + h; yy++)
    for (int xx = (int)x - h; xx <= (int)x + h; xx++) {
      if (xx < 0 || yy < 0 || xx >= A->w || yy >= A->h || xx >= B->w || yy >= B->h) continue;
      uint32_t a = px_at(A, xx, yy), b = px_at(B, xx, yy);
      n += abs((int)(a >> 16 & 255) - (int)(b >> 16 & 255)) > 40 || abs((int)(a >> 8 & 255) - (int)(b >> 8 & 255)) > 40 ||
           abs((int)(a & 255) - (int)(b & 255)) > 40;
    }
  return n;
}
static double patch_diff(const Frame *A, const Frame *B, float x, float y, int h) {
  double d = 0;
  int n = 0;
  for (int yy = (int)y - h; yy <= (int)y + h; yy++)
    for (int xx = (int)x - h; xx <= (int)x + h; xx++) {
      if (xx < 0 || yy < 0 || xx >= A->w || yy >= A->h || xx >= B->w || yy >= B->h) continue;
      uint32_t a = px_at(A, xx, yy), b = px_at(B, xx, yy);
      d += abs((int)(a >> 16 & 255) - (int)(b >> 16 & 255)) + abs((int)(a >> 8 & 255) - (int)(b >> 8 & 255)) + abs((int)(a & 255) - (int)(b & 255));
      n++;
    }
  return n ? d / (n * 3.0) : 0;
}
/* the hot exhaust at a nozzle: the brightest pixel and how many are flame-coloured (orange to white, or the
 * afterburner's blue-violet) in a box half h */
static void flame_near(const Frame *F, float x, float y, int h, float *best, int *hot) {
  *best = 0, *hot = 0;
  for (int yy = (int)y - h; yy <= (int)y + h; yy++)
    for (int xx = (int)x - h; xx <= (int)x + h; xx++) {
      if (xx < 0 || yy < 0 || xx >= F->w || yy >= F->h) continue;
      uint32_t c = px_at(F, xx, yy);
      int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
      float l = luma_of(c);
      if (l > *best) *best = l;
      if ((r > 190 && g > 70 && b < r - 40) || (l > 225) || (b > 170 && r > 120 && g < b - 20)) (*hot)++;
    }
}
static int px_len(const Frame *F, float metres, float z) { /* metres at depth z, in picture pixels */
  return z > 1 ? (int)fmaxf(2, fx(F, F->F * metres / z)) : 2;
}

/* to the chase view and a picture with the jet's own stores on the sheet; false if it never came */
static bool to_chase(Game *G, Frame *F) {
  if (!step(G, F, 20)) return false;
  for (int k = 0; k < 4; k++) {
    if (F->has_p && !strcmp(F->view, "chase") && F->nst + F->nnz > 0) return true;
    if (!F->has_p || strcmp(F->view, "chase")) game_say(G, "down tab"), step(G, F, 2), game_say(G, "up tab");
    if (!step(G, F, 10)) return false;
  }
  return F->has_p && !strcmp(F->view, "chase") && F->nst + F->nnz > 0;
}

static void item_stores_outside(const char *jets_only) {
  static const char *LOADS[] = { "", "2,6,0", "4,0,2", "6,0,1" }; /* the jet's own default first */
  static const char *KIND[3] = { "heat", "radar", "ground" };
  for (int j = 0; j < NJ; j++) {
    if (jets_only && !strstr(jets_only, JET_IDS[j])) continue;
    char b[1 << 14];
    size_t u = 0;
    int bad = 0;
    u += (size_t)snprintf(b + u, sizeof b - u,
                          "In chase view. The stations the game drew (its sheet) against the loadout asked for and the stores "
                          "really left; each loaded station on screen looked for its model's mark (heat: the dark seeker "
                          "dome; radar: the pale radome; ground: the olive body); then one missile fired, and its station "
                          "looked at again; then full power, and each nozzle's plume looked for.\n\n"
                          "| loadout asked (heat, radar, ground) | stations drawn | loaded | left (stores) | in a shut bay | "
                          "mark seen | after one shot | plumes, dry / afterburner | picture | verdict |\n"
                          "|---|---|---|---|---|---|---|---|---|---|\n");
    for (size_t l = 0; l < sizeof LOADS / sizeof LOADS[0]; l++) {
      char sd[16], set[64];
      snprintf(sd, sizeof sd, "%u", 40 + (unsigned)(j * 10 + l));
      const char *a[] = { "--play", "1p", "--seed", sd, "--jet", JET_IDS[j], NULL };
      snprintf(set, sizeof set, "%s%s%s", LOADS[l][0] ? "load1=" : "", LOADS[l], LOADS[l][0] ? "\n" : "");
      Game G;
      Frame F = { 0 }, A = { 0 };
      const char *asked = LOADS[l][0] ? LOADS[l] : "its own";
      if (!game_start(&G, &RUN, a, set) || !to_chase(&G, &F)) {
        u += (size_t)snprintf(b + u, sizeof b - u, "| %s | no chase picture with the stores | | | | | | | | FAIL |\n", asked);
        bad++;
        game_end(&G);
        frame_free(&F);
        continue;
      }
      read_screen(&F);
      int drawn[3] = { 0 }, loaded[3] = { 0 }, shut = 0, on = 0, seen = 0;
      bool seen_at[24] = { false };
      for (int i = 0; i < F.nst; i++) {
        const SheetStation *w = &F.st[i];
        if (w->cls < 0 || w->cls > 2) continue;
        drawn[w->cls]++;
        loaded[w->cls] += w->loaded;
        shut += !w->shown;
        if (w->loaded && w->shown && w->on) {
          on++;
          int h = px_len(&F, 1.6f, w->z);
          seen_at[i] = marks_near(&F, fx(&F, w->x), fy(&F, w->y), h, w->cls) > 0;
          seen += seen_at[i];
        }
      }
      int want[3] = { -1, -1, -1 };
      if (LOADS[l][0]) sscanf(LOADS[l], "%d,%d,%d", &want[0], &want[1], &want[2]);
      bool counts = F.has_q && loaded[0] == F.q_heat && loaded[1] == F.q_radar && loaded[2] == F.q_ground;
      bool asked_ok = want[0] < 0 || (drawn[0] == want[0] && drawn[1] == want[1] && drawn[2] == want[2]);
      /* one shot: the same match again (the same pictures, frame for frame), the missile key pressed where the first
       * run went on without it; a second on, the station it left is compared between the two runs, and the
       * loaded ones too (they should not differ): the missile gone is the difference, not the jet's motion */
      A = F;
      memset(&F, 0, sizeof F);
      long f0 = A.frame;
      Frame X = { 0 };
      bool okr = step(&G, &F, 15);
      if (okr) X = F, memset(&F, 0, sizeof F);
      char after[200] = "no shot seen";
      bool shot_ok = false;
      Game G2;
      Frame Y = { 0 };
      if (okr && game_start(&G2, &RUN, a, set) && to_chase(&G2, &Y)) {
        while (Y.frame < f0 && step(&G2, &Y, 1)) {}
        game_say(&G2, "down lshift");
        step(&G2, &Y, 2);
        game_say(&G2, "up lshift");
        while (Y.frame < X.frame && step(&G2, &Y, 1)) {}
        int gone = -1;
        for (int i = 0; i < X.nst && i < Y.nst; i++)
          if (X.st[i].loaded && !Y.st[i].loaded && X.st[i].cls == Y.st[i].cls) gone = i;
        bool same = Y.frame == X.frame;
        if (gone >= 0 && same) {
          const SheetStation *w = &X.st[gone];
          double dg = patch_changed(&X, &Y, fx(&X, w->x), fy(&X, w->y), px_len(&X, 0.7f, w->z)), dk = 0;
          int nk = 0;
          for (int i = 0; i < X.nst && i < Y.nst; i++)
            if (i != gone && Y.st[i].loaded && Y.st[i].shown && Y.st[i].on)
              dk += patch_changed(&X, &Y, fx(&X, X.st[i].x), fy(&X, X.st[i].y), px_len(&X, 0.7f, X.st[i].z)), nk++;
          dk = nk ? dk / nk : 0;
          bool left_ok = X.has_q && Y.has_q && (Y.q_heat + Y.q_radar + Y.q_ground) == (X.q_heat + X.q_radar + X.q_ground) - 1;
          /* (a station not seen before the shot, behind the wing or the body from here, cannot be seen empty) */
          bool hidden = !w->shown || !w->on || (gone < 24 && !seen_at[gone] && dg < 3);
          shot_ok = left_ok && (hidden || dg >= dk + 3);
          snprintf(after, sizeof after, "%s station emptied: %.0f pixels changed there (%.1f at a loaded one)%s%s",
                   KIND[w->cls], dg, dk, !w->shown ? ", in a shut bay" : hidden ? ", not in sight from here" : "",
                   left_ok ? "" : "; stores count off");
        } else if (!same)
          snprintf(after, sizeof after, "the second run did not reach the same frame");
        else if (X.has_q && Y.has_q && X.q_heat + X.q_radar + X.q_ground == Y.q_heat + Y.q_radar + Y.q_ground)
          snprintf(after, sizeof after, "nothing fired (the key may need a lock for this kind)"), shot_ok = true;
        else
          snprintf(after, sizeof after, "a missile left (stores %d -> %d) but no station on the sheet emptied",
                   X.q_heat + X.q_radar + X.q_ground, Y.q_heat + Y.q_radar + Y.q_ground);
        game_end(&G2);
      }
      frame_free(&Y);
      frame_free(&X);
      /* full power: the plumes */
      char pl[120] = "no nozzle on the sheet";
      bool plume_ok = false;
      float dry = 0, ab = 0;
      int ndry = 0, nab = 0;
      okr = okr && step(&G, &F, 1); /* a picture of this run again (the last went into the comparison) */
      int nn = F.nnz;
      if (okr && F.nnz > 0) {
        for (int i = 0; i < F.nnz; i++)
          if (F.nz[i].on) {
            float best;
            int hot;
            flame_near(&F, fx(&F, F.nz[i].x), fy(&F, F.nz[i].y), px_len(&F, 1.2f, F.nz[i].z), &best, &hot);
            dry += best, ndry += hot > 0;
          }
        game_say(&G, "down r");
        for (int k = 0; k < 40 && step(&G, &F, 4) && !(F.has_p && F.thr > 0.97f); k++) {}
        step(&G, &F, 8);
        game_say(&G, "up r");
        for (int i = 0; i < F.nnz; i++)
          if (F.nz[i].on) {
            float best;
            int hot;
            flame_near(&F, fx(&F, F.nz[i].x), fy(&F, F.nz[i].y), px_len(&F, 2.5f, F.nz[i].z), &best, &hot);
            ab += best, nab += hot > 2 && F.nz[i].ab > 0.5f;
          }
        plume_ok = nab == F.nnz && ab >= dry;
        snprintf(pl, sizeof pl, "%d nozzle%s: glow at %d / %d, plume at %d (brightest %.0f to %.0f)", nn, nn == 1 ? "" : "s", ndry,
                 nn, nab, F.nnz ? dry / F.nnz : 0, F.nnz ? ab / F.nnz : 0);
      }
      const char *pic = shot(&A, "stores-outside", 0, 0, 0, 0);
      bool marks_ok = seen * 4 >= on * 3;
      bool ok = counts && asked_ok && marks_ok && shot_ok && plume_ok;
      char why[160] = "";
      snprintf(why, sizeof why, "%s%s%s%s%s", asked_ok ? "" : "stations not the loadout asked; ", counts ? "" : "loaded not the stores left; ",
               marks_ok ? "" : "marks not seen; ", shot_ok ? "" : "the shot not seen; ", plume_ok ? "" : "plumes missing; ");
      bad += !ok;
      u += (size_t)snprintf(b + u, sizeof b - u,
                            "| %s | %d, %d, %d | %d, %d, %d | %d, %d, %d | %d | %d of %d on screen | %s | %s | [picture](%s) | %s |\n",
                            asked, drawn[0], drawn[1], drawn[2], loaded[0], loaded[1], loaded[2], A.q_heat, A.q_radar, A.q_ground,
                            shut, seen, on, after, pl, pic, ok ? "ok" : why);
      game_end(&G);
      frame_free(&F);
      frame_free(&A);
    }
    char id[8], title[96];
    snprintf(id, sizeof id, "S%d", j + 1);
    snprintf(title, sizeof title, "the %s's stores and exhaust seen from outside", JET_IDS[j]);
    report_item(id, title, bad ? V_FAIL : V_PASS, "%s", b);
  }
}

/* ---- the moving parts, seen from outside: control surfaces with the stick, gear and flaps on realistic ---- */
/* The colour a panel lamp is lit (its sheet text a prefix of the label, so a word cut short on a small panel
 * counts): "off", "green", "amber", "red", or "none" when no such lamp was drawn. The lamp's face is read beside
 * the letters, where only it is. */
static const char *lamp_seen(const Frame *F, const char *label) {
  for (int i = 0; i < F->nt; i++) {
    const SheetText *t = &F->t[i];
    size_t n = strlen(t->s);
    if (n < 2 || strncmp(t->s, label, n) || strchr(t->s, ' ')) continue;
    int y = (int)fy(F, (t->y0 + t->y1) / 2.0f), best = -1, sat = -1;
    for (int k = 0; k < 2; k++) {
      uint32_t c = px_at(F, (int)fx(F, k ? t->x1 + 0.5f : t->x0 - 1.5f), y);
      int r = c >> 16 & 255, g = c >> 8 & 255, bl = c & 255, mx = r > g ? (r > bl ? r : bl) : (g > bl ? g : bl);
      int mn = r < g ? (r < bl ? r : bl) : (g < bl ? g : bl);
      if (mx - mn > sat) sat = mx - mn, best = (int)c;
    }
    uint32_t c = (uint32_t)best;
    int r = c >> 16 & 255, g = c >> 8 & 255, bl = c & 255;
    if (sat < 60) return "off";
    if (g > r + 40 && g > bl + 40) return "green";
    if (r > g + 120 && r > bl + 120) return "red";
    if (r > 150 && g > 80 && g < r && bl < g) return "amber";
    return "lit, another colour";
  }
  return "none";
}

#define SF_TICKS 8
#define TAKEOFF_ROLL 200 /* pictures at full power on the runway */
static void item_moving_parts(const char *jets_only) {
  /* each panel's gear lamp */
  static const char *GEARL[NJ] = { "GEAR", "GEAR", "GEAR", "STÄLL", "GEAR", "ШАССИ", "ШАССИ", "GEAR" };
  for (int j = 0; j < NJ; j++) {
    if (jets_only && !strstr(jets_only, JET_IDS[j])) continue;
    char b[1 << 13];
    size_t u = 0;
    const char *a[] = { "--play", "1p", "--seed", "60", "--jet", JET_IDS[j], NULL };
    Game G;
    Frame F = { 0 }, A = { 0 };
    if (!game_start(&G, &RUN, a, "handling=realistic\n") || !to_chase(&G, &F)) {
      report_item("C", "moving parts", V_FAIL, "%s: no chase picture.", JET_IDS[j]);
      game_end(&G);
      frame_free(&F);
      continue;
    }
    u += (size_t)snprintf(b + u, sizeof b - u, "| part | input | on the sheet | seen | note |\n|---|---|---|---|---|\n");
    /* the gear: G up, wait, G down; the sheet's legs and the picture at the wheels, and the HUD's gear line */
    read_screen(&F);
    const Read *g0 = find_read_has("GEAR");
    char g0s[64];
    snprintf(g0s, sizeof g0s, "%s", g0 ? g0->s : "no GEAR text");
    float d0 = F.nlg ? F.lg[0].down : -1;
    const char *l0 = lamp_seen(&F, GEARL[j]);
    game_say(&G, "down g");
    step(&G, &F, 2);
    game_say(&G, "up g");
    step(&G, &F, 30); /* half way */
    read_screen(&F);
    const Read *g1 = find_read_has("GEAR");
    char g1s[64];
    snprintf(g1s, sizeof g1s, "%s", g1 ? g1->s : "no GEAR text");
    float d1 = F.nlg ? F.lg[0].down : -1;
    const char *l1 = lamp_seen(&F, GEARL[j]);
    A = F;
    memset(&F, 0, sizeof F);
    step(&G, &F, 100); /* 5 s of travel and some */
    read_screen(&F);
    const Read *g2 = find_read_has("GEAR");
    char g2s[64];
    snprintf(g2s, sizeof g2s, "%s", g2 ? g2->s : "no GEAR text");
    float d2 = F.nlg ? F.lg[0].down : -1;
    const char *l2 = lamp_seen(&F, GEARL[j]);
    /* in the air (faster than the gear may go): off up, red on its way and down; the runway run below sees the
     * green and the amber */
    bool fast = g2 && strstr(g2->s, "OVERSPEED");
    bool lamp_ok = !strcmp(l0, "off") && (fast ? !strcmp(l1, "red") && !strcmp(l2, "red") : !strcmp(l1, "amber") && !strcmp(l2, "green"));
    int wheels = 0, wchg = 0;
    for (int i = 0; i < A.nlg; i++)
      if (A.lg[i].on) {
        wheels++;
        wchg += patch_diff(&A, &F, fx(&A, A.lg[i].x), fy(&A, A.lg[i].y), px_len(&A, 0.8f, A.lg[i].z)) > 8;
      }
    bool moved = d0 != d1 || d1 != d2;
    const char *pic = shot(&A, "gear-moving", 0, 0, 0, 0);
    u += (size_t)snprintf(b + u, sizeof b - u,
                          "| landing gear (G; P2 comma) | G pressed once | down %.2f, then %.2f, then %.2f (%s) | %d of %d wheels "
                          "changed on screen as it went (the chase camera, behind and above, sees little under the jet) | [picture](%s) |\n"
                          "| gear lamp (%s) | the same, in the air | | panel: %s, %s, %s; HUD: \"%s\", \"%s\", \"%s\" | %s |\n",
                          d0, d1, d2, moved ? "it moved" : "it did not move", wchg, wheels, pic, GEARL[j], l0, l1, l2, g0s, g1s,
                          g2s, lamp_ok ? "as it should be (red: past the gear's speed)" : "NOT AS IT SHOULD BE");
    /* the padlock (B on realistic for player 1): the HUD's PADLOCK in the cockpit */
    game_say(&G, "down tab");
    step(&G, &F, 2);
    game_say(&G, "up tab");
    step(&G, &F, 6);
    game_say(&G, "down b");
    step(&G, &F, 20);
    read_screen(&F);
    bool pad = find_read_has("PADLOCK") != NULL;
    game_say(&G, "up b");
    u += (size_t)snprintf(b + u, sizeof b - u, "| padlock (B on realistic; H for P2) | B held in the cockpit | | %s | |\n",
                          pad ? "PADLOCK read on the HUD" : "no PADLOCK on the HUD (no enemy near, or not in the cockpit)");
    game_end(&G);
    frame_free(&F);
    frame_free(&A);
    /* the flaps: on the runway, where the jet starts with the take-off notch, V once to the next notch; the
     * sheet's flaps (or, on a delta with none, its elevons drooping) and the picture at their trailing edges, the
     * jet standing still */
    bool fl_ok = false, sf_ok = false;
    /* (a map with no carrier: every jet on a runway, the camera above its wings; on a deck it is level with them) */
    if (game_start(&G, &RUN, a, "handling=realistic\nstart=ground\nmap=dune-sea\n") && to_chase(&G, &F)) {
      /* control surfaces, on the runway where the jet stands still: a short pull and roll right (the keys' input
       * ramps up over a few ticks), the sheet's surfaces against the input, and the picture at each one's trailing edge
       * with the jet itself standing still on the screen. The limit of it: the camera is low behind the jet, and a
       * surface the jet's own body hides changes nothing it can see (the SY-26's canards on the retro picture, behind
       * its wing roots; the views audit sees them move from above) */
      static const char *SFN[] = { "ailerons", "elevons", "elevators", "canards", "rudders", "flaps" };
      /* the way each kind goes with a pull and a right roll, on the right of the jet: +1 the edge up (a rudder: to the right) */
      static const int SFDIR[] = { 1, 1, 1, -1, 1, 0 };
      step(&G, &F, 4);
      A = F;
      memset(&F, 0, sizeof F);
      game_say(&G, "down d");
      game_say(&G, "down s");
      step(&G, &F, SF_TICKS);
      game_say(&G, "up d");
      game_say(&G, "up s");
      /* the jet's own motion: where the sheet puts it on screen, before and after (standing, under a pixel) */
      float nzd = A.has_self && F.has_self ? hypotf(fx(&F, F.selfx - A.selfx), fy(&F, F.selfy - A.selfy)) : 99;
      int pieces[6] = { 0 }, onscr[6] = { 0 }, sfmv[6] = { 0 }, wrong[6] = { 0 }, rest_off = 0;
      float most[6] = { 0 };
      bool droop = true; /* no flaps: the elevons droop with the take-off notch, at rest too */
      for (int i = 0; i < A.ncs; i++) droop = droop && A.cs[i].kind != 5;
      for (int i = 0; i < A.ncs; i++) rest_off += fabsf(A.cs[i].deg) > 1 && A.cs[i].kind != 5 && !(droop && A.cs[i].kind == 1);
      for (int i = 0; i < F.ncs; i++) {
        const SheetSurface *c = &F.cs[i];
        if (c->kind < 0 || c->kind > 4) continue;
        pieces[c->kind]++;
        if (fabsf(c->deg) > fabsf(most[c->kind])) most[c->kind] = c->deg;
        int want = SFDIR[c->kind] * (c->kind == 0 ? c->side : 1);
        if (c->kind == 1 && c->side < 0) want = 0; /* the left elevon: the pull and the roll cancel */
        if (want && c->deg * want < 1) wrong[c->kind]++;
        if (!c->on) continue;
        onscr[c->kind]++;
        sfmv[c->kind] += nzd < 1 && patch_changed(&A, &F, fx(&F, c->x), fy(&F, c->y), px_len(&F, 0.5f, c->z)) >= 6;
      }
      sf_ok = rest_off == 0 && pieces[4] > 0 && pieces[0] + pieces[1] > 0 && pieces[1] + pieces[2] > 0;
      const char *pic_sf = shot(&F, "surfaces", 0, 0, 0, 0);
      size_t u0 = u;
      for (int k = 0; k < 5; k++) {
        if (!pieces[k]) continue;
        bool ok = !wrong[k] && sfmv[k] > 0;
        sf_ok = sf_ok && ok;
        u += (size_t)snprintf(b + u, sizeof b - u, "| %s | stick back and right, %d pictures | %d, up to %+.0f deg%s | %d of %d on screen "
                              "changed at the trailing edge (6 pixels or more; the jet moved %.1f px) | [picture](%s) |\n", SFN[k], SF_TICKS, pieces[k], most[k],
                              wrong[k] ? ", SOME THE WRONG WAY" : "", sfmv[k], onscr[k], nzd, pic_sf);
      }
      if (u == u0) u += (size_t)snprintf(b + u, sizeof b - u, "| control surfaces | stick | none | | the sheet names no surface on this jet |\n");
      if (rest_off) u += (size_t)snprintf(b + u, sizeof b - u, "| at rest | none | %d surfaces deflected with the stick centred | | |\n", rest_off);
      frame_free(&A);
      step(&G, &F, 30); /* the stick back to the middle */
      step(&G, &F, 4);
      A = F;
      memset(&F, 0, sizeof F);
      game_say(&G, "down v");
      step(&G, &F, 2);
      game_say(&G, "up v");
      step(&G, &F, 90);
      int kind = 5, n = 0, on = 0, mv = 0;
      for (int i = 0; i < F.ncs; i++) n += F.cs[i].kind == 5;
      if (!n) kind = 1; /* no flaps: the elevons */
      float f0 = 0, f1 = 0;
      for (int i = 0; i < A.ncs; i++)
        if (A.cs[i].kind == kind) f0 = fminf(f0, A.cs[i].deg);
      n = 0;
      for (int i = 0; i < F.ncs; i++) {
        const SheetSurface *c = &F.cs[i];
        if (c->kind != kind) continue;
        n++, f1 = fminf(f1, c->deg);
        if (!c->on) continue;
        on++;
        mv += patch_changed(&A, &F, fx(&F, c->x), fy(&F, c->y), px_len(&F, 0.5f, c->z)) >= 6;
      }
      fl_ok = n > 0 && fabsf(f1 - f0) > 3 && mv > 0;
      /* the gear lamp on the runway (down, standing: green), then a take-off, the gear raised well under its
       * speed: amber on its way, off once up */
      const char *r0 = lamp_seen(&A, GEARL[j]), *r1, *r2;
      game_say(&G, "down r");
      step(&G, &F, TAKEOFF_ROLL);
      game_say(&G, "down s"); /* past the rotation speed by then: the nose up, and off */
      step(&G, &F, 40);
      game_say(&G, "up s");
      game_say(&G, "up r");
      game_say(&G, "down f"); /* the power back, so the gear goes up well under its speed */
      step(&G, &F, 20);
      game_say(&G, "up f");
      read_screen(&F);
      const Read *hs = find_read_has("OVERSPEED");
      game_say(&G, "down g");
      step(&G, &F, 2);
      game_say(&G, "up g");
      step(&G, &F, 30);
      read_screen(&F);
      r1 = lamp_seen(&F, GEARL[j]); /* (both read below, after the steps) */
      if (!hs) hs = find_read_has("OVERSPEED");
      step(&G, &F, 100);
      r2 = lamp_seen(&F, GEARL[j]);
      bool rl = !strcmp(r0, "green") && (!strcmp(r1, "amber") || (hs && !strcmp(r1, "red"))) && !strcmp(r2, "off");
      lamp_ok = lamp_ok && rl;
      u += (size_t)snprintf(b + u, sizeof b - u, "| gear lamp (%s) | on the runway; then full power, off, G | | panel: %s on the ground, "
                            "%s on its way up%s, %s once up | %s |\n", GEARL[j], r0, r1, hs ? " (GEAR OVERSPEED on the HUD)" : "", r2,
                            rl ? "green down, amber on its way, off up" : "NOT AS IT SHOULD BE");
      u += (size_t)snprintf(b + u, sizeof b - u, "| %s (V; P2 period) | on the runway, take-off notch, V once | %d, %.0f deg, then "
                            "%.0f deg | %d of %d on screen changed at the trailing edge | [picture](%s) |\n",
                            kind == 5 ? "flaps" : "flaps: the elevons droop (no flaps of its own)", n, f0, f1, mv, on,
                            shot(&F, "flaps", 0, 0, 0, 0));
    } else u += (size_t)snprintf(b + u, sizeof b - u, "| flaps | | | no chase picture on the runway | |\n");
    game_end(&G);
    frame_free(&F);
    frame_free(&A);
    char id[8], title[96];
    snprintf(id, sizeof id, "C%d", j + 1);
    snprintf(title, sizeof title, "the %s's moving parts", JET_IDS[j]);
    report_item(id, title, moved && lamp_ok && sf_ok && fl_ok ? V_PASS : V_FAIL, "%s", b);
  }
}

static void item5(void) {
  Game G;
  Frame F = { 0 }, P = { 0 };
  const char *a[] = { NULL };
  Run R = RUN;
  R.every = 10;
  if (!game_start(&G, &R, a, "intro=1\n")) return;
  int moving = 0, blank = 0, pics = 0;
  bool menu = false;
  long skip_at = -1, menu_at = -1;
  while (game_read(&G, &F, 30000)) {
    if (!strcmp(F.state, "intro")) {
      pics++;
      blank += see_blank(&F);
      if (P.px && P.w == F.w) {
        int x0, y0, x1, y1;
        moving += see_diff(&P, &F, &x0, &y0, &x1, &y1) > 0.002;
      }
      Frame t = P;
      P = F;
      F = t;
      if (pics == 60) game_say(&G, "down space"), skip_at = P.frame;
      if (pics == 61) game_say(&G, "up space");
    } else if (!strcmp(F.state, "menu")) {
      read_screen(&F);
      if (find_read("START MISSION")) {
        menu = true;
        menu_at = F.frame;
        break;
      }
    }
    if (pics > 200) break;
    game_go(&G);
  }
  const char *pic = shot(menu ? &F : &P, "intro", 0, 0, 0, 0);
  game_end(&G);
  bool ok = menu && moving > pics / 2 && blank < pics / 4;
  report_item("5", "the intro plays and can be skipped", ok ? V_PASS : V_FAIL,
              "%d intro pictures (one each 1/6 s): %d moving on from the one before, %d blank. Space pressed at frame %ld: "
              "%s%s. [picture](%s)",
              pics, moving, blank, skip_at, menu ? "the menu came up and read START MISSION" : "no menu",
              menu ? "" : "", pic);
  if (menu) report_line("\nThe menu came %.2f s after the key.\n", (menu_at - skip_at) / 60.0);
  frame_free(&F);
  frame_free(&P);
}

static void item6(void) {
  static const char *REN[] = { "modern", "retro" }, *GFX[] = { "low", "medium", "high", "ultra" };
  char b[8192];
  size_t u = 0;
  int bad = 0;
  u += (size_t)snprintf(b + u, sizeof b - u, "| renderer | graphics | drawn by (log) | picture | texts read | horizon |\n|---|---|---|---|---|---|\n");
  for (int r = 0; r < 2; r++)
    for (int g = 0; g < 4; g++) {
      Run R = RUN;
      R.renderer = REN[r];
      R.gfx = GFX[g];
      Game G;
      Frame F = { 0 };
      const char *a[] = { "--play", "1p", "--seed", "15", NULL };
      if (!game_start(&G, &R, a, "")) continue;
      bool okr = false;
      for (int i = 0; i < 80 && (okr = game_read(&G, &F, 30000)); i++) game_go(&G);
      int texts = 0, good = 0;
      char missed[256] = "";
      bool hor = false, blank = true;
      if (okr) {
        read_screen(&F);
        for (int i = 0; i < F.nt; i++) {
          const SheetText *t = &F.t[i];
          char s[64];
          norm(t->s, s, sizeof s);
          if (!s[0] || !strcmp(s, " ")) continue;
          const Read *best;
          texts++;
          bool got = read_back(&F, (int)fx(&F, (float)t->x0), (int)fy(&F, (float)t->y0), (int)fx(&F, (float)t->x1),
                               (int)fy(&F, (float)t->y1), s, &best);
          good += got;
          if (!got && strlen(missed) + strlen(s) + 4 < sizeof missed) /* the ones not read back, for the report */
            snprintf(missed + strlen(missed), sizeof missed - strlen(missed), "%s\"%s\" (read \"%s\")", missed[0] ? ", " : "", s,
                     best ? best->s : "nothing");
        }
        blank = see_blank(&F);
      }
      /* the horizon: over the next pictures, in the band where the camera puts it (clouds may hide much of it) */
      for (int i = 0; okr && !hor && i < 40; i++) {
        if (F.has_v && fabsf(F.u[1]) > 0.35f) {
          float ey0 = F.cy + (F.r[1] * (F.vx - F.cx) + F.f[1] * F.F) / F.u[1];
          float ey1 = F.cy + (F.r[1] * (F.vx + F.vw - F.cx) + F.f[1] * F.F) / F.u[1];
          float lo = fminf(ey0, ey1) - 40, hi = fmaxf(ey0, ey1) + 40, aa, bb, q;
          if (lo < F.vy) lo = F.vy;
          if (hi > F.vy + F.sh) hi = F.vy + F.sh;
          if (hi - lo > 30) {
            float gb = (fy(&F, ey1) - fy(&F, ey0)) / (fx(&F, F.vx + F.vw) - fx(&F, F.vx)), ga = fy(&F, ey0) - gb * fx(&F, F.vx);
            see_horizon_near(&F, (int)fx(&F, F.vx), (int)fy(&F, F.vy), (int)fx(&F, F.vx + F.vw), (int)fy(&F, F.vy + F.sh), ga, gb,
                             fy(&F, F.sh) * 0.25f, 10, F.u[1] > 0, &aa, &bb, &q);
            float mx = fx(&F, F.vx + F.vw / 2), ry = aa + bb * mx, wy_ = fy(&F, (ey0 + ey1) / 2);
            hor = q >= 0.25f && fabsf(ry - wy_) < 0.04f * fy(&F, F.sh);
          }
        }
        if (!hor) {
          game_go(&G);
          if (!game_read(&G, &F, 30000)) break;
        }
      }
      char pic[160];
      snprintf(pic, sizeof pic, "%s", okr ? shot(&F, "renderer", 0, 0, 0, 0) : "");
      game_end(&G);
      /* which renderer the log names */
      char who[64] = "?", line[512];
      FILE *lf = fopen(G.log, "r");
      while (lf && fgets(line, sizeof line, lf)) { /* the game's start line: "..., video X, renderer Y" */
        const char *k = strstr(line, ", renderer ");
        if (k && who[0] == '?') sscanf(k + 11, "%63s", who);
      }
      if (lf) fclose(lf);
      bool ok = okr && !blank && good >= texts * 0.9 && hor;
      bad += !ok;
      u += (size_t)snprintf(b + u, sizeof b - u, "| %s | %s | %s | %s | %s | %s |\n", REN[r], GFX[g], who,
                            okr ? (blank ? "BLANK" : "drawn") : "none", pct(good, texts), hor ? "found" : "NOT FOUND");
      if (pic[0]) u += (size_t)snprintf(b + u, sizeof b - u, "|  | [picture](%s) | | | not read: %s | |\n", pic, missed[0] ? missed : "none");
      frame_free(&F);
    }
  report_item("6", "both renderers, every graphics level", bad ? V_FAIL : V_PASS,
              "%s\nWithout a screen the modern renderer may fall back to the software one: run with LP_DISPLAY=1 on a desktop for the GPU.", b);
}

static void item7(void) {
  /* change a setting by clicking it as read on the settings page, quit, start again, read it back */
  Game G;
  Frame F = { 0 };
  const char *a[] = { NULL };
  if (!to_menu(&G, &F, "", a) || !click_text(&G, &F, "SETTINGS")) {
    report_item("7", "a changed setting survives a restart", V_FAIL, "Could not open the settings page.");
    return;
  }
  step(&G, &F, 10);
  read_screen(&F);
  Read before[256];
  int nb = NRD < 256 ? NRD : 256;
  memcpy(before, RD, (size_t)nb * sizeof *RD);
  char changed_from[64] = "", changed_to[64] = "";
  int cx = 0, cy = 0;
  for (int i = 0; i < nb && !changed_to[0]; i++) {
    const Read *r = &before[i];
    if (r->x0 < F.w / 2 || strlen(r->s) < 2) continue; /* the values sit on the right */
    game_say(&G, "click %.0f %.0f", wx(&F, (r->x0 + r->x1) / 2.0f), wy(&F, (r->y0 + r->y1) / 2.0f));
    step(&G, &F, 6);
    read_screen(&F);
    for (int k = 0; k < NRD; k++)
      if (abs(RD[k].y0 - r->y0) <= 2 && RD[k].x1 > F.w / 2 && strcmp(RD[k].s, r->s)) {
        snprintf(changed_from, sizeof changed_from, "%s", r->s);
        snprintf(changed_to, sizeof changed_to, "%s", RD[k].s);
        cx = (RD[k].x0 + RD[k].x1) / 2, cy = (RD[k].y0 + RD[k].y1) / 2;
        break;
      }
  }
  const char *pic1 = shot(&F, "setting-changed", 0, 0, 0, 0);
  game_say(&G, "event quit");
  game_go(&G);
  game_wait(&G, 5000);
  game_end(&G);
  if (!changed_to[0]) {
    report_item("7", "a changed setting survives a restart", V_FAIL, "No value on the settings page changed when clicked. [page](%s)", pic1);
    return;
  }
  /* start again with the same settings folder */
  if (!to_menu(&G, &F, NULL, a) || !click_text(&G, &F, "SETTINGS")) {
    report_item("7", "a changed setting survives a restart", V_FAIL, "The game did not come back to its settings page.");
    return;
  }
  step(&G, &F, 10);
  read_screen(&F);
  const char *now = "";
  for (int k = 0; k < NRD; k++)
    if (abs((RD[k].y0 + RD[k].y1) / 2 - cy) <= 3 && abs((RD[k].x0 + RD[k].x1) / 2 - cx) < 80) now = RD[k].s;
  const char *pic2 = shot(&F, "setting-after-restart", 0, 0, 0, 0);
  game_end(&G);
  bool ok = !strcmp(now, changed_to);
  report_item("7", "a changed setting survives a restart", ok ? V_PASS : V_FAIL,
              "Clicked `%s`: it read `%s` ([picture](%s)). After quitting and starting again it reads `%s` ([picture](%s)).",
              changed_from, changed_to, pic1, now, pic2);
  frame_free(&F);
}

static void item8(void) {
  char b[1024];
  size_t u = 0;
  int bad = 0;
  for (int how = 0; how < 2; how++) {
    Game G;
    Frame F = { 0 };
    const char *a[] = { NULL };
    if (!to_menu(&G, &F, "", a)) {
      bad++;
      continue;
    }
    if (how == 0) {
      if (!click_text(&G, &F, "EXIT")) {
        u += (size_t)snprintf(b + u, sizeof b - u, "- EXIT is not on the menu\n");
        bad++;
        game_end(&G);
        continue;
      }
    } else game_say(&G, "event quit");
    game_go(&G);
    bool gone = game_wait(&G, 6000);
    u += (size_t)snprintf(b + u, sizeof b - u, "- %s: %s\n", how ? "the window's close button" : "EXIT clicked where it was read",
                          gone ? (G.status == 0 ? "the game ended cleanly (exit 0)" : "the game ended with an error") : "the game did not end");
    if (!gone || G.status != 0) bad++;
    game_end(&G);
    frame_free(&F);
  }
  report_item("8", "quit from the menu and the close button", bad ? V_FAIL : V_PASS, "%sCmd+Q is macOS only: run there.", b);
}

static void item12(void) {
  Game G;
  Frame F = { 0 }, P = { 0 };
  const char *a[] = { "--play", "1p", "--seed", "16", NULL };
  if (!game_start(&G, &RUN, a, "")) return;
  step(&G, &F, 60);
  game_say(&G, "event hide");
  bool alive = step(&G, &F, 40);
  read_screen(&F);
  bool paused = find_read("PAUSED") != NULL || !strcmp(F.state, "pause");
  game_say(&G, "event show");
  alive = alive && step(&G, &F, 5);
  if (paused) {
    game_say(&G, "down esc");
    step(&G, &F, 1);
    game_say(&G, "up esc");
  }
  step(&G, &F, 10);
  P = F;
  memset(&F, 0, sizeof F);
  bool moves = step(&G, &F, 10);
  int x0, y0, x1, y1;
  double d = moves ? see_diff(&P, &F, &x0, &y0, &x1, &y1) : 0;
  const char *pic = shot(&F, "after-minimise", 0, 0, 0, 0);
  game_end(&G);
  bool ok = alive && d > 0.002 && !strcmp(F.state, "play");
  report_item("12", "minimised mid-match", ok ? V_PASS : V_FAIL,
              "Hidden for 40 pictures: the game %s%s; shown again%s, the match %s (%.1f %% of the picture moved in 10 pictures). [picture](%s)",
              alive ? "kept answering" : "stopped answering", paused ? " and showed PAUSED" : ", no PAUSED on screen",
              paused ? " and Esc pressed" : "", ok ? "carries on" : "is stuck", d * 100, pic);
  frame_free(&F);
  frame_free(&P);
}

static void item16(void) {
  Game G;
  Frame F = { 0 };
  const char *a[] = { NULL };
  if (!to_menu(&G, &F, "sound=0\n", a)) return;
  char ini[700], before[4096] = "", after[4096] = "";
  snprintf(ini, sizeof ini, "%s/tailonlock/settings.ini", RUN.cfg);
  FILE *f = fopen(ini, "r");
  if (f) before[fread(before, 1, sizeof before - 1, f)] = 0, fclose(f);
  game_kill(&G, SIGKILL);
  game_wait(&G, 3000);
  game_end(&G);
  bool menu = to_menu(&G, &F, NULL, a);
  const char *pic = shot(&F, "after-kill", 0, 0, 0, 0);
  game_end(&G);
  f = fopen(ini, "r");
  if (f) after[fread(after, 1, sizeof after - 1, f)] = 0, fclose(f);
  bool same = !strcmp(before, after);
  report_item("16", "killed and started again", menu && same ? V_PASS : V_FAIL,
              "Killed on the menu; started again: %s; the settings file %s. [picture](%s)",
              menu ? "the menu came up and read START MISSION" : "no menu", same ? "is the same" : "CHANGED", pic);
  frame_free(&F);
}

static void item17(void) {
  /* every screen at 1280x800: menu, hangar, settings, network, a match, the pause page */
  int ow = WIN_W, oh = WIN_H;
  const char *oldw = RUN.window;
  WIN_W = 1280, WIN_H = 800;
  RUN.window = "1280x800";
  static const char *PAGES[] = { NULL, "HANGAR", "SETTINGS", "NETWORK" };
  for (size_t p = 0; p < 4; p++) {
    Game G;
    Frame F = { 0 };
    const char *a[] = { NULL };
    if (!to_menu(&G, &F, "", a)) continue;
    if (PAGES[p] && click_text(&G, &F, PAGES[p])) step(&G, &F, 10);
    for (int i = 0; i < 6; i++) {
      read_screen(&F);
      audit_texts(&F, PAGES[p] ? PAGES[p] : "menu");
      step(&G, &F, 2);
    }
    game_end(&G);
    frame_free(&F);
  }
  Game G;
  Frame F = { 0 };
  const char *a[] = { "--play", "1p", "--seed", "17", NULL };
  if (game_start(&G, &RUN, a, "")) {
    for (int i = 0; i < 120 && step(&G, &F, 1); i++) {
      read_screen(&F);
      if (i % 4 == 0) audit_texts(&F, "play");
    }
    game_say(&G, "down esc");
    step(&G, &F, 1);
    game_say(&G, "up esc");
    for (int i = 0; i < 6 && step(&G, &F, 2); i++) {
      read_screen(&F);
      audit_texts(&F, "pause");
    }
    game_end(&G);
    frame_free(&F);
  }
  report_texts("17", "text readable at 1280x800 (menus, hangar, settings, network, flight, pause)", 3);
  WIN_W = ow, WIN_H = oh;
  RUN.window = oldw;
}

/* LAN: a host and a guest on this computer, both played from their screens */
static void item_net(void) {
  Game H, J;
  Frame FH = { 0 }, FJ = { 0 };
  const char *ah[] = { "--host", NULL };
  Run RH = RUN;
  char cfgh[600], cfgj[600];
  snprintf(cfgh, sizeof cfgh, "%s/host", RUN.cfg);
  snprintf(cfgj, sizeof cfgj, "%s/guest", RUN.cfg);
  RH.cfg = cfgh;
  if (!game_start(&H, &RH, ah, "")) return;
  /* the code as the host's screen shows it */
  char code[16] = "";
  for (int i = 0; i < 300 && !code[0] && game_read(&H, &FH, 30000); i++) {
    read_screen(&FH);
    for (int k = 0; k < NRD && !code[0]; k++) {
      const char *s = RD[k].s;
      for (const char *q = s; *q; q++)
        if (match(q, "######*") && (q == s || !isdigit((unsigned char)q[-1]))) {
          memcpy(code, q, 6), code[6] = 0;
          break;
        }
    }
    game_go(&H);
  }
  const char *hostpic = shot(&FH, "host", 0, 0, 0, 0);
  if (!code[0]) {
    report_item("13", "LAN: host, join with the code, play", V_FAIL, "No six-digit code on the host's screen. [host](%s)", hostpic);
    game_end(&H);
    return;
  }
  /* a wrong code first */
  char wrong[8];
  snprintf(wrong, sizeof wrong, "%06d", ((int)safe_int(code, 0, INT_MIN, INT_MAX) + 111111) % 1000000);
  Run RJ = RUN;
  RJ.cfg = cfgj;
  const char *aj[] = { "--join", "127.0.0.1", "--code", wrong, NULL };
  bool refused = false;
  char said[64] = "";
  if (game_start(&J, &RJ, aj, "")) {
    for (int i = 0; i < 400 && !refused; i++) {
      game_go(&H);
      if (!game_read(&H, &FH, 30000)) break;
      if (!game_read(&J, &FJ, 30000)) break;
      read_screen(&FJ);
      const Read *r = find_read_has("CODE");
      if (!r) r = find_read_has("REFUSED");
      if (!r) r = find_read_has("WRONG");
      if (r && strcmp(FJ.state, "play")) refused = true, snprintf(said, sizeof said, "%s", r->s);
      game_go(&J);
    }
    report_item("14", "a wrong code is refused with a message", refused ? V_PASS : V_FAIL, "Joined with %s (the host shows %s): %s. [guest](%s)",
                wrong, code, refused ? said : "no message read", shot(&FJ, "wrong-code", 0, 0, 0, 0));
    game_end(&J);
  }
  /* the right code */
  const char *aj2[] = { "--join", "127.0.0.1", "--code", code, NULL };
  bool playing = false;
  if (game_start(&J, &RJ, aj2, NULL)) {
    for (int i = 0; i < 600; i++) {
      game_go(&H);
      if (!game_read(&H, &FH, 30000) || !game_read(&J, &FJ, 30000)) break;
      if (!strcmp(FH.state, "play") && !strcmp(FJ.state, "play")) {
        playing = true;
        break;
      }
      game_go(&J);
    }
    /* play a while, then the host goes silent as if its cable were pulled */
    for (int i = 0; playing && i < 200; i++) {
      game_go(&H);
      game_go(&J);
      if (!game_read(&H, &FH, 30000) || !game_read(&J, &FJ, 30000)) break;
    }
    report_item("13", "LAN: host, join with the code, play", playing ? V_PASS : V_FAIL,
                "The host's screen showed code %s ([host](%s)); the guest joined with it and both screens went to the match: %s. "
                "Rematch not tried yet.",
                code, hostpic, playing ? "yes" : "no");
    if (playing) {
      game_kill(&H, SIGSTOP);
      bool lost = false;
      struct timespec t0, t1;
      clock_gettime(CLOCK_MONOTONIC, &t0);
      long f0 = FJ.frame;
      for (int i = 0; i < 900 && !lost; i++) {
        game_go(&J);
        if (!game_read(&J, &FJ, 30000)) break;
        read_screen(&FJ);
        lost = find_read_has("CONNECTION LOST") != NULL;
      }
      clock_gettime(CLOCK_MONOTONIC, &t1);
      double wall = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
      report_item("15", "the other side gone mid-match: CONNECTION LOST", lost ? V_PASS : V_FAIL,
                  "The host frozen; the guest's screen %s after %.1f s (%ld frames of game time). [guest](%s)",
                  lost ? "read CONNECTION LOST" : "never read CONNECTION LOST", wall, FJ.frame - f0, shot(&FJ, "lost", 0, 0, 0, 0));
      game_kill(&H, SIGCONT);
    }
    game_end(&J);
  }
  game_end(&H);
  frame_free(&FH);
  frame_free(&FJ);
}

int main(int argc, char **argv) {
  const char *only = NULL, *jets = "all";
  bool net = false;
  RUN.every = 4;
  RUN.window = "1280x720";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--game") && i + 1 < argc) RUN.exe = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) snprintf(OUT, sizeof OUT, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--window") && i + 1 < argc) RUN.window = argv[++i];
    else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) RUN.renderer = argv[++i];
    else if (!strcmp(argv[i], "--jets") && i + 1 < argc) jets = argv[++i];
    else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds_per_jet = safe_num(argv[++i], 0);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)(int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
    else if (!strcmp(argv[i], "--net")) net = true;
    else {
      fprintf(stderr, "usage: livepilot --game EXE [--out DIR] [--only 1,2,3,5,6,7,8,12,13,16,17,K,W,S,C,F] [--window WxH]\n"
                      "                 [--renderer modern|retro] [--jets all|ID,...] [--seconds S] [--seed N] [--net]\n");
      return 2;
    }
  }
  if (!RUN.exe) {
    fprintf(stderr, "livepilot: --game EXE is needed\n");
    return 2;
  }
  sscanf(RUN.window, "%dx%d", &WIN_W, &WIN_H);
  char d[1700];
  snprintf(d, sizeof d, "mkdir -p '%s/shots' '%s/logs' '%s/cfg'", OUT, OUT, OUT);
  if (system(d)) return 2;
  static char cfg[600];
  snprintf(cfg, sizeof cfg, "%s/cfg", OUT);
  RUN.cfg = cfg;
  signal(SIGPIPE, SIG_IGN);
  see_init();
  time_t t0 = time(NULL);

  report_line("\n## The hands-on checklist, from the screen\n");
  if (want(only, "1")) item1();
  if (want(only, "2")) item2();
  if (want(only, "3")) item3();
  if (want(only, "4")) report_item("4", "music and sound", V_SKIP, "Not on the screen: tools/handson item 4 listens to the mix.");
  if (want(only, "5")) item5();
  if (want(only, "6")) item6();
  if (want(only, "7")) item7();
  if (want(only, "8")) item8();
  if (want(only, "9")) report_item("9", "a gamepad", V_SKIP, "Needs a pad plugged in by hand.");
  if (want(only, "10")) report_item("10", "HOTAS", V_SKIP, "Not supported yet.");
  if (want(only, "11")) report_item("11", "a gamepad unplugged mid-match", V_SKIP, "Needs a pad unplugged by hand.");
  if (want(only, "12")) item12();
  if (net && (want(only, "13") || want(only, "14") || want(only, "15"))) item_net();
  else if (want(only, "13")) report_item("13", "LAN host and join", V_SKIP, "Run with --net (a host and a guest on this computer, on a port of the run's own).");
  if (want(only, "16")) item16();
  if (want(only, "17")) item17();

  report_line("\n## Weapons and keys in every jet\n");
  if (want(only, "W")) item_weapons(strcmp(jets, "all") ? jets : NULL);

  report_line("\n## The jets seen from outside\n");
  if (want(only, "S")) item_stores_outside(strcmp(jets, "all") ? jets : NULL);
  if (want(only, "C")) item_moving_parts(strcmp(jets, "all") ? jets : NULL);

  report_line("\n## Flying and fighting from the cockpit\n");
  if (want(only, "F"))
    for (int j = 0; j < NJ; j++) {
      if (strcmp(jets, "all") && !strstr(jets, JET_IDS[j])) continue;
      Cock C;
      fly(JET_IDS[j], "scenario=dogfight\n", seconds_per_jet, &C);
      char id[8];
      snprintf(id, sizeof id, "F%d", j + 1);
      report_cockpit(id, JET_IDS[j], &C);
      if (NTS) {
        snprintf(id, sizeof id, "T%d", j + 1);
        char t[96];
        snprintf(t, sizeof t, "every text in the %s's cockpit reads back", JET_IDS[j]);
        report_texts(id, t, 3);
      }
    }

  char path[700];
  snprintf(path, sizeof path, "%s/report.md", OUT);
  rep = fopen(path, "w");
  if (rep) {
    fprintf(rep, "# Live test player report\n\nThe player saw only the game's pictures (%s window, one picture each %d frames) and "
                 "played with the keyboard and mouse. Ran %ld s. %d item%s failed.\n\n| item | check | result |\n|---|---|---|\n%s\n%s",
            RUN.window, RUN.every, (long)(time(NULL) - t0), fails, fails == 1 ? "" : "s", summary, body);
    fclose(rep);
  }
  printf("report: %s (%d failed)\n", path, fails);
  return fails ? 1 : 0;
}
