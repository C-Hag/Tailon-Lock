/* glb_dump.c - reads a .glb file with our own reader (src/core/glb.c) into
 * our mesh format and writes the mesh's canonical dump (mesh_dump), for the
 * checker's golden-data stage (tools/check). Original code for this project.
 *
 * usage: glb_dump FILE.glb */
#include <stdio.h>
#include <stdlib.h>

#include "core/glb.h"
#include "core/mesh.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: glb_dump FILE.glb\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
    fprintf(stderr, "%s: read failed\n", argv[1]);
    return 1;
  }
  fclose(f);
  GlbModel m;
  const char *err;
  static Mesh mesh;
  if (!glb_load(b, (size_t)n, &m, &err) || !glb_mesh(&m, &mesh, &err)) {
    printf("invalid: %s\n", err ? err : "?");
    return 1;
  }
  mesh_dump(&mesh, stdout);
  glb_close(&m);
  free(b);
  return 0;
}
