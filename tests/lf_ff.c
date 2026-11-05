/* lf_ff.c - a libFuzzer target for the floppy forge's unpackers (src/ff): TLPK, TLJ and TLH, which the game's
 * asset_load (src/assets.c) runs on every packed file it reads from the disk. Original code for this project. Build
 * with -DA4C_LIBFUZZER=ON (clang); run it under ASan and UBSan.
 *
 * The first byte picks the unpacker, the rest is the file. Each one must keep what its header promises: NULL or the
 * bytes with a 0 after them; TLPK never more than its limit; TLJ's text is JSON the game's own parser takes (the
 * forge writes it only from JSON). A broken rule aborts. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/json.h"
#include "ff/tlh.h"
#include "ff/tlj.h"
#include "ff/tlpk.h"

static void broken(const char *rule) {
  fprintf(stderr, "lf_ff: %s\n", rule);
  abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  if (n < 1) return 0;
  const uint8_t *in = data + 1;
  size_t m = n - 1, size = 0;
  switch (data[0] % 3) {
  case 0: {
    const size_t limit = 1 << 22; /* 4 MB: more than any asset, less than the fuzzer's memory */
    uint8_t *out = tlpk_unpack(in, m, limit, &size);
    if (out) {
      if (size > limit) broken("TLPK unpacked past its limit");
      if (out[size] != 0) broken("TLPK output without its 0 byte");
      free(out);
    }
    break;
  }
  case 1: {
    char *t = tlj_to_text(in, m, &size);
    if (t) {
      if (t[size] != 0) broken("TLJ text without its 0 byte");
      JsonError e;
      Json *j = json_parse(t, size, &e);
      if (!j) broken("TLJ made text that is not JSON");
      json_free(j);
      free(t);
    }
    break;
  }
  default: {
    uint8_t *h = tlh_unpack(in, m, &size);
    if (h) {
      if (h[size] != 0) broken("TLH output without its 0 byte");
      if (size % 2) broken("TLH samples of an odd byte count");
      free(h);
    }
  }
  }
  return 0;
}
