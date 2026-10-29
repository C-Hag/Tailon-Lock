/* test_json.c - checks for our own JSON reader (src/core/json.c): the
 * examples and the grammar of RFC 8259, 200 malformed inputs for the
 * tokenizer, and the tree: its shape, its getters, documents it must refuse,
 * and stress cases (huge, deep, wide), all freed again (ASan's leak check).
 * Original code for this project, written from RFC 8259 and RFC 3629. */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"

static int failures, checks;

#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) {                                                                    \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);        \
      failures++;                                                                     \
    }                                                                                 \
  } while (0)

/* every token of text as a string of letters: { } [ ] : , s n t f z, E for an error */
static void kinds(const char *text, size_t len, char *out, size_t cap) {
  static const char L[] = "\0E{}[]:,sntfz";
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, len);
  size_t n = 0;
  while (n + 1 < cap) {
    JsonTokKind k = json_lex_next(&lx, &t);
    if (k == JTOK_END) break;
    out[n++] = L[k];
    if (k == JTOK_ERROR) break;
  }
  out[n] = 0;
}

static void expect_kinds(const char *text, const char *want) {
  char got[256];
  kinds(text, strlen(text), got, sizeof got);
  if (strcmp(got, want) != 0) fprintf(stderr, "  tokens of %s: got \"%s\", want \"%s\"\n", text, got, want);
  CHECK(strcmp(got, want) == 0);
}

/* the one token of text, which must be a number */
static double number(const char *text) {
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, strlen(text));
  if (json_lex_next(&lx, &t) != JTOK_NUMBER) {
    fprintf(stderr, "  %s: not a number\n", text);
    failures++;
    return NAN;
  }
  CHECK(json_lex_next(&lx, &t) == JTOK_END);
  return t.num;
}

/* the one token of text, which must be a string: its bytes, unescaped */
static int string_is(const char *text, const char *want, size_t want_len) {
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, strlen(text));
  if (json_lex_next(&lx, &t) != JTOK_STRING) {
    fprintf(stderr, "  %s: not a string (%s)\n", text, lx.err ? lx.err : "?");
    return 0;
  }
  char *out = malloc(t.out_len ? t.out_len : 1); /* exactly out_len: ASan sees an overrun */
  json_lex_string(&lx, &t, out);
  int ok = t.out_len == want_len && memcmp(out, want, want_len) == 0;
  if (!ok) fprintf(stderr, "  %s: wrong bytes (%zu, want %zu)\n", text, t.out_len, want_len);
  free(out);
  return ok;
}

static void test_rfc_examples(void) {
  /* section 13: an object with a nested object and an array of numbers */
  expect_kinds("{\n"
               "  \"Image\": {\n"
               "    \"Width\":  800,\n"
               "    \"Height\": 600,\n"
               "    \"Title\":  \"View from 15th Floor\",\n"
               "    \"Thumbnail\": {\n"
               "      \"Url\":    \"http://www.example.com/image/481989943\",\n"
               "      \"Height\": 125,\n"
               "      \"Width\":  100\n"
               "    },\n"
               "    \"Animated\" : false,\n"
               "    \"IDs\": [116, 943, 234, 38793]\n"
               "  }\n"
               "}",
               "{s:{s:n,s:n,s:s,s:{s:s,s:n,s:n},s:f,s:[n,n,n,n]}}");
  /* section 13: an array of two objects, with negative and fractional numbers */
  expect_kinds("[\n"
               "  {\n"
               "    \"precision\": \"zip\",\n"
               "    \"Latitude\":  37.7668,\n"
               "    \"Longitude\": -122.3959,\n"
               "    \"Address\":   \"\",\n"
               "    \"City\":      \"SAN FRANCISCO\",\n"
               "    \"State\":     \"CA\",\n"
               "    \"Zip\":       \"94107\",\n"
               "    \"Country\":   \"US\"\n"
               "  }\n"
               "]",
               "[{s:s,s:n,s:n,s:s,s:s,s:s,s:s,s:s}]");
  /* section 13: texts that are only a value */
  expect_kinds("\"Hello world!\"", "s");
  expect_kinds("42", "n");
  expect_kinds("true", "t");
  CHECK(number("37.7668") == 37.7668);
  CHECK(number("-122.3959") == -122.3959);
  /* section 7: the G clef, U+1D11E, as a UTF-16 surrogate pair */
  CHECK(string_is("\"\\uD834\\uDD1E\"", "\xF0\x9D\x84\x9E", 4));
}

static void test_tokens(void) {
  expect_kinds("", "");
  expect_kinds(" \t\r\n", "");
  expect_kinds("{}[]:,", "{}[]:,");
  expect_kinds("[true,false,null]", "[t,f,z]");
  expect_kinds("  [ 1 , \"a\" ]  ", "[n,s]");
  expect_kinds("\xEF\xBB\xBF[1]", "[n]"); /* a byte order mark is skipped */
  /* structure is the tree's business: the tokenizer passes these */
  expect_kinds("[1,]", "[n,]");
  expect_kinds("}{", "}{");
  expect_kinds("1 2", "nn");
}

static void test_numbers(void) {
  CHECK(number("0") == 0 && !signbit(number("0")));
  CHECK(number("-0") == 0 && signbit(number("-0")));
  CHECK(number("1") == 1);
  CHECK(number("-1") == -1);
  CHECK(number("1.5") == 1.5);
  CHECK(number("1.5e3") == 1500);
  CHECK(number("1.5E3") == 1500);
  CHECK(number("1E+2") == 100);
  CHECK(number("-1.25e-2") == -0.0125);
  CHECK(number("0.1") == 0.1);
  CHECK(number("123456789") == 123456789);
  CHECK(number("9007199254740993") == 9007199254740992.0); /* nearest double: ties to even */
  CHECK(number("1e-320") > 0 && number("1e-320") < 1e-300); /* subnormal */
  CHECK(number("4.9406564584124654e-324") == 4.9406564584124654e-324);
  CHECK(number("1.7976931348623157e308") == 1.7976931348623157e308);
  CHECK(number("123456789012345678901234567890") == 1.2345678901234568e29);
  CHECK(number("0.30000000000000004") == 0.30000000000000004);
}

static void test_strings(void) {
  CHECK(string_is("\"\"", "", 0));
  CHECK(string_is("\"abc\"", "abc", 3));
  CHECK(string_is("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "\"\\/\b\f\n\r\t", 8));
  CHECK(string_is("\"\\u0041\\u00e9\\u20AC\"", "A\xC3\xA9\xE2\x82\xAC", 6));
  CHECK(string_is("\"\\u0000\"", "\0", 1));
  CHECK(string_is("\"\xC3\x85\xC3\x84\xC3\x96\"", "\xC3\x85\xC3\x84\xC3\x96", 6)); /* raw UTF-8 */
  CHECK(string_is("\"\xF4\x8F\xBF\xBF\"", "\xF4\x8F\xBF\xBF", 4));                 /* U+10FFFF, the last */
  CHECK(string_is("\"\xEF\xBF\xBF\"", "\xEF\xBF\xBF", 3));                         /* U+FFFF */
  CHECK(string_is("\"\\uDBFF\\uDFFF\"", "\xF4\x8F\xBF\xBF", 4));
  CHECK(string_is("\"a\x7F b\"", "a\x7F b", 4)); /* DEL is no control character in JSON */
}

/* 40 bad tokens in 5 places each: 200 inputs the tokenizer must refuse */
static void test_malformed(void) {
  static const char *const BAD[] = {
    /* numbers */
    "01", "-", "+1", ".5", "1.", "1.e5", "1e", "1e+", "--1", "0x10", "1a", "Infinity", "NaN", "-Infinity", "1.5.5",
    "0..1", "1e5.5",
    /* words */
    "tru", "nul", "True", "FALSE", "nulll", "undefined",
    /* strings */
    "\"abc", "\"a\nb\"", "\"\\x41\"", "\"\\u12G4\"", "\"\\u12\"", "\"\\uDC00\"", "\"\\uD800\"", "\"\\uD800x\"",
    "\"\\uD800\\u0041\"", "\"\xC0\xAF\"", "\"\xE0\x80\xAF\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\xC3\"",
    "\"\xFF\"",
    /* characters JSON has no use for */
    "'a'", "#",
  };
  static const char *const PLACE[] = { "%s", "[%s]", "{\"k\":%s}", " \n%s\t", "[1,%s,2]" };
  int n = 0, refused = 0;
  for (size_t b = 0; b < sizeof BAD / sizeof *BAD; b++)
    for (size_t p = 0; p < sizeof PLACE / sizeof *PLACE; p++) {
      char text[128];
      snprintf(text, sizeof text, PLACE[p], BAD[b]);
      char got[64];
      kinds(text, strlen(text), got, sizeof got);
      n++;
      if (strchr(got, 'E')) refused++;
      else fprintf(stderr, "  malformed input accepted: %s -> %s\n", text, got);
    }
  CHECK(n == 200);
  CHECK(refused == n);
  printf("malformed inputs: %d of %d refused\n", refused, n);
  /* a text cut short anywhere inside a token is refused, never read past its end */
  static const char *const WHOLE[] = { "\"\\uD834\\uDD1E\"", "-1.5e-3x", "\"\xF0\x9D\x84\x9E\"" };
  for (size_t w = 0; w < sizeof WHOLE / sizeof *WHOLE; w++)
    for (size_t cut = 1; cut + 1 < strlen(WHOLE[w]); cut++) {
      char *t = malloc(cut); /* exactly the bytes given: ASan sees a read past them */
      memcpy(t, WHOLE[w], cut);
      char got[64];
      kinds(t, cut, got, sizeof got);
      free(t);
      if (WHOLE[w][0] == '"') CHECK(strcmp(got, "E") == 0);
    }
}

static void test_errors(void) {
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, "[1, @]", 6);
  CHECK(json_lex_next(&lx, &t) == JTOK_ARR_OPEN);
  CHECK(json_lex_next(&lx, &t) == JTOK_NUMBER);
  CHECK(json_lex_next(&lx, &t) == JTOK_COMMA);
  CHECK(json_lex_next(&lx, &t) == JTOK_ERROR);
  CHECK(lx.err_at == 4 && lx.err && strstr(lx.err, "character"));
  CHECK(json_lex_next(&lx, &t) == JTOK_ERROR); /* stays stuck */
  json_lex_init(&lx, NULL, 5);
  CHECK(json_lex_next(&lx, &t) == JTOK_END);
}

/* ---- the tree ------------------------------------------------------------- */

static Json *parse(const char *text) { return json_parse(text, strlen(text), NULL); }

static void test_tree(void) {
  Json *d = parse("{\"Image\": {\"Width\": 800, \"Height\": 600, \"Title\": \"View from 15th Floor\","
                  " \"Thumbnail\": {\"Url\": \"http://www.example.com/image/481989943\", \"Height\": 125, \"Width\": 100},"
                  " \"Animated\": false, \"IDs\": [116, 943, 234, 38793]}}");
  CHECK(d && json_is(d, JSON_OBJECT) && json_count(d) == 1 && d->name == NULL);
  const Json *im = json_get(d, "Image");
  CHECK(json_is(im, JSON_OBJECT) && json_count(im) == 6 && im->name && !strcmp(im->name, "Image"));
  CHECK(json_number(json_get(im, "Width"), 0) == 800);
  CHECK(json_int(json_get(im, "Height"), 0) == 600);
  CHECK(!strcmp(json_string(json_get(im, "Title"), ""), "View from 15th Floor"));
  CHECK(json_number(json_get(json_get(im, "Thumbnail"), "Width"), 0) == 100);
  CHECK(json_is(json_get(im, "Animated"), JSON_FALSE) && !json_is_true(json_get(im, "Animated")));
  const Json *ids = json_get(im, "IDs");
  CHECK(json_count(ids) == 4 && json_int(json_at(ids, 3), 0) == 38793 && json_at(ids, 4) == NULL && json_at(ids, -1) == NULL);
  int sum = 0, n = 0;
  JSON_EACH(e, ids) {
    sum += json_int(e, 0);
    CHECK(e->name == NULL);
    n++;
  }
  CHECK(n == 4 && sum == 116 + 943 + 234 + 38793);
  /* members in document order, names on each */
  const char *want[] = { "Width", "Height", "Title", "Thumbnail", "Animated", "IDs" };
  n = 0;
  JSON_EACH(m, im) CHECK(n < 6 && !strcmp(m->name, want[n++]));
  /* NULL and wrong types fall back quietly */
  CHECK(json_get(NULL, "x") == NULL && json_get(ids, "x") == NULL && json_get(im, "nope") == NULL);
  CHECK(json_number(json_get(im, "Title"), -1) == -1 && json_string(json_get(im, "Width"), NULL) == NULL);
  CHECK(json_count(NULL) == 0 && json_count(json_get(im, "Width")) == 0 && json_int(NULL, 7) == 7);
  n = 0;
  JSON_EACH(e, (const Json *)NULL) n++;
  JSON_EACH(e, json_get(im, "Width")) n++;
  CHECK(n == 0);
  json_free(d);
  json_free(NULL);

  d = parse("[ ]");
  CHECK(d && json_count(d) == 0 && json_begin(d) == json_end(d));
  json_free(d);
  d = parse(" \"top\" ");
  CHECK(d && json_is(d, JSON_STRING) && !strcmp(d->str, "top") && d->len == 3);
  json_free(d);
  d = parse("{\"a\": 1, \"a\": 2, \"\": 3, \"x\\u0000y\": 4}"); /* the first of two equal names; empty and odd names */
  CHECK(d && json_count(d) == 4 && json_int(json_get(d, "a"), 0) == 1 && json_int(json_get(d, ""), 0) == 3);
  CHECK(json_get(d, "x") == NULL && json_at(d, 3)->name_len == 3);
  json_free(d);
  d = parse("[\"a\\u0000b\"]");
  CHECK(d && json_at(d, 0)->len == 3 && json_at(d, 0)->str[1] == 0 && json_at(d, 0)->str[3] == 0);
  json_free(d);
  /* json_int: toward zero, saturating */
  d = parse("[2.9, -2.9, 1e10, -1e10, 2147483647, -2147483648, 0.5]");
  CHECK(json_int(json_at(d, 0), 0) == 2 && json_int(json_at(d, 1), 0) == -2);
  CHECK(json_int(json_at(d, 2), 0) == INT_MAX && json_int(json_at(d, 3), 0) == INT_MIN);
  CHECK(json_int(json_at(d, 4), 0) == INT_MAX && json_int(json_at(d, 5), 0) == INT_MIN && json_int(json_at(d, 6), 9) == 0);
  json_free(d);
}

/* documents the tree must refuse, with tokens that are each fine */
static void test_tree_refuses(void) {
  static const char *const BAD[] = {
    "", " ", "[", "]", "{", "}", "[1,]", "[,1]", "[1 2]", "[1,,2]", "{,}", "{\"a\"}", "{\"a\":}", "{\"a\" 1}",
    "{\"a\":1,}", "{\"a\":1 \"b\":2}", "{1:2}", "{\"a\":1]", "[1}", "[[1]", "[1]]", "1 2", "\"a\" \"b\"", "[]x",
    "{} {}", "null null", "[:]", "{\"a\"::1}", "{:1}", "[\"a\":1]", "{\"a\",1}", "[true false]", ",", ":", "[}",
    "{]", "{\"a\":[}", "[{]}", "{\"a\":{\"b\":1}", "[[[]]",
  };
  int refused = 0, n = (int)(sizeof BAD / sizeof *BAD);
  for (int i = 0; i < n; i++) {
    JsonError e;
    Json *d = json_parse(BAD[i], strlen(BAD[i]), &e);
    if (!d && e.what) refused++;
    else fprintf(stderr, "  accepted: %s\n", BAD[i]);
    json_free(d);
  }
  CHECK(refused == n);
  printf("documents refused: %d of %d\n", refused, n);
  JsonError e;
  CHECK(json_parse("[1, 2", 5, &e) == NULL && e.what && e.at == 5);
  CHECK(json_parse("{\"a\" 1}", 7, &e) == NULL && e.at == 5);
  CHECK(json_parse("[1] 2", 5, &e) == NULL && e.at == 4);
  CHECK(json_parse(NULL, 0, &e) == NULL && e.what);
  Json *d = json_parse("[1]", 3, &e);
  CHECK(d && e.what == NULL);
  json_free(d);
}

/* a text of n opening brackets, then n closing ones */
static char *nested(int n) {
  char *t = malloc((size_t)n * 2 + 1);
  memset(t, '[', (size_t)n);
  memset(t + n, ']', (size_t)n);
  t[2 * n] = 0;
  return t;
}

static void test_stress(void) {
  /* as deep as allowed, and one deeper */
  char *t = nested(JSON_MAX_DEPTH);
  Json *d = json_parse(t, strlen(t), NULL);
  int depth = 0;
  for (const Json *v = d; json_count(v) == 1 || (v && json_is(v, JSON_ARRAY)); v = json_at(v, 0)) depth++;
  CHECK(d && depth == JSON_MAX_DEPTH);
  json_free(d);
  free(t);
  t = nested(JSON_MAX_DEPTH + 1);
  JsonError e;
  CHECK(json_parse(t, strlen(t), &e) == NULL && e.what && strstr(e.what, "deep"));
  free(t);
  /* a million numbers */
  enum { N = 1000000 };
  t = malloc((size_t)N * 8 + 2);
  size_t at = 0;
  t[at++] = '[';
  for (int i = 0; i < N; i++) at += (size_t)sprintf(t + at, i ? ",%d" : "%d", i);
  t[at++] = ']';
  d = json_parse(t, at, NULL);
  CHECK(d && json_count(d) == N && json_int(json_at(d, N - 1), 0) == N - 1 && json_int(json_at(d, 123456), 0) == 123456);
  long long sum = 0;
  JSON_EACH(v, d) sum += json_int(v, 0);
  CHECK(sum == (long long)N * (N - 1) / 2);
  json_free(d);
  free(t);
  /* a 16 MB string */
  enum { L = 16 << 20 };
  t = malloc(L + 3);
  t[0] = '"';
  memset(t + 1, 'x', L);
  t[L + 1] = '"';
  d = json_parse(t, L + 2, NULL);
  CHECK(d && d->len == L && d->str[L] == 0 && d->str[L - 1] == 'x');
  json_free(d);
  free(t);
  /* 100,000 members, each an object with an array */
  enum { K = 100000 };
  t = malloc((size_t)K * 40 + 2);
  at = 0;
  t[at++] = '{';
  for (int i = 0; i < K; i++) at += (size_t)sprintf(t + at, "%s\"k%d\":{\"v\":[%d,true]}", i ? "," : "", i, i);
  t[at++] = '}';
  d = json_parse(t, at, NULL);
  CHECK(d && json_count(d) == K && json_int(json_at(json_get(json_get(d, "k99999"), "v"), 0), 0) == 99999);
  CHECK(json_is_true(json_at(json_get(json_at(d, 5), "v"), 1)) && !strcmp(json_at(d, 5)->name, "k5"));
  json_free(d);
  free(t);
  /* many small documents, parsed and freed: nothing may leak (ASan's leak check sees it) */
  for (int i = 0; i < 20000; i++) {
    char doc[96];
    int n = snprintf(doc, sizeof doc, "{\"a\":[%d,\"s%d\",{\"b\":null}],\"c\":%d.5}", i, i, i);
    d = json_parse(doc, (size_t)n, NULL);
    if (!d || json_int(json_at(json_get(d, "a"), 0), -1) != i) {
      CHECK(0);
      break;
    }
    json_free(d);
    /* and every refused prefix of it frees what it built */
    if (i < 200) json_free(json_parse(doc, (size_t)(i % n), NULL));
  }
}

/* ---- the writer ----------------------------------------------------------- */

static void test_writer(void) {
  JsonWriter w = { 0 };
  json_w_open_object(&w, NULL);
  json_w_string(&w, "key", "fullburn \"DMX10\"\n\\");
  json_w_number(&w, "a", 0.1);
  json_w_number(&w, "b", 142.99);
  json_w_number(&w, "c", -0.0);
  json_w_number(&w, "d", 1e300);
  json_w_number(&w, "e", 3);
  json_w_number(&w, "nan", NAN);
  json_w_open_array(&w, "list");
  json_w_number(&w, NULL, 1);
  json_w_string(&w, NULL, "\x01");
  json_w_open_object(&w, NULL);
  json_w_close(&w);
  json_w_close(&w);
  json_w_close(&w);
  char *text = json_w_finish(&w);
  const char *want = "{\"key\":\"fullburn \\\"DMX10\\\"\\u000a\\\\\",\"a\":0.1,\"b\":142.99,\"c\":-0,\"d\":1e+300,\"e\":3,"
                     "\"nan\":null,\"list\":[1,\"\\u0001\",{}]}";
  CHECK(text && !strcmp(text, want));
  if (text && strcmp(text, want)) fprintf(stderr, "  wrote %s\n", text);
  /* and it reads back to the same values */
  Json *d = text ? json_parse(text, strlen(text), NULL) : NULL;
  CHECK(d && !strcmp(json_str(json_get(d, "key")), "fullburn \"DMX10\"\n\\") && json_num(json_get(d, "b")) == 142.99);
  CHECK(json_is(json_get(d, "nan"), JSON_NULL) && json_count(json_get(d, "list")) == 3);
  json_free(d);
  free(text);
  /* every double reads back exactly */
  double v = 1;
  int exact = 0;
  for (int i = 0; i < 2000; i++) {
    v = v * 1.7 + 0.123456789 * i;
    if (v > 1e200) v = 1e-200 * i;
    JsonWriter n = { 0 };
    json_w_number(&n, NULL, (i & 1) ? -v : v);
    char *t = json_w_finish(&n);
    Json *r = t ? json_parse(t, strlen(t), NULL) : NULL;
    if (r && json_num(r) == ((i & 1) ? -v : v)) exact++;
    json_free(r);
    free(t);
  }
  CHECK(exact == 2000);
  /* misuse fails instead of writing broken JSON */
  JsonWriter bad = { 0 };
  json_w_open_array(&bad, NULL);
  json_w_number(&bad, "named", 1); /* a name inside an array */
  json_w_close(&bad);
  CHECK(json_w_finish(&bad) == NULL);
  JsonWriter open = { 0 };
  json_w_open_object(&open, NULL);
  CHECK(json_w_finish(&open) == NULL); /* still open */
  JsonWriter two = { 0 };
  json_w_number(&two, NULL, 1);
  json_w_number(&two, NULL, 2); /* two values at the top */
  CHECK(json_w_finish(&two) == NULL);
}

int main(void) {
  test_rfc_examples();
  test_tokens();
  test_numbers();
  test_strings();
  test_malformed();
  test_errors();
  test_tree();
  test_tree_refuses();
  test_stress();
  test_writer();
  if (failures) {
    fprintf(stderr, "%d of %d json checks failed\n", failures, checks);
    return 1;
  }
  printf("json tests passed (%d checks)\n", checks);
  return 0;
}
