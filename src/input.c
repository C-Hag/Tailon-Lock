/* input.c - keyboard sets per seat and gamepads (JS BIND, readHuman, pads;
 * lines 2829-2888), through plat.h. Gamepad buttons are named by position,
 * so every maker's pads map the same. HOTAS through the
 * joystick API comes with milestone 4. */
#include "input.h"

#include <stdio.h>
#include <string.h>

typedef struct { int k[4]; } Keys; /* PK_ codes, 0 ends the list */
typedef struct { Keys up, down, left, right, thrUp, thrDn, gun, msl, flr, fire, sel, map; } Bind;

#define K0             { { 0 } }
#define K1(a)          { { a, 0 } }
#define K2(a, b)       { { a, b, 0 } }
#define K3(a, b, c)    { { a, b, c, 0 } }
#define K4(a, b, c, d) { { a, b, c, d } }

static const Bind BIND[2] = {
  { K1(PK_W), K1(PK_S), K1(PK_A), K1(PK_D), K1(PK_R),
    K1(PK_F), K1(PK_SPACE), K1(PK_LSHIFT), K1(PK_Q), K0, K0, K0 },
  { K3(PK_UP, PK_I, PK_KP_8),
    K4(PK_DOWN, PK_K, PK_KP_5, PK_KP_2),
    K3(PK_LEFT, PK_J, PK_KP_4),
    K3(PK_RIGHT, PK_L, PK_KP_6),
    K2(PK_P, PK_KP_PLUS), K2(PK_SEMICOLON, PK_KP_MINUS),
    K2(PK_RETURN, PK_KP_0), K2(PK_RSHIFT, PK_KP_ENTER),
    K2(PK_APOSTROPHE, PK_KP_PERIOD), K0, K0, K0 },
};

/* The keys of the classic 1987 PC game: dive, climb, bank left, bank right, power
 * up, power down; fire, select weapon and map view, and flares or chaff. */
static const Bind CLASSIC[2] = {
  { K1(PK_Q), K1(PK_A), K1(PK_Z), K1(PK_X), K1(PK_D),
    K1(PK_S), K0, K0, K1(PK_R), K1(PK_F), K1(PK_W), K1(PK_C) },
  { K1(PK_P), K1(PK_L), K1(PK_N), K1(PK_M), K1(PK_K),
    K1(PK_J), K0, K0, K1(PK_U), K1(PK_H), K1(PK_O), K1(PK_B) },
};

#define MAX_PADS 4
static int pads[MAX_PADS]; /* the pads' ids by seat, 0 for none */
/* each seat's pad by identity (plat_pad_key): while a seat's pad is away (a wireless pad's link lost, a cable
 * pulled) the seat waits for it, so the other pads keep their seats and the pad takes its own seat back */
static char seat_key[MAX_PADS][128];
static bool prevStart[MAX_PADS];
static float axis(int gp, int a);

static bool down(const Keys *k) {
  for (int i = 0; i < 4 && k->k[i] != 0; i++)
    if (plat_key_down(k->k[i])) return true;
  return false;
}

static void add_pad(int id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i] == id) return;
  if (!plat_pad_open(id)) return;
  const char *key = plat_pad_key(id);
  int seat = -1;
  for (int i = 0; i < MAX_PADS && seat < 0; i++) /* its own seat, waiting for it */
    if (!pads[i] && seat_key[i][0] && !strcmp(seat_key[i], key)) seat = i;
  for (int i = 0; i < MAX_PADS && seat < 0; i++) /* another pad in a hand that lost one */
    if (!pads[i] && seat_key[i][0]) seat = i;
  for (int i = 0; i < MAX_PADS && seat < 0; i++)
    if (!pads[i]) seat = i;
  if (seat < 0) {
    plat_pad_close(id);
    return;
  }
  bool back = seat_key[seat][0] && !strcmp(seat_key[seat], key);
  pads[seat] = id;
  snprintf(seat_key[seat], sizeof seat_key[seat], "%s", key);
  plat_log("gamepad %d: %s%s", seat + 1, plat_pad_name(id), back ? " (back)" : "");
}

void input_settle(void) {
  /* seats waiting for a pad that went away are let go, and later pads move up a seat, in connection order */
  int k = 0;
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i]) {
      if (k != i) {
        pads[k] = pads[i];
        memcpy(seat_key[k], seat_key[i], sizeof seat_key[k]);
        plat_log("gamepad %d: now seat %d", i + 1, k + 1);
      }
      k++;
    }
  for (; k < MAX_PADS; k++) pads[k] = 0, seat_key[k][0] = 0;
}

void input_init(void) {
  int ids[16], n = plat_pads(ids, 16);
  for (int i = 0; i < n; i++) add_pad(ids[i]);
}

void input_quit(void) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i]) plat_pad_close(pads[i]);
  memset(pads, 0, sizeof pads);
  memset(seat_key, 0, sizeof seat_key);
}

void input_event(const PlatEvent *ev) {
  if (ev->type == PE_PAD_ADDED) add_pad(ev->pad.id);
  else if (ev->type == PE_PAD_REMOVED) {
    for (int i = 0; i < MAX_PADS; i++)
      if (pads[i] && pads[i] == ev->pad.id) {
        plat_log("gamepad %d: gone, the seat waits for it", i + 1);
        plat_pad_close(pads[i]);
        pads[i] = 0; /* seat_key[i] stays: the seat is this pad's until input_settle */
        break;
      }
  }
}

bool input_seat_waiting(int seat) { return seat >= 0 && seat < MAX_PADS && !pads[seat] && seat_key[seat][0]; }

int input_pad_count(void) {
  int n = 0;
  for (int i = 0; i < MAX_PADS; i++) n += pads[i] != 0;
  return n;
}

void input_reset(void) { memset(prevStart, 0, sizeof prevStart); }

void input_read_walk(float *move, float *side, float *turn, float *look, bool *run, int seat) {
    const Bind *B = &BIND[seat & 1];
  float mv = 0, sd = 0, tn = 0, lk = 0;
  bool rn = down(&B->flr);
  if (down(&B->up)) mv += 1;
  if (down(&B->down)) mv -= 1;
  if (down(&B->right)) tn += 1;
  if (down(&B->left)) tn -= 1;
  if (down(&B->thrUp)) lk += 1;
  if (down(&B->thrDn)) lk -= 1;
  int gp = seat < MAX_PADS ? pads[seat] : 0;
  if (gp) {
#define DZ(v) (fabsf(v) < 0.18f ? 0.0f : (v))
    mv -= DZ(axis(gp, PAXIS_LEFTY));
    sd += DZ(axis(gp, PAXIS_LEFTX));
    tn += DZ(axis(gp, PAXIS_RIGHTX));
    lk -= DZ(axis(gp, PAXIS_RIGHTY));
#undef DZ
    rn = rn || plat_pad_button(gp, PPAD_RSHOULDER) || plat_pad_button(gp, PPAD_LSTICK);
  }
  *move = clampf(mv, -1, 1);
  *side = clampf(sd, -1, 1);
  *turn = clampf(tn, -1, 1);
  *look = clampf(lk, -1, 1);
  *run = rn;
}

int input_pad_seat(int id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i] && pads[i] == id) return i;
  return -1;
}

void input_read_turret(float *yaw, float *pitch, bool *fire, int seat) {
    const Bind *B = &BIND[seat & 1];
  float y = 0, p = 0;
  bool f = down(&B->gun);
  if (down(&B->right)) y += 1;
  if (down(&B->left)) y -= 1;
  if (down(&B->up)) p += 1;
  if (down(&B->down)) p -= 1;
  int gp = seat < MAX_PADS ? pads[seat] : 0;
  if (gp) {
    float ax = plat_pad_axis(gp, PAXIS_LEFTX) / 32767.0f, ay = plat_pad_axis(gp, PAXIS_LEFTY) / 32767.0f;
    if (fabsf(ax) > 0.15f) y += ax;
    if (fabsf(ay) > 0.15f) p -= ay;
    f = f || plat_pad_button(gp, PPAD_SOUTH) || plat_pad_axis(gp, PAXIS_RTRIGGER) > 16000;
  }
  *yaw = clampf(y, -1, 1);
  *pitch = clampf(p, -1, 1);
  *fire = f;
}

static float axis(int gp, int a) {
  float v = plat_pad_axis(gp, a) / 32767.0f;
  if (v < -1) v = -1;
  return v;
}

void input_shape(Controls *c, float pitch, float roll, bool analog, float dt) {
  float k = analog ? 14.0f : 6.0f;
  c->pitch += (pitch - c->pitch) * fminf(1, dt * k);
  /* keys roll in gently and let go at once: a short tap is a small correction and nothing lingers after it
   * (8/s both ways rocked the wings: every tap overshot and needed one the other way) */
  float kr = analog ? 14.0f : fabsf(roll) > fabsf(c->roll) && roll * c->roll >= 0 ? 4.0f : 16.0f;
  c->roll += (roll - c->roll) * fminf(1, dt * kr);
}

bool input_read_human(Game *g, Plane *p, float dt, int idx, bool invert) {
    Controls *c = &p->ctl;
  float pitch = 0, roll = 0, thr = 0;
  bool gun = false, msl = false, flr = false, fire = false, sel = false, map = false, ap = false, start = false, vec = false,
       look = false, gear = false, flap = false, tank = false;
  int b0 = g->nlocals > 1 ? idx : 0, b1 = g->nlocals > 1 ? idx : 1;
  for (int b = b0; b <= b1; b++) {
    const Bind *B = g->classic ? &CLASSIC[b] : &BIND[b];
    fire = fire || down(&B->fire);
    ap = ap || plat_key_down(b ? PK_Y : PK_T);
    /* the drone's thrust vectoring: the autopilot key, and E for player 1 */
    vec = vec || plat_key_down(b ? PK_Y : PK_T) || (!b && plat_key_down(PK_E));
    sel = sel || down(&B->sel);
    map = map || down(&B->map);
    /* G (H for player 2): eyes on the enemy; realistic handling works the gear with G, so player 1 looks with B */
    look = look || (!g->classic && plat_key_down(b ? PK_H : g->realistic ? PK_B : PK_G));
    /* realistic: the gear (G, comma for player 2) and the flaps (V, period), in either set of keys */
    gear = gear || (g->realistic && plat_key_down(b ? PK_COMMA : PK_G));
    flap = flap || (g->realistic && plat_key_down(b ? PK_PERIOD : PK_V));
    /* the tanker call: 4 (slash or keypad 9 for player 2), in every set of keys and both rules */
    tank = tank || plat_key_down(b ? PK_SLASH : PK_4) || (b && plat_key_down(PK_KP_9));
    if (down(&B->down)) pitch += 1;
    if (down(&B->up)) pitch -= 1;
    if (down(&B->right)) roll += 1;
    if (down(&B->left)) roll -= 1;
    if (down(&B->thrUp)) thr += 1;
    if (down(&B->thrDn)) thr -= 1;
    gun = gun || down(&B->gun);
    msl = msl || down(&B->msl);
    flr = flr || down(&B->flr);
  }
  bool analog = false;
  int gp = idx < MAX_PADS ? pads[idx] : 0;
  if (gp) {
#define DZ(v) (fabsf(v) < 0.15f ? 0.0f : (v))
    float ax = DZ(axis(gp, PAXIS_LEFTX)), ay = DZ(axis(gp, PAXIS_LEFTY));
    if (ax != 0 || ay != 0) analog = true;
    roll += ax;
    pitch += ay;
    gun = gun || plat_pad_button(gp, PPAD_SOUTH) || plat_pad_button(gp, PPAD_RSHOULDER);
    msl = msl || plat_pad_button(gp, PPAD_EAST);
    flr = flr || plat_pad_button(gp, PPAD_WEST) || plat_pad_button(gp, PPAD_LSHOULDER);
    sel = sel || plat_pad_button(gp, PPAD_NORTH);
    map = map || plat_pad_button(gp, PPAD_BACK);
    look = look || (!g->classic && fabsf(axis(gp, PAXIS_RIGHTX)) > 0.6f);
    ap = ap || plat_pad_button(gp, PPAD_RSTICK);
    gear = gear || (g->realistic && plat_pad_button(gp, PPAD_DDOWN)); /* the d-pad: down the gear, up the flaps */
    flap = flap || (g->realistic && plat_pad_button(gp, PPAD_DUP));
    vec = vec || plat_pad_button(gp, PPAD_RSTICK) || plat_pad_button(gp, PPAD_LSTICK) ||
          (!g->classic && plat_pad_button(gp, PPAD_NORTH));
    thr += axis(gp, PAXIS_RTRIGGER) - axis(gp, PAXIS_LTRIGGER) - DZ(axis(gp, PAXIS_RIGHTY));
#undef DZ
    bool st = plat_pad_button(gp, PPAD_START);
    start = st && !prevStart[idx];
    prevStart[idx] = st;
  }
  pitch = clampf(pitch, -1, 1);
  roll = clampf(roll, -1, 1);
  c->stick = fabsf(pitch) > 0.3f || fabsf(roll) > 0.3f;
  c->ap = ap;
  if (invert) pitch = -pitch;
  input_shape(c, pitch, roll, analog, dt);
  p->throttle = clampf(p->throttle + clampf(thr, -1, 1) * 0.6f * dt, 0, 1);
  c->look = look;
  c->gun = gun;
  c->msl = msl;
  c->flr = flr;
  c->fire = fire;
  c->sel = sel;
  c->map = map;
  c->vec = vec;
  c->gear = gear;
  c->flap = flap;
  c->tanker = tank;
  return start;
}
