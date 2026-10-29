/* test_glb.c - checks for our own GLB reader (src/core/glb.c): the container
 * rules of the glTF 2.0 binary format, buffer views and accessors (every
 * component type, normalized integers, matrices, strides), meshes, materials
 * and nodes (our own writer's files read back exactly; transforms, strips and
 * fans, colours from plain materials), and files it must refuse. Original
 * code for this project, written from the Khronos glTF 2.0 specification. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/glb.h"
#include "core/json.h"
#include "core/mesh.h"

static int failures, checks;

#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) {                                                                    \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);        \
      failures++;                                                                     \
    }                                                                                 \
  } while (0)

/* ---- building test files --------------------------------------------------- */

typedef struct {
  uint8_t b[16384];
  size_t  n;
} Buf;

static void put32(Buf *f, uint32_t v) {
  for (int i = 0; i < 4; i++) f->b[f->n++] = (uint8_t)(v >> (8 * i));
}

static void put_chunk(Buf *f, uint32_t type, const void *data, size_t len, uint8_t pad) {
  size_t padded = (len + 3) & ~(size_t)3;
  put32(f, (uint32_t)padded);
  put32(f, type);
  memcpy(f->b + f->n, data, len);
  memset(f->b + f->n + len, pad, padded - len);
  f->n += padded;
}

/* a GLB with a JSON chunk and, if bin, a BIN chunk; the header's length filled in */
static Buf make(const char *json, const void *bin, size_t bin_len) {
  Buf f = { { 0 }, 0 };
  put32(&f, GLB_MAGIC);
  put32(&f, 2);
  put32(&f, 0);
  put_chunk(&f, GLB_CHUNK_JSON, json, strlen(json), ' ');
  if (bin) put_chunk(&f, GLB_CHUNK_BIN, bin, bin_len, 0);
  f.b[8] = (uint8_t)f.n;
  f.b[9] = (uint8_t)(f.n >> 8);
  return f;
}

static void set32(Buf *f, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) f->b[at + i] = (uint8_t)(v >> (8 * i));
}

/* glb_open on exactly these bytes (a copy of their size: ASan sees a read past them) */
static int opens(const Buf *f, GlbFile *out, const char **err) {
  uint8_t *copy = malloc(f->n ? f->n : 1);
  memcpy(copy, f->b, f->n);
  GlbFile tmp;
  int ok = glb_open(copy, f->n, out ? out : &tmp, err);
  free(copy);
  return ok;
}

static void test_container(void) {
  const char *json = "{\"asset\":{\"version\":\"2.0\"}}";
  uint8_t bin[6] = { 1, 2, 3, 4, 5, 6 };
  Buf f = make(json, bin, sizeof bin);
  GlbFile g;
  const char *err;
  uint8_t *copy = malloc(f.n);
  memcpy(copy, f.b, f.n);
  CHECK(glb_open(copy, f.n, &g, &err) && err == NULL);
  CHECK(g.json_len == 28 && memcmp(g.json, json, strlen(json)) == 0 && g.json[27] == ' ');
  CHECK(g.bin && g.bin_len == 8 && g.bin[5] == 6 && g.bin[6] == 0);
  free(copy);
  /* no BIN chunk */
  f = make(json, NULL, 0);
  CHECK(opens(&f, &g, NULL) && g.bin == NULL && g.bin_len == 0);
  /* an unknown chunk after the BIN chunk is skipped */
  f = make(json, bin, sizeof bin);
  put_chunk(&f, 0x41424344u, "xyz", 3, 0);
  set32(&f, 8, (uint32_t)f.n);
  CHECK(opens(&f, &g, NULL) && g.bin_len == 8);
  /* bytes after the header's length are not part of the file */
  f = make(json, bin, sizeof bin);
  size_t real = f.n;
  memset(f.b + f.n, 0xAB, 8);
  f.n += 8;
  CHECK(opens(&f, &g, NULL) && (size_t)((const uint8_t *)g.bin + g.bin_len - (const uint8_t *)g.json) + 20 == real);
}

static void test_refused(void) {
  const char *json = "{\"asset\":{\"version\":\"2.0\"}}";
  uint8_t bin[8] = { 0 };
  int refused = 0, n = 0;
  const char *err;
#define REFUSE(build)                                     \
  do {                                                    \
    Buf f = make(json, bin, sizeof bin);                  \
    build;                                                \
    n++;                                                  \
    if (!opens(&f, NULL, &err) && err) refused++;         \
    else fprintf(stderr, "  accepted: %s\n", #build);     \
  } while (0)
  REFUSE(f.b[0] = 'G');                          /* the magic */
  REFUSE(set32(&f, 4, 1));                       /* version 1 */
  REFUSE(set32(&f, 4, 3));                       /* version 3 */
  REFUSE(set32(&f, 8, (uint32_t)f.n + 4));       /* longer than the file */
  REFUSE(set32(&f, 8, (uint32_t)f.n - 2));       /* not a multiple of 4 */
  REFUSE(set32(&f, 8, 16));                      /* too short for a chunk */
  REFUSE(set32(&f, 12, 30));                     /* the JSON chunk not a multiple of 4 */
  REFUSE(set32(&f, 12, 4000));                   /* the JSON chunk past the end */
  REFUSE(set32(&f, 12, 0));                      /* the JSON chunk empty */
  REFUSE(set32(&f, 16, GLB_CHUNK_BIN));          /* BIN first */
  REFUSE(set32(&f, 16, 0x12345678u));            /* an unknown chunk first */
  REFUSE(set32(&f, 12 + 8 + 28 + 4, GLB_CHUNK_JSON)); /* a second JSON chunk */
  REFUSE(set32(&f, 12 + 8 + 28, 12));            /* the BIN chunk past the end */
  REFUSE(f.n = 11);                              /* no room for the header */
  REFUSE(f.n = 0);
  /* a BIN chunk after an unknown one is out of place */
  {
    Buf f = make(json, NULL, 0);
    put_chunk(&f, 0x41424344u, "abcd", 4, 0);
    put_chunk(&f, GLB_CHUNK_BIN, bin, 8, 0);
    set32(&f, 8, (uint32_t)f.n);
    n++;
    if (!opens(&f, NULL, &err)) refused++;
  }
  CHECK(refused == n);
  /* the file cut short at every length is refused (or read only up to its end) */
  Buf whole = make(json, bin, sizeof bin);
  int cut_refused = 0;
  for (size_t cut = 0; cut < whole.n; cut++) {
    Buf f = whole;
    f.n = cut;
    if (!opens(&f, NULL, NULL)) cut_refused++;
  }
  CHECK(cut_refused == (int)whole.n);
  CHECK(glb_open(NULL, 100, &(GlbFile){ 0 }, &err) == false && err);
  printf("glb files refused: %d of %d, cut short: %d of %zu\n", refused, n, cut_refused, whole.n);
}

/* ---- buffer views and accessors ------------------------------------------ */

/* the BIN chunk of the accessor tests */
static uint8_t BIN[96];

static void fill_bin(void) {
  memset(BIN, 0, sizeof BIN);
  float f[6] = { 1.5f, -2.25f, 3.0f, 4.0f, 5.0f, -6.5f }; /* 0-23: two VEC3 floats */
  memcpy(BIN, f, sizeof f);
  uint16_t u16[4] = { 0, 1, 65535, 32768 };                 /* 24-31 */
  memcpy(BIN + 24, u16, sizeof u16);
  BIN[32] = 0;                                               /* 32-35: u8 */
  BIN[33] = 255;
  BIN[34] = 128;
  BIN[35] = 7;
  int8_t i8[4] = { -128, -127, 0, 127 };                     /* 36-39: i8 */
  memcpy(BIN + 36, i8, sizeof i8);
  uint32_t u32[2] = { 4000000000u, 7 };                      /* 40-47 */
  memcpy(BIN + 40, u32, sizeof u32);
  /* 48-55: a MAT2 of u8: columns (1,2) and (3,4), each padded to 4 bytes */
  BIN[48] = 1, BIN[49] = 2, BIN[52] = 3, BIN[53] = 4;
  /* 56-95: VEC2 floats with a 20-byte stride */
  for (int i = 0; i < 2; i++) {
    float v[2] = { 10.0f + i, 20.0f + i };
    memcpy(BIN + 56 + 20 * i, v, sizeof v);
  }
}

/* a model: buffer 0 of n bytes over BIN, the views and accessors given as JSON arrays */
static int load(const char *views, const char *accessors, size_t buf_len, GlbModel *m, Buf *f, const char **err) {
  char json[4096];
  snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":%s,\"accessors\":%s}",
           buf_len, views, accessors);
  *f = make(json, BIN, sizeof BIN);
  return glb_load(f->b, f->n, m, err);
}

static const char *VIEWS = "[{\"buffer\":0,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":24,\"byteLength\":8},"
                           "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":4},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":4},"
                           "{\"buffer\":0,\"byteOffset\":40,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":8},"
                           "{\"buffer\":0,\"byteOffset\":56,\"byteLength\":28,\"byteStride\":20}]";

static void test_accessors(void) {
  fill_bin();
  const char *acc = "[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
                    "{\"bufferView\":1,\"componentType\":5123,\"count\":4,\"type\":\"SCALAR\"},"
                    "{\"bufferView\":1,\"componentType\":5123,\"count\":4,\"type\":\"SCALAR\",\"normalized\":true},"
                    "{\"bufferView\":2,\"componentType\":5121,\"count\":4,\"type\":\"SCALAR\",\"normalized\":true},"
                    "{\"bufferView\":3,\"componentType\":5120,\"count\":4,\"type\":\"SCALAR\",\"normalized\":true},"
                    "{\"bufferView\":4,\"componentType\":5125,\"count\":2,\"type\":\"SCALAR\"},"
                    "{\"bufferView\":5,\"componentType\":5121,\"count\":1,\"type\":\"MAT2\"},"
                    "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC2\"},"
                    "{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                    "{\"bufferView\":3,\"componentType\":5120,\"count\":4,\"type\":\"SCALAR\"}]";
  GlbModel m;
  Buf f;
  const char *err;
  CHECK(load(VIEWS, acc, sizeof BIN, &m, &f, &err));
  GlbAccessor a;
  CHECK(glb_accessor(&m, 0, &a, &err) && a.count == 2 && a.comps == 3 && a.stride == 12);
  CHECK(glb_get(&a, 0, 0) == 1.5 && glb_get(&a, 0, 1) == -2.25 && glb_get(&a, 1, 2) == -6.5);
  CHECK(glb_get(&a, 2, 0) == 0 && glb_get(&a, 0, 3) == 0); /* past the end: 0, never a read */
  CHECK(glb_accessor(&m, 1, &a, NULL) && glb_get(&a, 2, 0) == 65535 && glb_get(&a, 3, 0) == 32768);
  CHECK(glb_accessor(&m, 2, &a, NULL) && glb_get(&a, 2, 0) == 1.0 && glb_get(&a, 1, 0) == 1 / 65535.0);
  CHECK(glb_accessor(&m, 3, &a, NULL) && glb_get(&a, 1, 0) == 1.0 && glb_get(&a, 2, 0) == 128 / 255.0);
  /* signed normalized: -128 and -127 both read as -1 */
  CHECK(glb_accessor(&m, 4, &a, NULL) && glb_get(&a, 0, 0) == -1.0 && glb_get(&a, 1, 0) == -1.0 && glb_get(&a, 3, 0) == 1.0);
  CHECK(glb_accessor(&m, 5, &a, NULL) && glb_get(&a, 0, 0) == 4000000000.0);
  /* a MAT2 of bytes: column-major, each column padded to 4 bytes */
  CHECK(glb_accessor(&m, 6, &a, NULL) && a.elem_size == 8 && glb_get(&a, 0, 0) == 1 && glb_get(&a, 0, 1) == 2 &&
        glb_get(&a, 0, 2) == 3 && glb_get(&a, 0, 3) == 4);
  /* a stride wider than the element */
  CHECK(glb_accessor(&m, 7, &a, NULL) && a.stride == 20 && glb_get(&a, 1, 0) == 11 && glb_get(&a, 1, 1) == 21);
  CHECK(glb_accessor(&m, 8, &a, NULL) && glb_get(&a, 0, 0) == 4.0);
  CHECK(glb_accessor(&m, 9, &a, NULL) && glb_get(&a, 0, 0) == -128 && glb_get(&a, 3, 0) == 127);
  CHECK(!glb_accessor(&m, 10, &a, &err) && err);
  glb_close(&m);
}

static void test_accessors_refused(void) {
  fill_bin();
  static const char *const BAD[] = {
    "{\"bufferView\":0,\"componentType\":5124,\"count\":1,\"type\":\"SCALAR\"}",            /* not a component type */
    "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC5\"}",              /* not a type */
    "{\"bufferView\":0,\"componentType\":5126,\"count\":0,\"type\":\"SCALAR\"}",            /* no elements */
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}",              /* one past the view */
    "{\"bufferView\":0,\"byteOffset\":2,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}", /* misaligned */
    "{\"bufferView\":0,\"byteOffset\":24,\"componentType\":5121,\"count\":1,\"type\":\"SCALAR\"}", /* offset at the end */
    "{\"bufferView\":9,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",            /* no such view */
    "{\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",                             /* no view */
    "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\",\"sparse\":{}}",
    "{\"bufferView\":0,\"componentType\":5126,\"count\":1.5,\"type\":\"SCALAR\"}",          /* not whole */
    "{\"bufferView\":-1,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",
    "{\"bufferView\":0,\"componentType\":5126,\"count\":4294967295,\"type\":\"SCALAR\"}",   /* a huge count */
    "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\",\"normalized\":true}",
    "{\"bufferView\":0,\"componentType\":5121,\"count\":1,\"type\":\"SCALAR\",\"normalized\":1}",
    "{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"MAT3\"}",              /* 72 bytes in 24 */
    "{\"bufferView\":0,\"componentType\":\"5126\",\"count\":1,\"type\":\"SCALAR\"}",
    "{\"bufferView\":1,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",            /* the view is past its buffer */
    "{\"bufferView\":2,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}",              /* a stride below the element */
    "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",            /* a stride not a multiple of 4 */
    "{\"bufferView\":4,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}",            /* buffer 1 */
  };
  const char *views = "[{\"buffer\":0,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":90,\"byteLength\":8},"
                      "{\"buffer\":0,\"byteLength\":24,\"byteStride\":4},{\"buffer\":0,\"byteLength\":24,\"byteStride\":6},"
                      "{\"buffer\":1,\"byteLength\":4}]";
  int refused = 0, n = (int)(sizeof BAD / sizeof *BAD);
  for (int i = 0; i < n; i++) {
    char acc[512];
    snprintf(acc, sizeof acc, "[%s,{\"bufferView\":2,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}]", BAD[i]);
    GlbModel m;
    Buf f;
    const char *err = NULL;
    GlbAccessor a;
    if (!load(views, acc, sizeof BIN, &m, &f, &err)) {
      fprintf(stderr, "  the test file did not load: %s\n", err);
      failures++;
      continue;
    }
    if (!glb_accessor(&m, 0, &a, &err) && err) refused++;
    else fprintf(stderr, "  accessor accepted: %s\n", BAD[i]);
    glb_close(&m);
  }
  CHECK(refused == n);
  /* the buffer itself: longer than the BIN chunk, or with a uri */
  GlbModel m;
  Buf f;
  const char *err;
  GlbAccessor a;
  CHECK(load(VIEWS, "[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}]", sizeof BIN + 4, &m, &f, &err));
  CHECK(!glb_accessor(&m, 0, &a, &err) && err && strstr(err, "longer"));
  glb_close(&m);
  f = make("{\"buffers\":[{\"byteLength\":96,\"uri\":\"x.bin\"}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":4}],"
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}]}", BIN, sizeof BIN);
  CHECK(glb_load(f.b, f.n, &m, &err) && !glb_accessor(&m, 0, &a, &err) && strstr(err, "uri"));
  glb_close(&m);
  /* a JSON chunk that is not JSON, or not an object */
  f = make("{\"asset\":", NULL, 0);
  CHECK(!glb_load(f.b, f.n, &m, &err) && err);
  f = make("[1,2]", NULL, 0);
  CHECK(!glb_load(f.b, f.n, &m, &err) && err && m.doc == NULL);
  printf("accessors refused: %d of %d\n", refused, n);
}

/* ---- meshes, materials and nodes ------------------------------------------ */

/* a mesh's canonical dump, as a string (free it) */
static char *dump_of(const Mesh *m) {
  FILE *f = tmpfile();
  if (!f) return NULL;
  mesh_dump(m, f);
  long n = ftell(f);
  rewind(f);
  char *s = malloc((size_t)n + 1);
  if (s) {
    s[fread(s, 1, (size_t)n, f)] = 0;
  }
  fclose(f);
  return s;
}

static int round_trip(const Mesh *src, const char *name) {
  size_t n;
  uint8_t *b = glb_write_mesh(src, name, &n);
  GlbModel m;
  const char *err = NULL;
  static Mesh back;
  int ok = b && glb_load(b, n, &m, &err) && glb_mesh(&m, &back, &err);
  if (ok) {
    char *a = dump_of(src), *c = dump_of(&back);
    ok = a && c && !strcmp(a, c);
    free(a);
    free(c);
    glb_close(&m);
  }
  if (!ok) fprintf(stderr, "  %s: round trip failed (%s)\n", name, err ? err : "the dumps differ");
  free(b);
  return ok;
}

static void test_mesh_round_trip(void) {
  mesh_init_builtin();
  CHECK(round_trip(&MESH_JET[0], "jet0"));
  CHECK(round_trip(&MESH_JET[1], "jet1"));
  CHECK(round_trip(&MESH_DRONE, "drone"));
  CHECK(round_trip(&MESH_TANKER, "tanker"));
  CHECK(round_trip(&MESH_CIWS_BASE, "ciws"));
  CHECK(round_trip(&MESH_SAM_BOX, "sam"));
  CHECK(round_trip(&MESH_BRIDGE, "bridge"));
  CHECK(round_trip(&MESH_CREW, "crew"));
  for (int t = 0; t < TGT_TYPES; t++) CHECK(round_trip(&MESH_TARGET[0][t], "target"));
  /* a glowing face, an octagon, a -0 vertex and one face of each corner count */
  static Mesh m;
  mesh_reset(&m, 12.5f);
  for (int i = 0; i < 8; i++) mesh_vert(&m, (float)i, i == 0 ? -0.0f : (float)(i * i) / 7.0f, -(float)i);
  int oct[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
  mesh_face(&m, oct, 8, 0x123456 | FACE_GLOW);
  mesh_face3(&m, 0, 1, 2, 0x123456);
  mesh_face4(&m, 3, 4, 5, 6, 0xabcdef);
  mesh_face3(&m, 7, 6, 5, 0x123456);
  CHECK(round_trip(&m, "edges"));
}

/* a model from JSON with positions at BIN 0 (4 VEC3 floats) and u16 indices at 48 */
static Buf model(const char *rest) {
  static uint8_t bin[64];
  float p[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
  uint16_t idx[6] = { 0, 1, 2, 3, 0, 2 };
  memcpy(bin, p, sizeof p);
  memcpy(bin + 48, idx, sizeof idx);
  char json[4096];
  snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":64}],"
           "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}],"
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
           "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
           "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
           "{\"bufferView\":1,\"byteOffset\":2,\"componentType\":5123,\"count\":2,\"type\":\"SCALAR\"}],%s}", rest);
  return make(json, bin, sizeof bin);
}

static int mesh_of(const char *rest, Mesh *out, const char **err) {
  Buf f = model(rest);
  GlbModel m;
  int ok = glb_load(f.b, f.n, &m, err) && glb_mesh(&m, out, err);
  if (m.doc) glb_close(&m);
  return ok;
}

static void test_mesh_reading(void) {
  static Mesh m;
  const char *err;
  const char *tri = "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1%s}]}]";
  char rest[1024], prim[256];
  /* two triangles, no material: grey; the radius from the farthest vertex */
  snprintf(prim, sizeof prim, tri, "");
  snprintf(rest, sizeof rest, "%s,\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && m.nv == 4 && m.nf == 2 && m.f[0].n == 3 && m.f[1].i[0] == 3 && m.f[0].c == 0xCCCCCC);
  CHECK(fabsf(m.rad - sqrtf(2)) < 1e-6f);
  /* a material without our extras: its base colour (linear) as sRGB; emission makes the face glow */
  snprintf(prim, sizeof prim, tri, ",\"material\":0");
  snprintf(rest, sizeof rest, "%s,\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.2140411,0,1]},"
           "\"emissiveFactor\":[0,0,0.5]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && m.f[0].c == (0xFF7F00 | FACE_GLOW));
  /* a translated child under a scaled parent */
  snprintf(prim, sizeof prim, tri, "");
  snprintf(rest, sizeof rest, "%s,\"nodes\":[{\"scale\":[2,2,2],\"children\":[1]},{\"mesh\":0,\"translation\":[10,0,0]}],"
           "\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && m.v[1].x == 22 && m.v[2].y == 2 && m.v[0].x == 20);
  /* a rotation of 90 degrees about Y: x goes to -z */
  snprintf(rest, sizeof rest, "%s,\"nodes\":[{\"mesh\":0,\"rotation\":[0,0.70710678,0,0.70710678]}],\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && fabsf(m.v[1].z + 1) < 1e-6f && fabsf(m.v[1].x) < 1e-6f);
  /* a matrix (column-major): translation in the last column */
  snprintf(rest, sizeof rest, "%s,\"nodes\":[{\"mesh\":0,\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,5,6,7,1]}],\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && m.v[0].x == 5 && m.v[0].y == 6 && m.v[0].z == 7);
  /* a strip and a fan over the 4 vertices: 2 triangles each, the strip's second turned */
  snprintf(rest, sizeof rest, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":5},"
           "{\"attributes\":{\"POSITION\":0},\"mode\":6},{\"attributes\":{\"POSITION\":0},\"mode\":1}]}],"
           "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]");
  CHECK(mesh_of(rest, &m, &err) && m.nv == 4 && m.nf == 4);
  CHECK(m.nf == 4 && m.f[1].i[0] == 2 && m.f[1].i[1] == 1 && m.f[1].i[2] == 3 && m.f[3].i[0] == 0 && m.f[3].i[2] == 3);
  /* our extras: one quad from the two fan triangles */
  snprintf(prim, sizeof prim, tri, ",\"extras\":{\"tl_faces\":[4]}");
  snprintf(rest, sizeof rest, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":2,\"extras\":{\"tl_faces\":[3]}}],"
           "\"extras\":{\"tl_rad\":9.5}}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]");
  CHECK(mesh_of(rest, &m, &err) && m.nf == 1 && m.rad == 9.5f);
  /* extras that do not fit the triangles are ignored: plain triangles */
  snprintf(prim, sizeof prim, tri, ",\"extras\":{\"tl_faces\":[5]}");
  snprintf(rest, sizeof rest, "%s,\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]", prim);
  CHECK(mesh_of(rest, &m, &err) && m.nf == 2 && m.f[0].n == 3);

  /* refused */
  static const char *const BAD[] = {
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":1}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],\"nodes\":[{\"mesh\":0,\"children\":[0]}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],\"nodes\":[{\"mesh\":3}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[4]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":1}}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":0}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":2}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],\"nodes\":[{\"mesh\":0,\"matrix\":[1,0,0]}],\"scenes\":[{\"nodes\":[0]}]",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":3",
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":9}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]",
  };
  int refused = 0, n = (int)(sizeof BAD / sizeof *BAD);
  for (int i = 0; i < n; i++) {
    err = NULL;
    if (!mesh_of(BAD[i], &m, &err) && err) refused++;
    else fprintf(stderr, "  mesh accepted: %s\n", BAD[i]);
  }
  CHECK(refused == n);
  printf("meshes refused: %d of %d\n", refused, n);
}

int main(void) {
  test_container();
  test_refused();
  test_accessors();
  test_accessors_refused();
  test_mesh_round_trip();
  test_mesh_reading();
  if (failures) {
    fprintf(stderr, "%d of %d glb checks failed\n", failures, checks);
    return 1;
  }
  printf("glb tests passed (%d checks)\n", checks);
  return 0;
}
