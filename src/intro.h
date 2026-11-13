/* intro.h - the opening cinematic: plays assets/intro/intro_script.json
 * (format: assets/intro/INTRO.md) on the clock of Steel Horizon, then holds
 * its last shot drifting behind the menu. */
#ifndef TL_INTRO_H
#define TL_INTRO_H

#include <stdbool.h>

#include "raster.h"

/* Loads the script. Returns false if the files are missing. */
bool intro_init(void);
bool intro_available(void);
/* Starts from the first frame (and restarts the music when it is on). */
void intro_start(bool music);
/* Advances; returns true once the intro has reached the menu. */
bool intro_update(float dt);
/* Jumps to the menu point (a skip): the last shot carries on behind the menu. */
void intro_skip(void);
bool intro_holding(void);
/* The music theme the film is cut to (the script's music.track). */
int  intro_music(void);
/* Seconds the film's cut of its music runs behind the song itself after its repeated bars: the menu's theme takes
 * over that much earlier in the song. */
float intro_song_shift(void);
/* Seconds since the start, for skipping only after the first half second. */
float intro_time(void);
void intro_render(Framebuffer *fb);

#endif
