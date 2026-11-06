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
/* Master volume of the whole mix, 1-10 in 3 dB steps (10 is full level). */
void sfx_volume(int level);
/* Seconds of sound played since the start (sound on), for the hearing break reminder. */
double sfx_listened(void);
/* Holds the mixer still while the main thread changes what it plays. */
/* The music's level in the mix, 0..1, eased without a click; only ever lowers it. The film's kills use it; 1 restores it. */
void sfx_duck_music(float gain);
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
/* Test hook (--render-sfx DIR): every effect the game makes, each over the Full Burn chorus at its in-game level
 * and the current master volume, as DIR/sfx-NAME.wav (float stereo, 48 kHz), for the ear check. */
bool sfx_render_effects(const char *dir);

#endif
