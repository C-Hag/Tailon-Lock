/* testpilot.h - the test pilots: CPU pilots flown through human hands (four
 * kinds of player), what they measure about how the game flies and fights,
 * the mission checks every jet goes through, and the written report. */
#ifndef TL_TESTPILOT_H
#define TL_TESTPILOT_H

#include <stdio.h>

#include "core/game.h"

enum { TP_FIGHTER, TP_SIMMER, TP_ARCADE, TP_ROOKIE, TP_CHEAT, TP_COUNT };
#define TP_FAIR 4   /* the four who play fair */
#define TP_ALL  100 /* --pilot all: the four take turns */

/* --pilot NAME: fighter, simmer, arcade, rookie, cheat or all; -1 for none. */
int  tp_persona_of(const char *name);
/* A new match: the players' seats get test pilots (persona -1: plain CPU
 * pilots); k numbers the match, for taking turns; seed is the match's, for
 * the report and the hands' own dice. */
void tp_start(Game *g, int persona, int k, uint32_t seed);
/* After every tick: measures every jet and runs the mission checks. */
void tp_tick(Game *g, float dt);
/* For the viewer: who flies plane i ("" for a plain CPU pilot), and what
 * they last said (NULL when nothing lately). */
const char *tp_who(int i);
const char *tp_said(const Game *g, int i);
/* Watching another computer's match: who flies plane i there, and what they said. */
void tp_remote(const Game *g, int i, const char *who, const char *said);
/* The match is over (or stopped): logs it and, with a report file, writes
 * its section; tp_summary writes the totals over every match so far. */
void tp_match_end(const Game *g, FILE *report);
void tp_summary(FILE *report);
/* --test-pilots: whole matches with no window, as fast as they run.
 * --matches N, --pilot NAME, --seed S, --report FILE. Returns non-zero
 * when a mission check failed. Needs the assets loaded. */
int  tp_batch(int argc, char **argv);

#endif
