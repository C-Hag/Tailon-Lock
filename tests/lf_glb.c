/* lf_glb.c - a libFuzzer target for our own glTF binary reader
 * (src/core/glb.c), the libFuzzer version of tests/fuzz_glb.c with the same
 * rules. Original code for this project. Build with -DTL_LIBFUZZER=ON
 * (clang); run it under ASan and UBSan.
 *
 * The input is the .glb file, in a buffer of exactly its own size. An
 * accepted file's chunks must lie inside the header's length and the buffer,
 * the JSON chunk first and not empty, and a refusal must say why. The model
 * is then loaded, every element of every accessor it accepts read (each
 * accessor inside the BIN chunk), and the mesh built: every face it makes
 * must point at vertices that exist. A broken rule aborts. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/glb.h"
#include "core/json.h"
#include "core/mesh.h"

static void broken(const char *rule) {
  fprintf(stderr, "lf_glb: %s\n", rule);
  abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *src, size_t n) {
  uint8_t *data = malloc(n ? n : 1);
  if (!data) return 0;
  if (n) memcpy(data, src, n);
  GlbFile g;
  const char *err = NULL;
  if (!glb_open(data, n, &g, &err)) {
    if (!err) broken("a refused file without a reason");
    free(data);
    return 0;
  }
  uint32_t total = (uint32_t)(data[8] | data[9] << 8 | data[10] << 16 | (uint32_t)data[11] << 24);
  const uint8_t *j = (const uint8_t *)g.json;
  if (err || !g.json || g.json_len == 0 || j != data + 20 || g.json_len > total - 20 || total > n)
    broken("the JSON chunk is not first, or not inside the file");
  if (g.bin && (g.bin < data + 20 || g.bin + g.bin_len > data + total)) broken("the BIN chunk is not inside the file");
  GlbModel m;
  if (glb_load(data, n, &m, &err)) {
    int na = json_count(json_get(m.doc, "accessors"));
    for (int i = 0; i < na && i < 64; i++) {
      GlbAccessor a;
      if (!glb_accessor(&m, i, &a, &err)) {
        if (!err) broken("a refused accessor without a reason");
        continue;
      }
      if (a.data < g.bin || a.data + (a.count - 1) * a.stride + a.elem_size > g.bin + g.bin_len)
        broken("an accessor outside the BIN chunk");
      volatile double sum = 0;
      for (size_t e = 0; e < a.count; e++)
        for (int c = 0; c < a.comps; c++) sum += glb_get(&a, e, c);
    }
    static Mesh mesh;
    if (glb_mesh(&m, &mesh, &err)) {
      if (mesh.nf < 1 || mesh.nv > 65535) broken("a mesh with no faces, or too many vertices");
      for (int f = 0; f < mesh.nf; f++) {
        if (mesh.f[f].n < 3 || mesh.f[f].n > FACE_MAX_V) broken("a face with too few or too many corners");
        for (int k = 0; k < mesh.f[f].n; k++)
          if (mesh.f[f].i[k] >= mesh.nv) broken("a face that points past the vertices");
      }
    } else if (!err) broken("a refused mesh without a reason");
    glb_close(&m);
  } else if (!err) broken("a refused model without a reason");
  free(data);
  return 0;
}
