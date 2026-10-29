/* glb.c - our own reader for glTF 2.0 binary files (.glb). Original code,
 * written from the Khronos glTF 2.0 specification; no other glTF
 * implementation was looked at. See glb.h. */
#include "core/glb.h"

#include <string.h>

/* ---- the container -------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool refuse(const char **err, const char *why) {
  if (err) *err = why;
  return false;
}

bool glb_open(const void *data, size_t n, GlbFile *out, const char **err) {
  const uint8_t *b = data;
  memset(out, 0, sizeof *out);
  if (err) *err = NULL;
  if (!b || n < 12) return refuse(err, "too short for a GLB header");
  if (le32(b) != GLB_MAGIC) return refuse(err, "not a GLB file (no glTF magic)");
  if (le32(b + 4) != 2) return refuse(err, "not GLB version 2");
  uint32_t total = le32(b + 8);
  if (total > n) return refuse(err, "the header's length is past the end of the file");
  if (total < 12 + 8 || total % 4) return refuse(err, "the header's length is not a whole number of 4-byte words");
  size_t at = 12;
  int chunk = 0;
  while (at < total) {
    if (total - at < 8) return refuse(err, "a chunk header is cut short");
    uint32_t len = le32(b + at), type = le32(b + at + 4);
    at += 8;
    if (len > total - at) return refuse(err, "a chunk runs past the end of the file");
    if (len % 4) return refuse(err, "a chunk does not end on a 4-byte boundary");
    if (chunk == 0) {
      if (type != GLB_CHUNK_JSON) return refuse(err, "the first chunk is not JSON");
      if (len == 0) return refuse(err, "the JSON chunk is empty");
      out->json = (const char *)b + at;
      out->json_len = len;
    } else if (chunk == 1 && type == GLB_CHUNK_BIN) {
      out->bin = b + at;
      out->bin_len = len;
    } else if (type == GLB_CHUNK_JSON || type == GLB_CHUNK_BIN) {
      return refuse(err, "a JSON or BIN chunk out of place");
    } /* any other chunk type is skipped, as the format allows */
    at += len;
    chunk++;
  }
  if (!out->json) return refuse(err, "no JSON chunk");
  return true;
}
