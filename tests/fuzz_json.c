/* fuzz_json.c - a mutation fuzzer for our own JSON reader (src/core/json.c).
 * Original code for this project. Run it under ASan/UBSan.
 *
 * usage: fuzz_json SECONDS [SEED_FILE...]
 *
 * Starts from the seed files (and a few built-in texts), and keeps making
 * new inputs from them: bytes flipped, inserted, deleted, spans duplicated or
 * swapped, tokens spliced in, cut short. Every input lives in a buffer of
 * exactly its own size, so a read past the end is a sanitizer error. For each
 * input it checks that the tokenizer ends (no more tokens than bytes), that
 * every token lies inside the text and after the last one, that a string
 * unescapes to exactly out_len bytes of valid UTF-8, and that an error stays
 * an error. It also builds the tree: a text the tree accepts must have no
 * token error, and every node must be well formed (a name exactly on an
 * object's members, strings zero-terminated at their length, counts that
 * match). Prints the inputs tried and the tokens seen; exits 1 on a broken
 * rule (the input is saved to fuzz-fail.json). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/json.h"

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) { /* xorshift64* */
  rng ^= rng >> 12;
  rng ^= rng << 25;
  rng ^= rng >> 27;
  return rng * 0x2545F4914F6CDD1Dull;
}
static size_t below(size_t n) { return n ? (size_t)(next() % n) : 0; }

static int valid_utf8(const unsigned char *s, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned char c = s[i];
    size_t k = c < 0x80 ? 1 : c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
    if (!k || i + k > n) return 0;
    unsigned long cp = k == 1 ? c : k == 2 ? c & 0x1F : k == 3 ? c & 0x0F : c & 0x07;
    for (size_t j = 1; j < k; j++) {
      if ((s[i + j] & 0xC0) != 0x80) return 0;
      cp = cp << 6 | (s[i + j] & 0x3F);
    }
    static const unsigned long MIN[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < MIN[k] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    i += k;
  }
  return 1;
}

static long tokens_seen, errors_seen, trees_built;

/* a node and everything under it is well formed; parent: the type it sits in, -1 at the root */
static int tree_ok(const Json *v, int parent, int depth) {
  if (depth > JSON_MAX_DEPTH) return 0;
  if ((parent == JSON_OBJECT) != (v->name != NULL)) return 0;
  if (v->name && v->name[v->name_len] != 0) return 0;
  if (v->type == JSON_STRING && (!v->str || v->str[v->len] != 0 || !valid_utf8((const unsigned char *)v->str, v->len)))
    return 0;
  if (v->type == JSON_ARRAY || v->type == JSON_OBJECT) {
    if (v->count < 0 || (v->count > 0) != (v->items != NULL) || json_count(v) != v->count) return 0;
    int n = 0;
    JSON_EACH(e, v) {
      if (!tree_ok(e, (int)v->type, depth + 1)) return 0;
      n++;
    }
    if (n != v->count) return 0;
  }
  return 1;
}

static int check(const char *src, size_t n) {
  char *text = malloc(n ? n : 1);
  memcpy(text, src, n);
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, n);
  size_t last_end = 0, count = 0;
  int ok = 1;
  for (;;) {
    JsonTokKind k = json_lex_next(&lx, &t);
    if (k == JTOK_END) break;
    if (k == JTOK_ERROR) {
      errors_seen++;
      if (!lx.err || lx.err_at > n || json_lex_next(&lx, &t) != JTOK_ERROR) ok = 0;
      break;
    }
    if (++count > n || t.at < last_end || t.at + t.len > n) {
      ok = 0;
      break;
    }
    if (k == JTOK_STRING) {
      unsigned char *out = malloc(t.out_len ? t.out_len : 1);
      json_lex_string(&lx, &t, (char *)out);
      /* \u0000 is a valid escape: a zero byte is valid UTF-8 */
      if (!valid_utf8(out, t.out_len)) ok = 0;
      free(out);
    }
    last_end = t.at + t.len;
    tokens_seen++;
  }
  /* the tree, from the same bytes */
  bool lex_error = lx.err != NULL;
  JsonError e;
  Json *d = json_parse(text, n, &e);
  if (d) {
    trees_built++;
    if (lex_error || e.what || !tree_ok(d, -1, 0)) ok = 0;
    json_free(d);
  } else if (!e.what || e.at > n) ok = 0;
  free(text);
  return ok;
}

static const char *const PIECES[] = { "{", "}", "[", "]", ":", ",", "\"", "\\", "\\u", "\\uD834", "\\uDD1E", "true",
                                      "false", "null", "-", "0", "1", "e", "E", "+", ".", " ", "\n", "\xC3", "\xA9",
                                      "\xF0\x9D\x84\x9E", "\xED\xA0\x80", "\x00", "\x1F", "\xFF", "123.456e-7" };

static size_t mutate(char *buf, size_t n, size_t cap) {
  int rounds = 1 + (int)below(4);
  while (rounds--) {
    switch (below(7)) {
    case 0: /* flip a byte */
      if (n) buf[below(n)] ^= (char)(1u << below(8));
      break;
    case 1: /* a random byte in place of one */
      if (n) buf[below(n)] = (char)next();
      break;
    case 2: { /* insert a piece */
      const char *p = PIECES[below(sizeof PIECES / sizeof *PIECES)];
      size_t k = p[0] ? strlen(p) : 1, at = below(n + 1);
      if (n + k > cap) break;
      memmove(buf + at + k, buf + at, n - at);
      memcpy(buf + at, p, k);
      n += k;
      break;
    }
    case 3: { /* delete a span */
      if (!n) break;
      size_t at = below(n), k = 1 + below(n - at < 8 ? n - at : 8);
      memmove(buf + at, buf + at + k, n - at - k);
      n -= k;
      break;
    }
    case 4: { /* duplicate a span */
      if (!n) break;
      size_t at = below(n), k = 1 + below(n - at < 32 ? n - at : 32);
      if (n + k > cap) break;
      memmove(buf + at + k, buf + at, n - at);
      n += k;
      break;
    }
    case 5: /* cut short */
      n = below(n + 1);
      break;
    default: { /* swap two bytes */
      if (n < 2) break;
      size_t a = below(n), b = below(n);
      char c = buf[a];
      buf[a] = buf[b];
      buf[b] = c;
      break;
    }
    }
  }
  return n;
}

static char *load(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *b = len > 0 ? malloc((size_t)len) : NULL;
  if (b && fread(b, 1, (size_t)len, f) != (size_t)len) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *n = b ? (size_t)len : 0;
  return b;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: fuzz_json SECONDS [SEED_FILE...]\n");
    return 2;
  }
  double secs = atof(argv[1]);
  enum { MAX_SEEDS = 64, SEED_CAP = 4096 };
  static const char *const BUILTIN[] = { "{\"a\":[1,-2.5e3,true,false,null,\"x\\u00e9\\uD834\\uDD1E\\n\"]}",
                                         "[\"\xC3\xA5\xE2\x82\xAC\xF0\x9D\x84\x9E\", 0, -0.0, 1E+2]", "\"\\\"\\\\\\/\"" };
  char *seeds[MAX_SEEDS];
  size_t seed_n[MAX_SEEDS];
  int ns = 0;
  for (size_t i = 0; i < sizeof BUILTIN / sizeof *BUILTIN; i++) {
    size_t n = strlen(BUILTIN[i]);
    seeds[ns] = malloc(n);
    memcpy(seeds[ns], BUILTIN[i], n);
    seed_n[ns++] = n;
  }
  for (int i = 2; i < argc && ns < MAX_SEEDS; i++) {
    size_t n;
    char *b = load(argv[i], &n);
    if (!b) continue;
    /* big files are cut into pieces: a fuzzer wants many short inputs */
    for (size_t at = 0; at < n && ns < MAX_SEEDS; at += SEED_CAP * 8) {
      size_t k = n - at < SEED_CAP ? n - at : SEED_CAP;
      seeds[ns] = malloc(k);
      memcpy(seeds[ns], b + at, k);
      seed_n[ns++] = k;
    }
    free(b);
  }
  rng ^= (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&secs; /* runs side by side get their own streams */
  enum { CAP = SEED_CAP * 2 };
  static char buf[CAP];
  long tried = 0;
  clock_t end = clock() + (clock_t)(secs * CLOCKS_PER_SEC);
  while (clock() < end) {
    for (int batch = 0; batch < 256; batch++) {
      int s = (int)below((size_t)ns);
      size_t n = seed_n[s];
      memcpy(buf, seeds[s], n);
      n = mutate(buf, n, CAP);
      tried++;
      if (!check(buf, n)) {
        FILE *f = fopen("fuzz-fail.json", "wb");
        if (f) {
          fwrite(buf, 1, n, f);
          fclose(f);
        }
        fprintf(stderr, "fuzz: a rule broke after %ld inputs; saved fuzz-fail.json\n", tried);
        return 1;
      }
    }
  }
  printf("fuzz: %ld inputs (%ld refused by the tokenizer, %ld trees built), %ld tokens, %d seeds, no rule broken\n", tried,
         errors_seen, trees_built, tokens_seen, ns);
  for (int i = 0; i < ns; i++) free(seeds[i]);
  return 0;
}
