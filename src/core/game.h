/* game.h - Game struct with fixed arrays; buildRoster, newGame, makePlane,
 * spawn (JS lines 2760-2827). Nothing here allocates: every pool is a fixed
 * array, and objects that others point at (jets, ground targets, flares) are
 * referred to by index plus a generation count instead of a pointer. */
#ifndef A4C_GAME_H
#define A4C_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "core/jets.h"
#include "core/mathx.h"
#include "core/world.h"

#define MAX_PLANES   8
#define MAX_TARGETS  12
#define MAX_BULLETS  1024
#define MAX_MISSILES 64
#define MAX_FLARES   96
#define MAX_PARTS    1700
#define MAX_SOUNDS   64

typedef enum { MODE_DEMO, MODE_1P, MODE_2P, MODE_DEFEND } GameMode;
typedef enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD } Difficulty;

typedef enum { REF_NONE, REF_PLANE, REF_GROUND, REF_FLARE, REF_MISSILE, REF_CARRIER } RefKind;
typedef struct { uint8_t kind; int16_t idx; uint32_t gen; } Ref;

typedef struct {
  float pitch, roll;
  bool  gun, msl, flr;
  bool  fire, sel, map; /* classic keys: fire the selected weapon, select, map view */
  bool  ap;             /* autopilot toggle */
  bool  stick;          /* the pilot is moving the stick (takes over from the autopilot) */
} Controls;

/* Classic weapons, cycled with the select key. */
enum { W_CANNON, W_HEAT, W_RADAR, W_GROUND, W_COUNT };
/* missile seekers: heat follows flares, radar and air-to-ground follow chaff */
enum { MK_HEAT, MK_RADAR, MK_GROUND };
#define CLASSIC_RADAR  2
#define CLASSIC_GROUND 2
#define MAX_CHAFF      16

typedef struct {
  Difficulty diff;
  float threatT, flareCool, mslCool, jitT;
  vec3  jit;
  bool  refuel, final_leg;
  int   gt; /* ground target being attacked, or -1 */
  float egressT; /* carrier strikes: time left running out before the next pass */
  vec3  egress;
  int   bfm;      /* the dogfight manoeuvre being flown (BFM_*) */
  float bfmT;     /* how long it has been flown */
  float burstT;   /* gun bursts: on while positive, then a pause */
} AiPilot;

/* A CPU pilot's dogfight manoeuvres (basic fighter manoeuvres). */
enum {
  BFM_PURSUIT, BFM_LEAD, BFM_LAG, BFM_HIGH_YOYO, BFM_LOW_YOYO, BFM_PERCH, BFM_BREAK, BFM_JINK, BFM_EXTEND, BFM_REVERSE,
  BFM_COUNT
};
extern const char *const BFM_NAMES[BFM_COUNT];

typedef struct {
  int   idx, team, hidx; /* hidx: local seat, or -1 for the CPU */
  int   jet;             /* index into JETS */
  char  name[16];
  bool  is_ai;
  AiPilot ai;

  vec3  pos, r, u, f, vel;
  float speed, throttle;
  float fuel, health, ammo;
  int   missiles, flares, lives, kills, tkills, prevKills;
  int   kc[MAX_PLANES]; /* kc[i]: times plane i shot this one down */
  bool  alive, chase;
  bool  inactive; /* not in play: a wave not yet launched, or a commander in the control room */
  float respawnT, invuln, gunCool, mslCool, lockT, hitFlash, rearmT, beepT, evT, lastHitT;
  Ref   lockTarget;
  bool  locked, stall, onGround, out, refuel, warn;
  int   gunSide, lastHitBy;
  float slot;
  Controls ctl;
  bool  prevMsl, prevFlr;
  int   wsel, radar, ground, chaff;  /* classic: selected weapon and the extra stores */
  bool  mapview, prevSel, prevMap, prevFire;
  /* on the ground before take-off: rolling down the runway, or held on the
   * carrier's catapult until full power fires it (cat > 0: the stroke) */
  bool  taxi, oncat;
  float cat;
  /* autopilot: wings level, holding the altitude and heading it was engaged at */
  bool  autopilot, prevAp;
  float apAlt, apHdg;
  float pitchRate; /* rad/s, last tick */
  float stunT;   /* after a missile hit the controls do not answer (classic rules) */
  int   score;    /* awarded on this computer */
  int   netScore; /* awarded on the other computer, in a network match */
  int   load[3];  /* chosen loadout: heat, radar, air-to-ground (-1 heat: the default) */
  /* G-load and what it does to the pilot: stress builds above about 6 G
   * (grey-out, then blackout) and below -2 G (redout); gloc counts down a
   * blackout during which the jet flies on with no input */
  float gload, gstress, gneg, gloc;
  char  ev[40];
  vec3  deathPos;
  bool  hasDeath;
  const char *deathMsg;
  /* network play: a jet the other computer flies, moved by its snapshots */
  bool  remote, netFire;
  struct { vec3 pos, f, u; float speed, thr, age; bool have; } snap;
} Plane;

typedef struct {
  int   team, type, by;
  vec3  pos;           /* 8 m above the ground it stands on */
  float ground, heading;
  float hp, cool, spin, smokeT;
  bool  alive;
  bool  remote; /* network play: the other computer decides when it falls */
} Target;

/* owner: a plane's index, or BY_SAM / BY_CARRIER */
#define BY_SAM     (-1)
#define BY_CARRIER (-2)
typedef struct { vec3 pos, prev, vel; float life; int owner, team; } Bullet;

typedef struct {
  vec3  pos, prev, dir;
  float speed, maxSpeed, turn, life, dmg, smokeT;
  int   team, owner; /* a plane, BY_SAM or BY_CARRIER */
  Ref   target;
  bool  sam, ashm;     /* ashm: an anti-ship missile, sea-skimming at the carrier */
  int   kind;          /* MK_HEAT, MK_RADAR (guided by the launcher's lock) or MK_GROUND */
  bool  remote;        /* network play: launched on the other computer */
  uint32_t id;         /* for references to missiles (the array is compacted) */
} Missile;

typedef struct { vec3 pos, vel; float life; int owner; uint32_t gen; bool live, chaff; } Flare;

/* particle types: 0 fire, 1 smoke, 2 debris, 3 spark */
typedef struct { vec3 pos, vel; float life, max, size, grow; int type; } Particle;

typedef struct {
  float th, R, alt, speed, cx, cz;
  vec3  pos, r, u, f, vel, drogue;
} Tanker;

/* ---- the carrier defence mission (core/defend.c) ---- */
#define MAX_TURRETS 6
enum { TUR_CIWS, TUR_SAM };
enum { TM_AUTO, TM_HOLD, TM_ASSIGNED, TM_MANUAL };
enum { CMD_WALK, CMD_TACTICAL, CMD_TURRET, CMD_FLYING };
typedef struct {
  int   kind, mode;
  const char *name;
  vec3  local;            /* carrier-local mount: +X starboard, +Z bow */
  float yaw, pitch;       /* world aim, radians (yaw clockwise from +Z) */
  Ref   target;
  float cool, heat, reload, restock;
  int   ammo;
  bool  overheat, firing;
  float man_yaw, man_pitch; /* manual control: aim rates from -1 to 1 */
  bool  man_fire;
} Turret;
typedef struct {
  bool  on, commander;    /* commander: player 2 runs the carrier */
  vec3  pos;
  float heading, hp, hpmax, deck, length, beam;
  Turret tur[MAX_TURRETS];
  int   nturrets;
  int   wave, waves, kills, ashm_down;
  float waveT;            /* countdown to the next wave */
  bool  between;
  int   cmd_state, sel, cmd_plane; /* the commander's seat */
  Ref   pick;             /* the contact the commander has selected */
  /* the captain on foot, in carrier-local coordinates (feet) */
  vec3  cap;
  float cap_yaw, cap_look, cap_bob;  /* heading (0 = bow), look pitch */
  bool  cap_bridge;
  float cap_move, cap_side, cap_turn, cap_lookv; /* input, -1..1 */
  bool  cap_run;
} Defense;

/* Sounds the core asks for; the SDL side drains and plays them. */
typedef enum { SND_NOISE, SND_SQUARE, SND_SINE } SoundKind;
typedef struct { SoundKind kind; float f0, f1, dur, vol; } SoundReq;

typedef struct {
  GameMode   mode;
  Difficulty diff;
  bool       coop, over;
  bool       g_effects; /* blackout and redout on */
  bool       classic;   /* classic rules: weapon selector, chaff, map edge home, 140 kt stall, 60,000 ft ceiling */
  bool       ground_start; /* team 0 from its carrier's catapult (or runway), team 1 from its runway */
  bool       no_crash;     /* crash detection off: the ground does not kill */
  bool       realistic;    /* realistic handling: turns limited by the g the jet can pull */
  int        hits_to_kill; /* missile hits needed (1-3); 0 means one, as before */
  int        lives;        /* planes each */
  bool       dogfight;     /* close range dogfight: cannon and heat-seekers, air start near each other */
  float      dfx, dfz, dfa;/* where and along which axis the dogfight starts */
  float      time, overT, focusT;
  int        winner, focus;
  char       reason[96];
  Rng        rng;

  Plane    planes[MAX_PLANES];   int nplanes;
  Target   targets[MAX_TARGETS]; int ntargets;
  Bullet   bullets[MAX_BULLETS]; int nbullets;
  Missile  missiles[MAX_MISSILES]; int nmissiles;
  Flare    flares[MAX_FLARES];   uint32_t flareGen;
  Particle parts[MAX_PARTS];     int nparts;
  Tanker   tanker;

  int locals[2], nlocals, my;

  SoundReq sounds[MAX_SOUNDS]; int nsounds;
  uint32_t missileSeq;

  Defense def;

  /* network play: what happened here that the other computer must copy */
  bool     net;
  int      net_side;   /* 0 the host, 1 the guest: the top byte of our missile ids */
  struct { int kind; int plane; bool chaff; Missile m; } netev[32];
  int      nnetev;
} Game;
enum { NE_MISSILE, NE_FLARES, NE_GONE };

extern const char *const TNAME[TGT_TYPES];
typedef struct { float hp, hx, hz, h; } TargetDef;
extern const TargetDef TDEF[TGT_TYPES];

/* seatJets: the jet each local seat flies (indexes into JETS); CPU jets are
 * picked at random. NULL gives every seat the first jet. */
/* Match rules picked in the menu. NULL in game_new gives the defaults. */
typedef struct {
  bool classic, ground_start, no_crash, dogfight, g_effects, realistic;
  bool pairs;       /* 2 vs 2: every side gets a CPU wingman */
  int  hits, lives; /* missile hits to kill (0: one), planes each (0: LIVES) */
  int  load[2][3];  /* each seat's loadout: heat, radar, air-to-ground; heat -1 for the default */
} GameRules;
void  game_new(Game *g, GameMode mode, Difficulty diff, bool coop, const int *seatJets, uint32_t seed);
void  game_new_rules(Game *g, GameMode mode, Difficulty diff, bool coop, const int *seatJets, uint32_t seed,
                     const GameRules *rules);
/* The classic arming rules: heat up to 8 (fewer with other stores), radar up
 * to 6 but none with air-to-ground, air-to-ground up to 2. Returns whether
 * the load fits. */
bool  game_load_fits(const int *load);
/* What the jet carries when full: heat, radar, air-to-ground, cannon rounds. */
void  game_full_load(const Game *g, const Plane *p, int *full);
void  game_spawn(Game *g, Plane *p);
Plane *game_enemy_of(Game *g, int team, vec3 pos);
void  game_event(Plane *p, const char *text);
float game_rand(Game *g, float a, float b);
void  game_sound(Game *g, SoundKind kind, float f0, float f1, float dur, float vol);

/* Resolves a reference. Returns false if it no longer points at anything alive. */
bool  ref_get(const Game *g, Ref r, vec3 *pos, vec3 *vel);
static inline Ref ref_plane(int i)  { Ref r = { REF_PLANE, (int16_t)i, 0 }; return r; }
static inline Ref ref_ground(int i) { Ref r = { REF_GROUND, (int16_t)i, 0 }; return r; }
static inline int plane_score(const Plane *p) { return p->score + p->netScore; }
static inline Ref ref_none(void)    { Ref r = { REF_NONE, -1, 0 }; return r; }
static inline bool ref_eq(Ref a, Ref b) { return a.kind == b.kind && a.idx == b.idx && a.gen == b.gen; }

#endif
