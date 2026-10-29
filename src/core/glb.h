/* glb.h - our own reader for glTF 2.0 binary files (.glb). Original code,
 * written from the Khronos glTF 2.0 specification (the GLB file format, and
 * the buffers, buffer views, accessors, meshes, materials, nodes and scenes
 * it describes); no other glTF implementation was looked at.
 *
 * Brick 5, the container: a GLB file is a 12-byte header (magic "glTF",
 * version 2, total length) and then chunks, each a length, a type and that
 * many bytes, starting and ending on 4-byte boundaries. The first chunk is
 * the JSON document; a BIN chunk, if any, comes second and holds buffer 0.
 * Chunks of other types are skipped. Every length is checked against the
 * bytes actually there, so a short or lying file is refused, never read past.
 */
#ifndef A4C_GLB_H
#define A4C_GLB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLB_MAGIC      0x46546C67u /* "glTF" */
#define GLB_CHUNK_JSON 0x4E4F534Au /* "JSON" */
#define GLB_CHUNK_BIN  0x004E4942u /* "BIN\0" */

typedef struct {
  const char    *json;     /* the JSON chunk (not zero-terminated) */
  size_t         json_len;
  const uint8_t *bin;      /* the BIN chunk, or NULL */
  size_t         bin_len;
} GlbFile;

/* Finds the chunks of a GLB file held in data[0..n). The result points into
 * data. Returns false, with why in *err if it is given, for anything that is
 * not a well-formed GLB file. */
bool glb_open(const void *data, size_t n, GlbFile *out, const char **err);

#endif
