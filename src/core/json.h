/* json.h - our own JSON reader. Original code, written from RFC 8259 (the
 * JSON data interchange format) and RFC 3629 (UTF-8); no other JSON
 * implementation was looked at.
 *
 * Brick 1, the tokenizer: it cuts a text into the tokens of RFC 8259
 * section 2 (the six structural characters, the three literal names,
 * numbers, strings) and checks each one against the grammar of sections
 * 3, 6 and 7 and the UTF-8 rules of RFC 3629. Whether the tokens make a
 * valid document (the order of values, commas and colons) is the tree
 * builder's job.
 *
 * Brick 2, the tree: json_parse builds the whole document in one arena (a
 * few large blocks, freed together by json_free). A container's members sit
 * side by side in one array, so indexing is O(1) and walking them is a
 * pointer step. Every getter takes NULL and wrong types quietly, so a lookup
 * chain needs no checks in between: json_number(json_get(json_get(doc, "a"),
 * "b"), 0) is 0 when anything on the way is missing. */
#ifndef A4C_JSON_H
#define A4C_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  JTOK_END,      /* the end of the text */
  JTOK_ERROR,    /* not a token; see JsonLexer.err */
  JTOK_OBJ_OPEN, /* { */
  JTOK_OBJ_CLOSE,
  JTOK_ARR_OPEN, /* [ */
  JTOK_ARR_CLOSE,
  JTOK_COLON,
  JTOK_COMMA,
  JTOK_STRING, /* span: between the quotes, escapes still in it */
  JTOK_NUMBER, /* span: the number's characters; num: its value */
  JTOK_TRUE,
  JTOK_FALSE,
  JTOK_NULL
} JsonTokKind;

typedef struct {
  JsonTokKind kind;
  size_t      at, len;   /* where the token's text is: the whole token, or a string's inside */
  size_t      out_len;   /* a string: its length once unescaped (bytes of UTF-8) */
  double      num;       /* a number: its value, the nearest double */
} JsonToken;

typedef struct {
  const char *text;
  size_t      len, pos;
  const char *err;       /* why the last JTOK_ERROR, or NULL */
  size_t      err_at;    /* the byte the error was found at */
} JsonLexer;

/* Starts reading text[0..len). A UTF-8 byte order mark at the start is
 * skipped (RFC 8259 section 8.1 lets a reader ignore it). */
void json_lex_init(JsonLexer *lx, const char *text, size_t len);
/* The next token. After JTOK_END or JTOK_ERROR it keeps returning the same. */
JsonTokKind json_lex_next(JsonLexer *lx, JsonToken *tok);
/* Writes a JTOK_STRING token's unescaped bytes into out, which holds at
 * least tok->out_len bytes (no terminating zero is written). */
void json_lex_string(const JsonLexer *lx, const JsonToken *tok, char *out);

/* ---- the tree ------------------------------------------------------------ */

typedef enum { JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;

typedef struct Json Json;
struct Json {
  JsonType    type;
  int         count; /* an array's or object's members */
  const char *name;  /* a member of an object: its name (zero-terminated); otherwise NULL */
  size_t      name_len;
  double      num;   /* JSON_NUMBER */
  const char *str;   /* JSON_STRING: zero-terminated (it may hold zero bytes; len counts them) */
  size_t      len;
  Json       *items; /* JSON_ARRAY, JSON_OBJECT: count members, in document order */
};

#define JSON_MAX_DEPTH 1000 /* nesting deeper than this is refused */

typedef struct {
  const char *what;  /* NULL when the parse succeeded */
  size_t      at;    /* the byte it went wrong at */
} JsonError;

/* Parses text[0..len): one value, with only whitespace around it. Returns
 * the root, or NULL (and why, in *err if it is given). */
Json *json_parse(const char *text, size_t len, JsonError *err);
/* Frees a tree from json_parse; NULL is fine. */
void json_free(Json *root);

/* An object's first member called name, or NULL. */
const Json *json_get(const Json *obj, const char *name);
/* The i'th member of an array or object, or NULL. */
const Json *json_at(const Json *v, int i);
/* Members of an array or object; 0 for anything else. */
int json_count(const Json *v);
bool json_is(const Json *v, JsonType t);
bool json_is_true(const Json *v);
/* A number's value, or fallback. */
double json_number(const Json *v, double fallback);
/* A number as an int: toward zero, saturating at INT_MIN and INT_MAX (NaN gives 0); fallback if not a number. */
int json_int(const Json *v, int fallback);
/* A string's text, or fallback. */
const char *json_string(const Json *v, const char *fallback);

/* Walks the members of an array or object: for each const Json *e. */
#define JSON_EACH(e, parent) \
  for (const Json *e = json_begin(parent), *e##_end_ = json_end(parent); e != e##_end_; e++)
const Json *json_begin(const Json *v);
const Json *json_end(const Json *v);

#endif
