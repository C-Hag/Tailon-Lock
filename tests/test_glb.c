/* test_glb.c - checks for our own GLB reader (src/core/glb.c): the container
 * rules of the glTF 2.0 binary format, and files it must refuse. Original
 * code for this project, written from the Khronos glTF 2.0 specification. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/glb.h"

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
  uint8_t b[4096];
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

int main(void) {
  test_container();
  test_refused();
  if (failures) {
    fprintf(stderr, "%d of %d glb checks failed\n", failures, checks);
    return 1;
  }
  printf("glb tests passed (%d checks)\n", checks);
  return 0;
}
