/* lf_json.c - a libFuzzer target for our own JSON reader (src/core/json.c),
 * the libFuzzer version of tests/fuzz_json.c with the same rules. Original
 * code for this project. Build with -DA4C_LIBFUZZER=ON (clang); run it under
 * ASan and UBSan.
 *
 * The input is the text, in a buffer of exactly its own size. The tokenizer
 * must end (no more tokens than bytes), every token lie inside the text and
 * after the last one, a string unescape to exactly out_len bytes of valid
 * UTF-8, and an error stay an error. A text the tree accepts must have no
 * token error, and every node be well formed. A broken rule aborts (a crash
 * libFuzzer saves). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"

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

static void broken(const char *rule) {
  fprintf(stderr, "lf_json: %s\n", rule);
  abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  char *text = malloc(n ? n : 1);
  if (!text) return 0;
  if (n) memcpy(text, data, n);
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, n);
  size_t last_end = 0, count = 0;
  for (;;) {
    JsonTokKind k = json_lex_next(&lx, &t);
    if (k == JTOK_END) break;
    if (k == JTOK_ERROR) {
      if (!lx.err || lx.err_at > n || json_lex_next(&lx, &t) != JTOK_ERROR) broken("an error that does not stay an error");
      break;
    }
    if (++count > n || t.at < last_end || t.at + t.len > n) broken("a token outside the text, or out of order");
    if (k == JTOK_STRING) {
      unsigned char *out = malloc(t.out_len ? t.out_len : 1);
      if (!out) break;
      json_lex_string(&lx, &t, (char *)out);
      if (!valid_utf8(out, t.out_len)) broken("a string that unescapes to invalid UTF-8"); /* \u0000 is valid */
      free(out);
    }
    last_end = t.at + t.len;
  }
  bool lex_error = lx.err != NULL;
  JsonError e;
  Json *d = json_parse(text, n, &e);
  if (d) {
    if (lex_error || e.what) broken("a tree from a text with a token error");
    if (!tree_ok(d, -1, 0)) broken("a node that is not well formed");
    json_free(d);
  } else if (!e.what || e.at > n) broken("a refusal that does not say why, or where");
  free(text);
  return 0;
}
