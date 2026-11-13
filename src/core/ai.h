/* ai.h - CPU pilots with easy/normal/hard tables, DIFF and aiControl (JS lines 2890-2974). */
#ifndef TL_AI_H
#define TL_AI_H

#include "core/game.h"

typedef struct { float maxIn, react, gunCone, mslGap, flareP, aimErr, lead, fightR; } DiffTable;
extern const DiffTable DIFF[3];

void ai_control(Game *g, Plane *p, float dt);

#endif
