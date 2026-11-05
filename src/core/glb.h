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
 *
 * Brick 6, buffer views and accessors: buffer 0 is the BIN chunk (buffers
 * with a uri are not read). A buffer view is a byte range of a buffer, with
 * an optional stride; an accessor is a typed array in a view: count elements
 * of SCALAR, VEC2-4 or MAT2-4, each component a signed or unsigned 8-, 16- or
 * 32-bit integer or a 32-bit float, read little-endian. Offsets must be
 * aligned to the component size, matrix columns start on 4-byte boundaries,
 * and the last element must end inside its view, which must end inside its
 * buffer. Sparse accessors and accessors without a view are refused.
 *
 * Brick 7, meshes, materials and nodes, into our mesh format (core/mesh.h):
 * the scene's node tree is walked with each node's matrix or translation,
 * rotation and scale; every triangle primitive (list, strip or fan) of every
 * mesh is added, with the colour of its material. Our own writer stores a
 * mesh so that it reads back exactly: the vertices once, as 32-bit floats;
 * the faces in their order, one primitive for each run of faces of one
 * colour, as triangle fans; and, in the primitives' and materials' extras
 * (the format's place for an application's own data), each face's corner
 * count, the exact colour and the mesh's radius. Other glTF readers see
 * ordinary coloured triangles. */
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

struct Json;

enum { GLB_I8 = 5120, GLB_U8 = 5121, GLB_I16 = 5122, GLB_U16 = 5123, GLB_U32 = 5125, GLB_F32 = 5126 };

typedef struct {
  GlbFile      file;
  struct Json *doc;  /* the JSON document */
} GlbModel;

typedef struct {
  const uint8_t *data;       /* the first element */
  size_t         count;
  int            comps;      /* components an element: 1 (SCALAR) to 16 (MAT4) */
  int            rows;       /* a matrix's rows (2-4), or 0 for scalars and vectors */
  int            ctype;      /* GLB_I8 ... GLB_F32 */
  int            csize;      /* bytes a component */
  size_t         elem_size;  /* bytes an element (matrix columns padded to 4 bytes) */
  size_t         stride;     /* bytes from one element to the next */
  bool           normalized; /* integers read as -1..1 or 0..1 */
} GlbAccessor;

/* Opens a GLB file and parses its JSON; the model points into data, which
 * must stay valid until glb_close. */
bool glb_load(const void *data, size_t n, GlbModel *m, const char **err);
void glb_close(GlbModel *m);
/* Accessor number index of the model, checked against its view and buffer. */
bool glb_accessor(const GlbModel *m, int index, GlbAccessor *out, const char **err);
/* Component comp (column-major for matrices) of element i, as a double: the
 * integer value, or the float, or the normalized value when normalized. */
double glb_get(const GlbAccessor *a, size_t i, int comp);

struct Mesh;

/* The model's default scene as one mesh (cleared first). False, with why, if
 * there is no triangle to draw or the mesh would pass 65535 vertices. */
bool glb_mesh(const GlbModel *m, struct Mesh *out, const char **err);
/* A .glb file holding mesh m; free() it. NULL when out of memory. */
uint8_t *glb_write_mesh(const struct Mesh *m, const char *name, size_t *len);

#endif
