/* synth.h - the SID-style synthesizer that plays the soundtrack: pulse, saw,
 * triangle and noise voices with ADSR, pulse-width sweeps, delayed vibrato
 * and a state-variable filter, driven by the scores in assets/music
 * (tools/extract_scores.js pulls them out of the Web Audio engines). */
#ifndef A4C_SYNTH_H
#define A4C_SYNTH_H

#include <stdbool.h>
#include <stdint.h>

#define SYNTH_RATE 48000

enum { W_PULSE, W_SAW, W_TRI, W_NOISE, W_SINE };
enum { F_NONE, F_LP, F_HP, F_BP };
/* Drum and effect hits */
enum { E_DRUM, E_JET, E_RISER, E_WAVE, E_SONAR, E_SWELL };

/* One instrument's sound. */
typedef struct {
  uint8_t wave, filter;
  float   level, pan;
  float   a, d, s, r;          /* seconds, sustain level */
  float   pw, pw_depth, pw_rate;
  float   vib_delay, vib_rate, vib_depth; /* semitones */
  float   cut0, cut1, cut_t, q; /* cutoff sweeps from cut0 to cut1 over cut_t */
  float   sweep_to, sweep_t;    /* pitch sweep (drums): ratio, seconds */
  float   octave;               /* extra octave layer level (0 = none) */
} Patch;

/* bend: the note starts this many semitones low and bends up to its pitch */
typedef struct { float t, dur, vel; int16_t inst; int8_t midi, bend; bool sel; } SongNote;
typedef struct { float t, vel, a1, a2, a3; int8_t effect; char code[4]; } SongHit;
typedef struct { char name[24]; Patch p; bool mute, solo; } SongInst;
#define SONG_MAX_INST 24

/* A theme: notes per instrument (sorted by time), drum and effect hits, the
 * instruments' patches, and where it ends and loops back to. The editor
 * changes these in place, under sfx_lock, then calls synth_sort and
 * synth_seek. */
typedef struct Score {
  char      key[16];
  float     end, loop;
  SongNote *notes;
  int       nnotes, capn;
  SongHit  *hits;
  int       nhits, caph;
  SongInst  inst[SONG_MAX_INST];
  int       ninst;
  bool      mute_drums;
} Score;

/* Loads assets/music/<key>.json; NULL if missing. Kept for the program's life. */
Score *synth_load(const char *key);
bool   synth_save(const Score *s, const char *path);
void   synth_free(Score *s);
bool   synth_default_patch(const char *name, Patch *out);
int    synth_inst(Score *s, const char *name); /* index, adding it if new */
SongNote *synth_add_note(Score *s, float t, int inst, int midi, float dur, float vel);
void   synth_sort(Score *s);
void   synth_seek(double t);       /* with the mixer locked */
double synth_song_time(void);      /* position in the score */
/* Called on the audio thread with the mixer locked. */
void   synth_start(Score *s);       /* from the first sample */
void   synth_stop(void);
bool   synth_playing(void);
double synth_time(void);            /* seconds since start (not wrapped) */
bool   synth_wrapped(void);         /* has it looped back yet? */
void   synth_render(float *out, int n, float gain); /* adds n stereo frames */

#endif
