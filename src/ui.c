/* ui.c - main menu, pause and game over (JS #menu, #pause and #over
 * overlays, syncMenu, showOver; lines 99-198 and 3682-3752), rebuilt as
 * in-game screens driven by keys and gamepad. */
#include "ui.h"
#include "brand.h"

#include "assets.h"
#include "core/jets.h"
#include "gpu.h"
#include "intro.h"
#include "music.h"
#include "net.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define C_EDGE     0x8a93b8 /* the glass panel's edge and the buttons' outline */
#define C_SHADOW   0x0a0c18 /* behind the titles */
#define C_SELECT   0xffffff /* the selected row's band, drawn faint */
#define C_INK      0xcdc6ff
#define C_DIM      0xa49be6
#define C_LAMP     0xB8C76F
#define C_LAMP_INK 0x1b1640

/* The menu has three pages: the match (with the buttons), the settings,
 * and the hangar where each seat picks its jet and arms it. */
enum {
  ROW_MODE, ROW_MATCH, ROW_SIZE, ROW_STARTAT, ROW_JET1, ROW_JET2, ROW_MAP, ROW_TOD, ROW_WX, ROW_DIFF, ROW_RULES, ROW_HANDLING,
  ROW_SCEN, ROW_LIVES, ROW_CRASH, ROW_HITS,
  ROW_GFORCE, ROW_RENDER, ROW_GFX, ROW_INVERT, ROW_SOUND, ROW_MUSIC, ROW_THEME, ROW_INTRO,
  ROW_HSEAT, ROW_HJET, ROW_HEAT, ROW_RADAR, ROW_AGM,
  ROW_NROLE, ROW_NMATCH, ROW_NVIA, ROW_NGAME, ROW_NADDR, ROW_NRELAY, ROW_NCODE, ROW_NNAME,
  ROW_START, ROW_HANGAR, ROW_SETTINGS, ROW_NETWORK, ROW_WATCH, ROW_EDITOR, ROW_EXIT, ROW_BACK, ROW_DONE, ROW_NGO, ROW_NBACK, ROWS
};
enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_HANGAR, PAGE_NET };
static int sel = ROW_START;
static int sel2;
static int page, hseat, lan_sel;

static int row_page(int r) {
  if ((r >= ROW_NROLE && r <= ROW_NNAME) || r == ROW_NGO || r == ROW_NBACK) return PAGE_NET;
  if (r >= ROW_GFORCE && r <= ROW_INTRO) return PAGE_SETTINGS;
  if ((r >= ROW_HSEAT && r <= ROW_AGM) || r == ROW_DONE) return PAGE_HANGAR;
  if (r == ROW_BACK) return PAGE_SETTINGS;
  return PAGE_MAIN;
}

static bool row_visible(const Settings *st, int r) {
  if (row_page(r) != page) return false;
  if (r == ROW_MATCH) return st->mode != MODE_1P;
  if (r == ROW_SIZE) return st->mode != MODE_DEFEND && !(st->mode == MODE_2P && st->coop);
  if (r == ROW_STARTAT) return st->mode != MODE_DEFEND;
  if (r == ROW_JET2) return st->mode == MODE_2P || (st->mode == MODE_DEFEND && st->commander);
  if (r == ROW_DIFF) return st->mode == MODE_1P || st->mode == MODE_DEFEND || (st->mode == MODE_2P && (st->coop || st->pairs));
  if (r == ROW_MAP) return assets_map_count() > 0;
  if (r == ROW_TOD || r == ROW_WX) return st->map >= 0;
  if (r >= ROW_SCEN && r <= ROW_HITS) return st->classic && st->mode != MODE_DEFEND;
  if (r == ROW_INTRO || r == ROW_WATCH) return intro_available();
  if (r == ROW_EDITOR) return false; /* the music editor is a development tool now (--music-editor) */
  if (r == ROW_GFX) return gpu_active();
  if (r == ROW_MUSIC) return st->sound;
  if (r == ROW_THEME) return st->sound && st->music;
  if (r == ROW_HSEAT) return st->mode == MODE_2P || (st->mode == MODE_DEFEND && st->commander);
  if (r == ROW_NMATCH) return !st->net_join;
  if (r == ROW_NGAME) return st->net_join && st->net_via == NV_LAN;
  if (r == ROW_NADDR) return st->net_join && st->net_via == NV_ADDRESS;
  if (r == ROW_NRELAY) return st->net_via == NV_RELAY;
  if (r == ROW_NCODE) return st->net_join;
  return true;
}

void ui_open(Screen s) {
  if (s == SCREEN_MENU) {
    sel = ROW_START;
    page = PAGE_MAIN;
  }
  sel2 = 0;
}

static void go_page(int pg, int row) {
  page = pg;
  sel = row;
}

void ui_open_hangar(void) {
  hseat = 0;
  go_page(PAGE_HANGAR, ROW_HJET);
}

int ui_hangar_seat(void) { return hseat; }

bool ui_net_page(void) { return page == PAGE_NET; }

void ui_open_net(void) { go_page(PAGE_NET, ROW_NGO); }

const char *ui_net_lan_addr(void) { return lan_sel < net_lan_count() ? net_lan_addr(lan_sel) : ""; }

char *ui_text_field(Settings *st, int *size) {
  if (page != PAGE_NET || net_active()) return NULL;
  switch (sel) {
  case ROW_NADDR:  *size = (int)sizeof st->net_addr; return st->net_addr;
  case ROW_NRELAY: *size = (int)sizeof st->net_relay; return st->net_relay;
  case ROW_NCODE:  *size = 7; return st->net_code;
  case ROW_NNAME:  *size = (int)sizeof st->net_name; return st->net_name;
  default: return NULL;
  }
}

void ui_text(Settings *st, const char *utf8) {
  int size;
  char *f = ui_text_field(st, &size);
  if (!f) return;
  int n = (int)strlen(f);
  if (!strcmp(utf8, "\b")) {
    if (n) f[n - 1] = 0;
    return;
  }
  for (const char *c = utf8; *c && n < size - 1; c++) {
    char ch = *c;
    if (sel == ROW_NNAME) ch = (char)SDL_toupper(ch);
    bool ok = sel == ROW_NCODE ? (ch >= '0' && ch <= '9') : sel == ROW_NNAME ? (ch > ' ' && ch < 127)
                                                                      : (SDL_isalnum(ch) || ch == '.' || ch == ':' || ch == '-' || ch == '[' || ch == ']');
    if (ok) {
      f[n++] = ch;
      f[n] = 0;
    }
  }
}

int ui_hangar_jet(const Settings *st) { return page == PAGE_HANGAR ? st->jets[hseat] : -1; }


/* Changes one store of the seat's arming by d, if the result still fits. */
static bool arm_step(Settings *st, int k, int d) {
  int l[3];
  memcpy(l, st->load[hseat], sizeof l);
  l[k] += d;
  if (!game_load_fits(l)) return false;
  memcpy(st->load[hseat], l, sizeof l);
  return true;
}

UiAction ui_nav(Screen s, Settings *st, Nav n) {
  if (s != SCREEN_MENU) {
    if (n == NAV_UP || n == NAV_DOWN || n == NAV_LEFT || n == NAV_RIGHT) { sel2 = 1 - sel2; return UI_NONE; }
    if (n == NAV_BACK) return s == SCREEN_PAUSE ? UI_RESUME : UI_TO_MENU;
    if (n == NAV_OK) {
      if (s == SCREEN_PAUSE) return sel2 ? UI_TO_MENU : UI_RESUME;
      return sel2 ? UI_TO_MENU : UI_START;
    }
    return UI_NONE;
  }
  if (n == NAV_UP || n == NAV_DOWN) {
    int d = n == NAV_UP ? -1 : 1;
    do sel = (sel + d + ROWS) % ROWS;
    while (!row_visible(st, sel));
    return UI_NONE;
  }
  if (n == NAV_BACK) {
    if (page == PAGE_MAIN) return UI_QUIT;
    if (page == PAGE_NET && net_active()) return UI_NET_CANCEL;
    go_page(PAGE_MAIN, page == PAGE_HANGAR ? ROW_HANGAR : page == PAGE_NET ? ROW_NETWORK : ROW_SETTINGS);
    return UI_CHANGED;
  }
  int d = n == NAV_LEFT ? -1 : 1;
  bool change = n == NAV_LEFT || n == NAV_RIGHT || n == NAV_OK;
  switch (sel) {
  case ROW_MODE:
    if (change) st->mode = (GameMode)(st->mode == MODE_1P ? (d > 0 ? MODE_2P : MODE_DEFEND)
                                      : st->mode == MODE_2P ? (d > 0 ? MODE_DEFEND : MODE_1P) : (d > 0 ? MODE_1P : MODE_2P));
    break;
  case ROW_MATCH:
    if (!change) break;
    if (st->mode == MODE_DEFEND) st->commander = !st->commander;
    else st->coop = !st->coop;
    break;
  case ROW_SIZE:    if (change) st->pairs = !st->pairs; break;
  case ROW_STARTAT: if (change) st->ground = !st->ground; break;
  case ROW_JET1:   if (change) st->jets[0] = (st->jets[0] + d + NJETS) % NJETS; break;
  case ROW_JET2:   if (change) st->jets[1] = (st->jets[1] + d + NJETS) % NJETS; break;
  case ROW_MAP:
    if (!change) break;
    st->map += d;
    if (st->map >= assets_map_count()) st->map = -1;
    if (st->map < -1) st->map = assets_map_count() - 1;
    return UI_MAP;
  case ROW_TOD:    if (change) st->tod = (st->tod + d + TOD_COUNT) % TOD_COUNT; return change ? UI_MAP : UI_NONE;
  case ROW_WX:
    if (!change) break;
    st->weather += d;
    if (st->weather >= WX_COUNT) st->weather = -1;
    if (st->weather < -1) st->weather = WX_COUNT - 1;
    return UI_MAP;
  case ROW_DIFF:   if (change) st->diff = (Difficulty)((st->diff + d + 3) % 3); break;
  case ROW_RULES:  if (change) st->classic = !st->classic; break;
  case ROW_HANDLING: if (change) st->realistic = !st->realistic; break;
  case ROW_SCEN:   if (change) st->dogfight = !st->dogfight; break;
  case ROW_LIVES:  if (change) st->lives = (st->lives - 1 + d + 20) % 20 + 1; break;
  case ROW_CRASH:  if (change) st->crash = !st->crash; break;
  case ROW_HITS:   if (change) st->hits = (st->hits - 1 + d + 3) % 3 + 1; break;
  case ROW_INVERT: if (change) st->invert = !st->invert; break;
  case ROW_GFORCE: if (change) st->gforce = !st->gforce; break;
  case ROW_RENDER: if (change) st->modern = !st->modern; break;
  case ROW_GFX:    if (change) st->gfx = (st->gfx + d + 4) % 4; return change ? UI_GFX : UI_NONE;
  case ROW_SOUND:  if (change) st->sound = !st->sound; break;
  case ROW_MUSIC:  if (change) st->music = !st->music; break;
  case ROW_THEME:  if (change) st->track = (st->track + d + MUSIC_THEMES) % MUSIC_THEMES; return change ? UI_THEME : UI_NONE;
  case ROW_INTRO:  if (change) st->intro = !st->intro; break;
  case ROW_HSEAT:  if (change) hseat = 1 - hseat; return UI_NONE;
  case ROW_HJET:   if (change) st->jets[hseat] = (st->jets[hseat] + d + NJETS) % NJETS; break;
  case ROW_HEAT:
  case ROW_RADAR:
  case ROW_AGM:
    if (!change) break;
    if (!arm_step(st, sel - ROW_HEAT, n == NAV_LEFT ? -1 : 1) && n == NAV_OK) {
      /* OK past the limit wraps back to none */
      st->load[hseat][sel - ROW_HEAT] = 0;
    }
    break;
  case ROW_NROLE:  if (change && !net_active()) st->net_join = !st->net_join; break;
  case ROW_NMATCH: if (change && !net_active()) st->coop = !st->coop; break;
  case ROW_NVIA:   if (change && !net_active()) st->net_via = (st->net_via + d + NV_COUNT) % NV_COUNT; break;
  case ROW_NGAME:
    if (change && net_lan_count()) lan_sel = (lan_sel + d + net_lan_count()) % net_lan_count();
    if (n == NAV_OK) { sel = ROW_NGO; return UI_NET_GO; }
    return UI_NONE;
  case ROW_NADDR: case ROW_NRELAY: case ROW_NCODE: case ROW_NNAME:
    if (n == NAV_OK) { do sel = (sel + 1) % ROWS; while (!row_visible(st, sel)); }
    return n == NAV_OK ? UI_CHANGED : UI_NONE;
  case ROW_NETWORK:
    if (n == NAV_OK) go_page(PAGE_NET, ROW_NGO);
    return UI_NONE;
  case ROW_NGO:    return n == NAV_OK ? (net_active() ? UI_NET_CANCEL : UI_NET_GO) : UI_NONE;
  case ROW_NBACK:
    if (n != NAV_OK) return UI_NONE;
    if (net_active()) return UI_NET_CANCEL;
    go_page(PAGE_MAIN, ROW_NETWORK);
    return UI_CHANGED;
  case ROW_WATCH:  return n == NAV_OK ? UI_INTRO : UI_NONE;
  case ROW_EDITOR: return n == NAV_OK ? UI_EDITOR : UI_NONE;
  case ROW_START:  return n == NAV_OK ? UI_START : UI_NONE;
  case ROW_HANGAR:
    if (n == NAV_OK) {
      hseat = 0;
      go_page(PAGE_HANGAR, ROW_HJET);
    }
    return UI_NONE;
  case ROW_SETTINGS:
    if (n == NAV_OK) go_page(PAGE_SETTINGS, ROW_GFORCE);
    return UI_NONE;
  case ROW_BACK:
    if (n == NAV_OK) go_page(PAGE_MAIN, ROW_SETTINGS);
    return UI_NONE;
  case ROW_DONE:
    if (n == NAV_OK) go_page(PAGE_MAIN, ROW_START);
    return n == NAV_OK ? UI_CHANGED : UI_NONE;
  case ROW_EXIT:   return n == NAV_OK ? UI_QUIT : UI_NONE;
  }
  return change ? UI_CHANGED : UI_NONE;
}

typedef struct { Framebuffer *fb; int x, y, w, h, k; } Box;

/* A see-through glass panel over the scene, centred, scaled to fit. */
static void glass(Framebuffer *fb, int x, int y, int w, int h, int k) {
  fb_rect_a(fb, x, y, w, h, rgb(8, 10, 22), 0.42f);
  fb_rect_stroke(fb, (float)x, (float)y, (float)w, (float)h, hexc(C_EDGE), (float)k);
}

static Box c64_box(Framebuffer *fb, int lw, int lh) {
  int k = 1;
  while ((lw + 16) * (k + 1) <= fb->w && (lh + 16) * (k + 1) <= fb->h) k++;
  Box b = { fb, 0, 0, lw * k, lh * k, k };
  b.x = (fb->w - b.w) / 2;
  b.y = (fb->h - b.h) / 2;
  fb_rect_a(fb, 0, 0, fb->w, fb->h, rgb(6, 4, 18), 0.18f);
  glass(fb, b.x, b.y, b.w, b.h, k);
  return b;
}

static void btext(const Box *b, const char *s, int x, int y, int scale, uint32_t c, int align) {
  fb_text(b->fb, s, (float)(b->x + x * b->k), (float)(b->y + y * b->k), scale * b->k, hexc(c), align, 0);
}

static void title(const Box *b, const char *s, int x, int y, int scale, int align) {
  fb_text(b->fb, s, (float)(b->x + (x + scale) * b->k), (float)(b->y + (y + scale) * b->k), scale * b->k, hexc(C_SHADOW), align, 0);
  btext(b, s, x, y, scale, 0xffffff, align);
}

static void button(const Box *b, const char *s, int x, int y, int w, bool on) {
  if (on) fb_rect(b->fb, b->x + x * b->k, b->y + (y - 6) * b->k, w * b->k, 12 * b->k, hexc(C_LAMP));
  else fb_rect_stroke(b->fb, (float)(b->x + x * b->k), (float)(b->y + (y - 6) * b->k), (float)(w * b->k), (float)(12 * b->k), hexc(C_EDGE), (float)b->k);
  btext(b, s, x + w / 2, y, 1, on ? C_LAMP_INK : C_INK, ALIGN_CENTER);
}

/* The jet's role, word-wrapped, and its hangar scores as bars (JS jetInfo). */
static void draw_jet_info(const Box *b, const JetType *j, int y) {
  char line[64];
  const char *s = j->role;
  int rows = 0;
  while (*s && rows < 2) {
    int n = (int)strlen(s), cut = n;
    if (n > 46) {
      cut = 46;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut == 0) cut = 46;
    }
    snprintf(line, sizeof line, "%.*s", cut, s);
    btext(b, line, 150, y - 4 + rows * 9, 1, C_DIM, ALIGN_CENTER);
    s += cut;
    while (*s == ' ') s++;
    rows++;
  }
  static const char *const lab[4] = { "SPEED", "TURN", "CLIMB", "RANGE" };
  const float v[4] = { j->g_speed, j->g_turn, j->g_climb, j->g_range };
  for (int i = 0; i < 4; i++) {
    int x = 14 + i * 70;
    btext(b, lab[i], x, y + 16, 1, C_INK, ALIGN_LEFT);
    int filled = (int)roundf(v[i] / 2);
    for (int k = 0; k < 5; k++) {
      int bx = b->x + (x + 34 + k * 6) * b->k, by = b->y + (y + 13) * b->k;
      if (k < filled) fb_rect(b->fb, bx, by, 5 * b->k, 6 * b->k, hexc(C_LAMP));
      else fb_rect_stroke(b->fb, (float)bx, (float)by, 5.0f * b->k - 1, 6.0f * b->k - 1, hexc(C_DIM), 1);
    }
  }
}

/* The hangar, laid out like the hangar model preview: the jet turning on
 * a dark backdrop with its designation, role, story, stat bars, performance
 * and avionics around it, and the loadout on the right. */
#define H_ACCENT 0xf0b429
#define H_FG     0xdde4e8
#define H_MUTED  0x8b9aa3
#define H_LINE   0x2b363d

/* Word-wraps s into lines of up to w characters from (x, y); returns the next y. */
static int hwrap(const Box *b, const char *s, int x, int y, int w, int maxlines, uint32_t c) {
  char line[96];
  for (int n = 0; *s && n < maxlines; n++) {
    int len = (int)strlen(s), cut = len;
    if (len > w) {
      cut = w;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut == 0) cut = w;
    }
    snprintf(line, sizeof line, "%.*s%s", cut, s, n == maxlines - 1 && cut < len ? "..." : "");
    btext(b, line, x, y, 1, c, ALIGN_LEFT);
    s += cut;
    while (*s == ' ') s++;
    y += 9;
  }
  return y;
}

/* a label and its value, the value wrapping under itself */
static int hrow(const Box *b, const char *label, const char *val, int x, int vx, int y, int w) {
  btext(b, label, x, y, 1, H_MUTED, ALIGN_LEFT);
  return hwrap(b, val, vx, y, w, 2, H_FG);
}

static void draw_hangar(Framebuffer *fb, const Settings *st) {
  int k = 1;
  while (640 * (k + 1) <= fb->w && 360 * (k + 1) <= fb->h) k++;
  Box b = { fb, (fb->w - 640 * k) / 2, (fb->h - 360 * k) / 2, 640 * k, 360 * k, k };
  const JetType *j = &JETS[st->jets[hseat]];
  const JetSpec *sp = assets_jet_spec(st->jets[hseat]);
  char v[200];

  /* top left: who it is */
  if (sp) {
    snprintf(v, sizeof v, "%s  -  %s  -  %s", sp->bloc, sp->era, sp->inspired);
    hwrap(&b, v, 12, 12, 66, 1, H_MUTED);
  }
  btext(&b, j->des, 12, 30, 2, H_ACCENT, ALIGN_LEFT);
  btext(&b, j->name, 12 + text_width(j->des, 2 * k) / k + 8, 30, 2, H_FG, ALIGN_LEFT);
  btext(&b, sp ? sp->role : j->role, 12, 47, 1, H_FG, ALIGN_LEFT);

  /* bottom left: performance and avionics */
  if (sp) {
    int y = 236;
    btext(&b, "PERFORMANCE", 12, y, 1, H_ACCENT, ALIGN_LEFT);
    fb_rect(fb, b.x + 12 * k, b.y + (y + 5) * k, 190 * k, k, hexc(H_LINE));
    y += 12;
    char a[64];
    snprintf(a, sizeof a, "MACH %.2f", sp->mach_alt);
    y = hrow(&b, "TOP SPEED 11 KM", a, 12, 108, y, 16);
    snprintf(a, sizeof a, sp->imperial ? "%.0f KT" : "%.0f KM/H", sp->imperial ? sp->vmax_sl / 1.852f : sp->vmax_sl);
    y = hrow(&b, "SEA LEVEL", a, 12, 108, y, 16);
    if (sp->supercruise > 0) snprintf(a, sizeof a, "MACH %.2f", sp->supercruise);
    else snprintf(a, sizeof a, "NO");
    y = hrow(&b, "SUPERCRUISE", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%.0f M/S", sp->climb);
    y = hrow(&b, "CLIMB RATE", a, 12, 108, y, 16);
    snprintf(a, sizeof a, sp->imperial ? "%.0f FT" : "%.0f M", sp->imperial ? sp->ceiling * 3.281f : sp->ceiling);
    y = hrow(&b, "CEILING", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%.1f DEG/S", sp->turn_inst);
    y = hrow(&b, "TURN, BEST", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%.1f DEG/S", sp->turn_sus);
    y = hrow(&b, "TURN, HELD", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%d DEG/S", sp->roll);
    y = hrow(&b, "ROLL RATE", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "+%.1f / %.0f G", sp->g_max, sp->g_min);
    y = hrow(&b, "G LIMITS", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%.0f KM", sp->range);
    y = hrow(&b, "FERRY RANGE", a, 12, 108, y, 16);
    snprintf(a, sizeof a, "%.0f M", sp->takeoff);
    hrow(&b, "TAKE-OFF ROLL", a, 12, 108, y, 16);

    y = 236;
    btext(&b, "AVIONICS", 214, y, 1, H_ACCENT, ALIGN_LEFT);
    fb_rect(fb, b.x + 214 * k, b.y + (y + 5) * k, 190 * k, k, hexc(H_LINE));
    y += 12;
    y = hrow(&b, "RADAR", sp->radar, 214, 270, y, 22);
    y = hrow(&b, "SENSORS", sp->sensors, 214, 270, y, 22);
    y = hrow(&b, "RCS", sp->rcs, 214, 270, y, 22);
    y = hrow(&b, "GUN", sp->gun, 214, 270, y, 22);
    y = hrow(&b, "ENGINES", sp->engines, 214, 270, y, 22);
    snprintf(a, sizeof a, "%d", sp->crew);
    hrow(&b, "CREW", a, 214, 270, y, 22);
  }

  /* right: the story, the stat bars, then the loadout */
  int x0 = 420, y = 12;
  if (sp) y = hwrap(&b, sp->blurb, x0, y, 34, 6, H_FG) + 4;
  static const char *const SN[7] = { "SPEED", "TURN", "CLIMB", "RANGE", "FIELD", "FIREPOWER", "STEALTH" };
  for (int i = 0; sp && i < 7; i++, y += 10) {
    btext(&b, SN[i], x0, y, 1, H_MUTED, ALIGN_LEFT);
    int bx = b.x + (x0 + 64) * k, by = b.y + (y - 3) * k, bw = 112 * k;
    fb_rect(fb, bx, by, bw, 5 * k, hexc(H_LINE));
    fb_rect(fb, bx, by, (int)(bw * sp->stat[i] / 10), 5 * k, hexc(H_ACCENT));
    char t[8];
    snprintf(t, sizeof t, "%.1f", sp->stat[i]);
    btext(&b, t, 628, y, 1, H_FG, ALIGN_RIGHT);
  }
  y += 4;
  btext(&b, "LOADOUT", x0, y, 1, H_ACCENT, ALIGN_LEFT);
  fb_rect(fb, b.x + x0 * k, b.y + (y + 5) * k, 208 * k, k, hexc(H_LINE));
  y += 12;
  const int rows[] = { ROW_HSEAT, ROW_HJET, ROW_HEAT, ROW_RADAR, ROW_AGM };
  const char *lab[] = { "SEAT", "JET", "HEAT-SEEKERS", "RADAR-GUIDED", "AIR-TO-GROUND" };
  for (int i = 0; i < 5; i++) {
    int r = rows[i];
    if (!row_visible(st, r)) continue;
    bool on = sel == r;
    if (r == ROW_HSEAT) snprintf(v, sizeof v, "PLAYER %d", hseat + 1);
    else if (r == ROW_HJET) snprintf(v, sizeof v, "%s %s", j->des, j->name);
    else snprintf(v, sizeof v, "%d", st->load[hseat][r - ROW_HEAT]);
    if (on) fb_rect_a(fb, b.x + (x0 - 4) * k, b.y + (y - 5) * k, 216 * k, 10 * k, hexc(0xffffff), 0.1f);
    btext(&b, lab[i], x0, y, 1, on ? H_FG : H_MUTED, ALIGN_LEFT);
    char vv[220];
    snprintf(vv, sizeof vv, on ? "< %s >" : "%s", v);
    btext(&b, vv, 628, y, 1, on ? H_ACCENT : H_FG, ALIGN_RIGHT);
    y += 11;
  }
  /* the load against the jet's stations (classic rules: 8, an air-to-ground missile takes 2) */
  const int *l = st->load[hseat];
  int used = l[0] + l[1] + 2 * l[2];
  btext(&b, "STATIONS", x0, y + 1, 1, H_MUTED, ALIGN_LEFT);
  for (int i = 0; i < 8; i++) {
    int bx = b.x + (x0 + 64 + i * 18) * k, by = b.y + (y - 2) * k;
    if (i < used) fb_rect(fb, bx, by, 15 * k, 6 * k, hexc(H_ACCENT));
    else fb_rect_stroke(fb, (float)bx, (float)by, 15.0f * k - 1, 6.0f * k - 1, hexc(H_MUTED), 1);
  }
  y += 12;
  btext(&b, l[2] ? "NO RADAR MISSILES WITH AIR-TO-GROUND" : st->classic ? "CLASSIC: THE FULL LOAD" : "MODERN: HEAT-SEEKERS ONLY", x0, y, 1,
        H_MUTED, ALIGN_LEFT);
  y += 11;
  for (int i = 0; sp && i < sp->nweapons && y < 330; i++) y = hwrap(&b, sp->weapons[i], x0, y, 34, 1, H_FG);
  button(&b, "DONE", 470, 344, 110, sel == ROW_DONE);
}

/* Word-wraps s into lines of up to w characters, centred at y. */
static int wrap_text(const Box *b, const char *s, int y, int w, uint32_t c) {
  char line[64];
  while (*s) {
    int n = (int)strlen(s), cut = n;
    if (n > w) {
      cut = w;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut == 0) cut = w;
    }
    snprintf(line, sizeof line, "%.*s", cut, s);
    btext(b, line, 150, y, 1, c, ALIGN_CENTER);
    s += cut;
    while (*s == ' ') s++;
    y += 10;
  }
  return y;
}

/* Network play: host or join, on the LAN, by address or through a relay. */
static void draw_net(Framebuffer *fb, const Settings *st, float time) {
  Box b = c64_box(fb, 300, 338);
  title(&b, "NETWORK", 150, 20, 3, ALIGN_CENTER);
  static const char *const via[] = { "LAN", "INTERNET ADDRESS", "INTERNET RELAY" };
  bool cur = ((int)(time * 3)) % 2 == 0;
  char v[64];
  int y = 56;
  for (int r = ROW_NROLE; r <= ROW_NNAME; r++) {
    if (!row_visible(st, r)) continue;
    bool on = sel == r;
    const char *lab = "";
    bool text = false;
    switch (r) {
    case ROW_NROLE:  lab = "I WANT TO"; snprintf(v, sizeof v, "%s", st->net_join ? "JOIN A GAME" : "HOST A GAME"); break;
    case ROW_NMATCH: lab = "MATCH"; snprintf(v, sizeof v, "%s", st->coop ? "CO-OP" : "VERSUS"); break;
    case ROW_NVIA:   lab = "OVER"; snprintf(v, sizeof v, "%s", via[st->net_via % NV_COUNT]); break;
    case ROW_NGAME:
      lab = "GAME";
      if (lan_sel >= net_lan_count()) lan_sel = 0;
      snprintf(v, sizeof v, "%s", net_lan_count() ? net_lan_name(lan_sel) : cur ? "LOOKING..." : "LOOKING");
      break;
    case ROW_NADDR:  lab = "HOST ADDRESS"; snprintf(v, sizeof v, "%s", st->net_addr); text = true; break;
    case ROW_NRELAY: lab = "RELAY SERVER"; snprintf(v, sizeof v, "%s", st->net_relay); text = true; break;
    case ROW_NCODE:  lab = "GAME CODE"; snprintf(v, sizeof v, "%s", st->net_code); text = true; break;
    case ROW_NNAME:  lab = "YOUR NAME"; snprintf(v, sizeof v, "%s", st->net_name); text = true; break;
    }
    if (on) fb_rect_a(fb, b.x + 30 * b.k, b.y + (y - 5) * b.k, 240 * b.k, 10 * b.k, hexc(C_SELECT), 0.12f);
    btext(&b, lab, 38, y, 1, on ? 0xffffff : C_DIM, ALIGN_LEFT);
    char vv[72];
    if (text && on && !net_active()) snprintf(vv, sizeof vv, "%s%s", v, cur ? "_" : " ");
    else snprintf(vv, sizeof vv, on && !text ? "< %s >" : "%s", v);
    btext(&b, vv, 262, y, 1, on ? C_LAMP : C_INK, ALIGN_RIGHT);
    y += 12;
  }
  y += 6;
  const char *go = net_active() ? "CANCEL" : st->net_join ? "JOIN" : "HOST GAME";
  button(&b, go, 40, y, 108, sel == ROW_NGO);
  button(&b, "BACK", 152, y, 108, sel == ROW_NBACK);
  y += 18;
  /* the relay code, big, while hosting through a relay */
  if (net_active() && net_is_host() && net_code()[0]) {
    btext(&b, "GAME CODE - TELL YOUR FRIEND, NOBODY ELSE", 150, y, 1, C_DIM, ALIGN_CENTER);
    title(&b, net_code(), 150, y + 22, 3, ALIGN_CENTER);
    y += 46;
  }
  if (net_status()[0] && !(net_active() && net_is_host() && net_code()[0] && !strncmp(net_status(), "GAME CODE", 9)))
    y = wrap_text(&b, net_status(), y, 46, C_LAMP) + 6;
  /* what each choice needs */
  const char *help;
  if (!st->net_join) {
    snprintf(v, sizeof v, "YOU HOST: %s, %s, %s.", st->coop ? "CO-OP" : "VERSUS", st->pairs ? "2 VS 2" : "1 VS 1",
             assets_map_name(st->map));
    y = wrap_text(&b, v, y, 46, C_INK);
    help = st->net_via == NV_LAN ? "PLAYERS ON YOUR NETWORK FIND YOUR GAME IN THEIR LIST AND JOIN WITH THE CODE. THE MAIN MENU SETS THE MAP AND RULES."
           : st->net_via == NV_ADDRESS ? "FORWARD UDP PORT 27960 ON YOUR ROUTER TO THIS COMPUTER, THEN GIVE YOUR FRIEND YOUR PUBLIC IP ADDRESS AND THE CODE."
                                       : "NO PORT FORWARDING: BOTH CONNECT TO THE RELAY (A4C_RELAY, UDP 27970), WHICH ONLY PASSES ON ENCRYPTED PACKETS.";
  } else {
    help = st->net_via == NV_LAN ? "PICK THE GAME AND TYPE THE 6-DIGIT CODE ITS HOST SEES. YOUR JET AND ARMING ARE PLAYER 1'S IN THE HANGAR."
           : st->net_via == NV_ADDRESS ? "TYPE THE HOST'S ADDRESS, LIKE 203.0.113.7 OR 192.168.1.20:27960, AND THE 6-DIGIT CODE THE HOST SEES."
                                       : "TYPE THE RELAY SERVER AND THE 6-DIGIT CODE THE HOST SEES.";
  }
  wrap_text(&b, help, y, 46, C_DIM);
}

static void draw_menu(Framebuffer *fb, const Settings *st, float time) {
  if (page == PAGE_HANGAR) {
    draw_hangar(fb, st);
    return;
  }
  if (page == PAGE_NET) {
    draw_net(fb, st, time);
    return;
  }
  Box b = c64_box(fb, 300, 338);
  (void)time;
  title(&b, page == PAGE_SETTINGS ? "SETTINGS" : GAME_LOGO, 150, 50, 3, ALIGN_CENTER);

  static const char *const diff[] = { "EASY", "NORMAL", "ACE" };
  char row[ROWS][48]; /* a jet row: designation (up to 15) + name (up to 23) */
  memset(row, 0, sizeof row);
  snprintf(row[ROW_MODE], sizeof row[0], "%s", st->mode == MODE_1P ? "1 PLAYER" : st->mode == MODE_2P ? "2 PLAYERS" : "DEFEND THE CARRIER");
  if (st->mode == MODE_DEFEND) snprintf(row[ROW_MATCH], sizeof row[0], "%s", st->commander ? "PILOT + COMMANDER" : "PILOT ONLY");
  else snprintf(row[ROW_MATCH], sizeof row[0], "%s", st->coop ? "CO-OP" : "VERSUS");
  snprintf(row[ROW_SIZE], sizeof row[0], "%s", st->pairs ? "2 VS 2" : "1 VS 1");
  snprintf(row[ROW_STARTAT], sizeof row[0], "%s", st->ground ? "CARRIER VS BASE" : "IN THE AIR");
  snprintf(row[ROW_DIFF], sizeof row[0], "%s", diff[st->diff]);
  static const char *const tods[] = { "DAWN", "DAY", "DUSK", "NIGHT" };
  static const char *const wxs[] = { "CLEAR", "SCATTERED", "OVERCAST", "STORM" };
  snprintf(row[ROW_MAP], sizeof row[0], "%s", assets_map_name(st->map));
  snprintf(row[ROW_TOD], sizeof row[0], "%s", tods[st->tod]);
  snprintf(row[ROW_WX], sizeof row[0], "%s", st->weather < 0 ? "MAP DEFAULT" : wxs[st->weather]);
  for (int k = 0; k < 2; k++) snprintf(row[ROW_JET1 + k], sizeof row[0], "%s %s", JETS[st->jets[k]].des, JETS[st->jets[k]].name);
  snprintf(row[ROW_INVERT], sizeof row[0], "%s", st->invert ? "ON" : "OFF");
  snprintf(row[ROW_GFORCE], sizeof row[0], "%s", st->gforce ? "BLACKOUT ON" : "ARCADE");
  snprintf(row[ROW_RULES], sizeof row[0], "%s", st->classic ? "CLASSIC" : "MODERN");
  snprintf(row[ROW_HANDLING], sizeof row[0], "%s", st->realistic ? "REALISTIC" : "ARCADE");
  snprintf(row[ROW_SCEN], sizeof row[0], "%s", st->dogfight ? "CLOSE RANGE DOGFIGHT" : "FULL-SCALE CONFLICT");
  snprintf(row[ROW_LIVES], sizeof row[0], "%d", st->lives);
  snprintf(row[ROW_CRASH], sizeof row[0], "%s", st->crash ? "ON" : "OFF");
  snprintf(row[ROW_HITS], sizeof row[0], "%d", st->hits);
  if (st->modern != gpu_active()) snprintf(row[ROW_RENDER], sizeof row[0], "%s (RESTART)", st->modern ? "MODERN" : "RETRO");
  else if (gpu_active()) {
    const char *dr = gpu_driver();
    snprintf(row[ROW_RENDER], sizeof row[0], "MODERN %s", !strcmp(dr, "direct3d12") ? "D3D12" : !strcmp(dr, "vulkan") ? "VULKAN" : dr);
  } else snprintf(row[ROW_RENDER], sizeof row[0], "RETRO");
  snprintf(row[ROW_GFX], sizeof row[0], "%s", (const char *[]){ "LOW", "MEDIUM", "HIGH", "ULTRA" }[st->gfx & 3]);
  snprintf(row[ROW_SOUND], sizeof row[0], "%s", st->sound ? "ON" : "OFF");
  snprintf(row[ROW_MUSIC], sizeof row[0], "%s", st->music ? "ON" : "OFF");
  snprintf(row[ROW_THEME], sizeof row[0], "%s", THEMES[st->track].name);
  snprintf(row[ROW_INTRO], sizeof row[0], "%s", st->intro ? "ON" : "OFF");
  const char *label[ROWS] = { 0 };
  label[ROW_MODE] = "MODE";
  label[ROW_MATCH] = st->mode == MODE_DEFEND ? "CREW" : "MATCH";
  label[ROW_SIZE] = "MATCH SIZE";
  label[ROW_STARTAT] = "START";
  label[ROW_JET1] = st->mode == MODE_1P ? "YOUR JET" : "P1 JET";
  label[ROW_JET2] = st->mode == MODE_DEFEND ? "COMMANDER JET" : "P2 JET";
  label[ROW_MAP] = "MAP";
  label[ROW_TOD] = "TIME";
  label[ROW_WX] = "WEATHER";
  label[ROW_DIFF] = "ENEMY PILOT";
  label[ROW_RULES] = "CONTROLS";
  label[ROW_HANDLING] = "HANDLING";
  label[ROW_SCEN] = "SCENARIO";
  label[ROW_LIVES] = "PLANES EACH";
  label[ROW_CRASH] = "CRASH DETECTION";
  label[ROW_HITS] = "MISSILE HITS TO KILL";
  label[ROW_GFORCE] = "G EFFECTS";
  label[ROW_RENDER] = "RENDERER";
  label[ROW_GFX] = "GRAPHICS";
  label[ROW_INVERT] = "INVERT PITCH";
  label[ROW_SOUND] = "SOUND";
  label[ROW_MUSIC] = "MUSIC";
  label[ROW_THEME] = "THEME";
  label[ROW_INTRO] = "INTRO AT START";
  int y = 78;
  for (int r = 0; r < ROW_START; r++) {
    if (!row_visible(st, r)) continue;
    bool on = sel == r;
    if (on) fb_rect_a(fb, b.x + 30 * b.k, b.y + (y - 5) * b.k, 240 * b.k, 10 * b.k, hexc(C_SELECT), 0.12f);
    btext(&b, label[r], 38, y, 1, on ? 0xffffff : C_DIM, ALIGN_LEFT);
    char v[sizeof row[0] + 4]; /* "< " row " >" */
    snprintf(v, sizeof v, on ? "< %s >" : "%s", row[r]);
    btext(&b, v, 262, y, 1, on ? C_LAMP : C_INK, ALIGN_RIGHT);
    y += 12;
  }
  y += 6;
  if (page == PAGE_SETTINGS) {
    button(&b, "BACK", 40, y, 220, sel == ROW_BACK);
    return;
  }
  button(&b, "START MISSION", 40, y, 220, sel == ROW_START);
  y += 15;
  button(&b, "HANGAR", 40, y, 72, sel == ROW_HANGAR);
  button(&b, "SETTINGS", 114, y, 72, sel == ROW_SETTINGS);
  button(&b, "NETWORK", 188, y, 72, sel == ROW_NETWORK);
  y += 15;
  if (row_visible(st, ROW_WATCH)) {
    button(&b, "WATCH INTRO", 40, y, 108, sel == ROW_WATCH);
    button(&b, "EXIT", 152, y, 108, sel == ROW_EXIT);
  } else button(&b, "EXIT", 40, y, 220, sel == ROW_EXIT);

  if (sel == ROW_JET1 || sel == ROW_JET2) {
    draw_jet_info(&b, &JETS[st->jets[sel - ROW_JET1]], 316);
    return;
  }
  if (st->mode == MODE_DEFEND && sel <= ROW_MATCH) {
    btext(&b, "CAPTAIN: ARROWS WALK  P/; LOOK  ' RUN", 150, 311, 1, C_DIM, ALIGN_CENTER);
    btext(&b, "ENTER USES THE CONSOLE", 150, 320, 1, C_DIM, ALIGN_CENTER);
    btext(&b, "CONSOLE: ARROWS PICK  ENTER ORDERS  P LAUNCH", 150, 329, 1, C_DIM, ALIGN_CENTER);
    return;
  }
  if (st->classic) {
    btext(&b, st->mode == MODE_2P ? "P1 Q/A DIVE/CLIMB Z/X BANK D/S POWER F FIRE" : "Q/A DIVE/CLIMB  Z/X BANK  D/S POWER  F FIRE", 150, 311, 1, C_DIM, ALIGN_CENTER);
    btext(&b, st->mode == MODE_2P ? "P1 W WPN R DECOY C MAP T AUTO  P2 P/L N/M K/J" : "W WEAPON  R FLARES/CHAFF  C MAP  T AUTOPILOT", 150, 320, 1, C_DIM, ALIGN_CENTER);
    btext(&b, st->mode == MODE_2P ? "P2 H FIRE O WEAPON U DECOY B MAP Y AUTO  F1/F2" : "F1 PAUSE  F2 QUIT", 150, 329, 1, C_DIM, ALIGN_CENTER);
    return;
  }
  /* three short lines: at 6 pixels a letter, 46 letters fill the 300-wide box */
  const char *help1 = st->mode == MODE_2P ? "P1  WASD  R/F THR  SPACE GUN  LSHIFT MSL"
                                          : "WASD OR ARROWS FLY  R/F THROTTLE  TAB VIEW";
  const char *help2 = st->mode == MODE_2P ? "P2  ARROWS  P/; THR  ENTER GUN  RSHIFT MSL"
                                          : "SPACE GUN  SHIFT MISSILE  Q FLARES";
  const char *help3 = st->mode == MODE_2P ? "FLARES P1 Q  P2 '    AUTOPILOT P1 T  P2 Y"
                                          : "T AUTOPILOT  M SOUND";
  btext(&b, help1, 150, 311, 1, C_DIM, ALIGN_CENTER);
  btext(&b, help2, 150, 320, 1, C_DIM, ALIGN_CENTER);
  btext(&b, help3, 150, 329, 1, C_DIM, ALIGN_CENTER);
}

static void draw_pause(Framebuffer *fb) {
  Box b = c64_box(fb, 160, 80);
  title(&b, "PAUSED", 80, 18, 3, ALIGN_CENTER);
  button(&b, "RESUME", 20, 44, 120, sel2 == 0);
  button(&b, "QUIT TO MENU", 20, 62, 120, sel2 == 1);
}

static void draw_over(Framebuffer *fb, const Game *g) {
  int rows = g->nplanes;
  Box b = c64_box(fb, 260, 110 + rows * 10);
  const char *t;
  int w = g->winner;
  if (g->def.on) t = w == 0 ? "CARRIER HELD" : "CARRIER LOST";
  else if (w < 0) t = "DRAW";
  else if (g->coop) t = w == 0 ? "MISSION COMPLETE" : "MISSION FAILED";
  else if (g->mode == MODE_1P) t = w == 0 ? "YOU WIN" : "CPU WINS";
  else t = w == 0 ? "PLAYER 1 WINS" : "PLAYER 2 WINS";
  title(&b, t, 130, 18, 2, ALIGN_CENTER);
  btext(&b, g->reason, 130, 38, 1, C_INK, ALIGN_CENTER);
  int y = 56;
  if (g->def.on) {
    char s2[64];
    snprintf(s2, sizeof s2, "CARRIER TURRETS: %d KILLS, %d MISSILES SHOT DOWN", g->def.kills, g->def.ashm_down);
    btext(&b, s2, 130, 48, 1, C_LAMP, ALIGN_CENTER);
  }
  btext(&b, "KILLS", 150, y, 1, C_DIM, ALIGN_RIGHT);
  btext(&b, "TARGETS", 200, y, 1, C_DIM, ALIGN_RIGHT);
  btext(&b, "JETS", 245, y, 1, C_DIM, ALIGN_RIGHT);
  for (int i = 0; i < rows; i++) {
    const Plane *p = &g->planes[i];
    char s[16];
    y += 10;
    btext(&b, p->name, 15, y, 1, TEAM[p->team].ui, ALIGN_LEFT);
    snprintf(s, sizeof s, "%d", p->kills);
    btext(&b, s, 150, y, 1, C_INK, ALIGN_RIGHT);
    snprintf(s, sizeof s, "%d/6", p->tkills);
    btext(&b, s, 200, y, 1, C_INK, ALIGN_RIGHT);
    snprintf(s, sizeof s, "%d", p->lives > 0 ? p->lives : 0);
    btext(&b, s, 245, y, 1, C_INK, ALIGN_RIGHT);
  }
  y += 20;
  button(&b, net_playing() && !net_is_host() ? "HOST DECIDES" : "REMATCH", 20, y, 105, sel2 == 0);
  button(&b, "MAIN MENU", 135, y, 105, sel2 == 1);
}

void ui_draw(Framebuffer *fb, Screen s, const Settings *st, const Game *g, float time) {
  fb_clip_reset(fb);
  if (s == SCREEN_MENU) draw_menu(fb, st, time);
  else if (s == SCREEN_PAUSE) draw_pause(fb);
  else draw_over(fb, g);
}
