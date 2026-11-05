/* compose.h - the music editor's rewrite tools: key detection, a melody
 * model learned from the game's own themes, and note transforms. Pure
 * functions over a Score; the editor locks the mixer around them. */
#ifndef A4C_COMPOSE_H
#define A4C_COMPOSE_H

#include "synth.h"

typedef struct { int root; bool minor; } Key;

/* Krumhansl-Schmuckler: the key whose profile best matches the time each
 * pitch class sounds. */
Key   compose_key(const Score *s);
/* The song's sixteenth: the commonest gap between note starts. */
float compose_step(const Score *s);
const char *compose_key_name(Key k);

/* Learns the melody model from every theme it is given (call once per score). */
void  compose_learn(const Score *s);

/* Each works on one instrument in [t0, t1) (or the selected notes), with a
 * seed so pressing again gives another take. Return how many notes changed. */
int compose_melody(Score *s, int inst, float t0, float t1, uint32_t seed);
int compose_vary(Score *s, uint32_t seed);
int compose_harmonize(Score *s);
int compose_transpose(Score *s, int semis);
int compose_mode_swap(Score *s);
int compose_humanize(Score *s, uint32_t seed);
int compose_arpeggiate(Score *s);

#endif
