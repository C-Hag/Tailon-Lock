/* input.c - keyboard sets per seat and SDL3 gamepads (JS BIND, readHuman,
 * pads; lines 2829-2888). SDL3 names gamepad buttons by position, so Xbox,
 * PlayStation and Switch pads all map the same. HOTAS through the joystick
 * API comes with milestone 4. */
#include "input.h"

#include <string.h>

typedef struct { SDL_Scancode k[4]; } Keys;
typedef struct { Keys up, down, left, right, thrUp, thrDn, gun, msl, flr, fire, sel, map; } Bind;

#define K0             { { SDL_SCANCODE_UNKNOWN } }
#define K1(a)          { { a, SDL_SCANCODE_UNKNOWN } }
#define K2(a, b)       { { a, b, SDL_SCANCODE_UNKNOWN } }
#define K3(a, b, c)    { { a, b, c, SDL_SCANCODE_UNKNOWN } }
#define K4(a, b, c, d) { { a, b, c, d } }

static const Bind BIND[2] = {
  { K1(SDL_SCANCODE_W), K1(SDL_SCANCODE_S), K1(SDL_SCANCODE_A), K1(SDL_SCANCODE_D), K1(SDL_SCANCODE_R),
    K1(SDL_SCANCODE_F), K1(SDL_SCANCODE_SPACE), K1(SDL_SCANCODE_LSHIFT), K1(SDL_SCANCODE_Q), K0, K0, K0 },
  { K3(SDL_SCANCODE_UP, SDL_SCANCODE_I, SDL_SCANCODE_KP_8),
    K4(SDL_SCANCODE_DOWN, SDL_SCANCODE_K, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_2),
    K3(SDL_SCANCODE_LEFT, SDL_SCANCODE_J, SDL_SCANCODE_KP_4),
    K3(SDL_SCANCODE_RIGHT, SDL_SCANCODE_L, SDL_SCANCODE_KP_6),
    K2(SDL_SCANCODE_P, SDL_SCANCODE_KP_PLUS), K2(SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_KP_MINUS),
    K2(SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_0), K2(SDL_SCANCODE_RSHIFT, SDL_SCANCODE_KP_ENTER),
    K2(SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_KP_PERIOD), K0, K0, K0 },
};

/* The keys of the classic 1987 PC game: dive, climb, bank left, bank right, power
 * up, power down; fire, select weapon and map view, and flares or chaff. */
static const Bind CLASSIC[2] = {
  { K1(SDL_SCANCODE_Q), K1(SDL_SCANCODE_A), K1(SDL_SCANCODE_Z), K1(SDL_SCANCODE_X), K1(SDL_SCANCODE_D),
    K1(SDL_SCANCODE_S), K0, K0, K1(SDL_SCANCODE_R), K1(SDL_SCANCODE_F), K1(SDL_SCANCODE_W), K1(SDL_SCANCODE_C) },
  { K1(SDL_SCANCODE_P), K1(SDL_SCANCODE_L), K1(SDL_SCANCODE_N), K1(SDL_SCANCODE_M), K1(SDL_SCANCODE_K),
    K1(SDL_SCANCODE_J), K0, K0, K1(SDL_SCANCODE_U), K1(SDL_SCANCODE_H), K1(SDL_SCANCODE_O), K1(SDL_SCANCODE_B) },
};

#define MAX_PADS 4
static SDL_Gamepad *pads[MAX_PADS];
static bool prevStart[MAX_PADS];
static float axis(SDL_Gamepad *gp, SDL_GamepadAxis a);

static bool down(const bool *ks, const Keys *k) {
  for (int i = 0; i < 4 && k->k[i] != SDL_SCANCODE_UNKNOWN; i++)
    if (ks[k->k[i]]) return true;
  return false;
}

static void add_pad(SDL_JoystickID id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i] && SDL_GetGamepadID(pads[i]) == id) return;
  for (int i = 0; i < MAX_PADS; i++)
    if (!pads[i]) {
      pads[i] = SDL_OpenGamepad(id);
      if (pads[i]) SDL_Log("gamepad %d: %s", i + 1, SDL_GetGamepadName(pads[i]));
      return;
    }
}

void input_init(void) {
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  for (int i = 0; ids && i < n; i++) add_pad(ids[i]);
  SDL_free(ids);
}

void input_quit(void) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i]) SDL_CloseGamepad(pads[i]);
  memset(pads, 0, sizeof pads);
}

void input_event(const SDL_Event *ev) {
  if (ev->type == SDL_EVENT_GAMEPAD_ADDED) add_pad(ev->gdevice.which);
  else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
    for (int i = 0; i < MAX_PADS; i++)
      if (pads[i] && SDL_GetGamepadID(pads[i]) == ev->gdevice.which) {
        SDL_CloseGamepad(pads[i]);
        /* keep connection order: later pads move up a seat */
        for (int j = i; j + 1 < MAX_PADS; j++) pads[j] = pads[j + 1];
        pads[MAX_PADS - 1] = NULL;
        break;
      }
  }
}

int input_pad_count(void) {
  int n = 0;
  for (int i = 0; i < MAX_PADS; i++) n += pads[i] != NULL;
  return n;
}

void input_reset(void) { memset(prevStart, 0, sizeof prevStart); }

void input_read_walk(float *move, float *side, float *turn, float *look, bool *run, int seat) {
  const bool *ks = SDL_GetKeyboardState(NULL);
  const Bind *B = &BIND[seat & 1];
  float mv = 0, sd = 0, tn = 0, lk = 0;
  bool rn = down(ks, &B->flr);
  if (down(ks, &B->up)) mv += 1;
  if (down(ks, &B->down)) mv -= 1;
  if (down(ks, &B->right)) tn += 1;
  if (down(ks, &B->left)) tn -= 1;
  if (down(ks, &B->thrUp)) lk += 1;
  if (down(ks, &B->thrDn)) lk -= 1;
  SDL_Gamepad *gp = seat < MAX_PADS ? pads[seat] : NULL;
  if (gp) {
#define DZ(v) (fabsf(v) < 0.18f ? 0.0f : (v))
    mv -= DZ(axis(gp, SDL_GAMEPAD_AXIS_LEFTY));
    sd += DZ(axis(gp, SDL_GAMEPAD_AXIS_LEFTX));
    tn += DZ(axis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
    lk -= DZ(axis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
#undef DZ
    rn = rn || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_STICK);
  }
  *move = clampf(mv, -1, 1);
  *side = clampf(sd, -1, 1);
  *turn = clampf(tn, -1, 1);
  *look = clampf(lk, -1, 1);
  *run = rn;
}

int input_pad_seat(SDL_JoystickID id) {
  for (int i = 0; i < MAX_PADS; i++)
    if (pads[i] && SDL_GetGamepadID(pads[i]) == id) return i;
  return -1;
}

void input_read_turret(float *yaw, float *pitch, bool *fire, int seat) {
  const bool *ks = SDL_GetKeyboardState(NULL);
  const Bind *B = &BIND[seat & 1];
  float y = 0, p = 0;
  bool f = down(ks, &B->gun);
  if (down(ks, &B->right)) y += 1;
  if (down(ks, &B->left)) y -= 1;
  if (down(ks, &B->up)) p += 1;
  if (down(ks, &B->down)) p -= 1;
  SDL_Gamepad *gp = seat < MAX_PADS ? pads[seat] : NULL;
  if (gp) {
    float ax = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f, ay = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (fabsf(ax) > 0.15f) y += ax;
    if (fabsf(ay) > 0.15f) p -= ay;
    f = f || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
  }
  *yaw = clampf(y, -1, 1);
  *pitch = clampf(p, -1, 1);
  *fire = f;
}

static float axis(SDL_Gamepad *gp, SDL_GamepadAxis a) {
  float v = SDL_GetGamepadAxis(gp, a) / 32767.0f;
  if (v < -1) v = -1;
  return v;
}

bool input_read_human(Game *g, Plane *p, float dt, int idx, bool invert) {
  const bool *ks = SDL_GetKeyboardState(NULL);
  Controls *c = &p->ctl;
  float pitch = 0, roll = 0, thr = 0;
  bool gun = false, msl = false, flr = false, fire = false, sel = false, map = false, ap = false, start = false;
  int b0 = g->nlocals > 1 ? idx : 0, b1 = g->nlocals > 1 ? idx : 1;
  for (int b = b0; b <= b1; b++) {
    const Bind *B = g->classic ? &CLASSIC[b] : &BIND[b];
    fire = fire || down(ks, &B->fire);
    ap = ap || ks[b ? SDL_SCANCODE_Y : SDL_SCANCODE_T];
    sel = sel || down(ks, &B->sel);
    map = map || down(ks, &B->map);
    if (down(ks, &B->down)) pitch += 1;
    if (down(ks, &B->up)) pitch -= 1;
    if (down(ks, &B->right)) roll += 1;
    if (down(ks, &B->left)) roll -= 1;
    if (down(ks, &B->thrUp)) thr += 1;
    if (down(ks, &B->thrDn)) thr -= 1;
    gun = gun || down(ks, &B->gun);
    msl = msl || down(ks, &B->msl);
    flr = flr || down(ks, &B->flr);
  }
  bool analog = false;
  SDL_Gamepad *gp = idx < MAX_PADS ? pads[idx] : NULL;
  if (gp) {
#define DZ(v) (fabsf(v) < 0.15f ? 0.0f : (v))
    float ax = DZ(axis(gp, SDL_GAMEPAD_AXIS_LEFTX)), ay = DZ(axis(gp, SDL_GAMEPAD_AXIS_LEFTY));
    if (ax != 0 || ay != 0) analog = true;
    roll += ax;
    pitch += ay;
    gun = gun || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    msl = msl || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST);
    flr = flr || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST) || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    sel = sel || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH);
    map = map || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_BACK);
    ap = ap || SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    thr += axis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) - axis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) - DZ(axis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
#undef DZ
    bool st = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START);
    start = st && !prevStart[idx];
    prevStart[idx] = st;
  }
  pitch = clampf(pitch, -1, 1);
  roll = clampf(roll, -1, 1);
  c->stick = fabsf(pitch) > 0.3f || fabsf(roll) > 0.3f;
  c->ap = ap;
  if (invert) pitch = -pitch;
  float k = analog ? 14.0f : 6.0f;
  c->pitch += (pitch - c->pitch) * fminf(1, dt * k);
  c->roll += (roll - c->roll) * fminf(1, dt * (analog ? 14.0f : 8.0f));
  p->throttle = clampf(p->throttle + clampf(thr, -1, 1) * 0.6f * dt, 0, 1);
  c->gun = gun;
  c->msl = msl;
  c->flr = flr;
  c->fire = fire;
  c->sel = sel;
  c->map = map;
  return start;
}
