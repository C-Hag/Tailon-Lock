/* sfx.h - sound-effect voices and engine drones mixed in the audio callback (JS sNoise, sTone, engines). */
#ifndef A4C_SFX_H
#define A4C_SFX_H

#include <stdbool.h>

#include "core/game.h"

bool sfx_init(void);
void sfx_quit(void);
void sfx_enable(bool on);
/* Music on/off; sound off silences it too, as in the JS. */
void sfx_music(bool on);
/* Holds the mixer still while the main thread changes what it plays. */
void sfx_lock(void);
void sfx_unlock(void);
/* Plays everything g->sounds holds and empties it. */
void sfx_drain(Game *g);
/* Plays one sound right away. */
void sfx_play(SoundKind kind, float f0, float f1, float dur, float vol);
/* Engine drones follow the local jets; paused or NULL silences them. */
void sfx_engines(const Game *g, bool paused);
/* Test hook (A4C_MIX_DUMP): with no sound device, mix frames now and write them to the dump file. */
void sfx_dump_tick(int frames);
bool sfx_dumping(void);

#endif
