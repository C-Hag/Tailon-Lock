/* game.c - Game struct with fixed arrays; buildRoster, newGame, makePlane,
 * spawn (JS lines 2760-2827). */
#include "core/game.h"

#include <stdio.h>
#include <string.h>

#include "core/defend.h"
#include "core/sim.h"

const char *const TNAME[TGT_TYPES] = { "SAM SITE", "FUEL DEPOT", "RADAR", "HANGAR", "FRIGATE", "RUBBLE" };
const TargetDef TDEF[TGT_TYPES] = {
  { 60, 16, 16, 12 }, { 40, 22, 22, 20 }, { 50, 13, 13, 30 }, { 120, 40, 28, 26 }, { 90, 8, 55, 24 }, { 0, 0, 0, 0 },
};

float game_rand(Game *g, float a, float b) { return rng_range(&g->rng, a, b); }

void game_sound(Game *g, SoundKind kind, float f0, float f1, float dur, float vol) {
  if (g->nsounds >= MAX_SOUNDS) return;
  SoundReq *s = &g->sounds[g->nsounds++];
  s->kind = kind;
  s->f0 = f0;
  s->f1 = f1;
  s->dur = dur;
  s->vol = vol;
}

void game_event(Plane *p, const char *text) {
  if (!p) return;
  snprintf(p->ev, sizeof p->ev, "%s", text);
  p->evT = 2.4f;
}

typedef struct { int team; bool ai; const char *name; int hidx; } RosterEntry;

static int build_roster(GameMode mode, bool coop, bool pairs, RosterEntry *out) {
  int n = 0;
#define ADD(t, a, nm, h) (out[n].team = (t), out[n].ai = (a), out[n].name = (nm), out[n].hidx = (h), n++)
  if (mode == MODE_DEMO) { ADD(0, true, "BLUE", -1); ADD(1, true, "RED", -1); }
  else if (mode == MODE_1P && pairs) { ADD(0, false, "YOU", 0); ADD(0, true, "WINGMAN", -1); ADD(1, true, "CPU 1", -1); ADD(1, true, "CPU 2", -1); }
  else if (mode == MODE_1P) { ADD(0, false, "YOU", 0); ADD(1, true, "CPU", -1); }
  else if (mode == MODE_DEFEND) {
    /* coop here means a second player commands the carrier */
    static const char *const BANDIT[6] = { "BANDIT 1", "BANDIT 2", "BANDIT 3", "BANDIT 4", "BANDIT 5", "BANDIT 6" };
    ADD(0, false, coop ? "PILOT" : "YOU", 0);
    if (coop) ADD(0, false, "COMMANDER", 1);
    for (int k = 0; k < 6; k++) ADD(1, true, BANDIT[k], -1);
  }
  else if (coop) { ADD(0, false, "PLAYER 1", 0); ADD(0, false, "PLAYER 2", 1); ADD(1, true, "CPU 1", -1); ADD(1, true, "CPU 2", -1); }
  else if (pairs) { ADD(0, false, "PLAYER 1", 0); ADD(0, true, "WINGMAN 1", -1); ADD(1, false, "PLAYER 2", 1); ADD(1, true, "WINGMAN 2", -1); }
  else { ADD(0, false, "PLAYER 1", 0); ADD(1, false, "PLAYER 2", 1); }
#undef ADD
  return n;
}

/* What a jet carries when full: heat, radar, air-to-ground, cannon rounds.
 * Classic rules use the chosen arming (or a standard 6 heat-seekers and an
 * air-to-ground missile), 3000 rounds, and in a dogfight heat-seekers only;
 * modern rules the chosen heat-seekers up to MAX_MSL. */
void game_full_load(const Game *g, const Plane *p, int *full) {
  bool chosen = p->load[0] >= 0;
  if (g->classic) {
    full[0] = chosen ? p->load[0] : MAX_MSL;
    full[1] = chosen ? p->load[1] : 0;
    full[2] = chosen ? p->load[2] : 1;
    full[3] = 3000;
    if (g->dogfight) {
      full[0] = MAX_MSL;
      full[1] = full[2] = 0;
    }
  } else {
    full[0] = chosen && p->load[0] < MAX_MSL ? p->load[0] : MAX_MSL;
    full[1] = CLASSIC_RADAR;
    full[2] = CLASSIC_GROUND;
    full[3] = MAX_AMMO;
  }
}

/* Loads the stores (classic rules: 6 flares and 6 chaff pods). */
static void arm(Game *g, Plane *p) {
  int full[4];
  game_full_load(g, p, full);
  p->missiles = full[0];
  p->radar = full[1];
  p->ground = full[2];
  p->ammo = (float)full[3];
  p->flares = g->classic ? 6 : MAX_FLR;
  p->chaff = g->classic ? 6 : MAX_CHAFF;
}

bool game_load_fits(const int *l) {
  if (l[0] < 0 || l[1] < 0 || l[2] < 0 || l[1] > 6 || l[2] > 2 || (l[1] && l[2])) return false;
  return l[0] + l[1] + 2 * l[2] <= 8;
}

/* On the ground: on the carrier's catapult (team 0, when the map has its
 * carrier) or at the end of the team's runway, engine idling. */
static void ground_spawn(Game *g, Plane *p) {
  const Carrier *cv = NULL;
  for (int i = 0; MAP && i < MAP->ncarriers; i++)
    if (MAP->carriers[i].team == p->team && p->team == 0) cv = &MAP->carriers[i];
  float hd, x, z;
  if (cv) {
    /* the two bow catapults, 8 m either side of the centre line */
    hd = cv->heading * PI_F / 180;
    float lx = p->slot <= 0 ? 8.0f : -8.0f, lz = cv->length * 0.12f;
    x = cv->x + lx * cosf(hd) + lz * sinf(hd);
    z = cv->z - lx * sinf(hd) + lz * cosf(hd);
    p->oncat = true;
  } else {
    float bx = BASES[p->team].x, bz = BASES[p->team].z;
    hd = MAP ? MAP->bases[p->team].heading * PI_F / 180 : (BASES[p->team].dir > 0 ? 0 : PI_F);
    /* the threshold, wingmen side by side */
    x = bx - sinf(hd) * 920 + cosf(hd) * p->slot * 22;
    z = bz - cosf(hd) * 920 - sinf(hd) * p->slot * 22;
    p->oncat = false;
  }
  p->f = v3(sinf(hd), 0, cosf(hd));
  p->u = v3(0, 1, 0);
  p->r = v3_cross(p->u, p->f);
  p->pos = v3(x, terrain_h(x, z) + 3, z);
  p->speed = 0;
  p->throttle = 0;
  p->taxi = true;
  p->cat = 0;
}

void game_spawn(Game *g, Plane *p) {
  const Base *b = &BASES[p->team];
  if (g->dogfight) {
    /* close range dogfight: in the air, a few kilometres apart, nose to nose */
    /* the meeting point and axis are shared by both sides, so they turn in
     * towards each other: offset sideways and in height, not head-on. A
     * fresh jet later in the match gets a new meeting point near the action. */
    if (g->time > 1) {
      Plane *e = game_enemy_of(g, p->team, p->pos);
      vec3 at = e && e->alive ? e->pos : v3(g->dfx, 0, g->dfz);
      g->dfx = clampf(at.x, -6000, 6000);
      g->dfz = clampf(at.z, -6000, 6000);
      g->dfa = game_rand(g, 0, 6.28f);
    }
    float cx = g->dfx, cz = g->dfz, a = g->dfa;
    float s = p->team == 0 ? -1.0f : 1.0f, sx = cosf(a), sz = -sinf(a);
    p->pos = v3(cx + s * (sinf(a) * 2000 + sx * 450) + p->slot * 300, 3000 + (p->team ? 400 : 0) + p->slot * 80,
                cz + s * (cosf(a) * 2000 + sz * 450));
    p->f = v3(-s * sinf(a), 0, -s * cosf(a));
    p->u = v3(0, 1, 0);
    p->r = v3_cross(p->u, p->f);
  } else if (MAP) {
    /* the map's air spawn over the base, wingmen spread along the wing line */
    float h = MAP->spawn[p->team].heading * PI_F / 180;
    p->f = v3(sinf(h), 0, cosf(h));
    p->u = v3(0, 1, 0);
    p->r = v3_cross(p->u, p->f);
    p->pos = v3_add(v3(MAP->spawn[p->team].x, MAP->spawn[p->team].y + p->slot * 60, MAP->spawn[p->team].z),
                    v3_scale(p->r, p->slot * 450 + game_rand(g, -60, 60)));
  } else {
    p->pos = v3(b->x + p->slot * 450 + game_rand(g, -60, 60), 1500 + p->slot * 60, b->z + b->dir * 600);
    p->f = v3(0, 0, b->dir);
    p->u = v3(0, 1, 0);
    p->r = v3_cross(p->u, p->f);
  }
  p->speed = 260;
  p->throttle = 0.75f;
  p->taxi = p->oncat = false;
  p->autopilot = false;
  p->stunT = 0;
  if (g->ground_start && !g->dogfight && !g->def.on) ground_spawn(g, p);
  p->vel = v3_scale(p->f, p->speed);
  p->fuel = 100;
  p->health = 100;
  arm(g, p);
  p->mapview = false;
  p->alive = true;
  p->invuln = 3;
  p->gunCool = p->mslCool = p->lockT = 0;
  p->lockTarget = ref_none();
  p->locked = false;
  p->hitFlash = 0;
  p->stall = p->onGround = p->out = p->refuel = p->warn = false;
  p->rearmT = 0;
  p->gunSide = 1;
  if (g->def.on && p->team == 0) defend_launch(g, p);
  p->ctl.pitch = p->ctl.roll = 0;
  p->gload = 1;
  p->gstress = p->gneg = p->gloc = 0;
  if (p->is_ai) {
    p->ai.refuel = false;
    p->ai.final_leg = false;
    p->ai.threatT = 0;
  }
}

static void make_plane(Game *g, Plane *p, int team, bool ai, Difficulty diff, float slot) {
  memset(p, 0, sizeof *p);
  p->team = team;
  p->is_ai = ai;
  if (ai) {
    p->ai.diff = diff;
    p->ai.mslCool = game_rand(g, 4, 8);
    p->ai.gt = -1;
  }
  p->lives = g->lives > 0 ? g->lives : LIVES;
  p->load[0] = -1;
  p->slot = slot;
  p->hidx = -1;
  p->lastHitBy = -1;
  p->lastHitT = -99;
  p->throttle = 0.75f;
  game_spawn(g, p);
}

void game_new(Game *g, GameMode mode, Difficulty diff, bool coop, const int *seatJets, uint32_t seed) {
  game_new_rules(g, mode, diff, coop, seatJets, seed, NULL);
}

void game_new_rules(Game *g, GameMode mode, Difficulty diff, bool coop, const int *seatJets, uint32_t seed,
                    const GameRules *rules) {
  world_init();
  jets_finish();
  memset(g, 0, sizeof *g);
  if (rules && mode != MODE_DEMO) {
    g->classic = rules->classic;
    g->g_effects = rules->g_effects;
    g->ground_start = rules->ground_start && mode != MODE_DEFEND;
    g->no_crash = rules->no_crash;
    g->realistic = rules->realistic;
    g->dogfight = rules->dogfight && rules->classic && mode != MODE_DEFEND;
    if (mode == MODE_DEFEND) { /* the classic-rules options are not shown for the carrier defence */
      g->no_crash = false;
      g->hits_to_kill = 0;
      g->lives = 0;
    }
    g->hits_to_kill = rules->hits;
    g->lives = rules->lives;
  }
  g->mode = mode;
  g->diff = diff;
  g->coop = coop;
  g->rng.state = seed;
  g->dfx = game_rand(g, -3000, 3000);
  g->dfz = game_rand(g, -3000, 3000);
  g->dfa = game_rand(g, 0, 6.28f);
  g->winner = -1;

  Tanker *T = &g->tanker;
  T->th = game_rand(g, 0, 6);
  T->R = MAP ? MAP->tanker_r : 3200;
  T->alt = MAP ? MAP->tanker_alt : 2300;
  T->cx = MAP ? MAP->tanker_x : 0;
  T->cz = MAP ? MAP->tanker_z : 0;
  T->speed = 150;

  RosterEntry roster[MAX_PLANES];
  int n = build_roster(mode, coop, rules && rules->pairs && mode != MODE_DEMO && mode != MODE_DEFEND && !coop, roster);
  for (int i = 0; i < n; i++) {
    int slot = 0, slots = 0;
    for (int j = 0; j < n; j++) {
      if (roster[j].team != roster[i].team) continue;
      if (j < i) slot++;
      slots++;
    }
    Plane *p = &g->planes[i];
    make_plane(g, p, roster[i].team, roster[i].ai, diff, slot - (slots - 1) / 2.0f);
    snprintf(p->name, sizeof p->name, "%s", roster[i].name);
    p->hidx = roster[i].hidx;
    if (rules && p->hidx >= 0 && game_load_fits(rules->load[p->hidx])) {
      memcpy(p->load, rules->load[p->hidx], sizeof p->load);
      arm(g, p);
    }
    p->idx = i;
    if (p->hidx >= 0) p->jet = seatJets ? seatJets[p->hidx] : 0;
    else p->jet = (int)(rng_f(&g->rng) * NJETS);
    if (p->jet < 0 || p->jet >= NJETS) p->jet = 0;
  }
  g->nplanes = n;
  for (int i = 0; i < n; i++)
    if (g->planes[i].hidx >= 0 && g->nlocals < 2) g->locals[g->nlocals++] = i;
  g->my = g->nlocals ? g->locals[0] : 0;

  static const struct { float dx, dz; int type; } OFF[6] = {
    { -1500, 1100, TGT_SAM }, { 1500, 1100, TGT_SAM }, { -800, -600, TGT_FUEL },
    { 800, -600, TGT_FUEL }, { -2000, 200, TGT_RADAR }, { 2000, 200, TGT_HANGAR },
  };
  /* the close range dogfight is jets only (classic rules) */
  bool targets = mode != MODE_DEFEND && !g->dogfight;
  for (int t = 0; t < 2 && !MAP && targets; t++) {
    const Base *b = &BASES[t];
    for (int k = 0; k < 6; k++) {
      Target *x = &g->targets[g->ntargets++];
      x->team = t;
      x->type = OFF[k].type;
      x->by = -1;
      x->pos = v3(b->x + OFF[k].dx, 8, b->z + OFF[k].dz * b->dir);
      x->hp = TDEF[x->type].hp;
      x->alive = true;
      x->cool = game_rand(g, 2, 6);
      x->spin = game_rand(g, 0, 6);
    }
  }
  for (int k = 0; MAP && targets && k < MAP->ntargets && g->ntargets < MAX_TARGETS; k++) {
    const MapTarget *mt = &MAP->targets[k];
    Target *x = &g->targets[g->ntargets++];
    x->team = mt->team;
    x->type = mt->type;
    x->by = -1;
    x->heading = mt->heading;
    x->ground = mt->type == TGT_SHIP ? MAP->water_level : map_height(MAP, mt->x, mt->z);
    x->pos = v3(mt->x, x->ground + 8, mt->z);
    x->hp = TDEF[x->type].hp;
    x->alive = true;
    x->cool = game_rand(g, 2, 6);
    x->spin = game_rand(g, 0, 6);
  }
  sim_update_tanker(g, 0);
  if (mode == MODE_DEFEND) {
    defend_setup(g, coop);
    for (int i = 0; i < g->nplanes; i++)
      if (g->planes[i].team == 0 && !g->planes[i].inactive) game_spawn(g, &g->planes[i]);
  }
}

Plane *game_enemy_of(Game *g, int team, vec3 pos) {
  Plane *best = NULL;
  double bd = 1e300;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *q = &g->planes[i];
    if (q->team == team || q->inactive) continue;
    double d = q->alive ? v3_dist(q->pos, pos) : 1e12;
    if (d < bd) { bd = d; best = q; }
  }
  return best;
}

bool ref_get(const Game *g, Ref r, vec3 *pos, vec3 *vel) {
  vec3 zero = v3(0, 0, 0);
  switch (r.kind) {
  case REF_PLANE: {
    const Plane *p = &g->planes[r.idx];
    if (!p->alive || p->inactive) return false;
    if (pos) *pos = p->pos;
    if (vel) *vel = p->vel;
    return true;
  }
  case REF_GROUND: {
    const Target *t = &g->targets[r.idx];
    if (!t->alive) return false;
    if (pos) *pos = t->pos;
    if (vel) *vel = zero;
    return true;
  }
  case REF_MISSILE:
    for (int i = 0; i < g->nmissiles; i++)
      if (g->missiles[i].id == r.gen) {
        if (pos) *pos = g->missiles[i].pos;
        if (vel) *vel = v3_scale(g->missiles[i].dir, g->missiles[i].speed);
        return true;
      }
    return false;
  case REF_CARRIER:
    if (!g->def.on || g->def.hp <= 0) return false;
    if (pos) *pos = v3(g->def.pos.x, g->def.deck * 0.6f, g->def.pos.z);
    if (vel) *vel = zero;
    return true;
  case REF_FLARE: {
    const Flare *f = &g->flares[r.idx];
    if (!f->live || f->gen != r.gen || f->life <= 0) return false;
    if (pos) *pos = f->pos;
    if (vel) *vel = f->vel;
    return true;
  }
  default:
    return false;
  }
}
