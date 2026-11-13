/* defend.h - the carrier defence mission: player 1 flies, player 2 commands
 * the carrier from its control room (turret orders, manning a turret,
 * launching a fighter), against waves of strike jets with anti-ship
 * missiles. The Defense state lives in Game (game.h). */
#ifndef TL_DEFEND_H
#define TL_DEFEND_H

#include "core/game.h"

#define DEF_WAVES 5

/* Sets up the carrier, its turrets and the first wave (game_new calls it). */
void defend_setup(Game *g, bool commander);
void defend_update(Game *g, float dt);
/* Puts a blue jet on a catapult and shoots it off the bow. */
void defend_launch(Game *g, Plane *p);
/* A strike jet fires an anti-ship missile at the carrier. Returns true if it did. */
bool defend_ashm(Game *g, Plane *p);
void defend_damage(Game *g, float amt);
/* Is (x, z) over the carrier's hull? */
bool defend_over_carrier(const Game *g, float x, float z);
vec3 defend_turret_pos(const Game *g, int k);
/* Threats the commander can pick from: active enemy jets and anti-ship
 * missiles, nearest the carrier first. Returns how many were written. */
int  defend_contacts(const Game *g, Ref *out, int max);
/* The commander's controls (from the input layer). */
void defend_cmd_launch(Game *g);

/* ---- the captain on foot ---- */
/* The bridge: a room in the island 17 m above the deck, windows all round. */
#define BRIDGE_X0 25.0f   /* where the captain can stand: an arm's length from the glass */
#define BRIDGE_X1 32.0f
#define BRIDGE_Z0 (-49.0f)
#define BRIDGE_Z1 (-35.2f)
#define BRIDGE_UP 17.0f
/* The island's footprint on the deck (carrier-local). */
#define ISLAND_X0 23.4f
#define ISLAND_X1 33.8f
#define ISLAND_Z0 (-58.6f)
#define ISLAND_Z1 (-25.2f)

enum { USE_NONE, USE_TACTICAL, USE_AIROPS, USE_DOWN, USE_UP, USE_TURRET, USE_JET };
typedef struct { int kind, arg; vec3 at; const char *label; } Usable;

/* What the captain can use from where he stands (USE_NONE if nothing). */
Usable defend_usable(const Game *g);
void   defend_use(Game *g);
/* Carrier-local to world, and the captain's eye in the world. */
vec3   defend_local(const Game *g, vec3 l);
vec3   defend_cap_eye(const Game *g);

#endif
