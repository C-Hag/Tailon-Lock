/* input.h - keyboard sets per seat and SDL3 gamepads (JS BIND, readHuman, pads). */
#ifndef A4C_INPUT_H
#define A4C_INPUT_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "core/game.h"

void input_init(void);
void input_quit(void);
/* Gamepad hot-plug. */
void input_event(const SDL_Event *ev);
int  input_pad_count(void);
/* Writes p->ctl and p->throttle from the keys and pad of seat idx. With one
 * local player both key sets fly the same jet, as in the JS. Returns true
 * on the frame Start is pressed on that seat's pad. */
bool input_read_human(Game *g, Plane *p, float dt, int idx, bool invert);
void input_reset(void);
/* Aim and trigger for a turret player 2 has manned (keys of seat 2, pad 2). */
void input_read_turret(float *yaw, float *pitch, bool *fire, int seat);
/* The captain on foot: move, strafe, turn, look and run (seat 2's keys and pad). */
void input_read_walk(float *move, float *side, float *turn, float *look, bool *run, int seat);
/* Which pad a gamepad event came from (its seat), or -1. */
int  input_pad_seat(SDL_JoystickID id);

#endif
