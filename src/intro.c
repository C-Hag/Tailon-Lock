/* intro.c - the opening cinematic (the browser game's IntroCore and Intro
 * modules, assets/intro/src/intro_core.js and intro.js; shot script version
 * 2, see assets/intro/INTRO.md). Everything that moves is keyed in the
 * script; this file samples the keys, poses the deck crew, runs a seeded
 * fixed-step particle simulation for smoke, fire, steam and spray, bolts
 * cameras to jets (with a cockpit head-up display), and draws the cards and
 * screen effects with the film grade and heat haze. The scene is the Twin
 * Isles map at dawn with its scattered cumulus, the CVN-90, the NF-24
 * Cormorant and the Ks-55. */
#include "intro.h"
#include "brand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "cic.h"
#include "figure.h"
#include "core/json.h"
#include "core/jets.h"
#include "core/world.h"
#include "gpu.h"
#include "hud.h"
#include "music.h"
#include "render3d.h"
#include "sfx.h"
#include "units.h"
#include "platform/plat.h"

#define D2R (PI_F / 180)

/* ---- key tracks (IntroCore.track / sample) ----------------------------- */
typedef struct { int n; float *k; } Seg; /* n keys of `stride` floats */
typedef struct { int nseg, stride; Seg *segs; } Track;

static bool parse_track(const Json *keys, int stride, Track *tr) {
  memset(tr, 0, sizeof *tr);
  tr->stride = stride;
  int n = json_count(keys);
  if (n <= 0) return false;
  tr->segs = calloc((size_t)n, sizeof(Seg));
  float *buf = malloc(sizeof(float) * (size_t)n * stride), *first = buf;
  if (!tr->segs || !buf) {
    free(tr->segs), free(buf);
    tr->segs = NULL;
    return false;
  }
  Seg *cur = NULL;
  const Json *k;
  JSON_FOR(k, keys) {
    if (!json_is_array(k)) {
      cur = NULL;
      continue;
    }
    if (!cur) {
      cur = &tr->segs[tr->nseg++];
      cur->k = buf;
      cur->n = 0;
    }
    for (int c = 0; c < stride; c++) {
      const Json *v = json_at(k, c);
      buf[c] = json_is_number(v) ? (float)v->num : 0;
    }
    buf += stride;
    cur->n++;
  }
  if (!tr->nseg) { /* no key was an array: nothing kept, nothing left behind */
    free(tr->segs), free(first);
    tr->segs = NULL;
    return false;
  }
  return true;
}

static float tangent(const float *p, const float *m, const float *n, int c) {
  if (p && p[1] == m[1] && p[2] == m[2] && p[3] == m[3]) return 0;
  if (n && n[1] == m[1] && n[2] == m[2] && n[3] == m[3]) return 0;
  if (!p) return n ? (n[c] - m[c]) / (n[0] - m[0]) : 0;
  if (!n) return (m[c] - p[c]) / (m[0] - p[0]);
  return (n[c] - p[c]) / (n[0] - p[0]);
}

/* Position: cubic Hermite with Catmull-Rom tangents; angles and fov linear. */
static bool sample(const Track *tr, float t, float *out) {
  const Seg *K = NULL; /* where one path ends as the next starts, the next one */
  for (int i = 0; i < tr->nseg; i++) {
    const Seg *s = &tr->segs[i];
    if (t >= s->k[0] && t <= s->k[(s->n - 1) * tr->stride]) K = s;
  }
  if (!K) return false;
  const int S = tr->stride;
  int lo = 0, hi = K->n - 1;
  while (hi - lo > 1) {
    int m = (lo + hi) >> 1;
    if (K->k[m * S] <= t) lo = m;
    else hi = m;
  }
  const float *a = &K->k[lo * S], *b = &K->k[hi * S];
  out[0] = t;
  if (lo == hi || b[0] == a[0]) {
    for (int c = 1; c < S; c++) out[c] = a[c];
    return true;
  }
  float dt = b[0] - a[0], u = (t - a[0]) / dt;
  const float *p = lo > 0 ? &K->k[(lo - 1) * S] : NULL, *n = hi + 1 < K->n ? &K->k[(hi + 1) * S] : NULL;
  float h00 = 2 * u * u * u - 3 * u * u + 1, h10 = u * u * u - 2 * u * u + u, h01 = -2 * u * u * u + 3 * u * u, h11 = u * u * u - u * u;
  for (int c = 1; c <= 3; c++) out[c] = h00 * a[c] + h10 * dt * tangent(p, a, b, c) + h01 * b[c] + h11 * dt * tangent(a, b, n, c);
  for (int c = 4; c < S; c++) out[c] = a[c] + (b[c] - a[c]) * u;
  return true;
}

typedef struct { vec3 pos, f, r, u; } Frame;

static Frame frame_of(float yaw, float pitch, float roll) {
  float y = yaw * D2R, p = pitch * D2R, r = roll * D2R;
  vec3 f = v3(sinf(y) * cosf(p), sinf(p), cosf(y) * cosf(p));
  vec3 r0 = v3(cosf(y), 0, -sinf(y));
  vec3 u0 = v3_cross(f, r0);
  float c = cosf(r), s = sinf(r);
  Frame F;
  F.f = f;
  F.r = v3_sub(v3_scale(r0, c), v3_scale(u0, s));
  F.u = v3_add(v3_scale(u0, c), v3_scale(r0, s));
  F.pos = v3(0, 0, 0);
  return F;
}

/* ---- the script ------------------------------------------------------- */
enum { K_SHIP, K_STATIC, K_JET, K_MISSILE, K_CREW };
enum { M_CVN90, M_FRIGATE, M_NF24, M_KS55, M_DR7, M_JA46, M_MISSILE };
typedef struct { float t0, t1, v; } Win;

typedef struct {
  char  id[16], callsign[16];
  int   kind, model, team, attach;
  Track tr;
  float sx, sz, heading, speed, wake; /* ships */
  vec3  lpos;                         /* statics: ship-local */
  float lheading;
  float spawn, end, dead;             /* -1 when unset */
  Win   ab[8], trail[4], vapour[8], cone[4];
  int   nab, ntrail, nvapour, ncone;
  uint32_t jersey, helmet;            /* crew */
  float pose_t[8];
  int   pose[8], npose;
} Actor;

/* crew pose: crouch, torso pitch, L arm elevation, azimuth, elbow, R arm
 * elevation, azimuth, elbow, walk, R hand circle (INTRO.md "poses") */
#define POSE_N 10
#define MAX_POSES 12
static char  pose_name[MAX_POSES][12];
static float pose_val[MAX_POSES][POSE_N];
static int   nposes;

typedef struct { int attach; vec3 a, b; float t0, t1, rate; } Emitter;
static Emitter emit[8];
static int     nemit;
static vec3    env_sun;
static bool    has_sun;
static bool    dawn_title;      /* a smoke title in the bright sky: dark text under it */
static float   clear_sky[8][3]; /* env.clear_clouds */
static int     nclear;

typedef struct { float t; vec3 p, v; float life; } FlareDef;
typedef struct { float t, t1; int type; int actor; vec3 pos; float size; int sound, repeat; float every, vol; } Event;
typedef struct {
  float t0, t1;
  Track cam;
  int   attach, hud, target; /* actors, or -1 */
  vec3  off, dir;
  bool  roll, guns, panel; /* panel: the game's HUD and instrument panel (version 5) */
  float lock0, lock1;       /* the lock closes over [lock0, lock1], or both -1 */
  float vibe_a, vibe_f;
  float shade;         /* backlight: how far this shot dims the jets and the crew */
  int   in;              /* IN_CUT, IN_DISSOLVE or IN_WHIP (version 3) */
  float in_dur, in_dir;
  bool  scene;           /* the control room, drawn in code (cic.c), not the 3D world */
  bool  radar;           /* and of those, the radar scope alone */
} Shot;
enum { IN_CUT, IN_DISSOLVE, IN_WHIP };
typedef struct { float t0, t1, type, blink; int style; char text[64], speaker[16]; } Card;
typedef struct {
  float t0, t1;
  int   type;
  float from, to, amount, period;
  uint32_t color;
  int   contacts;
  float bearing;
  bool  has_from;
  float warm, vignette, grain, y0, y1; /* grade, haze */
  char  text[24];                      /* smoketitle */
  vec3  pos;
  float f0, f1;  /* smoketitle: the letters form over [f0, f1] */
  float d0, d1;  /* and dissolve back into drifting smoke over [d0, d1] */
  uint32_t seed;
} Fx;

enum { EV_EXPLODE, EV_KILL, EV_SPLASH, EV_GUNS, EV_SOUND };
enum { SND_KLAXON, SND_CATAPULT, SND_MISSILE, SND_FLARES, SND_EXPLODE, SND_CLOUD, SND_FLYBY, SND_BOOM, SND_BOOM_TAIL, SND_BOOM_ECHO, SND_RUMBLE, SND_OTHER };
enum { CS_LOCATION, CS_SUB, CS_ALERT, CS_ALERT_SUB, CS_CALLSIGN, CS_RADIO, CS_TITLE, CS_PRESS, CS_SUBTITLE, CS_DIALOG };
enum { FX_FADE, FX_FLASH, FX_LETTERBOX, FX_ALARM, FX_RADAR, FX_SHAKE, FX_GRADE, FX_HAZE, FX_SMOKETITLE, FX_GLOW, FX_SEAHAZE, FX_NONE };

#define MAX_ACT 32
static Actor    act[MAX_ACT];
static int      nact;
static FlareDef *flares;
static int      nflares;
static Event    *events;
static int      nevents;
static Shot     *shots;
static int      nshots;
static Card     *cards;
static int      ncards;
static Fx       *fxs;
static int      nfx;
static float    menu_at = 36;
static Track    warp;    /* slow motion: [film clock, story time] pairs; outside them the two are the same */
static Cloud    extra[4];
static char     music_key[32];
static float    song_shift;   /* how far the film's cut of its music runs behind the song (repeated bars) */
static int      nextra;
static Mesh     nf24, nf24_lod;
static bool     ready;

static float numv(const Json *o, const char *k, float def) {
  const Json *n = json_get(o, k);
  return json_is_number(n) ? (float)n->num : def;
}
static const char *strv(const Json *o, const char *k) {
  const char *s = json_str(json_get(o, k));
  return s ? s : "";
}
static vec3 vecv(const Json *a) {
  return v3((float)json_at(a, 0)->num, (float)json_at(a, 1)->num,
            (float)json_at(a, 2)->num);
}
static int windows(const Json *a, Win *w, int max) {
  int n = 0;
  const Json *e;
  JSON_FOR(e, a) {
    if (n >= max) break;
    w[n].t0 = (float)json_at(e, 0)->num;
    w[n].t1 = (float)json_at(e, 1)->num;
    const Json *v = json_at(e, 2);
    w[n].v = json_is_number(v) ? (float)v->num : 1;
    n++;
  }
  return n;
}
static float in_win(const Win *w, int n, float t) {
  for (int i = 0; i < n; i++)
    if (t >= w[i].t0 && t < w[i].t1) return w[i].v;
  return 0;
}

static int find_actor(const char *id) {
  for (int i = 0; i < nact; i++)
    if (!strcmp(act[i].id, id)) return i;
  return -1;
}

static void load_clouds(const Json *list) {
  const Json *c;
  JSON_FOR(c, list) {
    if (nextra >= 4) break;
    Cloud *C = &extra[nextra++];
    C->x = numv(c, "x", 0);
    C->z = numv(c, "z", 0);
    C->base = numv(c, "base", 1000);
    C->top = numv(c, "top", 1500);
    C->albedo = 1;
    const Json *pl = json_get(c, "puffs");
    C->npuffs = json_count(pl);
    C->puffs = calloc((size_t)(C->npuffs ? C->npuffs : 1), sizeof(Puff));
    int q = 0;
    const Json *pf;
    JSON_FOR(pf, pl) {
      Puff *P = &C->puffs[q++];
      P->dx = (float)json_at(pf, 0)->num;
      P->dy = (float)json_at(pf, 1)->num;
      P->dz = (float)json_at(pf, 2)->num;
      P->r = (float)json_at(pf, 3)->num;
    }
  }
}

bool intro_init(void) {
  Json *S = assets_json("intro/intro_script.json"), *A = assets_json("intro/intro_assets.json");
  if (!S || !A || assets_map_find("twin-isles") < 0) {
    json_free(S);
    json_free(A);
    return false;
  }
  const Json *nf = json_get(A, "nf24");
  if (assets_mesh(json_get(nf, "m"), &nf24) && assets_mesh(json_get(nf, "lod"), &nf24_lod))
    nf24.lod = &nf24_lod;
  menu_at = numv(S, "menu_at", 36);
  snprintf(music_key, sizeof music_key, "%s", strv(json_get(S, "music"), "track"));
  song_shift = numv(json_get(S, "music"), "song_shift", 0);
  if (!(song_shift >= 0 && song_shift < 120)) song_shift = 0;

  const Json *e;
  JSON_FOR(e, json_get(S, "poses")) {
    if (nposes >= MAX_POSES) break;
    snprintf(pose_name[nposes], sizeof pose_name[0], "%s", e->name);
    for (int q = 0; q < POSE_N; q++) pose_val[nposes][q] = (float)json_num(json_at(e, q));
    nposes++;
  }
  const Json *sun = json_get(json_get(S, "env"), "sun");
  if (json_count(sun) == 3) {
    env_sun = v3_norm(vecv(sun));
    has_sun = true;
  }
  JSON_FOR(e, json_get(S, "actors")) {
    if (nact >= MAX_ACT) break;
    Actor *a = &act[nact];
    memset(a, 0, sizeof *a);
    snprintf(a->id, sizeof a->id, "%s", strv(e, "id"));
    snprintf(a->callsign, sizeof a->callsign, "%s", strv(e, "callsign"));
    const char *kind = strv(e, "kind"), *model = strv(e, "model");
    a->kind = !strcmp(kind, "ship") ? K_SHIP : !strcmp(kind, "static") ? K_STATIC : !strcmp(kind, "missile") ? K_MISSILE
            : !strcmp(kind, "crew") ? K_CREW : K_JET;
    a->model = !strcmp(model, "cvn90") ? M_CVN90 : !strcmp(model, "frigate") ? M_FRIGATE : !strcmp(model, "ks55") ? M_KS55
             : !strcmp(model, "nf24") ? M_NF24 : !strcmp(model, "dr7") ? M_DR7 : !strcmp(model, "ja46") ? M_JA46 : M_MISSILE;
    a->team = (int)numv(e, "team", 0);
    a->spawn = numv(e, "spawn", -1);
    a->end = numv(e, "end", -1);
    a->dead = numv(e, "dead", -1);
    a->nab = windows(json_get(e, "ab"), a->ab, 8);
    a->ntrail = windows(json_get(e, "trail"), a->trail, 4);
    a->nvapour = windows(json_get(e, "vapour"), a->vapour, 8);
    a->ncone = windows(json_get(e, "cone"), a->cone, 4);
    if (a->kind == K_CREW) {
      a->jersey = assets_hex(strv(e, "jersey"));
      a->helmet = assets_hex(strv(e, "helmet"));
      const Json *pz;
      JSON_FOR(pz, json_get(e, "poses")) {
        if (a->npose >= 8) break;
        a->pose_t[a->npose] = (float)json_num(json_at(pz, 0));
        const char *nm = json_str(json_at(pz, 1));
        a->pose[a->npose] = 0;
        for (int q = 0; nm && q < nposes; q++)
          if (!strcmp(pose_name[q], nm)) a->pose[a->npose] = q;
        a->npose++;
      }
    }
    const Json *sh = json_get(e, "ship");
    if (sh) {
      a->sx = numv(sh, "x", 0);
      a->sz = numv(sh, "z", 0);
      a->heading = numv(sh, "heading", 0);
      a->speed = numv(sh, "speed", 0);
      a->wake = numv(e, "wake", 0);
    }
    if (a->kind == K_STATIC) {
      a->lpos = vecv(json_get(e, "pos"));
      a->lheading = numv(e, "heading", 0);
    }
    parse_track(json_get(e, "keys"), a->kind == K_CREW ? 5 : 7, &a->tr);
    nact++;
  }
  /* statics attach to ships by id (second pass: the ship may come later) */
  int k = 0;
  JSON_FOR(e, json_get(S, "actors")) {
    if (k < nact) act[k].attach = find_actor(strv(e, "attach"));
    k++;
  }

  JSON_FOR(e, json_get(S, "emitters")) {
    if (nemit >= 8 || strcmp(strv(e, "type"), "steam")) continue;
    Emitter *m = &emit[nemit++];
    m->attach = find_actor(strv(e, "attach"));
    m->a = vecv(json_get(e, "a"));
    m->b = vecv(json_get(e, "b"));
    m->t0 = numv(e, "t0", 0);
    m->t1 = numv(e, "t1", 0);
    m->rate = numv(e, "rate", 5);
  }

  const Json *list = json_get(S, "flares");
  nflares = json_count(list);
  flares = calloc((size_t)(nflares + 1), sizeof *flares);
  k = 0;
  JSON_FOR(e, list) {
    FlareDef *f = &flares[k++];
    f->t = numv(e, "t", 0);
    f->p = vecv(json_get(e, "p"));
    f->v = vecv(json_get(e, "v"));
    f->life = numv(e, "life", 2);
  }

  list = json_get(S, "events");
  events = calloc((size_t)json_count(list) + 1, sizeof *events);
  JSON_FOR(e, list) {
    Event *v = &events[nevents];
    const char *type = strv(e, "type"), *name = strv(e, "name");
    v->type = !strcmp(type, "explode") ? EV_EXPLODE : !strcmp(type, "kill") ? EV_KILL : !strcmp(type, "splash") ? EV_SPLASH
            : !strcmp(type, "guns") ? EV_GUNS : EV_SOUND;
    v->t = numv(e, "t", numv(e, "t0", 0));
    v->t1 = numv(e, "t1", v->t);
    v->actor = find_actor(strv(e, "actor"));
    const Json *pos = json_get(e, "pos");
    if (pos) v->pos = vecv(pos);
    v->size = numv(e, "size", 1);
    v->sound = !strcmp(name, "klaxon") ? SND_KLAXON : !strcmp(name, "catapult") ? SND_CATAPULT : !strcmp(name, "missile") ? SND_MISSILE
             : !strcmp(name, "flares") ? SND_FLARES : !strcmp(name, "cloud") ? SND_CLOUD : !strcmp(name, "flyby") ? SND_FLYBY
             : !strcmp(name, "boom") ? SND_BOOM : SND_OTHER;
    v->vol = numv(e, "vol", 1);
    v->repeat = (int)numv(e, "repeat", 0);
    v->every = numv(e, "every", 0.9f);
    nevents++;
  }

  list = json_get(S, "shots");
  shots = calloc((size_t)json_count(list) + 1, sizeof *shots);
  JSON_FOR(e, list) {
    Shot *s = &shots[nshots];
    s->t0 = numv(e, "t0", 0);
    s->t1 = numv(e, "t1", 0);
    const Json *at = json_get(e, "attach"), *hud = json_get(e, "hud");
    const Json *vb = json_get(e, "vibe");
    s->attach = at ? find_actor(strv(at, "actor")) : -1;
    if (at) {
      s->off = vecv(json_get(at, "offset"));
      s->dir = vecv(json_get(at, "dir"));
      s->roll = numv(at, "roll", 1) != 0;
    }
    s->hud = hud ? find_actor(strv(hud, "actor")) : -1;
    s->target = hud ? find_actor(strv(hud, "target")) : -1;
    s->guns = hud && json_is_true(json_get(hud, "guns"));
    s->panel = hud && json_is_true(json_get(hud, "panel"));
    const Json *lk = hud ? json_get(hud, "lock") : NULL;
    s->lock0 = lk ? (float)json_at(lk, 0)->num : -1;
    s->lock1 = lk ? (float)json_at(lk, 1)->num : -1;
    s->vibe_a = vb ? numv(vb, "a", 0) : 0;
    s->vibe_f = vb ? numv(vb, "f", 1) : 1;
    s->shade = numv(e, "shade", 1);
    const Json *in = json_get(e, "in");
    const char *it = in ? strv(in, "type") : "";
    s->in = !strcmp(it, "dissolve") ? IN_DISSOLVE : !strcmp(it, "whip") ? IN_WHIP : IN_CUT;
    s->in_dur = in ? numv(in, "dur", 0.5f) : 0;
    s->in_dir = in ? numv(in, "dir", 1) : 1;
    s->radar = !strcmp(strv(e, "scene"), "radar");
    s->scene = s->radar || !strcmp(strv(e, "scene"), "cic");
    if (parse_track(json_get(e, "cam"), 8, &s->cam)) nshots++;
  }

  list = json_get(S, "cards");
  cards = calloc((size_t)json_count(list) + 1, sizeof *cards);
  JSON_FOR(e, list) {
    Card *c = &cards[ncards++];
    c->t0 = numv(e, "t0", 0);
    c->t1 = numv(e, "t1", 0);
    c->type = numv(e, "type", 0);
    c->blink = numv(e, "blink", 0);
    snprintf(c->text, sizeof c->text, "%s", strv(e, "text"));
    snprintf(c->speaker, sizeof c->speaker, "%s", strv(e, "speaker"));
    const char *st = strv(e, "style");
    c->style = !strcmp(st, "location") ? CS_LOCATION : !strcmp(st, "sub") ? CS_SUB : !strcmp(st, "alert") ? CS_ALERT
             : !strcmp(st, "alert-sub") ? CS_ALERT_SUB : !strcmp(st, "callsign") ? CS_CALLSIGN : !strcmp(st, "radio") ? CS_RADIO
             : !strcmp(st, "title") ? CS_TITLE : !strcmp(st, "subtitle") ? CS_SUBTITLE : !strcmp(st, "dialog") ? CS_DIALOG : CS_PRESS;
  }

  list = json_get(S, "fx");
  fxs = calloc((size_t)json_count(list) + 1, sizeof *fxs);
  JSON_FOR(e, list) {
    Fx *f = &fxs[nfx++];
    const char *type = strv(e, "type");
    f->type = !strcmp(type, "fade") ? FX_FADE : !strcmp(type, "flash") ? FX_FLASH : !strcmp(type, "letterbox") ? FX_LETTERBOX
            : !strcmp(type, "alarm") ? FX_ALARM : !strcmp(type, "radar") ? FX_RADAR : !strcmp(type, "shake") ? FX_SHAKE
            : !strcmp(type, "grade") ? FX_GRADE : !strcmp(type, "haze") ? FX_HAZE : !strcmp(type, "smoketitle") ? FX_SMOKETITLE
            : !strcmp(type, "glow") ? FX_GLOW : !strcmp(type, "seahaze") ? FX_SEAHAZE : FX_NONE;
    snprintf(f->text, sizeof f->text, "%s", strv(e, "text"));
    const Json *pp = json_get(e, "pos"), *fm = json_get(e, "form");
    if (json_count(pp) == 3) f->pos = vecv(pp);
    f->t0 = numv(e, "t0", 0); /* first: a smoke title without "form" forms from its own t0 */
    f->t1 = numv(e, "t1", 0);
    f->f0 = fm ? (float)json_num(json_at(fm, 0)) : f->t0;
    f->f1 = fm ? (float)json_num(json_at(fm, 1)) : f->t0 + 3;
    const Json *dz = json_get(e, "dissolve");
    f->d0 = dz ? (float)json_num(json_at(dz, 0)) : 1e9f;
    f->d1 = dz ? (float)json_num(json_at(dz, 1)) : 1e9f;
    f->seed = (uint32_t)numv(e, "seed", 1);
    f->warm = numv(e, "warm", 0);
    f->vignette = numv(e, "vignette", 0);
    f->grain = numv(e, "grain", 0);
    f->y0 = numv(e, "y0", 0);
    f->y1 = numv(e, "y1", 1);
    f->has_from = json_get(e, "from") != NULL;
    f->from = numv(e, "from", 0);
    f->to = numv(e, "to", 0);
    f->amount = numv(e, "amount", 1);
    f->period = numv(e, "period", 1);
    f->color = assets_hex(strv(e, "color"));
    f->contacts = (int)numv(e, "contacts", 0);
    f->bearing = numv(e, "bearing", 0);
  }
  parse_track(json_get(S, "warp"), 2, &warp);
  load_clouds(json_get(json_get(S, "env"), "extra_clouds"));
  JSON_FOR(e, json_get(json_get(S, "env"), "clear_clouds")) {
    if (nclear >= 8) break;
    for (int q = 0; q < 3; q++) clear_sky[nclear][q] = (float)json_num(json_at(e, q));
    nclear++;
  }
  json_free(S);
  json_free(A);
  ready = nshots > 0;
  plat_log("intro: %d actors, %d shots, %d cards", nact, nshots, ncards);
  return ready;
}

bool intro_available(void) { return ready; }
int intro_music(void) {
  /* Heartbeat when it is there (not on the floppy), else the cut the script was timed to */
  int hb = music_theme_index("heartbeat-intro");
  if (!strcmp(THEMES[hb].key, "heartbeat-intro") && music_recorded(hb)) return hb;
  return music_theme_index(music_key[0] ? music_key : "horizon-intro");
}
float intro_song_shift(void) { return song_shift; }

/* ---- run state ---------------------------------------------------------- */
enum { P_SMOKE, P_FIRE, P_FLASH, P_DEBRIS, P_STEAM, P_SPRAY, P_FSMOKE, P_TRAIL, P_BURN };
static const struct { uint32_t c0, c1; float a; } PK[9] = {
  { 0x969296, 0x5c5860, 0.55f }, { 0xffec96, 0xd24618, 1 }, { 0xfffae1, 0xffbe5a, 1 }, { 0x28221e, 0x1e1c1a, 1 },
  { 0xf5f0ec, 0xd2ced6, 0.5f },  { 0xf0f4fa, 0xc8d6e6, 0.2f }, { 0xd8d4d8, 0x8a868f, 0.42f },
  { 0x6e6664, 0xa8a0a0, 0.16f }, /* a shard's thin trail: many small puffs overlap into one streak */
  { 0x2c2624, 0x7c7476, 0.62f }, /* the burning wreck's oily smoke, black going grey */
};
typedef struct { int k; vec3 p, v; float life, max, s, g, d; } Part;
#define INTRO_PARTS 3200
static Part   parts[INTRO_PARTS];
static int    nparts;
typedef struct { vec3 p, v; float life; } Tracer;
static Tracer tracers[256];
static int    ntracers;
typedef struct { float t; vec3 l, r; } TrailPt;
static TrailPt trails[MAX_ACT][128];
static int     ntrail[MAX_ACT];
static vec3    lastPos[MAX_ACT];
static bool    hasLast[MAX_ACT];

static float  t_now, t_sim, t_film, wall, hold_t, hold_start; /* t_now: story time; t_film: the film clock, for the grain */
static int    ev_next;
static bool   holding, quiet, music_on;
static Rng    R;
typedef struct { float at; int sound; float vol; } Pending;
static Pending pending[16];
static int     npending;

static float rnd(void) { return (float)rng_f(&R); }

/* the music's duck at a kill: -4 dB for half a second (sfx.c eases it) */
#define DUCK_LEN  0.5f
#define DUCK_GAIN 0.631f
static float duck_end;
static bool  ducked;
static void duck_music(bool on) {
  if (on != ducked) sfx_duck_music(on ? DUCK_GAIN : 1);
  ducked = on;
}

static void reset(void) {
  nparts = ntracers = 0;
  memset(ntrail, 0, sizeof ntrail);
  memset(hasLast, 0, sizeof hasLast);
  t_now = t_sim = t_film = wall = duck_end = 0;
  duck_music(false);
  ev_next = 0;
  holding = quiet = false;
  npending = 0;
  R.state = 4711;
}

static void part(int k, vec3 p, vec3 v, float life, float size, float grow, float drag) {
  if (nparts >= INTRO_PARTS) return;
  parts[nparts++] = (Part){ k, p, v, life, life, size, grow, drag };
}

/* The film's random draws are taken one by one into locals, in the order the code reads: several draws in one
 * argument list or expression come in an order C leaves open (GCC on x86-64 takes them right to left, Clang left
 * to right), and the film came out different from one compiler to the next. tools/static names any such line. */
static vec3 rdir(float ybias) {
  float x = rnd() - 0.5f, y = rnd() - ybias, z = rnd() - 0.5f;
  return v3_norm(v3(x, y, z));
}

static void explode(vec3 pos, float size) {
  part(P_FLASH, pos, v3(0, 0, 0), 0.18f, 7 * size, 40 * size, 0.6f);
  for (int i = 0; i < 14 * size; i++) {
    vec3 d = rdir(0.4f);
    float v = 20 + 40 * rnd(), life = 0.45f + 0.6f * rnd(), r = 3 + 3 * size * rnd();
    part(P_FIRE, pos, v3_scale(d, v), life, r, 9, 1.6f);
  }
  for (int i = 0; i < 6 * size; i++) {
    vec3 d = rdir(0.3f);
    float off = 4 * rnd(), v = 12 + 20 * rnd(), life = 1.4f + 1.2f * rnd();
    part(P_FSMOKE, v3_add(pos, v3_scale(d, off)), v3_scale(d, v), life, 3 + 2 * size, 5, 1.2f);
  }
  if (size > 1) {
    for (int i = 0; i < 26; i++) {
      vec3 d = rdir(0.2f);
      float v = 60 + 90 * rnd(), life = 1.5f + 1.5f * rnd();
      part(P_DEBRIS, pos, v3_scale(d, v), life, 1.4f, 0, 0.3f);
    }
    for (int i = 0; i < 18 * size; i++) { /* the fireball's body turns to rolling black smoke */
      vec3 d = rdir(0.35f);
      float off = 3 + 7 * rnd(), v = 4 + 16 * rnd(), life = 2.2f + 2.2f * rnd(), r = 4 + 3 * rnd();
      part(P_BURN, v3_add(pos, v3_scale(d, off)), v3_scale(d, v), life, r, 5, 0.9f);
    }
    for (int i = 0; i < 70; i++) { /* white-hot sparks flung out past the fire */
      vec3 d = rdir(0.4f);
      float v = 70 + 120 * rnd(), life = 0.3f + 0.6f * rnd(), r = 0.35f + 0.35f * rnd();
      part(P_FIRE, pos, v3_scale(d, v), life, r, 0, 0.7f);
    }
  }
}

/* every sound of the film plays at this share of its level, under the music (0.45 until the music's ear-safe
 * mix came 5.1 dB down: 0.25 keeps the effects 18 dB under the music, as they were) */
#define IFX 0.25f
static void noise(float f0, float f1, float dur, float vol) { sfx_play(SND_NOISE, f0, f1, dur, vol * IFX); }
static void tone(SoundKind k, float f0, float f1, float dur, float vol) { sfx_play(k, f0, f1, dur, vol * IFX); }

/* a sonic boom heard from the sea, the classic "ba-boom": the N-wave's two shocks BOOM_GAP apart (the bow and the
 * tail shock), each a deep thump with a low roar on it, the second a little heavier; its reflection off the sea a
 * moment later, then the long roll of it across the water. The farther off (the lower vol), the softer the shocks. */
#define BOOM_GAP 0.21f
static void boom_shock(float a) {
  noise(2400, 80, 1.0f, 0.38f * a);
  tone(SND_SINE, 58, 28, 0.55f, 0.5f * a);
}
static void boom_wave(float a, float vol) { tone(SND_NWAVE, 140 + 140 * vol, BOOM_GAP, BOOM_GAP + 0.3f, a); }

static void play_sound(int s, float vol) {
  if (quiet) return;
  float g = 0.8f * 2.2f * vol; /* the boom's own level */
  switch (s) {
  case SND_KLAXON:    tone(SND_SQUARE, 330, 880, 0.7f, 0.1f); break;
  case SND_CATAPULT:  noise(3200, 160, 1.6f, 0.32f); tone(SND_SINE, 70, 38, 0.5f, 0.18f); break;
  case SND_MISSILE:   noise(3400, 600, 1.1f, 0.22f); break;
  case SND_FLARES:    noise(2400, 900, 0.5f, 0.12f); tone(SND_SQUARE, 1300, 500, 0.3f, 0.03f); break;
  case SND_EXPLODE:   noise(1100, 50, 1.6f, 0.42f); tone(SND_SINE, 90, 30, 0.7f, 0.3f); break;
  case SND_CLOUD:     noise(1400, 300, 1.8f, 0.15f); break;
  case SND_FLYBY:     noise(4200, 380, 1.5f, 0.34f); tone(SND_SINE, 160, 55, 1.2f, 0.2f); break;
  case SND_BOOM:      boom_wave(1.2f * g, vol); boom_shock(0.8f * g); break;
  case SND_BOOM_TAIL: boom_shock(1.1f * g); break;
  case SND_BOOM_ECHO: boom_wave(0.45f * g, 0.6f * vol); break;
  case SND_RUMBLE:    noise(260, 260, 2.6f, 0.18f * g); break;
  default: break;
  }
}

static vec3 ship_local(const Actor *s, float t, vec3 l) {
  float h = s->heading * D2R, c = cosf(h), n = sinf(h);
  vec3 o = v3(s->sx + n * s->speed * t, 0, s->sz + c * s->speed * t);
  return v3(o.x + l.x * c + l.z * n, l.y, o.z - l.x * n + l.z * c);
}

static bool actor_frame(const Actor *a, float t, Frame *fr) {
  if (a->kind == K_SHIP) {
    *fr = frame_of(a->heading, 0, 0);
    fr->pos = ship_local(a, t, v3(0, 0, 0));
    return true;
  }
  if (a->kind == K_STATIC) {
    if (a->attach < 0) return false;
    const Actor *s = &act[a->attach];
    *fr = frame_of(s->heading + a->lheading, 0, 0);
    fr->pos = ship_local(s, t, a->lpos);
    return true;
  }
  if (a->kind == K_CREW) {
    float k[5];
    if (a->attach < 0 || !a->tr.nseg) return false;
    const Seg *s0 = &a->tr.segs[0], *sl = &a->tr.segs[a->tr.nseg - 1];
    float tc = clampf(t, s0->k[0], sl->k[(sl->n - 1) * 5]);
    if (!sample(&a->tr, tc, k)) return false;
    const Actor *s = &act[a->attach];
    *fr = frame_of(s->heading + k[4], 0, 0);
    fr->pos = ship_local(s, t, v3(k[1], k[2], k[3]));
    return true;
  }
  if (a->spawn >= 0 && t < a->spawn) return false;
  if (a->end >= 0 && t > a->end) return false;
  float k[7];
  if (!a->tr.nseg || !sample(&a->tr, t, k)) return false;
  *fr = frame_of(k[4], k[5], k[6]);
  fr->pos = v3(k[1], k[2], k[3]);
  return true;
}

static bool flare_pos(const FlareDef *f, float t, vec3 *out) {
  float tau = t - f->t;
  if (tau < 0 || tau > f->life) return false;
  const float K = 1.6f, G = 5;
  float e = (1 - expf(-K * tau)) / K;
  *out = v3(f->p.x + f->v.x * e, f->p.y + f->v.y * e - 0.5f * G * tau * tau, f->p.z + f->v.z * e);
  return true;
}

static void step(float t, float dt) {
  for (; ev_next < nevents && events[ev_next].t <= t; ev_next++) {
    const Event *e = &events[ev_next];
    if (e->type == EV_EXPLODE) {
      explode(e->pos, e->size);
      play_sound(SND_EXPLODE, 1);
      if (e->size > 1 && !quiet) duck_end = t_film + DUCK_LEN; /* a kill: the music steps back so it is heard */
    } else if (e->type == EV_SOUND) {
      play_sound(e->sound, e->vol);
      for (int r = 1; e->sound == SND_KLAXON && r <= e->repeat && npending < 16; r++)
        pending[npending++] = (Pending){ e->t + r * e->every, SND_KLAXON, 1 };
      if (e->sound == SND_FLARES && npending < 15) {
        pending[npending++] = (Pending){ e->t + 0.16f, SND_FLARES, 1 };
        pending[npending++] = (Pending){ e->t + 0.32f, SND_FLARES, 1 };
      }
      if (e->sound == SND_BOOM && npending < 14) {
        pending[npending++] = (Pending){ e->t + 0.04f, SND_BOOM_ECHO, e->vol };
        pending[npending++] = (Pending){ e->t + BOOM_GAP, SND_BOOM_TAIL, e->vol };
        pending[npending++] = (Pending){ e->t + BOOM_GAP + 0.1f, SND_RUMBLE, e->vol };
      }
    } else if (e->type == EV_SPLASH) {
      for (int i = 0; i < 30; i++) {
        float x = (rnd() - 0.5f) * 30, y = 30 + 40 * rnd(), z = (rnd() - 0.5f) * 30, life = 2 + rnd();
        part(P_SPRAY, e->pos, v3(x, y, z), life, 8, 10, 0.5f);
      }
    }
  }
  for (int i = 0; i < npending;) {
    if (pending[i].at <= t) {
      play_sound(pending[i].sound, pending[i].vol);
      pending[i] = pending[--npending];
    } else i++;
  }
  /* guns: about 30 tracers a second from the nose */
  for (int i = 0; i < nevents; i++) {
    const Event *e = &events[i];
    Frame fr;
    if (e->type != EV_GUNS || t < e->t || t >= e->t1 || e->actor < 0 || !actor_frame(&act[e->actor], t, &fr)) continue;
    if (floorf(t * 30) != floorf((t - dt) * 30) && ntracers < 256) {
      float x = (rnd() - 0.5f) * 12, y = (rnd() - 0.5f) * 12, z = (rnd() - 0.5f) * 12;
      tracers[ntracers++] = (Tracer){ v3_add(fr.pos, v3_scale(fr.f, 9)), v3_add(v3_scale(fr.f, 950), v3(x, y, z)), 0.7f };
    }
  }
  for (int i = 0; i < ntracers;) {
    Tracer *b = &tracers[i];
    b->p = v3_add(b->p, v3_scale(b->v, dt));
    b->v.y -= 9.81f * dt;
    b->life -= dt;
    if (b->life <= 0) tracers[i] = tracers[--ntracers];
    else i++;
  }
  for (int i = 0; i < nact; i++) {
    const Actor *a = &act[i];
    Frame fr;
    if (a->kind == K_MISSILE) {
      if (!actor_frame(a, t, &fr)) continue;
      float x = (rnd() - 0.5f) * 3, y = (rnd() - 0.5f) * 3, z = (rnd() - 0.5f) * 3, life = 2.2f + rnd();
      part(P_SMOKE, v3_sub(fr.pos, v3_scale(fr.f, 2.5f)), v3(x, y, z), life, 2.2f, 5.5f, 1.5f);
      continue;
    }
    if (a->kind != K_JET) continue;
    if (!actor_frame(a, t, &fr)) {
      ntrail[i] = 0;
      continue;
    }
    if (a->dead >= 0 && t >= a->dead) {
      float x = (rnd() - 0.5f) * 8, y = (rnd() - 0.5f) * 8, z = (rnd() - 0.5f) * 8, life = 0.18f + 0.12f * rnd(), r = 3 + 2 * rnd();
      part(P_FIRE, fr.pos, v3(x, y, z), life, r, 6, 1.5f);
      float sx = (rnd() - 0.5f) * 4, sy = 2 + rnd() * 3, sz = (rnd() - 0.5f) * 4, slife = 0.5f + 0.3f * rnd();
      part(P_SMOKE, fr.pos, v3(sx, sy, sz), slife, 3, 5, 1.2f);
      continue;
    }
    float sp = hasLast[i] ? v3_dist(lastPos[i], fr.pos) / dt : 0;
    lastPos[i] = fr.pos;
    hasLast[i] = true;
    /* catapult steam while the jet runs down the deck */
    if (fr.pos.y < 23 && fr.pos.y > 19 && sp > 20 && sp < 130 && rnd() < 0.12f) {
      float x = (rnd() - 0.5f) * 4, y = 2 + 3 * rnd(), z = (rnd() - 0.5f) * 4, life = 0.9f + 0.6f * rnd();
      part(P_STEAM, v3_sub(fr.pos, v3(0, 1.2f, 0)), v3(x, y, z), life, 1.5f, 3.5f, 1.6f);
    }
    /* sea spray under a low jet in afterburner */
    float tail = a->model == M_NF24 ? 9.2f : a->model == M_DR7 ? 5.6f : a->model == M_JA46 ? 7.8f : JETS[0].tail;
    if (in_win(a->ab, a->nab, t) > 0 && fr.pos.y < 45 && fr.pos.y > 5 && rnd() < 0.9f) {
      vec3 tp = v3_sub(fr.pos, v3_scale(fr.f, tail + 6));
      float px = tp.x + (rnd() - 0.5f) * 5, pz = tp.z + (rnd() - 0.5f) * 5;
      float vx = (rnd() - 0.5f) * 6, vy = 3 + 5 * rnd(), vz = (rnd() - 0.5f) * 6, life = 0.8f + 0.6f * rnd();
      part(P_SPRAY, v3(px, 0.5f, pz), v3(vx, vy, vz), life, 4, 7, 0.9f); /* large and thin: small ones read as a row of white dots */
    }
    /* wingtip vapour */
    if (in_win(a->trail, a->ntrail, t) > 0) {
      float span = a->model == M_NF24 ? 9.4f : a->model == M_DR7 ? 5.6f : a->model == M_JA46 ? 5.2f : 7;
      if (ntrail[i] == 128) {
        memmove(trails[i], trails[i] + 1, sizeof(TrailPt) * 127);
        ntrail[i]--;
      }
      trails[i][ntrail[i]++] = (TrailPt){ t, v3_add(fr.pos, v3_scale(fr.r, -span)), v3_add(fr.pos, v3_scale(fr.r, span)) };
      while (ntrail[i] && t - trails[i][0].t > 1.6f) {
        memmove(trails[i], trails[i] + 1, sizeof(TrailPt) * (size_t)(ntrail[i] - 1));
        ntrail[i]--;
      }
    } else ntrail[i] = 0;
  }
  /* deck steam: puffs that hang in the air while the ship moves on */
  for (int i = 0; i < nemit; i++) {
    const Emitter *m = &emit[i];
    if (t < m->t0 || t >= m->t1 || m->attach < 0 || rnd() >= m->rate * dt) continue;
    vec3 l = v3_add(m->a, v3_scale(v3_sub(m->b, m->a), rnd()));
    float x = (rnd() - 0.5f) * 1.2f, y = 0.8f + rnd(), z = (rnd() - 0.5f) * 1.2f, life = 2.2f + 1.5f * rnd();
    part(P_STEAM, ship_local(&act[m->attach], t, l), v3(x, y, z), life, 1.6f, 2.2f, 1.2f);
  }
  for (int i = 0; i < nflares; i++) {
    vec3 p;
    if (flare_pos(&flares[i], t, &p) && rnd() < 0.7f) {
      float x = (rnd() - 0.5f) * 2, z = (rnd() - 0.5f) * 2;
      part(P_FSMOKE, p, v3(x * 0.4f, 0.6f, z * 0.4f), 1.8f, 0.7f, 1.4f, 1.2f);
    }
  }
  for (int i = 0; i < nparts;) {
    Part *q = &parts[i];
    q->v = v3_scale(q->v, expf(-q->d * dt));
    if (q->k == P_DEBRIS) {
      q->v.y -= 9.81f * dt;
      /* each shard trails smoke, a puff every 2.5 m or so whatever its speed: spaced by time they read as rows of dots */
      if (rnd() < v3_len(q->v) * dt / 2.5f) part(P_TRAIL, q->p, v3(0, 1, 0), 0.9f, 0.7f, 3.5f, 1.5f);
    }
    else if (q->k == P_SPRAY) q->v.y -= 6 * dt;
    else if (q->k == P_SMOKE || q->k == P_FSMOKE || q->k == P_BURN || q->k == P_TRAIL) q->v.y += 0.8f * dt;
    q->p = v3_add(q->p, v3_scale(q->v, dt));
    q->s += q->g * dt;
    q->life -= dt;
    if (q->life <= 0 || q->p.y < -2) parts[i] = parts[--nparts];
    else i++;
  }
}

/* The film clock to story time. The music, and so the picture's sync points, run on the film clock. */
static float warped(float m) {
  if (!warp.nseg) return m;
  const float *k = warp.segs[0].k;
  int n = warp.segs[0].n;
  if (m < k[0] || m >= k[(n - 1) * 2]) return m;
  for (int i = 1; i < n; i++)
    if (m < k[i * 2]) return k[i * 2 - 1] + (k[i * 2 + 1] - k[i * 2 - 1]) * (m - k[i * 2 - 2]) / fmaxf(k[i * 2] - k[i * 2 - 2], 1e-4f);
  return m;
}

static void advance(float to) {
  const float FIXED = 1.0f / 60;
  while (t_sim + FIXED <= to) {
    t_sim += FIXED;
    step(t_sim, FIXED);
  }
  t_now = to;
}

void intro_start(bool music) {
  reset();
  int m = assets_map_find("twin-isles");
  world_set_map(assets_map(m), TOD_DAWN, WX_SCATTERED);
  if (has_sun) LIGHT = env_sun; /* the low sunrise behind the carrier's island */
  r3d_extra_clouds(extra, nextra);
  r3d_clear_clouds((const float (*)[3])clear_sky, nclear);
  music_on = music;
  if (music) music_restart(intro_music());
  music_level(false);
}

float intro_time(void) { return wall; }
bool intro_holding(void) { return holding; }

static void enter_hold(bool skipped) {
  if (holding) return;
  holding = true;
  duck_end = 0;
  duck_music(false); /* never left down when the film stops */
  if (skipped && t_now < menu_at) {
    /* jump to the menu point quietly, keeping only what is on screen there */
    quiet = true;
    t_sim = fmaxf(t_sim, menu_at - 2);
    nparts = 0;
    ev_next = 0;
    while (ev_next < nevents && events[ev_next].t <= menu_at) ev_next++;
    advance(menu_at);
  }
  hold_t = fmaxf(t_now, menu_at);
  hold_start = wall;
}

void intro_skip(void) { enter_hold(true); }

bool intro_update(float dt) {
  wall += dt;
  if (!holding) {
    double mc = music_on ? music_clock() : -1;
    t_film = mc >= 0 ? (float)mc : wall;
    advance(warped(t_film));
    duck_music(t_film < duck_end);
    if (t_now >= menu_at) enter_hold(false);
  } else {
    /* the last shot keeps drifting at a quarter speed and stops at the end of its keys */
    float end = shots[nshots - 1].t1 - 0.05f;
    float t = fminf(hold_t + (wall - hold_start) / 4, end);
    if (t > t_sim) advance(t);
    else t_now = t;
    t_film = t_now;
  }
  r3d_extra_clouds(extra, nextra);
  r3d_clear_clouds((const float (*)[3])clear_sky, nclear);
  return holding;
}

/* ---- drawing -------------------------------------------------------------- */
static int active_fx(int type, float t, const Fx **out) {
  for (int i = 0; i < nfx; i++)
    if (fxs[i].type == type && t >= fxs[i].t0 && t < fxs[i].t1) {
      *out = &fxs[i];
      return 1;
    }
  return 0;
}

static uint32_t mixc(uint32_t a, uint32_t b, float t) {
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return rgb((uint8_t)(ar + (br - ar) * t), (uint8_t)(ag + (bg - ag) * t), (uint8_t)(ab + (bb - ab) * t));
}

static const Mesh *model_mesh(int m) {
  switch (m) {
  case M_CVN90:   return &MESH_CARRIER;
  case M_FRIGATE: return &MESH_TARGET[0][TGT_SHIP];
  case M_NF24:    return nf24.nf ? &nf24 : &JETS[0].mesh[0];
  case M_DR7:     return MESH_DRONE.nv ? &MESH_DRONE : &JETS[0].mesh[1];
  case M_KS55: {
    int k = jets_find("ks55");
    return &JETS[k >= 0 ? k : 0].mesh[1];
  }
  case M_JA46: {
    int k = jets_find("ja46");
    return &JETS[k >= 0 ? k : 0].mesh[0];
  }
  default: return NULL;
  }
}

static void draw_wakes(Framebuffer *fb, const View *V, float t) {
  for (int i = 0; i < nact; i++) {
    const Actor *a = &act[i];
    if (a->kind != K_SHIP || a->wake <= 0) continue;
    float Lh = a->model == M_CVN90 ? 160.0f : 62.0f, W0 = a->model == M_CVN90 ? 22.0f : 7.0f, k = a->wake;
    if (v3_dist(ship_local(a, t, v3(0, 0, 0)), V->cam.pos) > 30000) continue;
    /* the wake as soft ribbons, each piece running on from the last: puffs shrink to white dots in a wide view */
    const int segs = 40;
    float len0 = 1100 * k;
    for (int s = 0; s < segs; s++) {
      float u0 = (float)s / segs, u1 = (float)(s + 1) / segs, z0 = -Lh - len0 * u0, z1 = -Lh - len0 * u1;
      float w0 = W0 + u0 * 120 * k, w1 = W0 + u1 * 120 * k, fade = (1 - u0) * (1 - u0), r = (4 + 16 * u0) * k;
      r3d_seg(fb, V, ship_local(a, t, v3(0, 0.4f, z0)), ship_local(a, t, v3(0, 0.4f, z1)), hexc(0xe2e8ee), w0 * 1.1f, 0.07f * fade, 0);
      r3d_seg(fb, V, ship_local(a, t, v3(-w0, 0.4f, z0)), ship_local(a, t, v3(-w1, 0.4f, z1)), hexc(0xf0f4f8), r * 2, 0.13f * fade, 0);
      r3d_seg(fb, V, ship_local(a, t, v3(w0, 0.4f, z0)), ship_local(a, t, v3(w1, 0.4f, z1)), hexc(0xf0f4f8), r * 2, 0.13f * fade, 0);
    }
  }
}

/* ---- the deck crew: limbs are boxes from joint to joint ------------------ */
enum { LB_TORSO, LB_UPPER, LB_FORE, LB_HEAD, LB_THIGH, LB_SHIN, LB_N };
static const float LIMB[LB_N][3] = { /* length, width, depth */
  { 0.58f, 0.40f, 0.24f }, { 0.31f, 0.11f, 0.11f }, { 0.30f, 0.09f, 0.09f },
  { 0.26f, 0.22f, 0.24f }, { 0.46f, 0.15f, 0.16f }, { 0.46f, 0.12f, 0.12f },
};
typedef struct { uint32_t jersey, helmet; Mesh m[LB_N]; } CrewKit;
static CrewKit kits[8];
static int     nkits;

static void limb_mesh(Mesh *m, float L, float w, float d, uint32_t col, uint32_t end) {
  mesh_reset(m, L + w);
  int b = m->nv;
  for (int k = 0; k < 8; k++) mesh_vert(m, (k & 1 ? 0.5f : -0.5f) * w, (k & 2 ? 0.5f : -0.5f) * d, k & 4 ? L : 0);
  mesh_face4(m, b + 0, b + 1, b + 3, b + 2, col);
  mesh_face4(m, b + 4, b + 5, b + 7, b + 6, end);
  mesh_face4(m, b + 0, b + 1, b + 5, b + 4, col);
  mesh_face4(m, b + 2, b + 3, b + 7, b + 6, col);
  mesh_face4(m, b + 0, b + 2, b + 6, b + 4, col);
  mesh_face4(m, b + 1, b + 3, b + 7, b + 5, col);
}

static const CrewKit *kit_for(uint32_t jersey, uint32_t helmet) {
  for (int i = 0; i < nkits; i++)
    if (kits[i].jersey == jersey && kits[i].helmet == helmet) return &kits[i];
  if (nkits == 8) return &kits[0];
  CrewKit *k = &kits[nkits++];
  k->jersey = jersey;
  k->helmet = helmet;
  const uint32_t PANTS = 0x3a3e46, SKIN = 0xc89a78, BOOT = 0x22201e;
  limb_mesh(&k->m[LB_TORSO], LIMB[0][0], LIMB[0][1], LIMB[0][2], jersey, jersey);
  limb_mesh(&k->m[LB_UPPER], LIMB[1][0], LIMB[1][1], LIMB[1][2], jersey, jersey);
  limb_mesh(&k->m[LB_FORE], LIMB[2][0], LIMB[2][1], LIMB[2][2], jersey, SKIN);
  limb_mesh(&k->m[LB_HEAD], LIMB[3][0], LIMB[3][1], LIMB[3][2], SKIN, helmet);
  limb_mesh(&k->m[LB_THIGH], LIMB[4][0], LIMB[4][1], LIMB[4][2], PANTS, PANTS);
  limb_mesh(&k->m[LB_SHIN], LIMB[5][0], LIMB[5][1], LIMB[5][2], PANTS, BOOT);
  return k;
}

/* side must be square to dir: mesh x = width, y = depth, z = along the limb */
static float shade_now;
static void limb(Framebuffer *fb, const View *V, const Mesh *M, vec3 from, vec3 dir, vec3 side) {
  r3d_mesh(fb, V, M, from, side, v3_cross(dir, side), dir, 1, shade_now < 1 ? 0.1f : 1); /* a figure against the sun is a black shape */
}

/* The current pose, blended from the previous one over 0.25 s. */
static void crew_pose(const Actor *a, float t, float *out) {
  int cur = 0;
  for (int i = 0; i < a->npose; i++)
    if (t >= a->pose_t[i]) cur = i;
  const float *b = pose_val[a->npose ? a->pose[cur] : 0];
  float u = a->npose ? clampf((t - a->pose_t[cur]) / 0.25f, 0, 1) : 1;
  const float *p = cur > 0 ? pose_val[a->pose[cur - 1]] : b;
  for (int i = 0; i < POSE_N; i++) out[i] = nposes ? p[i] + (b[i] - p[i]) * u : 0;
}

static void draw_crew(Framebuffer *fb, const View *V, const Actor *a, const Frame *fr, float t, int seed) {
  const CrewKit *K = kit_for(a->jersey, a->helmet);
  float q[POSE_N];
  crew_pose(a, t, q);
  vec3 F = fr->f, Rt = fr->r, Up = v3(0, 1, 0);
  float crouch = q[0], walk = q[8], ph = t * 7.5f + seed * 1.7f;
  vec3 hip = v3_add(fr->pos, v3_scale(Up, 0.92f * (1 - crouch * 0.42f)));
  for (int s = -1; s <= 1; s += 2) { /* legs */
    float sw = sinf(ph) * 0.45f * walk * s, a1 = sw + crouch * 1.1f, a2 = a1 - crouch * 2.2f - fmaxf(0, -sinf(ph) * s) * 0.6f * walk;
    vec3 hs = v3_add(hip, v3_scale(Rt, 0.1f * s));
    vec3 d1 = v3_add(v3_scale(Up, -cosf(a1)), v3_scale(F, sinf(a1)));
    vec3 knee = v3_add(hs, v3_scale(d1, LIMB[LB_THIGH][0]));
    limb(fb, V, &K->m[LB_THIGH], hs, d1, Rt);
    limb(fb, V, &K->m[LB_SHIN], knee, v3_add(v3_scale(Up, -cosf(a2)), v3_scale(F, sinf(a2))), Rt);
  }
  float tp = q[1] * D2R;
  vec3 td = v3_add(v3_scale(Up, cosf(tp)), v3_scale(F, sinf(tp)));
  limb(fb, V, &K->m[LB_TORSO], hip, td, Rt);
  vec3 neck = v3_add(hip, v3_scale(td, LIMB[LB_TORSO][0]));
  limb(fb, V, &K->m[LB_HEAD], v3_add(neck, v3_scale(td, 0.04f)), td, Rt);
  for (int s = -1; s <= 1; s += 2) { /* arms: elevation from hanging down, azimuth from forward to that side */
    const float *A = s < 0 ? &q[2] : &q[5];
    float el = A[0] + 18 * sinf(ph + PI_F) * walk * s, az = A[1], elb = A[2];
    if (s > 0 && q[9] > 0) {
      az += 25 * sinf(t * 9) * q[9];
      el += 10 * cosf(t * 9) * q[9];
    }
    vec3 out = v3_add(v3_scale(F, cosf(az * D2R)), v3_scale(Rt, s * sinf(az * D2R)));
    vec3 sh = v3_add(v3_sub(neck, v3_scale(td, 0.06f)), v3_scale(Rt, 0.24f * s));
    vec3 d1 = v3_add(v3_scale(Up, -cosf(el * D2R)), v3_scale(out, sinf(el * D2R)));
    vec3 d2 = v3_add(v3_scale(Up, -cosf((el + elb) * D2R)), v3_scale(out, sinf((el + elb) * D2R)));
    vec3 side = v3_norm(v3_cross(out, Up));
    if (v3_len(side) < 0.5f) side = Rt;
    limb(fb, V, &K->m[LB_UPPER], sh, d1, side);
    limb(fb, V, &K->m[LB_FORE], v3_add(sh, v3_scale(d1, LIMB[LB_UPPER][0])), d2, side);
  }
}

/* Wing vapour from the g load and the vapour cone of a fast pass. */
static void draw_vapour(const View *V, const Actor *a, const Frame *fr, float t, int i) {
  float span = a->model == M_NF24 ? 8.5f : a->model == M_DR7 || a->model == M_JA46 ? 5 : 7;
  float v = in_win(a->vapour, a->nvapour, t), c = in_win(a->cone, a->ncone, t);
  r3d_sprite_kind(SPR_SOFT);
  if (v > 0)
    for (int k = 0; k < 10; k++) {
      float s = k % 2 ? 1 : -1, u = (k / 2) / 4.0f, fl = 0.7f + 0.3f * sinf(t * 23 + k * 1.9f + i);
      vec3 p = v3_add(v3_add(v3_add(fr->pos, v3_scale(fr->r, s * (1.4f + u * span * 0.75f))), v3_scale(fr->u, 0.7f)),
                      v3_scale(fr->f, -1.2f - u * 2.4f));
      r3d_ball(V, p, 1.3f + 0.6f * fl, hexc(0xf6f6fa), 0.32f * v * fl);
    }
  if (c > 0) {
    vec3 base = v3_add(fr->pos, v3_scale(fr->f, 2.2f));
    for (int j = 0; j < 4; j++)
      for (int k = 0; k < 12; k++) {
        float ang = k * PI_F / 6 + t * 3, rad = 2.3f + j * 1.3f;
        vec3 p = v3_add(v3_add(v3_add(base, v3_scale(fr->f, -j * 2.6f)), v3_scale(fr->r, cosf(ang) * rad)), v3_scale(fr->u, sinf(ang) * rad));
        r3d_ball(V, p, 1.2f + j * 0.5f, hexc(j ? 0xeef0f6 : 0xffffff), c * (j ? 0.28f - j * 0.06f : 0.5f));
      }
  }
}

static int hide_actor = -1;

static void draw_actors(Framebuffer *fb, const View *V, float t) {
  for (int i = 0; i < nact; i++) {
    const Actor *a = &act[i];
    Frame fr;
    if (i == hide_actor || !actor_frame(a, t, &fr)) continue;
    if (a->kind == K_CREW) {
      r3d_gloss(-1);
      if (v3_dist(fr.pos, V->cam.pos) < 900) draw_crew(fb, V, a, &fr, t, i);
      continue;
    }
    if (a->kind == K_MISSILE) {
      r3d_seg(fb, V, v3_add(fr.pos, v3_scale(fr.f, 1.8f)), v3_sub(fr.pos, v3_scale(fr.f, 1.8f)), hexc(0xe8e8ec), 0.35f, 1, 1);
      r3d_sprite_kind(SPR_GLOW);
      r3d_ball(V, v3_sub(fr.pos, v3_scale(fr.f, 2.2f)), 1.6f, hexc(0xffe6a0), 1);
      r3d_sprite_kind(SPR_SOFT);
      continue;
    }
    const Mesh *M = model_mesh(a->model);
    r3d_gloss(a->kind == K_JET || a->kind == K_STATIC ? 1.0f : -1.0f);
    if (!M) continue;
    bool dead = a->dead >= 0 && t >= a->dead;
    r3d_mesh(fb, V, M, fr.pos, fr.r, fr.u, fr.f, 1, (dead ? 0.35f : 1) * (a->kind == K_SHIP ? 1 : shade_now));
    if (a->kind != K_JET || dead) continue;
    draw_vapour(V, a, &fr, t, i);
    float ab = in_win(a->ab, a->nab, t);
    if (ab > 0) {
      /* two nozzles, or the drone's one square vectoring nozzle on the centre line */
      bool one = a->model == M_DR7 || a->model == M_JA46;
      float tail = a->model == M_NF24 ? 9.2f : a->model == M_JA46 ? 8.4f : one ? 5.6f : 9.7f, fl = 0.85f + 0.3f * sinf(t * 61 + i) * sinf(t * 37);
      for (int s = -1; s <= 1; s += 2) {
        if (one && s > 0) break;
        vec3 n0 = v3_add(v3_add(fr.pos, v3_scale(fr.f, -tail + 0.2f)), v3_scale(fr.r, one ? 0 : 0.85f * s));
        r3d_seg(fb, V, n0, v3_add(n0, v3_scale(fr.f, -(4 + 6 * ab) * fl)), hexc(0xff9a3a), 1.3f, 0.8f * ab, 1);
        r3d_seg(fb, V, n0, v3_add(n0, v3_scale(fr.f, -(2.5f + 3 * ab) * fl)), hexc(0xfff2c8), 0.7f, 0.95f, 1);
        r3d_sprite_kind(SPR_GLOW); /* the hot nozzle itself */
        r3d_ball(V, n0, (0.8f + 0.7f * ab) * fl, hexc(0xffa850), 0.7f);
        r3d_sprite_kind(SPR_SOFT);
      }
    }
  }
  for (int i = 0; i < nact; i++)
    for (int k = 1; k < ntrail[i]; k++) {
      float al = 0.55f * (1 - (t_now - trails[i][k].t) / 1.6f);
      if (al <= 0) continue;
      r3d_seg(fb, V, trails[i][k - 1].l, trails[i][k].l, hexc(0xf4f2f6), 0.5f, al, 1);
      r3d_seg(fb, V, trails[i][k - 1].r, trails[i][k].r, hexc(0xf4f2f6), 0.5f, al, 1);
    }
  for (int i = 0; i < nflares; i++) {
    vec3 p;
    if (!flare_pos(&flares[i], t, &p)) continue;
    float k = 1 - (t - flares[i].t) / flares[i].life;
    r3d_sprite_kind(SPR_GLOW);
    r3d_ball(V, p, 1.6f * k + 0.9f, hexc(0xffd890), 0.8f * k);
    r3d_ball(V, p, 0.6f + 0.06f * sinf(t * 50 + flares[i].t), hexc(0xffffff), 1); /* the hot core: a wider beat strobes at 60 frames */
    r3d_sprite_kind(SPR_SOFT);
  }
  for (int i = 0; i < ntracers; i++)
    r3d_seg(fb, V, tracers[i].p, v3_sub(tracers[i].p, v3_scale(v3_norm(tracers[i].v), 14)), hexc(0xffe680), 0.5f, 0.9f, 1);
  for (int i = 0; i < nparts; i++) {
    const Part *q = &parts[i];
    if (i == nparts - 1) r3d_sprite_kind(SPR_SOFT);
    float u = 1 - q->life / q->max;
    if (q->k == P_DEBRIS) { /* a tumbling shard */
      float w = u * 14 + (float)i;
      /* a chunk, not a needle: short and broad; it glows at first, and goes out before it ends */
      vec3 d = v3_scale(v3(sinf(w), cosf(w * 0.7f), sinf(w * 1.3f + 1)), q->s * (0.3f + 0.2f * (float)(i % 3)));
      float hot = clampf(1 - u / 0.3f, 0, 1);
      r3d_seg(fb, V, v3_sub(q->p, d), v3_add(q->p, d), mixc(0x2a2624, 0xff9440, hot), 0.7f + 0.25f * (float)(i % 4), clampf((1 - u) * 5, 0, 1), 1);
      if (hot > 0) {
        r3d_sprite_kind(SPR_GLOW);
        r3d_ball(V, q->p, 1.6f, hexc(0xff9440), 0.7f * hot);
      }
      continue;
    }
    float a = PK[q->k].a * (q->k == P_FIRE || q->k == P_FLASH ? 1 - u : (1 - u) * fminf(1, u * 6 + 0.3f));
    r3d_sprite_kind(q->k == P_FIRE || q->k == P_FLASH ? SPR_GLOW : SPR_SOFT);
    r3d_ball(V, q->p, q->s, mixc(PK[q->k].c0, PK[q->k].c1, u), a);
  }
}

static int text_scale(float px) {
  int s = (int)roundf(px / 8);
  return s < 1 ? 1 : s;
}

static void typed(const Card *c, float t, char *out, size_t n) {
  snprintf(out, n, "%s", c->text);
  if (c->type > 0) {
    float kf = floorf((t - c->t0) * c->type);
    int k = kf > 0 ? (kf < 100000 ? (int)kf : 100000) : 0; /* characters typed so far */
    utf8_cut(out, n, k); /* k characters, not k bytes: a letter is never cut in half (it would draw as '?') */
  }
}

static void draw_radar(Framebuffer *fb, const Fx *f, float t, float u) {
  int W = fb->w, H = fb->h;
  float rad = roundf(fminf((float)W, (float)H) * 0.16f), cx = W - rad - fmaxf(14, W * 0.04f), cy = H - rad - fmaxf(14, H * 0.20f); /* clear of the lower letterbox bar, its caption too */
  float al = clampf(u * 6, 0, 1) * clampf((1 - u) * 8, 0, 1);
  fb_clip_circle(fb, cx, cy, rad);
  fb_rect_a(fb, (int)(cx - rad), (int)(cy - rad), (int)(2 * rad + 1), (int)(2 * rad + 1), rgb(4, 22, 10), 0.85f * al);
  fb_clip_circle_off(fb);
  for (int k = 1; k <= 3; k++) fb_ring(fb, cx, cy, rad * k / 3, rgb(120, 240, 140), 1);
  fb_line(fb, cx - rad, cy, cx + rad, cy, rgb(120, 240, 140), 1, 0.5f * al);
  fb_line(fb, cx, cy - rad, cx, cy + rad, rgb(120, 240, 140), 1, 0.5f * al);
  float sw = fmodf(t * 2.4f, 6.2831853f);
  for (int k = 0; k < 8; k++) {
    float a2 = sw - k * 0.06f;
    fb_line(fb, cx, cy, cx + sinf(a2) * rad, cy - cosf(a2) * rad, rgb(120, 255, 150), 1, al * (0.9f - k * 0.1f));
  }
  for (int i = 0; i < f->contacts; i++) {
    float b = (f->bearing + (i - 1.5f) * 6) * D2R, rr = rad * (0.85f - 0.25f * u - (i % 2) * 0.05f);
    fb_rect_a(fb, (int)(cx + sinf(b) * rr - 2), (int)(cy - cosf(b) * rr - 2), 4, 4, ((int)(t * 4) % 2) ? hexc(0xff5a48) : hexc(0xffb0a0), al);
  }
  fb_rect_a(fb, (int)cx - 2, (int)cy - 2, 4, 4, hexc(0x9cf0a8), al);
  fb_text(fb, "AIR SEARCH", cx, cy + rad + fmaxf(8, rad * 0.14f), text_scale(rad * 0.11f), hexc(0x9cf0a8), ALIGN_CENTER, 1);
}

static void draw_title(Framebuffer *fb, float dt) {
  int W = fb->w, H = fb->h;
  float u0 = fminf((float)W, H * 1.6f), k = clampf(dt / 0.22f, 0, 1), sc = lerpf(1.9f, 1, 1 - powf(1 - k, 3));
  int s = text_scale(u0 * 0.12f * sc / 1.0f);
  while (s > 1 && text_width(GAME_LOGO, s) > W * 0.9f) s--;
  float x = W / 2.0f, y = H * 0.44f;
  int off = s / 2 > 1 ? s / 2 : 1;
  if (k < 0.05f) return;
  fb_text(fb, GAME_LOGO, x + off * 2, y + off * 2, s, 0xFF000000u, ALIGN_CENTER, 0);
  fb_text(fb, GAME_LOGO, x + off, y + off, s, hexc(0x6C5EB5), ALIGN_CENTER, 0);
  fb_text(fb, GAME_LOGO, x, y, s, hexc(0xfff1d8), ALIGN_CENTER, 0);
  /* a glint sweeping across once */
  float gx = (dt - 0.3f) / 0.9f;
  if (gx > 0 && gx < 1) {
    int w = text_width(GAME_LOGO, s), h = 7 * s;
    float px = x - w / 2.0f + w * gx;
    float xy[8] = { px - h * 0.3f, y - h * 0.6f, px + h * 0.05f, y - h * 0.6f, px - h * 0.15f, y + h * 0.6f, px - h * 0.5f, y + h * 0.6f };
    fb_poly(fb, xy, 4, 0xFFFFFFFFu, 0.3f, 0);
  }
  fb_text(fb, GAME_SUBTITLE, x, y + 7 * s * 0.5f + fmaxf(8, u0 * 0.03f), text_scale(u0 * 0.022f), hexc(0xcdc6ff), ALIGN_CENTER, 1);
}

static float letterbox_at(float t) {
  float lb = 0;
  for (int i = 0; i < nfx; i++) {
    const Fx *e = &fxs[i];
    if (e->type != FX_LETTERBOX || t < e->t0) continue;
    if (t < e->t1) {
      float u = (t - e->t0) / (e->t1 - e->t0);
      lb = e->has_from ? lerpf(e->from, e->to, u * u * (3 - 2 * u)) : e->amount;
    } else if (e->has_from) lb = e->to;
  }
  return lb;
}
static int bar_height(int W, int H, float t) { return (int)roundf(fmaxf(fmaxf(0, (H - W / 2.35f) / 2), H * 0.09f) * letterbox_at(t)); }

/* panel: a cockpit shot with the instrument panel where the lower bar was;
 * the callsign and radio lines move to the upper bar */
static void draw_overlay(Framebuffer *fb, float t, bool panel) {
  int W = fb->w, H = fb->h;
  const Fx *f;
  fb_clip_reset(fb);
  /* red alarm vignette */
  if (active_fx(FX_ALARM, t, &f)) {
    float k = 0.5f + 0.5f * sinf((t - f->t0) / f->period * 6.2831853f - PI_F / 2);
    float r0 = fminf((float)W, (float)H) * 0.25f, r1 = fmaxf((float)W, (float)H) * 0.75f;
    for (int y = 0; y < H; y += 1) {
      uint32_t *row = fb->px + (size_t)y * W;
      float dy = y + 0.5f - H / 2.0f;
      for (int x = 0; x < W; x++) {
        float dx = x + 0.5f - W / 2.0f, d = sqrtf(dx * dx + dy * dy);
        float a = clampf((d - r0) / (r1 - r0), 0, 1) * 0.45f * k;
        if (a <= 0.004f) continue;
        (void)row;
        fb_blend_px(fb, x, y, rgb(255, 30, 20), a);
      }
    }
  }
  if (active_fx(FX_RADAR, t, &f)) draw_radar(fb, f, t, (t - f->t0) / (f->t1 - f->t0));
  if (active_fx(FX_FLASH, t, &f)) fb_rect_a(fb, 0, 0, W, H, hexc(f->color), clampf(lerpf(f->from, f->to, (t - f->t0) / (f->t1 - f->t0)), 0, 1));
  /* letterbox */
  int barH = bar_height(W, H, t);
  if (barH > 0) {
    fb_rect(fb, 0, 0, W, barH, 0xFF000000u);
    if (!panel) fb_rect(fb, 0, H - barH, W, barH, 0xFF000000u);
  }
  float lowY = panel ? barH / 2.0f : H - barH / 2.0f; /* the line in the bar for callsigns and radio */
  /* cards */
  float u0 = fminf((float)W, H * 1.6f), sh = (float)H;
  char s[80];
  for (int i = 0; i < ncards; i++) {
    const Card *c = &cards[i];
    if (t < c->t0 || t >= c->t1) continue;
    if (c->blink > 0 && ((int)floorf((t - c->t0) / c->blink)) % 2 == 1) continue;
    if (holding && (c->style == CS_TITLE || c->style == CS_PRESS)) continue;
    typed(c, t, s, sizeof s);
    switch (c->style) {
    case CS_LOCATION: {
      int sc = text_scale(u0 * 0.042f);
      float y = H - barH - sh * 0.2f;
      fb_text(fb, s, W / 2.0f, y, sc, hexc(0xfbe9cc), ALIGN_CENTER, 1);
      float full = (float)text_width(c->text, sc), k = clampf((t - c->t0) / 1.2f, 0, 1);
      fb_rect_a(fb, (int)(W / 2.0f - full / 2 * k), (int)(y + 5 * sc), (int)(full * k), sc > 1 ? sc / 2 + 1 : 1, hexc(0xfbe9cc), 0.8f);
      break;
    }
    case CS_SUB: fb_text(fb, s, W / 2.0f, H - barH - sh * 0.2f + u0 * 0.07f, text_scale(u0 * 0.02f), hexc(0xe2c49e), ALIGN_CENTER, 1); break;
    case CS_ALERT: {
      int sc = text_scale(u0 * 0.06f);
      float y = barH + sh * 0.26f;
      int w = text_width(s, sc);
      fb_rect_stroke(fb, W / 2.0f - w / 2.0f - 4 * sc, y - 7 * sc, (float)w + 8 * sc, 14.0f * sc, hexc(0xff4a3a), (float)(sc > 1 ? sc / 2 + 1 : 1));
      fb_text(fb, s, W / 2.0f, y, sc, hexc(0xff4a3a), ALIGN_CENTER, 1);
      break;
    }
    case CS_ALERT_SUB: fb_text(fb, s, W / 2.0f, barH + sh * 0.26f + u0 * 0.07f, text_scale(u0 * 0.022f), hexc(0xffd25a), ALIGN_CENTER, 1); break;
    case CS_CALLSIGN:
      fb_text(fb, s, fmaxf(12, W * 0.04f), barH ? lowY : H - u0 * 0.05f, text_scale(fminf(u0 * 0.02f, (barH ? barH : 40) * 0.42f)),
              hexc(0xa8f08c), ALIGN_LEFT, 1);
      break;
    case CS_RADIO: {
      char q[90];
      snprintf(q, sizeof q, "\"%s\"", s);
      fb_text(fb, q, W / 2.0f, barH ? lowY : H - u0 * 0.06f, text_scale(fminf(u0 * 0.024f, (barH ? barH : 40) * 0.48f)),
              hexc(0xffffff), ALIGN_CENTER, 1);
      break;
    }
    case CS_DIALOG: {
      /* a line spoken in the control room: the speaker in amber, the words in white, centred as one line in the lower bar */
      char who[24];
      snprintf(who, sizeof who, "%s:  ", c->speaker);
      /* the words appear as they are spoken (the figure engine's speech timeline for this card) */
      FigSpeech sp;
      fig_speech(c->text, c->t0, c->t1, &sp);
      fig_spoken_upto(c->text, fig_words_shown(&sp, t), s, sizeof s);
      int sc = text_scale(fminf(u0 * 0.024f, (barH ? barH : 40) * 0.46f));
      float y = barH ? lowY : H - u0 * 0.06f, w0 = (float)text_width(who, sc), w1 = (float)text_width(c->text, sc);
      float x = W / 2.0f - (w0 + w1) / 2; /* centred on the whole line, so it stays put while it types */
      fb_text(fb, who, x, y, sc, hexc(0xffd25a), ALIGN_LEFT, 1);
      fb_text(fb, s, x + w0, y, sc, hexc(0xffffff), ALIGN_LEFT, 1);
      break;
    }
    case CS_TITLE: draw_title(fb, t - c->t0); break;
    case CS_PRESS: fb_text(fb, s, W / 2.0f, H * 0.5f + u0 * 0.11f, text_scale(u0 * 0.022f), hexc(dawn_title ? 0x3c2f78 : 0xfbe9cc), ALIGN_CENTER, !dawn_title); break;
    case CS_SUBTITLE: {
      /* under the smoke title, fading in */
      float k = clampf((t - c->t0) / 1.2f, 0, 1);
      /* warm white with a shadow, so it reads over the sea and the sky alike (the deep violet it had was lost on
       * the grey sea under the finale's title); it brightens in from a dim grey */
      int sc = text_scale(u0 * 0.022f);
      if (k > 0.05f) fb_text(fb, s, W / 2.0f, H * 0.42f + u0 * 0.115f, sc, mixc(0x6a6470, 0xf6ead8, k), ALIGN_CENTER, 1);
      break;
    }
    }
  }
  if (active_fx(FX_FADE, t, &f)) fb_rect_a(fb, 0, 0, W, H, hexc(f->color), clampf(lerpf(f->from, f->to, (t - f->t0) / (f->t1 - f->t0)), 0, 1));
  if (!holding && wall < 5) fb_text(fb, "ANY KEY SKIPS", W - fmaxf(10, W * 0.03f), barH ? barH / 2.0f : 14, text_scale(fmaxf(8, u0 * 0.014f)), hexc(0xcdc6ff), ALIGN_RIGHT, 1);
}

/* ---- the game's cockpit (version 5 `hud.panel`) ---------------------------
 * The cockpit shots wear the game's own head-up display and instrument panel
 * (hud.c, cockpit.c). A stand-in Game holds the script's live jets as planes,
 * so the HUD brackets them and the panel reads them as it does in a match. */
static Game *cg;

static int panel_height(int W, int H) { return (int)roundf(fmaxf(52, fminf(H * 0.24f, W * 0.21f))); }

static bool cockpit_plane(Plane *p, int idx, const Actor *a, float t) {
  Frame fr, f0, f1;
  if (!actor_frame(a, t, &fr) || (a->dead >= 0 && t >= a->dead)) return false;
  memset(p, 0, sizeof *p);
  p->idx = idx;
  p->team = a->team;
  p->hidx = -1;
  int j = jets_find(a->model == M_KS55 ? "ks55" : a->model == M_JA46 ? "ja46" : "nf24");
  p->jet = j >= 0 ? j : 0;
  { /* the call sign (or the id), at most what the name holds: copied, not printed, so no compiler sees a cut */
    const char *nm = a->callsign[0] ? a->callsign : a->id;
    const char *end = memchr(nm, 0, sizeof p->name - 1); /* (strnlen is POSIX, not C11) */
    size_t len = end ? (size_t)(end - nm) : sizeof p->name - 1;
    memcpy(p->name, nm, len);
    p->name[len] = 0;
  }
  p->pos = fr.pos;
  p->r = fr.r;
  p->u = fr.u;
  p->f = fr.f;
  const float d = 0.15f; /* wide enough to smooth over the keys' joints */
  bool b0 = actor_frame(a, t - d, &f0), b1 = actor_frame(a, t + d, &f1);
  if (!b0) f0 = fr;
  if (!b1) f1 = fr;
  float h = (b0 && b1) ? 2 * d : d;
  p->vel = v3_scale(v3_sub(f1.pos, f0.pos), 1 / h);
  p->speed = v3_len(p->vel);
  if (b0 && b1) { /* the load factor from the path's curvature, as the pilot feels it */
    vec3 acc = v3_scale(v3_add(v3_sub(f1.pos, v3_scale(fr.pos, 2)), f0.pos), 1 / (d * d));
    p->gload = clampf(v3_dot(v3_add(acc, v3(0, 9.81f, 0)), fr.u) / 9.81f, -3, 9);
  } else p->gload = 1;
  p->gmeter = p->gload;
  p->throttle = in_win(a->ab, a->nab, t) > 0 ? 1 : 0.8f;
  p->fuel = 74;
  p->health = 100;
  p->ammo = 100;
  p->missiles = 4;
  p->flares = 24;
  p->lives = 1;
  p->alive = true;
  p->lockTarget.kind = REF_NONE;
  return true;
}

static void draw_game_cockpit(Framebuffer *fb, const Camera *cam, float F, const Shot *sh, float t, int barH) {
  int W = fb->w, H = fb->h, ph = panel_height(W, H), pt = H - ph;
  if (!cg && !(cg = calloc(1, sizeof *cg))) return;
  Game *g = cg;
  g->time = 100 + t; /* past the game's opening hints */
  g->nplanes = 0;
  g->nmissiles = 0;
  int me = -1, tgt = -1;
  for (int i = 0; i < nact && g->nplanes < MAX_PLANES; i++) {
    if (act[i].kind != K_JET || !cockpit_plane(&g->planes[g->nplanes], g->nplanes, &act[i], t)) continue;
    if (i != sh->hud && act[i].team == act[sh->hud].team) continue; /* the film's HUD tags no wingman: he stays visible */
    if (i == sh->hud) me = g->nplanes;
    if (i == sh->target) tgt = g->nplanes;
    g->nplanes++;
  }
  if (me < 0) return;
  Plane *p = &g->planes[me];
  p->hidx = 0;
  for (int k = 0; k < 2; k++) g->tankers[k].gone = true; /* none in the film */
  if (sh->lock0 >= 0 && t >= sh->lock0 && tgt >= 0) {
    p->lockTarget = ref_plane(tgt);
    p->lockT = clampf((t - sh->lock0) / fmaxf(0.01f, sh->lock1 - sh->lock0), 0, 1) * LOCK_TIME;
    p->locked = t >= sh->lock1;
  }
  /* the canopy: the bow arching over the top with its mirrors, the side rails */
  uint32_t frame = hexc(0x16181a), edge = hexc(0x2c3034);
  float cx = W / 2.0f, top = barH + H * 0.04f;
  for (int x = 0; x < W; x += 2) {
    float u = (x - cx) / (W / 2.0f), y = top + u * u * H * 0.2f;
    fb_rect(fb, x, 0, 2, (int)fmaxf(0, y), frame);
    fb_rect(fb, x, (int)y, 2, (int)fmaxf(1, H * 0.004f), edge);
  }
  for (int m = -1; m <= 1; m += 2) { /* rear-view mirrors on the bow */
    float mx = cx + m * W * 0.3f, u = (mx - cx) / (W / 2.0f), my = top + u * u * H * 0.2f;
    fb_rect(fb, (int)(mx - W * 0.035f), (int)(my - H * 0.005f), (int)(W * 0.07f), (int)(H * 0.028f), hexc(0x0c0d0e));
    fb_rect_a(fb, (int)(mx - W * 0.032f), (int)(my - H * 0.002f), (int)(W * 0.064f), (int)(H * 0.02f), hexc(0x6a7a8c), 0.55f);
  }
  float rail[8] = { 0, 0, W * 0.075f, 0, W * 0.02f, (float)pt, 0, (float)pt };
  fb_poly(fb, rail, 4, frame, 1, 0);
  float rail2[8] = { (float)W, 0, W * 0.925f, 0, W * 0.98f, (float)pt, (float)W, (float)pt };
  fb_poly(fb, rail2, 4, frame, 1, 0);
  /* the HUD combiner on its posts, a faint green glass over the gunsight */
  float hw = W * 0.12f, g0 = H * 0.26f, g1 = pt - H * 0.045f;
  fb_rect_a(fb, (int)(cx - hw), (int)g0, (int)(2 * hw), (int)(g1 - g0), hexc(0x9cffb0), 0.045f);
  fb_line(fb, cx - hw, g0, cx + hw, g0, hexc(0x9cffb0), 1, 0.25f);
  fb_line(fb, cx - hw, g0, cx - hw * 1.06f, g1, frame, fmaxf(1, H * 0.003f), 0.8f);
  fb_line(fb, cx + hw, g0, cx + hw * 1.06f, g1, frame, fmaxf(1, H * 0.003f), 0.8f);
  /* the game's head-up display, over the picture above the panel */
  float vy = top + H * 0.015f; /* under the canopy bow */
  HudView hv = { cam->pos, cam->r, cam->u, cam->f, 0, vy, (float)W, pt - vy, cx, H / 2.0f, F };
  hud_draw(fb, g, p, &hv, false);
  /* the glare shield, then the jet's own instrument panel below it */
  float gs[10] = { 0, (float)pt + 1, 0, pt - H * 0.02f, cx, pt - H * 0.05f, (float)W, pt - H * 0.02f, (float)W, (float)pt + 1 };
  fb_poly(fb, gs, 5, hexc(0x0e0f10), 1, 0);
  fb_rect(fb, (int)(cx - hw * 1.1f), (int)(pt - H * 0.055f), (int)(hw * 2.2f), (int)fmaxf(2, H * 0.01f), hexc(0x2a2c30));
  fb_clip(fb, 0, pt, W, ph);
  hud_panel(fb, g, p, 0, (float)pt, (float)W, (float)ph);
  fb_clip_reset(fb);
}

/* ---- the cockpit view: canopy frame, glare shield and head-up display ------ */
static void draw_cockpit(Framebuffer *fb, const Camera *cam, float F, const Shot *sh, float t) {
  int W = fb->w, H = fb->h;
  Frame fr;
  if (!actor_frame(&act[sh->hud], t, &fr)) return;
  float cx = W / 2.0f, cy = H / 2.0f, u0 = fminf((float)W, H * 1.6f);
  uint32_t G = hexc(0x7dff8c), frame = hexc(0x16181a);
  int ks = text_scale(u0 * 0.016f);
  /* the HUD: a combiner glass box in the middle */
  float hw = u0 * 0.21f, hh = H * 0.3f;
  float pitch = asinf(clampf(fr.f.y, -1, 1)) / D2R, bank = atan2f(-fr.r.y, fr.u.y);
  float ppd = F * D2R; /* pixels per degree, conformal */
  fb_clip(fb, (int)(cx - hw), (int)(cy - hh), (int)(2 * hw), (int)(2 * hh));
  float cb = cosf(bank), sb = sinf(bank);
  for (int p = -30; p <= 30; p += 5) { /* pitch ladder */
    float off = (pitch - p) * ppd, len = p ? u0 * 0.05f : u0 * 0.16f;
    float bx0 = -len, bx1 = len, by = off;
    float x0 = cx + cb * bx0 - sb * by, y0 = cy + sb * bx0 + cb * by, x1 = cx + cb * bx1 - sb * by, y1 = cy + sb * bx1 + cb * by;
    fb_line(fb, x0, y0, x1, y1, G, 1, p ? 0.7f : 0.9f);
    if (p && p % 10 == 0) {
      char b[8];
      snprintf(b, sizeof b, "%d", p);
      fb_text(fb, b, x1 + 4 * ks * cb, y1 + 4 * ks * sb, ks, G, ALIGN_LEFT, 0);
    }
  }
  /* flight path marker */
  float r = 4.0f * ks;
  fb_ring(fb, cx, cy + u0 * 0.01f, r, G, 1);
  fb_line(fb, cx - 3 * r, cy + u0 * 0.01f, cx - r, cy + u0 * 0.01f, G, 1, 1);
  fb_line(fb, cx + r, cy + u0 * 0.01f, cx + 3 * r, cy + u0 * 0.01f, G, 1, 1);
  fb_clip_reset(fb);
  /* heading, speed, altitude */
  char b[24];
  float hdg = fmodf(atan2f(fr.f.x, fr.f.z) / D2R + 360, 360);
  Frame f0;
  float spd = actor_frame(&act[sh->hud], t - 0.05f, &f0) ? v3_dist(fr.pos, f0.pos) / 0.05f : 0;
  snprintf(b, sizeof b, "%03d", (int)hdg);
  fb_text(fb, b, cx, cy - hh + 6 * ks, ks, G, ALIGN_CENTER, 0);
  snprintf(b, sizeof b, "%d", (int)roundf(spd * KT_PER_MPS));
  fb_rect_stroke(fb, cx - hw - 2, cy - 6 * ks, 28.0f * ks, 12.0f * ks, G, 1);
  fb_text(fb, b, cx - hw + 12 * ks, cy, ks, G, ALIGN_CENTER, 0);
  snprintf(b, sizeof b, "%d", (int)(fr.pos.y * FT_PER_M / 10) * 10);
  fb_rect_stroke(fb, cx + hw - 26 * ks, cy - 6 * ks, 28.0f * ks, 12.0f * ks, G, 1);
  fb_text(fb, b, cx + hw - 12 * ks, cy, ks, G, ALIGN_CENTER, 0);
  /* the target: a box with LOCK, or the gun pipper */
  Frame tf;
  if (sh->target >= 0 && actor_frame(&act[sh->target], t, &tf)) {
    vec3 c = to_cam(cam, tf.pos);
    if (c.z > 5) {
      float sx = cx + c.x / c.z * F, sy = cy - c.y / c.z * F, bx = 9.0f * ks;
      if (sh->guns) {
        fb_ring(fb, sx, sy, bx, G, 1);
        fb_rect(fb, (int)sx - 1, (int)sy - 1, 2, 2, G);
        fb_text(fb, "GUNS", cx, cy + hh - 8 * ks, ks, G, ALIGN_CENTER, 0);
      } else {
        fb_rect_stroke(fb, sx - bx, sy - bx, 2 * bx, 2 * bx, G, 1);
        if (((int)(t * 6)) % 2) fb_text(fb, "LOCK", sx, sy - bx - 6 * ks, ks, G, ALIGN_CENTER, 0);
      }
      snprintf(b, sizeof b, "%.1f", c.z / 1852);
      fb_text(fb, b, sx + bx + 3 * ks, sy + bx, ks, G, ALIGN_LEFT, 0);
    }
  }
  /* the canopy bow arching over the top, side rails, and the glare shield */
  float top = H * 0.06f;
  for (int x = 0; x < W; x += 2) {
    float u = (x - cx) / (W / 2.0f), y = top + u * u * H * 0.16f;
    fb_rect(fb, x, 0, 2, (int)fmaxf(0, y), frame);
  }
  float rail[8] = { 0, 0, W * 0.07f, 0, W * 0.015f, (float)H, 0, (float)H };
  fb_poly(fb, rail, 4, frame, 1, 0);
  float rail2[8] = { (float)W, 0, W * 0.93f, 0, W * 0.985f, (float)H, (float)W, (float)H };
  fb_poly(fb, rail2, 4, frame, 1, 0);
  float gs = H * 0.82f;
  float sh_xy[10] = { 0, (float)H, 0, gs + H * 0.05f, cx, gs - H * 0.03f, (float)W, gs + H * 0.05f, (float)W, (float)H };
  fb_poly(fb, sh_xy, 5, hexc(0x0e0f10), 1, 0);
  fb_rect(fb, (int)(cx - hw * 0.6f), (int)(gs - H * 0.035f), (int)(hw * 1.2f), (int)fmaxf(2, H * 0.012f), hexc(0x2a2c30));
}

/* The film grade (warm tint, vignette, grain) and the heat haze: on the
 * GPU in the final pass, in software straight on the frame. */
static void grade_and_haze(Framebuffer *fb, float t) {
  const Fx *g = NULL, *h = NULL;
  active_fx(FX_GRADE, t, &g);
  active_fx(FX_HAZE, t, &h);
  if (gpu_active()) {
    gpu_set_post(g ? g->warm : 0, g ? g->vignette : 0, g ? g->grain : 0, h ? h->y0 : 0, h ? h->y1 : 0, h ? h->amount : 0, t);
    return;
  }
  int W = fb->w, H = fb->h;
  if (h) { /* shimmer: shift rows sideways in the band, more toward the bottom */
    int y0 = (int)(h->y0 * H), y1 = (int)(h->y1 * H);
    uint32_t *tmp = malloc(sizeof(uint32_t) * (size_t)W);
    for (int y = y0 < 0 ? 0 : y0; tmp && y < y1 && y < H; y++) { /* the script's y0 may be below the top */
      float k = (float)(y - y0) / fmaxf(1, (float)(y1 - y0));
      int dx = (int)roundf(sinf(y * 0.9f + t * 23) * sinf(y * 0.13f - t * 7) * h->amount * H / 360.0f * k);
      if (!dx) continue;
      uint32_t *row = fb->px + (size_t)y * W;
      memcpy(tmp, row, sizeof(uint32_t) * (size_t)W);
      for (int x = 0; x < W; x++) row[x] = tmp[x - dx < 0 ? 0 : x - dx >= W ? W - 1 : x - dx];
    }
    free(tmp);
  }
  if (!g) return;
  uint32_t seed = (uint32_t)(t * 60) * 2654435761u;
  for (int y = 0; y < H; y++) {
    uint32_t *row = fb->px + (size_t)y * W;
    float dy = (y + 0.5f) / H - 0.5f;
    for (int x = 0; x < W; x++) {
      float dx = (x + 0.5f) / W - 0.5f, v = 1 - (dx * dx + dy * dy) * g->vignette * 2.2f;
      seed = seed * 1664525u + 1013904223u;
      float n = ((seed >> 24) / 255.0f - 0.5f) * g->grain * 120;
      uint32_t c = row[x];
      float r = ((c >> 16) & 255) * (1 + g->warm * 0.5f), gg = ((c >> 8) & 255) * (1 + g->warm * 0.1f), b = (c & 255) * (1 - g->warm * 0.4f);
      row[x] = rgb((uint8_t)clampf(r * v + n, 0, 255), (uint8_t)clampf(gg * v + n, 0, 255), (uint8_t)clampf(b * v + n, 0, 255));
    }
  }
}

/* ---- the smoke title: the last kill's fire and smoke become a cloud that
 * spells the title (INTRO.md, smoketitle) ----------------------------------- */
typedef struct { float tx, ty, lit, d, sx, sy, curl, ph, size; } TitlePuff;
static TitlePuff *tpuffs;
static int       ntpuffs, tp_w, tp_h;
static float     tp_step;

static uint32_t tp_rng;
static float tp_rand(void) {
  tp_rng = tp_rng * 1664525u + 1013904223u;
  return (tp_rng >> 8) / 16777216.0f;
}

static void title_layout(const Fx *f, int W, int H) {
  if (tpuffs && tp_w == W && tp_h == H) return;
  free(tpuffs);
  tpuffs = NULL;
  ntpuffs = 0;
  tp_w = W;
  tp_h = H;
  float size = 0.14f * fminf((float)W, 1.6f * H);
  int sc = (int)fmaxf(1, roundf(size / 7));
  while (sc > 1 && text_width(f->text, sc) > W * 0.84f) sc--;
  /* rasterize the text, bold (drawn twice, a pixel apart), into a mask */
  Framebuffer m = { 0 };
  int mw = text_width(f->text, sc) + sc + 2, mh = 7 * sc + 2;
  if (!fb_resize(&m, mw, mh)) return;
  fb_clear(&m, 0);
  fb_text(&m, f->text, 1, mh / 2.0f, sc, 0xFFFFFFFFu, ALIGN_LEFT, 0);
  fb_text(&m, f->text, 1 + (sc + 3) / 4, mh / 2.0f, sc, 0xFFFFFFFFu, ALIGN_LEFT, 0);
  tp_step = fmaxf(3, size / 20);
  float ox = W / 2.0f - mw / 2.0f, oy = H * 0.42f - mh / 2.0f;
  int cap = (int)((mw / tp_step + 2) * (mh / tp_step + 2));
  tpuffs = calloc((size_t)cap, sizeof *tpuffs);
  tp_rng = f->seed * 2654435761u + 1;
  for (float y = tp_step / 2; y < mh && tpuffs; y += tp_step)
    for (float x = tp_step / 2; x < mw; x += tp_step) {
      if (!(m.px[(size_t)(int)y * mw + (int)x] & 0xFFFFFF) || ntpuffs >= cap) continue;
      TitlePuff *q = &tpuffs[ntpuffs++];
      q->tx = ox + x + (tp_rand() - 0.5f) * 0.8f * tp_step;
      q->ty = oy + y + (tp_rand() - 0.5f) * 0.8f * tp_step;
      q->lit = 1 - y / mh;
      q->d = tp_rand();
      q->sx = tp_rand() - 0.5f;
      q->sy = tp_rand() - 0.5f;
      float big = fmaxf(fabsf(q->sx), fabsf(q->sy)), len = sqrtf(q->sx * q->sx + q->sy * q->sy); /* a round fireball, not a square */
      if (len > 0) q->sx *= big / len, q->sy *= big / len;
      float curl = 0.3f + 0.7f * tp_rand(), turn = tp_rand() < 0.5f ? -1.0f : 1.0f; /* one draw after the other */
      q->curl = curl * turn;
      q->ph = tp_rand() * 6.2831853f;
      q->size = 0.85f + 0.4f * tp_rand();
    }
  fb_free(&m);
}

/* A soft round puff: alpha 1 at the middle, 0.75 at 45% out, 0 at the rim. */
static void soft_puff(Framebuffer *fb, float cx, float cy, float r, uint32_t col, float a) {
  if (r < 0.5f || a <= 0.003f) return;
  int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r), y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  for (int y = y0; y <= y1; y++) {
    if (y < fb->cy0 || y >= fb->cy1) continue;
    float dy = (y + 0.5f - cy) / r;
    for (int x = x0; x <= x1; x++) {
      if (x < fb->cx0 || x >= fb->cx1) continue;
      float dx = (x + 0.5f - cx) / r, d = sqrtf(dx * dx + dy * dy);
      if (d >= 1) continue;
      float k = d < 0.45f ? 1 - d / 0.45f * 0.25f : 0.75f * (1 - (d - 0.45f) / 0.55f);
      fb_blend_px(fb, x, y, col, a * k);
    }
  }
}

static float ease3(float x) { return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2; }

static void draw_smoke_title(Framebuffer *fb, const Fx *f, const Camera *cam, float F, float t) {
  int W = fb->w, H = fb->h;
  title_layout(f, W, H);
  if (!ntpuffs) return;
  vec3 c = to_cam(cam, f->pos);
  float fx = c.z > 1 ? W / 2.0f + c.x / c.z * F : W / 2.0f, fy = c.z > 1 ? H / 2.0f - c.y / c.z * F : H * 0.6f;
  float span = f->f1 - f->f0, mn = fminf((float)W, (float)H);
  float driftx = fmaxf(0, t - f->f1) * 0.04f * tp_step, drifty = -fmaxf(0, t - f->f1) * 0.02f * tp_step;
  for (int pass = 0; pass < 2; pass++) /* shadows behind, then the puffs */
    for (int i = 0; i < ntpuffs; i++) {
      const TitlePuff *q = &tpuffs[i];
      float e = ease3(clampf((t - f->f0 - 0.4f * q->d * span) / (0.6f * span), 0, 1));
      if (pass == 0 && e <= 0.6f) continue;
      float sx = fx + q->sx * 0.06f * mn * (1 + 2 * e), sy = fy + q->sy * 0.06f * mn * (1 + 2 * e);
      float px = q->tx - sx, py = q->ty - sy, sw = sinf(PI_F * e);
      float x = sx + px * e - py * sw * q->curl * 0.25f, y = sy + py * e + px * sw * q->curl * 0.25f - sw * 0.048f * mn;
      if (t > f->f1) { /* billowing once formed */
        x += (sinf(0.7f * t + q->ph) * 0.35f) * tp_step + driftx;
        y += (cosf(0.55f * t + 1.3f * q->ph) * 0.25f) * tp_step + drifty;
      }
      float r = tp_step * q->size * lerpf(0.5f, 1.2f, e) * (1 + 0.08f * sinf(t * 1.3f + q->ph));
      /* dissolving: each puff lets go in turn, drifts up and away with the wind, swells and thins out */
      float fade = 1;
      if (t > f->d0) {
        float g = clampf((t - f->d0 - 0.35f * q->d * (f->d1 - f->d0)) / (0.65f * (f->d1 - f->d0)), 0, 1);
        if (g >= 1) continue;
        float gg = g * g;
        x += (q->sx * 0.5f + 0.35f + q->curl * 0.2f) * gg * 0.22f * mn;
        y -= (0.25f + 0.5f * q->d) * gg * 0.12f * mn;
        r *= 1 + 1.6f * g;
        fade = (1 - g) * (1 - g);
      }
      if (pass == 0) {
        soft_puff(fb, x + 0.2f * r, y + 0.35f * r, 1.1f * r, rgb(150, 138, 176), 0.55f * fade * clampf((e - 0.6f) * 4, 0, 1));
        continue;
      }
      uint32_t col;
      float a = 0.9f;
      if (e < 0.18f) {
        col = mixc(0xffecaa, 0xff8032, e / 0.18f);
        a = 0.6f;
      } else if (e < 0.38f) col = mixc(0xff8032, 0x766870, (e - 0.18f) / 0.2f);
      else if (e < 0.5f) col = 0x766870;
      else {
        uint32_t cloud = mixc(0xc4b4d2, 0xfff2e4, powf(q->lit, 0.6f));
        col = mixc(0x766870, cloud, clampf((e - 0.5f) / 0.4f, 0, 1));
      }
      soft_puff(fb, x, y, r, hexc(col), a * fade);
    }
}

/* Looking down from height the sea's mirror of the sky breaks into hard blotches: a haze below the jets softens them. */
static void sea_haze(const View *V, const Camera *c, float t) {
  const Fx *f;
  if (!active_fx(FX_SEAHAZE, t, &f) || c->f.y > -0.15f || c->pos.y < 60) return;
  /* just beyond the farthest jet, and high enough that its lower edge never cuts through the sea */
  float k = clampf((-c->f.y - 0.15f) / 0.35f, 0, 1), d = 60;
  for (int i = 0; i < nact; i++) {
    Frame fr;
    if (act[i].kind == K_JET && actor_frame(&act[i], t, &fr)) {
      float z = v3_dot(v3_sub(fr.pos, c->pos), c->f);
      if (z > 0 && z < 900) d = fmaxf(d, z + 30);
    }
  }
  d = fminf(d, c->pos.y - 10);
  r3d_sprite_kind(SPR_SOFT);
  r3d_ball(V, v3_add(c->pos, v3_scale(c->f, d)), d * 3, hexc(0x8e7884), f->amount * k);
}

/* The low sun in the lens: a soft bloom on the disc, a veil round it and two ghosts across the centre. The bloom and veil lie behind the world, so a
 * jet in front of the sun cuts them; the ghosts are in the lens. Under the grade, like everything in the picture. */
static void lens_glow(const View *V, const Camera *c, float t) {
  const Fx *f;
  float z = v3_dot(LIGHT, c->f);
  if (!active_fx(FX_GLOW, t, &f) || z < 0.25f) return;
  float x = v3_dot(LIGHT, c->r) / z, y = v3_dot(LIGHT, c->u) / z, a = f->amount * clampf(1.2f - sqrtf(x * x + y * y) * 0.6f, 0, 1);
  static const float G[4][3] = { { 1, 0.1f, 0.5f }, { 1, 0.26f, 0.28f }, { -0.35f, 0.03f, 0.05f }, { -0.8f, 0.06f, 0.035f } }; /* where, size, strength */
  /* the sky's disc has a hard edge: soft layers over it make it a soft ball of light */
  r3d_sprite_kind(SPR_SOFT);
  for (int i = 0; i < 3; i++) r3d_ball(V, v3_add(c->pos, v3_scale(LIGHT, 4000)), 4000 * 0.07f, hexc(0xfff0d8), 1);
  r3d_sprite_kind(SPR_GLOW);
  for (int i = 0; i < 4; i++) {
    float d = i > 1 ? 5 : 4000;
    vec3 w = v3_add(c->pos, v3_scale(v3_add(c->f, v3_add(v3_scale(c->r, x * G[i][0]), v3_scale(c->u, y * G[i][0]))), d));
    r3d_ball(V, w, d * G[i][1], hexc(i == 2 ? 0xffd8a0 : i ? 0xffb070 : 0xffe0b0), a * G[i][2]);
  }
  r3d_sprite_kind(SPR_SOFT);
}

/* Draws one shot's picture at time t (yaw_off for a whip pan); returns its
 * camera and focal length for whatever is drawn over it. */
static void render_shot(Framebuffer *fb, const Shot *sh, float t, float yaw_off, Camera *cam_out, float *F_out) {
  float k[8];
  const Seg *last = &sh->cam.segs[sh->cam.nseg - 1];
  float tend = last->k[(last->n - 1) * sh->cam.stride];
  if (!sample(&sh->cam, clampf(t, sh->t0, tend), k)) memcpy(k, &last->k[(last->n - 1) * sh->cam.stride], sizeof k);
  float yaw = k[4] + yaw_off, pitch = k[5], roll = k[6], fov = k[7] > 1 ? k[7] : 42;
  /* camera jitter: a slow drift for a long lens, a buzz on an airframe */
  if (sh->vibe_a > 0) {
    float a = sh->vibe_a * fov, w = sh->vibe_f * 6.2831853f;
    yaw += a * (sinf(t * w) * 0.6f + sinf(t * w * 2.3f + 1) * 0.4f);
    pitch += a * (sinf(t * w * 1.3f + 2) * 0.6f + sinf(t * w * 2.9f) * 0.4f);
  }
  const Fx *f;
  if (active_fx(FX_SHAKE, t, &f)) {
    float sk = f->amount * (1 - (t - f->t0) / (f->t1 - f->t0));
    yaw += sinf(t * 71) * 0.9f * sk;
    pitch += sinf(t * 53 + 1) * 0.8f * sk;
    roll += sinf(t * 37 + 2) * 1.2f * sk;
  }
  Frame fr = frame_of(yaw, pitch, roll);
  Camera cam = { v3(k[1], k[2], k[3]), fr.r, fr.u, fr.f, true };
  Frame af;
  if (sh->attach >= 0 && actor_frame(&act[sh->attach], t, &af)) {
    /* bolted to the jet: built from its frame, plus the jitter and any whip */
    vec3 d = v3_norm(v3_add(v3_add(v3_scale(af.r, sh->dir.x), v3_scale(af.u, sh->dir.y)), v3_scale(af.f, sh->dir.z)));
    vec3 up = sh->roll ? af.u : v3(0, 1, 0);
    vec3 jr = v3_scale(v3_norm(v3_cross(up, d)), (yaw - k[4]) * D2R), ju = v3_scale(up, (pitch - k[5]) * D2R);
    d = v3_norm(v3_add(d, v3_add(jr, ju)));
    cam.pos = v3_add(af.pos, v3_add(v3_add(v3_scale(af.r, sh->off.x), v3_scale(af.u, sh->off.y)), v3_scale(af.f, sh->off.z)));
    cam.f = d;
    cam.r = v3_norm(v3_cross(up, d));
    cam.u = v3_cross(d, cam.r);
  }
  int W = fb->w, H = fb->h;
  float vfov = fov * D2R;
  float F = (H / 2.0f) / tanf(vfov / 2);
  if ((float)W / H > 1.9f) F = (W / 2.0f) / tanf(vfov * 0.9f);
  View V;
  hide_actor = sh->hud;
  shade_now = sh->shade;
  r3d_begin(fb, &cam, 0, 0, W, H, F, false, t, &V);
  r3d_whitecaps(fb, &V, t);
  draw_wakes(fb, &V, t);
  sea_haze(&V, &cam, t);
  draw_actors(fb, &V, t);
  r3d_gloss(-1);
  lens_glow(&V, &cam, t);
  r3d_end(fb, &V, t);
  hide_actor = -1;
  *cam_out = cam;
  *F_out = F;
}

/* software: lay a copy of the previous picture back over the new one */
static void blend_frames(Framebuffer *fb, const uint32_t *prev, float keep) {
  size_t n = (size_t)fb->w * fb->h;
  int a = (int)(keep * 256);
  for (size_t i = 0; i < n; i++) {
    uint32_t p = prev[i], c = fb->px[i];
    uint32_t rb = ((p & 0xff00ff) * (uint32_t)a + (c & 0xff00ff) * (uint32_t)(256 - a)) >> 8 & 0xff00ff;
    uint32_t g = ((p & 0xff00) * (uint32_t)a + (c & 0xff00) * (uint32_t)(256 - a)) >> 8 & 0xff00;
    fb->px[i] = 0xFF000000u | rb | g;
  }
}

/* the drawn control room over the picture with alpha a: blended in software,
 * on the GPU as the HUD layer's own alpha over the 3D picture under it */
static Framebuffer room_fb;
/* The drawn scenes are painted by the CPU, and a Retina screen has four times the pixels. So they are painted at about
 * 1080 lines at most, scaled up, and every other frame on a large screen: the film holds its pace. */
static Framebuffer room_lo;
static void lay_scene(Framebuffer *fb, const Shot *sc, float t, float a, bool gpu) {
  static const Shot *last;
  static int tick;
  int k = fb->h > 1200 ? (fb->h + 1079) / 1080 : 1; /* sharp up to 1080 lines: the commander's gestures read */
  bool same = sc == last && room_fb.w == fb->w && room_fb.h == fb->h;
  if (!sc || a <= 0 || !fb_resize(&room_fb, fb->w, fb->h)) return;
  if (k == 1 || !same || ++tick % 2 == 0) {
    Framebuffer *to = &room_fb;
    if (k > 1) {
      if (!fb_resize(&room_lo, (fb->w + k - 1) / k, (fb->h + k - 1) / k)) return;
      to = &room_lo;
    }
    if (sc->radar) cic_radar_draw(to, t, sc->t0, sc->t1);
    else cic_draw(to, t, sc->t0, sc->t1);
    if (k > 1) fb_scale_up(&room_fb, &room_lo, k);
  }
  last = sc;
  size_t n = (size_t)fb->w * fb->h;
  if (gpu) {
    uint32_t al = (uint32_t)lroundf(clampf(a, 0, 1) * 255) << 24;
    for (size_t i = 0; i < n; i++) fb->px[i] = al | (room_fb.px[i] & 0xFFFFFFu);
  } else if (a >= 1) memcpy(fb->px, room_fb.px, sizeof(uint32_t) * n);
  else blend_frames(fb, room_fb.px, a);
}

static void smear_frame(Framebuffer *fb, float px) {
  int W = fb->w, H = fb->h;
  uint32_t *row = malloc(sizeof(uint32_t) * (size_t)W);
  if (!row) return;
  for (int y = 0; y < H; y++) {
    uint32_t *r = fb->px + (size_t)y * W;
    memcpy(row, r, sizeof(uint32_t) * (size_t)W);
    for (int x = 0; x < W; x++) {
      int acc[3] = { 0, 0, 0 };
      for (int k = 0; k < 5; k++) { /* five copies, each shifted further, as the browser draws it */
        int sx = x - (int)(px * k);
        uint32_t c = row[sx < 0 ? 0 : sx >= W ? W - 1 : sx];
        acc[0] += (c >> 16) & 255;
        acc[1] += (c >> 8) & 255;
        acc[2] += c & 255;
      }
      r[x] = rgb((uint8_t)(acc[0] / 5), (uint8_t)(acc[1] / 5), (uint8_t)(acc[2] / 5));
    }
  }
  free(row);
}

static void intro_render_frame(Framebuffer *fb);

void intro_render(Framebuffer *fb) {
  r3d_film_look(true);
  hud_film_labels(true);
  intro_render_frame(fb);
  hud_film_labels(false);
  r3d_film_look(false);
}

static void intro_render_frame(Framebuffer *fb) {
  if (!ready) return;
  float t = t_now;
  int idx = nshots - 1;
  for (int i = 0; i < nshots; i++)
    if (t >= shots[i].t0 && t < shots[i].t1) idx = i;
  const Shot *sh = &shots[idx];
  int W = fb->w, H = fb->h;
  bool gpu = gpu_active();
  fb_clear(fb, gpu ? 0 : 0xFF000000u);
  Camera cam = { v3(0, 100, 0), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1), true };
  float F = (float)H;
  const Shot *room = sh->scene ? sh : NULL; /* the drawn control room, laid over at room_a */
  float room_a = 1;
  /* the next shot may come in with a whip that starts before its cut */
  const Shot *nx = idx + 1 < nshots ? &shots[idx + 1] : NULL;
  if (nx && nx->in == IN_WHIP && t >= nx->t0 - nx->in_dur / 2) {
    float u = (t - (nx->t0 - nx->in_dur / 2)) / nx->in_dur;
    render_shot(fb, sh, t, nx->in_dir * 80 * (2 * u) * (2 * u), &cam, &F);
    float sm = nx->in_dir * 0.035f * sinf(PI_F * u);
    if (gpu) gpu_set_blend(1, sm);
    else smear_frame(fb, sm * W);
  } else if (sh->in == IN_WHIP && idx > 0 && t < sh->t0 + sh->in_dur / 2) {
    float u = (t - (sh->t0 - sh->in_dur / 2)) / sh->in_dur;
    render_shot(fb, sh, t, -sh->in_dir * 80 * (2 * (1 - u)) * (2 * (1 - u)), &cam, &F);
    float sm = sh->in_dir * 0.035f * sinf(PI_F * u);
    if (gpu) gpu_set_blend(1, sm);
    else smear_frame(fb, sm * W);
  } else if (sh->in == IN_DISSOLVE && idx > 0 && t < sh->t0 + sh->in_dur) {
    /* the previous shot keeps moving under the new one as it fades in */
    float u = clampf((t - sh->t0) / sh->in_dur, 0, 1), a = u * u * (3 - 2 * u);
    Camera pc;
    float pF;
    const Shot *pv = &shots[idx - 1];
    if (sh->scene || pv->scene) {
      /* to or from the control room: the 3D side plays on, the room fades over it */
      if (!sh->scene || !pv->scene) render_shot(fb, sh->scene ? pv : sh, t, 0, &cam, &F);
      room = sh->scene ? sh : pv;
      room_a = sh->scene ? a : 1 - a;
    } else if (gpu) {
      gpu_view_layer(1);
      render_shot(fb, &shots[idx - 1], t, 0, &pc, &pF);
      fb_clear(fb, 0);
      render_shot(fb, sh, t, 0, &cam, &F);
      gpu_set_blend(a, 0);
    } else {
      render_shot(fb, &shots[idx - 1], t, 0, &pc, &pF);
      uint32_t *prev = malloc(sizeof(uint32_t) * (size_t)W * H);
      if (prev) memcpy(prev, fb->px, sizeof(uint32_t) * (size_t)W * H);
      fb_clear(fb, 0xFF000000u);
      render_shot(fb, sh, t, 0, &cam, &F);
      if (prev) blend_frames(fb, prev, 1 - a);
      free(prev);
    }
  } else if (!sh->scene) render_shot(fb, sh, t, 0, &cam, &F);
  const Fx *st = NULL;
  bool title = active_fx(FX_SMOKETITLE, t, &st);
  if (title && !gpu) draw_smoke_title(fb, st, &cam, F, t); /* in the picture, under the grade */
  grade_and_haze(fb, t_film);
  if (title && gpu) draw_smoke_title(fb, st, &cam, F, t); /* on the HUD layer, over the picture */
  if (room) lay_scene(fb, room, t, room_a, gpu);
  bool panel = sh->hud >= 0 && sh->panel;
  if (panel) draw_game_cockpit(fb, &cam, F, sh, t, bar_height(W, H, t));
  else if (sh->hud >= 0) draw_cockpit(fb, &cam, F, sh, t);
  draw_overlay(fb, t, panel);
}
