/* cheat.h - the cheater (--pilot cheat): a test pilot that tries every
 * trick to get ahead, and the rule watch that holds every jet to the rules
 * each tick and reports what was broken (cheat.c says what it tries). */
#ifndef TL_CHEAT_H
#define TL_CHEAT_H

#include <stdio.h>

#include "core/game.h"

/* A new match: cheater is the plane the cheater flies (-1: just watch the rules); k picks the trick. */
void        cheat_start(Game *g, int cheater, int k, uint32_t seed);
/* The cheater's hands, after the CPU's decision (testpilot.c calls it). */
void        cheat_hands(Game *g, Plane *p, float dt);
/* Every tick: every jet against the rules. */
void        cheat_watch(Game *g, float dt);
/* This match's trick, for the viewer ("" without a cheater). */
const char *cheat_trick(void);
/* The match is over: logs the findings and writes them to the report. */
void        cheat_match_end(const Game *g, FILE *report);
/* The findings over every match; returns how many rules were broken. */
int         cheat_summary(FILE *report);

#endif
