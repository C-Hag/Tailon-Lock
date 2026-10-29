/* defend.c - the carrier defence mission. The carrier is the team 0 carrier
 * of a sea map; its turrets are three CIWS guns (fast, short range, they
 * shred missiles) and two SAM launchers. Each turret runs on its own (AUTO:
 * missiles first, then the nearest jet), holds fire, takes the target the
 * commander assigned, or is aimed and fired by hand. Waves of strike jets
 * come in from the north with anti-ship missiles and guns. */
#include "core/defend.h"

#include <stdio.h>
#include <string.h>

#include "core/world.h"

static const struct { int kind; const char *name; float x, y, z; } MOUNTS[] = {
  { TUR_CIWS, "CIWS 1 BOW", -30, 22, 148 },
  { TUR_CIWS, "CIWS 2 STERN", 34, 22, -152 },
  { TUR_CIWS, "CIWS 3 PORT", -40, 22, -40 },
  { TUR_SAM, "SAM 1 FWD", 40, 22, 95 },
  { TUR_SAM, "SAM 2 AFT", -38, 22, -118 },
};
#define NMOUNTS ((int)(sizeof MOUNTS / sizeof MOUNTS[0]))

static float wrap_pi(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

static vec3 to_world(const Defense *d, vec3 l) {
  float h = d->heading * PI_F / 180, c = cosf(h), s = sinf(h);
  return v3(d->pos.x + l.x * c + l.z * s, l.y, d->pos.z - l.x * s + l.z * c);
}

bool defend_over_carrier(const Game *g, float x, float z) {
  const Defense *d = &g->def;
  float h = d->heading * PI_F / 180, s = sinf(h), c = cosf(h);
  float dx = x - d->pos.x, dz = z - d->pos.z;
  float lx = dx * c - dz * s, lz = dx * s + dz * c;
  return fabsf(lx) < d->beam / 2 && fabsf(lz) < d->length / 2;
}

vec3 defend_turret_pos(const Game *g, int k) { return to_world(&g->def, g->def.tur[k].local); }
vec3 defend_local(const Game *g, vec3 l) { return to_world(&g->def, l); }

vec3 defend_cap_eye(const Game *g) {
  const Defense *d = &g->def;
  return to_world(d, v3(d->cap.x, d->cap.y + 1.7f + 0.04f * sinf(d->cap_bob), d->cap.z));
}

/* The jet waiting on catapult 2, and the stairs at each end. */
#define JET_SPOT  v3(8, 0, 66)
#define STAIRS_UP v3(31.4f, 0, -48.4f)
#define DECK_DOOR v3(21.8f, 0, -42)

static void place_captain(Defense *d, bool bridge, vec3 at, float yaw) {
  d->cap_bridge = bridge;
  d->cap = v3(at.x, d->deck + (bridge ? BRIDGE_UP : 0), at.z);
  d->cap_yaw = yaw;
  d->cap_look = 0;
}

static float dist_xz(vec3 a, vec3 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

Usable defend_usable(const Game *g) {
  const Defense *d = &g->def;
  Usable u = { USE_NONE, 0, v3(0, 0, 0), "" };
  float best = 1e9f;
#define TRY(kind_, arg_, pos_, r_, label_)                         \
  do {                                                             \
    float dd = dist_xz(d->cap, pos_);                              \
    if (dd < (r_) && dd < best) {                                  \
      best = dd;                                                   \
      u.kind = kind_; u.arg = arg_; u.at = pos_; u.label = label_; \
    }                                                              \
  } while (0)
  if (d->cap_bridge) {
    TRY(USE_TACTICAL, 0, v3(28.5f, 0, -36.2f), 2.6f, "TACTICAL CONSOLE");
    TRY(USE_AIROPS, 0, v3(25.4f, 0, -43), 2.4f, "AIR OPS: LAUNCH A JET");
    TRY(USE_DOWN, 0, STAIRS_UP, 2.2f, "STAIRS DOWN TO THE DECK");
  } else {
    TRY(USE_UP, 0, DECK_DOOR, 3.0f, "STAIRS UP TO THE BRIDGE");
    if (d->cmd_plane >= 0) TRY(USE_JET, 0, JET_SPOT, 7.0f, "CLIMB IN AND LAUNCH");
    for (int k = 0; k < d->nturrets; k++) TRY(USE_TURRET, k, d->tur[k].local, 7.5f, d->tur[k].name);
  }
#undef TRY
  return u;
}

void defend_use(Game *g) {
  Defense *d = &g->def;
  Usable u = defend_usable(g);
  switch (u.kind) {
  case USE_TACTICAL: d->cmd_state = CMD_TACTICAL; break;
  case USE_AIROPS:
  case USE_JET:      defend_cmd_launch(g); break;
  case USE_DOWN:     place_captain(d, false, v3(DECK_DOOR.x - 1.5f, 0, DECK_DOOR.z), -PI_F / 2); break;
  case USE_UP:       place_captain(d, true, v3(STAIRS_UP.x - 1, 0, STAIRS_UP.z + 1.5f), 0); break;
  case USE_TURRET: {
    Turret *t = &d->tur[u.arg];
    d->sel = u.arg;
    t->mode = TM_MANUAL;
    t->man_yaw = t->man_pitch = 0;
    t->man_fire = false;
    d->cmd_state = CMD_TURRET;
    break;
  }
  default: break;
  }
}

/* Walking: a fixed-step mover that slides along the walls (each axis is
 * clamped on its own, so a diagonal push into a wall still moves along it). */
static void captain_update(Defense *d, float dt) {
  if (d->cmd_state != CMD_WALK) return;
  d->cap_yaw = wrap_pi(d->cap_yaw + d->cap_turn * 2.2f * dt);
  d->cap_look = clampf(d->cap_look + d->cap_lookv * 1.4f * dt, -1.2f, 1.2f);
  float sp = d->cap_run ? 8.0f : 4.2f;
  float fx = sinf(d->cap_yaw), fz = cosf(d->cap_yaw);
  float mx = (fx * d->cap_move + fz * d->cap_side) * sp * dt, mz = (fz * d->cap_move - fx * d->cap_side) * sp * dt;
  if (fabsf(d->cap_move) + fabsf(d->cap_side) > 0.1f) d->cap_bob += dt * (d->cap_run ? 14 : 9);
  float x = d->cap.x + mx, z = d->cap.z + mz;
  if (d->cap_bridge) {
    x = clampf(x, BRIDGE_X0, BRIDGE_X1);
    z = clampf(z, BRIDGE_Z0, BRIDGE_Z1);
  } else {
    x = clampf(x, -45.5f, 38.5f);
    z = clampf(z, -155, 160);
    /* the island: undo whichever axis carried us into it */
    bool in = x > ISLAND_X0 && x < ISLAND_X1 && z > ISLAND_Z0 && z < ISLAND_Z1;
    if (in) {
      bool inx = d->cap.x > ISLAND_X0 && d->cap.x < ISLAND_X1, inz = d->cap.z > ISLAND_Z0 && d->cap.z < ISLAND_Z1;
      if (!inz) z = d->cap.z;
      else if (!inx) x = d->cap.x;
      else { x = d->cap.x; z = d->cap.z; }
    }
  }
  d->cap.x = x;
  d->cap.z = z;
}

void defend_damage(Game *g, float amt) {
  Defense *d = &g->def;
  if (d->hp <= 0) return;
  d->hp -= amt;
  for (int i = 0; i < g->nlocals; i++) {
    Plane *q = &g->planes[g->locals[i]];
    if (amt >= 20) game_event(q, d->hp > 0 ? "CARRIER HIT" : "CARRIER SINKING");
  }
}

static int wave_size(int w) { return w + 1 > 6 ? 6 : w + 1; }

static void spawn_wave(Game *g) {
  Defense *d = &g->def;
  d->wave++;
  int n = wave_size(d->wave), k = 0;
  char msg[40];
  snprintf(msg, sizeof msg, "WAVE %d  -  %d BOGEYS", d->wave, n);
  for (int i = 0; i < g->nlocals; i++) game_event(&g->planes[g->locals[i]], msg);
  for (int i = 0; i < g->nplanes && k < n; i++) {
    Plane *p = &g->planes[i];
    if (p->team != 1) continue;
    p->inactive = false;
    p->lives = 1;
    /* come in from the north in a loose line abreast */
    float side = (k - (n - 1) / 2.0f) * 900;
    p->pos = v3(d->pos.x + side + game_rand(g, -200, 200), 1400 + game_rand(g, 0, 1200), d->pos.z + 15000 + game_rand(g, 0, 2500));
    p->f = v3_norm(v3(d->pos.x - p->pos.x, 0, d->pos.z - p->pos.z));
    p->u = v3(0, 1, 0);
    p->r = v3_cross(p->u, p->f);
    p->speed = 230;
    p->vel = v3_scale(p->f, p->speed);
    p->alive = true;
    p->invuln = 0;
    p->fuel = 100;
    p->health = 100;
    p->missiles = 2; /* anti-ship missiles */
    p->flares = MAX_FLR;
    p->ammo = MAX_AMMO;
    p->ai.mslCool = game_rand(g, 1, 4);
    p->ai.refuel = false;
    /* a fresh pilot: nothing carries over from the last life */
    p->gload = 1;
    p->gstress = p->gneg = p->gloc = p->stunT = 0;
    p->lockTarget = ref_none();
    p->locked = false;
    p->lockT = p->gunCool = p->mslCool = 0;
    p->lastHitBy = -1;
    p->ai.threatT = p->ai.egressT = 0;
    p->ai.final_leg = false;
    p->ctl.pitch = p->ctl.roll = 0;
    k++;
  }
  d->between = false;
}

void defend_setup(Game *g, bool commander) {
  Defense *d = &g->def;
  memset(d, 0, sizeof *d);
  d->on = true;
  d->commander = commander;
  d->waves = DEF_WAVES;
  d->hpmax = d->hp = 1000;
  d->deck = 20.5f;
  d->length = 320;
  d->beam = 86;
  if (MAP)
    for (int i = 0; i < MAP->ncarriers; i++)
      if (MAP->carriers[i].team == 0) {
        d->pos = v3(MAP->carriers[i].x, MAP->water_level, MAP->carriers[i].z);
        d->heading = MAP->carriers[i].heading;
        d->deck = MAP->carriers[i].deck;
        break;
      }
  for (int k = 0; k < NMOUNTS; k++) {
    Turret *t = &d->tur[d->nturrets++];
    t->kind = MOUNTS[k].kind;
    t->name = MOUNTS[k].name;
    t->local = v3(MOUNTS[k].x, MOUNTS[k].y, MOUNTS[k].z);
    t->mode = TM_AUTO;
    t->yaw = d->heading * PI_F / 180;
    t->ammo = t->kind == TUR_SAM ? 8 : 0;
    t->target = ref_none();
  }
  d->cmd_state = CMD_WALK;
  place_captain(d, true, v3(28.5f, 0, -38.5f), 0);
  d->cmd_plane = -1;
  d->pick = ref_none();
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    if (p->team == 1) {
      p->inactive = true;
      p->alive = false;
      p->lives = 0;
    } else if (p->hidx == 1 && commander) {
      d->cmd_plane = i;
      p->inactive = true;
      p->alive = false;
      p->lives = 99;
    }
  }
  d->waveT = 6;
  d->between = true;
}

void defend_launch(Game *g, Plane *p) {
  const Defense *d = &g->def;
  /* cat 1 for the pilot, cat 2 for the commander, both on the bow */
  float lx = p->hidx == 1 ? -8.0f : 8.0f;
  float h = d->heading * PI_F / 180;
  p->f = v3_norm(v3(sinf(h), 0.12f, cosf(h)));
  p->u = v3(0, 1, 0);
  p->r = v3_norm(v3_cross(p->u, p->f));
  p->u = v3_cross(p->f, p->r);
  p->pos = to_world(d, v3(lx, d->deck + 8, 165));
  p->speed = 150;
  p->throttle = 1;
  p->vel = v3_scale(p->f, p->speed);
}

void defend_cmd_launch(Game *g) {
  Defense *d = &g->def;
  if (d->cmd_plane < 0 || d->cmd_state == CMD_FLYING) return;
  Plane *p = &g->planes[d->cmd_plane];
  if (d->cmd_state == CMD_TURRET) d->tur[d->sel].mode = TM_AUTO;
  /* the captain is in the jet now */
  d->cmd_state = CMD_FLYING;
  p->inactive = false;
  game_spawn(g, p);
  game_event(p, "LAUNCHED");
}

bool defend_ashm(Game *g, Plane *p) {
  if (p->missiles <= 0 || g->nmissiles >= MAX_MISSILES) return false;
  p->missiles--;
  Missile m = { 0 };
  m.pos = v3_add(p->pos, v3_scale(p->u, -2));
  m.prev = m.pos;
  m.dir = p->f;
  m.speed = p->speed;
  m.maxSpeed = 290;
  m.turn = 1.2f;
  m.life = 40;
  m.team = p->team;
  m.owner = p->idx;
  m.target.kind = REF_CARRIER;
  m.target.idx = 0;
  m.dmg = 140;
  m.ashm = true;
  m.id = ++g->missileSeq;
  g->missiles[g->nmissiles++] = m;
  game_sound(g, SND_NOISE, 1400, 200, 0.9f, 0.08f);
  for (int i = 0; i < g->nlocals; i++) game_event(&g->planes[g->locals[i]], "VAMPIRE! MISSILE INBOUND");
  return true;
}

int defend_contacts(const Game *g, Ref *out, int max) {
  float dist[MAX_PLANES + MAX_MISSILES];
  int n = 0;
  for (int i = 0; i < g->nplanes && n < max; i++) {
    const Plane *p = &g->planes[i];
    if (p->team != 1 || p->inactive || !p->alive) continue;
    out[n] = ref_plane(i);
    dist[n++] = v3_dist(p->pos, g->def.pos);
  }
  for (int i = 0; i < g->nmissiles && n < max; i++) {
    const Missile *m = &g->missiles[i];
    if (!m->ashm) continue;
    Ref r = { REF_MISSILE, (int16_t)i, m->id };
    out[n] = r;
    dist[n++] = v3_dist(m->pos, g->def.pos) - 2000; /* missiles sort first */
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && dist[j] < dist[j - 1]; j--) {
      float td = dist[j];
      dist[j] = dist[j - 1];
      dist[j - 1] = td;
      Ref tr = out[j];
      out[j] = out[j - 1];
      out[j - 1] = tr;
    }
  return n;
}

/* ---- turrets ----------------------------------------------------------- */
#define CIWS_RANGE 2400.0f
#define SAM_RANGE  7500.0f
#define CIWS_SPEED 1100.0f


static Ref auto_target(Game *g, vec3 at, float range) {
  Ref list[MAX_PLANES + MAX_MISSILES];
  int n = defend_contacts(g, list, MAX_PLANES + MAX_MISSILES);
  for (int i = 0; i < n; i++) {
    vec3 pos;
    if (ref_get(g, list[i], &pos, NULL) && v3_dist(pos, at) < range && pos.y > at.y - 15) return list[i];
  }
  return ref_none();
}

static void fire_ciws(Game *g, Turret *t, vec3 at) {
  if (g->nbullets >= MAX_BULLETS) return;
  vec3 dir = v3(sinf(t->yaw) * cosf(t->pitch), sinf(t->pitch), cosf(t->yaw) * cosf(t->pitch));
  dir = v3_norm(v3_add(dir, v3(game_rand(g, -0.008f, 0.008f), game_rand(g, -0.008f, 0.008f), game_rand(g, -0.008f, 0.008f))));
  Bullet *b = &g->bullets[g->nbullets++];
  b->pos = b->prev = v3_add(at, v3_scale(dir, 3));
  b->vel = v3_scale(dir, CIWS_SPEED);
  b->life = 2.4f;
  b->owner = BY_CARRIER;
  b->team = 0;
  game_sound(g, SND_NOISE, 3000, 900, 0.04f, 0.035f);
}

static void fire_sam(Game *g, Turret *t, vec3 at) {
  if (g->nmissiles >= MAX_MISSILES || t->ammo <= 0) return;
  t->ammo--;
  t->reload = 5;
  Missile m = { 0 };
  m.pos = m.prev = v3_add(at, v3(0, 4, 0));
  m.dir = v3_norm(v3(sinf(t->yaw) * cosf(t->pitch), fmaxf(0.4f, sinf(t->pitch)), cosf(t->yaw) * cosf(t->pitch)));
  m.speed = 150;
  m.maxSpeed = 720;
  m.turn = 2.2f;
  m.life = 14;
  m.team = 0;
  m.owner = BY_CARRIER;
  m.target = t->target;
  m.dmg = 100;
  m.sam = true;
  m.id = ++g->missileSeq;
  g->missiles[g->nmissiles++] = m;
  game_sound(g, SND_NOISE, 1600, 200, 0.7f, 0.1f);
}

static void update_turret(Game *g, Turret *t, float dt) {
  Defense *d = &g->def;
  vec3 at = to_world(d, t->local);
  float range = t->kind == TUR_CIWS ? CIWS_RANGE : SAM_RANGE;
  t->cool -= dt;
  t->reload -= dt;
  t->heat = fmaxf(0, t->heat - 0.22f * dt);
  if (t->overheat && t->heat < 0.3f) t->overheat = false;
  if (t->kind == TUR_SAM && t->ammo < 8 && (t->restock += dt) > 20) {
    t->restock = 0;
    t->ammo++;
  }
  t->firing = false;

  if (t->mode == TM_MANUAL) {
    t->yaw = wrap_pi(t->yaw + t->man_yaw * 1.4f * dt);
    t->pitch = clampf(t->pitch + t->man_pitch * 1.0f * dt, -0.05f, 1.45f);
    if (t->kind == TUR_SAM) {
      /* the launcher locks whatever is nearest its line of sight */
      Ref best = ref_none();
      float ba = 0.12f;
      Ref list[MAX_PLANES + MAX_MISSILES];
      int n = defend_contacts(g, list, MAX_PLANES + MAX_MISSILES);
      vec3 fwd = v3(sinf(t->yaw) * cosf(t->pitch), sinf(t->pitch), cosf(t->yaw) * cosf(t->pitch));
      for (int i = 0; i < n; i++) {
        vec3 pos;
        if (!ref_get(g, list[i], &pos, NULL)) continue;
        vec3 to = v3_sub(pos, at);
        float dl = v3_len(to), a = acosf(clampf(v3_dot(to, fwd) / fmaxf(dl, 1), -1, 1));
        if (dl < range && a < ba) { ba = a; best = list[i]; }
      }
      t->target = best;
      if (t->man_fire && t->reload <= 0 && best.kind != REF_NONE) fire_sam(g, t, at);
    } else if (t->man_fire && t->cool <= 0 && !t->overheat) {
      t->cool = 0.035f;
      t->heat += 0.02f;
      if (t->heat >= 1) t->overheat = true;
      t->firing = true;
      fire_ciws(g, t, at);
    }
    return;
  }
  if (t->mode == TM_HOLD) return;
  if (t->mode == TM_ASSIGNED && !ref_get(g, t->target, NULL, NULL)) t->mode = TM_AUTO;
  if (t->mode == TM_AUTO) {
    vec3 tp;
    if (!ref_get(g, t->target, &tp, NULL) || v3_dist(tp, at) > range) t->target = auto_target(g, at, range);
  }
  vec3 pos, vel;
  if (!ref_get(g, t->target, &pos, &vel)) return;
  float dist = v3_dist(pos, at);
  /* lead the target for the shell's flight time */
  vec3 aim = t->kind == TUR_CIWS ? v3_add(pos, v3_scale(vel, dist / CIWS_SPEED)) : pos;
  vec3 to = v3_sub(aim, at);
  float wy = atan2f(to.x, to.z), wp = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z));
  float rate = (t->kind == TUR_CIWS ? 2.6f : 1.3f) * dt;
  float dy = wrap_pi(wy - t->yaw);
  t->yaw = wrap_pi(t->yaw + clampf(dy, -rate, rate));
  t->pitch += clampf(wp - t->pitch, -rate, rate);
  bool aligned = fabsf(wrap_pi(wy - t->yaw)) < 0.03f && fabsf(wp - t->pitch) < 0.03f;
  if (dist > range || pos.y < at.y - 15) return;
  if (t->kind == TUR_CIWS) {
    if (aligned && t->cool <= 0 && !t->overheat) {
      t->cool = 0.035f;
      t->heat += 0.02f;
      if (t->heat >= 1) t->overheat = true;
      t->firing = true;
      fire_ciws(g, t, at);
    }
  } else if (fabsf(wrap_pi(wy - t->yaw)) < 0.3f && t->reload <= 0) {
    fire_sam(g, t, at);
  }
}

void defend_update(Game *g, float dt) {
  Defense *d = &g->def;
  if (!d->on || g->over) return;
  for (int k = 0; k < d->nturrets; k++) update_turret(g, &d->tur[k], dt);
  if (d->commander) captain_update(d, dt);

  /* the commander's jet: back to the control room when it is lost, or once
   * it has landed on the deck and slowed down */
  if (d->cmd_plane >= 0 && d->cmd_state == CMD_FLYING) {
    Plane *p = &g->planes[d->cmd_plane];
    bool landed = p->alive && p->onGround && p->speed < 45 && defend_over_carrier(g, p->pos.x, p->pos.z);
    if (landed || (!p->alive && p->respawnT < 2.5f)) {
      d->cmd_state = CMD_WALK;
      /* climb out where the jet stopped, or wake up on the bridge */
      if (landed) {
        float h = d->heading * PI_F / 180, dx = p->pos.x - d->pos.x, dz = p->pos.z - d->pos.z;
        place_captain(d, false, v3(dx * cosf(h) - dz * sinf(h) - 4, 0, dx * sinf(h) + dz * cosf(h)), 0);
      } else place_captain(d, true, v3(28.5f, 0, -38.5f), 0);
      p->inactive = true;
      p->alive = false;
      p->respawnT = 1e9f;
    }
  }

  /* waves */
  bool any = false;
  for (int i = 0; i < g->nplanes; i++)
    if (g->planes[i].team == 1 && !g->planes[i].inactive && (g->planes[i].alive || g->planes[i].lives > 0)) any = true;
  for (int i = 0; i < g->nmissiles; i++)
    if (g->missiles[i].ashm) any = true;
  if (!any && !d->between) {
    for (int i = 0; i < g->nplanes; i++)
      if (g->planes[i].team == 1) g->planes[i].inactive = true;
    if (d->wave >= d->waves) {
      g->over = true;
      g->overT = 3.2f;
      g->winner = 0;
      snprintf(g->reason, sizeof g->reason, "The carrier held out against all %d waves.", d->waves);
      return;
    }
    d->between = true;
    d->waveT = 8;
    for (int i = 0; i < g->nlocals; i++) game_event(&g->planes[g->locals[i]], "WAVE CLEARED");
  }
  if (d->between && (d->waveT -= dt) <= 0) spawn_wave(g);
  if (d->hp <= 0) {
    g->over = true;
    g->overT = 3.2f;
    g->winner = 1;
    snprintf(g->reason, sizeof g->reason, "The carrier went down in wave %d.", d->wave);
  }
}
