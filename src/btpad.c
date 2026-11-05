/* btpad.c - the Bluetooth pad simulator, for tests: what a wireless pad does that a wired one does not.
 *
 * The hand (--pad) moves the pad's buttons and sticks. Every report interval the pad sends its whole state as
 * one report, as a Bluetooth HID pad does. The radio (--btpad) loses some reports and delays the rest by its
 * latency plus a random jitter, keeping their order; nothing is sent again, so a lost report only means the
 * next one brings the change, later. The link can drop (drop=MS) and come back after a while, which the game
 * sees as the pad pulled out and plugged in again under a new id; off and on are the pad's power switch.
 *
 * The pads reach the game through the platform's virtual pads (plat_vpad_*), so it sees them through the same
 * pad events and calls as a real one. Time runs on the frame clock (60 frames a second), so a run with the
 * same seed and script is the same every time. Nothing here runs unless --btpad or --pad was given.
 *
 * --btpad pads=N,interval=MS,latency=MS,jitter=MS,loss=PERCENT,seed=S,vid=N,pid=N,name=TEXT
 *   defaults: one pad, a report every 8 ms, 12 ms on the way plus up to 4 ms, nothing lost, a "Virtual Wireless
 *   Pad" with no maker's ids (0:0): the game knows a pad by the system's generic gamepad class, never by its maker
 * --pad F:N:WHAT[:HOLD]  at frame F, pad N (1-4):
 *   a b x y back guide start ls rs lb rb up down left right   pressed for HOLD frames (3)
 *   lx= ly= rx= ry= lt= rt= -1..1                             a stick or trigger there, back at 0 after HOLD
 *   off on                                                    the power switch
 *   drop=MS                                                   the link lost for MS, then back by itself
 *
 * The log names every change: the hand's ("btpad 1: start down at frame 120"), and each report that changes
 * what the game sees ("btpad 1: in at frame 121, 14.2 ms on the way"), so a test can measure the time from
 * the hand to the game's answer. Original code for this project. */
#include "btpad.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/plat.h"
#include "core/safe.h"

#define MAXPADS   4
#define MAXSTEPS  128
#define MAXFLIGHT 64
#define FRAME_MS  (1000.0 / 60.0)

typedef struct { bool b[PPAD_BUTTONS]; int a[PAXIS_COUNT]; } PadState;
typedef struct { double sent, at; PadState s; } Report;

static struct {
  int      pads, interval, latency, jitter, loss; /* ms, ms, ms, percent */
  unsigned vid, pid;
  char     name[64];
  uint32_t rng;
  bool     on;
} R = { 1, 8, 12, 4, 0, 0, 0, "Virtual Wireless Pad", 1, false };

static struct {
  int      h;          /* the platform's handle; 0 while switched off or the link is down */
  bool     powered;
  double   back_at;    /* the link comes back then (ms), when it was lost */
  PadState hand, seen; /* what the hand holds; what the game was last sent */
  double   next_send, last_at;
  int      sent, lost, in;
  Report   fl[MAXFLIGHT]; /* on the way, oldest first */
  int      nfl;
} P[MAXPADS];

static struct { long f; int pad, hold; bool after_start; char what[24]; } S[MAXSTEPS];
static int ns;

static const char *BUTTON_NAMES[PPAD_BUTTONS] = { "a", "b", "x", "y", "back", "guide", "start", "ls", "rs",
                                                  "lb", "rb", "up", "down", "left", "right" };
static const char *AXIS_NAMES[PAXIS_COUNT] = { "lx", "ly", "rx", "ry", "lt", "rt" };

static uint32_t rnd(void) {
  R.rng ^= R.rng << 13;
  R.rng ^= R.rng >> 17;
  R.rng ^= R.rng << 5;
  return R.rng;
}

bool btpad_radio(const char *spec) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", spec);
  char *cur = buf;
  for (char *kv = safe_token(&cur, ","); kv; kv = safe_token(&cur, ",")) {
    char *eq = strchr(kv, '=');
    if (!eq) return false;
    *eq++ = 0;
    long v = strtol(eq, NULL, 0);
    if (!strcmp(kv, "pads")) R.pads = v < 1 ? 1 : v > MAXPADS ? MAXPADS : (int)v;
    else if (!strcmp(kv, "interval")) R.interval = v < 1 ? 1 : (int)v;
    else if (!strcmp(kv, "latency")) R.latency = v < 0 ? 0 : (int)v;
    else if (!strcmp(kv, "jitter")) R.jitter = v < 0 ? 0 : (int)v;
    else if (!strcmp(kv, "loss")) R.loss = v < 0 ? 0 : v > 100 ? 100 : (int)v;
    else if (!strcmp(kv, "seed")) R.rng = v ? (uint32_t)v : 1;
    else if (!strcmp(kv, "vid")) R.vid = (unsigned)v;
    else if (!strcmp(kv, "pid")) R.pid = (unsigned)v;
    else if (!strcmp(kv, "name")) snprintf(R.name, sizeof R.name, "%s", eq);
    else return false;
  }
  R.on = true;
  return true;
}

bool btpad_script(const char *step) {
  if (ns >= MAXSTEPS) return false;
  int hold = -1;
  S[ns].after_start = step[0] == 'p';
  if (S[ns].after_start) step++;
  if (sscanf(step, "%ld:%d:%23[^:]:%d", &S[ns].f, &S[ns].pad, S[ns].what, &hold) < 3) return false;
  if (S[ns].pad < 1 || S[ns].pad > MAXPADS) return false;
  S[ns].hold = hold;
  ns++;
  R.on = true;
  return true;
}

bool btpad_on(void) { return R.on; }

static void connect(int i, double t) {
  P[i].h = plat_vpad_attach(R.name, R.vid, R.pid);
  P[i].nfl = 0;
  P[i].next_send = t;
  P[i].last_at = t;
  memset(&P[i].seen, 0, sizeof P[i].seen);
  plat_log("btpad %d: %s at %.0f ms", i + 1, P[i].h ? "connected" : "could not connect", t);
}

static void disconnect(int i, const char *why, double t) {
  if (!P[i].h) return;
  plat_vpad_detach(P[i].h);
  P[i].h = 0;
  P[i].nfl = 0; /* what was on the way is gone with the link */
  plat_log("btpad %d: %s at %.0f ms", i + 1, why, t);
}

void btpad_start(void) {
  for (int i = 0; i < R.pads; i++) {
    P[i].powered = true;
    connect(i, 0);
  }
}

static void send_until(int i, double t, bool inclusive) {
  while (P[i].h && (inclusive ? P[i].next_send <= t : P[i].next_send < t)) {
    double s = P[i].next_send;
    P[i].next_send += R.interval;
    P[i].sent++;
    if ((int)(rnd() % 100) < R.loss) {
      P[i].lost++;
      continue;
    }
    double at = s + R.latency + (R.jitter ? (double)(rnd() % (unsigned)(R.jitter * 10 + 1)) / 10 : 0);
    if (at < P[i].last_at) at = P[i].last_at; /* the link keeps the order */
    P[i].last_at = at;
    if (P[i].nfl == MAXFLIGHT) { /* the radio's queue full: the oldest goes */
      memmove(P[i].fl, P[i].fl + 1, (MAXFLIGHT - 1) * sizeof *P[i].fl);
      P[i].nfl--;
      P[i].lost++;
    }
    P[i].fl[P[i].nfl++] = (Report){ s, at, P[i].hand };
  }
}

static void step(int k, long frame, double t, bool release) {
  int i = S[k].pad - 1;
  const char *w = S[k].what;
  if (release) {
    for (int b = 0; b < PPAD_BUTTONS; b++)
      if (!strcmp(w, BUTTON_NAMES[b])) P[i].hand.b[b] = false, plat_log("btpad %d: %s up at frame %ld", i + 1, w, frame);
    for (int a = 0; a < PAXIS_COUNT; a++)
      if (!strncmp(w, AXIS_NAMES[a], 2) && w[2] == '=') P[i].hand.a[a] = 0;
    return;
  }
  if (!strcmp(w, "off")) {
    P[i].powered = false;
    P[i].back_at = 0;
    disconnect(i, "switched off", t);
  } else if (!strcmp(w, "on")) {
    P[i].powered = true;
    if (!P[i].h) connect(i, t);
  } else if (!strncmp(w, "drop=", 5)) {
    if (P[i].h) {
      disconnect(i, "link lost", t);
      P[i].back_at = t + safe_num(w + 5, 0);
    }
  } else {
    for (int b = 0; b < PPAD_BUTTONS; b++)
      if (!strcmp(w, BUTTON_NAMES[b])) P[i].hand.b[b] = true, plat_log("btpad %d: %s down at frame %ld", i + 1, w, frame);
    for (int a = 0; a < PAXIS_COUNT; a++)
      if (!strncmp(w, AXIS_NAMES[a], 2) && w[2] == '=') {
        float v = (float)safe_num(w + 3, 0);
        v = v < -1 ? -1 : v > 1 ? 1 : v;
        P[i].hand.a[a] = (int)(v * 32767);
      }
  }
}

void btpad_playing(long frame) {
  static bool started;
  if (started) return;
  started = true;
  for (int k = 0; k < ns; k++)
    if (S[k].after_start) S[k].f += frame, S[k].after_start = false;
}

void btpad_frame(long frame) {
  if (!R.on) return;
  double t = frame * FRAME_MS;
  for (int i = 0; i < MAXPADS; i++) send_until(i, t, false); /* the reports sent before this moment */
  for (int k = 0; k < ns; k++) {
    if (S[k].after_start) continue; /* the match has not started */
    if (S[k].f == frame) step(k, frame, t, false);
    int hold = S[k].hold >= 0 ? S[k].hold : 3;
    bool axis = strchr(S[k].what, '=') && strncmp(S[k].what, "drop=", 5);
    if ((S[k].hold >= 0 || !axis) && S[k].f + hold == frame && hold > 0) step(k, frame, t, true);
  }
  for (int i = 0; i < MAXPADS; i++) {
    if (P[i].powered && !P[i].h && P[i].back_at > 0 && t >= P[i].back_at) {
      P[i].back_at = 0;
      connect(i, t);
      plat_log("btpad %d: link back at %.0f ms", i + 1, t);
    }
    send_until(i, t, true);
    int n = 0;
    while (n < P[i].nfl && P[i].fl[n].at <= t) {
      const Report *r = &P[i].fl[n++];
      P[i].in++;
      /* the buttons and the axes on their own: memcmp of the whole PadState would also compare its padding */
      if (memcmp(r->s.b, P[i].seen.b, sizeof r->s.b) || memcmp(r->s.a, P[i].seen.a, sizeof r->s.a)) {
        plat_log("btpad %d: in at frame %ld, %.1f ms on the way", i + 1, frame, r->at - r->sent);
        P[i].seen = r->s;
      }
      plat_vpad_set(P[i].h, r->s.b, r->s.a);
    }
    memmove(P[i].fl, P[i].fl + n, (size_t)(P[i].nfl - n) * sizeof *P[i].fl);
    P[i].nfl -= n;
  }
}

void btpad_stop(void) {
  if (!R.on) return;
  for (int i = 0; i < MAXPADS; i++) {
    if (P[i].sent) plat_log("btpad %d: %d reports sent, %d lost, %d in", i + 1, P[i].sent, P[i].lost, P[i].in);
    disconnect(i, "stopped", 0);
  }
}
