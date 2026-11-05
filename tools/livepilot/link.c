/* link.c - runs the game under the live test player: its own settings folder, the A4C_EYES pipes, pictures in
 * and hands out. Original code for this project; POSIX. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lp.h"

static void mkdirs(const char *path) {
  char b[600];
  snprintf(b, sizeof b, "%s", path);
  for (char *p = b + 1; *p; p++)
    if (*p == '/') {
      *p = 0;
      mkdir(b, 0755);
      *p = '/';
    }
  mkdir(b, 0755);
}

static long ms_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

bool game_start(Game *G, const Run *R, const char *const *args, const char *settings_extra) {
  memset(G, 0, sizeof *G);
  G->pid = -1;
  /* a clean settings file, unless the caller keeps the one there (settings_extra NULL) */
  char dir[600], ini[700];
  snprintf(dir, sizeof dir, "%s/tailonlock", R->cfg);
  mkdirs(dir);
  snprintf(ini, sizeof ini, "%s/settings.ini", dir);
  if (settings_extra) {
    FILE *f = fopen(ini, "w");
    if (!f) return false;
    fprintf(f, "intro=0\nstart=air\n%s", settings_extra);
    fclose(f);
  }
  static int nrun;
  snprintf(G->log, sizeof G->log, "%s/logs/run%03d.log", OUT, ++nrun);
  int p2g[2], g2p[2];
  if (pipe(p2g) || pipe(g2p)) return false;
  char *argv[64];
  int n = 0;
  argv[n++] = (char *)R->exe;
  argv[n++] = "--frames";
  argv[n++] = "100000000";
  argv[n++] = "--window";
  argv[n++] = (char *)(R->window ? R->window : "1280x720");
  if (R->renderer) argv[n++] = "--renderer", argv[n++] = (char *)R->renderer;
  if (R->gfx) argv[n++] = "--gfx", argv[n++] = (char *)R->gfx;
  for (int i = 0; args && args[i] && n < 62; i++) argv[n++] = (char *)args[i];
  argv[n] = NULL;
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    close(p2g[1]);
    close(g2p[0]);
    char eyes[32], every[16];
    snprintf(eyes, sizeof eyes, "%d,%d", p2g[0], g2p[1]);
    snprintf(every, sizeof every, "%d", R->every > 0 ? R->every : 4);
    setenv("A4C_EYES", eyes, 1);
    setenv("A4C_EYES_EVERY", every, 1);
    setenv("XDG_CONFIG_HOME", R->cfg, 1);
    char cfgdir[1100]; /* the settings folder itself on every system (macOS does not read XDG_CONFIG_HOME) */
    snprintf(cfgdir, sizeof cfgdir, "%s/tailonlock", R->cfg);
    setenv("A4C_CONFIG_DIR", cfgdir, 1);
    /* a game port of this run's own, the same for its host and guest, so a game or another test hosting on this
     * computer keeps UDP 27960 (an A4C_NET_PORT given is kept) */
    char port[8];
    snprintf(port, sizeof port, "%d", 30000 + (int)(getppid() % 20000));
    setenv("A4C_NET_PORT", port, 0);
    setenv("A4C_STATE", "1", 1);
    if (!getenv("LP_DISPLAY")) setenv("A4C_VIDEO", "offscreen", 1);
    setenv("A4C_AUDIO", "dummy", 1);
    int lf = open(G->log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (lf >= 0) dup2(lf, 1), dup2(lf, 2), close(lf);
    signal(SIGPIPE, SIG_DFL);
    execv(R->exe, argv);
    _exit(127);
  }
  close(p2g[0]);
  close(g2p[1]);
  G->pid = pid;
  G->to = p2g[1];
  G->from = g2p[0];
  return true;
}

/* ---- reading: a small buffered reader over the pipe, with a time limit ---- */
static char  rb[1 << 16];
static size_t rpos, rlen;
static int   rfd_cur = -1;

static int fill(int fd, int timeout_ms) {
  struct pollfd p = { fd, POLLIN, 0 };
  int k = poll(&p, 1, timeout_ms);
  if (k <= 0) return k == 0 ? -2 : -1; /* -2: nothing in time */
  ssize_t n = read(fd, rb, sizeof rb);
  if (n <= 0) return -1;
  rpos = 0, rlen = (size_t)n;
  return 1;
}
/* one line (without the newline); 1, or -1 gone, -2 hung */
static int rline(int fd, char *out, size_t max, long deadline) {
  size_t n = 0;
  for (;;) {
    if (rpos >= rlen) {
      long left = deadline - ms_now();
      int k = fill(fd, left > 0 ? (int)left : 0);
      if (k < 0) return k;
    }
    char c = rb[rpos++];
    if (c == '\n') {
      out[n] = 0;
      return 1;
    }
    if (n < max - 1) out[n++] = c;
  }
}
static int rbytes(int fd, void *dst, size_t len, long deadline) {
  char *d = dst;
  while (len) {
    if (rpos >= rlen) {
      long left = deadline - ms_now();
      int k = fill(fd, left > 0 ? (int)left : 0);
      if (k < 0) return k;
    }
    size_t k = rlen - rpos < len ? rlen - rpos : len;
    memcpy(d, rb + rpos, k);
    rpos += k, d += k, len -= k;
  }
  return 1;
}

void frame_free(Frame *F) {
  free(F->px);
  free(F->t);
  memset(F, 0, sizeof *F);
}

bool game_read(Game *G, Frame *F, int timeout_ms) {
  if (G->gone) return false;
  if (rfd_cur != G->from) rpos = rlen = 0, rfd_cur = G->from; /* one game read at a time between calls */
  long deadline = ms_now() + timeout_ms;
  char l[512];
  F->nt = F->nd = F->ne = F->nb = F->nm = F->nr = F->nk = F->nst = F->nnz = F->nlg = F->ncs = 0;
  F->has_q = false;
  F->has_p = F->has_v = F->lock = F->has_self = false;
  F->gstress = 0;
  for (;;) {
    int k = rline(G->from, l, sizeof l, deadline);
    if (k < 0) {
      G->gone = k == -1;
      if (k == -2) G->status = -2; /* hung */
      return false;
    }
    switch (l[0]) {
    case 'F': {
      int gpu = 0, cl = 0;
      sscanf(l, "F %ld %15s %15s %d %d %d %d %d %d", &F->frame, F->state, F->page, &F->w, &F->h, &F->fbw, &F->fbh, &gpu, &cl);
      F->gpu = gpu, F->classic = cl;
      break;
    }
    case 'P':
      if (l[1] == 'X') break; /* the picture's line, below */
      if (l[1] == 'G') { /* the grey-out */
        F->gstress = 0;
        sscanf(l, "PG %f", &F->gstress);
        break;
      }
      F->has_p = sscanf(l, "P %d %11s %11s %f %f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d %d %f %d", &F->alive, F->view,
                        F->jet, &F->spd, &F->alt, &F->hdg, &F->pitch, &F->bank, &F->gmeter, &F->gload, &F->fuel, &F->hull,
                        &F->ammo, &F->missiles, &F->flares, &F->wsel, &F->locked, &F->warn, &F->stall, &F->ground,
                        &F->gloc, &F->thr, &F->lives) >= 20;
      break;
    case 'V':
      F->has_v = sscanf(l, "V %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &F->vx, &F->vy, &F->vw, &F->sh, &F->cx, &F->cy,
                        &F->F, &F->r[0], &F->r[1], &F->r[2], &F->u[0], &F->u[1], &F->u[2], &F->f[0], &F->f[1],
                        &F->f[2]) == 16;
      break;
    case 'E':
      if (F->ne < 16) {
        SheetJet *e = &F->e[F->ne];
        int foe, on, cloud = 0;
        if (sscanf(l, "E %d %d %d %f %f %f %f %d", &e->idx, &foe, &on, &e->x, &e->y, &e->z, &e->km, &cloud) >= 7)
          e->foe = foe, e->on = on, e->cloud = cloud, F->ne++;
      }
      break;
    case 'B':
      if (F->nb < 16 && sscanf(l, "B %d %f %f %f", &F->b[F->nb].idx, &F->b[F->nb].x, &F->b[F->nb].y, &F->b[F->nb].km) == 4)
        F->nb++;
      break;
    case 'Q':
      F->has_q = sscanf(l, "Q %d %d %d %d %d %d", &F->q_ammo, &F->q_heat, &F->q_radar, &F->q_ground, &F->q_flares, &F->q_chaff) == 6;
      break;
    case 'K':
      if (F->nk < 8) {
        SheetPips *kp = &F->k[F->nk];
        kp->kind = 0; /* a sheet from before the kinds: heat-seekers */
        if (sscanf(l, "K %f %f %f %f %f %d %d %x %x %d", &kp->x, &kp->y, &kp->w, &kp->h, &kp->step, &kp->n, &kp->max, &kp->on, &kp->off,
                   &kp->kind) >= 9)
          F->nk++;
      }
      break;
    case 'L':
      F->lock = sscanf(l, "L %f %f", &F->lx, &F->ly) == 2;
      break;
    case 'M':
      if (F->nm < 32) {
        SheetMissile *m = &F->m[F->nm];
        int on, cloud = 0;
        if (sscanf(l, "M %u %d %d %f %f %f %f %f %f %f %d", &m->id, &m->kind, &on, &m->x, &m->y, &m->z, &m->bx, &m->by, &m->life,
                   &m->dist, &cloud) >= 10)
          m->on = on, m->cloud = cloud, F->nm++;
      }
      break;
    case 'R':
      if (F->nr < 24 && sscanf(l, "R %f %f %f", &F->rd[F->nr].x, &F->rd[F->nr].y, &F->rd[F->nr].z) == 3) F->nr++;
      break;
    case 'W':
      if (F->nst < 24) {
        SheetStation *w = &F->st[F->nst];
        int ld, sh, on;
        if (sscanf(l, "W %d %d %d %d %f %f %f", &w->cls, &ld, &sh, &on, &w->x, &w->y, &w->z) == 7)
          w->loaded = ld, w->shown = sh, w->on = on, F->nst++;
      }
      break;
    case 'N':
      if (F->nnz < 4) {
        SheetNozzle *n = &F->nz[F->nnz];
        int on;
        if (sscanf(l, "N %d %f %f %f %f %f", &on, &n->x, &n->y, &n->z, &n->thr, &n->ab) == 6) n->on = on, F->nnz++;
      }
      break;
    case 'G':
      if (F->nlg < 4) {
        SheetLeg *q = &F->lg[F->nlg];
        int on;
        if (sscanf(l, "G %d %d %f %f %f %f", &q->leg, &on, &q->x, &q->y, &q->z, &q->down) == 6) q->on = on, F->nlg++;
      }
      break;
    case 'C':
      if (F->ncs < 48) {
        SheetSurface *q = &F->cs[F->ncs];
        int on;
        if (sscanf(l, "C %d %d %d %f %f %f %f", &q->kind, &q->side, &on, &q->x, &q->y, &q->z, &q->deg) == 7) q->on = on, F->ncs++;
      }
      break;
    case 'S':
      F->has_self = sscanf(l, "S %f %f %f", &F->selfx, &F->selfy, &F->selfz) == 3;
      break;
    case 'T': {
      if ((size_t)F->nt >= F->tcap) {
        size_t cap = F->tcap ? F->tcap * 2 : 128;
        SheetText *nt = realloc(F->t, cap * sizeof *F->t);
        if (!nt) break; /* out of memory: this line's text is left out */
        F->t = nt;
        F->tcap = cap;
      }
      SheetText *t = &F->t[F->nt];
      int off = 0;
      if (sscanf(l, "T %d %d %d %d %n", &t->x0, &t->y0, &t->x1, &t->y1, &off) >= 4 && off > 0) {
        snprintf(t->s, sizeof t->s, "%s", l + off);
        F->nt++;
      }
      break;
    }
    case 'D':
      if (F->nd < 24) {
        SheetDial *d = &F->d[F->nd];
        d->v2 = d->len2 = 0;
        if (sscanf(l, "D %f %f %f %f %f %f %f %f %15s %f %f", &d->cx, &d->cy, &d->r, &d->a0, &d->a1, &d->lo, &d->hi, &d->v, d->title,
                   &d->v2, &d->len2) >= 9)
          F->nd++;
      }
      break;
    }
    if (l[0] == 'P' && l[1] == 'X') {
      int w = 0, h = 0;
      sscanf(l, "PX %d %d", &w, &h);
      if (w > 0 && h > 0) {
        size_t need = (size_t)w * h;
        if (F->cap < need) {
          free(F->px);
          F->px = malloc(need * 4);
          F->cap = F->px ? need : 0; /* out of memory: no room, so the frame is not read into it */
        }
        if (!F->px) return false; /* out of memory: no frame */
        int k2 = rbytes(G->from, F->px, need * 4, deadline);
        if (k2 < 0) {
          G->gone = k2 == -1;
          return false;
        }
        F->w = w, F->h = h;
      } else F->w = F->h = 0;
      G->frames++;
      return true;
    }
  }
}

void game_say(Game *G, const char *fmt, ...) {
  if (G->gone) return;
  char b[256];
  va_list a;
  va_start(a, fmt);
  int n = vsnprintf(b, sizeof b - 1, fmt, a);
  va_end(a);
  if (n < 0) return;
  b[n++] = '\n';
  if (write(G->to, b, (size_t)n) != n) G->gone = true;
}
void game_go(Game *G) { game_say(G, "go"); }

void game_key(Game *G, const char *key, bool down) {
  int i;
  for (i = 0; i < G->nheld && strcmp(G->held[i], key); i++) {}
  bool is = i < G->nheld;
  if (down == is) return;
  game_say(G, "%s %s", down ? "down" : "up", key);
  if (down && G->nheld < 16) snprintf(G->held[G->nheld++], sizeof G->held[0], "%s", key);
  else if (!down) {
    for (int j = i; j < G->nheld - 1; j++) memcpy(G->held[j], G->held[j + 1], sizeof G->held[0]);
    G->nheld--;
  }
}

bool game_wait(Game *G, int ms) {
  if (G->pid <= 0) return true;
  long end = ms_now() + ms;
  for (;;) {
    int st;
    pid_t r = waitpid(G->pid, &st, WNOHANG);
    if (r == G->pid) {
      G->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
      G->pid = -1;
      G->gone = true;
      return true;
    }
    if (ms_now() >= end) return false;
    /* keep a game that is still sending pictures from blocking on a full pipe while it shuts down */
    struct pollfd p = { G->from, POLLIN, 0 };
    if (poll(&p, 1, 20) > 0) {
      char sink[65536];
      if (read(G->from, sink, sizeof sink) > 0) game_go(G);
    }
  }
}

void game_kill(Game *G, int sig) {
  if (G->pid > 0) kill(G->pid, sig);
}

void game_end(Game *G) {
  if (G->pid <= 0) {
    close(G->from), close(G->to);
    return;
  }
  game_say(G, "quit");
  game_go(G);
  if (!game_wait(G, 8000)) {
    kill(G->pid, SIGKILL);
    game_wait(G, 3000);
  }
  close(G->from);
  close(G->to);
  rfd_cur = -1;
}
