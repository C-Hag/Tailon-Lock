/* lp.h - the live test player (tools/livepilot): a player who sees only the picture the game drew and plays with
 * the keyboard and mouse, as a person does. Original code for this project; C11 and POSIX only.
 *
 * The game hands over every Nth finished picture through its A4C_EYES link (src/eyes.h) with an answer sheet:
 * what it drew (texts, dials) and what the jet really does. The player reads the picture on its own (see.c) and
 * only then holds what it read against the sheet; a difference is a finding. */
#ifndef LP_H
#define LP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* ---- one picture and its answer sheet ---- */
typedef struct { int x0, y0, x1, y1; char s[64]; } SheetText;     /* fb pixels; x1, y1 past the end */
typedef struct { float cx, cy, r, a0, a1, lo, hi, v, v2, len2; char title[16]; } SheetDial; /* len2: 2nd needle, 0 none */
typedef struct { int idx; bool foe, on, cloud; float x, y, z, km; } SheetJet; /* cloud: behind a cloud puff */
typedef struct { int idx; float x, y, km; } SheetBox;
typedef struct { unsigned id; int kind; bool on, cloud; float x, y, z, bx, by, life, dist; } SheetMissile;
typedef struct { float x, y, z; } SheetRound;
typedef struct { float x, y, w, h, step; int n, max; uint32_t on, off; int kind; } SheetPips; /* kind: heat, radar, ground */
/* the player's own jet as drawn: a station of its full load (cls heat, radar, ground; loaded; shown: not in a shut bay), a
 * nozzle's exhaust, a gear leg's wheel (leg -1: the gear up, none drawn) */
typedef struct { int cls; bool loaded, shown, on; float x, y, z; } SheetStation;
typedef struct { bool on; float x, y, z, thr, ab; } SheetNozzle;
typedef struct { int leg; bool on; float x, y, z, down; } SheetLeg;
typedef struct { int kind, side; bool on; float x, y, z, deg; } SheetSurface; /* surfaces.h's kinds; deg: edge up/right */

typedef struct {
  long  frame;
  char  state[16], page[16];
  int   w, h, fbw, fbh;      /* the picture and the HUD's framebuffer */
  bool  gpu, classic;
  bool  has_p;               /* the first local player's jet */
  int   alive;
  char  view[12], jet[12];
  float spd, alt, hdg, pitch, bank, gmeter, gload, fuel, hull, thr, gstress; /* gstress: the grey-out, above 1 */
  int   ammo, missiles, flares, wsel, locked, warn, stall, ground, gloc, lives;
  bool  has_q; int q_ammo, q_heat, q_radar, q_ground, q_flares, q_chaff; /* the stores really left */
  SheetPips     k[8]; int nk;
  bool  has_v;               /* the HUD's camera this frame */
  float vx, vy, vw, sh, cx, cy, F, r[3], u[3], f[3];
  SheetText    *t;  int nt;
  SheetDial     d[24]; int nd;
  SheetJet      e[16]; int ne;
  SheetBox      b[16]; int nb;
  bool          lock; float lx, ly;
  SheetMissile  m[32]; int nm;
  SheetRound    rd[24]; int nr;
  bool          has_self; float selfx, selfy, selfz;
  SheetStation  st[24]; int nst;
  SheetNozzle   nz[4]; int nnz;
  SheetLeg      lg[4]; int nlg;
  SheetSurface  cs[48]; int ncs;
  uint32_t     *px;          /* w x h, 0xAARRGGBB */
  size_t        cap, tcap;
} Frame;

/* fb (HUD) coordinates to picture pixels */
static inline float fx(const Frame *f, float x) { return x * (float)f->w / (float)f->fbw; }
static inline float fy(const Frame *f, float y) { return y * (float)f->h / (float)f->fbh; }

/* ---- the game, run under the player ---- */
typedef struct {
  pid_t pid;
  int   from, to;     /* pictures in, hands out */
  bool  gone;         /* exited or closed the link */
  int   status;       /* its exit status once gone */
  char  log[600];     /* its stdout and stderr */
  long  frames;       /* pictures seen */
  /* keys held, so a game can be let go of cleanly */
  char  held[16][16]; int nheld;
} Game;

typedef struct {
  const char *exe, *cfg;  /* the game, its own settings folder */
  int   every;            /* frames per picture */
  const char *window, *renderer, *gfx;
} Run;

bool game_start(Game *G, const Run *R, const char *const *args, const char *settings_extra);
bool game_read(Game *G, Frame *F, int timeout_ms);  /* the next picture; false: gone or hung */
void game_say(Game *G, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void game_go(Game *G);
void game_key(Game *G, const char *key, bool down);
void game_end(Game *G);                          /* quit, wait, and collect */
void game_kill(Game *G, int sig);
bool game_wait(Game *G, int ms);                 /* until it exits; false: still running */
void frame_free(Frame *F);

/* ---- reading the picture (see.c): nothing here looks at the answer sheet ---- */
typedef struct { int x0, y0, x1, y1, scale; uint32_t col; float err; char s[64]; } Read;  /* picture pixels */
void  see_init(void);
int   see_texts(const Frame *F, Read *out, int max);          /* every line of text in the game's font */
/* the horizon in the box (picture pixels): the line between sky and ground as y = a + b x; false: none found */
bool  see_horizon(const Frame *F, int x0, int y0, int x1, int y1, float *a, float *b, float *quality);
/* the same near a guess (y = a0 + b0 x): the best line within dang degrees and band pixels of it */
bool  see_horizon_near(const Frame *F, int x0, int y0, int x1, int y1, float a0, float b0, float band, float dang, bool sky_up,
                       float *a, float *b, float *quality);
/* the angle (degrees clockwise from 12) of the brightest needle in a dial; false: none */
bool  see_needle(const Frame *F, float cx, float cy, float r, float *deg, float *quality);
/* how much an object stands out at (x, y) within radius r against the ring around it: share of pixels apart */
float see_contrast(const Frame *F, float x, float y, float r);
/* the same, the best of any point within near pixels (the picture and the sheet are a moment apart) */
float see_contrast_near(const Frame *F, float x, float y, float r, float near);
/* a missile at (x, y), its tail towards (bx, by): body seen, trail share along the tail, a round fireball's
 * radius (0: none) */
typedef struct { bool body; float trail, fireball, bodyr; } MissileLook; /* trail < 0: tail too short to judge */
/* finpx: its fins' size on screen; lenpx: its body's length on screen along the tail; sizepx: its length side on */
MissileLook see_missile(const Frame *F, float x, float y, float bx, float by, float finpx, float lenpx, float sizepx);
int   see_cells(const Frame *F, int x0, int y0, int x1, int y1, char *out, int max); /* a drawn box read cell by cell */
uint32_t see_cells_ink(void);                                                      /* the ink the last one read in */
float see_luma_contrast(const Frame *F, int x0, int y0, int x1, int y1, uint32_t col); /* text colour vs around */
bool  see_blank(const Frame *F);                                /* one colour, or nearly */
double see_diff(const Frame *A, const Frame *B, int *bx0, int *by0, int *bx1, int *by1); /* share changed + box */
uint32_t px_at(const Frame *F, int x, int y);
void  save_bmp(const char *path, const Frame *F);
void  save_bmp_box(const char *path, const Frame *F, int x0, int y0, int x1, int y1, int zoom);

/* ---- the report ---- */
typedef enum { V_PASS, V_FAIL, V_SKIP, V_NOTE } Verdict;
void report_item(const char *id, const char *title, Verdict v, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
void report_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern char OUT[512];

#endif
