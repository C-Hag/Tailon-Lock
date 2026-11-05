/* json_dump.c - writes a JSON file as a canonical text tree, one value a
 * line, for the checker's golden-data stage (tools/check). The references
 * were taken with the old reader; ours must give them byte for byte.
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"

static void put_str(FILE *o, const char *s) {
  fputc('"', o);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') fprintf(o, "\\%c", *p);
    else if (*p < 0x20) fprintf(o, "\\x%02X", *p);
    else fputc(*p, o);
  }
  fputc('"', o);
}

static void dump(FILE *o, const Json *v, int depth) {
  for (int i = 0; i < depth; i++) fputc(' ', o);
  if (v->name) {
    put_str(o, v->name);
    fputc(':', o);
  }
  switch (v->type) {
  case JSON_NULL: fputs("null\n", o); break;
  case JSON_TRUE: fputs("true\n", o); break;
  case JSON_FALSE: fputs("false\n", o); break;
  case JSON_NUMBER: fprintf(o, "n %.17g\n", v->num); break;
  case JSON_STRING:
    fputs("s ", o);
    put_str(o, v->str);
    fputc('\n', o);
    break;
  default:
    fprintf(o, "%c %d\n", v->type == JSON_ARRAY ? '[' : '{', v->count);
    JSON_EACH(e, v) dump(o, e, depth + 1);
  }
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
  char *text = n >= 0 ? malloc((size_t)n + 1) : NULL; /* ftell is -1 on a pipe: no size to read */
  if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) {
    fprintf(stderr, "%s: read failed\n", argv[1]);
    free(text);
    fclose(f);
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
      free(text);
      return 1;
    }
    len = (size_t)(b - a + 1);
  }
  Json *d = json_parse(a, len, NULL);
  if (!d) printf("invalid\n");
  else dump(stdout, d, 0);
  json_free(d);
  free(text);
  return 0;
}
