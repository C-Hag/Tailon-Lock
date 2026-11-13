/* music.h - the soundtrack, played by the native SID-style synth. */
#ifndef TL_MUSIC_H
#define TL_MUSIC_H

#include <stdbool.h>

#define MUSIC_THEMES 3               /* the ones the menu offers */
#define MUSIC_ALL    (MUSIC_THEMES + 3) /* and the opening film's own cuts */

/* mix: the gain that brings the theme to a common loudness */
typedef struct { const char *key, *name; float play, menu, mix; } Theme;
extern const Theme THEMES[MUSIC_ALL];

bool music_init(void);
void music_quit(void);
/* Starts theme t looping (-1 stops). Does nothing if t is already playing. */
void music_play(int theme);
/* Starts theme t from its first sample, even if it is already playing. */
void music_restart(int theme);
/* Seconds of the current track sent to the sound card since it started, or
 * -1 when nothing is playing (the intro then runs on the wall clock). */
double music_clock(void);
int  music_current(void);
/* A theme can be a recording (assets/music/<key>.wav) instead of a score: it loops as its <key>.loop
 * says, or plays once, and then this is true once it has run out (and so for a score that plays once). */
bool music_finished(void);
bool music_recorded(int theme);
/* Switches to a theme at the current one's place plus offset seconds (one song cut two ways: the opening film's
 * cut hands over to the menu's theme without a seam), a recording from a recording or a score from a score. */
void music_continue(int theme, double offset);

/* menu or in-game level, as the JS TRACKS table sets it */
void music_level(bool in_game);
int  music_theme_index(const char *key);
/* The theme's score (loaded on first use; the editor changes it in place). */
struct Score *music_score(int theme);

/* Called by the mixer on the audio thread with its lock held: adds n stereo
 * frames at 48 kHz into out. */
void music_render(float *out, int n);

#endif
