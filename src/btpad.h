/* btpad.h - the Bluetooth pad simulator (tests only): made-up wireless pads, played from a script
 * (--pad), sent through a radio model (--btpad) into the platform's virtual pads (plat_vpad_*). */
#ifndef A4C_BTPAD_H
#define A4C_BTPAD_H

#include <stdbool.h>

/* --btpad SPEC: the radio, as key=value pairs split by commas (see btpad.c). */
bool btpad_radio(const char *spec);
/* --pad F:N:WHAT[:HOLD]: what the hand on pad N does at frame F (pF: F frames after the first match starts, so a
 * test's timing does not hang on how fast the menus before it run). */
bool btpad_script(const char *step);
/* Is the simulator on (a --btpad or a --pad was given)? */
bool btpad_on(void);
/* After plat_init: the pads switched on before the game starts. */
void btpad_start(void);
/* Once a frame, after the frame: the hand, the pad's reports and the radio. */
void btpad_frame(long frame);
/* The game is playing a match (each frame it is): the pF steps are timed from the first such frame. */
void btpad_playing(long frame);
void btpad_stop(void);

#endif
