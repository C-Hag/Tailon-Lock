/* glb.c - our own reader for glTF 2.0 binary files (.glb). Original code,
 * written from the Khronos glTF 2.0 specification; no other glTF
 * implementation was looked at. See glb.h. */
#include "core/glb.h"

#include <math.h>
#include <string.h>

#include "core/json.h"

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

/* ---- buffer views and accessors ------------------------------------------ */

bool glb_load(const void *data, size_t n, GlbModel *m, const char **err) {
  memset(m, 0, sizeof *m);
  if (!glb_open(data, n, &m->file, err)) return false;
  JsonError je;
  m->doc = json_parse(m->file.json, m->file.json_len, &je);
  if (!m->doc) return refuse(err, "the JSON chunk is not valid JSON");
  if (!json_is(m->doc, JSON_OBJECT)) {
    glb_close(m);
    return refuse(err, "the JSON chunk is not an object");
  }
  return true;
}

void glb_close(GlbModel *m) {
  json_free(m->doc);
  m->doc = NULL;
}

/* a whole number in 0..max, as the format's integer fields must be */
static bool whole(const Json *v, double max, size_t *out) {
  if (!json_is(v, JSON_NUMBER)) return false;
  double d = v->num;
  if (!(d >= 0 && d <= max) || d != floor(d)) return false;
  *out = (size_t)d;
  return true;
}

/* an optional whole number: the default when it is absent */
static bool whole_or(const Json *obj, const char *name, double max, size_t def, size_t *out) {
  const Json *v = json_get(obj, name);
  if (!v) {
    *out = def;
    return true;
  }
  return whole(v, max, out);
}

static int csize_of(int ctype) {
  switch (ctype) {
  case GLB_I8: case GLB_U8: return 1;
  case GLB_I16: case GLB_U16: return 2;
  case GLB_U32: case GLB_F32: return 4;
  default: return 0;
  }
}

bool glb_accessor(const GlbModel *m, int index, GlbAccessor *out, const char **err) {
  memset(out, 0, sizeof *out);
  const Json *acc = json_at(json_get(m->doc, "accessors"), index);
  if (!json_is(acc, JSON_OBJECT)) return refuse(err, "no such accessor");
  if (json_get(acc, "sparse")) return refuse(err, "sparse accessors are not supported");
  size_t view_i, acc_off, count, ctype;
  if (!json_get(acc, "bufferView")) return refuse(err, "an accessor without a buffer view is not supported");
  if (!whole(json_get(acc, "bufferView"), 1e9, &view_i)) return refuse(err, "an accessor's bufferView is not an index");
  if (!whole_or(acc, "byteOffset", 4294967295.0, 0, &acc_off)) return refuse(err, "an accessor's byteOffset is not valid");
  if (!whole(json_get(acc, "count"), 4294967295.0, &count) || count < 1) return refuse(err, "an accessor's count is not valid");
  if (!whole(json_get(acc, "componentType"), 65535, &ctype) || !csize_of((int)ctype))
    return refuse(err, "an accessor's componentType is not valid");
  const char *type = json_str(json_get(acc, "type"));
  static const struct { const char *name; int comps, rows; } TYPES[] = {
    { "SCALAR", 1, 0 }, { "VEC2", 2, 0 }, { "VEC3", 3, 0 }, { "VEC4", 4, 0 }, { "MAT2", 4, 2 }, { "MAT3", 9, 3 }, { "MAT4", 16, 4 },
  };
  int t = -1;
  for (int i = 0; type && i < 7; i++)
    if (!strcmp(type, TYPES[i].name)) t = i;
  if (t < 0) return refuse(err, "an accessor's type is not valid");
  const Json *nv = json_get(acc, "normalized");
  bool normalized = json_is_true(nv);
  if (nv && !json_is(nv, JSON_TRUE) && !json_is(nv, JSON_FALSE)) return refuse(err, "an accessor's normalized is not a boolean");
  if (normalized && (ctype == GLB_U32 || ctype == GLB_F32)) return refuse(err, "only 8- and 16-bit integers can be normalized");

  const Json *view = json_at(json_get(m->doc, "bufferViews"), (int)view_i);
  if (view_i > 0x7FFFFFFF || !json_is(view, JSON_OBJECT)) return refuse(err, "an accessor's buffer view does not exist");
  size_t buf_i, view_off, view_len, stride;
  if (!whole(json_get(view, "buffer"), 1e9, &buf_i)) return refuse(err, "a buffer view's buffer is not an index");
  if (!whole_or(view, "byteOffset", 4294967295.0, 0, &view_off)) return refuse(err, "a buffer view's byteOffset is not valid");
  if (!whole(json_get(view, "byteLength"), 4294967295.0, &view_len) || view_len < 1)
    return refuse(err, "a buffer view's byteLength is not valid");
  bool has_stride = json_get(view, "byteStride") != NULL;
  if (!whole_or(view, "byteStride", 252, 0, &stride) || (has_stride && (stride < 4 || stride % 4)))
    return refuse(err, "a buffer view's byteStride is not valid");

  const Json *buf = json_at(json_get(m->doc, "buffers"), (int)buf_i);
  size_t buf_len;
  if (buf_i != 0 || !json_is(buf, JSON_OBJECT)) return refuse(err, "only buffer 0, the BIN chunk, can be read");
  if (json_get(buf, "uri")) return refuse(err, "buffer 0 has a uri: external and embedded data are not read");
  if (!whole(json_get(buf, "byteLength"), 4294967295.0, &buf_len) || buf_len < 1) return refuse(err, "buffer 0's byteLength is not valid");
  if (!m->file.bin || buf_len > m->file.bin_len) return refuse(err, "buffer 0 is longer than the BIN chunk");
  if (view_off > buf_len || view_len > buf_len - view_off) return refuse(err, "a buffer view runs past its buffer");

  int csize = csize_of((int)ctype), rows = TYPES[t].rows, comps = TYPES[t].comps;
  size_t elem = rows ? (size_t)rows * (((size_t)rows * (size_t)csize + 3) & ~(size_t)3) : (size_t)comps * (size_t)csize;
  if (!has_stride) stride = elem;
  if (stride < elem) return refuse(err, "a buffer view's byteStride is shorter than an element");
  if ((view_off + acc_off) % (size_t)csize || stride % (size_t)csize) return refuse(err, "an accessor is not aligned to its component size");
  if (acc_off > view_len || elem > view_len - acc_off || (count - 1) > (view_len - acc_off - elem) / stride)
    return refuse(err, "an accessor runs past its buffer view");

  out->data = m->file.bin + view_off + acc_off;
  out->count = count;
  out->comps = comps;
  out->rows = rows;
  out->ctype = (int)ctype;
  out->csize = csize;
  out->elem_size = elem;
  out->stride = stride;
  out->normalized = normalized;
  if (err) *err = NULL;
  return true;
}

double glb_get(const GlbAccessor *a, size_t i, int comp) {
  if (!a->data || i >= a->count || comp < 0 || comp >= a->comps) return 0;
  size_t at = i * a->stride;
  if (a->rows) { /* column-major: each column starts on a 4-byte boundary */
    int col = comp / a->rows, row = comp % a->rows;
    at += (size_t)col * (((size_t)a->rows * (size_t)a->csize + 3) & ~(size_t)3) + (size_t)row * (size_t)a->csize;
  } else at += (size_t)comp * (size_t)a->csize;
  const uint8_t *p = a->data + at;
  uint32_t u = p[0];
  if (a->csize >= 2) u |= (uint32_t)p[1] << 8;
  if (a->csize == 4) u |= (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  switch (a->ctype) {
  case GLB_I8: return a->normalized ? fmax((int8_t)u / 127.0, -1.0) : (int8_t)u;
  case GLB_U8: return a->normalized ? u / 255.0 : u;
  case GLB_I16: return a->normalized ? fmax((int16_t)u / 32767.0, -1.0) : (int16_t)u;
  case GLB_U16: return a->normalized ? u / 65535.0 : u;
  case GLB_U32: return u;
  default: {
    float f;
    memcpy(&f, &u, 4);
    return f;
  }
  }
}
