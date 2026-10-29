/* ai.c - CPU pilots with easy/normal/hard tables, DIFF and aiControl (JS lines 2890-2974). */
#include "core/ai.h"

#include "core/defend.h"
#include "core/jets.h"

const char *const BFM_NAMES[BFM_COUNT] = { "PURE PURSUIT", "LEAD PURSUIT", "LAG PURSUIT", "HIGH YO-YO", "LOW YO-YO",
                                           "PERCH",        "BREAK",        "GUNS JINK",   "EXTENSION",  "REVERSAL",
                                           "COBRA",        "FLIP",         "J-TURN",      "NOSE POINT", "VECTOR ROLL" };

const DiffTable DIFF[3] = {
  { 0.62f, 1.0f, 0.06f, 15, 0.35f, 0.07f, 0.5f, 5000 },
  { 0.85f, 0.55f, 0.045f, 9, 0.65f, 0.035f, 0.85f, 6500 },
  { 1.0f, 0.25f, 0.035f, 5, 0.9f, 0.012f, 1.0f, 9000 },
};

static float angle_to(vec3 goal, const Plane *p) {
  return acosf(clampf(v3_dot(v3_norm(v3_sub(goal, p->pos)), p->f), -1, 1));
}

/* Dogfighting, from the basic fighter manoeuvres and the aces' habits
 * (Boelcke, Yeager): see the enemy first, attack from above and out of the
 * sun, fly the pursuit curve that fits the range, stay near corner speed,
 * use the vertical (yo-yos) rather than overshoot, hold fire until close
 * and shoot in short bursts; when attacked, break into the attacker, jink
 * against a gun solution, and extend away when out of energy. Easy pilots
 * only chase; normal ones also break, yo-yo and lag; aces use everything. */
static void dogfight(Game *g, Plane *p, const Plane *e, float dist, float dt, vec3 *goal, float *thr, bool *gun, bool *msl,
                     bool *hard) {
  AiPilot *ai = &p->ai;
  const DiffTable *D = &DIFF[ai->diff];
  const JetType *jt = jet_type(p->jet, g->realistic);
  vec3 los = v3_scale(v3_sub(e->pos, p->pos), 1 / fmaxf(dist, 1));
  float ata = acosf(clampf(v3_dot(p->f, los), -1, 1));                  /* my nose off the enemy */
  float aot = acosf(clampf(v3_dot(e->f, los), -1, 1));                  /* off his tail: 0 = right behind him */
  float his_ata = acosf(clampf(-v3_dot(e->f, los), -1, 1));             /* his nose off me */
  float closure = v3_dot(v3_sub(p->vel, e->vel), los);                  /* m/s, positive closing */
  float vc = jet_corner(jt);
  float myE = p->pos.y + p->speed * p->speed / 19.62f, hisE = e->pos.y + e->speed * e->speed / 19.62f;
  bool behind_me = v3_dot(p->f, los) < 0;                                /* he is past my 3-9 line */
  bool threatened = his_ata < 0.5f && dist < 2600 && behind_me;
  int skill = ai->diff; /* 0 easy, 1 normal, 2 ace */
  int want;

  if (threatened && skill > 0) {
    if (dist < 650 && his_ata < 0.15f) want = BFM_JINK;                    /* he has a gun solution */
    else if (p->speed < vc * 0.55f && dist > 1200 && myE < hisE && p->pos.y - terrain_h(p->pos.x, p->pos.z) > 1500)
      want = BFM_EXTEND; /* no energy left to turn: dive away, with height to do it */
    else want = BFM_BREAK;
  } else if (skill > 0 && dist < 450 && closure > 60 && aot > 1.4f && !behind_me) {
    want = BFM_REVERSE; /* he overshot, or we are about to: turn back across his path */
  } else if (skill == 2 && dist > 3000 && e->pos.y + 700 > p->pos.y && !threatened) {
    want = BFM_PERCH; /* get above him first, out of the sun */
  } else if (dist > 1600) {
    want = BFM_PURSUIT; /* nose on him: missile range */
  } else if (skill > 0 && closure > 110 && aot > 0.6f) {
    want = BFM_HIGH_YOYO; /* closing too fast at an angle: use the vertical, don't overshoot */
  } else if (skill > 0 && ata > 1.0f && p->speed > e->speed * 0.8f && p->pos.y - terrain_h(p->pos.x, p->pos.z) > 1500) {
    want = BFM_LOW_YOYO; /* he out-turns us: cut across below his circle */
  } else if (skill > 0 && closure > 70 && dist < 700) {
    want = BFM_LAG; /* hold the range from inside his turn, keep the energy */
  } else want = BFM_LEAD;
  /* hold a manoeuvre for a moment rather than switch every frame */
  if (want != ai->bfm && (ai->bfmT > 0.8f || want == BFM_BREAK || want == BFM_JINK)) {
    ai->bfm = want;
    ai->bfmT = 0;
  }
  ai->bfmT += dt;

  float tLead = dist / (1150 + p->speed) * D->lead;
  vec3 lead = v3_add(v3_add(e->pos, v3_scale(e->vel, tLead)), v3_scale(ai->jit, dist * D->aimErr));
  /* a turning fight is won at corner speed */
  *thr = p->speed > vc * 1.15f ? 0.35f : 1;
  switch (ai->bfm) {
  case BFM_PERCH:
    *goal = v3_add(e->pos, v3(0, 900, 0));
    *thr = 1;
    break;
  case BFM_PURSUIT:
    *goal = e->pos;
    *thr = 1;
    break;
  case BFM_LEAD:
    *goal = lead;
    if (closure > 60 && dist < 600) *thr = 0.2f;
    break;
  case BFM_LAG: /* nose behind his tail */
    *goal = v3_sub(e->pos, v3_scale(e->f, 350));
    break;
  case BFM_HIGH_YOYO: /* pull up out of his plane, then come back down behind him */
    *goal = v3_add(v3_sub(e->pos, v3_scale(e->f, 300)), v3(0, 450, 0));
    *thr = 0.6f;
    break;
  case BFM_LOW_YOYO: /* drop the nose inside his turn for speed, then pull up to him */
    *goal = v3_add(lead, v3(0, -450, 0));
    *thr = 1;
    *hard = true;
    break;
  case BFM_BREAK: { /* turn hard into him, across his line of fire */
    vec3 side = v3_cross(v3(0, 1, 0), p->f);
    if (v3_len(side) < 0.1f) side = p->r;
    side = v3_norm(side);
    if (v3_dot(side, los) < 0) side = v3_scale(side, -1);
    *goal = v3_add(p->pos, v3_scale(v3_add(side, v3(0, p->pos.y - terrain_h(p->pos.x, p->pos.z) > 1200 ? -0.15f : 0.1f, 0)), 1000));
    *hard = true;
    break;
  }
  case BFM_JINK: { /* random rolls and pulls to spoil his aim */
    vec3 off = v3_add(v3_scale(p->r, ai->jit.x * 900), v3_scale(p->u, ai->jit.y * 600 + 200));
    *goal = v3_add(v3_add(p->pos, v3_scale(p->f, 400)), off);
    *hard = true;
    *thr = 1;
    break;
  }
  case BFM_EXTEND: /* unload and dive away at full power */
    *goal = v3_add(p->pos, v3_add(v3_scale(v3_norm(v3(p->f.x, 0, p->f.z)), 1000), v3(0, -500, 0)));
    *thr = 1;
    break;
  case BFM_REVERSE: { /* turn back across his flight path, behind him, into the scissors */
    vec3 away = v3_cross(v3(0, 1, 0), e->f);
    if (v3_dot(away, los) > 0) away = v3_scale(away, -1);
    *goal = v3_add(v3_add(e->pos, v3_scale(e->f, -600)), v3_add(v3_scale(v3_norm(away), 250), v3(0, 150, 0)));
    *hard = true;
    break;
  }
  }
  /* never fly into him: if the paths pass within 150 m in the next three
   * seconds, turn away from where he will be */
  vec3 r = v3_sub(e->pos, p->pos), w = v3_sub(e->vel, p->vel);
  float ww = v3_dot(w, w), tc = ww > 1 ? -v3_dot(r, w) / ww : -1;
  if (tc > 0 && tc < 3) {
    vec3 miss = v3_add(r, v3_scale(w, tc));
    if (v3_len(miss) < 150) {
      vec3 dodge = v3_len(miss) > 5 ? v3_norm(miss) : p->u;
      *goal = v3_add(p->pos, v3_sub(v3_scale(p->f, 300), v3_scale(dodge, 700)));
      *hard = true;
    }
  }
  /* guns: Yeager held his fire until he was close, then short bursts */
  float gunR = skill == 0 ? 1300 : skill == 1 ? 900 : 750;
  ai->burstT -= dt;
  if (ai->burstT < -0.5f) ai->burstT = 0.6f;
  if (angle_to(lead, p) < D->gunCone && dist < gunR && ai->burstT > 0) *gun = true;
  if (p->locked && ref_eq(p->lockTarget, ref_plane(e->idx)) && ai->mslCool <= 0 && dist > 500 &&
      (skill < 2 || aot < 1.2f)) { /* an ace shoots from behind, where it hits */
    *msl = true;
    ai->mslCool = D->mslGap + game_rand(g, 0, 3);
  }
}

/* The drone's post-stall moves (thrust vectoring, sim.c fly_drone), flown
 * between its jet-style dogfighting: with a fighter on its tail it throws
 * the nose up into a Cobra or a flip, so the fighter overshoots, then
 * points the nose and the gun at him; with him behind and far off it
 * J-turns to face him; close and off its nose it points at him without
 * turning the path; and against a missile it rolls hard round the
 * vectored nose. An easy drone does this rarely, an ace drone often.
 * Writes the stick directly (direct) or a goal to steer at. Returns false
 * when no move is being flown. */
static bool drone_moves(Game *g, Plane *p, const Plane *e, float dist, float missileD, float dt, vec3 *goal, float *thr,
                        bool *gun, bool *hard, bool *vec, bool *direct, float *dp, float *dr) {
  AiPilot *ai = &p->ai;
  const DiffTable *D = &DIFF[ai->diff];
  int skill = ai->diff;
  float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
  ai->moveCool -= dt;
  bool moving = ai->bfm >= BFM_COBRA;
  if (!e || !e->alive) {
    if (moving) ai->bfm = BFM_PURSUIT;
    return false;
  }
  vec3 los = v3_scale(v3_sub(e->pos, p->pos), 1 / fmaxf(dist, 1));
  float ata = acosf(clampf(v3_dot(p->f, los), -1, 1));
  float his_ata = acosf(clampf(-v3_dot(e->f, los), -1, 1));
  float tLead = dist / (1150 + p->speed) * D->lead;
  vec3 lead = v3_add(e->pos, v3_scale(e->vel, tLead));
  if (!moving && ai->moveCool <= 0 && agl > 800) {
    static const float COOL[3] = { 14, 7, 3.5f };
    int want = -1;
    if (missileD < 1400) want = BFM_ROLL;
    else if (his_ata < 0.6f && dist < 1400 && ata > 1.9f) /* on our tail */
      want = p->speed > 120 ? (game_rand(g, 0, 1) < 0.6f ? BFM_COBRA : BFM_FLIP) : BFM_JTURN;
    else if (ata > 2.0f && dist > 700 && dist < 4000) want = BFM_JTURN;
    else if (skill > 0 && dist < 1000 && ata > 0.2f && ata < 1.3f) want = BFM_POINT;
    if (want >= 0) {
      ai->bfm = want;
      ai->bfmT = 0;
      ai->moveCool = COOL[skill] * (want == BFM_POINT ? 0.6f : 1.0f);
      moving = true;
    }
  }
  if (!moving) return false;
  ai->bfmT += dt;
  float t = ai->bfmT;
  bool done = false;
  switch (ai->bfm) {
  case BFM_COBRA: /* nose up past the vertical, hold, let it fall back */
    *thr = 1;
    *direct = true;
    *dr = 0;
    if (t < 1.4f && !(t > 0.2f && p->speed < 45)) {
      *vec = true;
      *dp = clampf((1.55f - p->aoa) * 4, -1, 1); /* hold the nose near 90 degrees */
    } else if (t < 2.0f) *dp = 0;
    else done = true;
    break;
  case BFM_FLIP: /* the nose all the way round, back onto the path */
    *thr = 1;
    *direct = true;
    *vec = true;
    *dp = 1;
    *dr = 0;
    if (fabsf(p->stuntPitch) > 2 * PI_F - 0.25f || t > 3.2f) {
      *vec = false;
      *dp = 0;
      done = t > 3.6f || p->aoa < 0.3f;
    }
    break;
  case BFM_JTURN: { /* nose round onto him, held there until the path has followed it */
    *thr = 1;
    *vec = true;
    *hard = true;
    *goal = e->pos;
    vec3 path = v3_norm(p->vel);
    done = (ata < 0.3f && v3_dot(path, los) > 0.7f) || t > 5.0f;
    break;
  }
  case BFM_POINT: /* point nose and gun at him off the path */
    *vec = true;
    *hard = true;
    *goal = lead;
    *thr = 0.7f;
    if (acosf(clampf(v3_dot(v3_norm(v3_sub(lead, p->pos)), p->f), -1, 1)) < D->gunCone * 1.5f && dist < 900) *gun = true;
    done = t > 1.6f || ata > 1.6f || dist > 1300;
    break;
  case BFM_ROLL: /* a hard vectored roll to spoil the missile's aim */
    *thr = 1;
    *direct = true;
    *vec = true;
    *dp = 0.6f;
    *dr = ai->jit.x < 0 ? -1.0f : 1.0f;
    done = t > 1.4f;
    break;
  default:
    done = true;
  }
  if (done) {
    /* a Cobra or a flip leaves him out in front: point at him */
    int prev = ai->bfm;
    ai->bfm = BFM_PURSUIT;
    ai->bfmT = 0;
    if ((prev == BFM_COBRA || prev == BFM_FLIP) && skill > 0 && dist < 1300 && ata < 1.3f) {
      ai->bfm = BFM_POINT;
      return true;
    }
    *vec = *direct = false;
  }
  return true;
}

void ai_control(Game *g, Plane *p, float dt) {
  AiPilot *ai = &p->ai;
  const DiffTable *D = &DIFF[ai->diff];
  Controls *c = &p->ctl;
  Plane *e = game_enemy_of(g, p->team, p->pos);
  vec3 goal = v3(0, 2000, 0);
  float thr = 1, minAlt = 220;
  bool gun = false, msl = false, flr = false, hard = false;

  ai->flareCool -= dt;
  ai->mslCool -= dt;
  ai->jitT -= dt;
  if (ai->jitT <= 0) {
    ai->jitT = 0.5f;
    ai->jit = v3(game_rand(g, -1, 1), game_rand(g, -1, 1), game_rand(g, -1, 1));
  }

  const Missile *threat = NULL;
  float td = 1e9f;
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    if (m->target.kind == REF_PLANE && m->target.idx == p->idx) {
      float d = v3_dist(m->pos, p->pos);
      if (d < td) { td = d; threat = m; }
    }
  }
  if (threat && td < 2600) ai->threatT += dt;
  else ai->threatT = 0;

  /* home to refuel with enough left to get there: the base can be the far
   * side of the map, and full power burns a tank in about two minutes */
  float need;
  {
    const Base *hb = &BASES[p->team];
    float home = sqrtf((p->pos.x - hb->x) * (p->pos.x - hb->x) + (p->pos.z - hb->z) * (p->pos.z - hb->z)) + 3500;
    need = home / 220 * (0.18f + 0.55f * 0.8f) * jet_type(p->jet, g->realistic)->burn * (p->drone ? 0.33f : 1.0f) *
                 (g->realistic ? 2.0f : 1.4f) + 6;
    need = fminf(need, 35);
    if (p->fuel < fmaxf(22, need) || (p->missiles == 0 && p->ammo < 60)) ai->refuel = true;
  }
  /* full again: fuel, and the missiles it carries (a loadout can have fewer than four) */
  int full[4];
  game_full_load(g, p, full);
  if (ai->refuel && p->fuel > 94 && p->missiles >= (full[0] < 4 ? full[0] : 4)) { ai->refuel = false; ai->final_leg = false; }
  float eDist = e && e->alive ? v3_dist(e->pos, p->pos) : 1e9f;

  bool enemyTargetsLeft = false;
  for (int i = 0; i < g->ntargets; i++)
    if (g->targets[i].alive && g->targets[i].team != p->team) enemyTargetsLeft = true;

  if (threat && ai->threatT > D->react) {
    if (ai->flareCool <= 0 && p->flares > 0) {
      if (game_rand(g, 0, 1) < D->flareP) flr = true;
      ai->flareCool = 1.1f;
    }
    vec3 tm = v3_norm(v3_sub(threat->pos, p->pos));
    vec3 side = v3_cross(tm, v3(0, 1, 0));
    if (v3_len(side) < 0.1f) side = p->r;
    side = v3_norm(side);
    if (v3_dot(side, p->f) < 0) side = v3_scale(side, -1);
    /* beam it (keep it at 3 or 9 o'clock, slightly nose low); an ace waits
     * until it is close, then breaks hard across its path */
    goal = v3_add(p->pos, v3_scale(v3_add(side, v3(0, -0.08f, 0)), 1000));
    if (ai->diff == DIFF_HARD && td < 900) {
      goal = v3_add(p->pos, v3_scale(v3_add(v3_scale(side, -1), v3_scale(tm, 0.6f)), 1000));
      hard = true;
    }
  } else if (ai->refuel && !(e && e->alive && eDist < 1200 && p->fuel > need)) {
    const Base *b = &BASES[p->team];
    float elev = terrain_h(b->x, b->z);
    vec3 E = v3(b->x, elev + 260, b->z + b->dir * 3200), T = v3(b->x, elev + 70, b->z - b->dir * 900);
    if (!ai->final_leg) {
      goal = E;
      thr = 0.8f;
      /* realistic slows down slower (no arcade drag): come in at circuit
       * speed, power off and speed brakes out until there */
      if (g->realistic && p->speed > 165) thr = 0;
      /* over the gate, whatever the height (it can sit over a hill the
       * terrain avoidance keeps us above); realistic turns wider, and a
       * 600 m gate is one it can only circle */
      float gx = E.x - p->pos.x, gz = E.z - p->pos.z;
      if (gx * gx + gz * gz < (g->realistic ? 1500.0f * 1500.0f : 600.0f * 600.0f) && (!g->realistic || p->speed < 200))
        ai->final_leg = true;
    } else {
      goal = T;
      thr = 0.03f;
      minAlt = 25;
      if ((p->pos.z - T.z) * b->dir < 0 || fabsf(p->pos.x - b->x) > 900) ai->final_leg = false;
    }
  } else if (g->def.on && p->team == 1 && !(e && e->alive && eDist < 1500) && g->def.hp > 0) {
    /* carrier strike: run in low, fire the anti-ship missiles from about
     * 5 km, strafe on the way past, then run out and come round again */
    const Defense *d = &g->def;
    vec3 cv = v3(d->pos.x, d->deck + 40, d->pos.z);
    float cd = v3_dist(cv, p->pos);
    ai->egressT -= dt;
    if (ai->egressT > 0) {
      goal = ai->egress;
      thr = 1;
    } else {
      goal = cd > 6000 ? v3(cv.x, 450, cv.z) : cv;
      thr = 1;
      float ang = angle_to(cv, p);
      if (cd < 6500 && cd > 2500 && ang < 0.25f && ai->mslCool <= 0 && p->missiles > 0) {
        defend_ashm(g, p);
        ai->mslCool = D->mslGap * 0.5f + game_rand(g, 1, 3);
      }
      if (cd < 1500 && ang < D->gunCone * 2) gun = true;
      if (cd < 500) {
        vec3 away = v3_norm(v3(p->f.x, 0, p->f.z));
        ai->egress = v3_add(p->pos, v3_add(v3_scale(away, 5000), v3(game_rand(g, -1500, 1500), 600, 0)));
        ai->egressT = 12;
      }
    }
    minAlt = 60;
  } else if (e && e->alive && (eDist < D->fightR || !enemyTargetsLeft)) {
    dogfight(g, p, e, eDist, dt, &goal, &thr, &gun, &msl, &hard);
  } else {
    if (ai->gt < 0 || !g->targets[ai->gt].alive) {
      int best = -1;
      float bd = 1e9f;
      for (int i = 0; i < g->ntargets; i++) {
        const Target *t = &g->targets[i];
        if (!t->alive || t->team == p->team) continue;
        float d = v3_dist(t->pos, p->pos);
        if (d < bd) { bd = d; best = i; }
      }
      ai->gt = best;
    }
    if (ai->gt >= 0) {
      const Target *t = &g->targets[ai->gt];
      float d = v3_dist(t->pos, p->pos);
      goal = d > 2600 ? v3(t->pos.x, t->ground + 900, t->pos.z) : v3_add(t->pos, v3(0, 6, 0));
      minAlt = d < 2600 ? 120 : 220;
      thr = d < 2600 ? 0.6f : 1;
      if (angle_to(goal, p) < 0.05f && d < 1200) gun = true;
      if (p->locked && ref_eq(p->lockTarget, ref_ground(ai->gt)) && ai->mslCool <= 0) {
        msl = true;
        ai->mslCool = D->mslGap * 0.6f;
      }
    }
  }

  bool vec = false, direct = false;
  float dp = 0, dr = 0;
  if (p->drone && !(ai->refuel && !(e && e->alive && eDist < 1200)))
    drone_moves(g, p, e, eDist, threat ? td : 1e9f, dt, &goal, &thr, &gun, &hard, &vec, &direct, &dp, &dr);

  /* never fly into anyone, a wingman included: too close, or paths that
   * pass within 120 m in the next three seconds, turn away from where the
   * other will be (wingmen off the same runway fly the same line) */
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q == p || !q->alive || q->inactive) continue;
    vec3 r = v3_sub(q->pos, p->pos), w = v3_sub(q->vel, p->vel);
    float rl = v3_len(r);
    if (rl > 1500) continue;
    float ww = v3_dot(w, w), tc = ww > 1 ? -v3_dot(r, w) / ww : -1;
    vec3 miss = tc > 0 && tc < 3 ? v3_add(r, v3_scale(w, tc)) : r;
    if (rl < 60 || (tc > 0 && tc < 3 && v3_len(miss) < 120)) {
      vec3 away = v3_len(miss) > 5 ? v3_norm(miss) : (i < p->idx ? p->r : v3_scale(p->r, -1));
      goal = v3_add(p->pos, v3_sub(v3_scale(p->f, 300), v3_scale(away, 700)));
      hard = true;
      vec = direct = false;
      break;
    }
  }

  /* terrain and ground avoidance */
  /* look further ahead the faster and the lower on energy we are: a slow
   * jet cannot pull out as hard (the wing's lift grows with speed squared) */
  float ahead = 2.4f + (p->speed < 150 ? 1.5f : 0) + fmaxf(0, -p->f.y) * 2;
  if (g->realistic) ahead *= 1.8f; /* half the arcade's pull, and gravity in the path: start the pull-out sooner */
  vec3 look = v3_add(p->pos, v3_scale(p->vel, ahead));
  float here = terrain_h(p->pos.x, p->pos.z);
  float gh = fmaxf(fmaxf(terrain_h(look.x, look.z), here), terrain_h((p->pos.x + look.x) / 2, (p->pos.z + look.z) / 2));
  bool low = look.y < gh + minAlt || p->pos.y < here + minAlt * 0.5f;
  /* realistic: the hills between, along the whole path ahead */
  for (int k = 1; g->realistic && k <= 8 && !low; k++) {
    vec3 q = v3_add(p->pos, v3_scale(p->vel, ahead * (float)k / 8));
    low = q.y < terrain_h(q.x, q.z) + minAlt;
  }
  if (low) {
    vec3 fh = v3_norm(v3(p->f.x, 0, p->f.z));
    goal = v3_add(p->pos, v3_add(v3_scale(fh, 600), v3(0, 700, 0)));
    thr = 1;
    hard = p->f.y < 0; /* nose down toward the ground: roll level and pull */
    if (p->drone && ai->bfm >= BFM_COBRA) ai->bfm = BFM_PURSUIT;
    vec = direct = false;
  }
  /* the drone, slow and low: hang on the nozzle, nose up, until the speed is back */
  if (p->drone && p->speed < 95 && p->pos.y - here < 1500) {
    vec3 fh = v3_len(v3(p->f.x, 0, p->f.z)) > 0.1f ? v3_norm(v3(p->f.x, 0, p->f.z)) : v3_norm(v3(p->u.x, 0, p->u.z));
    goal = v3_add(p->pos, v3_add(v3_scale(fh, 600), v3(0, 500, 0)));
    thr = 1;
    hard = true;
    vec = true;
    direct = false;
    if (ai->bfm >= BFM_COBRA) ai->bfm = BFM_PURSUIT;
  }
  if (fabsf(p->pos.x) > ARENA_X - 600 || fabsf(p->pos.z) > ARENA_Z - 600) goal = v3(0, fmaxf(800, p->pos.y), 0);

  vec3 d = v3_norm(v3_sub(goal, p->pos));
  float lx = v3_dot(d, p->r), ly = v3_dot(d, p->u), lz = v3_dot(d, p->f);
  float off = acosf(clampf(lz, -1, 1));
  float rollErr = atan2f(lx, ly);
  float bank = atan2f(-p->r.y, p->u.y);
  float roll = off < 0.12f ? -bank * 1.5f + lx * 6 : rollErr * 2.2f;
  float pitch = (fabsf(rollErr) < 0.9f || off < 0.12f) ? clampf(atan2f(ly, lz) * 3, -0.5f, 1) : 0.2f;
  /* a break or a hard turn: roll the lift onto the goal and pull all the way */
  if (hard && off > 0.2f) pitch = fabsf(rollErr) < 0.6f ? 1 : 0.6f;
  roll = clampf(roll, -1, 1) * D->maxIn;
  pitch = clampf(pitch, -1, 1) * D->maxIn;
  /* slow and nose low: pulling only stalls the wing; let the nose fall and
   * the speed come back first, then pull out */
  if (p->speed < (g->classic ? 72 : 90) * 1.25f && p->f.y < -0.2f && p->pos.y - here > (g->realistic ? 900 : 250) && !vec)
    pitch = fminf(pitch, 0.1f);
  /* realistic: near the stall the wing has little to give; ease the pull
   * and let the speed build rather than stall it into the ground */
  if (g->realistic && !p->drone && p->speed < (g->classic ? 72 : 90) * 1.35f) {
    pitch = fminf(pitch, p->f.y < 0 ? 0.6f : 0.25f);
    thr = 1;
  }
  if (direct) {
    pitch = dp;
    roll = dr;
  }
  /* a CPU pilot feels the G too, and eases off before blacking out */
  if (p->gstress > 2.5f && pitch > 0.5f) pitch = 0.5f;
  c->pitch += (pitch - c->pitch) * fminf(1, dt * 6);
  c->roll += (roll - c->roll) * fminf(1, dt * 7);
  p->throttle += clampf(thr - p->throttle, -0.8f * dt, 0.8f * dt);
  c->gun = gun;
  c->msl = msl;
  c->flr = flr;
  c->vec = vec;
}
