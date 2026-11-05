/* lf_song.c - a libFuzzer target for the songs (src/synth.c): the score
 * files in assets/music and the ones the music editor saves, read by
 * synth_load. Original code for this project. Build with -DA4C_LIBFUZZER=ON
 * (clang); run it under ASan and UBSan.
 *
 * The input is the song's JSON, written as music/fz.json in the fuzzer's own
 * assets folder. A song that loads must point every note at one of its
 * instruments and every hit at an effect there is; it is then saved as the
 * editor saves it and read back (the same notes and hits), and played for a
 * moment from its start and from its loop point. A broken rule aborts. */
#include "lf_common.h" /* first: it asks for POSIX */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"
#include "synth.h"

static void broken(const char *rule) {
  fprintf(stderr, "lf_song: %s\n", rule);
  abort();
}

static void check(const Score *s) {
  if (s->nnotes < 0 || s->nhits < 0 || s->ninst < 0 || s->ninst > SONG_MAX_INST) broken("counts out of range");
  for (int i = 0; i < s->nnotes; i++)
    if (s->notes[i].inst < 0 || s->notes[i].inst >= s->ninst) broken("a note on an instrument the song has not");
  for (int i = 0; i < s->nhits; i++)
    if (s->hits[i].effect < E_DRUM || s->hits[i].effect > E_SWELL) broken("a hit of no effect");
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  static char saved[700];
  if (!saved[0]) {
    static const char *const subs[] = { "assets", "assets/music", NULL };
    char d[600];
    snprintf(d, sizeof d, "%s/assets/", lf_folder(subs));
    assets_fuzz_dir(d);
    snprintf(saved, sizeof saved, "%smusic/fz2.json", d);
  }
  lf_write("assets/music/fz.json", data, n);
  Score *s = synth_load("fz");
  if (!s) return 0;
  check(s);
  if (synth_save(s, saved)) {
    Score *b = synth_load("fz2");
    if (!b) broken("a saved song that does not read back");
    check(b);
    if (b->nnotes != s->nnotes || b->nhits != s->nhits) broken("a saved song with other notes or hits");
    synth_free(b);
  }
  static float out[2 * 2048];
  synth_start(s);
  synth_render(out, 2048, 1);
  synth_seek(s->loop);
  synth_render(out, 2048, 1);
  synth_stop();
  synth_render(out, 256, 1);
  synth_free(s);
  return 0;
}
