/* tlh.c - TLH terrain heights back into a .h16 file's bytes: the model of tlh_model.h, reading. Original code for this
 * project. */
#include "ff/tlh.h"

#include "ff/tlh_model.h"

bool tlh_is(const uint8_t *in, size_t n) { return n >= TLH_HEADER && !memcmp(in, "TLH1", 4); }

uint8_t *tlh_unpack(const uint8_t *in, size_t n, size_t *size) {
  if (!tlh_is(in, n)) return NULL;
  int w = in[4] | in[5] << 8, h = in[6] | in[7] << 8;
  uint32_t crc = rd32(in + 8);
  if (w < 1 || h < 1 || w > 4096 || h > 4096) return NULL;
  size_t ns = (size_t)w * (size_t)h;
  int16_t *s = calloc(ns, sizeof *s); /* zeroed: the shared model reads the sample it is about to fill (the encoder's, unused here) */
  uint8_t *out = malloc(ns * 2 + 1);
  if (!s || !out) {
    free(s), free(out);
    return NULL;
  }
  Rc r;
  rc_init(&r, in + TLH_HEADER, n - TLH_HEADER);
  bool ok = tlh_grid(&r, w, h, s);
  for (size_t i = 0; ok && i < ns; i++) out[2 * i] = (uint8_t)(s[i] & 255), out[2 * i + 1] = (uint8_t)((uint16_t)s[i] >> 8);
  free(s);
  if (!ok || crc32_of(out, ns * 2) != crc) {
    free(out);
    return NULL;
  }
  out[ns * 2] = 0;
  if (size) *size = ns * 2;
  return out;
}
