/* ai.c - CPU pilots with easy/normal/hard tables, DIFF and aiControl (JS lines 2890-2974). */
#include "core/ai.h"

#include <string.h>

#include "core/fmath.h"

#include "core/defend.h"
#include "core/sim.h"
#include "core/jets.h"
#include "core/world.h"

const char *const BFM_NAMES[BFM_COUNT] = { "PURE PURSUIT", "LEAD PURSUIT", "LAG PURSUIT", "HIGH YO-YO", "LOW YO-YO",
                                           "PERCH",        "BREAK",        "GUNS JINK",   "EXTENSION",  "REVERSAL",
                                           "ONE CIRCLE",   "TWO CIRCLE",   "SEPARATION",  "UNLOAD",     "TOP COVER",
                                           "BAIT",         "MISSILE BEAM", "SEARCH",      "HOME CAP",     "STRIKE",
                                           "HOMEWARD",
                                           "COBRA",        "FLIP",         "J-TURN",      "NOSE POINT", "VECTOR ROLL" };

const DiffTable DIFF[3] = {
  { 0.62f, 1.0f, 0.06f, 15, 0.35f, 0.07f, 0.5f, 5000 },
  { 0.85f, 0.55f, 0.045f, 9, 0.65f, 0.035f, 0.85f, 6500 },
  { 1.0f, 0.25f, 0.035f, 5, 0.9f, 0.012f, 1.0f, 9000 },
};

static float angle_to(vec3 goal, const Plane *p) {
  return fm_acosf(clampf(v3_dot(v3_norm(v3_sub(goal, p->pos)), p->f), -1, 1));
}

/* ---- the doctrine ---------------------------------------------------------
 * Each jet fights the way its design suits (its fighting style), from what its
 * pilot has seen rather than from where every jet is, and to rules that come
 * from the basic fighter manoeuvres: no long straight and level line with an
 * enemy about, a missile turned to the beam with decoys, the one-circle or the
 * two-circle fight by which one the jet turns better, a jet without a gun
 * never merging, hit and leave, the perch on the sun's side, a wingman on top
 * cover, and the bait. Easy pilots use little of it, aces all of it. */
enum { ST_SHOOTER, ST_TURNER, ST_ENERGY, ST_AMBUSH };
static bool jet_is(int jet, const char *id) { return !strcmp(JETS[jet].id, id); }
/* shooters: sensors and missiles, avoid the slow turning fight; turners: take
 * the close fight; energy: stay fast, the rate fight, never slow and nose high;
 * ambush: one fast pass, then away */
static int style_of(int jet) {
  if (jet_is(jet, "af40") || jet_is(jet, "sy26") || jet_is(jet, "nf24")) return ST_SHOOTER;
  if (jet_is(jet, "lf27")) return ST_ENERGY;
  if (jet_is(jet, "ja46")) return ST_AMBUSH;
  return ST_TURNER; /* vk33, ks55, ecf90, the drone and any other */
}
/* hit and leave (aces): away after a pass or a missile shot, back from a new direction */
static bool hit_and_leave(int jet) { return jet_is(jet, "ja46") || jet_is(jet, "af40") || jet_is(jet, "sy26"); }

/* Seeing the enemy: on radar inside 60 degrees of the nose, out to 16 km on a
 * big jet and less the stealthier it is (its stealth score: under 7 km at the
 * top), or by eye to 5 km, half that in the rear quarter. A pilot looks round
 * every so often (easy pilots seldom) and remembers a contact for a while
 * after losing it, flying at where it should be by now. */
static const float LOOK[3] = { 2.5f, 1.2f, 0.6f }, MEMORY[3] = { 4, 8, 12 };
#define EYE_R      5000.0f
#define RADAR_R    16000.0f
#define RADAR_CONE 1.05f
static float radar_range(const Plane *q) { return RADAR_R * clampf(1.15f - 0.075f * JETS[q->jet].stealth, 0.3f, 1); }
static bool can_see(const Plane *p, const Plane *q) {
  vec3 d = v3_sub(q->pos, p->pos);
  float dl = v3_len(d);
  if (dl < 1) return true;
  float a = fm_acosf(clampf(v3_dot(d, p->f) / dl, -1, 1));
  if (a < RADAR_CONE && dl < radar_range(q)) return true;
  return dl < (a > 2.0f ? EYE_R * 0.5f : EYE_R);
}

/* the other CPU pilot of our side, if there is one */
static const Plane *wingmate(const Game *g, const Plane *p) {
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q != p && q->team == p->team && q->is_ai && q->alive && !q->inactive && !q->drone) return q;
  }
  return NULL;
}

/* A look round: the nearest contact in sight, the one we have first, and (a
 * wingman's split) not the one our wingman is on if there is another. */
static void look_round(const Game *g, Plane *p) {
  AiPilot *ai = &p->ai;
  const Plane *mate = ai->diff > DIFF_EASY ? wingmate(g, p) : NULL;
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q->team == p->team || !q->alive || q->inactive) continue;
    if (!can_see(p, q) && !(p->lastHitBy == i && g->time - p->lastHitT < 3)) continue; /* or it just shot at us */
    float d = v3_dist(q->pos, p->pos) - (ai->seen == i + 1 ? 1500 : 0) + (mate && mate->ai.seen == i + 1 ? 4000 : 0);
    if (d < bd) { bd = d; best = i; }
  }
  if (best < 0) return;
  ai->seen = best + 1;
  ai->seenAge = 0;
}

/* The one-circle or the two-circle fight, whichever favours our jet: the
 * tighter circle (radius, which below corner speed is the 1 g speed squared
 * over g) wins the one-circle, slow, nose to nose; the quicker turn (the most g
 * at corner speed, and the thrust to hold it) the two-circle, fast, nose to
 * tail. */
static int circle_fight(const Game *g, const Plane *p, const Plane *e) {
  const JetType *a = jet_type(p->jet, g->realistic), *b = jet_type(e->jet, g->realistic);
  float ca = jet_corner(a), cb = jet_corner(b), va = ca / sqrtf(a->g_max), vb = cb / sqrtf(b->g_max);
  float radius = (vb * vb) / (va * va);                                              /* his circle over ours */
  float rate = (a->g_max / ca * sqrtf(a->climb)) / (b->g_max / cb * sqrtf(b->climb)); /* our turn over his */
  return radius > rate && radius > 1.02f ? BFM_ONE_CIRCLE : BFM_TWO_CIRCLE;
}

/* Away from him after a pass, a missile shot or to reset, not straight back
 * down the line we came in on but turned off it, so the next attack comes
 * from somewhere new; against a short-legged jet (vk33), away from its base,
 * so the fight drags out where its fuel runs short. */
static void start_leave(const Game *g, Plane *p, const Plane *e, float t) {
  AiPilot *ai = &p->ai;
  vec3 away = v3(p->pos.x - e->pos.x, 0, p->pos.z - e->pos.z);
  if (v3_len(away) < 1) away = v3(p->f.x, 0, p->f.z);
  away = v3_norm(away);
  vec3 side = v3_cross(v3(0, 1, 0), away);
  if (v3_dot(side, p->f) < 0) side = v3_scale(side, -1); /* the short way round */
  ai->leaveDir = v3_norm(v3_add(away, v3_scale(side, 0.6f)));
  if (jet_is(e->jet, "vk33")) {
    const Base *b = &BASES[e->team];
    vec3 out = v3(p->pos.x - b->x, 0, p->pos.z - b->z);
    if (v3_len(out) > 1) ai->leaveDir = v3_norm(v3_add(ai->leaveDir, v3_norm(out)));
  }
  ai->axis = v3_norm(v3_sub(e->pos, p->pos));
  ai->leaveT = t;
  ai->passing = false;
}

/* Dogfighting, from the basic fighter manoeuvres and the old rules of air
 * combat: see the enemy first, attack from above and out of the sun, fly the
 * pursuit curve that fits the range, stay near corner speed, use the vertical
 * (yo-yos) rather than overshoot, hold fire until close and shoot in short
 * bursts; when attacked, break into the attacker, jink against a gun
 * solution, and extend away when out of energy. On top of that the jet's
 * fighting style (above). Easy pilots only chase; normal ones also break,
 * yo-yo, lag, fly their style and pick the circle; aces use everything. */
#define RADAR_FROM 2500 /* m: beyond it the radar missile, closer the heat-seeker */
#define STRIKE_AGL 420  /* m: the air-to-ground run, under the missile's 610 m ceiling */
#define STRIKE_SPD 230  /* m/s: and under its 257 m/s limit */
#define DECK_TD    0.22f /* the touchdown point on a carrier: this share of its length from the stern */
#define DECK_GS    0.0612f /* the glide slope to it, 3.5 degrees */
#define DECK_GATE  3200  /* m astern: the gate the final approach starts from */
#define SHOT_SPEED 520   /* m/s: a missile's mean speed over a shot, from the launch to its top speed */
#define SHOT_LIFE  0.7f  /* the share of its flight time a shot may need to reach him */
#define SHOT_LIFE_RADAR 0.55f /* the same for a radar missile */
#define SHOT_LOCK_T 0.6f /* s: the lock held before a shot */
#define SHOT_DECOY_R 600 /* m: a decoy this near him draws the missile off */
/* m: the ground kept clear on the circuit to the gate. The gate is 190-260 m up; at the 220 m kept clear
 * elsewhere, looking 7 s ahead in realistic handling, every sink toward the gate was a pull-up at full power to
 * 700 m above, round and round, and a fifth of the trips home ran dry */
#define CIRCUIT_MIN_ALT 120
#define HOME_MAX_THR 0.9f /* military power: the afterburner starts above it (game_burn) */
/* m/s: realistic, lined up on the way in, slowed to this with the gear and flaps out, so the slope from the gate
 * ends at landing speed (at 135, clean, the jets reached the far end of the runway at 110 still flying, and round
 * again); over the near-stall rule's 1.35 times 90 below, which goes to full power */
#define CIRCUIT_SPD 128
#define ACE_FLARE_P   0.35f /* an ace's chance of flares at each look, a heat-seeker on the beam (chaff: DIFF's 0.9) */
#define ACE_FLARE_GAP 1.2f  /* s between them (chaff: 0.7) */

/* Whether a missile of this kind fired now would reach and hit: inside its flight time (the time to catch a
 * target running from it), the lock held long enough to be good, no missile of ours already on the way to him,
 * and no decoy of the kind that fools it (chaff for a radar missile, flares for a heat-seeker) in the air around
 * him. A shot outside this is a missile thrown away. */
/* Easy and normal pilots shoot their radar missiles with less discipline than an ace: longer shots, out to the
 * missile's whole flight time and past it (easy), so fewer hit, on a lock held longer before they trust it (they
 * are slower to react). With an ace's envelope they never found a radar shot at all: they fight from behind, where
 * a jet running from the missile takes it 9-15 s to catch, and the window between RADAR_FROM and the envelope's
 * end is a few dozen metres; an ace meets them head-on, where the radar missile comes first anyway. */
static const float RADAR_LIFE[3] = { 1.25f, 1.0f, SHOT_LIFE_RADAR }, RADAR_LOCK_T[3] = { 1.5f, 1.0f, SHOT_LOCK_T };
static const float RADAR_FIRST[3] = { 1500, 1800, RADAR_FROM }; /* m: the radar missile first from here, in a chase */

static bool shot_good(const Game *g, const Plane *p, const Plane *e, float dist, vec3 los, int kind) {
  float life = g->classic ? (kind == MK_RADAR ? 26 : 15) : 9;
  float recede = v3_dot(e->vel, los);                       /* m/s he runs from it along the line of sight */
  float tof = dist / fmaxf(80, SHOT_SPEED - recede);
  int sk = p->ai.diff < 0 ? 0 : p->ai.diff > 2 ? 2 : p->ai.diff;
  /* a radar missile is guided only while we hold the lock on him: shorter shots, less time pointing at him */
  if (kind == MK_RADAR ? (tof > life * RADAR_LIFE[sk] || p->lockT < RADAR_LOCK_T[sk])
                       : (tof > life * SHOT_LIFE || p->lockT < SHOT_LOCK_T))
    return false;
  for (int i = 0; i < g->nmissiles; i++) {
    const Missile *m = &g->missiles[i];
    if (m->owner == p->idx && m->target.kind == REF_PLANE && m->target.idx == e->idx && m->life > 1) return false;
  }
  for (int i = 0; i < MAX_FLARES; i++) {
    const Flare *f = &g->flares[i];
    if (f->live && f->chaff == (kind == MK_RADAR) && v3_dist(f->pos, e->pos) < SHOT_DECOY_R) return false;
  }
  return true;
}

void (*ai_gear_flaps)(Plane *p, bool gear_down, float flaps);

/* Where to land: the team's own carrier when it is nearer than its runway, else the runway (0) */
static int home_site(const Plane *p) {
  const Base *b = &BASES[p->team];
  float best = v3_dist(p->pos, v3(b->x, p->pos.y, b->z));
  int site = 0;
  for (int i = 0; MAP && i < MAP->ncarriers; i++) {
    const Carrier *c = &MAP->carriers[i];
    float d = v3_dist(p->pos, v3(c->x, p->pos.y, c->z));
    if (c->team == p->team && d < best) best = d, site = i + 1;
  }
  return site;
}

/* A carrier's frame: its bow (cf), its starboard (cr), the touchdown point near the stern (deck height + 3) */
static void deck_frame(const Carrier *c, vec3 *cf, vec3 *cr, vec3 *td) {
  float h = c->heading * PI_F / 180;
  *cf = v3(fm_sinf(h), 0, fm_cosf(h));
  *cr = v3(fm_cosf(h), 0, -fm_sinf(h));
  *td = v3_add(v3(c->x, terrain_h(c->x, c->z) + 3, c->z), v3_scale(*cf, -c->length * (0.5f - DECK_TD)));
}

static void dogfight(Game *g, Plane *p, const Plane *e, float dist, float dt, vec3 *goal, float *thr, bool *gun, bool *msl,
                     bool *hard) {
  AiPilot *ai = &p->ai;
  const DiffTable *D = &DIFF[ai->diff];
  const JetType *jt = jet_type(p->jet, g->realistic);
  vec3 los = v3_scale(v3_sub(e->pos, p->pos), 1 / fmaxf(dist, 1));
  float ata = fm_acosf(clampf(v3_dot(p->f, los), -1, 1));                  /* my nose off the enemy */
  float aot = fm_acosf(clampf(v3_dot(e->f, los), -1, 1));                  /* off his tail: 0 = right behind him */
  float his_ata = fm_acosf(clampf(-v3_dot(e->f, los), -1, 1));             /* his nose off me */
  float closure = v3_dot(v3_sub(p->vel, e->vel), los);                  /* m/s, positive closing */
  float vc = jet_corner(jt);
  float agl = p->pos.y - terrain_h(p->pos.x, p->pos.z);
  float myE = p->pos.y + p->speed * p->speed / 19.62f, hisE = e->pos.y + e->speed * e->speed / 19.62f;
  bool behind_me = v3_dot(p->f, los) < 0;                                /* he is past my 3-9 line */
  bool threatened = his_ata < 0.5f && dist < 2600 && behind_me;
  int skill = ai->diff; /* 0 easy, 1 normal, 2 ace */
  int style = skill > 0 ? style_of(p->jet) : ST_TURNER;
  bool noGun = JETS[p->jet].nogun && !p->drone;
  bool dry = p->missiles + p->radar == 0;
  /* an ace with the gun: with the missiles gone, or close, it takes the close fight to the gun (a jet's own style
   * would leave, and come back for a missile it may not have); never against the drone, which out-turns it */
  bool guns = skill == 2 && !noGun && !e->drone && p->ammo >= 60 && (dry || dist < 2500);
  if (guns) style = ST_TURNER;
  bool offensive = aot < 0.8f && ata < 0.7f; /* behind him, nose on */
  /* no gun, or a shooter with no missiles left: never into the merge, away and back for a shot from range */
  bool noMerge = skill > 0 && (noGun || (style == ST_SHOOTER && dry));
  int want;

  /* the pass: inside 900 m, then opening again; an ambusher (and an ace that hits and leaves) is gone */
  if (dist < 900) ai->passing = true;
  else if (ai->passing && dist > 1100) {
    ai->passing = false;
    if (ai->leaveT <= 0 && !offensive && (style == ST_AMBUSH || (skill == 2 && hit_and_leave(p->jet) && !guns))) start_leave(g, p, e, 12);
  }
  if (ai->leaveT > 0) {
    ai->leaveT -= dt;
    if (dist > 6000 || ai->leaveT <= 0) { /* out far enough: round again, from a new direction (aces) */
      ai->leaveT = 0;
      ai->reattT = skill == 2 ? 25 : 0;
    }
  }
  if (ai->reattT > 0) ai->reattT -= dt;

  if (threatened && skill > 0) {
    if (dist < 650 && his_ata < 0.15f) want = BFM_JINK;                    /* he has a gun solution */
    else if (p->speed < vc * 0.55f && dist > 1200 && myE < hisE && agl > 1500)
      want = BFM_EXTEND; /* no energy left to turn: dive away, with height to do it */
    else want = BFM_BREAK;
    ai->leaveT = 0;
  } else if (ai->leaveT > 0) {
    want = BFM_SEPARATE;
  } else if (noMerge && dist < 2500 && !offensive) {
    start_leave(g, p, e, 10);
    want = BFM_SEPARATE;
  } else if (skill > 0 && dist < 450 && closure > 60 && aot > 1.4f && !behind_me) {
    want = BFM_REVERSE; /* he overshot, or we are about to: turn back across his path */
  } else if (skill > 0 && (ai->bfm == BFM_ONE_CIRCLE || ai->bfm == BFM_TWO_CIRCLE) && ai->bfmT < 6 && ata > 0.8f) {
    want = ai->bfm; /* the circle chosen at the merge, flown round until the nose comes back onto him */
  } else if (skill > 0 && dist < 800 && aot > 2.2f && closure > 80) {
    /* the merge, nose to nose: the circle that suits the jet; an energy fighter (and a shooter, which
     * must not get slow) always the two-circle */
    want = style == ST_TURNER ? circle_fight(g, p, e) : BFM_TWO_CIRCLE;
  } else if (style == ST_SHOOTER && dist < 1500 && !offensive && p->speed < vc * 0.8f) {
    start_leave(g, p, e, 8); /* a shooter in a slow turning fight: out to missile range */
    want = BFM_SEPARATE;
  } else if (style == ST_ENERGY && p->speed < vc * 0.75f && p->f.y > 0.3f) {
    want = BFM_UNLOAD; /* slow and nose high is where an energy fighter dies: unload for speed */
  } else if (style == ST_ENERGY && dist < 2000 && ata > 1.2f && !offensive) {
    if (p->speed > vc) want = BFM_HIGH_YOYO; /* out-turned: up into the vertical, not round the circle */
    else {
      start_leave(g, p, e, 8);
      want = BFM_SEPARATE;
    }
  } else if ((skill == 2 || style == ST_ENERGY) && dist > 3000 && e->pos.y + 700 > p->pos.y && !threatened) {
    want = BFM_PERCH; /* get above him first, out of the sun */
  } else if (dist > 1600) {
    want = BFM_PURSUIT; /* nose on him: missile range */
  } else if (skill > 0 && closure > 110 && aot > 0.6f) {
    want = BFM_HIGH_YOYO; /* closing too fast at an angle: use the vertical, don't overshoot */
  } else if (skill > 0 && ata > 1.0f && p->speed > e->speed * 0.8f && agl > 1500 && style != ST_ENERGY) {
    want = BFM_LOW_YOYO; /* he out-turns us: cut across below his circle */
  } else if (skill > 0 && closure > 70 && dist < 700) {
    want = BFM_LAG; /* hold the range from inside his turn, keep the energy */
  } else want = BFM_LEAD;
  /* hold a manoeuvre for a moment rather than switch every frame */
  if (want != ai->bfm && (ai->bfmT > 0.8f || want == BFM_BREAK || want == BFM_JINK || want == BFM_SEPARATE)) {
    ai->bfm = want;
    ai->bfmT = 0;
  }
  ai->bfmT += dt;

  float tLead = dist / (1150 + p->speed) * D->lead;
  vec3 lead = v3_add(v3_add(e->pos, v3_scale(e->vel, tLead)), v3_scale(ai->jit, dist * D->aimErr));
  /* a turning fight is won at corner speed; an energy fighter stays fast */
  *thr = p->speed > vc * (style == ST_ENERGY ? 1.35f : 1.15f) ? 0.35f : 1;
  switch (ai->bfm) {
  case BFM_PERCH: {
    vec3 at = v3(0, 900, 0);
    if (skill == 2 && LIGHT.y > 0.05f) { /* on the sun's side of him, so he looks into it */
      vec3 sun = v3(LIGHT.x, 0, LIGHT.z);
      if (v3_len(sun) > 0.05f) at = v3_add(at, v3_scale(v3_norm(sun), 1200));
    }
    if (ai->reattT > 0) { /* back from a new direction: off the line of the last attack */
      vec3 off = v3_cross(v3(0, 1, 0), ai->axis);
      if (v3_len(off) > 0.1f) {
        off = v3_norm(off);
        if (v3_dot(off, v3_sub(p->pos, e->pos)) < 0) off = v3_scale(off, -1);
        at = v3_add(at, v3_scale(off, 1500));
      }
    }
    if (skill > 0 && jet_is(e->jet, "vk33")) { /* the short-legged jet: perch on the side away from its base */
      vec3 out = v3(e->pos.x - BASES[e->team].x, 0, e->pos.z - BASES[e->team].z);
      if (v3_len(out) > 1) at = v3_add(at, v3_scale(v3_norm(out), 1500));
    }
    *goal = v3_add(e->pos, at);
    *thr = 1;
    break;
  }
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
    *goal = v3_add(p->pos, v3_scale(v3_add(side, v3(0, agl > 1200 ? -0.15f : 0.1f, 0)), 1000));
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
  case BFM_ONE_CIRCLE: case BFM_TWO_CIRCLE: {
    /* past him: the two-circle turns toward the side he went by (nose to tail, fast), the one-circle away
     * from it (nose to nose, slow, the tighter circle wins) */
    vec3 lat = v3_sub(los, v3_scale(p->f, v3_dot(los, p->f)));
    lat.y = 0;
    lat = v3_len(lat) > 0.01f ? v3_norm(lat) : v3_norm(v3(p->r.x, 0, p->r.z));
    if (ai->bfm == BFM_ONE_CIRCLE) lat = v3_scale(lat, -1);
    *goal = v3_add(p->pos, v3_scale(v3_add(v3_scale(lat, 1), v3_scale(p->f, 0.25f)), 1000));
    *hard = true;
    if (ai->bfm == BFM_ONE_CIRCLE) *thr = p->speed > vc * 0.85f ? 0.3f : 1;
    else *thr = 1;
    break;
  }
  case BFM_SEPARATE: /* away at full power, a little downhill for speed, never into the ground */
    *goal = v3_add(p->pos, v3_add(v3_scale(ai->leaveDir, 1500), v3(0, agl > 2500 ? -200 : agl < 1200 ? 150 : 0, 0)));
    *thr = 1;
    break;
  case BFM_UNLOAD: /* push the nose down to the horizon and let the speed build */
    *goal = v3_add(p->pos, v3_add(v3_scale(v3_norm(v3(p->f.x, 0, p->f.z)), 1000), v3(0, agl > 1500 ? -120 : 0, 0)));
    *thr = 1;
    break;
  default:
    *goal = lead;
  }
  /* and close behind him with the nose near, it tracks the lead point for the shot */
  if (guns && !threatened && dist < 1200 && angle_to(lead, p) < 0.35f) {
    *goal = lead;
    *hard = angle_to(lead, p) > 0.08f;
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
  /* guns: hold fire until close, then short bursts (a jet built without a gun has none to fire) */
  float gunR = skill == 0 ? 1300 : skill == 1 ? 900 : 1000;
  ai->burstT -= dt;
  if (ai->burstT < -0.5f) ai->burstT = 0.6f;
  if (!noGun && angle_to(lead, p) < D->gunCone && dist < gunR && ai->burstT > 0) *gun = true;
  if (p->locked && ref_eq(p->lockTarget, ref_plane(e->idx)) && ai->mslCool <= 0 && dist > 500 &&
      (skill < 2 || aot < 1.2f || style == ST_SHOOTER)) { /* an ace shoots from behind, where it hits; a shooter from anywhere */
    /* the kind: a radar missile from range or from in front of him (or with no heat-seekers left), a heat-seeker
     * in close and from behind; the other kind when the first would not hit and it would */
    bool radarFirst = p->radar > 0 && (dist > RADAR_FIRST[skill] || aot > 1.6f || p->missiles == 0);
    int first = radarFirst ? MK_RADAR : MK_HEAT, second = radarFirst ? MK_HEAT : MK_RADAR;
    bool have[3] = { [MK_HEAT] = p->missiles > 0, [MK_RADAR] = p->radar > 0 };
    int kind = have[first] && shot_good(g, p, e, dist, los, first) ? first
             : have[second] && shot_good(g, p, e, dist, los, second) ? second : -1;
    if (kind >= 0) {
      *msl = true;
      p->ctl.mslk = kind == MK_RADAR ? 1 : 0;
      ai->mslCool = D->mslGap + game_rand(g, 0, 3);
      /* a radar missile flies on the launcher's lock: stay on him until it gets there; a heat-seeker needs
       * nothing more, so a hit-and-leave ace turns away at once */
      if (kind == MK_RADAR) ai->supportT = dist / fmaxf(80, SHOT_SPEED - v3_dot(e->vel, los)) + 0.5f;
      else if (skill == 2 && hit_and_leave(p->jet) && ai->leaveT <= 0) start_leave(g, p, e, 10); /* fired: away, and back from elsewhere */
    }
  }
  if (ai->supportT > 0) {
    ai->supportT -= dt;
    if (!threatened) { /* the nose on him (ahead of him a little) while the radar missile flies */
      *goal = v3_add(e->pos, v3_scale(e->vel, 0.5f));
      ai->leaveT = 0;
    }
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
  ai->unloadT -= dt;
  bool moving = ai->bfm >= BFM_COBRA;
  if (!e || !e->alive) {
    if (moving) ai->bfm = BFM_PURSUIT;
    return false;
  }
  vec3 los = v3_scale(v3_sub(e->pos, p->pos), 1 / fmaxf(dist, 1));
  float ata = fm_acosf(clampf(v3_dot(p->f, los), -1, 1));
  float his_ata = fm_acosf(clampf(-v3_dot(e->f, los), -1, 1));
  float tLead = dist / (1150 + p->speed) * D->lead;
  vec3 lead = v3_add(e->pos, v3_scale(e->vel, tLead));
  /* the post-stall budget (normal and ace): one move at a time and only with energy to spare, 150 m/s and
   * 1200 m under it; after a Cobra, a flip or a J-turn the nose goes down and the speed comes back before the
   * next (the roll against a missile is always there) */
  bool budget = skill > 0;
  bool spare = !budget || (p->speed > 150 && agl > 1200 && ai->unloadT <= 0);
  if (!moving && budget && ai->unloadT > 0 && p->speed < 170 && missileD >= 1400) {
    vec3 fh = v3_len(v3(p->f.x, 0, p->f.z)) > 0.1f ? v3_norm(v3(p->f.x, 0, p->f.z)) : v3_norm(v3(p->u.x, 0, p->u.z));
    *goal = v3_add(p->pos, v3_add(v3_scale(fh, 1000), v3(0, agl > 1500 ? -150 : 0, 0)));
    *thr = 1;
    *hard = false;
    if (ai->bfm != BFM_UNLOAD) ai->bfm = BFM_UNLOAD, ai->bfmT = 0;
    return true;
  }
  if (!moving && ai->moveCool <= 0 && agl > 800) {
    static const float COOL[3] = { 14, 7, 3.5f };
    int want = -1;
    if (missileD < 1400) want = BFM_ROLL;
    else if (his_ata < 0.6f && dist < 1400 && ata > 1.9f && spare) /* on our tail */
      want = p->speed > 120 ? (game_rand(g, 0, 1) < 0.6f ? BFM_COBRA : BFM_FLIP) : BFM_JTURN;
    else if (ata > 2.0f && dist > 700 && dist < 4000 && spare) want = BFM_JTURN;
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
    if (fm_acosf(clampf(v3_dot(v3_norm(v3_sub(lead, p->pos)), p->f), -1, 1)) < D->gunCone * 1.5f && dist < 900) *gun = true;
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
    if (prev == BFM_COBRA || prev == BFM_FLIP || prev == BFM_JTURN) ai->unloadT = 6; /* then the unload */
    if ((prev == BFM_COBRA || prev == BFM_FLIP) && skill > 0 && dist < 1300 && ata < 1.3f) {
      ai->bfm = BFM_POINT;
      return true;
    }
    *vec = *direct = false;
  }
  return true;
}

/* The landing approach to a base: how far past the threshold to touch down, and how steep the glide path
 * (rise per metre), so that the path is 40 m clear of the ground under it from the gate in (a hill can stand
 * right before a runway); 4 degrees on open ground, never steeper than 8.5 (14 in arcade handling), never past the runway's middle. */
static void glide_path(const Base *b, float elev, bool realistic, float *td, float *slope) {
  /* steeper in arcade handling, whose drag holds the speed down a steep slope: a hill a kilometre before one
   * threshold, cleared at 7 degrees, floated every approach past mid-runway and round again until dry */
  float ms = realistic ? 0.15f : 0.25f;
  *slope = 0.07f;
  *td = 200;
  for (int k = 0; k <= 22; k++) { /* every 100 m from the threshold out to the gate, 2.2 km */
    float d = 100.0f * (float)k, h = terrain_h(b->x, b->z + b->dir * (1000 + d)) + 40 - elev;
    if (h > *slope * (d + *td)) *td = fminf(600, fmaxf(*td, h / ms - d));
    if (h > *slope * (d + *td)) *slope = fminf(ms, h / (d + *td));
    if (h > *slope * (d + *td)) *td = fminf(1000, h / *slope - d);
  }
}

/* toward a circuit's gate: with the gate behind, a level turn toward its side (steering straight at a point
 * behind pulled the jet up into a loop, a kilometre and more up at full power, and home ran dry) */
static vec3 circuit_goal(const Plane *p, vec3 gate) {
  vec3 to = v3(gate.x - p->pos.x, 0, gate.z - p->pos.z), fh = v3(p->f.x, 0, p->f.z);
  float d = v3_len(to), fl = v3_len(fh);
  if (d < 1 || fl < 0.1f || v3_dot(to, fh) / (d * fl) > 0.2f) return gate;
  fh = v3_scale(fh, 1 / fl);
  vec3 side = v3(-fh.z, 0, fh.x);
  if (v3_dot(side, to) < 0) side = v3_scale(side, -1);
  vec3 g = v3_add(p->pos, v3_add(v3_scale(side, 1500), v3_scale(fh, 600)));
  g.y = gate.y;
  return g;
}

static bool inbound(const Plane *p, vec3 dir);

/* the height that clears the ground on the way from here to a goal (sampled every 250 m, out to 6 km) by twice the
 * circuit's margin: steered level at a gate's 260 m, the jets met the classic arena's 1 km hill on the way and
 * pulled up too late */
static float route_floor(vec3 from, vec3 to) {
  vec3 d = v3_sub(v3(to.x, 0, to.z), v3(from.x, 0, from.z));
  float len = fminf(v3_len(d), 6000), top = -1e9f;
  if (len < 1) return top;
  d = v3_scale(v3_norm(d), 250);
  for (int k = 0; k * 250.0f <= len; k++) top = fmaxf(top, terrain_h(from.x + d.x * (float)k, from.z + d.z * (float)k));
  return top + 2 * CIRCUIT_MIN_ALT;
}

/* no enemy within 6 km of home: the short way in */
static bool field_clear(const Game *g, const Plane *p, vec3 home) {
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q->team != p->team && q->alive && !q->inactive && v3_dist(v3(q->pos.x, 0, q->pos.z), v3(home.x, 0, home.z)) < 6000) return false;
  }
  return true;
}

/* where along and beside a line in a jet is: u out beyond `at` (out its unit way out), lat to the side */
static void line_pos(const Plane *p, vec3 at, vec3 out, float *u, float *lat) {
  *u = (p->pos.x - at.x) * out.x + (p->pos.z - at.z) * out.z;
  *lat = (p->pos.x - at.x) * out.z - (p->pos.z - at.z) * out.x;
}

/* out beyond at along the line, within a cone about it */
static bool in_cone(const Plane *p, vec3 at, vec3 out) {
  float u, lat;
  line_pos(p, at, out, &u, &lat);
  return u > 1500 && fabsf(lat) < fminf(1000, u * 0.3f);
}

/* on the line in near the gate (a jet that only had to pass within so many metres of the gate itself orbited
 * it, 700 m out, until the tank ran dry) */
static bool at_gate(const Plane *p, vec3 gate, vec3 out, bool realistic) {
  float u, lat;
  line_pos(p, gate, out, &u, &lat);
  return u > -500 && u < 2000 && fabsf(lat) < (realistic ? 400 : 300);
}

/* to a circuit's gate along the line in: first to a point 4 km further out on the line, then (latched, as
 * steering at a point from the side orbited it kilometres wide until the tank ran dry) along the line toward
 * the gate, the aim point a lead ahead on it; well off the line or past the gate, out to the point again */
/* With the field clear (no enemy within 6 km of home), a jet already out on the line's side and heading in
 * joins the line where it is instead of flying out to the point first (a nearer point, 1.5 km, missed the turn
 * onto the line: the audit counted 120 more crashes and 240 more jets run dry) */
static vec3 approach_goal(const Plane *p, AiPilot *ai, vec3 gate, vec3 out, bool realistic, bool clear) {
  float u, lat, lead = realistic ? 2500 : 1500;
  line_pos(p, gate, out, &u, &lat);
  vec3 f = v3(gate.x + out.x * 4000, gate.y, gate.z + out.z * 4000);
  if (!ai->lined && v3_dist(v3(f.x, 0, f.z), v3(p->pos.x, 0, p->pos.z)) < (realistic ? 2000 : 1500)) ai->lined = true;
  if (!ai->lined && clear && u > 500 && fabsf(lat) < u * 0.7f && inbound(p, v3_scale(out, -1))) ai->lined = true;
  if (ai->lined && (fabsf(lat) > 3000 || u < -1000)) ai->lined = false;
  if (!ai->lined) return circuit_goal(p, f);
  float a = fmaxf(0, u - lead);
  return circuit_goal(p, v3(gate.x + out.x * a, gate.y, gate.z + out.z * a));
}

/* lined up for the way in: heading within about 45 degrees of it */
static bool inbound(const Plane *p, vec3 dir) {
  vec3 fh = v3(p->f.x, 0, p->f.z);
  float fl = v3_len(fh);
  return fl > 0.1f && v3_dot(fh, dir) / fl > 0.7f;
}

/* on a carrier's deck (it has no runway to turn round on) */
static bool onDeckNow(const Plane *p) { return MAP && map_carrier_at(MAP, p->pos.x, p->pos.z) >= 0; }

/* The runway busy rule. A runway as a strip: its middle at the surface's height, its unit way along, half its
 * length and its width; a carrier's deck is one too. A landing jet goes round while another jet is on the strip
 * (rolling, parked, taking off or low over it) or climbing out past one of its ends; a jet about to take off holds
 * on the brakes while another one is on short final for the strip or rolling on it toward it (a landing jet and a
 * departing one met head-on on the runway: the take-off runs the other way to the landing). Any jet counts, a
 * player's too: it only steers the CPU's own decisions. */
typedef struct {
  vec3  c, a;
  float half, beam;
  bool  runway; /* a runway (its jets wait at an end), not a deck (they wait on the catapults, beside a bolter's path) */
} Strip;
#define STRIP_LOW    30   /* m over the strip: on it, rolling, lifting off or about to touch down */
#define CLIMB_OUT_H  300  /* m: below it, past an end of the strip, a jet is still climbing out */
#define CLIMB_OUT_R  1000 /* m past the end */
#define FINAL_R      4000 /* m past the end: a jet on the line in, closing, is on short final */
#define FINAL_H      1000 /* m: and below it (a steep arcade glide path starts 800 m up) */
#define FINAL_SPD    170  /* m/s: and slower */
#define SAME_WAY_SPD 40   /* m/s: a jet in the air going the landing's way faster than this is no danger to it */
#define HOLD_END     150  /* m from a runway's end: a jet waiting there to take off is at the holding point */
#define BUSY_ZONE    600  /* m: the landing jet keeps off the busy strip from this far outside its gate in */

static Strip base_strip(int team) {
  const Base *b = &BASES[team];
  return (Strip){ v3(b->x, terrain_h(b->x, b->z), b->z), v3(0, 0, 1), 1000, 45, true };
}

static Strip deck_strip(const Carrier *cv) {
  vec3 cf, cr, tdp;
  deck_frame(cv, &cf, &cr, &tdp);
  return (Strip){ v3(cv->x, tdp.y - 3, cv->z), cf, cv->length / 2, cv->beam, false };
}

/* where a jet is: along the strip from its middle, beside it, over its surface; and its speed along */
static void strip_pos(const Strip *s, const Plane *q, float *along, float *lat, float *h, float *va) {
  float dx = q->pos.x - s->c.x, dz = q->pos.z - s->c.z;
  *along = dx * s->a.x + dz * s->a.z;
  *lat = dx * s->a.z - dz * s->a.x;
  *h = q->pos.y - s->c.y;
  *va = q->vel.x * s->a.x + q->vel.z * s->a.z;
}

static bool on_strip(const Strip *s, const Plane *q) {
  float along, lat, h, va;
  strip_pos(s, q, &along, &lat, &h, &va);
  return fabsf(along) < s->half + 60 && fabsf(lat) < s->beam / 2 + 40 && h < STRIP_LOW;
}

/* in the air past an end, low, flying away from the strip along its line */
static bool climbing_out(const Strip *s, const Plane *q) {
  float along, lat, h, va;
  strip_pos(s, q, &along, &lat, &h, &va);
  return !q->onGround && fabsf(along) >= s->half && fabsf(along) < s->half + CLIMB_OUT_R && fabsf(lat) < 400 &&
         h < CLIMB_OUT_H && along * va > 0;
}

/* in the air on the line in, slow and low enough to land, closing on the strip or over it */
static bool short_final(const Strip *s, const Plane *q) {
  float along, lat, h, va;
  strip_pos(s, q, &along, &lat, &h, &va);
  float hs = v3_len(v3(q->vel.x, 0, q->vel.z));
  return !q->onGround && fabsf(along) < s->half + FINAL_R && fabsf(lat) < 600 && h < FINAL_H && q->speed < FINAL_SPD &&
         fabsf(va) > 0.7f * hs && (fabsf(along) < s->half || along * va < 0);
}

/* a jet standing at a runway's holding point, waiting to take off, at the end the landing rolls toward (the roll-out
 * stops 100 m short of the end, the overrun rule): it holds for the landing (hold_for_strip), and the landing does
 * not wait for it */
static bool at_holding_point(const Strip *s, const Plane *q, float vp) {
  float along, lat, h, va;
  strip_pos(s, q, &along, &lat, &h, &va);
  return s->runway && q->taxi && q->speed < 3 && fabsf(along) > s->half - HOLD_END && along * vp > 0;
}

/* for a landing jet: another jet on the strip or climbing out from it; not one in the air going the same way along
 * it, fast (a jet landing ahead, or going round or off ahead), nor one waiting at the holding point at the far end
 * (sent round for those, the jets behind went round again and again and twice as many ran dry on the way home) */
static bool strip_busy(const Game *g, const Plane *p, const Strip *s) {
  float vp = p->vel.x * s->a.x + p->vel.z * s->a.z;
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q == p || !q->alive || q->inactive) continue;
    float vq = q->vel.x * s->a.x + q->vel.z * s->a.z;
    if (!q->onGround && vq * vp > 0 && q->speed > SAME_WAY_SPD) continue;
    if (at_holding_point(s, q, vp)) continue;
    if (on_strip(s, q) || climbing_out(s, q)) return true;
  }
  return false;
}

/* for a jet about to take off (on the brakes or the catapult, not rolling yet): hold while another jet is on
 * short final for its strip, rolling on it toward it (a wingman rolling alongside or away is no danger), or down
 * from a landing and still on it (rolling out or refuelling: it stops in the take-off's path) */
static bool hold_for_strip(const Game *g, const Plane *p) {
  if (!p->taxi || p->speed >= 3 || p->cat > 0) return false;
  int k = MAP ? map_carrier_at(MAP, p->pos.x, p->pos.z) : -1;
  Strip s = k >= 0 ? deck_strip(&MAP->carriers[k]) : base_strip(p->team);
  if (!on_strip(&s, p)) return false;
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q == p || !q->alive || q->inactive) continue;
    if (short_final(&s, q)) return true;
    if (on_strip(&s, q) && q->speed >= 3 && v3_dot(v3_sub(p->pos, q->pos), q->vel) > 0) return true;
    if (on_strip(&s, q) && q->onGround && !q->taxi) return true;
  }
  return false;
}

void ai_control(Game *g, Plane *p, float dt) {
  AiPilot *ai = &p->ai;
  const DiffTable *D = &DIFF[ai->diff];
  Controls *c = &p->ctl;
  int skill = ai->diff;
  vec3 goal = v3(0, 2000, 0);
  float thr = 1, minAlt = 220;
  bool gun = false, msl = false, flr = false, hard = false, landing = false, flare = false;
  c->mslk = 0; /* the key's own order, unless a shot below asks for a kind */
  c->tanker = false; /* pressed below on the tick it calls */
  float flareP = 0;

  ai->flareCool -= dt;
  ai->mslCool -= dt;
  ai->jitT -= dt;
  if (ai->jitT <= 0) {
    ai->jitT = 0.5f;
    ai->jit = game_rand3(g, -1, 1, -1, 1, -1, 1);
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

  /* the enemy it knows of: the contact seen at the last look round, or where a lost one should be by now;
   * a missile coming at us gives away where it was fired from */
  ai->seenAge += dt;
  ai->lookT -= dt;
  if (ai->lookT <= 0) {
    ai->lookT = LOOK[skill];
    look_round(g, p);
  }
  if (threat && threat->owner >= 0 && threat->owner < g->nplanes && !ai->seen && g->planes[threat->owner].alive) {
    ai->seen = threat->owner + 1;
    ai->seenAge = LOOK[skill];
    ai->seenPos = g->planes[threat->owner].pos;
    ai->seenVel = g->planes[threat->owner].vel;
  }
  Plane *e = NULL, ghost;
  if (ai->seen) {
    Plane *q = &g->planes[ai->seen - 1];
    if (!q->alive || q->inactive || q->team == p->team || ai->seenAge > MEMORY[skill]) ai->seen = 0;
    else if (ai->seenAge < LOOK[skill] + dt) { /* in sight: followed */
      e = q;
      ai->seenPos = q->pos;
      ai->seenVel = q->vel;
    } else { /* lost: flown at where it should be */
      ghost = *q;
      ghost.pos = v3_add(ai->seenPos, v3_scale(ai->seenVel, fminf(ai->seenAge, 4)));
      ghost.vel = ai->seenVel;
      ghost.speed = v3_len(ai->seenVel);
      if (ghost.speed > 1) ghost.f = v3_scale(ghost.vel, 1 / ghost.speed);
      e = &ghost;
    }
  }

  /* an ace guards its side's tanker: an enemy near it is the one to go for */
  const Tanker *OT = &g->tankers[p->team & 1], *ET = &g->tankers[!(p->team & 1)];
  bool otUp = OT->state == TK_INBOUND || OT->state == TK_STATION;
  if (skill == 2 && otUp && !ai->refuel) {
    float best = 6000;
    for (int i = 0; i < g->nplanes; i++) {
      Plane *q = &g->planes[i];
      float d = v3_dist(q->pos, OT->pos);
      if (q->team != p->team && q->alive && !q->inactive && d < best) best = d, e = q;
    }
    if (e && e != &ghost) ai->seen = e->idx + 1, ai->seenAge = 0, ai->seenPos = e->pos, ai->seenVel = e->vel;
  }

  /* home to refuel with enough left to get there: the base can be the far
   * side of the map, and full power burns a tank in about two minutes */
  float need = game_bingo(g, p);
  {
    /* home for fuel, or to rearm with no air-to-air missile of either kind left and the gun nearly empty */
    /* a CPU pilot keeps a fifth more than the bingo figure (a half more in realistic handling, which burns
     * more on the way), and a margin of 20 s at a third of the power; never above 80. An ace flies to the bingo
     * figure itself (it has the circuit in it, and a margin): with the reserve on top, aces spent half their time
     * going home, and the extra reserve was not what ran jets dry (the tanker was, below). (The bingo figure has
     * the circuit in it already: a second whole minute sent arcade jets home at 80 %, 20 s after take-off,
     * five trips a match and a quarter of the matches into sudden death.) */
    float circuit = 20 * game_burn(g, p, 0.3f);
    if (!ai->refuel && (p->fuel < fmaxf(22, fminf(80, need * (skill == 2 ? 1.0f : g->realistic ? 1.5f : 1.2f) + circuit)) || (p->missiles + p->radar == 0 && p->ammo < 60)) &&
        g->time < TANKER_STAYS) {
      ai->refuel = true;
      ai->final_leg = false;
      ai->site = home_site(p);
      ai->missed = 0;
      ai->lined = false;
    }
  }
  /* the tanker call, once a match: home for fuel without enough to get there, or home far (12 km and more);
   * an ace calls it only with no enemy near where it will orbit */
  float homeD = ai->site > 0 && MAP && ai->site <= MAP->ncarriers
                    ? v3_dist(p->pos, v3(MAP->carriers[ai->site - 1].x, p->pos.y, MAP->carriers[ai->site - 1].z))
                    : v3_dist(p->pos, v3(BASES[p->team].x, p->pos.y, BASES[p->team].z));
  if (ai->refuel && !p->tcalled && !otUp && g->time < TANKER_STAYS && !p->onGround && !p->taxi && !p->drone &&
      (p->fuel < need || homeD > 12000)) {
    bool safe = true;
    for (int i = 0; skill == 2 && i < g->nplanes; i++) {
      const Plane *q = &g->planes[i];
      if (q->team != p->team && q->alive && v3_dist(q->pos, v3(OT->cx, OT->alt, OT->cz)) < 6000) safe = false;
    }
    if (safe) c->tanker = true;
  }
  /* full again: fuel, and the missiles it carries (a loadout can have fewer than four); or sudden death,
   * when nothing more is coming, so back into the fight with what it has */
  int full[4];
  game_full_load(g, p, full);
  bool loaded = p->missiles >= full[0] && p->radar >= full[1] && p->ground >= full[2]; /* all of it, not four */
  bool done = ai->refuel && ((p->fuel > 94 && loaded) || g->time >= TANKER_STAYS);
  bool on_deck = done && p->onGround && !p->taxi && ai->site > 0 && onDeckNow(p);
  /* the carrier it stands on (onDeckNow takes any), not the one it set out for when it turned home */
  const Carrier *deck = on_deck ? &MAP->carriers[map_carrier_at(MAP, p->pos.x, p->pos.z)] : NULL;
  vec3 cat_pos = v3(0, 0, 0);
  /* full on the deck: one of the two bow catapults, 8 m either side of the centre line, that no other jet is on
   * (a player starts on one); with both taken it waits on the deck, still refuelling */
  bool cat_free = false;
  if (on_deck) {
    const Carrier *cv = deck;
    vec3 cf, cr, tdp;
    deck_frame(cv, &cf, &cr, &tdp);
    for (int side = 0; side < 2 && !cat_free; side++) {
      vec3 spot = v3_add(v3(cv->x, 0, cv->z), v3_add(v3_scale(cf, cv->length * 0.12f), v3_scale(cr, side ? -8.0f : 8.0f)));
      cat_pos = v3(spot.x, terrain_h(spot.x, spot.z) + 3, spot.z);
      cat_free = true;
      for (int i = 0; i < g->nplanes; i++) {
        const Plane *q = &g->planes[i];
        if (q != p && q->alive && !q->inactive && v3_dist(q->pos, cat_pos) < 15) cat_free = false;
      }
    }
  }
  /* full on the runway but still rolling, with too little runway ahead: the turn round waits until it has stopped
   * (turned round at 90 m/s on the roll-out, a jet took off head-on into the one landing behind it) */
  if (done && (!on_deck || cat_free)) {
    ai->refuel = false;
    ai->final_leg = false;
    /* full on the runway: wings level and the take-off run from here, as from a ground start; with less
     * runway ahead than a take-off needs, turned round first (taxied back) - the roll-out ends near the far
     * end, and the run from there went off the runway into the ground */
    if (on_deck) {
      /* to the free catapult (the deck crew taxis it there) and off as at the start */
      const Carrier *cv = deck;
      vec3 cf, cr, tdp;
      deck_frame(cv, &cf, &cr, &tdp);
      p->pos = cat_pos;
      p->f = cf;
      p->u = v3(0, 1, 0);
      p->r = v3_cross(p->u, p->f);
      p->speed = 0;
      p->throttle = 0;
      p->taxi = p->oncat = true;
      p->cat = 0;
    } else if (p->onGround && !p->taxi) {
      const Base *b = &BASES[p->team];
      p->u = v3(0, 1, 0);
      p->f = v3_norm(v3(p->f.x, 0, p->f.z));
      float ahead = 1000 - (p->pos.z - b->z) * (p->f.z > 0 ? 1 : -1);
      if (ahead < 1100 && !onDeckNow(p)) p->f = v3_scale(p->f, -1);
      p->r = v3_cross(p->u, p->f);
      p->taxi = true;
    }
  }
  float eDist = e && e->alive ? v3_dist(e->pos, p->pos) : 1e9f;
  bool eThreat = e && eDist < 2600 && v3_dot(e->f, v3_norm(v3_sub(p->pos, e->pos))) > 0.85f; /* his nose on us, close */

  bool enemyTargetsLeft = false;
  for (int i = 0; i < g->ntargets; i++)
    if (g->targets[i].alive && g->targets[i].team != p->team) enemyTargetsLeft = true;

  if (threat && ai->threatT > D->react) {
    vec3 tm = v3_norm(v3_sub(threat->pos, p->pos));
    if (skill == 0) {
      if (ai->flareCool <= 0 && p->flares > 0) {
        if (game_rand(g, 0, 1) < D->flareP) flr = true;
        ai->flareCool = 1.1f;
      }
    } else {
      /* decoys (chaff for a radar missile) once it is on the beam and close enough for them to pull it off */
      float onBeam = fm_acosf(clampf(v3_dot(tm, p->f), -1, 1));
      bool decoys = threat->kind == MK_HEAT ? p->flares > 0 : p->chaff > 0;
      if (ai->flareCool <= 0 && decoys && td < 2400 && onBeam > 1.1f && onBeam < 2.05f) {
        /* an ace's flares against a heat-seeker less certain than its chaff: at its old 0.9 and 0.7 s the decoys
         * beat two heat-seekers in three, matches ran 17 minutes and a quarter into sudden death */
        bool heat = threat->kind == MK_HEAT;
        if (game_rand(g, 0, 1) < (skill == 2 && heat ? ACE_FLARE_P : D->flareP)) flr = true;
        ai->flareCool = skill == 2 ? (heat ? ACE_FLARE_GAP : 0.7f) : 1.0f;
      }
      if (ai->bfm < BFM_COBRA) ai->bfm = BFM_BEAM;
    }
    vec3 side = v3_cross(tm, v3(0, 1, 0));
    if (v3_len(side) < 0.1f) side = p->r;
    side = v3_norm(side);
    if (v3_dot(side, p->f) < 0) side = v3_scale(side, -1);
    /* beam it (keep it at 3 or 9 o'clock, slightly nose low); an ace waits
     * until it is close, then breaks hard across its path */
    goal = v3_add(p->pos, v3_scale(v3_add(side, v3(0, -0.08f, 0)), 1000));
    if (ai->diff == DIFF_HARD && td < 700) {
      goal = v3_add(p->pos, v3_scale(v3_add(v3_scale(side, -1), v3_scale(tm, 0.6f)), 1000));
      hard = true;
    }
  } else if (ai->refuel && !(skill == 2 && g->realistic) && (OT->state == TK_STATION || (OT->state == TK_INBOUND && p->fuel < need)) &&
             (g->realistic ? p->fuel < need && v3_dist(p->pos, OT->drogue) < homeD * 0.6f
                           : p->fuel < need || v3_dist(p->pos, OT->pos) < homeD * 0.7f) &&
             !(e && e->alive && eDist < 1200 && p->fuel > need)) {
    /* (realistic: only without the fuel to get home and with the tanker much the nearer: the join takes a minute
     * and more of real station-keeping, and the jets that went for it whenever it was the nearer ran dry chasing
     * it twice as often as they did going home; an ace not at all: four in ten of its dry tanks were still behind
     * the drogue, in afterburner to catch it) */
    /* to the side's tanker: up behind its drogue (it may still be on its way in), then station on it, closing
     * at a few m/s and matching its speed (realistic needs 40 m and 15 m/s held for 3 s) */
    if (ai->bfm < BFM_COBRA) ai->bfm = BFM_HOME;
    float dd = v3_dist(p->pos, OT->drogue);
    float along = v3_dot(v3_sub(OT->drogue, p->pos), OT->f); /* metres still to close, behind it */
    if (dd > 2500) {
      goal = v3_add(v3_sub(OT->drogue, v3_scale(OT->f, 800)), v3_scale(OT->vel, fminf(dd / 250, 20)));
      /* slowing on the way, the nearer the slower (arriving 100 m/s fast it overshot and fell behind again) */
      thr = p->speed > OT->speed + 20 + dd * 0.008f ? 0 : 1;
    } else {
      goal = v3_add(OT->drogue, v3_scale(OT->vel, 0.8f));
      /* closing fast while far (a 10 m/s closure from a kilometre took two minutes and the tank), 4 m/s and more
       * to within 30 m (slowing all the way in, the last 200 m took 40 s), then matched; past it, back off and
       * under the tanker, not into it */
      float rate = along > 30 ? clampf(along * 0.12f, 4, 60) : along > 0 ? along * 0.1f : fmaxf(-12, along * 0.1f);
      if (along < -10) goal = v3_sub(goal, v3_scale(OT->u, 40));
      float want = OT->speed + rate;
      /* the throttle walked toward the speed wanted, gently (0.05 a tick was on-off, and the speed swung past) */
      thr = clampf(p->throttle + (want - p->speed) * (g->realistic ? 0.5f : 1.5f) * dt, 0, 1);
      if (p->speed > want + 15) thr = 0; /* the speed brakes, well over it */
    }
    minAlt = 220;
  } else if (skill == 2 && !ai->refuel && !ET->gone && v3_dist(p->pos, ET->pos) < 15000 && (!e || eDist > 5000) &&
             (p->missiles + p->radar > 0 || p->ammo > 100)) {
    /* an ace hunts the other side's tanker when no fighter is near: missiles from range, then the gun */
    if (ai->bfm < BFM_COBRA) ai->bfm = BFM_PURSUIT;
    float tkd = v3_dist(p->pos, ET->pos);
    goal = v3_add(ET->pos, v3_scale(ET->vel, fminf(tkd / 400, 6)));
    thr = 1;
    float ang = angle_to(ET->pos, p);
    if (p->locked && p->lockTarget.kind == REF_TANKER && ai->mslCool <= 0 && tkd > 600 && tkd < 4500) {
      msl = true;
      ai->mslCool = D->mslGap * 0.5f + game_rand(g, 1, 2);
    }
    if (tkd < 900 && ang < 0.08f && plane_has_gun(p)) gun = true;
  } else if (ai->refuel && ai->site > 0 && MAP && ai->site <= MAP->ncarriers && !(e && e->alive && eDist < 1200 && p->fuel > need)) {
    /* home to the carrier: a gate 3.2 km astern on its heading, then down a 3.5 degree slope to the touchdown
     * point near the stern at about 100 m/s, where the wire stops it within the deck; past the bow or well off
     * the centre line, round again */
    if (ai->bfm < BFM_COBRA) ai->bfm = BFM_HOME;
    const Carrier *cv = &MAP->carriers[ai->site - 1];
    vec3 cf, cr, tdp;
    deck_frame(cv, &cf, &cr, &tdp);
    vec3 rel = v3_sub(p->pos, tdp);
    float along = v3_dot(rel, cf), lat = v3_dot(rel, cr), deckY = tdp.y - 3;
    vec3 gate = v3_add(tdp, v3_scale(cf, -DECK_GATE));
    gate.y = tdp.y + DECK_GATE * DECK_GS;
    /* the deck busy, from just outside the gate in: no final, and round again from one */
    Strip ds = deck_strip(cv);
    bool busy = along > -(DECK_GATE + BUSY_ZONE) && strip_busy(g, p, &ds);
    if (!ai->final_leg) {
      minAlt = CIRCUIT_MIN_ALT;
      bool clear = field_clear(g, p, tdp);
      goal = approach_goal(p, ai, gate, v3_scale(cf, -1), g->realistic, clear);
      goal.y = fmaxf(goal.y, route_floor(p->pos, goal));
      float gd = v3_dist(v3(gate.x, 0, gate.z), v3(p->pos.x, 0, p->pos.z));
      thr = gd < 6000 ? (p->speed > 135 ? 0 : p->speed > 115 ? 0.3f : 0.7f) : 0.8f;
      if (g->realistic && p->speed > 165) thr = 0;
      /* realistic, on the line in: idle and level down to CIRCUIT_SPD, gear and flaps out, the 4 km before the
       * gate (it came onto the slope at 170, touched down at 135-150 and ran out of runway) */
      if (g->realistic && ai->lined) thr = p->speed > CIRCUIT_SPD ? 0 : p->speed > CIRCUIT_SPD - 6 ? 0.35f : 0.7f;
      if ((gd < (g->realistic ? 1500 : 700) || at_gate(p, gate, v3_scale(cf, -1), g->realistic)) && p->speed < 150 &&
          inbound(p, cf))
        ai->final_leg = true;
      if (p->fuel < 8 && inbound(p, cf) && in_cone(p, tdp, v3_scale(cf, -1))) ai->final_leg = true;
      /* missed twice: straight in from anywhere out along the line, slow enough to land */
      if (ai->missed >= 2 && p->speed < 150 && inbound(p, cf) && in_cone(p, tdp, v3_scale(cf, -1)))
        ai->final_leg = true; /* nearly dry: straight in from here, no gate */
      if (busy) ai->final_leg = false;
    } else {
      /* on the slope all the way into the wires, no flare (a flare floated the jets past the whole deck): the
       * aim point on the slope ahead, the slope meeting the deck itself at the touchdown point */
      float lead = clampf(-along * 0.4f, 200, 900), a = along + lead;
      goal = v3_add(tdp, v3_scale(cf, a));
      goal.y = deckY + 1 + DECK_GS * fmaxf(0, -a) - (a > 0 ? a * DECK_GS : 0);
      if (p->onGround) goal = v3_add(v3(tdp.x, tdp.y, tdp.z), v3_scale(cf, 2000));
      thr = p->onGround || p->speed > 108 ? 0 : p->speed > 98 ? 0.1f : 0.45f;
      minAlt = 15;
      landing = true;
      /* a bolter: on the deck too fast to stop on what is left of it (the wire and the brakes take 30 m/s each
       * second), full power and off the bow flying (rolled out at idle, jets left the bow at 30-120 m/s and went
       * into the sea) */
      float deck_left = cv->length * (1 - DECK_TD) - along;
      bool bolter = p->onGround && onDeckNow(p) && p->speed > 25 && p->speed * p->speed / 60 > deck_left - 15;
      if (bolter) {
        ai->final_leg = false;
        ai->missed++;
        landing = false;
        goal = v3_add(p->pos, v3_add(v3_scale(cf, 2500), v3(0, 150, 0)));
        thr = 1;
      }
      /* round again: well off the line close in, or past the bow still flying */
      else if (!p->onGround && (busy || (fabsf(lat) > 45 && along > -900) || (fabsf(lat) > 20 && along > -400) || along > cv->length * (1 - DECK_TD))) {
        ai->final_leg = false;
        ai->missed++;
        landing = false;
        goal = v3_add(p->pos, v3_add(v3_scale(cf, 2500), v3(0, 250, 0)));
        goal.y = fminf(goal.y, gate.y); /* round low, at the gate's height: a climb burnt the fuel home */
        thr = g->realistic ? 0.7f : 1;
      }
    }
  } else if (ai->refuel && !(e && e->alive && eDist < 1200 && p->fuel > need)) {
    if (ai->bfm < BFM_COBRA) ai->bfm = BFM_HOME;
    const Base *b = &BASES[p->team];
    float elev = terrain_h(b->x, b->z);
    /* the circuit: a gate 3.2 km out on the runway's line, then in toward the base on a glide path that
     * clears the ground before the threshold (the runway is 2 km long, b->z +- 1000) */
    float tdown, slope;
    glide_path(b, elev, g->realistic, &tdown, &slope);
    vec3 E = v3(b->x, elev + 3 + fmaxf(257, slope * (2200 + tdown)), b->z + b->dir * 3200),
         T = v3(b->x, elev + 3, b->z + b->dir * (1000 - tdown));
    float endZ = b->z - b->dir * 1000;
    /* the runway busy, from just outside the gate in: no final, and round again from one */
    Strip rs = base_strip(p->team);
    bool busy = (p->pos.z - T.z) * b->dir < (E.z - T.z) * b->dir + BUSY_ZONE && strip_busy(g, p, &rs);
    if (!ai->final_leg) {
      minAlt = CIRCUIT_MIN_ALT;
      bool clear = field_clear(g, p, v3(b->x, 0, b->z));
      goal = approach_goal(p, ai, E, v3(0, 0, b->dir), g->realistic, clear);
      goal.y = fmaxf(goal.y, route_floor(p->pos, goal));
      thr = 0.8f;
      /* realistic slows down slower (no arcade drag): come in at circuit
       * speed, power off and speed brakes out until there */
      if (g->realistic && p->speed > 165) thr = 0;
      /* realistic, on the line in: idle and level down to CIRCUIT_SPD, gear and flaps out, the 4 km before the
       * gate (it came onto the slope at 170, touched down at 135-150 and ran out of runway) */
      if (g->realistic && ai->lined) thr = p->speed > CIRCUIT_SPD ? 0 : p->speed > CIRCUIT_SPD - 6 ? 0.35f : 0.7f;
      /* arcade too, the last 6 km: arrive slow enough to land */
      float ex = E.x - p->pos.x, ez = E.z - p->pos.z;
      if (ex * ex + ez * ez < 6000.0f * 6000.0f && !(g->realistic && ai->lined)) thr = p->speed > 135 ? 0 : p->speed > 115 ? 0.3f : 0.7f;
      /* over the gate, whatever the height (it can sit over a hill the
       * terrain avoidance keeps us above); realistic turns wider, and a
       * 600 m gate is one it can only circle */
      float gx = E.x - p->pos.x, gz = E.z - p->pos.z;
      /* nearly dry: straight in from here, no gate */
      if (p->fuel < 8 && inbound(p, v3(0, 0, -b->dir)) && in_cone(p, T, v3(0, 0, b->dir))) ai->final_leg = true;
      /* missed twice: straight in from anywhere out along the line, slow enough to land, no gate */
      if (ai->missed >= 2 && p->speed < 150 && inbound(p, v3(0, 0, -b->dir)) && in_cone(p, T, v3(0, 0, b->dir)))
        ai->final_leg = true;
      if ((gx * gx + gz * gz < (g->realistic ? 1500.0f * 1500.0f : 600.0f * 600.0f) || at_gate(p, E, v3(0, 0, b->dir), g->realistic)) &&
          p->speed < 150 && inbound(p, v3(0, 0, -b->dir)))
        ai->final_leg = true;
      if (busy) ai->final_leg = false;
    } else {
      /* steered at the point on the glide path 1 km ahead, power off down to landing speed; past the
       * touchdown point, along the runway; on the wheels, power off so the brakes stop us, and sit there
       * until full (refuelling needs the jet slow and on the base) */
      float rem = (p->pos.z - T.z) * b->dir; /* along the approach to the touchdown point */
      /* onto the centre line: the aim point comes closer the nearer the runway, so a jet off the line turns
       * onto it in time (with a fixed 1 km lead it touched down a few hundred metres to the side) */
      float lead = clampf(rem * 0.4f, 350, 1000);
      /* (no higher than the gate: a straight-in from far out climbed toward a slope extended kilometres up) */
      goal = v3(b->x, elev + 3 + fminf(slope * fmaxf(0, rem - lead), E.y - elev - 3), p->pos.z - b->dir * lead);
      if (p->onGround || rem < 0) goal = v3(b->x, elev + 3, endZ - b->dir * 2000);
      /* the flare, the last 30 m (and anywhere past the touchdown point, where the goal at the runway's far
       * end left the jet floating its whole length a few metres up): the wheels meet the runway at a few m/s,
       * not the 7-16 of the glide */
      /* realistic (no arcade drag): a short firm flare, or the float carried it past mid-runway and round again */
      float flareH = g->realistic ? 8 : 18, sinkMin = g->realistic ? 2.5f : 2;
      if (!p->onGround && (p->pos.y - elev < flareH || rem < 0)) { /* pitch on the sink rate, down to 1.5 at the wheels */
        float want = -fmaxf(sinkMin, (p->pos.y - elev - 3) * 0.45f);
        flare = true;
        flareP = clampf((want - p->vel.y) * 0.12f, -0.3f, 1);
      }
      /* down the glide at about 105 m/s, 15 % over the stall (at 125 the roll-out needed most of the runway) */
      thr = p->onGround || p->speed > 112 ? 0 : p->speed > 100 ? 0.08f : 0.4f;
      /* the ground kept clear as on the circuit until the last 3 km: a straight-in from far out (after two misses,
       * or nearly dry) came down the line at 25 m over a hill 4 km out, too slow to climb it */
      minAlt = rem > 3000 ? CIRCUIT_MIN_ALT : 25;
      landing = true;
      /* off the end, or off the line by more than the way in allows (a flat 900 m ended finals begun from the
       * wider gate the same tick, on and off every tick) */
      if ((p->pos.z - endZ) * b->dir < 0 || fabsf(p->pos.x - b->x) > fmaxf(900, rem * 0.8f + 200)) ai->final_leg = false, ai->missed++;
      /* go around when low and far off the line (going round for speed or a small offset as well crashed
       * more jets than it saved: the audit counted 160 more crashes), or on the wheels with too little runway */
      float agl = p->pos.y - elev; /* over the runway, not the ground under us: a ridge before the threshold
                                      * made every approach a go-around */
      /* too little runway left to stop on the brakes: a touch-and-go, while still fast */
      float left = 1000 - (p->pos.z - b->z) * (p->f.z > 0 ? 1 : -1);
      bool overrun = p->onGround && p->speed > 95 && p->speed * p->speed / (2 * sim_brake(g)) > left - 100;
      /* (off the line low only near the runway: far out, below 60 m over the runway's height is a valley or the
       * sea under the slope, and that alone sent finals round by the thousand) */
      /* and round again rather than onto the grass: under 20 m over the runway more than 30 m off its centre line
       * (the wheels count only within 35 m of it; jets 40-70 m off at 110 m/s touched down beside it and crashed) */
      bool beside = !p->onGround && agl < 20 && fabsf(p->pos.x - b->x) > 30 && fabsf(p->pos.z - b->z) < 1100;
      if (overrun || beside || (!p->onGround && busy) || (!p->onGround && agl < 60 && fabsf(p->pos.x - b->x) > 150 && left < 2300)) {
        ai->final_leg = false;
        ai->missed++;
        landing = false;
        goal = v3_add(p->pos, v3(0, 250, -b->dir * 2500)); /* a gentle climb: slow, a steep one stalls */
        goal.y = fminf(goal.y, E.y); /* and no higher than the gate: a long climb burnt the fuel home */
        thr = g->realistic ? 0.7f : 1;
      }
    }
  } else if (g->def.on && p->team == 1 && !(e && e->alive && eDist < 1500) && g->def.hp > 0) {
    /* carrier strike: run in low, fire the anti-ship missiles from about
     * 5 km, strafe on the way past, then run out and come round again */
    const Defense *d = &g->def;
    vec3 cv = v3(d->pos.x, d->deck + 40, d->pos.z);
    ai->bfm = BFM_STRIKE;
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
      if (cd < 1500 && ang < D->gunCone * 2 && !JETS[p->jet].nogun) gun = true;
      if (cd < 500) {
        vec3 away = v3_norm(v3(p->f.x, 0, p->f.z));
        ai->egress = v3_add(p->pos, v3_add(v3_scale(away, 5000), v3(game_rand(g, -1500, 1500), 600, 0)));
        ai->egressT = 12;
      }
    }
    minAlt = 60;
  } else if (e && e->alive && (eDist < D->fightR || !enemyTargetsLeft)) {
    const Base *home = &BASES[p->team];
    float hx = e->pos.x - home->x, hz = e->pos.z - home->z;
    const Plane *mate = skill > 0 ? wingmate(g, p) : NULL;
    bool strike = enemyTargetsLeft && !g->dogfight && g->mode != MODE_DRONE;
    if (ai->baitCool > 0) ai->baitCool -= dt;
    if (skill > 0 && jet_is(p->jet, "vk33") && !g->dogfight && g->mode != MODE_DRONE && !eThreat &&
        hx * hx + hz * hz > (6000 + 100 * p->fuel) * (6000 + 100 * p->fuel)) {
      /* the shortest legs: the fight only near home (out to 16 km full, 11 km half full); a combat air patrol
       * 5 km out toward him until he comes */
      float hl = sqrtf(hx * hx + hz * hz);
      goal = v3(home->x + hx / hl * 5000, terrain_h(home->x, home->z) + 3000, home->z + hz / hl * 5000);
      thr = 0.7f;
      ai->bfm = BFM_CAP;
    } else if (skill == 2 && strike && (ai->baitT > 0 || (ai->baitCool <= 0 && eDist > 4500 && eDist < 9000 && ai->jit.x > 0))) {
      /* the bait (aces, strike missions): a steady, predictable run away from him to draw him in, then round
       * to fight once he is close behind */
      if (ai->baitT <= 0) {
        ai->baitT = 25;
        vec3 away = v3(p->pos.x - e->pos.x, 0, p->pos.z - e->pos.z);
        ai->baitDir = v3_len(away) > 1 ? v3_norm(away) : v3_norm(v3(p->f.x, 0, p->f.z));
      }
      ai->baitT -= dt;
      goal = v3_add(p->pos, v3_add(v3_scale(ai->baitDir, 2000), v3(0, fmaxf(0, terrain_h(p->pos.x, p->pos.z) + 1500 - p->pos.y), 0)));
      thr = 0.8f;
      ai->bfm = BFM_BAIT;
      if (eDist < 3800 || eThreat || ai->baitT <= 0 || fabsf(p->pos.x) > ARENA_X - 2500 || fabsf(p->pos.z) > ARENA_Z - 2500) {
        ai->baitT = 0; /* sprung: turn and fight */
        ai->baitCool = 90;
        ai->bfm = BFM_BREAK;
        ai->bfmT = 0;
      }
    } else if (mate && mate->ai.seen == ai->seen && p->idx > mate->idx && eDist > 2500 && !eThreat &&
               !(v3_dist(e->pos, mate->pos) < 2500 && v3_dot(e->f, v3_norm(v3_sub(mate->pos, e->pos))) > 0.8f)) {
      /* the wingman on one enemy with the lead: top cover, above and behind the lead, down into the fight when
       * he gets onto the lead's tail or comes at us */
      goal = v3_add(v3_add(mate->pos, v3_scale(mate->f, -800)), v3(0, 1500, 0));
      thr = v3_dist(goal, p->pos) > 1500 ? 1 : 0.7f;
      ai->bfm = BFM_COVER;
    } else dogfight(g, p, e, eDist, dt, &goal, &thr, &gun, &msl, &hard);
  } else if (!enemyTargetsLeft || g->dogfight || g->mode == MODE_DRONE) {
    /* nothing in sight and nothing to strike: search, out toward where the enemy comes from, then the middle */
    const Base *b = &BASES[1 - p->team];
    float bx = b->x - p->pos.x, bz = b->z - p->pos.z;
    goal = bx * bx + bz * bz > 6000.0f * 6000.0f ? v3(b->x, 3000, b->z) : v3(0, 3000, 0);
    goal.y = fmaxf(goal.y, terrain_h(goal.x, goal.z) + 1500);
    thr = 0.85f;
    if (ai->bfm < BFM_COBRA) ai->bfm = BFM_SEARCH;
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
      if (ai->bfm < BFM_COBRA) ai->bfm = BFM_STRIKE;
      float d = v3_dist(t->pos, p->pos);
      goal = d > 2600 ? v3(t->pos.x, t->ground + 900, t->pos.z) : v3_add(t->pos, v3(0, 6, 0));
      minAlt = d < 2600 ? 120 : 220;
      thr = d < 2600 ? 0.6f : 1;
      /* with an air-to-ground missile: the run in low and slow enough to launch it, from inside the lock range */
      bool agm = p->ground > 0;
      if (agm && d < 8000 && d > 1500 && eDist > 8000) { /* slow and low only with no enemy near */
        goal = v3(t->pos.x, t->ground + STRIKE_AGL, t->pos.z);
        thr = p->speed > STRIKE_SPD ? 0 : p->speed > STRIKE_SPD - 25 ? 0.5f : 0.9f;
        minAlt = 160;
      }
      if (angle_to(goal, p) < 0.05f && d < 1200 && !JETS[p->jet].nogun) gun = true;
      if (p->locked && ref_eq(p->lockTarget, ref_ground(ai->gt)) && ai->mslCool <= 0) {
        msl = true;
        p->ctl.mslk = agm ? 2 : 0;
        ai->mslCool = D->mslGap * 0.6f;
      }
    }
  }

  /* never long straight and level with an enemy about (normal pilots 25 s, aces 18): a turn of 40 degrees and
   * 600 m up or down (up when low) breaks the line his radar and his missile would draw */
  bool defending = threat && ai->threatT > D->react;
  if (skill > 0 && e && eDist < 10000 && !landing && !p->onGround && !p->taxi && ai->baitT <= 0 && !defending) {
    float bank = fm_atan2f(-p->r.y, p->u.y);
    bool level = fabsf(bank) < 0.14f && fabsf(p->f.y) < 0.07f && v3_dot(p->f, ai->levelF) > 0.985f &&
                 fabsf(p->pos.y - ai->levelH) < 150;
    if (level) ai->levelT += dt;
    else {
      ai->levelT = 0;
      ai->levelF = p->f;
      ai->levelH = p->pos.y;
    }
    if (ai->levelT > (skill == 2 ? 18 : 25) && ai->jinkT <= 0) {
      vec3 fh = v3_norm(v3(p->f.x, 0, p->f.z)), rh = v3_cross(v3(0, 1, 0), fh);
      float side = ai->jit.x < 0 ? -1.0f : 1.0f, up = p->pos.y - terrain_h(p->pos.x, p->pos.z) < 2000 || ai->jit.y > 0 ? 1.0f : -1.0f;
      ai->jink = v3_add(v3_add(v3_scale(fh, 1150), v3_scale(rh, side * 960)), v3(0, up * 600, 0));
      ai->jinkT = 6;
      ai->levelT = 0;
    }
  } else ai->levelT = 0;
  if (ai->jinkT > 0) {
    ai->jinkT -= dt;
    if (!landing && !defending) goal = v3_add(p->pos, ai->jink);
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
  /* landing: the runway ahead is ground we mean to reach, not ground to pull away from */
  if (landing && ai->site > 0 && MAP && ai->site <= MAP->ncarriers) {
    /* lined up on the carrier, down at landing speed: its deck ahead is where we mean to be */
    const Carrier *cv = &MAP->carriers[ai->site - 1];
    vec3 cf, cr, tdp;
    deck_frame(cv, &cf, &cr, &tdp);
    vec3 rel = v3_sub(p->pos, tdp);
    if ((fabsf(v3_dot(rel, cr)) < 45 && v3_dot(rel, cf) > -2500 && p->speed < 150) || p->onGround) low = false;
  } else if (landing) {
    const Base *b = &BASES[p->team];
    /* lined up and looking at the runway itself, down at landing speed (it was 130 m/s, faster than the
     * approach could slow to), or rolling on it: the runway ahead is ground we mean to reach */
    /* (up to 200 m/s and 100 m off the centre line: below 150 and 45 m, realistic jets a little fast pulled up
     * from their own runway and flew off its far end, round again) */
    if ((fabsf(look.x - b->x) < 100 && fabsf(look.z - b->z) < 1100 && fabsf(p->pos.x - b->x) < 100 && p->speed < 200) ||
        p->onGround)
      low = false;
  }
  /* held 1.2 s once it starts: on and off every few ticks over rising ground it rocked the wings */
  if (low) ai->lowT = 1.2f;
  else if (ai->lowT > 0 && !landing) {
    ai->lowT -= dt;
    low = true;
  }
  if (low) {
    vec3 fh = v3_norm(v3(p->f.x, 0, p->f.z));
    /* on the way home over ground no higher than we are (the sink toward a low gate): a gentle climb on the speed
     * (700 m up at full power each time burnt the fuel home); over a ridge as high as we are, the full pull-up */
    bool ridge = gh > p->pos.y - minAlt;
    for (int k = 1; g->realistic && k <= 8 && !ridge; k++) {
      vec3 q = v3_add(p->pos, v3_scale(p->vel, ahead * (float)k / 8));
      ridge = terrain_h(q.x, q.z) > p->pos.y - minAlt;
    }
    bool gentle = ai->refuel && !ridge;
    goal = v3_add(p->pos, v3_add(v3_scale(fh, 600), v3(0, gentle ? 250.0f : 700.0f, 0)));
    /* on the way in, with landing speed and more: the speed is the pull-up (full power over a ridge before
     * the threshold ran the speed up to 190 m/s, too fast to land, and round again) */
    thr = (landing || gentle) && p->speed > 120 ? 0 : 1;
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
  /* near the edge: back toward the middle, unless the ground ahead needs the pull-up first */
  if (!low && (fabsf(p->pos.x) > ARENA_X - 600 || fabsf(p->pos.z) > ARENA_Z - 600)) goal = v3(0, fmaxf(800, p->pos.y), 0);

  vec3 d = v3_norm(v3_sub(goal, p->pos));
  float lx = v3_dot(d, p->r), ly = v3_dot(d, p->u), lz = v3_dot(d, p->f);
  float off = fm_acosf(clampf(lz, -1, 1));
  float rollErr = fm_atan2f(lx, ly);
  float bank = fm_atan2f(-p->r.y, p->u.y);
  float roll = off < 0.12f ? -bank * 1.5f + lx * 6 : rollErr * 2.2f;
  float pitch = (fabsf(rollErr) < 0.9f || off < 0.12f) ? clampf(fm_atan2f(ly, lz) * 3, -0.5f, 1) : 0.2f;
  /* landing: wings near level, a small bank toward the line and the nose down onto the slope (rolling the lift
   * onto a goal below the nose put jets on the runway at up to 90 degrees of bank, and the wingtip in) */
  if (landing) {
    float lim = p->pos.y - here < 30 ? 0.2f : 0.5f;
    roll = (clampf(lx * 4, -lim, lim) - bank) * 1.5f;
    pitch = clampf(fm_atan2f(ly, lz) * 3, -0.5f, 1);
  }
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
  if (g->realistic && !p->drone && !p->onGround && !landing && p->speed < (g->classic ? 72 : 90) * 1.35f) { /* not rolling out */
    pitch = fminf(pitch, p->f.y < 0 ? 0.6f : 0.25f);
    thr = 1;
  }
  if (direct) {
    pitch = dp;
    roll = dr;
  }
  if (flare && !low) pitch = flareP;
  /* a CPU pilot feels the G too, and eases off before blacking out */
  if (p->gstress > 2.5f && pitch > 0.5f) pitch = 0.5f;
  c->pitch += (pitch - c->pitch) * fminf(1, dt * 6);
  c->roll += (roll - c->roll) * fminf(1, dt * 7);
  /* home for fuel in realistic handling, and nothing after us: no afterburner (past 0.9 it burns five times
   * the fuel, and the pull-ups and the near-stall rule's full power on the way in emptied the tanks) */
  if (g->realistic && ai->refuel && !threat && !p->drone) thr = fminf(thr, HOME_MAX_THR);
  if (hold_for_strip(g, p)) thr = 0; /* on the brakes (off the catapult's power) until the strip is clear */
  p->throttle += clampf(thr - p->throttle, -0.8f * dt, 0.8f * dt);
  c->gun = gun;
  c->msl = msl;
  c->flr = flr;
  /* the gear down on the final approach and on the ground, flaps out on the way home's last stretch */
  bool lined_in = g->realistic && ai->refuel && ai->lined; /* realistic, lined up on the way in: slowed with gear and flaps */
  if (ai_gear_flaps) ai_gear_flaps(p, landing || p->onGround || p->taxi || lined_in, landing ? 1 : (ai->refuel && ai->final_leg) || lined_in ? 0.5f : 0);
  c->vec = vec;
}
