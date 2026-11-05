/* tlpk.c - the TLPK unpacker the game carries: the coding model of tlpk_model.h, reading. Every distance and length
 * is checked against what has been unpacked so far, so a damaged file stops with NULL, never a read or write out of
 * bounds; the CRC-32 of the result must match the header's. Original code for this project. */
#include "ff/tlpk.h"

#include "ff/tlpk_model.h"

bool tlpk_is(const uint8_t *in, size_t n) { return n >= TLPK_HEADER && !memcmp(in, "TLPK", 4) && in[4] == TLPK_VERSION; }

uint8_t *tlpk_unpack(const uint8_t *in, size_t n, size_t limit, size_t *size) {
  if (!tlpk_is(in, n)) return NULL;
  int kind = in[5], param = in[6], o2bits = in[7];
  uint32_t raw = rd32(in + 8), crc = rd32(in + 12);
  if (raw > limit || o2bits < 16 || o2bits > 22) return NULL;
  uint8_t *out = malloc((size_t)raw + 1);
  Model *m = malloc(sizeof *m);
  if (!out || !m) {
    free(out), free(m);
    return NULL;
  }
  if (!stretch_t[4095]) stretch_init();
  model_init(m, o2bits);
  Rc r;
  rc_init(&r, in + TLPK_HEADER, n - TLPK_HEADER);
  uint32_t pos = 0;
  bool ok = true;
  while (pos < raw) {
    int ps = pos & 3;
    if (!bit_ad(&r, &m->is_match[m->state][ps], 0)) {
      int mb = pos >= m->rep[0] ? out[pos - m->rep[0]] : -1;
      int b = lit_code(&r, &m->lit, pos ? out[pos - 1] : 0, pos > 1 ? out[pos - 2] : 0, mb, m->mrun, 0);
      m->mrun = mb == b ? m->mrun + 1 : 0;
      out[pos++] = (uint8_t)b;
      m->state = next_state(m->state, K_LIT);
      continue;
    }
    int tk = 0, k = 0;
    uint32_t len = 0, dist = 0;
    token_code(&r, m, ps, &tk, &k, &len, &dist);
    if (dist == 0 || dist > pos || len > raw - pos) {
      ok = false;
      break;
    }
    for (uint32_t i = 0; i < len; i++, pos++) out[pos] = out[pos - dist];
    m->mrun = 0;
    m->state = next_state(m->state, tk);
  }
  lit_free(&m->lit);
  free(m);
  if (ok) filter(out, raw, kind, param, 0);
  if (!ok || crc32_of(out, raw) != crc) {
    free(out);
    return NULL;
  }
  out[raw] = 0;
  if (size) *size = raw;
  return out;
}
