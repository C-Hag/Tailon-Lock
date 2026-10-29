/* json_dump.c - writes a JSON file as a canonical text tree, one value a
 * line, for the checker's golden-data stage (tools/check). The same dump
 * from the old reader and from ours must be byte for byte the same.
 * Original code for this project.
 *
 * usage: json_dump FILE [OPEN CLOSE]
 *   OPEN CLOSE: a table inside a .js file, from the first OPEN to the last
 *   CLOSE (as assets.c reads jets_game.js and carrier_game.js)
 *
 * The lines: depth spaces, then the member name in quotes and a colon for an
 * object's member, then one of
 *   null | true | false | n <number as %.17g> | s "<string>" | [ <count> | { <count>
 * Strings keep their UTF-8 bytes; '"', '\' and bytes below 0x20 are written
 * as \" \\ \xHH. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static void put_str(FILE *o, const char *s) {
  fputc('"', o);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') fprintf(o, "\\%c", *p);
    else if (*p < 0x20) fprintf(o, "\\x%02X", *p);
    else fputc(*p, o);
  }
  fputc('"', o);
}

static void dump(FILE *o, const cJSON *v, int depth) {
  for (int i = 0; i < depth; i++) fputc(' ', o);
  if (v->string) {
    put_str(o, v->string);
    fputc(':', o);
  }
  if (cJSON_IsNull(v)) fputs("null\n", o);
  else if (cJSON_IsTrue(v)) fputs("true\n", o);
  else if (cJSON_IsFalse(v)) fputs("false\n", o);
  else if (cJSON_IsNumber(v)) fprintf(o, "n %.17g\n", v->valuedouble);
  else if (cJSON_IsString(v)) {
    fputs("s ", o);
    put_str(o, v->valuestring);
    fputc('\n', o);
  } else if (cJSON_IsArray(v) || cJSON_IsObject(v)) {
    fprintf(o, "%c %d\n", cJSON_IsArray(v) ? '[' : '{', cJSON_GetArraySize(v));
    const cJSON *e;
    cJSON_ArrayForEach(e, v) dump(o, e, depth + 1);
  } else fputs("?\n", o);
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 4) {
    fprintf(stderr, "usage: json_dump FILE [OPEN CLOSE]\n");
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
  char *text = malloc((size_t)n + 1);
  if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) {
    fprintf(stderr, "%s: read failed\n", argv[1]);
    return 1;
  }
  fclose(f);
  text[n] = 0;
  const char *a = text;
  size_t len = (size_t)n;
  if (argc == 4) {
    const char *b = strrchr(text, argv[3][0]);
    a = strchr(text, argv[2][0]);
    if (!a || !b || b < a) {
      fprintf(stderr, "%s: no %s...%s table\n", argv[1], argv[2], argv[3]);
      return 1;
    }
    len = (size_t)(b - a + 1);
  }
  cJSON *j = cJSON_ParseWithLength(a, len);
  if (!j) {
    printf("invalid\n");
    free(text);
    return 0;
  }
  dump(stdout, j, 0);
  cJSON_Delete(j);
  free(text);
  return 0;
}
