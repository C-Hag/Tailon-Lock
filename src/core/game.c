/* game.c - Game struct with fixed arrays; buildRoster, newGame, makePlane,
 * spawn (JS lines 2760-2827). */
#include "core/game.h"
#include "core/fmath.h"

#include <stdio.h>
#include <string.h>

#include "core/defend.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"

const char *const TNAME[TGT_TYPES] = { "SAM SITE", "FUEL DEPOT", "RADAR", "HANGAR", "FRIGATE", "RUBBLE" };
const TargetDef TDEF[TGT_TYPES] = {
  { 60, 16, 16, 12 }, { 40, 22, 22, 20 }, { 50, 13, 13, 30 }, { 120, 40, 28, 26 }, { 90, 8, 55, 24 }, { 0, 0, 0, 0 },
};

float game_rand(Game *g, float a, float b) { return rng_range(&g->rng, a, b); }
vec3 game_rand3(Game *g, float ax, float bx, float ay, float by, float az, float bz) {
  float x = game_rand(g, ax, bx), y = game_rand(g, ay, by);
  return v3(x, y, game_rand(g, az, bz));
}

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

const char *const STUNT_NAMES[4] = { "", "COBRA", "FLIP", "J-TURN" };

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
  else if (mode == MODE_DRONE) {
    /* coop here means player 2 flies the drone */
    ADD(0, false, coop ? "PLAYER 1" : "YOU", 0);
    ADD(1, !coop, "DRONE", coop ? 1 : -1);
  }
  else if (coop) { ADD(0, false, "PLAYER 1", 0); ADD(0, false, "PLAYER 2", 1); ADD(1, true, "CPU 1", -1); ADD(1, true, "CPU 2", -1); }
  else if (pairs) { ADD(0, false, "PLAYER 1", 0); ADD(0, true, "WINGMAN 1", -1); ADD(1, false, "PLAYER 2", 1); ADD(1, true, "WINGMAN 2", -1); }
  else { ADD(0, false, "PLAYER 1", 0); ADD(1, false, "PLAYER 2", 1); }
#undef ADD
  return n;
}

/* A CPU pilot's own loadout (modern rules), within the 8 stations a player has (heat + radar + 2 x ground):
 * for an air fight it leans on radar missiles, the more the better the pilot; with ground targets to strike it
 * takes one air-to-ground missile; attacking the carrier, heat-seekers for the anti-ship shots; a jet without a
 * gun keeps its six heat-seekers. Worked out from
 * the match alone (its mode, rules and difficulty), so both computers of a network match pick the same. */
static void cpu_load(const Game *g, const Plane *p, int *full) {
  /* (easy and normal carry radar missiles to strike missions too: with 6/0/1 they never had one to fire) */
  static const int AIR[3][3] = { { 6, 2, 0 }, { 6, 2, 0 }, { 4, 4, 0 } }, STRIKE[3][3] = { { 5, 1, 1 }, { 4, 2, 1 }, { 4, 2, 1 } };
  int d = g->diff < 0 ? 0 : g->diff > 2 ? 2 : (int)g->diff;
  /* the defence by its mode: def.on is set only later (defend_setup), after the jets are armed in game_new */
  bool defence = g->def.on || g->mode == MODE_DEFEND;
  bool strike = MAP && !g->dogfight && g->mode != MODE_DRONE && !defence;
  /* a jet built without a gun lives on its heat-seekers: the easy pilot's load, six of them, at every level */
  if (!plane_has_gun(p)) d = 0;
  const int *l = (defence && p->team == 1) || (strike && !plane_has_gun(p)) ? (const int[3]){ 6, 0, 1 } : strike ? STRIKE[d] : AIR[d];
  full[0] = l[0], full[1] = l[1], full[2] = l[2];
}

/* What a jet carries when full: heat, radar, air-to-ground, cannon rounds.
 * Classic rules use the chosen arming (or a standard 6 heat-seekers and an
 * air-to-ground missile), 3000 rounds, and in a dogfight heat-seekers only;
 * modern rules the chosen arming too (a jet with none, a CPU's: its own,
 * cpu_load). */
void game_full_load(const Game *g, const Plane *p, int *full) {
  bool chosen = p->load[0] >= 0;
  if (p->drone) { /* the drone: its cannon and four heat-seekers in the bay */
    full[0] = 4;
    full[1] = full[2] = 0;
    full[3] = g->classic ? 3000 : MAX_AMMO;
    return;
  }
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
    if (chosen) {
      full[0] = p->load[0] < MAX_MSL ? p->load[0] : MAX_MSL;
      full[1] = p->load[1];
      full[2] = p->load[2];
    } else cpu_load(g, p, full); /* a jet with no loadout picked: a CPU's */
    full[3] = MAX_AMMO;
  }
  if (!plane_has_gun(p)) full[3] = 0; /* missiles only */
}

/* Loads the stores (classic rules: 6 flares and 6 chaff pods). */
static void arm(Game *g, Plane *p) {
  int full[4];
  game_full_load(g, p, full);
  p->missiles = full[0];
  p->radar = full[1];
  p->ground = full[2];
  p->ammo = (float)full[3];
  if (!plane_has_gun(p) && p->wsel == W_CANNON) p->wsel = W_HEAT; /* classic: no cannon to select */
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
    x = cv->x + lx * fm_cosf(hd) + lz * fm_sinf(hd);
    z = cv->z - lx * fm_sinf(hd) + lz * fm_cosf(hd);
    p->oncat = true;
  } else {
    float bx = BASES[p->team].x, bz = BASES[p->team].z;
    hd = MAP ? MAP->bases[p->team].heading * PI_F / 180 : (BASES[p->team].dir > 0 ? 0 : PI_F);
    /* the threshold, wingmen side by side */
    x = bx - fm_sinf(hd) * 920 + fm_cosf(hd) * p->slot * 22;
    z = bz - fm_cosf(hd) * 920 - fm_sinf(hd) * p->slot * 22;
    p->oncat = false;
  }
  p->f = v3(fm_sinf(hd), 0, fm_cosf(hd));
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
  if (g->dogfight || g->mode == MODE_DRONE) {
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
    float s = p->team == 0 ? -1.0f : 1.0f, sx = fm_cosf(a), sz = -fm_sinf(a);
    p->pos = v3(cx + s * (fm_sinf(a) * 2000 + sx * 450) + p->slot * 300, 3000 + (p->team ? 400 : 0) + p->slot * 80,
                cz + s * (fm_cosf(a) * 2000 + sz * 450));
    p->f = v3(-s * fm_sinf(a), 0, -s * fm_cosf(a));
    p->u = v3(0, 1, 0);
    p->r = v3_cross(p->u, p->f);
  } else if (MAP) {
    /* the map's air spawn over the base, wingmen spread along the wing line */
    float h = MAP->spawn[p->team].heading * PI_F / 180;
    p->f = v3(fm_sinf(h), 0, fm_cosf(h));
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
  p->vecOn = p->stunt = false;
  p->aoa = 0;
  p->lastStunt = STUNT_NONE;
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
  p->gload = p->gmeter = 1;
  p->gstress = p->gneg = p->gloc = 0;
  /* on the runway or the catapult with the gear down and take-off flaps (realistic), in the air clean */
  p->gearDown = p->taxi && !p->drone;
  p->gearT = p->gearDown ? 1 : 0;
  p->flaps = (int8_t)(p->gearDown && g->realistic ? 1 : 0);
  p->flapT = p->flaps;
  p->flapUp = p->flaps > 0; /* off the ground, the next press brings them up */
  p->cpuLand = false;
  p->cpuFlaps = 0;
  p->gearWarnT = 0;
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
    g->ground_start = rules->ground_start && mode != MODE_DEFEND && mode != MODE_DRONE;
    g->no_crash = rules->no_crash;
    g->realistic = rules->realistic;
    g->dogfight = rules->dogfight && rules->classic && mode != MODE_DEFEND;
    g->hits_to_kill = rules->hits;
    g->lives = rules->lives;
    if (mode == MODE_DEFEND) { /* the classic rules options are not shown for the carrier defence */
      g->no_crash = false;
      g->hits_to_kill = 0;
      g->lives = 0;
    }
  }
  g->mode = mode;
  g->diff = diff;
  g->coop = coop && mode != MODE_DRONE; /* the drone's crew is not a squadron */
  g->rng.state = seed;
  g->dfx = game_rand(g, -3000, 3000);
  g->dfz = game_rand(g, -3000, 3000);
  g->dfa = game_rand(g, 0, 6.28f);
  g->winner = -1;

  sim_reset_tankers(g); /* none up: each pilot may call his side's in */
  (void)game_rand(g, 0, 6); /* the draw the old always-up tanker's orbit took: the rest of a match draws as before */

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
    if (mode == MODE_DRONE && roster[i].team == 1) {
      p->drone = true;
      arm(g, p);
    }
    if (rules && p->hidx >= 0 && !p->drone && game_load_fits(rules->load[p->hidx])) {
      memcpy(p->load, rules->load[p->hidx], sizeof p->load);
      arm(g, p);
    }
    p->idx = i;
    if (p->hidx >= 0) p->jet = seatJets ? seatJets[p->hidx] : 0;
    else p->jet = (int)(rng_f(&g->rng) * NJETS);
    if (p->jet < 0 || p->jet >= NJETS) p->jet = 0;
    if (p->drone) p->jet = DRONE_JET;
    /* armed above before the jet was known: again for a jet without a gun (no rounds), and for a CPU's own
     * loadout, now its team and the match are known */
    if (!plane_has_gun(p) || p->load[0] < 0) arm(g, p);
  }
  g->nplanes = n;
  for (int i = 0; i < n; i++)
    if (g->planes[i].hidx >= 0 && g->nlocals < 2) g->locals[g->nlocals++] = i;
  g->my = g->nlocals ? g->locals[0] : 0;

  static const struct { float dx, dz; int type; } OFF[6] = {
    { -1500, 1100, TGT_SAM }, { 1500, 1100, TGT_SAM }, { -800, -600, TGT_FUEL },
    { 800, -600, TGT_FUEL }, { -2000, 200, TGT_RADAR }, { 2000, 200, TGT_HANGAR },
  };
  /* the close range dogfight is jets only (classic rules), and so is the drone */
  bool targets = mode != MODE_DEFEND && mode != MODE_DRONE && !g->dogfight;
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
  if (mode == MODE_DRONE)
    for (int i = 0; i < g->nplanes; i++)
      if (g->planes[i].hidx >= 0) game_event(&g->planes[i], g->planes[i].drone ? "YOU FLY THE DRONE" : "BEAT THE DRONE!");
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
  case REF_TANKER: {
    const Tanker *T = &g->tankers[r.idx & 1];
    if (T->gone) return false;
    if (pos) *pos = T->pos;
    if (vel) *vel = T->vel;
    return true;
  }
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

/* Fuel burnt, % of the tank a second, at throttle thr (0 idle, 0.9 full dry power, 1 the afterburner).
 * Arcade: the old law, a quarter of the full rate at idle, two and a quarter minutes of tank at full power.
 * Realistic: a jet engine's own shape. A fighter's internal fuel lasts one to two hours at an economical
 * cruise, and its idle and approach flow is a small fraction of the afterburner's (the afterburner burns several
 * times full dry power on its own): so idle 0.02 (over an hour of tank), cruise at half power 0.13 (13 min:
 * the game's short area, flown fast), full dry power 0.22 (7.5 min), and the afterburner ramping in over the
 * last tenth to 0.74, the same 2.3 minutes of tank as arcade, so fuel still matters in a fight. A landing
 * circuit, a minute or two near idle and a third of the power, costs a few percent: under the old law it cost a
 * third of a tank and a missed approach left jets dry short of the runway. The jet's range figure scales it all
 * (burn), and the drone carries three times the fuel. */
float game_burn(const Game *g, const Plane *p, float thr) {
  float k = jet_type(p->jet, g->realistic)->burn * (p->drone ? 0.33f : 1.0f);
  if (!g->realistic) return (0.18f + 0.55f * thr) * k;
  return (0.02f + 0.22f * thr + (thr > 0.9f ? (thr - 0.9f) * 5 : 0)) * k;
}

float game_bingo(const Game *g, const Plane *p) {
  const Base *hb = &BASES[p->team];
  /* home: the runway, or the team's carrier when that is nearer (the jet lands there) */
  float d2 = (p->pos.x - hb->x) * (p->pos.x - hb->x) + (p->pos.z - hb->z) * (p->pos.z - hb->z);
  for (int i = 0; MAP && i < MAP->ncarriers; i++) {
    const Carrier *c = &MAP->carriers[i];
    float cd2 = (p->pos.x - c->x) * (p->pos.x - c->x) + (p->pos.z - c->z) * (p->pos.z - c->z);
    if (c->team == p->team && cd2 < d2) d2 = cd2;
  }
  float home = sqrtf(d2) + 3500;
  float need = home / 220 * game_burn(g, p, 0.8f) *
               (g->realistic ? 2.0f : 1.4f) + 6;
  /* and the circuit and the landing: about a minute more, mostly near idle (the test pilots found jets that
   * left at the old figure, capped at 35 %, running dry on the way in) */
  return fminf(need + 60 * game_burn(g, p, 0.2f), 50);
}

void game_let_go(Game *g) {
  for (int i = 0; i < g->nlocals; i++) {
    Controls *c = &g->planes[g->locals[i]].ctl;
    int8_t mslk = c->mslk; /* the CPU pilots' choice, not a control held */
    memset(c, 0, sizeof *c);
    c->mslk = mslk;
  }
  if (g->def.on) {
    g->def.cap_move = g->def.cap_side = g->def.cap_turn = g->def.cap_lookv = 0;
    g->def.cap_run = false;
    if (g->def.sel >= 0 && g->def.sel < g->def.nturrets) {
      Turret *t = &g->def.tur[g->def.sel];
      t->man_yaw = t->man_pitch = 0;
      t->man_fire = false;
    }
  }
}
