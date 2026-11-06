/* testpilot.c - the test pilots (--watch --pilot, --test-pilots).
 *
 * A test pilot is the CPU pilot's judgement flown through a person's hands.
 * The CPU decides where to go; the hands decide how it reaches the
 * controls: late by the person's reaction time, through keys (all or
 * nothing, tapped for a gentle input) or a stick (smooth, a little
 * shaky), over-corrected, with moments of looking away. Four kinds of
 * player, written from what pilots and flight-game players commonly say
 * about flying in games:
 *
 *   FIGHTER PILOT   flies the energy and the g, smooth and quick, eases the
 *                   pull before the body gives up, trusts the gauges
 *   SIM VETERAN     plays study sims with a stick: expects real numbers and
 *                   notices floaty physics and anything that cheats
 *   ARCADE PLAYER   keyboard habits from action games: full deflection,
 *                   throttle at the stop, guns early, wants a quick fight
 *   NEWCOMER        a first flight game: slow, over-corrects, looks away,
 *                   sprays the gun and fires missiles without a lock
 *
 * While they fly, every jet is measured (how long to find the enemy, the
 * time the enemy was out of sight in a close fight, overshoots, stalls,
 * blackouts, the wings rocking, rounds and hits, crashes, fuel, take-off
 * and landing) and put through the mission checks: take-off, landing,
 * refuelling at the base and the tanker, rearming, ground strikes, kills
 * and the score. At the end of a match a table of rules turns the numbers
 * and the instrument check (watch.c) into the pilots' remarks. */
#include "testpilot.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "cheat.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"
#include "input.h"
#include "platform/plat.h"
#include "watch.h"
#include "core/safe.h"

/* ---- the four kinds of player --------------------------------------------- */
typedef struct {
  const char *id, *name, *about;
  float react;   /* seconds from seeing to the hands moving */
  bool  keys;    /* keys (tapped for part inputs) rather than a stick */
  float noise;   /* shake in the hands */
  float gain;    /* over-correction: 1 is just right */
  float gcap;    /* eases the pull above this g; 0: pulls regardless */
  float lapse;   /* chances a second of looking away for a moment */
  bool  spray;   /* fires the gun at anything near the nose */
  bool  noLock;  /* fires missiles without waiting for a lock */
  bool  stops;   /* throttle at idle or at the stop, nothing between */
} Persona;

static const Persona PERSONA[TP_COUNT] = {
  { "fighter", "FIGHTER PILOT", "smooth and quick on a stick, flies the energy, eases the pull above 7.5 g",
    0.20f, false, 0.02f, 1.0f, 7.5f, 0.0f, false, false, false },
  { "simmer", "SIM VETERAN", "a stick and study-sim habits, expects real numbers, pulls to the limit",
    0.25f, false, 0.04f, 1.0f, 0, 0.01f, false, false, false },
  { "arcade", "ARCADE PLAYER", "keys at full deflection, throttle at the stop, guns early",
    0.22f, true, 0, 1.2f, 0, 0.02f, true, false, true },
  { "rookie", "NEWCOMER", "slow on keys, over-corrects, looks away, sprays, fires without a lock",
    0.35f, true, 0.08f, 1.3f, 0, 0.04f, true, true, true },
  { "cheat", "CHEATER", "plays to win by any trick it can find (cheat.c), quick and steady on a stick",
    0.15f, false, 0, 1.0f, 0, 0, false, false, false },
};

int tp_persona_of(const char *name) {
  if (!strcmp(name, "all")) return TP_ALL;
  for (int i = 0; i < TP_COUNT; i++)
    if (!strcmp(name, PERSONA[i].id)) return i;
  return -1;
}

/* ---- the missions -------------------------------------------------------- */
enum { MS_TAKEOFF, MS_LANDING, MS_REFUEL, MS_TANKER, MS_REARM, MS_STRIKE, MS_KILL, MS_SCORE, MS_FLYBY, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = { "Take-off", "Landing", "Refuel at base", "Tanker refuel",
                                               "Rearm", "Ground strike", "Kill credit", "Score",
                                               "Refuelled flying past (open design point)" };
typedef struct {
  int  pass, fail;
  char note[3][120]; /* the first failures */
  int  nnote;
} Mission;

/* ---- one jet: the hands, then what was measured -------------------------- */
#define RING 64
typedef struct {
  int   persona; /* -1: a plain CPU pilot */
  /* the hands */
  float aiP, aiR;                   /* the CPU's own stick, kept apart from the hands' */
  float qp[RING], qr[RING];         /* what the CPU wanted, a reaction time ago */
  bool  qg[RING], qm[RING], qf[RING];
  int   qh;
  float outP, outR, dutyP, dutyR, lapseT;
  uint32_t rng;
  /* what is measured */
  float spotT, fightT, blindT, airT, outT, lowFuelT, minFuel, firstKillT, takeoffT, worstSink;
  int   overshoots, stalls, glocs, greys, reversals, rounds, hits, missiles, kills, deaths, crashes, collisions;
  int   refuels, tankers, landings;
  /* the state last tick */
  bool  alive, taxi, ground, stall, gloc, grey, refuel, ahead, rollingOff;
  float offWatch;
  float lastRevT, lastRoll, prevRollSign, bankPrev, aheadT, health, ammo, vy, airborneT, spawnT, leftGroundT;
  int   stores, kills0, score;
  /* refuel episode */
  float refT0, refFuel0, refSpeed, refAgl, refTop; /* refTop: the most fuel it had */
  int   refStores0;
  bool  refAir, refTanker;
  /* take-off and landing being watched */
  float toWatch, ldWatch, ldSink;
  int   pendingKill; /* the plane that shot us, waiting for its credit; -1 none */
  float pendingT;
  /* what the pilot said last, for the viewer */
  char  said[64];
  float saidT;
  char  remoteWho[24]; /* a match watched from another computer: the pilot's name from there */
} Pilot;

static struct {
  Pilot    p[MAX_PLANES];
  bool     tgtAlive[MAX_TARGETS];
  Mission  ms[MS_COUNT], total[MS_COUNT];
  int      persona, match;
  /* the totals per kind of player over every match */
  struct { int n; double fly[8]; int remark[32]; } sum[TP_COUNT + 1];
  int      remarkCount[32];
  double   checkOff[CK_COUNT];
  int      checkN[CK_COUNT];
  int      failed;
  uint32_t seed;
  int      turn; /* --pilot all: whose turn it is */
} S;

static float frand(Pilot *P) {
  P->rng = P->rng * 1664525u + 1013904223u;
  return (float)(P->rng >> 8) / 16777216.0f;
}

static void say(Pilot *P, const Game *g, const char *s) {
  snprintf(P->said, sizeof P->said, "%s", s);
  P->saidT = g->time;
}

A4C_PRINTF(4, 5) static void mission(const Game *g, int m, bool ok, const char *fmt, ...) {
  Mission *M = &S.ms[m];
  if (ok) {
    M->pass++;
    return;
  }
  M->fail++;
  if (m != MS_FLYBY) S.failed++; /* an open design point, reported, not failed */
  if (M->nnote < 3) {
    char b[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    snprintf(M->note[M->nnote++], sizeof M->note[0], "t=%.0f s: %s", g->time, b);
  }
}

static int stores(const Plane *p) { return p->missiles + p->radar + p->ground; }
static float agl(const Plane *p) { return p->pos.y - terrain_h(p->pos.x, p->pos.z); }
static float off_nose(const Plane *p, vec3 at) {
  return fm_acosf(clampf(v3_dot(v3_norm(v3_sub(at, p->pos)), p->f), -1, 1)) * (180 / PI_F);
}

/* ---- the doctrine: how the CPU pilots fight ----------------------------------
 * Measured from what the jets really did, over every match: straight and level
 * with an enemy near, the merges (who came out of each one on top), what kind
 * of fight each kill came from, the drone's post-stall moves flown back to
 * back, jets running dry in the air, the overshoots, two jets of a side on one
 * enemy while another goes free, top cover, and the wins by jet. */
#define DOC_LEVEL_R  10000 /* an enemy this near: radar range */
#define DOC_MERGE_R  1000  /* a pass this close is a merge */
#define DOC_CLOSE_R  1500  /* inside this, the fight is close */
#define DOC_JUDGE    20    /* seconds after a merge, who is on whose tail */
enum { FK_BVR, FK_MERGE, FK_TURN, FK_COUNT };
static const char *const FK_NAME[FK_COUNT] = { "from range (never within 1.5 km)", "at the merge (within 20 s of closing to 1.5 km)",
                                               "turning fight (later)" };
typedef struct {
  int   matches, wins, kills, deaths, merges, mergeWins, mergeLosses, overshoots, fuelOuts, fk[FK_COUNT];
  double levelT, fightT;
  float levelMax;
  int   level30, level30seen;
} DocJet;
static struct {
  DocJet jet[MAX_JETS];
  /* this match */
  float  levelRun[MAX_PLANES], levelSeen[MAX_PLANES];
  vec3   levelF[MAX_PLANES];
  float  levelH[MAX_PLANES];
  bool   dry[MAX_PLANES], stunt[MAX_PLANES], stuntUnrec[MAX_PLANES];
  float  closeT[MAX_PLANES][MAX_PLANES]; /* when the pair first closed to 1.5 km, -1 not yet */
  bool   passing[MAX_PLANES][MAX_PLANES];
  float  mergeT[MAX_PLANES][MAX_PLANES]; /* a merge being judged: its time, -1 none */
  float  stuntEnd[MAX_PLANES], recoverFrom[MAX_PLANES];
  int    kills0[MAX_PLANES], deaths0[MAX_PLANES], over0[MAX_PLANES];
  /* over every match */
  int    stunts, backToBack;
  double recoverSum;
  int    recoverN;
  double gangT, coverT, pairT;
  int    kindN[2], kindHit[2];
} DS;

static float doc_aspect(const Plane *a, const Plane *b) { /* a's nose off b plus b's tail off a, degrees */
  vec3 los = v3_norm(v3_sub(b->pos, a->pos));
  return (fm_acosf(clampf(v3_dot(a->f, los), -1, 1)) + fm_acosf(clampf(v3_dot(b->f, los), -1, 1))) * (180 / PI_F);
}

static void doc_start(const Game *g) {
  for (int i = 0; i < MAX_PLANES; i++) {
    DS.levelRun[i] = DS.levelSeen[i] = 0;
    DS.dry[i] = DS.stunt[i] = false;
    DS.stuntEnd[i] = -99;
    DS.recoverFrom[i] = -1;
    for (int j = 0; j < MAX_PLANES; j++) DS.closeT[i][j] = DS.mergeT[i][j] = -1, DS.passing[i][j] = false;
  }
  for (int i = 0; i < g->nplanes; i++) DS.kills0[i] = DS.deaths0[i] = DS.over0[i] = 0;
}

/* a merge judged: the one on the other's tail (or who got the kill) won it */
static void doc_judge(const Game *g, int i, int j, int killer) {
  const Plane *a = &g->planes[i], *b = &g->planes[j];
  int w = killer;
  if (w < 0 && a->alive && b->alive) {
    float sa = doc_aspect(a, b), sb = doc_aspect(b, a);
    if (sa < 120 && sa + 30 < sb) w = i;
    else if (sb < 120 && sb + 30 < sa) w = j;
  }
  if (w == i || w == j) {
    DS.jet[g->planes[w].jet].mergeWins++;
    DS.jet[g->planes[w == i ? j : i].jet].mergeLosses++;
  }
  DS.mergeT[i][j] = -1;
}

static void doc_tick(const Game *g, float dt) {
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *p = &g->planes[i];
    if (p->inactive || p->remote) continue;
    bool air = p->alive && !p->taxi && !p->onGround;
    DocJet *J = &DS.jet[p->jet];
    /* straight and level with an enemy in radar range */
    const Plane *e = game_enemy_of((Game *)g, p->team, p->pos);
    bool near = air && e && e->alive && v3_dist(e->pos, p->pos) < DOC_LEVEL_R;
    float bank = fm_atan2f(-p->r.y, p->u.y) * (180 / PI_F);
    bool level = near && fabsf(bank) < 8 && fabsf(p->f.y) < 0.07f;
    if (level && DS.levelRun[i] > 0 && (v3_dot(p->f, DS.levelF[i]) < 0.985f || fabsf(p->pos.y - DS.levelH[i]) > 150)) level = false;
    if (level) {
      if (DS.levelRun[i] == 0) DS.levelF[i] = p->f, DS.levelH[i] = p->pos.y;
      DS.levelRun[i] += dt;
      J->levelT += dt;
      J->levelMax = fmaxf(J->levelMax, DS.levelRun[i]);
      if (DS.levelRun[i] >= 30 && DS.levelRun[i] - dt < 30) J->level30++;
      /* the same with the enemy in the pilot's own picture (ai.c: seen on radar or by eye), which is what
       * a pilot can act on */
      DS.levelSeen[i] = p->ai.seen ? DS.levelSeen[i] + dt : 0;
      if (DS.levelSeen[i] >= 30 && DS.levelSeen[i] - dt < 30) J->level30seen++;
    } else DS.levelRun[i] = DS.levelSeen[i] = 0;
    if (near) J->fightT += dt;
    /* dry in the air */
    if (air && p->fuel <= 0 && !DS.dry[i]) J->fuelOuts++;
    DS.dry[i] = p->alive && p->fuel <= 0;
    /* the drone's post-stall moves, the Cobra and the flip: one begun within 6 s of the last one's end, or
     * before the speed came back from it (150 m/s), is back to back; and the time the speed takes to come back */
    if (p->drone && p->alive) {
      if (p->stunt && !DS.stunt[i]) DS.stuntUnrec[i] = g->time - DS.stuntEnd[i] < 6 || DS.recoverFrom[i] >= 0;
      if (!p->stunt && DS.stunt[i] && (p->lastStunt == STUNT_COBRA || p->lastStunt == STUNT_FLIP)) {
        DS.stunts++;
        if (DS.stuntUnrec[i]) DS.backToBack++;
        DS.stuntEnd[i] = DS.recoverFrom[i] = g->time;
      }
      if (DS.recoverFrom[i] >= 0 && !p->stunt && p->speed >= 150) {
        DS.recoverSum += g->time - DS.recoverFrom[i];
        DS.recoverN++;
        DS.recoverFrom[i] = -1;
      }
    }
    DS.stunt[i] = p->alive && p->stunt;
  }
  /* pairs of enemies: closing to 1.5 km, the merges, and judging them */
  for (int i = 0; i < g->nplanes; i++)
    for (int j = i + 1; j < g->nplanes; j++) {
      const Plane *a = &g->planes[i], *b = &g->planes[j];
      if (a->team == b->team) continue;
      if (DS.mergeT[i][j] >= 0 && g->time - DS.mergeT[i][j] >= DOC_JUDGE) doc_judge(g, i, j, -1);
      if (!a->alive || !b->alive || a->inactive || b->inactive) {
        DS.closeT[i][j] = -1;
        DS.passing[i][j] = false;
        continue;
      }
      float d = v3_dist(a->pos, b->pos);
      if (d < DOC_CLOSE_R && DS.closeT[i][j] < 0) DS.closeT[i][j] = g->time;
      if (d > 6000) DS.closeT[i][j] = -1; /* apart: the next meeting is a new fight */
      vec3 los = v3_norm(v3_sub(b->pos, a->pos));
      bool facing = v3_dot(a->f, los) > 0.5f && -v3_dot(b->f, los) > 0.5f; /* both noses within 60 degrees */
      if (d < DOC_MERGE_R && facing && !DS.passing[i][j] && v3_dot(v3_sub(a->vel, b->vel), los) > 0) {
        DS.passing[i][j] = true;
        DS.jet[a->jet].merges++;
        DS.jet[b->jet].merges++;
        if (DS.mergeT[i][j] < 0) DS.mergeT[i][j] = g->time;
      }
      if (d > 3000) DS.passing[i][j] = false;
    }
  /* two jets of a side on one enemy while another enemy goes free; one of them well above the fight */
  for (int i = 0; i < g->nplanes; i++)
    for (int j = i + 1; j < g->nplanes; j++) {
      const Plane *a = &g->planes[i], *b = &g->planes[j];
      if (a->team != b->team || !a->alive || !b->alive || a->inactive || b->inactive) continue;
      int enemies = 0;
      for (int k = 0; k < g->nplanes; k++)
        if (g->planes[k].team != a->team && g->planes[k].alive && !g->planes[k].inactive) enemies++;
      const Plane *ea = game_enemy_of((Game *)g, a->team, a->pos), *eb = game_enemy_of((Game *)g, b->team, b->pos);
      if (!ea || !eb || !ea->alive || !eb->alive) continue;
      bool fa = v3_dist(ea->pos, a->pos) < 3000, fb = v3_dist(eb->pos, b->pos) < 3000;
      if (!fa && !fb) continue;
      DS.pairT += dt;
      if (enemies >= 2 && fa && fb && ea == eb) DS.gangT += dt;
      if ((fa && !fb && b->pos.y > fmaxf(a->pos.y, ea->pos.y) + 1000) || (fb && !fa && a->pos.y > fmaxf(b->pos.y, eb->pos.y) + 1000))
        DS.coverT += dt;
    }
}

/* a kill: what kind of fight it came from */
static void doc_kill(const Game *g, int killer, int victim) {
  int i = killer < victim ? killer : victim, j = killer < victim ? victim : killer;
  float t0 = DS.closeT[i][j];
  int k = t0 < 0 ? FK_BVR : g->time - t0 < DOC_JUDGE ? FK_MERGE : FK_TURN;
  DS.jet[g->planes[killer].jet].fk[k]++;
  if (DS.mergeT[i][j] >= 0) doc_judge(g, i, j, killer);
}

static void doc_match_end(const Game *g) {
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *p = &g->planes[i];
    if (p->inactive && !p->alive) continue;
    DocJet *J = &DS.jet[p->jet];
    J->matches++;
    if (g->winner == p->team) J->wins++;
    J->kills += S.p[i].kills;
    J->deaths += S.p[i].deaths;
    J->overshoots += S.p[i].overshoots;
  }
  for (int i = 0; i < g->nplanes; i++)
    for (int j = i + 1; j < g->nplanes; j++)
      if (DS.mergeT[i][j] >= 0) doc_judge(g, i, j, -1);
}

static void doc_summary(FILE *f) {
  fprintf(f, "\n## Doctrine\n\nHow the CPU pilots fought, measured from what the jets did (every seat, every match). "
             "Straight and level: wings within 8 degrees, nose within 4, heading within 10 and height within 150 m, "
             "with an enemy inside 10 km. A merge: two enemies passing inside 1 km nose to nose; the winner is on the "
             "other's tail 20 s later (or got the kill).\n\n");
  fprintf(f, "| Jet | Matches | Wins | Kills | Deaths | Level near enemy | Longest level | Level over 30 s | Merges | "
             "Merges won / lost | Overshoots | Fuel-outs | Kills from range / merge / turning |\n"
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
  for (int k = 0; k < MAX_JETS; k++) {
    const DocJet *J = &DS.jet[k];
    if (!J->matches) continue;
    fprintf(f, "| %s (%s) | %d | %d (%.0f%%) | %d | %d | %.1f%% | %.0f s | %d | %d | %d / %d | %d | %d | %d / %d / %d |\n",
            JETS[k].name, JETS[k].id, J->matches, J->wins, 100.0 * J->wins / J->matches, J->kills, J->deaths,
            J->fightT > 0 ? 100 * J->levelT / J->fightT : 0, J->levelMax, J->level30, J->merges, J->mergeWins, J->mergeLosses,
            J->overshoots, J->fuelOuts, J->fk[FK_BVR], J->fk[FK_MERGE], J->fk[FK_TURN]);
  }
  int fk[FK_COUNT] = { 0 }, lv = 0, lvs = 0, mg = 0;
  for (int k = 0; k < MAX_JETS; k++) {
    for (int c = 0; c < FK_COUNT; c++) fk[c] += DS.jet[k].fk[c];
    lv += DS.jet[k].level30;
    lvs += DS.jet[k].level30seen;
  }
  int sy = jets_find("sy26"), vk = jets_find("vk33");
  mg = sy >= 0 ? DS.jet[sy].merges : 0;
  fprintf(f, "\n| | |\n|---|---|\n");
  fprintf(f, "| Kills %s | %d |\n| Kills %s | %d |\n| Kills %s | %d |\n", FK_NAME[0], fk[0], FK_NAME[1], fk[1], FK_NAME[2], fk[2]);
  fprintf(f, "| Straight and level over 30 s near an enemy | %d times |\n", lv);
  fprintf(f, "| ... of those with the enemy in the pilot's sight (CPU pilots that know only what they see) | %d times |\n", lvs);
  fprintf(f, "| Merges flown by the jet without a gun (sy26) | %d |\n", mg);
  fprintf(f, "| Fuel-outs in the air, sy26 / vk33 / all | %d / %d / ", sy >= 0 ? DS.jet[sy].fuelOuts : 0, vk >= 0 ? DS.jet[vk].fuelOuts : 0);
  int fo = 0;
  for (int k = 0; k < MAX_JETS; k++) fo += DS.jet[k].fuelOuts;
  fprintf(f, "%d |\n", fo);
  fprintf(f, "| Drone Cobras and flips / back to back | %d / %d |\n", DS.stunts, DS.backToBack);
  if (DS.recoverN) fprintf(f, "| Drone: speed back to 150 m/s after a Cobra or flip | %.1f s on average |\n", DS.recoverSum / DS.recoverN);
  if (DS.pairT > 0)
    fprintf(f, "| Two of a side on one enemy, another free | %.0f%% of the time a pair fought |\n| One of a pair 1 km above the fight | %.0f%% |\n",
            100 * DS.gangT / DS.pairT, 100 * DS.coverT / DS.pairT);
}

/* ---- the hands ------------------------------------------------------------ */
static float tap(float x, float *duty) {
  /* a key is on or off: a part input is a key tapped that share of the time */
  float a = fabsf(x);
  if (a > 0.85f) return x > 0 ? 1.0f : -1.0f;
  if (a < 0.12f) return 0;
  *duty += a;
  if (*duty < 1) return 0;
  *duty -= 1;
  return x > 0 ? 1.0f : -1.0f;
}

static void hands(Game *g, Plane *p, float dt, bool after) {
  Pilot *P = &S.p[p->idx];
  if (P->persona < 0) return;
  Controls *c = &p->ctl;
  if (!after) { /* the CPU thinks on from its own stick, not from where the hands put it */
    c->pitch = P->aiP;
    c->roll = P->aiR;
    return;
  }
  const Persona *K = &PERSONA[P->persona];
  P->aiP = c->pitch;
  P->aiR = c->roll;
  int h = P->qh = (P->qh + 1) % RING;
  P->qp[h] = c->pitch;
  P->qr[h] = c->roll;
  P->qg[h] = c->gun;
  P->qm[h] = c->msl;
  P->qf[h] = c->flr;
  int lag = (int)(K->react / dt + 0.5f);
  if (lag >= RING) lag = RING - 1;
  int d = (h - lag + RING) % RING;
  float wp = P->qp[d], wr = P->qr[d];
  bool gun = P->qg[d], msl = P->qm[d], flr = P->qf[d];
  /* looking away: the hands stay where they were */
  if (P->lapseT > 0) {
    P->lapseT -= dt;
    wp = P->outP;
    wr = P->outR;
    gun = msl = false;
  } else if (frand(P) < K->lapse * dt) {
    P->lapseT = 0.4f + frand(P) * 0.8f;
  }
  wp = clampf(wp * K->gain + (frand(P) - 0.5f) * 2 * K->noise, -1, 1);
  wr = clampf(wr * K->gain + (frand(P) - 0.5f) * 2 * K->noise, -1, 1);
  /* a person's hand is gentle near the centre and firm at the stops (the CPU's sharp corrections around wings
   * level, a reaction time late, rock the wings) */
  wr *= fabsf(wr);
  if (K->gcap > 0 && p->gload > K->gcap && wp > 0) wp *= 0.5f;
  /* on the approach anyone flies gently: with their reaction time, the CPU's full corrections porpoise */
  if (p->ai.final_leg) {
    wp *= 0.55f;
    wr *= 0.7f;
  }
  if (K->keys) {
    wp = tap(wp, &P->dutyP);
    wr = tap(wr, &P->dutyR);
  }
  Controls out = *c;
  out.pitch = P->outP;
  out.roll = P->outR;
  input_shape(&out, wp, wr, !K->keys, dt);
  P->outP = c->pitch = out.pitch;
  P->outR = c->roll = out.roll;
  /* the trigger: the CPU's, a reaction time late, or anything near the nose */
  Plane *e = game_enemy_of(g, p->team, p->pos);
  if (K->spray && e && e->alive && v3_dist(e->pos, p->pos) < 2500 && off_nose(p, e->pos) < 20) gun = true;
  if (K->noLock && e && e->alive && p->mslCool <= 0 && v3_dist(e->pos, p->pos) < 4000 && off_nose(p, e->pos) < 15) msl = true;
  c->gun = gun;
  c->msl = msl;
  c->flr = flr;
  /* the pilots who know it keep their eyes on a close enemy off the nose (the padlock view) */
  c->look = !K->keys && e && e->alive && !p->ai.refuel && v3_dist(e->pos, p->pos) < 4000 && off_nose(p, e->pos) > 40;
  /* the throttle at a stop: idle or full, whatever the CPU had in mind */
  if (K->stops && !p->taxi && !p->onGround && !p->ai.refuel) p->throttle = p->throttle > 0.3f ? 1 : 0; /* going home: as told */
  if (P->persona == TP_CHEAT) cheat_hands(g, p, dt);
}

void tp_start(Game *g, int persona, int k, uint32_t seed) {
  int cheater = -1;
  memset(S.p, 0, sizeof S.p);
  memset(S.ms, 0, sizeof S.ms);
  S.persona = persona;
  S.match = k;
  S.seed = seed;
  for (int i = 0; i < g->nplanes; i++) {
    Pilot *P = &S.p[i];
    const Plane *p = &g->planes[i];
    P->persona = -1;
    if (persona >= 0 && p->hidx >= 0)
      P->persona = persona == TP_ALL ? S.turn++ % TP_FAIR : persona == TP_CHEAT && cheater >= 0 ? TP_FIGHTER : persona;
    if (P->persona == TP_CHEAT) cheater = i; /* one cheater a match; a second seat flies fair against it */
    P->rng = seed * 2654435761u + (uint32_t)i * 40503u + 1;
    P->spotT = P->firstKillT = P->takeoffT = -1;
    P->minFuel = 100;
    P->alive = p->alive;
    P->taxi = p->taxi;
    P->ground = p->onGround;
    P->health = p->health;
    P->ammo = p->ammo;
    P->stores = stores(p);
    P->score = p->score;
    P->kills0 = p->kills;
    P->pendingKill = -1;
    P->spawnT = g->time;
    P->aiP = p->ctl.pitch;
    P->aiR = p->ctl.roll;
  }
  for (int i = 0; i < g->ntargets; i++) S.tgtAlive[i] = g->targets[i].alive;
  sim_hands = persona >= 0 ? hands : NULL;
  cheat_start(g, cheater, k, seed);
  doc_start(g);
}

const char *tp_who(int i) {
  if (S.p[i].remoteWho[0]) return S.p[i].remoteWho;
  return S.p[i].persona >= 0 ? PERSONA[S.p[i].persona].name : "";
}

void tp_remote(const Game *g, int i, const char *who, const char *said) {
  Pilot *P = &S.p[i];
  snprintf(P->remoteWho, sizeof P->remoteWho, "%s", who);
  if (strcmp(P->said, said) != 0) say(P, g, said);
}

const char *tp_said(const Game *g, int i) { return S.p[i].said[0] && g->time - S.p[i].saidT < 5 ? S.p[i].said : NULL; }

/* ---- measuring ------------------------------------------------------------ */
static void measure(Game *g, Plane *p, Pilot *P, float dt) {
  Plane *e = game_enemy_of(g, p->team, p->pos);
  bool  air = !p->taxi && !p->onGround;
  if (p->alive && air) P->airT += dt;
  if (p->alive && p->out) P->outT += dt;
  if (p->alive) P->minFuel = fminf(P->minFuel, p->fuel);
  if (p->alive && air && p->fuel < 20) P->lowFuelT += dt;
  if (p->alive && e && e->alive) {
    float d = v3_dist(e->pos, p->pos), a = off_nose(p, e->pos);
    if (P->spotT < 0 && d < 12000 && a < 44) {
      P->spotT = g->time;
      say(P, g, "TALLY - ENEMY IN SIGHT");
    }
    if (d < 3000 && air) {
      P->fightT += dt;
      if (a > 44 && !(p->ctl.look && a < 165)) P->blindT += dt; /* out of sight: off the front, and not padlocked */
    }
    /* an overshoot: close behind him, then he is behind us within two seconds */
    bool behindHim = v3_dot(e->f, v3_norm(v3_sub(e->pos, p->pos))) > 0.3f;
    if (d < 1500 && a < 50 && behindHim) {
      P->ahead = true;
      P->aheadT = g->time;
    } else if (P->ahead && d < 1500 && a > 130 && g->time - P->aheadT < 2) {
      P->overshoots++;
      P->ahead = false;
      say(P, g, "OVERSHOT - FLEW RIGHT PAST HIM");
    } else if (g->time - P->aheadT > 2) {
      P->ahead = false;
    }
    if (d < 3000 && a > 120 && P->saidT < g->time - 6) say(P, g, "LOST HIM - WHERE IS HE?");
  }
  if (p->alive && p->stall && !P->stall) {
    P->stalls++;
    say(P, g, "STALL - TOO SLOW");
  }
  P->stall = p->alive && p->stall;
  bool gloc = p->alive && p->gloc > 0, grey = p->alive && p->gstress > 1;
  if (gloc && !P->gloc) {
    P->glocs++;
    say(P, g, "BLACKED OUT");
  }
  if (grey && !P->grey) P->greys++;
  P->gloc = gloc;
  P->grey = grey;
  /* the wings rocking, as it looks from the cockpit when just flying (in a fight the bank swings on purpose):
   * the bank swinging back and forth more than 6 degrees, a swing within 1.2 s of the last */
  float bank = fm_atan2f(-p->r.y, p->u.y) * (180 / PI_F);
  bool cruise = !(e && e->alive && v3_dist(e->pos, p->pos) < 3000) && fabsf(bank) < 45; /* not fighting: just flying */
  if (p->alive && cruise && !p->onGround && !p->taxi && fabsf(bank - P->bankPrev) > 0.05f && fabsf(bank - P->bankPrev) < 90) {
    float dir = bank > P->bankPrev ? 1.0f : -1.0f;
    if (P->prevRollSign != 0 && dir != P->prevRollSign) { /* a turning point at the last bank */
      if (fabsf(P->bankPrev - P->lastRoll) > 6) {
        if (g->time - P->lastRevT < 1.2f) P->reversals++;
        P->lastRevT = g->time;
      }
      P->lastRoll = P->bankPrev;
    }
    P->prevRollSign = dir;
  }
  P->bankPrev = bank;
  if (p->alive && P->alive) {
    if (p->ammo < P->ammo) P->rounds += (int)(P->ammo - p->ammo);
    if (stores(p) < P->stores && !p->refuel) P->missiles += P->stores - stores(p);
  }
  if (p->kills > P->kills0) {
    if (P->firstKillT < 0) P->firstKillT = g->time;
    P->kills += p->kills - P->kills0;
    say(P, g, "SPLASH ONE");
  }
}

/* the hits each jet scores: another's hull going down with its name on it */
static void hits(Game *g) {
  for (int i = 0; i < g->nplanes; i++) {
    Plane *q = &g->planes[i];
    Pilot *Q = &S.p[i];
    if (q->alive && Q->alive && q->health < Q->health && q->lastHitBy >= 0 && q->lastHitBy < g->nplanes &&
        g->planes[q->lastHitBy].team != q->team)
      S.p[q->lastHitBy].hits++;
  }
}

static void missions(Game *g, Plane *p, Pilot *P, float dt) {
  const char *who = p->name;
  bool air = !p->taxi && !p->onGround;
  /* a jet that goes straight off again from its landing's roll-out has not departed: its next seconds count as the landing's */
  if (!P->taxi && p->taxi) P->rollingOff = p->speed > 30;
  /* take-off: off the ground and still flying five seconds later */
  if (P->alive && P->taxi && p->alive && !p->taxi) {
    if (P->rollingOff) P->offWatch = 5;
    else P->toWatch = 5;
    if (P->takeoffT < 0) P->takeoffT = g->time - P->spawnT;
  }
  if (P->offWatch > 0 && p->alive) P->offWatch -= dt;
  if (P->alive && !p->alive && (P->offWatch > 0 || (P->taxi && P->rollingOff)) && p->deathMsg && strcmp(p->deathMsg, "SHOT DOWN"))
    mission(g, MS_LANDING, false, "%s %s rolling off from a landing (%s)", who, p->deathMsg, JETS[p->jet].name);
  if (!p->alive) P->offWatch = 0;
  if (P->toWatch > 0 && p->alive) {
    P->toWatch -= dt;
    if (P->toWatch <= 0) mission(g, MS_TAKEOFF, true, "%s", "");
  }
  /* shot down while rolling or climbing out is the fight, not the take-off */
  if (P->alive && !p->alive && (P->taxi || P->toWatch > 0) && p->deathMsg && !strcmp(p->deathMsg, "SHOT DOWN")) P->toWatch = 0;
  else if (P->alive && !p->alive && ((P->taxi && !P->rollingOff) || P->toWatch > 0)) {
    mission(g, MS_TAKEOFF, false, "%s %s on take-off (%s)", who, p->deathMsg ? p->deathMsg : "lost", JETS[p->jet].name);
    P->toWatch = 0;
  }
  /* landing: wheels down from flight, gently, and still in one piece three seconds on */
  if (air && p->alive && agl(p) > 10) P->airborneT = g->time; /* really up: not the first metres of the climb-out */
  if (p->alive && P->alive && p->onGround && !P->ground && !p->taxi && g->time - P->airborneT < 5) {
    P->ldWatch = 3;
    P->ldSink = -P->vy;
    P->landings++;
    P->worstSink = fmaxf(P->worstSink, P->ldSink);
    say(P, g, "TOUCHDOWN");
  }
  if (P->ldWatch > 0) {
    if (!p->alive) {
      mission(g, MS_LANDING, false, "%s %s after touching down at %.0f ft/min", who, p->deathMsg ? p->deathMsg : "lost",
              P->ldSink * 196.85f);
      P->ldWatch = 0;
    } else if ((P->ldWatch -= dt) <= 0) {
      mission(g, MS_LANDING, P->ldSink < 6, "%s touched down at %.0f ft/min (hard)", who, P->ldSink * 196.85f);
    }
  }
  /* refuelling: fuel comes in while the jet is at the base or on the tanker's hose, and only there */
  bool tanker = !g->tankers[p->team & 1].gone && v3_dist(p->pos, g->tankers[p->team & 1].drogue) < 150;
  if (p->alive && p->refuel && !P->refuel) {
    P->refT0 = g->time;
    P->refFuel0 = p->fuel;
    P->refStores0 = stores(p);
    P->refAir = air;
    P->refTanker = tanker;
    P->refSpeed = p->speed;
    P->refAgl = agl(p);
    P->refTop = p->fuel;
    if (tanker) say(P, g, "ON THE HOSE - TAKING FUEL");
  }
  if (P->refuel && P->refAir && !P->refTanker && p->speed > P->refSpeed) P->refSpeed = p->speed;
  if (P->refuel) P->refTop = fmaxf(P->refTop, p->fuel);
  if (P->refuel && (!p->refuel || !p->alive)) {
    float dur = g->time - P->refT0, want = fminf(dur * 14 * 0.8f, 100 - P->refFuel0) - 1;
    int   m = P->refTanker ? MS_TANKER : MS_REFUEL;
    if (P->refTop > P->refFuel0 + 1 || stores(p) > P->refStores0) { /* something taken on, not just parked full */
      if (P->refTanker) P->tankers++;
      else P->refuels++;
    }
    if (dur >= 1 && P->refFuel0 < 95)
      mission(g, m, P->refTop - P->refFuel0 >= want || P->refTop >= 94, "%s got %.0f%% fuel in %.1f s (%.0f to %.0f%%)%s", who,
              P->refTop - P->refFuel0, dur, P->refFuel0, P->refTop, p->alive ? "" : ", then went down");
    /* the base fills a jet on the ground or crawling over the runway, not one flying past (a jet just
     * launched from it is full anyway) */
    bool gained = p->fuel > P->refFuel0 + 1 || stores(p) > P->refStores0;
    if (!P->refTanker && P->refAir && P->refSpeed > 100 && gained && P->refT0 - P->spawnT > 15)
      mission(g, MS_FLYBY, false, "%s took fuel and weapons from the base while still flying, up to %.0f kt, from %.0f ft up",
              who, P->refSpeed * 1.944f, P->refAgl * 3.281f);
    int full[4];
    game_full_load(g, p, full);
    int fullStores = full[0] + full[1] + full[2];
    if (dur >= 8 && P->refStores0 < fullStores)
      mission(g, MS_REARM, stores(p) >= (int)fminf((float)fullStores, P->refStores0 + floorf(dur / 1.4f) - 1) ||
                               stores(p) - P->refStores0 >= floorf(dur / 1.4f) - 1, /* one of each kind every 1.4 s */
              "%s had %d of %d stores after %.0f s on the base", who, stores(p), fullStores, dur);
  }
  P->refuel = p->alive && p->refuel;
  /* a kill: the jet that shot us down gets the credit */
  if (P->alive && !p->alive) {
    P->deaths++;
    if (p->deathMsg && !strcmp(p->deathMsg, "CRASHED")) P->crashes++, say(P, g, "FLEW INTO THE GROUND");
    else if (p->deathMsg && !strncmp(p->deathMsg, "MID-AIR", 7)) P->collisions++;
    else say(P, g, "I'M HIT - EJECTING");
    if (p->deathMsg && !strcmp(p->deathMsg, "SHOT DOWN") && p->lastHitBy >= 0 && p->lastHitBy < g->nplanes &&
        g->planes[p->lastHitBy].team != p->team && g->time - p->lastHitT < 2 * dt) { /* that hit, not a SAM after it */
      P->pendingKill = p->lastHitBy;
      P->pendingT = 1;
      doc_kill(g, p->lastHitBy, p->idx);
    }
  }
  if (P->pendingKill >= 0 && (P->pendingT -= dt) <= 0) {
    const Plane *k = &g->planes[P->pendingKill];
    mission(g, MS_KILL, p->kc[P->pendingKill] > 0 && k->kills > 0, "%s shot down %s and got no credit", k->name, who);
    P->pendingKill = -1;
  }
  /* the score only goes up */
  if (p->score < P->score) mission(g, MS_SCORE, false, "%s's score went down from %d to %d", who, P->score, p->score);
  else if (p->score > P->score) mission(g, MS_SCORE, true, "%s", "");
  if (!P->alive && p->alive) P->spawnT = g->time;
}

void tp_tick(Game *g, float dt) {
  cheat_watch(g, dt);
  hits(g);
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    Pilot *P = &S.p[i];
    if (p->inactive || p->remote) continue;
    measure(g, p, P, dt);
    missions(g, p, P, dt);
    P->alive = p->alive;
    P->taxi = p->taxi;
    P->ground = p->onGround;
    P->health = p->health;
    P->ammo = p->ammo;
    P->stores = stores(p);
    P->kills0 = p->kills;
    P->score = p->score;
    P->vy = p->vel.y;
  }
  /* a ground target down: the jet that did it is on the other side and has the points */
  for (int i = 0; i < g->ntargets; i++) {
    const Target *t = &g->targets[i];
    if (S.tgtAlive[i] && !t->alive && !t->remote) {
      if (t->by >= 0 && t->by < g->nplanes) {
        const Plane *by = &g->planes[t->by];
        mission(g, MS_STRIKE, by->team != t->team && by->score >= S.p[t->by].score,
                "%s got the wrong credit for a %s target", by->name, t->team ? "red" : "blue");
        say(&S.p[t->by], g, "TARGET DESTROYED");
      }
    }
    S.tgtAlive[i] = t->alive;
  }
  doc_tick(g, dt); /* after the kills were credited: a kill reads the pair's fight before it is cleared */
}

/* ---- the remarks ----------------------------------------------------------- */
enum { FT = 1 << TP_FIGHTER, SV = 1 << TP_SIMMER, AP = 1 << TP_ARCADE, NC = 1 << TP_ROOKIE, ALL = 15 };
typedef struct {
  int who;          /* the kinds of player who would say it */
  const char *text; /* %s-free; numbers are filled in by the rule */
} Remark;

/* Writes the remarks a pilot makes about the match into out (one per line,
 * "- " in front); returns how many, and marks which rules fired in hit. */
static int remarks(const Game *g, int i, char *out, size_t n, int *hit) {
  const Pilot *P = &S.p[i];
  int who = P->persona >= 0 ? 1 << P->persona : ALL, k = 0;
  size_t at = 0;
  float minutes = fmaxf(0.5f, g->time / 60);
#define PL(n) ((n) == 1 ? "" : "s")
#define SAY(rule, mask, ...)                                                                   \
  do {                                                                                         \
    if (((mask) & who) && at < n) {                                                            \
      at += (size_t)snprintf(out + at, n - at, "- ");                                          \
      if (at < n) at += (size_t)snprintf(out + at, n - at, __VA_ARGS__);                       \
      if (at < n) at += (size_t)snprintf(out + at, n - at, "\n");                              \
      hit[rule] = 1;                                                                           \
      k++;                                                                                     \
    }                                                                                          \
  } while (0)
  float goff = watch_off(i, CK_G);
  if (goff > 0.3f && !g->realistic)
    SAY(0, FT | SV, "The G-meter showed about half of what the turns felt like on the outside view (off %.0f%% of the time). "
                    "I fly by that number, so here I can't judge my turn or my energy from it.", goff * 100);
  if (goff >= 0 && goff < 0.05f)
    SAY(1, FT | SV, "The G-meter matched the jet's real motion. Good: I could fly the turn by the number.");
  if (watch_worst(i, CK_G) > 15)
    SAY(2, SV, "Something swung my jet round at about %.0f g with no warning, and the meter hardly moved. "
               "Nothing that flies does that; it feels like the game grabbing the jet.", watch_worst(i, CK_G));
  if (watch_off(i, CK_SPEED) == 0 && watch_off(i, CK_CLIMB) == 0 && watch_off(i, CK_ALT) == 0)
    SAY(3, FT | SV, "Airspeed, vertical speed and the altimeter agreed with the world for the whole match.");
  if (P->stalls >= 3) {
    SAY(4, NC | AP, "I stalled %d time%s. I didn't see it coming until the nose dropped.", P->stalls, PL(P->stalls));
    SAY(5, FT | SV, "Stalled %d time%s. The jet gives little warning before it goes; real wings buffet first.", P->stalls, PL(P->stalls));
  }
  if (P->glocs >= 1) {
    SAY(6, FT, "Blacked out %d time%s even easing the pull above 7.5 g; the G builds up quicker than my body's limit would.",
        P->glocs, PL(P->glocs));
    SAY(7, NC | AP | SV, "The screen went black %d time%s in hard turns and I lost control for a while.", P->glocs, PL(P->glocs));
  }
  if (P->fightT > 20 && P->blindT / P->fightT > 0.5f) {
    SAY(8, AP | NC, "In the close fight the enemy was out of my front view %.0f%% of the time, and I never found the "
                    "key to look around.", 100 * P->blindT / P->fightT);
    SAY(29, FT | SV, "Even with the padlock (G) the enemy was out of sight %.0f%% of the close fight: behind me, past "
                     "where a head turns.", 100 * P->blindT / P->fightT);
  }
  if (P->overshoots >= 3) {
    SAY(9, FT, "Overshot %d times. I couldn't bleed off speed fast enough at the merge.", P->overshoots);
    SAY(10, AP | NC, "I kept flying straight past him (%d times).", P->overshoots);
  }
  if (P->reversals / minutes > 6) {
    SAY(11, AP | NC, "On the keys I kept rocking the wings (%.0f swings a minute).", P->reversals / minutes);
    SAY(12, FT | SV, "The wings kept rocking, %.0f swings a minute: the roll is twitchy near the centre.", P->reversals / minutes);
  }
  if (P->rounds > 300 && (float)P->hits / (float)P->rounds < 0.02f)
    SAY(13, ALL, "%d rounds for %d hits: I hardly ever had the lead pipper on him long enough.", P->rounds, P->hits);
  if (P->spotT < 0) SAY(14, ALL, "I never got the enemy in my front view at all.");
  else if (P->spotT > 90) SAY(14, ALL, "It took %.0f s to find the enemy.", P->spotT);
  if (P->crashes > 0) {
    SAY(15, NC | AP, "I flew into the ground %d time%s.", P->crashes, PL(P->crashes));
    SAY(16, FT | SV, "%d controlled flight%s into terrain.", P->crashes, PL(P->crashes));
  }
  if (P->takeoffT > 45) SAY(17, AP | NC, "The take-off took %.0f s before I was in the fight.", P->takeoffT);
  if (P->worstSink > 5) SAY(18, FT | SV, "Hardest touchdown %.0f ft/min and the gear just took it.", P->worstSink * 196.85f);
  if (P->tankers > 0) SAY(19, SV | FT, "Took fuel from the tanker %d time%s; it's a real skill test and it worked.", P->tankers, PL(P->tankers));
  if (P->kills > 0 && P->deaths == 0) SAY(20, AP, "Quick kills, no deaths: that was fun.");
  if (P->firstKillT > 180 || (P->firstKillT < 0 && g->time > 180))
    SAY(21, AP, "Too long without a kill (%.0f s). I want the action sooner.", P->firstKillT < 0 ? g->time : P->firstKillT);
  if (P->minFuel < 5 && P->persona >= 0)
    SAY(22, ALL, "I got down to %.0f%% fuel before I made it home.", P->minFuel);
  if (P->outT > 30) SAY(23, ALL, "I spent %.0f s outside the combat area being pushed back in.", P->outT);
  if (P->missiles > 0 && P->kills == 0 && P->hits == 0)
    SAY(24, NC | AP, "Fired %d missile%s and nothing happened. I don't know what a lock is for.", P->missiles, PL(P->missiles));
  if (P->deaths >= 3 && P->kills == 0) SAY(25, NC, "I died %d times without a kill. Too hard to start with.", P->deaths);
  if (P->deaths == 0 && P->kills == 0 && g->time > 300)
    SAY(26, FT | SV, "A long match with no result: both jets just circled each other.");
  if (P->refuels > 0 && P->landings == 0)
    SAY(28, FT | SV, "I got fuel and new missiles flying low over the base without ever touching down.");
  if (k == 0 && P->persona >= 0) SAY(27, ALL, "Nothing stood out: it flew the way I expected.");
#undef SAY
#undef PL
  return k;
}

/* ---- the report ----------------------------------------------------------- */
static const char *pname(int i) { return S.p[i].persona >= 0 ? PERSONA[S.p[i].persona].name : "CPU PILOT"; }

void tp_match_end(const Game *g, FILE *f) {
  char cfg[160];
  static const char *const MODE[] = { "demo", "1 player", "2 players", "carrier defence", "beat the drone" };
  snprintf(cfg, sizeof cfg, "%s, %s, %s handling%s, seed %u", MODE[g->mode], MAP ? MAP->name : "classic arena",
           g->realistic ? "realistic" : "arcade", g->g_effects ? ", G effects" : "", S.seed);
  plat_log("TEST MATCH %d: %s, %.0f s, winner %d", S.match + 1, cfg, g->time, g->winner);
  if (f) {
    fprintf(f, "\n## Match %d\n\n%s. Lasted %.0f s; %s.\n\n", S.match + 1, cfg, g->time,
            g->winner < 0 ? "no winner" : g->winner == 0 ? "blue won" : "red won");
    fprintf(f, "### Gauges and picture against the world\n\nShare of the time each reading was off (worst difference in brackets). "
               "The picture rows are checked only on the jet the viewer showed.\n\n| Check |");
    for (int i = 0; i < g->nplanes; i++) fprintf(f, " %s (%s) |", g->planes[i].name, JETS[g->planes[i].jet].name);
    fprintf(f, "\n|---|");
    for (int i = 0; i < g->nplanes; i++) fprintf(f, "---|");
    fprintf(f, "\n");
    for (int c = 0; c < CK_COUNT; c++) {
      fprintf(f, "| %s |", CK_NAME[c]);
      for (int i = 0; i < g->nplanes; i++) {
        float o = watch_off(i, c);
        if (o < 0) fprintf(f, " - |");
        else if (o == 0) fprintf(f, " ok |");
        else fprintf(f, " %.1f%% (%.1f) |", o * 100, watch_worst(i, c));
      }
      fprintf(f, "\n");
    }
    fprintf(f, "\n### Missions\n\n| Mission | Passed | Failed | First failures |\n|---|---|---|---|\n");
    for (int m = 0; m < MS_COUNT; m++) {
      const Mission *M = &S.ms[m];
      fprintf(f, "| %s | %d | %d | ", MS_NAME[m], M->pass, M->fail);
      for (int k = 0; k < M->nnote; k++) fprintf(f, "%s%s", k ? "; " : "", M->note[k]);
      fprintf(f, " |\n");
    }
    fprintf(f, "\n### Pilots\n\n| | ");
    for (int i = 0; i < g->nplanes; i++) fprintf(f, "%s %s | ", g->planes[i].name, pname(i));
    fprintf(f, "\n|---|");
    for (int i = 0; i < g->nplanes; i++) fprintf(f, "---|");
    fprintf(f, "\n");
#define ROW(label, fmt, ...)                                              \
  do {                                                                \
    fprintf(f, "| %s |", label);                                      \
    for (int i = 0; i < g->nplanes; i++) {                            \
      const Pilot *P = &S.p[i];                                       \
      (void)P;                                                        \
      fprintf(f, " " fmt " |", __VA_ARGS__);                          \
    }                                                                 \
    fprintf(f, "\n");                                                 \
  } while (0)
    ROW("Jet", "%s", JETS[g->planes[i].jet].name);
    ROW("Kills / deaths", "%d / %d", P->kills, P->deaths);
    ROW("Found the enemy (s)", "%.0f", P->spotT);
    ROW("Enemy out of view in close fight", "%.0f%%", P->fightT > 0 ? 100 * P->blindT / P->fightT : 0);
    ROW("Overshoots", "%d", P->overshoots);
    ROW("Stalls / blackouts / grey-outs", "%d / %d / %d", P->stalls, P->glocs, P->greys);
    ROW("Wing rocks", "%d", P->reversals);
    ROW("Rounds / hits", "%d / %d", P->rounds, P->hits);
    ROW("Missiles fired", "%d", P->missiles);
    ROW("Crashes / collisions", "%d / %d", P->crashes, P->collisions);
    ROW("Take-off (s)", "%.0f", P->takeoffT);
    ROW("Landings (worst ft/min)", "%d (%.0f)", P->landings, P->worstSink * 196.85f);
    ROW("Refuels base / tanker", "%d / %d", P->refuels, P->tankers);
    ROW("Lowest fuel", "%.0f%%", P->minFuel);
#undef ROW
  }
  for (int i = 0; i < g->nplanes; i++) {
    char  txt[2400];
    int   hit[32] = { 0 };
    int   n = remarks(g, i, txt, sizeof txt, hit);
    const Pilot *P = &S.p[i];
    int   s = P->persona >= 0 ? P->persona : TP_COUNT;
    if (g->planes[i].hidx < 0 && P->persona < 0 && S.persona >= 0) continue; /* the opposition */
    S.sum[s].n++;
    double fly[8] = { P->kills, P->deaths, P->stalls, P->glocs, P->overshoots, P->crashes,
                      P->fightT > 0 ? P->blindT / P->fightT : 0, P->rounds ? (double)P->hits / P->rounds : 0 };
    for (int k = 0; k < 8; k++) S.sum[s].fly[k] += fly[k];
    for (int k = 0; k < 32; k++) S.sum[s].remark[k] += hit[k];
    if (f && n) fprintf(f, "\n**%s %s** (%s):\n\n%s", g->planes[i].name, pname(i), JETS[g->planes[i].jet].name, txt);
  }
  for (int c = 0; c < CK_COUNT; c++)
    for (int i = 0; i < g->nplanes; i++)
      if (watch_off(i, c) >= 0) S.checkOff[c] += watch_off(i, c), S.checkN[c]++;
  for (int m = 0; m < MS_COUNT; m++) {
    Mission *T = &S.total[m];
    T->pass += S.ms[m].pass;
    T->fail += S.ms[m].fail;
    for (int k = 0; k < S.ms[m].nnote && T->nnote < 3; k++) snprintf(T->note[T->nnote++], sizeof T->note[0], "%s", S.ms[m].note[k]);
    if (S.ms[m].fail) plat_log("  MISSION %s: %d passed, %d FAILED (%s)", MS_NAME[m], S.ms[m].pass, S.ms[m].fail, S.ms[m].note[0]);
  }
  doc_match_end(g);
  cheat_match_end(g, f);
  if (f) fflush(f);
}

void tp_summary(FILE *f) {
  if (!f) return;
  fprintf(f, "\n## Over every match\n\n### Missions\n\n| Mission | Passed | Failed | Examples |\n|---|---|---|---|\n");
  for (int m = 0; m < MS_COUNT; m++) {
    const Mission *M = &S.total[m];
    fprintf(f, "| %s | %d | %d | ", MS_NAME[m], M->pass, M->fail);
    for (int k = 0; k < M->nnote; k++) fprintf(f, "%s%s", k ? "; " : "", M->note[k]);
    fprintf(f, " |\n");
  }
  fprintf(f, "\n### Gauges against the world, average share of the time off\n\n| Check | Off |\n|---|---|\n");
  for (int c = 0; c < CK_COUNT; c++)
    if (S.checkN[c]) fprintf(f, "| %s | %.1f%% |\n", CK_NAME[c], 100 * S.checkOff[c] / S.checkN[c]);
  fprintf(f, "\n### Per kind of pilot, averages per match\n\n| Pilot | Matches | Kills | Deaths | Stalls | Blackouts | "
             "Overshoots | Crashes | Enemy out of view | Hit rate |\n|---|---|---|---|---|---|---|---|---|---|\n");
  for (int s = 0; s <= TP_COUNT; s++) {
    if (!S.sum[s].n) continue;
    double n = S.sum[s].n, *v = S.sum[s].fly;
    fprintf(f, "| %s | %d | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.0f%% | %.1f%% |\n", s < TP_COUNT ? PERSONA[s].name : "CPU PILOT",
            S.sum[s].n, v[0] / n, v[1] / n, v[2] / n, v[3] / n, v[4] / n, v[5] / n, 100 * v[6] / n, 100 * v[7] / n);
  }
  doc_summary(f);
  fprintf(f, "\n### Who are the pilots\n\n");
  for (int s = 0; s < TP_COUNT; s++)
    fprintf(f, "- **%s**: %s; reacts in %.2f s on %s.\n", PERSONA[s].name, PERSONA[s].about, PERSONA[s].react,
            PERSONA[s].keys ? "keys" : "a stick");
  fflush(f);
}

/* ---- --test-pilots: matches with no window -------------------------------- */
int tp_batch(int argc, char **argv) {
  int n = 12, persona = TP_ALL, diff = DIFF_NORMAL, pairs = 0, drones = 0;
  uint32_t seed = 2026;
  const char *path = NULL;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--matches") && i + 1 < argc) n = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
    else if (!strcmp(argv[i], "--pilot") && i + 1 < argc) persona = !strcmp(argv[i + 1], "cpu") ? (i++, -2) : tp_persona_of(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "--report") && i + 1 < argc) path = argv[++i];
    else if (!strcmp(argv[i], "--diff") && i + 1 < argc) {
      i++;
      diff = !strcmp(argv[i], "easy") ? DIFF_EASY : !strcmp(argv[i], "hard") ? DIFF_HARD : DIFF_NORMAL;
    } else if (!strcmp(argv[i], "--pairs") && i + 1 < argc) pairs = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
    else if (!strcmp(argv[i], "--drone") && i + 1 < argc) drones = (int)safe_int(argv[++i], 0, INT_MIN, INT_MAX);
  }
  if (persona == -1) persona = TP_ALL;
  if (persona == -2) persona = -1; /* --pilot cpu: plain CPU pilots everywhere, to compare with */
  FILE *f = path ? fopen(path, "w") : NULL;
  if (f) fprintf(f, "# Test pilot report\n\n%d matches, %s CPU pilots. Every jet's gauges are checked against its real motion in the world "
                    "every tick; the missions are checked on every jet; the test pilots fly the players' seats.\n", n + pairs + drones,
                    diff == DIFF_EASY ? "easy" : diff == DIFF_HARD ? "ace" : "normal");
  static Game G;
  Game *g = &G;
  int nm = assets_map_count();
  for (int k = 0; k < n + pairs + drones; k++) {
    /* odd matches: two test pilots against each other; even: one against a CPU pilot; then the 2 vs 2
     * matches (a test pilot and a CPU wingman against two CPU pilots), then Beat the Drone */
    GameMode mode = k >= n + pairs ? MODE_DRONE : k >= n ? MODE_1P : k % 2 ? MODE_2P : MODE_1P;
    GameRules R = { 0 };
    R.pairs = k >= n && k < n + pairs;
    R.realistic = (k / 2) % 2;
    R.g_effects = true;
    R.ground_start = (k / 4) % 2;
    R.classic = k % 5 == 4; /* every fifth: the classic rules, weapon selector and all */
    R.load[0][0] = R.load[1][0] = -1;
    const Map *mp = nm ? assets_map(k % nm) : NULL;
    world_set_map(mp, k % TOD_COUNT, mp ? mp->default_weather : WX_CLEAR);
    int jets[2] = { (int)((seed + k * 3) % NJETS), (int)((seed + k * 5 + 1) % NJETS) };
    game_new_rules(g, mode, (Difficulty)diff, false, jets, seed + (uint32_t)k * 7919u, &R);
    for (int i = 0; i < g->nplanes; i++) {
      Plane *p = &g->planes[i];
      if (p->hidx < 0) continue;
      p->is_ai = true;
      p->ai.diff = (Difficulty)diff;
      p->ai.gt = -1;
      p->ai.mslCool = 4;
    }
    watch_start(g, -1, -1);
    tp_start(g, persona, k, seed + (uint32_t)k * 7919u);
    const float dt = 1.0f / 60;
    for (int t = 0; t < 60 * 60 * 30 && !(g->over && g->overT <= 0); t++) {
      sim_update(g, dt);
      g->nsounds = 0;
      watch_tick(g, dt);
      tp_tick(g, dt);
    }
    watch_report(g);
    tp_match_end(g, f);
  }
  tp_summary(f);
  int broken = cheat_summary(f);
  sim_hands = NULL;
  if (f) fclose(f);
  plat_log("test pilots: %d matches, %d mission checks failed%s%s", n + pairs + drones, S.failed, path ? ", report in " : "", path ? path : "");
  if (broken) plat_log("test pilots: %d rules broken (the cheater's findings)", broken);
  return S.failed || broken ? 1 : 0;
}
