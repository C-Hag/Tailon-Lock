/* input.h - keyboard sets per seat and gamepads (JS BIND, readHuman, pads). */
#ifndef A4C_INPUT_H
#define A4C_INPUT_H

#include <stdbool.h>

#include "core/game.h"
#include "platform/plat.h"

void input_init(void);
void input_quit(void);
/* Gamepad hot-plug. */
void input_event(const PlatEvent *ev);
int  input_pad_count(void);
/* Between matches (the menu): seats waiting for a pad that went away are let go, and the pads still there move
 * up to fill them, so a lone pad is player 1 again. During a match a seat waits for its own pad. */
void input_settle(void);
/* Is this seat's pad away, the seat waiting for it to come back? */
bool input_seat_waiting(int seat);
/* Writes p->ctl and p->throttle from the keys and pad of seat idx. With one
 * local player both key sets fly the same jet, as in the JS. Returns true
 * on the frame Start is pressed on that seat's pad. */
bool input_read_human(Game *g, Plane *p, float dt, int idx, bool invert);
void input_reset(void);
/* How the stick follows the hands: keys (-1, 0 or 1) ease in, a pad's
 * stick follows quickly. The test pilots fly through this too. */
void input_shape(Controls *c, float pitch, float roll, bool analog, float dt);
/* Aim and trigger for a turret player 2 has manned (keys of seat 2, pad 2). */
void input_read_turret(float *yaw, float *pitch, bool *fire, int seat);
/* The captain on foot: move, strafe, turn, look and run (seat 2's keys and pad). */
void input_read_walk(float *move, float *side, float *turn, float *look, bool *run, int seat);
/* Which pad a gamepad event came from (its seat), or -1. */
int  input_pad_seat(int id);

#endif
