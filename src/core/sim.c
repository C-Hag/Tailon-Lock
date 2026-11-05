/* sim.c - flight, guns, missiles, flares, damage, tanker refuel (JS lines 2975-3280). */
#include "core/sim.h"
#include "core/fmath.h"

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

static vec3 rand3(Game *g, float a, float b) { return game_rand3(g, a, b, a, b, a, b); }

static void explode(Game *g, vec3 pos, float big) {
  /* every draw in its own statement, in order (see game_rand3) */
  for (int i = 0; i < 26 * big; i++) {
    vec3 r = game_rand3(g, -1, 1, -0.5f, 1, -1, 1), v = v3(r.x * 90 * big, r.y * 90 * big, r.z * 90 * big);
    float life = game_rand(g, 0.5f, 1.2f);
    part(g, pos, v, life, game_rand(g, 6, 14) * big, 0, 0);
  }
  for (int i = 0; i < 14 * big; i++) {
    vec3 at = v3_add(pos, rand3(g, -10, 10)), v = game_rand3(g, -25, 25, 5, 35, -25, 25);
    float life = game_rand(g, 2, 3.6f);
    part(g, at, v, life, game_rand(g, 10, 18) * big, 1, 14);
  }
  for (int i = 0; i < 10 * big; i++) {
    vec3 r = game_rand3(g, -1, 1, 0, 1, -1, 1), v = v3(r.x * 140, r.y * 140, r.z * 140);
    part(g, pos, v, game_rand(g, 1, 2), 3, 2, 0);
  }
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
  if (g->nbullets >= MAX_BULLETS || !plane_has_gun(p)) return;
  p->gunCool = 0.075f;
  p->ammo -= 1;
  p->gunShots++;
  p->invuln = 0; /* shooting ends the spawn protection (the cheater shot from behind it) */
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
  p->invuln = 0; /* as the gun does */
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

/* The missile key (modern rules): a heat-seeker while there are any, then the radar-guided ones, which need a
 * lock on a jet, then the air-to-ground ones, which need a lock on a ground target and are launched as the
 * classic rules launch them (below 2000 ft, under 500 kt); and when none can go, why not */
static void fire_any_missile(Game *g, Plane *p) {
  if (p->mslCool > 0) return;
  bool jet = p->locked && p->lockTarget.kind == REF_PLANE, gnd = p->locked && p->lockTarget.kind == REF_GROUND;
  float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
  bool radar_ok = p->radar > 0 && jet, ground_ok = p->ground > 0 && gnd && agl <= 610 && p->speed <= 257;
  /* a CPU pilot may ask for a kind first: a radar missile at range, an air-to-ground one on a ground lock */
  if (p->ctl.mslk == 1 && radar_ok) {
    p->radar--;
    fire_missile_kind(g, p, MK_RADAR);
    return;
  }
  if (p->ctl.mslk == 2 && ground_ok) {
    p->ground--;
    fire_missile_kind(g, p, MK_GROUND);
    return;
  }
  if (p->missiles > 0) {
    fire_missile(g, p);
    if (p->missiles == 0 && p->radar > 0 && !p->is_ai) game_event(p, "RADAR MISSILES NEXT");
    return;
  }
  if (radar_ok) {
    p->radar--;
    fire_missile_kind(g, p, MK_RADAR);
    return;
  }
  if (ground_ok) {
    p->ground--;
    fire_missile_kind(g, p, MK_GROUND);
    return;
  }
  if (p->is_ai) return;
  if (p->ground > 0 && gnd) game_event(p, agl > 610 ? "TOO HIGH - BELOW 2000 FT" : "TOO FAST - UNDER 500 KT");
  else if (p->radar > 0) game_event(p, "NO RADAR LOCK");
  else if (p->ground > 0) game_event(p, "NO TARGET LOCK");
  else game_event(p, "NO MISSILES");
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
    float jx = game_rand(g, -20, 20);
    f->vel = v3_add(v3_add(v3_scale(p->vel, 0.35f), v3_scale(p->r, 60 * s)), v3(jx, -40, game_rand(g, -20, 20)));
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

/* A side's tanker's orbit: over its own half, between its base and the middle, and where it comes in from: its
 * own edge of the area behind the base */
static void tanker_geometry(const Game *g, Tanker *T, int team) {
  const Base *b = &BASES[team];
  float side = b->z < 0 ? -1.0f : 1.0f;
  T->team = (uint8_t)team;
  T->R = fminf(MAP ? MAP->tanker_r : 3200, 2500);
  T->alt = MAP ? MAP->tanker_alt : 2300;
  T->cx = b->x * 0.6f;
  T->cz = b->z * 0.55f;
  T->from = v3(b->x, T->alt, side * (ARENA_Z - 300));
  T->speed = 150;
  (void)g;
}

void sim_reset_tankers(Game *g) {
  g->tankersDown[0] = g->tankersDown[1] = 0;
  for (int t = 0; t < 2; t++) {
    Tanker *T = &g->tankers[t];
    memset(T, 0, sizeof *T);
    tanker_geometry(g, T, t);
    T->state = TK_NONE;
    T->owner = -1;
    T->gone = true;
    T->pos = T->from;
  }
}

static void tanker_frame(Tanker *T) {
  T->u = v3(0, 1, 0);
  T->r = v3_cross(T->u, T->f);
  T->vel = v3_scale(T->f, T->speed);
  T->drogue = v3_add(v3_add(T->pos, v3_scale(T->f, -95)), v3(0, -14, 0));
}

/* Each tanker on its way: in from its edge to the nearest point of its orbit at 220 m/s, round the orbit, then
 * home again, climbing out; and every one up leaves at TANKER_STAYS. dt 0: just where it is (a network copy). */
void sim_update_tanker(Game *g, float dt) {
  for (int t = 0; t < 2; t++) {
    Tanker *T = &g->tankers[t];
    if (T->state == TK_NONE || T->state == TK_DOWN) {
      T->gone = true;
      continue;
    }
    T->t += dt;
    if (g->time >= TANKER_STAYS && T->state != TK_LEAVING) T->state = TK_LEAVING, T->t = 0;
    vec3 c = v3(T->cx, T->alt, T->cz), entry = v3_add(c, v3(fm_cosf(T->th) * T->R, 0, fm_sinf(T->th) * T->R));
    if (T->state == TK_INBOUND) {
      vec3 d = v3_sub(entry, T->from);
      float len = fmaxf(1, v3_len(d)), k = fminf(1, T->t * 220 / len);
      T->pos = v3_add(T->from, v3_scale(d, k));
      T->f = v3_scale(d, 1 / len);
      T->speed = 220;
      if (k >= 1) T->state = TK_STATION, T->t = 0;
    } else if (T->state == TK_STATION) {
      T->speed = 150;
      T->th += T->speed / T->R * dt;
      T->pos = v3_add(c, v3(fm_cosf(T->th) * T->R, 0, fm_sinf(T->th) * T->R));
      T->f = v3(-fm_sinf(T->th), 0, fm_cosf(T->th));
      if (T->t >= TANKER_ON_STATION) T->state = TK_LEAVING, T->t = 0;
    } else { /* leaving: from where it was, home toward its edge and up */
      T->speed = 180;
      vec3 h = v3(T->from.x - T->pos.x, 0, T->from.z - T->pos.z);
      if (v3_len(h) > 50) T->f = v3_norm(h);
      T->pos = v3_add(T->pos, v3_add(v3_scale(T->f, T->speed * dt), v3(0, 8 * dt, 0)));
      if (T->t > 150 || v3_len(h) < 300) T->state = TK_NONE;
    }
    T->gone = T->state == TK_NONE;
    tanker_frame(T);
  }
}

/* The tanker key: the pilot's side's tanker in, once a match; refused (and not spent) with one already up */
bool sim_call_tanker(Game *g, Plane *p) {
  Tanker *T = &g->tankers[p->team & 1];
  if (p->tcalled) {
    game_event(p, "TANKER CALL USED");
    return false;
  }
  if (g->time >= TANKER_STAYS) {
    game_event(p, "NO TANKERS NOW - SUDDEN DEATH");
    return false;
  }
  if (T->state == TK_INBOUND || T->state == TK_STATION) {
    game_event(p, "OUR TANKER IS UP ALREADY");
    return false;
  }
  tanker_geometry(g, T, p->team & 1);
  T->state = TK_INBOUND;
  T->t = 0;
  T->hp = TANKER_HP;
  T->owner = (int8_t)p->idx;
  T->th = fm_atan2f(T->from.z - T->cz, T->from.x - T->cx); /* the orbit's point nearest its way in */
  p->tcalled = true;
  p->tlost = false;
  T->gone = false;
  sim_update_tanker(g, 0);
  game_event(p, "TANKER INBOUND");
  return true;
}

/* What the HUD and the panel say about a pilot's call */
const char *sim_tanker_status(const Game *g, const Plane *p) {
  const Tanker *T = &g->tankers[p->team & 1];
  if (p->tlost) return "TANKER LOST";
  if (p->tcalled && T->owner == p->idx && T->state == TK_INBOUND) return "TANKER INBOUND";
  if (p->tcalled && T->owner == p->idx && T->state == TK_STATION) return "TANKER ON STATION";
  if (p->tcalled) return "TANKER USED";
  if (g->time >= TANKER_STAYS) return "NO TANKER";
  return "TANKER READY";
}

/* A hit on a tanker by the other side: dmg 100 for a missile of any kind, 7 a gun round. Only the host judges it
 * in network play (its state goes to the guest). */
static void hit_tanker(Game *g, Tanker *T, int shooter, float dmg) {
  if (T->gone || T->state == TK_DOWN) return;
  if (g->net && g->net_side != 0) return;
  T->hp -= dmg;
  for (int n = 0; n < (dmg > 50 ? 6 : 1); n++) part(g, T->pos, rand3(g, -30, 30), 0.6f, 6, 2, 0);
  if (T->hp > 0) return;
  T->state = TK_DOWN;
  T->gone = true;
  g->tankersDown[T->team & 1]++;
  for (int n = 0; n < 40; n++) part(g, T->pos, rand3(g, -60, 60), 2.5f, 12, n & 1, 0);
  if (T->owner >= 0 && T->owner < g->nplanes) {
    g->planes[T->owner].tlost = true;
    game_event(&g->planes[T->owner], "OUR TANKER IS DOWN");
  }
  if (shooter >= 0 && shooter < g->nplanes) game_event(&g->planes[shooter], "ENEMY TANKER DOWN");
}

/* Ground between a and b (the radar and the seeker's eye can't see through a hill). Sampled every 40 m,
 * a little below the ground's surface so a target standing on it stays in view. */
static bool hill_between(vec3 a, vec3 b) {
  int n = (int)clampf(v3_dist(a, b) / 40, 8, 128); /* every 40 m: a thin ridge between two samples let a lock through */
  for (int k = 1; k < n; k++) {
    vec3 q = v3_add(a, v3_scale(v3_sub(b, a), (float)k / (float)n));
    if (q.y < terrain_h(q.x, q.z) - 4) return true;
  }
  return false;
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
      float a = fm_acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
      /* no lock through a mountain (the cheater found the radar saw jets behind the ground) */
      if (a < bestA && !hill_between(p->pos, e->pos)) { best = ref_plane(i); bestA = a; }
    }
  }
  /* the other side's tanker, an air target too */
  const Tanker *ET = &g->tankers[!(p->team & 1)];
  if (!ET->gone) {
    vec3 d = v3_sub(ET->pos, p->pos);
    float dl = v3_len(d);
    if (dl < airR && dl > 0) {
      float a = fm_acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
      if (a < bestA && !hill_between(p->pos, ET->pos)) { best = ref_tanker(ET->team); bestA = a; }
    }
  }
  for (int i = 0; i < g->ntargets; i++) {
    const Target *t = &g->targets[i];
    if (!t->alive || t->team == p->team) continue;
    vec3 d = v3_sub(t->pos, p->pos);
    float dl = v3_len(d);
    if (dl < gndR && dl > 0) {
      float a = fm_acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
      if (a < bestA && !hill_between(p->pos, t->pos)) { best = ref_ground(i); bestA = a; }
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

/* ---- the landing gear and the flaps ----
 * Realistic handling: the pilot lowers and raises the gear (G) and steps the flaps (V) through up, take-off and
 * landing and back again, a notch a press. The gear takes GEAR_TIME to travel and drags in the airflow as hard as the clean jet's whole parasite
 * drag again; much faster than GEAR_VLE it is torn at. The flaps add lift at the same speed (the stall comes
 * lower, the jet lifts off sooner) and drag, the landing notch a lot; past a notch's speed it blows back to the
 * one the air allows, and comes out again on slowing. Touching down with the gear not locked down is a belly
 * landing. Arcade has none of it: its gear comes down on its own, for the picture only. */
#define GEAR_TIME 5.0f   /* s, up or down */
#define FLAP_TIME 2.5f   /* s a notch */
#define GEAR_CD   1.3f   /* drag over the clean jet's low-speed parasite drag (0.3 tw x^2): about +0.02 on its Cd0 */
static const float FLAP_VFE[3] = { 560, 128, 103 }; /* each notch's top speed: 250 and 200 kt */
static const float FLAP_CL[3] = { 0, 0.12f, 0.30f }; /* lift added, as a share of the clean wing's */
static const float FLAP_CD[3] = { 0, 0.25f, 1.1f };  /* drag, as GEAR_CD */

/* a notch's number for flaps flapT of the way out (between notches, in proportion) */
static float flap_at(const float *k, float t) {
  int i = t >= 1 ? 1 : 0;
  return k[i] + (k[i + 1] - k[i]) * clampf(t - (float)i, 0, 1);
}
/* the flaps' share of extra lift, realistic only */
static float flap_lift(const Game *g, const Plane *p) { return g->realistic && !p->drone ? flap_at(FLAP_CL, p->flapT) : 0; }
/* the g the wing can pull, flaps and all (never past the airframe) */
static float wing_g(const Game *g, const Plane *p, const JetType *jt, float v) {
  float n = jet_lift_g(jt, v), cl = flap_lift(g, p);
  return cl > 0 ? fminf(jt->g_max, n * (1 + cl)) : n;
}
/* the stall speed: lower with the flaps out, as the wing's lift at a speed grows */
static float stall_speed(const Game *g, const Plane *p) {
  float vs = g->classic ? 72 : 90, cl = flap_lift(g, p);
  return cl > 0 ? vs / sqrtf(1 + cl) : vs;
}

/* ai.c's hook (ai_gear_flaps): what a CPU pilot's landing asks for, every tick: the gear down, the flaps from 0
 * (up) to 1 (landing) */
static void cpu_asks(Plane *p, bool gear_down, float flaps) {
  p->cpuLand = gear_down;
  p->cpuFlaps = flaps;
}

/* A CPU pilot's levers (realistic): the gear down for the take-off roll, the landing approach and any low, slow
 * descent (a pass over the base, a carrier's deck) or when the landing asks (ai_gear_flaps), up once climbing
 * away; the take-off notch on the ground and
 * slowing for the circuit, the landing notch on the final approach. */
static void cpu_gear_flaps(const Game *g, Plane *p) {
  float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
  bool land = (p->cpuLand && !p->onGround && !p->taxi) || (p->ai.refuel && p->ai.final_leg);
  bool low_slow = agl < 250 && p->speed < 120 && p->vel.y < 0;
  if (p->taxi || p->onGround || land || low_slow) p->gearDown = true;
  else if (agl > 30 && p->vel.y > 2) p->gearDown = false;
  int want = 0;
  if (p->taxi || p->onGround) want = p->speed > 60 && !p->taxi ? 2 : 1; /* rolling out after a landing: as landed */
  else if (land || low_slow) want = p->speed < 112 ? 2 : 1;
  else if (p->ai.refuel && p->speed < 140) want = 1;
  else if (p->gearDown && agl < 300 && p->vel.y > 0) want = 1; /* just off the ground */
  int asked = (int)(p->cpuFlaps * 2 + 0.5f); /* and as far as the landing asks */
  (void)g;
  p->flaps = (int8_t)(asked > want ? asked : want);
}

static void gear_flaps(Game *g, Plane *p, float dt) {
  Controls *c = &p->ctl;
  if (p->drone) { /* the drone lands on its belly skids: nothing to work */
    p->gearDown = false;
    p->flaps = 0;
  } else if (!g->realistic) {
    /* arcade: down on the ground and slow near it, up when flying */
    float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
    p->gearDown = p->taxi || p->onGround || (agl < 150 && p->speed < 125);
    p->flaps = 0;
  } else if (p->is_ai) cpu_gear_flaps(g, p);
  else {
    if (c->gear && !p->prevGear) {
      if (p->onGround && p->gearDown) game_event(p, "WEIGHT ON WHEELS"); /* the gear will not come up on the ground */
      else {
        p->gearDown = !p->gearDown;
        game_event(p, p->gearDown ? "GEAR DOWN" : "GEAR UP");
        game_sound(g, SND_NOISE, 500, 80, GEAR_TIME * 0.6f, 0.05f);
      }
    }
    if (c->flap && !p->prevFlap) {
      static const char *const FN[3] = { "FLAPS UP", "FLAPS TAKE-OFF", "FLAPS LANDING" };
      if (p->flaps == 0) p->flapUp = false;
      if (p->flaps == 2) p->flapUp = true;
      p->flaps = (int8_t)(p->flaps + (p->flapUp ? -1 : 1));
      game_event(p, FN[p->flaps]);
      game_sound(g, SND_NOISE, 300, 60, 0.8f, 0.04f);
    }
  }
  p->prevGear = c->gear;
  p->prevFlap = c->flap;
  /* the gear and the flaps on their way; the flaps only as far as the air lets them */
  float gs = dt / GEAR_TIME;
  p->gearT = p->gearDown ? fminf(1, p->gearT + gs) : fmaxf(0, p->gearT - gs);
  int fmax = p->flaps;
  while (fmax > 0 && p->speed > FLAP_VFE[fmax]) fmax--;
  float fs = dt / FLAP_TIME;
  p->flapT = p->flapT < fmax ? fminf((float)fmax, p->flapT + fs) : fmaxf((float)fmax, p->flapT - fs);
  if (!g->realistic || p->drone) return;
  /* too fast with the gear out: the doors and the legs are torn at (never quite to the last) */
  p->gearWarnT -= dt;
  if (p->gearT > 0 && p->speed > GEAR_VLE) {
    if (p->health > 10) p->health = fmaxf(10, p->health - 4 * dt * (p->speed - GEAR_VLE) / 20);
    if (!p->is_ai && p->gearWarnT <= 0) {
      game_event(p, "GEAR OVERSPEED");
      p->gearWarnT = 2;
    }
  }
}

/* Before take-off. On the carrier the jet sits on the catapult until the
 * power goes to full, then the stroke flings it to flying speed in about
 * two seconds. On a runway it rolls with the throttle and lifts off when the
 * pilot pulls back above the rotation speed (on its own a little later). */
static void take_off(Game *g, Plane *p, const JetType *jt, float dt) {
  Controls *c = &p->ctl;
  float vr = g->classic ? 80 : 92;
  if (flap_lift(g, p) > 0) vr /= sqrtf(1 + flap_lift(g, p)); /* realistic: the flaps lift it off sooner */
  p->fuel = fmaxf(0, p->fuel - (g->realistic ? game_burn(g, p, p->throttle) : (0.1f + 0.4f * p->throttle) * jt->burn) * dt);
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
    /* the nose rotates up into the climb over half a second (it used to jump 9 degrees in one tick: a 90 g
     * jolt) */
    p->liftT = 0.5f;
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
    p->apHdg = fm_atan2f(p->f.x, p->f.z);
    game_event(p, p->autopilot ? "AUTOPILOT ON" : "AUTOPILOT OFF");
  }
  p->prevAp = c->ap;
  if (!p->autopilot) return;
  if (c->stick) {
    p->autopilot = false;
    game_event(p, "AUTOPILOT OFF");
    return;
  }
  float hdg = fm_atan2f(p->f.x, p->f.z), err = p->apHdg - hdg;
  while (err > PI_F) err -= 2 * PI_F;
  while (err < -PI_F) err += 2 * PI_F;
  float bank = fm_atan2f(-p->r.y, p->u.y), want = clampf(err * 1.2f, -0.5f, 0.5f);
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
  /* its gear and flaps as the snapshots set them, on their way here at their own pace */
  float gs = dt / GEAR_TIME, fs = dt / FLAP_TIME;
  p->gearT = p->gearDown ? fminf(1, p->gearT + gs) : fmaxf(0, p->gearT - gs);
  p->flapT = p->flapT < p->flaps ? fminf((float)p->flaps, p->flapT + fs) : fmaxf((float)p->flaps, p->flapT - fs);
  if (p->netFire && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
}

/* Energy (the specific excess power, Ps = V (T - D) / W), as the
 * acceleration along the flight path: thrust against drag, where the drag of
 * the turn grows with the square of the g pulled, and gravity along the
 * path, so climbing trades speed for height and diving gives it back (the
 * yo-yos, boom and zoom). Thin air at height gives less thrust. Arcade runs
 * the engine and drag faster than life; gravity is always real. */
float sim_excess_power(const Game *g, const Plane *p, const JetType *jt, float thr) {
  float v = fmaxf(p->speed, 30), vmax = 390 * jt->speed, vc = jet_corner(jt);
  float h = fmaxf(0, p->pos.y), rho = fm_expf(-h / 9000), rk = fm_expf(-h / 9000 * 0.7f); /* rho^0.7 */
  float tw = 1.1f * jt->climb; /* full power: thrust a little over the weight */
  float thrust = tw * (0.04f + 0.96f * thr) * rk;
  /* full power just holds top speed. Arcade drags with the square of the
   * speed; realistic has a clean jet's curve, little parasite drag until the
   * transonic rise near top speed, which leaves the excess power for the
   * sheets' climb and sustained turn */
  float x = v / vmax, x2 = x * x;
  float drag = tw * (g->realistic ? 0.3f * x2 + 0.7f * x2 * x2 * x2 * x2 : x2) * rk;
  /* power back to idle: the speed brakes come out, so the jet can slow down to land (and bleed speed in a
   * fight); arcade had none, and the test pilots found no jet slowing enough on the approach to land */
  drag += tw * 0.7f * x2 * clampf(1 - thr / 0.15f, 0, 1) * rk;
  /* realistic: the gear and the flaps out in the airflow */
  if (g->realistic && !p->drone) drag += tw * 0.3f * x2 * (GEAR_CD * p->gearT + flap_at(FLAP_CD, p->flapT)) * rk;
  /* the g the wing is making: never more than it can at this speed (the
   * g felt lags a little behind); on the wheels it carries nothing */
  float n = p->onGround ? 0 : fminf(fabsf(p->gload), fmaxf(1, wing_g(g, p, jt, v)));
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
    /* turned back toward the middle, no harder than a 4 g turn would (it used to be 0.9 rad/s at any
     * speed: some 30 g at full speed, with nothing on the G-meter) */
    vec3 to = v3(-p->pos.x, 0, -p->pos.z);
    float a = signf(p->f.z * to.x - p->f.x * to.z) * fminf(0.9f, 4 * 9.81f / fmaxf(p->speed, 40)) * dt;
    p->f = rot_y(p->f, a);
    p->u = rot_y(p->u, a);
    p->r = rot_y(p->r, a);
    p->vel = rot_y(p->vel, a);
  }
  return false;
}

/* The ceiling (60,000 ft classic): the air too thin to climb in. The nose falls back to level as in a stall
 * and the path holds the height; it used to hold the height but leave the nose and the climb as they were, so
 * a jet could hang there pointing up, the climb gauge reading 50,000 ft/min while it went nowhere. */
static void ceiling(const Game *g, Plane *p, float dt) {
  float top = g->classic ? 18288 : 7500;
  if (p->pos.y < top) return;
  p->pos.y = top;
  if (p->f.y > 0) {
    p->f = v3_norm(v3_add(p->f, v3(0, -fminf(p->f.y, 1.5f * dt), 0)));
    p->r = v3_norm(v3_cross(p->u, p->f));
    p->u = v3_cross(p->f, p->r);
  }
  if (p->vel.y > 0) p->vel.y = 0;
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
  float lift = g->realistic ? wing_g(g, p, jt, v) : jet_lift_g(jt, v), lo = -fminf(-jt->g_min, lift);
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

  if (p->liftT > 0) { /* the rotation after lift-off, 0.32 rad/s for half a second */
    p->liftT -= dt;
    rot_pair(&p->f, &p->u, 0.32f * dt);
  }

  /* classic rules: stall at 140 kt (realistic: lower with the flaps out) */
  const float vs = g->realistic ? stall_speed(g, p) : g->classic ? 72 : 90;
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
    p->speed = fmaxf(0, p->speed - (onDeckPrev(g, p) ? 30 : sim_brake(g)) * dt);
  } else {
    p->speed += sim_excess_power(g, p, jt, thrEff) * dt;
    p->speed = clampf(p->speed, p->onGround ? 0 : 30, 560);
  }
  p->vel = v3_scale(p->f, p->speed);
  p->vel.y -= sink;
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  ceiling(g, p, dt);
  return false;
}

/* v turned about the unit axis k by a (Rodrigues). */
static vec3 rot_axis(vec3 v, vec3 k, float a) {
  float c = fm_cosf(a), s = fm_sinf(a);
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
  float alpha = fm_acosf(clampf(v3_dot(p->f, vd), -1, 1));
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
  float h = fmaxf(0, p->pos.y), rho = fm_expf(-h / 9000), rk = fm_expf(-h / 9000 * 0.7f); /* rho^0.7 */
  float k = g->realistic ? 1.0f : 2.2f, kt = g->realistic ? 1.0f : 1.2f, kg = g->realistic ? 1.0f : 1.5f;
  float tw = 1.1f * jt->climb * DRONE_THRUST, vmax = 390 * jt->speed, vc = jet_corner(jt);
  float thrust = 9.81f * k * tw * (0.04f + 0.96f * thr) * rk;
  float cl = alpha < 0.45f ? 1.0f : fmaxf(0.06f, 1 - (alpha - 0.45f) * 1.6f); /* the wing stalls past 26 degrees */
  float sa = alpha < PI_F / 2 ? fm_sinf(alpha) : 1; /* past 90 degrees the nozzle still pushes across, and tail first drags */
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
  v = fmaxf(0, v + (thrust * fm_cosf(alpha) - drag - induced - broad - 9.81f * kg * vd.y) * dt);
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
    p->stuntMaxA = fmaxf(p->stuntMaxA, fm_asinf(clampf(p->f.y, -1, 1)) - fm_asinf(clampf(vd.y, -1, 1))); /* nose above the path */
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
  p->aoa = fm_acosf(clampf(v3_dot(p->f, vd), -1, 1));
  p->speed = clampf(v, 0, 560);
  p->vel = v3_scale(vd, p->speed);
  p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
  ceiling(g, p, dt);
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
  gear_flaps(g, p, dt);
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
  p->fuel = fmaxf(0, p->fuel - game_burn(g, p, p->throttle) * dt);
  float thrEff = p->fuel > 0 ? p->throttle : 0;
  vec3 vel0 = p->vel;
  if (p->drone) {
    if (fly_drone(g, p, jt, thrEff, dt)) return;
  } else if (fly_jet(g, p, jt, thrEff, dt)) return;
  /* the G-meter: the jet's real acceleration across its wings, plus gravity's share, filtered as the
   * airframe's gload is (arcade turns faster than gload says; the meter shows what is really flown) */
  if (!p->onGround) {
    float n = clampf(v3_dot(v3_scale(v3_sub(p->vel, vel0), 1 / dt), p->u) / 9.81f + p->u.y, -6, 15);
    p->gmeter += (n - p->gmeter) * fminf(1, dt * 6);
  } else {
    p->gmeter += (1 - p->gmeter) * fminf(1, dt * 6);
  }

  /* ground contact and runway */
  float h = terrain_h(p->pos.x, p->pos.z);
  int deck = MAP ? map_carrier_at(MAP, p->pos.x, p->pos.z) : -1;
  bool onDeck = deck >= 0 && MAP->carriers[deck].team == p->team;
  bool onRunway = (fabsf(p->pos.x - b->x) < 35 && fabsf(p->pos.z - b->z) < 1060) || onDeck; /* 45 x 2000 m and a margin */
  p->onGround = false;
  if (p->pos.y < h + 3.05f) { /* resting on the gear counts as contact too */
    /* realistic: on the wheels only with the gear locked down (a jet already rolling has it down) */
    bool wheels = !g->realistic || p->drone || p->gearT >= 1;
    if (onRunway && !wheels && !g->no_crash) {
      if (!p->is_ai) game_event(p, "WHEELS UP - BELLY LANDING");
      kill(g, p, -1, "CRASHED");
      return;
    }
    if (onRunway && p->f.y > -0.25f && p->speed < 240 && fabsf(p->r.y) < 0.35f) {
      p->pos.y = h + 3;
      p->onGround = true;
      rot_pair(&p->u, &p->r, clampf(fm_atan2f(-p->r.y, p->u.y), -1, 1) * -2 * dt);
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
  const Tanker *T = &g->tankers[p->team & 1];
  /* the base fills a jet on it, or a player's passing low and slow over it; the CPU pilots land for it, on the
   * runway or the carrier's deck */
  bool atBase = ((fabsf(p->pos.x - b->x) < 220 && fabsf(p->pos.z - b->z) < 1100) || onDeck) && p->pos.y < h + 150 && p->speed < 230 &&
                (p->onGround || !p->is_ai);
  /* the side's own tanker on station: in formation behind its drogue for a moment first (arcade: within 150 m and
   * 70 m/s for 1 s; realistic: real station-keeping, within 40 m and 15 m/s for 3 s) */
  bool formation = T->state == TK_STATION && (g->realistic ? v3_dist(p->pos, T->drogue) < 40 && fabsf(p->speed - T->speed) < 15
                                                            : v3_dist(p->pos, T->drogue) < 150 && fabsf(p->speed - T->speed) < 70);
  p->tjoin = formation ? p->tjoin + dt : 0;
  bool atTanker = formation && p->tjoin >= (g->realistic ? 3 : 1);
  if (atTanker && p->tjoin - dt < (g->realistic ? 3 : 1)) p->tankerFills++;
  p->refuel = (atBase || atTanker) && g->time < TANKER_STAYS; /* then sudden death: no fuel or rearming anywhere */
  if (p->refuel) {
    /* a quick turnaround, the same for players and the CPU: arcade fills a tank in 3 s and hangs a missile of
     * each kind every 0.5 s, so a jet is full within its roll-out and off again from the runway; realistic
     * as it was, 7 s a tank and 1.4 s a missile */
    float q = g->realistic ? 1 : 2.5f;
    p->fuel = fminf(100, p->fuel + 14 * q * dt);
    p->health = fminf(100, p->health + 8 * q * dt);
    /* back up to what the jet took off with */
    int full[4], fl = g->classic ? 6 : MAX_FLR;
    game_full_load(g, p, full);
    p->ammo = fminf((float)full[3], p->ammo + (g->classic ? 480 : 80) * q * dt);
    p->rearmT += dt;
    if (p->rearmT > (g->realistic ? 1.4f : 0.5f)) {
      p->rearmT = 0;
      if (p->missiles < full[0]) p->missiles++;
      if (p->radar < full[1]) p->radar++;
      if (p->ground < full[2]) p->ground++;
      p->flares = p->flares + 2 > fl ? fl : p->flares + 2;
      p->chaff = p->chaff + 2 > fl ? fl : p->chaff + 2;
    }
  }
  for (int t = 0; t < 2; t++)
    if (!g->tankers[t].gone && v3_dist(p->pos, g->tankers[t].pos) < 28) {
      kill(g, p, -1, "HIT THE TANKER");
      return;
    }

  lock_on(g, p, dt);

  /* weapons */
  if (g->classic && !p->is_ai) {
    static const char *const WNAME[W_COUNT] = { "CANNON", "HEAT-SEEKER", "RADAR-GUIDED", "AIR-TO-GROUND" };
    if (c->sel && !p->prevSel) {
      p->wsel = (p->wsel + 1) % W_COUNT;
      if (p->wsel == W_CANNON && !plane_has_gun(p)) p->wsel = W_HEAT; /* missiles only */
      game_event(p, WNAME[p->wsel]);
    }
    if (p->wsel == W_CANNON) {
      if (c->fire && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    } else if (c->fire && !p->prevFire) fire_selected(g, p);
    if (c->flr && !p->prevFlr) countermeasure(g, p);
    if (c->map && !p->prevMap) p->mapview = !p->mapview;
    /* the modern keys still work alongside */
    if (c->gun && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    if (c->msl && !p->prevMsl) fire_any_missile(g, p);
  } else {
    if (c->gun && p->gunCool <= 0 && p->ammo >= 1) fire_gun(g, p);
    /* a jet built without a gun: the gun key says so (once while the message shows) */
    if (c->gun && !plane_has_gun(p) && !p->is_ai && p->evT <= 0) game_event(p, "NO GUN - MISSILES ONLY");
    if (c->msl && !p->prevMsl) fire_any_missile(g, p);
    if (c->flr && !p->prevFlr) {
      if (p->is_ai) countermeasure(g, p); /* the CPU pilot: chaff against a radar missile, as the classic key */
      else drop_flares(g, p);
    }
  }
  /* the tanker key: in network play the guest asks the host, whose tanker it is to fly (its state comes back) */
  if (c->tanker && !p->prevTanker) {
    const Tanker *OT = &g->tankers[p->team & 1];
    if (!g->net || g->net_side == 0) sim_call_tanker(g, p);
    else if (p->tcalled) game_event(p, "TANKER CALL USED");
    else if (OT->state == TK_INBOUND || OT->state == TK_STATION) game_event(p, "OUR TANKER IS UP ALREADY");
    else if (g->time >= TANKER_STAYS) game_event(p, "NO TANKERS NOW - SUDDEN DEATH");
    else {
      p->tcalled = true;
      net_event(g, NE_TANKER, p->idx, false, NULL);
      game_event(p, "TANKER INBOUND");
    }
  }
  p->prevTanker = c->tanker;
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
        float jx = game_rand(g, -8, 8);
        vec3 at = v3_add(t->pos, v3(jx, 4, game_rand(g, -8, 8)));
        part(g, at, game_rand3(g, -4, 4, 25, 40, -4, 4), 4, 14, 1, 10);
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
      if (bl->owner >= 0 && bl->owner < g->nplanes) g->planes[bl->owner].gunHitsJet++;
      for (int n = 0; n < 4; n++) part(g, bl->pos, rand3(g, -40, 40), 0.3f, 3, 3, 0);
    }
    for (int t = 0; t < 2 && !hit; t++) { /* the other side's tanker: a big target, 7 a round */
      Tanker *T = &g->tankers[t];
      if (T->gone || T->team == bl->team || seg_dist(bl->prev, bl->pos, T->pos) >= 30) continue;
      hit_tanker(g, T, bl->owner, 7);
      hit = true;
      if (bl->owner >= 0 && bl->owner < g->nplanes) g->planes[bl->owner].gunHitsTanker++;
    }
    if (!hit && bl->pos.y < 40 + (MAP ? 3500.0f : 0.0f)) {
      for (int k = 0; k < g->ntargets; k++) {
        Target *t = &g->targets[k];
        if (!t->alive || t->team == bl->team) continue;
        const TargetDef *d = &TDEF[t->type];
        /* ships lie along their heading; the maps only use 0 and 180 for ground sites */
        bool across = fabsf(fm_sinf(t->heading * PI_F / 180)) > 0.7f;
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
      float jx = game_rand(g, -10, 10);
      part(g, bl->pos, v3(jx, 25, game_rand(g, -10, 10)), 0.7f, 6, 1, 8);
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
      float ang = fm_acosf(clampf(v3_dot(m->dir, want), -1, 1));
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
      else if (m->target.kind == REF_TANKER)
        hit_tanker(g, &g->tankers[m->target.idx & 1], m->owner, 100); /* a missile of any kind, whatever hits-to-kill */
      else if (m->target.kind == REF_MISSILE) /* a SAM on an inbound missile */
        for (int k = 0; k < g->nmissiles; k++) {
          Missile *v = &g->missiles[k];
          if (v->id != m->target.gen || v->life <= 0) continue;
          v->life = -1;
          if (v->ashm) g->def.ashm_down++;
        }
    }
    for (int t = 0; t < 2 && !boom; t++) { /* a missile passing close to the other side's tanker */
      Tanker *T = &g->tankers[t];
      if (!T->gone && T->team != m->team && seg_dist(m->prev, m->pos, T->pos) < 30) {
        boom = true;
        hit_tanker(g, T, m->owner, 100);
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

static bool has_targets(const Game *g, int team) {
  for (int i = 0; i < g->ntargets; i++)
    if (g->targets[i].team == team) return true;
  return false;
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
  else if (count != 1) who = mine ? "your team" : (t == 0 ? "the blue team" : "the red team"); /* 0: a side with no jet */
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
  if (p->alive || !p->snap.have) return;
  p->alive = true;
  int full[4]; /* back with a full load, as its own computer arms it */
  game_full_load(g, p, full);
  p->radar = full[1];
  p->ground = full[2];
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
  /* the snapshots carry its heat-seekers; its radar and air-to-ground missiles are counted off here, so its
   * pylons show what it has left */
  if (m->owner >= 0 && m->owner < g->nplanes && g->planes[m->owner].remote && !m->sam && !m->ashm) {
    Plane *o = &g->planes[m->owner];
    if (m->kind == MK_RADAR && o->radar > 0) o->radar--;
    if (m->kind == MK_GROUND && o->ground > 0) o->ground--;
  }
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

void (*sim_hands)(Game *g, Plane *p, float dt, bool after);

void sim_update(Game *g, float dt) {
  if (!ai_gear_flaps) ai_gear_flaps = cpu_asks;
  g->time += dt;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    if (p->inactive) continue;
    if (p->alive && p->is_ai && !p->remote) {
      if (sim_hands) sim_hands(g, p, dt, false);
      ai_control(g, p, dt);
      if (sim_hands) sim_hands(g, p, dt, true);
    }
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
  } else if (g->over) {
    g->overT -= dt;
  } else if (!(g->net && g->net_side == 1)) { /* a network guest takes the host's verdict (net.c) */
    bool jets[2], lost[2];
    for (int t = 0; t < 2; t++) {
      jets[t] = out_of_jets(g, t);
      /* out of targets: only a team that had some (a dogfight has none, and a map may give one team none) */
      lost[t] = jets[t] || (has_targets(g, t) && !targets_left(g, t));
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
  }
  if (g->mode == MODE_DEMO) {
    g->focusT += dt;
    if (g->focusT > 14 && g->planes[g->focus].alive) {
      g->focusT = 0;
      g->focus = 1 - g->focus;
    }
  }
}
