/* fuzz_glb.c - a mutation fuzzer for our own GLB reader (src/core/glb.c).
 * Original code for this project. Run it under ASan/UBSan.
 *
 * usage: fuzz_glb SECONDS [SEED.glb...]
 *
 * Starts from a few built-in GLB files (and any given), and mutates them:
 * header and chunk fields set to edge values, bytes flipped, cut short,
 * grown. Every input lives in a buffer of exactly its own size, so a read past
 * the end is a sanitizer error. For each accepted input it checks that the
 * chunks lie inside the header's length and inside the buffer, that the JSON
 * chunk is first and non-empty, and that a refusal says why. It then loads
 * the model and reads every element of every accessor it accepts; the numbers
 * in the JSON chunk are mutated too (digits changed in place, so the chunk
 * keeps its length), and builds the mesh (brick 7): every face it makes must
 * point at vertices that exist. Exits 1 on a broken rule (the input is saved to
 * fuzz-fail.glb). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/glb.h"
#include "core/json.h"
#include "core/mesh.h"
#include "core/safe.h"

static uint64_t rng = 0x243F6A8885A308D3ull;
static uint64_t next(void) {
  rng ^= rng >> 12;
  rng ^= rng << 25;
  rng ^= rng >> 27;
  return rng * 0x2545F4914F6CDD1Dull;
}
static size_t below(size_t n) { return n ? (size_t)(next() % n) : 0; }

static void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* a GLB: header, JSON chunk, optional BIN chunk */
static size_t build(uint8_t *b, const char *json, size_t bin_len) {
  size_t jl = (strlen(json) + 3) & ~(size_t)3, n = 12;
  put32(b, GLB_MAGIC);
  put32(b + 4, 2);
  put32(b + n, (uint32_t)jl);
  put32(b + n + 4, GLB_CHUNK_JSON);
  memset(b + n + 8, ' ', jl);
  memcpy(b + n + 8, json, strlen(json));
  n += 8 + jl;
  if (bin_len) {
    put32(b + n, (uint32_t)bin_len);
    put32(b + n + 4, GLB_CHUNK_BIN);
    for (size_t i = 0; i < bin_len; i++) b[n + 8 + i] = (uint8_t)i;
    n += 8 + bin_len;
  }
  put32(b + 8, (uint32_t)n);
  return n;
}

static long accepted, accessors_read, elements_read, meshes_built;

static int check(const uint8_t *src, size_t n) {
  uint8_t *data = malloc(n ? n : 1);
  memcpy(data, src, n);
  GlbFile g;
  const char *err;
  int ok = 1;
  if (glb_open(data, n, &g, &err)) {
    accepted++;
    uint32_t total = n >= 12 ? (uint32_t)(data[8] | data[9] << 8 | data[10] << 16 | (uint32_t)data[11] << 24) : 0;
    const uint8_t *j = (const uint8_t *)g.json;
    if (err || !g.json || g.json_len == 0 || j != data + 20 || g.json_len > total - 20 || total > n) ok = 0;
    if (g.bin && (g.bin < data + 20 || g.bin + g.bin_len > data + total)) ok = 0;
    GlbModel m;
    if (ok && glb_load(data, n, &m, &err)) {
      int na = json_count(json_get(m.doc, "accessors"));
      for (int i = 0; i < na && i < 64; i++) {
        GlbAccessor a;
        if (!glb_accessor(&m, i, &a, &err)) {
          if (!err) ok = 0;
          continue;
        }
        accessors_read++;
        /* the accessor lies inside the BIN chunk */
        if (a.data < g.bin || a.data + (a.count - 1) * a.stride + a.elem_size > g.bin + g.bin_len) ok = 0;
        double sum = 0;
        for (size_t e = 0; e < a.count; e++)
          for (int c = 0; c < a.comps; c++) sum += glb_get(&a, e, c);
        elements_read += (long)a.count;
        (void)sum;
      }
      static Mesh mesh;
      if (glb_mesh(&m, &mesh, &err)) {
        meshes_built++;
        if (mesh.nf < 1 || mesh.nv > 65535) ok = 0;
        for (int f = 0; f < mesh.nf && ok; f++) {
          if (mesh.f[f].n < 3 || mesh.f[f].n > FACE_MAX_V) ok = 0;
          for (int k = 0; k < mesh.f[f].n; k++)
            if (mesh.f[f].i[k] >= mesh.nv) ok = 0;
        }
      } else if (!err) ok = 0;
      glb_close(&m);
    }
  } else if (!err) ok = 0;
  free(data);
  return ok;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: fuzz_glb SECONDS [SEED.glb...]\n");
    return 2;
  }
  double secs = safe_num(argv[1], 0);
  enum { CAP = 1 << 16, MAX_SEEDS = 16 };
  static uint8_t seeds[MAX_SEEDS][CAP], buf[CAP];
  size_t seed_n[MAX_SEEDS];
  int ns = 0;
  seed_n[ns] = build(seeds[ns], "{\"asset\":{\"version\":\"2.0\"}}", 0), ns++;
  seed_n[ns] = build(seeds[ns], "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":24}]}", 24), ns++;
  seed_n[ns] = build(seeds[ns], "{}", 4), ns++;
  seed_n[ns] = build(seeds[ns],
                     "{\"buffers\":[{\"byteLength\":96}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},"
                     "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":40,\"byteStride\":20}],\"accessors\":["
                     "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                     "{\"bufferView\":0,\"byteOffset\":4,\"componentType\":5123,\"count\":8,\"type\":\"SCALAR\"},"
                     "{\"bufferView\":1,\"componentType\":5121,\"count\":2,\"type\":\"MAT2\",\"normalized\":true},"
                     "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
                     "{\"bufferView\":0,\"componentType\":5120,\"count\":3,\"type\":\"MAT4\"}]}",
                     96), ns++;
  /* our own writer's files: a built-in jet and the drone */
  mesh_init_builtin();
  const Mesh *own[] = { &MESH_JET[0], &MESH_DRONE, &MESH_SAM_BOX };
  for (size_t k = 0; k < sizeof own / sizeof *own && ns < MAX_SEEDS; k++) {
    size_t n;
    uint8_t *b = glb_write_mesh(own[k], "seed", &n);
    if (b && n <= CAP) {
      memcpy(seeds[ns], b, n);
      seed_n[ns++] = n;
    }
    free(b);
  }
  for (int i = 2; i < argc && ns < MAX_SEEDS; i++) {
    FILE *f = fopen(argv[i], "rb");
    if (!f) continue;
    seed_n[ns] = fread(seeds[ns], 1, CAP, f);
    fclose(f);
    if (seed_n[ns]) ns++;
  }
  rng ^= (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&secs;
  static const uint32_t EDGE[] = { 0, 1, 2, 3, 4, 8, 11, 12, 16, 20, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFCu,
                                   GLB_MAGIC, GLB_CHUNK_JSON, GLB_CHUNK_BIN };
  long tried = 0;
  clock_t end = clock() + (clock_t)(secs * CLOCKS_PER_SEC);
  while (clock() < end) {
    for (int batch = 0; batch < 1024; batch++) {
      int s = (int)below((size_t)ns);
      size_t n = seed_n[s];
      memcpy(buf, seeds[s], n);
      for (int r = 1 + (int)below(3); r > 0; r--) {
        switch (below(7)) {
        case 5:
        case 6: { /* a digit in the JSON chunk changed in place */
          size_t jl = n >= 16 ? (size_t)(buf[12] | buf[13] << 8) : 0;
          if (jl && 20 + jl <= n) {
            for (int tries = 0; tries < 32; tries++) {
              size_t at = 20 + below(jl);
              if (buf[at] >= '0' && buf[at] <= '9') {
                static const char REP[] = "0123456789-.e9";
                buf[at] = (uint8_t)REP[below(sizeof REP - 1)];
                break;
              }
            }
          }
          break;
        }
        case 0: /* an edge value in a header or chunk field (or anywhere, 4-aligned) */
          if (n >= 4) put32(buf + 4 * below(n / 4 < 12 ? n / 4 : 12), EDGE[below(sizeof EDGE / sizeof *EDGE)]);
          break;
        case 1:
          if (n) buf[below(n)] ^= (uint8_t)(1u << below(8));
          break;
        case 2:
          n = below(n + 1);
          break;
        case 3: { /* grow by a few bytes */
          size_t k = 1 + below(16);
          if (n + k <= CAP) {
            for (size_t i = 0; i < k; i++) buf[n + i] = (uint8_t)next();
            n += k;
          }
          break;
        }
        default:
          if (n) buf[below(n)] = (uint8_t)next();
        }
      }
      tried++;
      if (!check(buf, n)) {
        FILE *f = fopen("fuzz-fail.glb", "wb");
        if (f) {
          fwrite(buf, 1, n, f);
          fclose(f);
        }
        fprintf(stderr, "fuzz_glb: a rule broke after %ld inputs; saved fuzz-fail.glb\n", tried);
        return 1;
      }
    }
  }
  printf("fuzz_glb: %ld inputs (%ld accepted, %ld accessors and %ld elements read, %ld meshes built), %d seeds, no rule "
         "broken\n", tried, accepted, accessors_read, elements_read, meshes_built, ns);
  return 0;
}
