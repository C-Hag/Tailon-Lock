/* lf_net.c - a libFuzzer target for every packet the game reads (src/net.c):
 * the LAN beacons, the relay's answers, the handshake (CPace hello,
 * challenge, sealed join, reject), the welcome, the match's state and events,
 * and the watch stream's hello, answer, INFO, STATE and SHOTS. Original code
 * for this project. Build with -DTL_LIBFUZZER=ON (clang); run it under ASan
 * and UBSan.
 *
 * An input is a phase byte, an option byte and packets (net_fuzz in net.c
 * says the layout); each packet goes to the handler the game's receive loop
 * calls, with no socket. The platform here is a stand-in (lf_common.h, and
 * "random" numbers that start the same for every input, so a crash replays).
 *
 * TL_LF_SEEDS=DIR lf_net writes the seeds (packets from the game's own
 * builders, net_fuzz_seed) into DIR and stops: tests/corpus/net was made so. */
#include "lf_common.h" /* first: it asks for POSIX */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "platform/plat.h"

/* ---- the stand-in platform ---- */
static uint64_t rng;
int plat_random(void *buf, size_t size) {
  uint8_t *b = buf;
  for (size_t i = 0; i < size; i++) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    b[i] = (uint8_t)(rng >> 24);
  }
  return 1;
}
bool plat_is_file(const char *path) { return (void)path, false; }
const char *plat_base_path(void) { return NULL; }
bool plat_file_sync(FILE *f) { return (void)f, false; } /* plat_common.c's saves: the targets save nothing */
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }

static Game g;
static void start(void) {
  rng = 0x9E3779B97F4A7C15ull;
  lf_clock = 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc, (void)argv;
  lf_folder(NULL);
  const char *dir = getenv("TL_LF_SEEDS");
  if (!dir) return 0;
  static uint8_t seed[1 << 16];
  size_t n;
  for (int k = 0; start(), (n = net_fuzz_seed(&g, k, seed, sizeof seed)) > 0; k++) {
    char path[600];
    snprintf(path, sizeof path, "%s/seed-%02d", dir, k);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(seed, 1, n, f) != n || fclose(f) != 0) {
      perror(path);
      exit(1);
    }
  }
  exit(0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  start();
  net_fuzz(&g, data, size);
  return 0;
}
