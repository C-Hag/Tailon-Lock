/* lf_wav.c - a libFuzzer target for the WAV reader (plat_load_wav in
 * src/platform/plat_core.c): the chunks, every sample format it takes, and
 * the resampler to the game's rate. Original code for this project. Build
 * with -DTL_LIBFUZZER=ON (clang); run it under ASan and UBSan.
 *
 * The input is the file, written to the fuzzer's own folder and read at the
 * mixer's rate, as the music player does. plat_core.c is linked as it is,
 * with a stand-in backend (as tests/test_plat_core.c). */
#define LF_PLAT_CORE
#include "lf_common.h" /* first: it asks for POSIX */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "platform/plat_core.h"
#include "synth.h"

/* ---- the stand-in backend ---- */
bool plat_os_init(unsigned parts) { return (void)parts, true; }
void plat_os_quit(void) {}
void plat_os_pump(void) {}
void plat_os_pad_open(int id) { (void)id; }
void plat_os_pad_close(int id) { (void)id; }
uint64_t plat_counter(void) { return (uint64_t)clock(); }
uint64_t plat_counter_hz(void) { return CLOCKS_PER_SEC; }
const PlatApp *plat_app(void) { return NULL; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  static char path[600];
  if (!path[0]) snprintf(path, sizeof path, "%s/in.wav", lf_folder(NULL));
  lf_write("in.wav", data, n);
  size_t frames = 0;
  float *s = plat_load_wav(path, SYNTH_RATE, &frames);
  if (s) {
    volatile float sum = 0;
    for (size_t i = 0; i < frames * 2; i++) sum += s[i]; /* every frame it says it made is there */
    free(s);
  }
  return 0;
}
