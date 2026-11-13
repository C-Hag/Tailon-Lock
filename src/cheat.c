/* cheat.c - the cheater (--pilot cheat): a test pilot that plays to win by
 * any means, the way a pentester attacks a system, and reports every way it
 * found to get ahead of the rules.
 *
 * Each match it tries one trick, flying the CPU's judgement through hands
 * that do something no fair player would plan: mashing every control at
 * once, firing through the spawn protection, hiding in the valleys, sitting
 * outside the arena, hanging under the ceiling, parking on the base to heal
 * and rearm in the middle of a fight, hiding beside the tanker, spamming
 * flares and missiles without a lock, flipping every switch every tick.
 *
 * Meanwhile the rule watch holds every jet (the cheater's and everyone
 * else's) to the rules each tick: stores and fuel only up on the base or
 * after a respawn, never past a full load; the gun no faster than its
 * cycle, missiles no faster than theirs; no damage taken while protected,
 * and none dealt by a protected jet either; inside the arena, under the
 * ceiling and over the ground; no lock and no hit through a mountain; no
 * points without a reason; no two jets through each other. Each broken rule
 * is a finding with where and when, and the tricks are scored against the
 * fair pilots on the same seeds. */
#include "cheat.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "core/jets.h"
#include "core/world.h"
#include "platform/plat.h"

enum { TR_MASH, TR_SPAWN, TR_VALLEY, TR_EDGE, TR_CEILING, TR_BASE, TR_TANKER, TR_SPAM, TR_SWITCH, TR_COUNT };
static const char *const TR_NAME[TR_COUNT] = { "mash every control", "shoot through the spawn protection",
                                               "hide in the valleys", "sit outside the arena", "hang under the ceiling",
                                               "heal and rearm on the base mid-fight", "hide beside the tanker",
                                               "spam flares and missiles without a lock", "flip every switch every tick" };

enum { RL_STORES, RL_FULL, RL_GUN, RL_MSL, RL_HEAL, RL_HIT_PROTECTED, RL_PROTECTED_HITS, RL_SPEED, RL_CEILING,
       RL_GROUND, RL_ARENA, RL_LOCK_TERRAIN, RL_HIT_TERRAIN, RL_SCORE, RL_THROUGH, RL_NAN, RL_COUNT };
static const char *const RL_NAME[RL_COUNT] = {
  "stores came back off the base",       "more stores than a full load",          "the gun fired faster than its cycle",
  "missiles faster than their cycle",    "the hull mended off the base",          "hit while spawn-protected",
  "a spawn-protected jet scored hits",   "faster than the jet can go",            "above the ceiling",
  "under the ground",                    "far outside the arena",                 "a lock through a mountain",
  "a hit through a mountain",            "points from nothing",                   "two jets through each other",
  "broken numbers (NaN)" };

typedef struct {
  int   n;                  /* times broken */
  float firstT;
  char  first[200];         /* the first time, in words */
} Rule;

typedef struct {
  float ammo, health, score, lastShot, lastMsl, fuel, spawnT;
  int   msl, radar, ground, flares, chaff, kills;
  bool  alive, refuel, have;
  vec3  pos;
  int   hidTicks; /* ticks locked on a jet behind the ground */
} Seen;

static struct {
  int   trick, cheater;     /* this match's trick, and the cheater's plane (-1 none) */
  float t;                  /* the trick's own clock */
  uint32_t rng;
  Seen  s[MAX_PLANES];
  Rule  r[RL_COUNT], total[RL_COUNT];
  /* the trick against fair play: the cheater's kills, deaths and time alive, per trick, over every match */
  struct { int n, kills, deaths, hitsTaken, hitsDealt; } score[TR_COUNT + 1];
  int   hitsTaken, hitsDealt;
} C = { .cheater = -1 };

static float crand(void) {
  C.rng = C.rng * 1664525u + 1013904223u;
  return (float)(C.rng >> 8) / 16777216.0f;
}

void cheat_start(Game *g, int cheater, int k, uint32_t seed) {
  memset(C.s, 0, sizeof C.s);
  memset(C.r, 0, sizeof C.r);
  C.trick = k % TR_COUNT;
  C.cheater = cheater;
  C.t = 0;
  C.rng = seed * 747796405u + 1;
  C.hitsTaken = C.hitsDealt = 0;
  for (int i = 0; i < MAX_PLANES; i++) C.s[i].lastShot = C.s[i].lastMsl = -10; /* nothing fired before the match */
  (void)g;
}

const char *cheat_trick(void) { return C.cheater >= 0 ? TR_NAME[C.trick] : ""; }

/* ---- the hands -------------------------------------------------------------- */
static float agl(const Plane *p) { return p->pos.y - terrain_h(p->pos.x, p->pos.z); }

/* pitch toward a height above the ground, keeping the CPU's turns */
static void hold_height(Plane *p, float want) {
  float err = want - agl(p), climb = p->vel.y;
  p->ctl.pitch = clampf(err * 0.004f - climb * 0.03f, -0.6f, 0.8f);
}

static void steer_to(Plane *p, vec3 at) {
  vec3 d = v3_norm(v3_sub(at, p->pos));
  float lx = v3_dot(d, p->r), lz = v3_dot(d, p->f), bank = fm_atan2f(-p->r.y, p->u.y);
  float tb = clampf(fm_atan2f(lx, fmaxf(lz, 0.05f)) * 2, -1.1f, 1.1f);
  p->ctl.roll = clampf((tb - bank) * 2, -1, 1);
}

void cheat_hands(Game *g, Plane *p, float dt) {
  Controls *c = &p->ctl;
  C.t += dt;
  const Plane *e = game_enemy_of(g, p->team, p->pos);
  switch (C.trick) {
  case TR_MASH: /* every control, at random, every tick */
    c->pitch = crand() * 2 - 1;
    c->roll = crand() * 2 - 1;
    c->gun = crand() < 0.5f;
    c->msl = crand() < 0.5f;
    c->flr = crand() < 0.5f;
    c->fire = crand() < 0.5f;
    c->sel = crand() < 0.3f;
    c->map = crand() < 0.1f;
    c->ap = crand() < 0.2f;
    c->vec = crand() < 0.5f;
    c->look = crand() < 0.5f;
    p->throttle = crand() < 0.5f ? 0 : 1;
    break;
  case TR_SPAWN: /* fire everything while the spawn protection lasts */
    if (p->invuln > 0) c->gun = c->msl = c->fire = true;
    break;
  case TR_VALLEY: /* as low as it goes: under the radar, behind the hills */
    if (!p->taxi && !p->ai.refuel) hold_height(p, 25);
    break;
  case TR_EDGE: { /* out past the edge, and stay there */
    if (!p->taxi && !p->ai.refuel) {
      vec3 out = v3(p->pos.x > 0 ? ARENA_X + 3000 : -ARENA_X - 3000, p->pos.y, p->pos.z);
      steer_to(p, out);
      c->pitch = clampf(c->pitch, -0.2f, 0.2f);
    }
    break;
  }
  case TR_CEILING: /* as high as it goes, firing down */
    if (!p->taxi && !p->ai.refuel) {
      hold_height(p, (g->classic ? 18288.0f : 7500.0f) - terrain_h(p->pos.x, p->pos.z) - 50);
      c->msl = c->gun = e && e->alive && v3_dist(e->pos, p->pos) < 4000;
    }
    break;
  case TR_BASE: /* go home and sit there with the enemy about: it heals and rearms */
    if (C.t > 20) p->ai.refuel = true;
    break;
  case TR_TANKER: /* beside the tanker, where a missile might take it instead */
    if (!p->taxi && !p->ai.refuel && !g->tankers[p->team & 1].gone) {
      const Tanker *T = &g->tankers[p->team & 1];
      vec3 at = v3_add(T->pos, v3_scale(T->r, 60));
      steer_to(p, at);
      hold_height(p, T->pos.y - terrain_h(p->pos.x, p->pos.z));
      p->throttle = v3_dist(p->pos, at) > 400 ? 1 : 0.5f;
    }
    break;
  case TR_SPAM: /* every flare and missile, lock or not */
    c->flr = !c->flr;
    c->msl = !c->msl;
    c->fire = !c->fire;
    c->sel = crand() < 0.05f;
    break;
  case TR_SWITCH: /* autopilot, map, vector, look and weapon select on and off every tick */
    c->ap = !c->ap;
    c->map = !c->map;
    c->vec = !c->vec;
    c->look = !c->look;
    c->sel = !c->sel;
    break;
  }
}

/* ---- the rule watch ---------------------------------------------------------- */
TL_PRINTF(3, 4) static void broke(const Game *g, int rule, const char *fmt, ...) {
  Rule *R = &C.r[rule];
  if (R->n++ == 0) {
    R->firstT = g->time;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(R->first, sizeof R->first, fmt, ap);
    va_end(ap);
  }
}

/* is there ground between a and b? */
static bool blocked(vec3 a, vec3 b) {
  for (int k = 1; k < 200; k++) { /* finer than the game looks, to catch what it misses */
    vec3 q = v3_add(a, v3_scale(v3_sub(b, a), (float)k / 200));
    if (q.y < terrain_h(q.x, q.z) - 5) return true;
  }
  return false;
}

void cheat_watch(Game *g, float dt) {
  (void)dt;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    Seen *S = &C.s[i];
    const char *who = p->name;
    if (p->inactive || p->remote) {
      S->have = false;
      continue;
    }
    /* a respawn, or classic rules' landing back on the carrier off its own end of the map (refuelled and
     * rearmed, launched again: the jet jumps there) */
    bool respawned = (p->alive && !S->alive) || (S->have && v3_dist(p->pos, S->pos) > 2000);
    if (respawned || !S->have) S->spawnT = g->time;
    if (S->have && p->alive && !respawned) {
      bool base = p->refuel || S->refuel;
      if (!base && (p->ammo > S->ammo + 0.01f || p->missiles > S->msl || p->radar > S->radar || p->ground > S->ground ||
                    p->flares > S->flares || p->chaff > S->chaff))
        broke(g, RL_STORES, "%s's stores went up in the air (ammo %.0f/%.0f, heat %d/%d, radar %d/%d, ground %d/%d, flares %d/%d, chaff %d/%d)",
              who, p->ammo, S->ammo, p->missiles, S->msl, p->radar, S->radar, p->ground, S->ground, p->flares, S->flares, p->chaff, S->chaff);
      int full[4];
      game_full_load(g, p, full);
      if (p->missiles > full[0] || p->radar > full[1] || p->ground > full[2] || p->ammo > full[3] + 0.5f ||
          p->flares > MAX_FLR || p->chaff > MAX_CHAFF)
        broke(g, RL_FULL, "%s carried %d missiles of %d, %.0f rounds of %d", who, p->missiles, full[0], p->ammo, full[3]);
      if (p->ammo < S->ammo - 1.5f)
        broke(g, RL_GUN, "%s fired %.0f rounds in one tick", who, S->ammo - p->ammo);
      else if (p->ammo < S->ammo) {
        if (g->time - S->lastShot < 0.07f) broke(g, RL_GUN, "%s fired again after %.3f s", who, g->time - S->lastShot);
        S->lastShot = g->time;
      }
      if (p->missiles + p->radar + p->ground < S->msl + S->radar + S->ground) {
        if (g->time - S->lastMsl < 0.75f) broke(g, RL_MSL, "%s launched again after %.2f s", who, g->time - S->lastMsl);
        S->lastMsl = g->time;
      }
      if (!base && p->health > S->health + 0.01f) broke(g, RL_HEAL, "%s's hull went from %.0f to %.0f off the base", who, S->health, p->health);
      if (p->health < S->health && p->invuln > 0) broke(g, RL_HIT_PROTECTED, "%s took %.0f damage while protected", who, S->health - p->health);
      /* hits dealt: another jet's hull going down with this one's name on it */
      if (p->health < S->health && p->lastHitBy >= 0 && p->lastHitBy < g->nplanes) {
        const Plane *by = &g->planes[p->lastHitBy];
        const Seen *B = &C.s[p->lastHitBy];
        /* a round or missile fired since the respawn (one from the last life may still land) */
        if (by->invuln > 0 && fmaxf(B->lastShot, B->lastMsl) >= B->spawnT) broke(g, RL_PROTECTED_HITS, "%s hit %s while %s was still spawn-protected", by->name, who, by->name);
        /* a gun hit (7 a round) inside gun range, with ground in between */
        if (by->alive && S->health - p->health < 7.5f && v3_dist(by->pos, p->pos) < 1800 && blocked(by->pos, p->pos)) broke(g, RL_HIT_TERRAIN, "%s was hit by %s through the ground, %.0f m apart", who, by->name, v3_dist(by->pos, p->pos));
        if (p->lastHitBy == C.cheater) C.hitsDealt++;
        if (i == C.cheater) C.hitsTaken++;
      }
    }
    if (p->alive && !p->taxi) {
      const JetType *jt = jet_type(p->jet, g->realistic);
      float vmax = 390 * jt->speed;
      if (p->speed > vmax * 1.25f && !p->drone) broke(g, RL_SPEED, "%s at %.0f kt, the jet's top is %.0f", who, p->speed * 1.944f, vmax * 1.944f);
      if (p->pos.y > (g->classic ? 18300 : 7510)) broke(g, RL_CEILING, "%s at %.0f ft", who, p->pos.y * 3.281f);
      if (p->pos.y < terrain_h(p->pos.x, p->pos.z) - 1) broke(g, RL_GROUND, "%s %.0f m under the ground", who, terrain_h(p->pos.x, p->pos.z) - p->pos.y);
      if (fabsf(p->pos.x) > ARENA_X + 6000 || fabsf(p->pos.z) > ARENA_Z + 6000)
        broke(g, RL_ARENA, "%s %.1f km outside the arena", who, (fmaxf(fabsf(p->pos.x) - ARENA_X, fabsf(p->pos.z) - ARENA_Z)) / 1000);
      if (!(p->pos.x == p->pos.x && p->pos.y == p->pos.y && p->speed == p->speed)) broke(g, RL_NAN, "%s's position is not a number", who);
      /* held a quarter of a second: the game looks once a tick, before the others have moved */
      bool hid = p->locked && p->lockTarget.kind == REF_PLANE && p->lockTarget.idx >= 0 && p->lockTarget.idx < g->nplanes &&
                 blocked(p->pos, g->planes[p->lockTarget.idx].pos);
      S->hidTicks = hid ? S->hidTicks + 1 : 0;
      if (S->hidTicks == 15)
        broke(g, RL_LOCK_TERRAIN, "%s locked on %s behind the ground", who, g->planes[p->lockTarget.idx].name);
      for (int j = i + 1; j < g->nplanes; j++) {
        const Plane *q = &g->planes[j];
        if (q->alive && !q->taxi && !q->inactive && v3_dist(p->pos, q->pos) < 10 && !p->remote)
          broke(g, RL_THROUGH, "%s and %s flew through each other, %.0f m apart", who, q->name, v3_dist(p->pos, q->pos));
      }
    }
    /* points: a kill, a hit or a target, nothing else */
    if (S->have && p->score > S->score) {
      bool why = p->kills > S->kills;
      for (int j = 0; j < g->nplanes && !why; j++) why = g->planes[j].lastHitBy == i && g->time - g->planes[j].lastHitT < 0.05f;
      for (int t = 0; t < g->ntargets && !why; t++) why = !g->targets[t].alive && g->targets[t].by == i;
      if (!why && !g->def.on) broke(g, RL_SCORE, "%s gained %.0f points with no kill, hit or target", who, p->score - S->score);
    }
    S->ammo = p->ammo;
    S->health = p->health;
    S->score = (float)p->score;
    S->msl = p->missiles;
    S->radar = p->radar;
    S->ground = p->ground;
    S->flares = p->flares;
    S->chaff = p->chaff;
    S->kills = p->kills;
    S->alive = p->alive;
    S->refuel = p->refuel;
    S->fuel = p->fuel;
    S->pos = p->pos;
    S->have = true;
  }
}

void cheat_match_end(const Game *g, FILE *f) {
  int t = C.cheater >= 0 ? C.trick : TR_COUNT;
  if (C.cheater >= 0) {
    const Plane *p = &g->planes[C.cheater];
    int lives0 = g->lives > 0 ? g->lives : LIVES;
    C.score[t].n++;
    C.score[t].kills += p->kills;
    C.score[t].deaths += lives0 - (p->lives < 0 ? 0 : p->lives);
    C.score[t].hitsTaken += C.hitsTaken;
    C.score[t].hitsDealt += C.hitsDealt;
  }
  int n = 0;
  for (int r = 0; r < RL_COUNT; r++) {
    if (!C.r[r].n) continue;
    n++;
    Rule *T = &C.total[r];
    if (!T->n) {
      T->firstT = C.r[r].firstT;
      memcpy(T->first, C.r[r].first, sizeof T->first);
    }
    T->n += C.r[r].n;
    plat_log("  FINDING %s: %d times, first at t=%.0f: %s", RL_NAME[r], C.r[r].n, C.r[r].firstT, C.r[r].first);
  }
  if (f && (n || C.cheater >= 0)) {
    fprintf(f, "\n### The cheater%s\n\n", C.cheater >= 0 ? "" : "'s rule watch");
    if (C.cheater >= 0) fprintf(f, "Trick: **%s** (%s).\n\n", TR_NAME[C.trick], g->planes[C.cheater].name);
    for (int r = 0; r < RL_COUNT; r++)
      if (C.r[r].n) fprintf(f, "- %s: %d times, first at t=%.0f s: %s\n", RL_NAME[r], C.r[r].n, C.r[r].firstT, C.r[r].first);
    if (!n) fprintf(f, "No rule broken.\n");
  }
}

int cheat_summary(FILE *f) {
  int broken = 0;
  for (int r = 0; r < RL_COUNT; r++) broken += C.total[r].n > 0;
  if (!f) return broken;
  fprintf(f, "\n## The cheater's findings\n\n| Rule | Broken | First time |\n|---|---|---|\n");
  for (int r = 0; r < RL_COUNT; r++)
    fprintf(f, "| %s | %s | %s |\n", RL_NAME[r], C.total[r].n ? "**yes**" : "no", C.total[r].n ? C.total[r].first : "");
  fprintf(f, "\n### Did a trick pay?\n\nThe cheater's results per trick, a match on average.\n\n| Trick | Matches | Kills | Deaths | Hits dealt | Hits taken |\n|---|---|---|---|---|---|\n");
  for (int t = 0; t < TR_COUNT; t++) {
    if (!C.score[t].n) continue;
    float n = (float)C.score[t].n;
    fprintf(f, "| %s | %d | %.1f | %.1f | %.0f | %.0f |\n", TR_NAME[t], C.score[t].n, C.score[t].kills / n, C.score[t].deaths / n,
            C.score[t].hitsDealt / n, C.score[t].hitsTaken / n);
  }
  fflush(f);
  return broken;
}
