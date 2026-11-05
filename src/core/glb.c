/* glb.c - our own reader for glTF 2.0 binary files (.glb). Original code,
 * written from the Khronos glTF 2.0 specification; no other glTF
 * implementation was looked at. See glb.h. */
#include "core/glb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/mesh.h"

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

/* ---- meshes, materials and nodes ------------------------------------------ */

/* sRGB transfer (the format's colours are linear) */
static double to_linear(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double to_srgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055; }
static uint32_t byte_of(double c) {
  c = to_srgb(c < 0 ? 0 : c > 1 ? 1 : c) * 255 + 0.5;
  return c < 0 ? 0 : c > 255 ? 255 : (uint32_t)c;
}

/* a face colour (0xRRGGBB, plus FACE_GLOW) from material number mi */
static uint32_t material_colour(const GlbModel *m, int mi) {
  const Json *mat = json_at(json_get(m->doc, "materials"), mi);
  if (!mat) return 0xCCCCCC;
  const Json *tl = json_get(json_get(mat, "extras"), "tl_color");
  if (json_is_number(tl) && tl->num >= 0 && tl->num <= 0xFFFFFFFF && tl->num == floor(tl->num)) return (uint32_t)tl->num;
  const Json *bc = json_get(json_get(mat, "pbrMetallicRoughness"), "baseColorFactor");
  uint32_t c = 0xFFFFFF;
  if (json_count(bc) >= 3)
    c = byte_of(json_number(json_at(bc, 0), 1)) << 16 | byte_of(json_number(json_at(bc, 1), 1)) << 8 | byte_of(json_number(json_at(bc, 2), 1));
  const Json *em = json_get(mat, "emissiveFactor");
  if (json_count(em) >= 3 && json_number(json_at(em, 0), 0) + json_number(json_at(em, 1), 0) + json_number(json_at(em, 2), 0) > 0)
    c |= FACE_GLOW;
  return c;
}

typedef struct {
  double m[16]; /* column-major, as the format stores matrices */
  bool   identity;
} Xform;

static Xform xf_identity(void) {
  Xform x = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, true };
  return x;
}

static Xform xf_mul(const Xform *a, const Xform *b) {
  if (b->identity) return *a;
  if (a->identity) return *b;
  Xform r = { { 0 }, false };
  for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
      double v = 0;
      for (int k = 0; k < 4; k++) v += a->m[k * 4 + row] * b->m[c * 4 + k];
      r.m[c * 4 + row] = v;
    }
  return r;
}

/* a node's own transform: its matrix, or translation x rotation x scale */
static bool node_xform(const Json *node, Xform *out) {
  *out = xf_identity();
  const Json *mx = json_get(node, "matrix");
  if (mx) {
    if (json_count(mx) != 16) return false;
    for (int i = 0; i < 16; i++) out->m[i] = json_number(json_at(mx, i), 0);
    static const double ID[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    out->identity = memcmp(out->m, ID, sizeof ID) == 0;
    return true;
  }
  const Json *t = json_get(node, "translation"), *r = json_get(node, "rotation"), *s = json_get(node, "scale");
  if ((t && json_count(t) != 3) || (r && json_count(r) != 4) || (s && json_count(s) != 3)) return false;
  if (!t && !r && !s) return true;
  double tx = json_number(json_at(t, 0), 0), ty = json_number(json_at(t, 1), 0), tz = json_number(json_at(t, 2), 0);
  double qx = json_number(json_at(r, 0), 0), qy = json_number(json_at(r, 1), 0), qz = json_number(json_at(r, 2), 0),
         qw = r ? json_number(json_at(r, 3), 1) : 1;
  double sx = s ? json_number(json_at(s, 0), 1) : 1, sy = s ? json_number(json_at(s, 1), 1) : 1, sz = s ? json_number(json_at(s, 2), 1) : 1;
  /* the rotation matrix of the unit quaternion (x, y, z, w), columns scaled */
  double R[9] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
                  2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
                  2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy) };
  double S[3] = { sx, sy, sz };
  for (int c = 0; c < 3; c++)
    for (int row = 0; row < 3; row++) out->m[c * 4 + row] = R[c * 3 + row] * S[c];
  out->m[12] = tx, out->m[13] = ty, out->m[14] = tz;
  out->identity = false;
  return true;
}

typedef struct {
  const GlbModel *m;
  Mesh           *out;
  const char     *err;
  int             tris;
  /* vertices already added for (node, accessor) */
  struct { int node, acc, base; } seen[64];
  int             nseen;
  long            visits; /* nodes walked so far, each time it is reached */
} Walk;

/* the vertices of POSITION accessor acc of node, added once; their first index, or -1 */
static int add_vertices(Walk *w, int node, int acc, const Xform *x) {
  for (int i = 0; i < w->nseen; i++)
    if (w->seen[i].node == node && w->seen[i].acc == acc) return w->seen[i].base;
  GlbAccessor a;
  if (!glb_accessor(w->m, acc, &a, &w->err)) return -1;
  if (a.comps != 3 || a.rows) {
    w->err = "a POSITION accessor is not VEC3";
    return -1;
  }
  if ((size_t)w->out->nv + a.count > 65535) {
    w->err = "more than 65535 vertices";
    return -1;
  }
  int base = w->out->nv;
  for (size_t i = 0; i < a.count; i++) {
    double p[3] = { glb_get(&a, i, 0), glb_get(&a, i, 1), glb_get(&a, i, 2) };
    if (x->identity) mesh_vert(w->out, (float)p[0], (float)p[1], (float)p[2]);
    else {
      const double *M = x->m;
      mesh_vert(w->out, (float)(M[0] * p[0] + M[4] * p[1] + M[8] * p[2] + M[12]),
                (float)(M[1] * p[0] + M[5] * p[1] + M[9] * p[2] + M[13]),
                (float)(M[2] * p[0] + M[6] * p[1] + M[10] * p[2] + M[14]));
    }
  }
  if (w->nseen < 64) {
    w->seen[w->nseen].node = node;
    w->seen[w->nseen].acc = acc;
    w->seen[w->nseen++].base = base;
  }
  return base;
}

static bool add_primitive(Walk *w, int node, const Json *prim, const Xform *x) {
  size_t mode = 4;
  const Json *mv = json_get(prim, "mode");
  if (mv && !whole(mv, 6, &mode)) {
    w->err = "a primitive's mode is not valid";
    return false;
  }
  if (mode < 4) return true; /* points and lines: nothing to fill */
  size_t pos;
  if (!whole(json_get(json_get(prim, "attributes"), "POSITION"), 1e9, &pos)) return true; /* no positions, nothing to draw */
  int base = add_vertices(w, node, (int)pos, x);
  if (base < 0) return false;
  int nv = w->out->nv - base; /* this accessor's vertices */
  for (int i = 0; i < w->nseen; i++)
    if (w->seen[i].node == node && w->seen[i].acc == (int)pos) {
      GlbAccessor a;
      glb_accessor(w->m, (int)pos, &a, NULL);
      nv = (int)a.count;
    }
  /* the corner list: from the indices, or 0..n-1 */
  GlbAccessor ia;
  size_t ni;
  const Json *iv = json_get(prim, "indices");
  if (iv) {
    size_t ii;
    if (!whole(iv, 1e9, &ii) || !glb_accessor(w->m, (int)ii, &ia, &w->err)) {
      if (!w->err) w->err = "a primitive's indices are not valid";
      return false;
    }
    if (ia.comps != 1 || (ia.ctype != GLB_U8 && ia.ctype != GLB_U16 && ia.ctype != GLB_U32) || ia.normalized) {
      w->err = "indices are not unsigned integer scalars";
      return false;
    }
    ni = ia.count;
  } else ni = (size_t)nv;
#define CORNER(k) (iv ? (long)glb_get(&ia, (k), 0) : (long)(k))
  for (size_t k = 0; k < ni; k++)
    if (CORNER(k) < 0 || CORNER(k) >= nv) {
      w->err = "an index is past the vertices";
      return false;
    }
  uint32_t col = 0xCCCCCC;
  size_t mi;
  if (json_get(prim, "material")) {
    if (!whole(json_get(prim, "material"), 1e9, &mi) || !json_at(json_get(w->m->doc, "materials"), (int)mi)) {
      w->err = "a primitive's material does not exist";
      return false;
    }
    col = material_colour(w->m, (int)mi);
  }
  /* our own writer's faces: fans of the given corner counts, read back as polygons */
  const Json *fl = json_get(json_get(prim, "extras"), "tl_faces");
  if (mode == 4 && fl) {
    size_t need = 0;
    bool fits = true;
    JSON_EACH(e, fl) {
      size_t c;
      if (!whole(e, FACE_MAX_V, &c) || c < 3) fits = false;
      else need += 3 * (c - 2);
    }
    if (fits && need == ni) {
      size_t k = 0;
      bool fans = true;
      JSON_EACH(e, fl) { /* check every fan first: shared first corner, each triangle on from the last */
        int c = (int)e->num;
        long a0 = CORNER(k);
        for (int t = 0; t < c - 2; t++) {
          if (CORNER(k + 3 * (size_t)t) != a0 || (t > 0 && CORNER(k + 3 * (size_t)t + 1) != CORNER(k + 3 * (size_t)t - 1)))
            fans = false;
        }
        k += 3 * (size_t)(c - 2);
      }
      if (fans) {
        k = 0;
        JSON_EACH(e, fl) {
          int c = (int)e->num, idx[FACE_MAX_V];
          idx[0] = base + (int)CORNER(k);
          idx[1] = base + (int)CORNER(k + 1);
          for (int t = 0; t < c - 2; t++) idx[t + 2] = base + (int)CORNER(k + 3 * (size_t)t + 2);
          mesh_face(w->out, idx, c, col);
          w->tris += c - 2;
          k += 3 * (size_t)(c - 2);
        }
        return true;
      }
    }
  }
  /* plain triangles: a list, a strip or a fan */
  size_t nt = mode == 4 ? ni / 3 : ni >= 3 ? ni - 2 : 0;
  for (size_t t = 0; t < nt; t++) {
    long a, b, c;
    if (mode == 4) a = CORNER(3 * t), b = CORNER(3 * t + 1), c = CORNER(3 * t + 2);
    else if (mode == 5) {
      a = CORNER(t), b = CORNER(t + 1), c = CORNER(t + 2);
      if (t & 1) { /* every other triangle of a strip turns the other way */
        long s = a;
        a = b;
        b = s;
      }
    } else a = CORNER(0), b = CORNER(t + 1), c = CORNER(t + 2);
    if (a == b || b == c || a == c) continue; /* degenerate */
    mesh_face3(w->out, base + (int)a, base + (int)b, base + (int)c, col);
    w->tris++;
  }
#undef CORNER
  return true;
}

static bool walk_node(Walk *w, int ni, const Xform *parent, int depth, unsigned char *visiting) {
  const Json *nodes = json_get(w->m->doc, "nodes"), *node = json_at(nodes, ni);
  if (!json_is(node, JSON_OBJECT)) {
    w->err = "a node does not exist";
    return false;
  }
  if (depth > 64 || visiting[ni]) {
    w->err = "the node tree is too deep or has a cycle";
    return false;
  }
  /* a node shared by several parents (glTF forbids it, but nothing stops a file) is walked once per path: 2 parents
   * a level are 2^depth walks, so a model gets a fixed number of them in all */
  if (++w->visits > 65536) {
    w->err = "the node tree reaches its nodes too many times (shared nodes)";
    return false;
  }
  visiting[ni] = 1;
  Xform local, world;
  if (!node_xform(node, &local)) {
    w->err = "a node's transform is not valid";
    return false;
  }
  world = xf_mul(parent, &local);
  size_t mesh;
  if (json_get(node, "mesh")) {
    const Json *mj = json_at(json_get(w->m->doc, "meshes"), whole(json_get(node, "mesh"), 1e9, &mesh) ? (int)mesh : -1);
    if (!json_is(mj, JSON_OBJECT)) {
      w->err = "a node's mesh does not exist";
      return false;
    }
    JSON_EACH(p, json_get(mj, "primitives")) if (!add_primitive(w, ni, p, &world)) return false;
    const Json *rad = json_get(json_get(mj, "extras"), "tl_rad");
    if (json_is_number(rad)) w->out->rad = (float)rad->num;
  }
  JSON_EACH(c, json_get(node, "children")) {
    size_t ci;
    if (!whole(c, (double)json_count(nodes) - 1, &ci)) {
      w->err = "a node's child does not exist";
      return false;
    }
    if (!walk_node(w, (int)ci, &world, depth + 1, visiting)) return false;
  }
  visiting[ni] = 0;
  return true;
}

bool glb_mesh(const GlbModel *m, Mesh *out, const char **err) {
  mesh_reset(out, 0);
  Walk w = { m, out, NULL, 0, { { 0 } }, 0, 0 };
  const Json *scenes = json_get(m->doc, "scenes");
  size_t si = 0;
  if (json_get(m->doc, "scene") && !whole(json_get(m->doc, "scene"), 1e9, &si)) return refuse(err, "the scene is not an index");
  const Json *scene = json_at(scenes, (int)si);
  int nn = json_count(json_get(m->doc, "nodes"));
  unsigned char *visiting = calloc((size_t)nn + 1, 1);
  if (!visiting) return refuse(err, "out of memory");
  bool ok = true;
  if (scene) {
    JSON_EACH(r, json_get(scene, "nodes")) {
      size_t ri;
      if (!whole(r, (double)nn - 1, &ri)) {
        w.err = "a scene's node does not exist";
        ok = false;
        break;
      }
      Xform id = xf_identity();
      if (!(ok = walk_node(&w, (int)ri, &id, 0, visiting))) break;
    }
  } else if (scenes) {
    w.err = "the scene does not exist";
    ok = false;
  }
  free(visiting);
  if (!ok) return refuse(err, w.err ? w.err : "the model could not be read");
  if (!out->nf) return refuse(err, "no triangles to draw");
  if (out->rad <= 0) { /* no radius stored: the farthest vertex from the origin */
    float r2 = 0;
    for (int i = 0; i < out->nv; i++) r2 = fmaxf(r2, v3_dot(out->v[i], out->v[i]));
    out->rad = sqrtf(r2);
  }
  if (err) *err = NULL;
  return true;
}

/* ---- writing a mesh -------------------------------------------------------- */

uint8_t *glb_write_mesh(const Mesh *m, const char *name, size_t *len) {
  *len = 0;
  size_t ncorner = 0;
  for (int f = 0; f < m->nf; f++) ncorner += 3 * (size_t)(m->f[f].n - 2);
  size_t pos_bytes = (size_t)m->nv * 12, idx_bytes = ncorner * 2, idx_padded = (idx_bytes + 3) & ~(size_t)3;
  size_t bin_len = pos_bytes + idx_padded;
  /* the distinct colours, in order of first use */
  uint32_t *cols = malloc(((size_t)m->nf + 1) * sizeof *cols);
  uint8_t *bin = calloc(bin_len ? bin_len : 4, 1);
  if (!cols || !bin) {
    free(cols);
    free(bin);
    return NULL;
  }
  int ncols = 0;
  for (int f = 0; f < m->nf; f++) {
    int k = 0;
    while (k < ncols && cols[k] != m->f[f].c) k++;
    if (k == ncols) cols[ncols++] = m->f[f].c;
  }
  float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
  for (int i = 0; i < m->nv; i++) {
    float p[3] = { m->v[i].x, m->v[i].y, m->v[i].z };
    memcpy(bin + 12 * (size_t)i, p, 12);
    for (int c = 0; c < 3; c++) {
      if (i == 0 || p[c] < lo[c]) lo[c] = p[c];
      if (i == 0 || p[c] > hi[c]) hi[c] = p[c];
    }
  }
  size_t at = pos_bytes;
  for (int f = 0; f < m->nf; f++)
    for (int t = 0; t < m->f[f].n - 2; t++) {
      uint16_t tri[3] = { m->f[f].i[0], m->f[f].i[t + 1], m->f[f].i[t + 2] };
      memcpy(bin + at, tri, 6);
      at += 6;
    }

  JsonWriter w = { 0 };
  json_w_open_object(&w, NULL);
  json_w_open_object(&w, "asset");
  json_w_string(&w, "version", "2.0");
  json_w_string(&w, "generator", "Tailon Lock");
  json_w_close(&w);
  json_w_number(&w, "scene", 0);
  json_w_open_array(&w, "scenes");
  json_w_open_object(&w, NULL);
  json_w_open_array(&w, "nodes");
  json_w_number(&w, NULL, 0);
  json_w_close(&w);
  json_w_close(&w);
  json_w_close(&w);
  json_w_open_array(&w, "nodes");
  json_w_open_object(&w, NULL);
  json_w_string(&w, "name", name);
  json_w_number(&w, "mesh", 0);
  json_w_close(&w);
  json_w_close(&w);
  json_w_open_array(&w, "buffers");
  json_w_open_object(&w, NULL);
  json_w_number(&w, "byteLength", (double)bin_len);
  json_w_close(&w);
  json_w_close(&w);
  json_w_open_array(&w, "bufferViews");
  json_w_open_object(&w, NULL);
  json_w_number(&w, "buffer", 0);
  json_w_number(&w, "byteLength", (double)(pos_bytes ? pos_bytes : 4));
  json_w_number(&w, "target", 34962); /* vertex attributes */
  json_w_close(&w);
  json_w_open_object(&w, NULL);
  json_w_number(&w, "buffer", 0);
  json_w_number(&w, "byteOffset", (double)pos_bytes);
  json_w_number(&w, "byteLength", (double)(idx_bytes ? idx_bytes : 2));
  json_w_number(&w, "target", 34963); /* indices */
  json_w_close(&w);
  json_w_close(&w);
  /* accessor 0: the vertices; then one index accessor for each run of faces of one colour */
  json_w_open_array(&w, "accessors");
  json_w_open_object(&w, NULL);
  json_w_number(&w, "bufferView", 0);
  json_w_number(&w, "componentType", GLB_F32);
  json_w_number(&w, "count", m->nv);
  json_w_string(&w, "type", "VEC3");
  json_w_open_array(&w, "min");
  for (int c = 0; c < 3; c++) json_w_number(&w, NULL, lo[c]);
  json_w_close(&w);
  json_w_open_array(&w, "max");
  for (int c = 0; c < 3; c++) json_w_number(&w, NULL, hi[c]);
  json_w_close(&w);
  json_w_close(&w);
  size_t off = 0;
  for (int f = 0; f < m->nf;) {
    int g = f;
    size_t n = 0;
    while (g < m->nf && m->f[g].c == m->f[f].c) n += 3 * (size_t)(m->f[g++].n - 2);
    json_w_open_object(&w, NULL);
    json_w_number(&w, "bufferView", 1);
    json_w_number(&w, "byteOffset", (double)(off * 2));
    json_w_number(&w, "componentType", GLB_U16);
    json_w_number(&w, "count", (double)n);
    json_w_string(&w, "type", "SCALAR");
    json_w_close(&w);
    off += n;
    f = g;
  }
  json_w_close(&w);
  json_w_open_array(&w, "materials");
  for (int k = 0; k < ncols; k++) {
    char hex[16];
    uint32_t c = cols[k];
    snprintf(hex, sizeof hex, "#%06x", c & 0xFFFFFF);
    json_w_open_object(&w, NULL);
    json_w_string(&w, "name", hex);
    json_w_open_object(&w, "pbrMetallicRoughness");
    json_w_open_array(&w, "baseColorFactor");
    for (int s = 16; s >= 0; s -= 8) json_w_number(&w, NULL, to_linear(((c >> s) & 255) / 255.0));
    json_w_number(&w, NULL, 1);
    json_w_close(&w);
    json_w_number(&w, "metallicFactor", 0);
    json_w_number(&w, "roughnessFactor", 1);
    json_w_close(&w);
    if (c & FACE_GLOW) {
      json_w_open_array(&w, "emissiveFactor");
      for (int s = 16; s >= 0; s -= 8) json_w_number(&w, NULL, to_linear(((c >> s) & 255) / 255.0));
      json_w_close(&w);
    }
    json_w_open_object(&w, "extras");
    json_w_number(&w, "tl_color", c);
    json_w_close(&w);
    json_w_close(&w);
  }
  json_w_close(&w);
  json_w_open_array(&w, "meshes");
  json_w_open_object(&w, NULL);
  json_w_string(&w, "name", name);
  json_w_open_array(&w, "primitives");
  int acc = 1;
  for (int f = 0; f < m->nf;) {
    int g = f;
    while (g < m->nf && m->f[g].c == m->f[f].c) g++;
    int k = 0;
    while (cols[k] != m->f[f].c) k++;
    json_w_open_object(&w, NULL);
    json_w_open_object(&w, "attributes");
    json_w_number(&w, "POSITION", 0);
    json_w_close(&w);
    json_w_number(&w, "indices", acc++);
    json_w_number(&w, "material", k);
    json_w_open_object(&w, "extras");
    json_w_open_array(&w, "tl_faces");
    for (int h = f; h < g; h++) json_w_number(&w, NULL, m->f[h].n);
    json_w_close(&w);
    json_w_close(&w);
    json_w_close(&w);
    f = g;
  }
  json_w_close(&w);
  json_w_open_object(&w, "extras");
  json_w_number(&w, "tl_rad", m->rad);
  json_w_close(&w);
  json_w_close(&w);
  json_w_close(&w);
  json_w_close(&w);
  free(cols);
  char *json = json_w_finish(&w);
  if (!json) {
    free(bin);
    return NULL;
  }
  size_t jl = strlen(json), jpad = (jl + 3) & ~(size_t)3, total = 12 + 8 + jpad + 8 + bin_len;
  uint8_t *out = malloc(total);
  if (out) {
    uint8_t *p = out;
#define PUT32(v)                                                     \
  do {                                                               \
    uint32_t v_ = (uint32_t)(v);                                     \
    p[0] = (uint8_t)v_, p[1] = (uint8_t)(v_ >> 8), p[2] = (uint8_t)(v_ >> 16), p[3] = (uint8_t)(v_ >> 24); \
    p += 4;                                                          \
  } while (0)
    PUT32(GLB_MAGIC);
    PUT32(2);
    PUT32(total);
    PUT32(jpad);
    PUT32(GLB_CHUNK_JSON);
    memcpy(p, json, jl);
    memset(p + jl, ' ', jpad - jl);
    p += jpad;
    PUT32(bin_len);
    PUT32(GLB_CHUNK_BIN);
    memcpy(p, bin, bin_len);
#undef PUT32
    *len = total;
  }
  free(json);
  free(bin);
  return out;
}
