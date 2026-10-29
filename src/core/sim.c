/* sim.c - flight, guns, missiles, flares, damage, tanker refuel (JS lines 2975-3280). */
#include "core/sim.h"

#include <stdio.h>
#include <string.h>

#include "core/ai.h"
#include "core/defend.h"

static void part(Game *g, vec3 pos, vec3 vel, float life, float size, int type, float grow) {
  if (g->nparts >= MAX_PARTS) return;
  Particle *q = &g->parts[g->nparts++];
  q->pos = pos;
  q->vel = vel;
  q->life = q->max = life;
  q->size = size;
  q->type = type;
  q->grow = grow;
}

static vec3 rand3(Game *g, float a, float b) {
  return v3(game_rand(g, a, b), game_rand(g, a, b), game_rand(g, a, b));
}

static void explode(Game *g, vec3 pos, float big) {
  for (int i = 0; i < 26 * big; i++)
    part(g, pos, v3(game_rand(g, -1, 1) * 90 * big, game_rand(g, -0.5f, 1) * 90 * big, game_rand(g, -1, 1) * 90 * big),
         game_rand(g, 0.5f, 1.2f), game_rand(g, 6, 14) * big, 0, 0);
  for (int i = 0; i < 14 * big; i++)
    part(g, v3_add(pos, rand3(g, -10, 10)), v3(game_rand(g, -25, 25), game_rand(g, 5, 35), game_rand(g, -25, 25)),
         game_rand(g, 2, 3.6f), game_rand(g, 10, 18) * big, 1, 14);
  for (int i = 0; i < 10 * big; i++)
    part(g, pos, v3(game_rand(g, -1, 1) * 140, game_rand(g, 0, 1) * 140, game_rand(g, -1, 1) * 140),
         game_rand(g, 1, 2), 3, 2, 0);
  game_sound(g, SND_NOISE, 900, 60, 1.1f * fminf(big, 1.4f), 0.3f);
}

#define ARCADE_TURN 2.0f /* arcade turns this much quicker than the real curve */

static void kill(Game *g, Plane *p, int by, const char *how) {
  if (!p->alive || p->remote) return; /* the computer that flies it says when it goes down */
  if (by >= 0 && by != p->idx && g->planes[by].team != p->team) {
    p->kc[by]++;
    g->planes[by].score += 200; /* classic rules: enemy aircraft destroyed */
  }
  else if (by == BY_CARRIER && p->team != 0) g->def.kills++;
  p->alive = false;
  p->lives--;
  p->respawnT = 4;
  p->deathPos = p->pos;
  p->hasDeath = true;
  p->deathMsg = how;
  p->hitFlash = 1;
  explode(g, p->pos, 1.6f);
  for (int i = 0; i < g->nmissiles; i++)
    if (ref_eq(g->missiles[i].target, ref_plane(p->idx))) g->missiles[i].target = ref_none();
}

static void damage(Game *g, Plane *p, float amt, int by) {
  if (!p->alive || p->invuln > 0) return;
  if (p->remote) { /* its own computer decides; just show the hit */
    p->hitFlash = 1;
    return;
  }
  p->health -= amt;
  p->hitFlash = 1;
  if (by >= 0) {
    p->lastHitBy = by;
    p->lastHitT = g->time;
    if (g->planes[by].team != p->team) g->planes[by].score += amt >= 30 ? 50 : 20; /* missile or cannon hit */
  }
  /* classic rules: a missile hit leaves the controls dead for a few seconds */
  if (g->classic && amt >= 30) {
    p->stunT = 2.5f;
    if (!p->is_ai) game_event(p, "HIT - NO CONTROL");
  }
  if (!p->is_ai) game_sound(g, SND_NOISE, 900, 200, 0.12f, 0.12f);
  if (p->health <= 0) kill(g, p, by, "SHOT DOWN");
}

static void target_down(Game *g, Target *t) {
  char msg[40];
  t->alive = false;
  explode(g, t->pos, 1.3f);
  if (t->by >= 0 && t->by < g->nplanes) g->planes[t->by].score += 2000;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *q = &g->planes[i];
    if (q->hidx < 0) continue;
    if (q->idx == t->by) snprintf(msg, sizeof msg, "%s DESTROYED", TNAME[t->type]);
    else if (q->team == t->team) snprintf(msg, sizeof msg, "YOUR %s IS HIT", TNAME[t->type]);
    else snprintf(msg, sizeof msg, "WINGMAN HIT A %s", TNAME[t->type]);
    game_event(q, msg);
  }
}

static void damage_target(Game *g, Target *t, float amt, int by) {
  if (!t->alive || t->remote) return;
  t->hp -= amt;
  if (t->hp <= 0) {
    t->by = by;
    target_down(g, t);
  }
}

static void fire_gun(Game *g, Plane *p) {
  if (g->nbullets >= MAX_BULLETS) return;
  p->gunCool = 0.075f;
  p->ammo -= 1;
  p->gunSide = -p->gunSide;
  vec3 muzzle = v3_add(v3_add(p->pos, v3_scale(p->f, 8)), v3_scale(p->r, 1.4f * p->gunSide));
  vec3 dir = v3_norm(v3_add(p->f, rand3(g, -0.006f, 0.006f)));
  Bullet *b = &g->bullets[g->nbullets++];
  b->pos = b->prev = muzzle;
  b->vel = v3_add(p->vel, v3_scale(dir, 1150));
  b->life = 1.5f;
  b->owner = p->idx;
  b->team = p->team;
  game_sound(g, SND_NOISE, 2400, 600, 0.06f, p->is_ai ? 0.03f : 0.09f);
}

static void net_event(Game *g, int kind, int plane, bool chaff, const Missile *m) {
  if (!g->net || g->nnetev >= 32) return;
  g->netev[g->nnetev].kind = kind;
  g->netev[g->nnetev].plane = plane;
  g->netev[g->nnetev].chaff = chaff;
  if (m) g->netev[g->nnetev].m = *m;
  g->nnetev++;
}

static void launch(Game *g, const Missile *m) {
  if (g->nmissiles >= MAX_MISSILES) return;
  g->missiles[g->nmissiles] = *m;
  g->missiles[g->nmissiles].id = (uint32_t)g->net_side << 24 | (++g->missileSeq & 0xffffff);
  net_event(g, NE_MISSILE, -1, false, &g->missiles[g->nmissiles]);
  g->nmissiles++;
}

static void fire_missile_kind(Game *g, Plane *p, int kind) {
  if (p->mslCool > 0) return;
  p->mslCool = 0.8f;
  Missile m = { 0 };
  m.pos = v3_add(p->pos, v3_scale(p->u, -2));
  m.prev = p->pos;
  m.dir = p->f;
  m.speed = p->speed + 60;
  m.maxSpeed = 720;
  m.turn = 1.75f;
  m.life = 9;
  m.team = p->team;
  m.owner = p->idx;
  m.target = p->locked ? p->lockTarget : ref_none();
  m.dmg = g->hits_to_kill > 1 ? 100.0f / g->hits_to_kill + 0.5f : 100;
  m.kind = kind;
  if (g->classic) /* longer reach: heat to 8 miles, radar to 25 */
    m.life = kind == MK_RADAR ? 26 : kind == MK_HEAT ? 15 : 12;
  launch(g, &m);
  game_sound(g, SND_NOISE, 1600, 200, 0.7f, p->is_ai ? 0.06f : 0.14f);
}

static void fire_missile(Game *g, Plane *p) {
  if (p->missiles <= 0 || p->mslCool > 0) return;
  p->missiles--;
  fire_missile_kind(g, p, MK_HEAT);
}

/* Classic rules: the fire key fires the selected weapon. Radar and
 * air-to-ground missiles need a lock, and air-to-ground ones are launched
 * from below 2000 ft and under 500 kt. */
static void fire_selected(Game *g, Plane *p) {
  if (p->mslCool > 0) return;
  switch (p->wsel) {
  case W_HEAT:
    if (p->missiles > 0) fire_missile(g, p);
    else game_event(p, "NO HEAT-SEEKERS");
    break;
  case W_RADAR:
    if (p->radar <= 0) game_event(p, "NO RADAR MISSILES");
    else if (!p->locked || p->lockTarget.kind != REF_PLANE) game_event(p, "NO RADAR LOCK");
    else {
      p->radar--;
      fire_missile_kind(g, p, MK_RADAR);
    }
    break;
  case W_GROUND: {
    float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
    if (p->ground <= 0) game_event(p, "NO AIR-TO-GROUND");
    else if (agl > 610) game_event(p, "TOO HIGH - BELOW 2000 FT");
    else if (p->speed > 257) game_event(p, "TOO FAST - UNDER 500 KT");
    else if (!p->locked || p->lockTarget.kind != REF_GROUND) game_event(p, "NO TARGET LOCK");
    else {
      p->ground--;
      fire_missile_kind(g, p, MK_GROUND);
    }
    break;
  }
  default: break;
  }
}

static int alloc_flare(Game *g) {
  for (int i = 0; i < MAX_FLARES; i++)
    if (!g->flares[i].live) return i;
  return -1;
}

/* Flares pull heat-seekers away, chaff pulls radar-guided missiles. */
static void drop_decoys(Game *g, Plane *p, bool chaff) {
  if (chaff ? p->chaff <= 0 : p->flares <= 0) return;
  if (chaff) p->chaff--;
  else p->flares--;
  if (!p->remote) net_event(g, NE_FLARES, p->idx, chaff, NULL);
  Ref made[2] = { ref_none(), ref_none() };
  for (int k = 0; k < 2; k++) {
    int i = alloc_flare(g);
    if (i < 0) break;
    float s = k ? 1.0f : -1.0f;
    Flare *f = &g->flares[i];
    f->pos = p->pos;
    f->vel = v3_add(v3_add(v3_scale(p->vel, 0.35f), v3_scale(p->r, 60 * s)),
                    v3(game_rand(g, -20, 20), -40, game_rand(g, -20, 20)));
    f->life = 3;
    f->live = true;
    f->chaff = chaff;
    f->owner = p->idx;
    f->gen = ++g->flareGen;
    made[k].kind = REF_FLARE;
    made[k].idx = (int16_t)i;
    made[k].gen = f->gen;
  }
  for (int i = 0; i < g->nmissiles; i++) {
    Missile *m = &g->missiles[i];
    if ((m->kind != MK_HEAT) != chaff) continue;
    if (ref_eq(m->target, ref_plane(p->idx)) && v3_dist(m->pos, p->pos) < 2600 && game_rand(g, 0, 1) < 0.6f) {
      Ref pick = made[game_rand(g, 0, 1) < 0.5f ? 0 : 1];
      if (pick.kind == REF_NONE) pick = made[0];
      if (pick.kind != REF_NONE) m->target = pick;
    }
  }
  game_sound(g, SND_NOISE, chaff ? 3000 : 5000, chaff ? 1500 : 2000, 0.25f, p->is_ai ? 0.03f : 0.07f);
}

static void drop_flares(Game *g, Plane *p) { drop_decoys(g, p, false); }

/* Classic: one key for both. Chaff when a radar missile is on us, else flares. */
static void countermeasure(Game *g, Plane *p) {
  bool radar = false;
  for (int i = 0; i < g->nmissiles; i++)
    if (g->missiles[i].kind != MK_HEAT && ref_eq(g->missiles[i].target, ref_plane(p->idx))) radar = true;
  drop_decoys(g, p, radar && p->chaff > 0);
}

void sim_update_tanker(Game *g, float dt) {
  Tanker *T = &g->tanker;
  T->th += T->speed / T->R * dt;
  T->pos = v3(T->cx + cosf(T->th) * T->R, T->alt, T->cz + sinf(T->th) * T->R);
  T->f = v3(-sinf(T->th), 0, cosf(T->th));
  T->u = v3(0, 1, 0);
  T->r = v3_cross(T->u, T->f);
  T->vel = v3_scale(T->f, T->speed);
  T->drogue = v3_add(v3_add(T->pos, v3_scale(T->f, -95)), v3(0, -14, 0));
}

static void lock_on(Game *g, Plane *p, float dt) {
  Ref best = ref_none();
  float bestA = LOCK_CONE;
  /* classic: each weapon's reach, and air-to-ground locks only on the ground */
  bool cl = g->classic && !p->is_ai;
  float airR = !cl ? 4500 : p->wsel == W_RADAR ? 40000 : p->wsel == W_HEAT ? 12900 : p->wsel == W_CANNON ? 1600 : 0;
  float gndR = !cl ? 4000 : p->wsel == W_GROUND ? 8000 : p->wsel == W_CANNON ? 1600 : 0;
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *e = &g->planes[i];
    if (e->team == p->team || !e->alive) continue;
    vec3 d = v3_sub(e->pos, p->pos);
    float dl = v3_len(d);
    if (dl < airR && dl > 0) {
      float a = acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
      if (a < bestA) { best = ref_plane(i); bestA = a; }
    }
  }
  for (int i = 0; i < g->ntargets; i++) {
    const Target *t = &g->targets[i];
    if (!t->alive || t->team == p->team) continue;
    vec3 d = v3_sub(t->pos, p->pos);
    float dl = v3_len(d);
    if (dl < gndR && dl > 0) {
      float a = acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
      if (a < bestA) { best = ref_ground(i); bestA = a; }
    }
  }
  if (best.kind != REF_NONE && ref_eq(best, p->lockTarget)) p->lockT += dt;
  else {
    p->lockTarget = best;
    p->lockT = 0;
  }
  bool was = p->locked;
  p->locked = best.kind != REF_NONE && p->lockT >= LOCK_TIME;
  if (p->locked && !was && !p->is_ai) game_sound(g, SND_SQUARE, 1400, 1400, 0.08f, 0.05f);
}

/* G-load from how hard the stick is pulled at this speed: the full pull
 * reaches the jet's limit above about 150 m/s, less when slow. With G
 * effects on, sustained high G greys out and then blacks out the pilot, and
 * negative G reds them out; a blackout (G-LOC) leaves the jet with no input
 * for a few seconds. */
static void g_forces(Game *g, Plane *p, const JetType *jt, float dt) {
  Controls *c = &p->ctl;
  float bank = p->u.y; /* 1 level, -1 inverted: gravity adds or subtracts */
  float G = bank + p->speed * p->pitchRate / 9.81f / (g->realistic ? 1.0f : ARCADE_TURN); /* the g of the turn flown */
  p->gload += (G - p->gload) * fminf(1, dt * 6);
  if (!g->g_effects) {
    p->gstress = p->gneg = p->gloc = 0;
    return;
  }
  if (p->gload > 6) p->gstress += (p->gload - 6) / 3 * dt;
  else p->gstress = fmaxf(0, p->gstress - (p->gload < 4 ? 0.9f : 0.4f) * dt);
  if (p->gload < -2) p->gneg += (-2 - p->gload) * 0.8f * dt;
  else p->gneg = fmaxf(0, p->gneg - 1.0f * dt);
  if (p->gloc > 0) {
    p->gloc -= dt;
    c->pitch = c->roll = 0;
    c->gun = c->msl = c->flr = c->fire = c->sel = c->map = false;
    if (p->gloc <= 0) {
      p->gstress = 2.5f;
      p->gneg = fminf(p->gneg, 1.5f);
    }
  } else if (p->gstress >= 5 || p->gneg >= 3.5f) {
    p->gloc = 3.5f;
    if (!p->is_ai) game_event(p, p->gneg >= 3.5f ? "REDOUT" : "G-LOC");
  }
}

/* Before take-off. On the carrier the jet sits on the catapult until the
 * power goes to full, then the stroke flings it to flying speed in about
 * two seconds. On a runway it rolls with the throttle and lifts off when the
 * pilot pulls back above the rotation speed (on its own a little later). */
static void take_off(Game *g, Plane *p, const JetType *jt, float dt) {
  Controls *c = &p->ctl;
  const float vr = g->classic ? 80 : 92;
  p->fuel = fmaxf(0, p->fuel - (0.1f + 0.4f * p->throttle) * jt->burn * dt);
  p->invuln = fmaxf(0, p->invuln - dt);
  p->evT -= dt;
  bool fly = false;
  if (p->oncat) {
    if (p->cat <= 0 && p->throttle > 0.85f) {
      p->cat = 0.001f;
      if (!p->is_ai) game_sound(g, SND_NOISE, 3200, 160, 1.6f, 0.3f);
    }
    if (p->cat > 0) {
      p->cat += dt;
      p->speed += 50 * dt;
      fly = p->speed >= 100;
    } else p->speed = 0;
    /* past the bow: flying, whatever the stroke reached */
    if (terrain_h(p->pos.x + p->f.x * 4, p->pos.z + p->f.z * 4) < p->pos.y - 8) {
      fly = true;
      p->speed = fmaxf(p->speed, 100);
    }
  } else {
    float acc = (p->fuel > 0 ? p->throttle : 0) * 15 * jt->speed - 1.5f - p->speed * 0.015f;
    if (p->throttle < 0.05f) acc -= 4; /* brakes */
    p->speed = fmaxf(0, p->speed + acc * dt);
    fly = p->speed > vr && (c->pitch > 0.2f || p->speed > vr + 30);
  }
  p->vel = v3_scale(p->f, p->speed);
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  if (!fly) p->pos.y = terrain_h(p->pos.x, p->pos.z) + 3;
  p->onGround = true;
  p->stall = false;
  if (fly) {
    p->taxi = p->oncat = false;
    p->cat = 0;
    p->onGround = false;
    p->f = v3_norm(v3_add(p->f, v3(0, 0.16f, 0)));
    p->r = v3_norm(v3_cross(p->u, p->f));
    p->u = v3_cross(p->f, p->r);
    if (!p->is_ai) game_event(p, "AIRBORNE");
  }
}

/* The autopilot holds the wings level, the altitude and the heading it was
 * engaged at. Moving the stick takes the jet back. */
static void autopilot(Game *g, Plane *p, float dt) {
  Controls *c = &p->ctl;
  (void)dt;
  if (p->is_ai || p->drone) return; /* the drone's autopilot key holds the vectoring */
  if (c->ap && !p->prevAp) {
    p->autopilot = !p->autopilot;
    p->apAlt = fmaxf(p->pos.y, terrain_h(p->pos.x, p->pos.z) + 300);
    p->apHdg = atan2f(p->f.x, p->f.z);
    game_event(p, p->autopilot ? "AUTOPILOT ON" : "AUTOPILOT OFF");
  }
  p->prevAp = c->ap;
  if (!p->autopilot) return;
  if (c->stick) {
    p->autopilot = false;
    game_event(p, "AUTOPILOT OFF");
    return;
  }
  float hdg = atan2f(p->f.x, p->f.z), err = p->apHdg - hdg;
  while (err > PI_F) err -= 2 * PI_F;
  while (err < -PI_F) err += 2 * PI_F;
  float bank = atan2f(-p->r.y, p->u.y), want = clampf(err * 1.2f, -0.5f, 0.5f);
  c->roll = clampf((want - bank) * 2.5f, -1, 1);
  float climb = clampf((p->apAlt - p->pos.y) * 0.0025f, -0.25f, 0.25f);
  c->pitch = clampf((climb - p->f.y) * 4 + fabsf(bank) * 0.4f, -0.6f, 0.6f);
  c->gun = c->msl = c->fire = false;
}

static bool onDeckPrev(const Game *g, const Plane *p) {
  int deck = MAP ? map_carrier_at(MAP, p->pos.x, p->pos.z) : -1;
  return (deck >= 0 && MAP->carriers[deck].team == p->team) || (g->def.on && defend_over_carrier(g, p->pos.x, p->pos.z));
}

/* A jet flown on the other computer: run on from its last snapshot and ease
 * toward where that says it is now (as the JS netApplyRemote). */
static void apply_remote(Game *g, Plane *p, float dt) {
  p->evT -= dt;
  p->gunCool -= dt;
  if (!p->snap.have || !p->alive) return;
  p->taxi = p->oncat = false; /* the owner rolls it; we follow */
  p->snap.age += dt;
  float age = fminf(0.5f, p->snap.age);
  vec3 pred = v3_add(p->snap.pos, v3_scale(p->snap.vel, age)); /* the path, which on the drone is not the nose */
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  p->pos = v3_dist(p->pos, pred) > 400 ? pred : v3_add(p->pos, v3_scale(v3_sub(pred, p->pos), fminf(1, dt * 6)));
  float k = fminf(1, dt * 12);
  p->f = v3_norm(v3_add(p->f, v3_scale(v3_sub(p->snap.f, p->f), k)));
  p->u = v3_norm(v3_add(p->u, v3_scale(v3_sub(p->snap.u, p->u), k)));
  p->r = v3_norm(v3_cross(p->u, p->f));
  p->u = v3_cross(p->f, p->r);
  p->speed = p->snap.speed;
  p->throttle = p->snap.thr;
  p->vel = p->snap.vel;
  p->onGround = p->pos.y < terrain_h(p->pos.x, p->pos.z) + 3.5f;
  if (p->netFire && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
}

/* Energy (Boyd's specific excess power, Ps = V (T - D) / W), as the
 * acceleration along the flight path: thrust against drag, where the drag of
 * the turn grows with the square of the g pulled, and gravity along the
 * path, so climbing trades speed for height and diving gives it back (the
 * yo-yos, boom and zoom). Thin air at height gives less thrust. Arcade runs
 * the engine and drag faster than life; gravity is always real. */
float sim_excess_power(const Game *g, const Plane *p, const JetType *jt, float thr) {
  float v = fmaxf(p->speed, 30), vmax = 390 * jt->speed, vc = jet_corner(jt);
  float h = fmaxf(0, p->pos.y), rho = expf(-h / 9000);
  float tw = 1.1f * jt->climb; /* full power: thrust a little over the weight */
  float thrust = tw * (0.04f + 0.96f * thr) * powf(rho, 0.7f);
  /* full power just holds top speed. Arcade drags with the square of the
   * speed; realistic has a clean jet's curve, little parasite drag until the
   * transonic rise near top speed, which leaves the excess power for the
   * sheets' climb and sustained turn */
  float x = v / vmax, x2 = x * x;
  float drag = tw * (g->realistic ? 0.3f * x2 + 0.7f * x2 * x2 * x2 * x2 : x2) * powf(rho, 0.7f);
  /* realistic, power back to idle: the speed brakes come out, back to the
   * arcade's drag, so the jet can slow down to land */
  if (g->realistic) drag += tw * 0.7f * x2 * clampf(1 - thr / 0.15f, 0, 1) * powf(rho, 0.7f);
  /* the g the wing is making: never more than it can at this speed (the
   * g felt lags a little behind); on the wheels it carries nothing */
  float n = p->onGround ? 0 : fminf(fabsf(p->gload), fmaxf(1, jet_lift_g(jt, v)));
  float ci = g->realistic && jt->s_induced > 0 ? jt->s_induced : 0.034f; /* arcade: 5 g held at corner speed */
  float turn = ci * n * n * (vc / v) * (vc / v) / fmaxf(rho, 0.2f);
  float k = g->realistic ? 1.0f : 2.2f, kt = g->realistic ? 1.0f : 1.2f, kg = g->realistic ? 1.0f : 1.5f;
  return 9.81f * (k * (thrust - drag) - kt * turn - kg * p->f.y);
}

/* Outside the combat area the jet is turned back toward the middle; in
 * classic rules, flown off its own end low enough, it lands back on its
 * carrier. Returns true when it did (and was launched again). */
static bool map_edge(Game *g, Plane *p, float dt) {
  p->out = fabsf(p->pos.x) > ARENA_X || fabsf(p->pos.z) > ARENA_Z;
  /* classic: fly off your own end of the map below 1000 ft to land back
   * on the carrier: refuelled, rearmed and launched again */
  if (g->classic && p->out && (p->team == 0 ? p->pos.z < -ARENA_Z : p->pos.z > ARENA_Z) &&
      p->pos.y - terrain_h(p->pos.x, p->pos.z) < 305) {
    int lives = p->lives, w = p->wsel;
    game_spawn(g, p);
    p->lives = lives;
    p->wsel = w;
    p->invuln = 0;
    game_event(p, "BACK ON THE CARRIER - REARMED");
    return true;
  }
  if (p->out) {
    vec3 to = v3(-p->pos.x, 0, -p->pos.z);
    float a = signf(p->f.z * to.x - p->f.x * to.z) * 0.9f * dt;
    p->f = rot_y(p->f, a);
    p->u = rot_y(p->u, a);
    p->r = rot_y(p->r, a);
    p->vel = rot_y(p->vel, a);
  }
  return false;
}

/* A jet's flight for one tick. Returns true when it landed back on its
 * carrier (classic) and was launched again. */
static bool fly_jet(Game *g, Plane *p, const JetType *jt, float thrEff, float dt) {
  Controls *c = &p->ctl;
  /* Turning (basic fighter manoeuvres): the rate is the g pulled over the
   * speed. The g the wing can pull grows with speed squared up to the
   * airframe limit, reached at corner speed, so the turn is fastest there:
   * slower the wing runs out of lift, faster the circle grows. Arcade turns
   * ARCADE_TURN times quicker on the same curve; a bank alone turns as fast
   * as gravity allows. */
  float v = fmaxf(p->speed, 60), boost = g->realistic ? 1.0f : ARCADE_TURN;
  float lift = jet_lift_g(jt, v), lo = -fminf(-jt->g_min, lift);
  float rate;
  if (g->realistic) {
    /* Realistic: the load n is the lift over the weight, and gravity bends
     * the path too. The fly-by-wire holds 1 g with the stick at rest, full
     * back is the most the wing (or the airframe) gives, full forward the
     * negative limit. Across the wings' plane the path turns by the lift
     * less gravity's share (level: none at 1 g); along the wings gravity
     * sags it toward the low wing. A level turn then comes out at
     * g sqrt(n^2 - 1) / v, the textbook rate. */
    float n = c->pitch >= 0 ? 1 + c->pitch * (lift - 1) : 1 + c->pitch * (1 - lo);
    n = clampf(n, lo, lift);
    rate = (n - p->u.y) * 9.81f / v;
    p->pitchRate = rate;
    rot_pair(&p->f, &p->u, rate * dt);
    rot_pair(&p->u, &p->r, c->roll * 2.5f * jt->roll * dt);
    rot_pair(&p->f, &p->r, -p->r.y * 9.81f / v * dt);
    p->u = v3_norm(v3_cross(p->f, p->r));
  } else {
    /* the pull, kept so the load it makes on this path (with gravity's
     * share, p->u.y) stays inside the airframe's limits */
    float pull = c->pitch >= 0 ? c->pitch * lift : c->pitch * -lo;
    pull = clampf(pull, jt->g_min - p->u.y, jt->g_max - p->u.y);
    rate = boost * pull * 9.81f / v;
    float yawk = boost * 9.81f / v;
    p->pitchRate = rate;
    rot_pair(&p->f, &p->u, rate * dt);
    rot_pair(&p->u, &p->r, c->roll * 2.5f * jt->roll * dt);
    float yaw = -p->r.y * yawk * dt;
    p->f = rot_y(p->f, yaw);
    p->u = rot_y(p->u, yaw);
    p->r = rot_y(p->r, yaw);
  }

  /* classic rules: stall at 140 kt */
  const float vs = g->classic ? 72 : 90;
  p->stall = p->speed < vs && !p->onGround;
  float sink = 0;
  if (p->stall) {
    float k = (vs - p->speed) / vs;
    p->f = v3_norm(v3_add(p->f, v3(0, -1.5f * k * dt, 0)));
    sink = k * 60;
  }

  if (map_edge(g, p, dt)) return true;
  if (p->onGround && p->f.y < 0) p->f.y = 0;
  p->f = v3_norm(p->f);
  p->r = v3_norm(v3_cross(p->u, p->f));
  p->u = v3_cross(p->f, p->r);

  if (p->onGround && p->throttle < 0.1f) {
    /* rolling out after a landing: wheel brakes (and the deck's arresting wire) */
    p->speed = fmaxf(0, p->speed - (onDeckPrev(g, p) ? 30 : 9) * dt);
  } else {
    p->speed += sim_excess_power(g, p, jt, thrEff) * dt;
    p->speed = clampf(p->speed, p->onGround ? 0 : 30, 560);
  }
  p->vel = v3_scale(p->f, p->speed);
  p->vel.y -= sink;
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  if (p->pos.y > (g->classic ? 18288 : 7500)) p->pos.y = g->classic ? 18288 : 7500; /* 60,000 ft */
  return false;
}

/* v turned about the unit axis k by a (Rodrigues). */
static vec3 rot_axis(vec3 v, vec3 k, float a) {
  float c = cosf(a), s = sinf(a);
  return v3_add(v3_add(v3_scale(v, c), v3_scale(v3_cross(k, v), s)), v3_scale(k, v3_dot(k, v) * (1 - c)));
}

/* Tells everyone flying here which post-stall move the drone just pulled. */
static void stunt_done(Game *g, Plane *p, int kind) {
  char msg[40];
  p->lastStunt = kind;
  if (kind == STUNT_NONE) return;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *q = &g->planes[i];
    if (q->hidx < 0 || q->remote || !q->alive) continue;
    if (q == p) snprintf(msg, sizeof msg, "%s!", STUNT_NAMES[kind]);
    else snprintf(msg, sizeof msg, "THE DRONE PULLED A %s", STUNT_NAMES[kind]);
    game_event(q, msg);
  }
}

/* The drone (Beat the Drone!): no pilot, and a nozzle that swings the
 * thrust. Its nose and its flight path part company. In normal flight the
 * wing drags the path round after the nose the way the jets fly (and as
 * fast, a little quicker to roll). With the vector key held the nozzle
 * swings the nose at the same rate at any speed, far past the stall: the
 * wing flown broadside piles up drag and the speed falls away (the Cobra);
 * hold the pull and the nose goes all the way round (the flip); slow
 * enough, the thrust itself pulls the path round onto the nose, so it can
 * reverse in its own length (the J-turn, Herbst's manoeuvre). Let go and
 * the nose weathervanes back onto the path. Arcade and realistic use the
 * same constants as the jets. */
#define DRONE_VEC_RATE 3.0f  /* nose rate the nozzle gives at full deflection, rad/s, at any speed */
#define DRONE_THRUST   1.5f  /* thrust (and drag) against a jet's: the same top speed, more push */
static bool fly_drone(Game *g, Plane *p, const JetType *jt, float thr, float dt) {
  Controls *c = &p->ctl;
  const float vsl = g->classic ? 72 : 90;
  float boost = g->realistic ? 1.0f : ARCADE_TURN;
  float v = p->speed;
  vec3 vd = v > 1 ? v3_scale(p->vel, 1 / v) : p->f;
  bool vec = c->vec && !p->onGround && thr > 0;
  p->vecOn = vec;
  float vs = fmaxf(v, 60), lift = jet_lift_g(jt, vs);

  /* the nose: the wing's rate in normal flight, the nozzle's when vectoring */
  float rate;
  if (vec) rate = c->pitch * DRONE_VEC_RATE * (g->realistic ? 0.75f : 1.0f);
  else {
    float pull = c->pitch >= 0 ? c->pitch * lift : c->pitch * fminf(-jt->g_min, lift);
    rate = boost * pull * 9.81f / vs;
  }
  rot_pair(&p->f, &p->u, rate * dt);
  rot_pair(&p->u, &p->r, c->roll * 2.5f * jt->roll * (vec ? 2.4f : 1.5f) * dt);
  /* a bank turns the path as gravity allows (as on the jets), less so when
   * the wing is not flying */
  float yaw = -p->r.y * boost * 9.81f / vs * dt * (p->aoa < 0.6f ? 1.0f : 0.3f);
  p->f = rot_y(p->f, yaw);
  p->u = rot_y(p->u, yaw);
  p->r = rot_y(p->r, yaw);
  vd = rot_y(vd, yaw);

  /* let go of the nozzle and the nose weathervanes back onto the path */
  float alpha = acosf(clampf(v3_dot(p->f, vd), -1, 1));
  if (!vec && alpha > 0.15f && v > 25) {
    vec3 ax = v3_cross(p->f, vd);
    ax = v3_len(ax) > 1e-3f ? v3_norm(ax) : p->r;
    float step = fminf(alpha, (2.5f + 1.5f * alpha) * dt);
    p->f = rot_axis(p->f, ax, step);
    p->u = rot_axis(p->u, ax, step);
    alpha -= step;
  }

  /* the path: the wing pulls it round toward the nose, at most as hard as
   * it can lift (less past the stall angle); the thrust's share across the
   * path pulls too, which is everything when slow */
  float h = fmaxf(0, p->pos.y), rho = expf(-h / 9000), rk = powf(rho, 0.7f);
  float k = g->realistic ? 1.0f : 2.2f, kt = g->realistic ? 1.0f : 1.2f, kg = g->realistic ? 1.0f : 1.5f;
  float tw = 1.1f * jt->climb * DRONE_THRUST, vmax = 390 * jt->speed, vc = jet_corner(jt);
  float thrust = 9.81f * k * tw * (0.04f + 0.96f * thr) * rk;
  float cl = alpha < 0.45f ? 1.0f : fmaxf(0.06f, 1 - (alpha - 0.45f) * 1.6f); /* the wing stalls past 26 degrees */
  float sa = alpha < PI_F / 2 ? sinf(alpha) : 1; /* past 90 degrees the nozzle still pushes across, and tail first drags */
  /* (vectoring, the nose moves faster than the wing can follow: it lifts
   * as it really would, without the arcade boost) */
  float side = (vec ? 1.0f : boost) * lift * 9.81f * cl + thrust * sa * clampf(1 - v / 150, 0.2f, 1);
  float turn = fminf(alpha, fminf(alpha * (vec ? 4.0f : 10.0f), side / fmaxf(v, 15)) * dt);
  if (turn > 1e-6f) {
    vec3 ax = v3_cross(vd, p->f);
    ax = v3_len(ax) > 1e-3f ? v3_norm(ax) : p->r; /* nose dead astern: over the top */
    vd = v3_norm(rot_axis(vd, ax, turn));
  }

  /* speed: thrust along the path, less the jets' drag, the wing's drag
   * when flown broadside (the Cobra's brake) and gravity along the path */
  float n = fminf(fabsf(p->gload), fmaxf(1, lift));
  float drag = 9.81f * k * tw * (v / vmax) * (v / vmax) * rk;
  float induced = 9.81f * kt * 0.034f * n * n * (vc / vs) * (vc / vs) / fmaxf(rho, 0.2f);
  float broad = 9.81f * k * sa * sa * (v / 70) * (v / 70);
  v = fmaxf(0, v + (thrust * cosf(alpha) - drag - induced - broad - 9.81f * kg * vd.y) * dt);
  /* below the stall speed the wing holds up less than the weight; nose
   * high, the nozzle holds up the rest */
  float hold = clampf(v / vsl, 0, 1);
  hold *= hold * (alpha < 0.6f ? 1.0f : cl);
  float sag = fmaxf(0, 9.81f * kg * (1 - hold) - thrust * fmaxf(0, p->f.y));
  vec3 vel = v3_scale(vd, v);
  vel.y -= sag * dt;
  v = v3_len(vel);
  vd = v > 1 ? v3_scale(vel, 1 / v) : p->f;
  p->stall = !vec && v < vsl && !p->onGround;

  /* the g the airframe feels, and no pilot to grey out */
  float G = p->u.y + turn / dt * v / 9.81f / boost;
  p->gload += (G - p->gload) * fminf(1, dt * 6);
  p->gstress = p->gneg = p->gloc = 0;
  p->pitchRate = turn / dt;

  /* which move it was: a full turn of the nose is a flip, the heading
   * reversed a J-turn, and the nose flung up 65 degrees over the path a Cobra */
  if (vec && !p->stunt) {
    p->stunt = true;
    p->stuntV0 = vd;
    p->stuntPitch = p->stuntMaxA = 0;
  }
  if (p->stunt) {
    p->stuntPitch += rate * dt;
    p->stuntMaxA = fmaxf(p->stuntMaxA, asinf(clampf(p->f.y, -1, 1)) - asinf(clampf(vd.y, -1, 1))); /* nose above the path */
    if (!vec && alpha < 0.3f) {
      p->stunt = false;
      vec3 h0 = v3_norm(v3(p->stuntV0.x, 0, p->stuntV0.z)), h1 = v3(vd.x, 0, vd.z);
      bool back = v3_len(h1) > 0.3f && v3_dot(v3_norm(h1), h0) < -0.3f;
      int kind = fabsf(p->stuntPitch) > 5.4f ? STUNT_FLIP : back ? STUNT_JTURN
               : p->stuntMaxA > 1.1f ? STUNT_COBRA : STUNT_NONE;
      stunt_done(g, p, kind);
    }
  }

  p->vel = vel;
  if (map_edge(g, p, dt)) return true;
  vd = v > 1 ? v3_scale(p->vel, 1 / v) : p->f;
  if (p->onGround && p->f.y < 0) p->f.y = 0;
  p->f = v3_norm(p->f);
  p->r = v3_norm(v3_cross(p->u, p->f));
  p->u = v3_cross(p->f, p->r);
  p->aoa = acosf(clampf(v3_dot(p->f, vd), -1, 1));
  p->speed = clampf(v, 0, 560);
  p->vel = v3_scale(vd, p->speed);
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  if (p->pos.y > (g->classic ? 18288 : 7500)) p->pos.y = g->classic ? 18288 : 7500;
  return false;
}

static void update_plane(Game *g, Plane *p, float dt) {
  p->hitFlash = fmaxf(0, p->hitFlash - dt * 2);
  if (p->remote) {
    apply_remote(g, p, dt);
    return;
  }
  if (!p->alive) {
    p->respawnT -= dt;
    if (p->respawnT <= 0 && p->lives > 0 && !g->over) game_spawn(g, p);
    return;
  }
  Controls *c = &p->ctl;
  const Base *b = &BASES[p->team];
  const JetType *jt = jet_type(p->jet, g->realistic);
  if (p->taxi) {
    take_off(g, p, jt, dt);
    return;
  }
  autopilot(g, p, dt);
  if (p->stunT > 0) {
    p->stunT -= dt;
    c->pitch = c->roll = 0;
    c->gun = c->msl = c->flr = c->fire = false;
  }
  if (!p->drone) g_forces(g, p, jt, dt);
  p->invuln = fmaxf(0, p->invuln - dt);
  p->evT -= dt;
  p->gunCool -= dt;
  p->mslCool -= dt;
  /* the drone carries fuel where a cockpit would be: three times the endurance */
  p->fuel = fmaxf(0, p->fuel - (0.18f + 0.55f * p->throttle) * jt->burn * (p->drone ? 0.33f : 1.0f) * dt);
  float thrEff = p->fuel > 0 ? p->throttle : 0;
  if (p->drone) {
    if (fly_drone(g, p, jt, thrEff, dt)) return;
  } else if (fly_jet(g, p, jt, thrEff, dt)) return;

  /* ground contact and runway */
  float h = terrain_h(p->pos.x, p->pos.z);
  int deck = MAP ? map_carrier_at(MAP, p->pos.x, p->pos.z) : -1;
  bool onDeck = deck >= 0 && MAP->carriers[deck].team == p->team;
  bool onRunway = (fabsf(p->pos.x - b->x) < 35 && fabsf(p->pos.z - b->z) < 1060) || onDeck; /* 45 x 2000 m and a margin */
  p->onGround = false;
  if (p->pos.y < h + 3.05f) { /* resting on the gear counts as contact too */
    if (onRunway && p->f.y > -0.25f && p->speed < 240 && fabsf(p->r.y) < 0.35f) {
      p->pos.y = h + 3;
      p->onGround = true;
      rot_pair(&p->u, &p->r, clampf(atan2f(-p->r.y, p->u.y), -1, 1) * -2 * dt);
    } else if (g->no_crash) {
      /* crash detection off: bounce off the ground or the sea */
      p->pos.y = h + 3;
      if (p->f.y < 0.15f) {
        p->f = v3_norm(v3(p->f.x, 0.15f, p->f.z));
        p->r = v3_norm(v3_cross(p->u, p->f));
        p->u = v3_cross(p->f, p->r);
      }
    } else {
      kill(g, p, p->lastHitBy >= 0 && g->time - p->lastHitT < 6 ? p->lastHitBy : -1, "CRASHED");
      return;
    }
  }

  /* refuelling */
  const Tanker *T = &g->tanker;
  bool atBase = ((fabsf(p->pos.x - b->x) < 220 && fabsf(p->pos.z - b->z) < 1100) || onDeck) && p->pos.y < h + 150 && p->speed < 230;
  bool atTanker = v3_dist(p->pos, T->drogue) < 150 && fabsf(p->speed - T->speed) < 70;
  p->refuel = atBase || atTanker;
  if (p->refuel) {
    p->fuel = fminf(100, p->fuel + 14 * dt);
    p->health = fminf(100, p->health + 8 * dt);
    /* back up to what the jet took off with */
    int full[4], fl = g->classic ? 6 : MAX_FLR;
    game_full_load(g, p, full);
    p->ammo = fminf((float)full[3], p->ammo + (g->classic ? 480 : 80) * dt);
    p->rearmT += dt;
    if (p->rearmT > 1.4f) {
      p->rearmT = 0;
      if (p->missiles < full[0]) p->missiles++;
      if (p->radar < full[1]) p->radar++;
      if (p->ground < full[2]) p->ground++;
      p->flares = p->flares + 2 > fl ? fl : p->flares + 2;
      p->chaff = p->chaff + 2 > fl ? fl : p->chaff + 2;
    }
  }
  if (v3_dist(p->pos, T->pos) < 28) {
    kill(g, p, -1, "HIT THE TANKER");
    return;
  }

  lock_on(g, p, dt);

  /* weapons */
  if (g->classic && !p->is_ai) {
    static const char *const WNAME[W_COUNT] = { "CANNON", "HEAT-SEEKER", "RADAR-GUIDED", "AIR-TO-GROUND" };
    if (c->sel && !p->prevSel) {
      p->wsel = (p->wsel + 1) % W_COUNT;
      game_event(p, WNAME[p->wsel]);
    }
    if (p->wsel == W_CANNON) {
      if (c->fire && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    } else if (c->fire && !p->prevFire) fire_selected(g, p);
    if (c->flr && !p->prevFlr) countermeasure(g, p);
    if (c->map && !p->prevMap) p->mapview = !p->mapview;
    /* the modern keys still work alongside */
    if (c->gun && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    if (c->msl && !p->prevMsl) fire_missile(g, p);
  } else {
    if (c->gun && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    if (c->msl && !p->prevMsl) fire_missile(g, p);
    if (c->flr && !p->prevFlr) drop_flares(g, p);
  }
  p->prevMsl = c->msl;
  p->prevFlr = c->flr;
  p->prevSel = c->sel;
  p->prevMap = c->map;
  p->prevFire = c->fire;

  /* warnings and beeps */
  p->warn = false;
  for (int i = 0; i < g->nmissiles; i++)
    if (ref_eq(g->missiles[i].target, ref_plane(p->idx)) && v3_dist(g->missiles[i].pos, p->pos) < 3500) p->warn = true;
  if (!p->is_ai) {
    p->beepT -= dt;
    if (p->beepT <= 0) {
      if (p->warn) { game_sound(g, SND_SQUARE, 1800, 1100, 0.12f, 0.06f); p->beepT = 0.22f; }
      else if (p->locked) { game_sound(g, SND_SINE, 1250, 1250, 0.05f, 0.035f); p->beepT = 0.12f; }
      else if (p->lockTarget.kind != REF_NONE) { game_sound(g, SND_SINE, 700, 700, 0.05f, 0.025f); p->beepT = 0.4f; }
      else p->beepT = 0.05f;
    }
  }
}

static void update_targets(Game *g, float dt) {
  for (int i = 0; i < g->ntargets; i++) {
    Target *t = &g->targets[i];
    if (!t->alive) {
      t->smokeT -= dt;
      if (t->smokeT <= 0) {
        t->smokeT = 0.35f;
        part(g, v3_add(t->pos, v3(game_rand(g, -8, 8), 4, game_rand(g, -8, 8))),
             v3(game_rand(g, -4, 4), game_rand(g, 25, 40), game_rand(g, -4, 4)), 4, 14, 1, 10);
      }
      continue;
    }
    t->spin += dt * 1.2f;
    if (t->type != TGT_SAM && t->type != TGT_SHIP) continue;
    t->cool -= dt;
    Plane *e = game_enemy_of(g, t->team, t->pos);
    if (!t->remote && t->cool <= 0 && e && e->alive && e->invuln <= 0 && e->pos.y < t->ground + 3500 && v3_dist(e->pos, t->pos) < 3800) {
      t->cool = game_rand(g, 9, 13);
      Missile m = { 0 };
      m.pos = m.prev = v3_add(t->pos, v3(0, t->type == TGT_SHIP ? 20.0f : 14.0f, 0));
      m.dir = v3_norm(v3_add(v3_norm(v3_sub(e->pos, t->pos)), v3(0, 1, 0)));
      m.speed = 140;
      m.maxSpeed = 540;
      m.turn = 1.25f;
      m.life = 11;
      m.team = t->team;
      m.owner = -1;
      m.target = ref_plane(e->idx);
      m.dmg = 70;
      m.sam = true;
      launch(g, &m);
      game_event(e, "SAM LAUNCH");
      game_sound(g, SND_NOISE, 1200, 150, 0.8f, 0.08f);
    }
  }
}

static void update_bullets(Game *g, float dt) {
  for (int i = g->nbullets - 1; i >= 0; i--) {
    Bullet *bl = &g->bullets[i];
    bl->prev = bl->pos;
    bl->pos = v3_add(bl->pos, v3_scale(bl->vel, dt));
    bl->life -= dt;
    bool hit = false;
    for (int k = 0; k < g->nplanes && !hit; k++) {
      Plane *e = &g->planes[k];
      if (e->team == bl->team || !e->alive || seg_dist(bl->prev, bl->pos, e->pos) >= 11) continue;
      damage(g, e, 7, bl->owner);
      hit = true;
      for (int n = 0; n < 4; n++) part(g, bl->pos, rand3(g, -40, 40), 0.3f, 3, 3, 0);
    }
    if (!hit && bl->pos.y < 40 + (MAP ? 3500.0f : 0.0f)) {
      for (int k = 0; k < g->ntargets; k++) {
        Target *t = &g->targets[k];
        if (!t->alive || t->team == bl->team) continue;
        const TargetDef *d = &TDEF[t->type];
        /* ships lie along their heading; the maps only use 0 and 180 for ground sites */
        bool across = fabsf(sinf(t->heading * PI_F / 180)) > 0.7f;
        float hx = across ? d->hz : d->hx, hz = across ? d->hx : d->hz;
        if (fabsf(bl->pos.x - t->pos.x) < hx + 3 && fabsf(bl->pos.z - t->pos.z) < hz + 3 && bl->pos.y - t->ground < d->h + 3) {
          damage_target(g, t, 8, bl->owner);
          hit = true;
          part(g, bl->pos, v3(0, 30, 0), 0.4f, 5, 3, 0);
          break;
        }
      }
    }
    /* shells against missiles: the CIWS's real job */
    for (int k = 0; k < g->nmissiles && !hit; k++) {
      Missile *m = &g->missiles[k];
      if (m->team == bl->team || m->life <= 0 || seg_dist(bl->prev, bl->pos, m->pos) >= 7) continue;
      m->life = -1; /* destroyed: update_missiles blows it up */
      if (m->ashm) g->def.ashm_down++;
      hit = true;
    }
    if (!hit && g->def.on && bl->team == 1 && bl->pos.y < g->def.deck + 12 && defend_over_carrier(g, bl->pos.x, bl->pos.z)) {
      defend_damage(g, 0.6f);
      hit = true;
      part(g, bl->pos, v3(0, 20, 0), 0.4f, 4, 3, 0);
    }
    if (!hit && bl->pos.y < terrain_h(bl->pos.x, bl->pos.z)) {
      hit = true;
      part(g, bl->pos, v3(game_rand(g, -10, 10), 25, game_rand(g, -10, 10)), 0.7f, 6, 1, 8);
    }
    if (hit || bl->life <= 0) g->bullets[i] = g->bullets[--g->nbullets];
  }
}

static void update_missiles(Game *g, float dt) {
  for (int i = g->nmissiles - 1; i >= 0; i--) {
    Missile *m = &g->missiles[i];
    m->life -= dt;
    vec3 tpos, tvel;
    if (m->target.kind != REF_NONE && !ref_get(g, m->target, NULL, NULL)) m->target = ref_none();
    bool guided = true;
    if (m->kind != MK_HEAT && !m->remote && m->owner >= 0 && m->target.kind != REF_FLARE) {
      /* radar-guided: steered by the launcher's radar, so it flies on
       * straight while the launcher has lost the lock */
      const Plane *o = &g->planes[m->owner];
      guided = o->alive && o->locked && ref_eq(o->lockTarget, m->target);
    }
    if (guided && ref_get(g, m->target, &tpos, &tvel)) {
      if (m->ashm) {
        /* sea-skimming: hold 15 m until the last 1.5 km, then the hull */
        float hd = sqrtf((tpos.x - m->pos.x) * (tpos.x - m->pos.x) + (tpos.z - m->pos.z) * (tpos.z - m->pos.z));
        if (hd > 1500) tpos.y = (MAP ? MAP->water_level : 0) + 15;
      }
      float dl = v3_dist(tpos, m->pos);
      vec3 aim = v3_add(tpos, v3_scale(tvel, clampf(dl / m->speed, 0, 2) * 0.6f));
      vec3 want = v3_norm(v3_sub(aim, m->pos));
      float ang = acosf(clampf(v3_dot(m->dir, want), -1, 1));
      if (ang > 1.3f) m->target = ref_none();
      else if (ang > 1e-4f) {
        float k = fminf(1, m->turn * dt / ang);
        m->dir = v3_norm(v3_add(v3_scale(m->dir, 1 - k), v3_scale(want, k)));
      }
    }
    m->speed = fminf(m->maxSpeed, m->speed + 420 * dt);
    m->prev = m->pos;
    m->pos = v3_add(m->pos, v3_scale(m->dir, m->speed * dt));
    m->smokeT -= dt;
    if (m->smokeT <= 0) {
      m->smokeT = 0.03f;
      part(g, m->prev, rand3(g, -3, 3), 1.6f, 4, 1, 9);
    }
    bool boom = m->life < -0.5f; /* shot down */
    /* network play: a hit on a jet the other computer flies is that
     * computer's to judge, on its own copy of this missile; ours just goes
     * out of the sky without telling it to remove that copy */
    bool theirs = false;
    if (!boom && m->team == 1 && g->def.on && defend_over_carrier(g, m->pos.x, m->pos.z) && m->pos.y < g->def.deck + 15) {
      boom = true;
      defend_damage(g, m->ashm ? m->dmg : 20);
    }
    if (!boom && m->target.kind != REF_CARRIER && ref_get(g, m->target, &tpos, NULL) && seg_dist(m->prev, m->pos, tpos) < 26) {
      boom = true;
      if (m->target.kind == REF_PLANE) {
        theirs = g->planes[m->target.idx].remote;
        damage(g, &g->planes[m->target.idx], m->dmg, m->owner);
      }
      else if (m->target.kind == REF_GROUND) {
        theirs = g->targets[m->target.idx].remote;
        damage_target(g, &g->targets[m->target.idx], 100, m->owner);
      }
      else if (m->target.kind == REF_MISSILE) /* a SAM on an inbound missile */
        for (int k = 0; k < g->nmissiles; k++) {
          Missile *v = &g->missiles[k];
          if (v->id != m->target.gen || v->life <= 0) continue;
          v->life = -1;
          if (v->ashm) g->def.ashm_down++;
        }
    }
    for (int k = 0; k < g->nplanes && !boom; k++) {
      Plane *e = &g->planes[k];
      if (e->team != m->team && e->alive && seg_dist(m->prev, m->pos, e->pos) < 16) {
        boom = true;
        theirs = e->remote;
        damage(g, e, m->dmg, m->owner);
      }
    }
    if (!boom && m->pos.y < terrain_h(m->pos.x, m->pos.z) + 2) {
      boom = true;
      for (int k = 0; k < g->ntargets; k++) {
        Target *t = &g->targets[k];
        if (t->alive && t->team != m->team && v3_dist(t->pos, m->pos) < 70) {
          theirs |= t->remote;
          damage_target(g, t, 100, m->owner);
        }
      }
    }
    if (boom || m->life <= 0) {
      if (!m->remote && !theirs) net_event(g, NE_GONE, -1, false, m);
      explode(g, m->pos, boom ? 0.8f : 0.5f);
      g->missiles[i] = g->missiles[--g->nmissiles];
    }
  }
}

static void update_flares_parts(Game *g, float dt) {
  for (int i = 0; i < MAX_FLARES; i++) {
    Flare *f = &g->flares[i];
    if (!f->live) continue;
    f->life -= dt;
    f->vel.y -= 30 * dt;
    f->vel = v3_scale(f->vel, 1 - 0.6f * dt);
    f->pos = v3_add(f->pos, v3_scale(f->vel, dt));
    if (game_rand(g, 0, 1) < 0.5f) part(g, f->pos, v3(0, 4, 0), 0.8f, 4, 1, 6);
    if (f->life <= 0) f->live = false;
  }
  for (int i = g->nparts - 1; i >= 0; i--) {
    Particle *q = &g->parts[i];
    q->life -= dt;
    if (q->life <= 0) {
      g->parts[i] = g->parts[--g->nparts];
      continue;
    }
    q->pos = v3_add(q->pos, v3_scale(q->vel, dt));
    q->vel = v3_scale(q->vel, 1 - (q->type == 2 ? 0.2f : 1.4f) * dt);
    if (q->type == 2) q->vel.y -= 120 * dt;
    q->size += q->grow * dt;
  }
}

static void tally(Game *g) {
  for (int i = 0; i < g->nplanes; i++) {
    Plane *q = &g->planes[i];
    int k = 0, t = 0;
    for (int j = 0; j < g->nplanes; j++) k += g->planes[j].kc[i];
    for (int j = 0; j < g->ntargets; j++)
      if (!g->targets[j].alive && g->targets[j].by == i) t++;
    q->kills = k;
    q->tkills = t;
    if (q->kills > q->prevKills && q->hidx >= 0) game_event(q, "ENEMY DOWN");
    q->prevKills = q->kills;
  }
}

static bool out_of_jets(const Game *g, int team) {
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q->team == team && (q->lives > 0 || q->alive)) return false;
  }
  return true;
}

static bool targets_left(const Game *g, int team) {
  for (int i = 0; i < g->ntargets; i++)
    if (g->targets[i].team == team && g->targets[i].alive) return true;
  return false;
}

static void possessive(const Plane *p, char *buf, size_t n) {
  if (!strcmp(p->name, "YOU")) snprintf(buf, n, "Your");
  else if (!strcmp(p->name, "CPU")) snprintf(buf, n, "The CPU's");
  else {
    /* "PLAYER 1" -> "Player 1's" */
    size_t k = 0;
    for (const char *s = p->name; *s && k + 3 < n; s++, k++)
      buf[k] = (char)(k == 0 ? *s : (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s));
    snprintf(buf + k, n - k, "'s");
  }
}

static void reason_for(Game *g, int t, bool jets) {
  int count = 0, first = -1;
  bool mine = false;
  for (int i = 0; i < g->nplanes; i++)
    if (g->planes[i].team == t) {
      if (first < 0) first = i;
      count++;
    }
  for (int i = 0; i < g->nlocals; i++)
    if (g->planes[g->locals[i]].team == t) mine = true;
  const char *who;
  if (g->coop) who = t == 0 ? "your squadron" : "the CPU squadron";
  else if (count > 1) who = mine ? "your team" : (t == 0 ? "the blue team" : "the red team");
  else {
    char pos[24];
    possessive(&g->planes[first], pos, sizeof pos);
    if (jets) snprintf(g->reason, sizeof g->reason, "%s last jet is down.", pos);
    else snprintf(g->reason, sizeof g->reason, "All of %s ground targets are destroyed.", !strcmp(pos, "Your") ? "your" : pos);
    return;
  }
  if (jets) snprintf(g->reason, sizeof g->reason, "Every jet in %s is down.", who);
  else snprintf(g->reason, sizeof g->reason, "All of %s's ground targets are destroyed.", who);
}

/* ---- network play: what the other computer reports ---------------------- */
void sim_remote_down(Game *g, Plane *p, const char *how) {
  if (!p->alive) return;
  p->alive = false;
  p->respawnT = 4;
  p->deathPos = p->pos;
  p->hasDeath = true;
  p->deathMsg = how;
  p->hitFlash = 1;
  explode(g, p->pos, 1.6f);
  for (int i = 0; i < g->nmissiles; i++)
    if (ref_eq(g->missiles[i].target, ref_plane(p->idx))) g->missiles[i].target = ref_none();
}

void sim_remote_up(Game *g, Plane *p) {
  (void)g;
  if (p->alive || !p->snap.have) return;
  p->alive = true;
  p->pos = p->snap.pos;
  p->f = p->snap.f;
  p->u = p->snap.u;
  p->r = v3_norm(v3_cross(p->u, p->f));
  p->u = v3_cross(p->f, p->r);
  p->speed = p->snap.speed;
  p->vel = p->snap.vel;
}

void sim_target_fall(Game *g, Target *t, int by) {
  if (!t->alive) return;
  t->by = by;
  target_down(g, t);
}

void sim_remote_missile(Game *g, const Missile *m) {
  if (g->nmissiles >= MAX_MISSILES) return;
  for (int i = 0; i < g->nmissiles; i++)
    if (g->missiles[i].id == m->id) return; /* already here */
  Missile *n = &g->missiles[g->nmissiles++];
  *n = *m;
  n->remote = true;
  n->prev = n->pos;
  if (!m->sam) game_sound(g, SND_NOISE, 1600, 200, 0.7f, 0.06f);
}

void sim_remote_flares(Game *g, Plane *p, bool chaff) {
  if (chaff) p->chaff++;
  else p->flares++;
  drop_decoys(g, p, chaff);
}

void sim_missile_gone(Game *g, uint32_t id, vec3 pos) {
  for (int i = 0; i < g->nmissiles; i++)
    if (g->missiles[i].id == id) {
      explode(g, pos, 0.8f);
      g->missiles[i] = g->missiles[--g->nmissiles];
      return;
    }
}

void sim_update(Game *g, float dt) {
  g->time += dt;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    if (p->inactive) continue;
    if (p->alive && p->is_ai && !p->remote) ai_control(g, p, dt);
    update_plane(g, p, dt);
  }
  if (g->def.on) defend_update(g, dt);
  for (int i = 0; i < g->nplanes; i++)
    for (int j = i + 1; j < g->nplanes; j++) {
      Plane *a = &g->planes[i], *b = &g->planes[j];
      if (a->alive && b->alive && v3_dist(a->pos, b->pos) < 14) {
        kill(g, a, -1, "MID-AIR COLLISION");
        kill(g, b, -1, "MID-AIR COLLISION");
      }
    }
  sim_update_tanker(g, dt);
  update_targets(g, dt);
  update_bullets(g, dt);
  update_missiles(g, dt);
  update_flares_parts(g, dt);

  tally(g);
  if (g->def.on) {
    if (g->over) g->overT -= dt;
  } else if (!g->over) {
    bool jets[2], lost[2];
    for (int t = 0; t < 2; t++) {
      jets[t] = out_of_jets(g, t);
      lost[t] = jets[t] || (g->ntargets > 0 && !targets_left(g, t)); /* a dogfight has no targets */
    }
    if (lost[0] || lost[1]) {
      g->over = true;
      g->overT = 3.2f;
      g->winner = lost[0] && lost[1] ? -1 : lost[0] ? 1 : 0;
      if (lost[0] && lost[1]) snprintf(g->reason, sizeof g->reason, "Both sides are finished at the same moment.");
      else if (g->mode == MODE_DRONE)
        snprintf(g->reason, sizeof g->reason, "%s", lost[1] ? "The last drone is down." : "The drone got the last jet.");
      else reason_for(g, lost[0] ? 0 : 1, jets[lost[0] ? 0 : 1]);
    }
  } else {
    g->overT -= dt;
  }
  if (g->mode == MODE_DEMO) {
    g->focusT += dt;
    if (g->focusT > 14 && g->planes[g->focus].alive) {
      g->focusT = 0;
      g->focus = 1 - g->focus;
    }
  }
}
