/* json.c - our own JSON reader. Original code, written from RFC 8259 (the
 * JSON data interchange format) and RFC 3629 (UTF-8); no other JSON
 * implementation was looked at. See json.h. */
#include "core/json.h"

#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- the tokenizer -------------------------------------------------------- */

void json_lex_init(JsonLexer *lx, const char *text, size_t len) {
  lx->text = text;
  lx->len = text ? len : 0;
  lx->pos = 0;
  lx->err = NULL;
  lx->err_at = 0;
  if (lx->len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
    lx->pos = 3;
}

static JsonTokKind fail(JsonLexer *lx, JsonToken *tok, size_t at, const char *why) {
  lx->err = why;
  lx->err_at = at;
  lx->pos = lx->len + 1; /* stuck: every later call fails too */
  tok->kind = JTOK_ERROR;
  tok->at = at;
  tok->len = 0;
  return JTOK_ERROR;
}

static int hexval(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* four hex digits at s[0..3] (within the text), or -1 */
static long hex4(const unsigned char *s, size_t avail) {
  if (avail < 4) return -1;
  long v = 0;
  for (int i = 0; i < 4; i++) {
    int h = hexval(s[i]);
    if (h < 0) return -1;
    v = v * 16 + h;
  }
  return v;
}

/* the length of the UTF-8 sequence at s (RFC 3629 section 4), or 0 if it is not a valid one */
static size_t utf8_seq(const unsigned char *s, size_t avail) {
  unsigned char c = s[0];
  size_t n;
  unsigned char lo = 0x80, hi = 0xBF; /* the range of the second byte */
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) n = 2;
  else if (c >= 0xE0 && c <= 0xEF) {
    n = 3;
    if (c == 0xE0) lo = 0xA0;      /* no overlong forms */
    else if (c == 0xED) hi = 0x9F; /* no UTF-16 surrogates */
  } else if (c >= 0xF0 && c <= 0xF4) {
    n = 4;
    if (c == 0xF0) lo = 0x90;      /* no overlong forms */
    else if (c == 0xF4) hi = 0x8F; /* nothing above U+10FFFF */
  } else return 0;
  if (avail < n) return 0;
  if (s[1] < lo || s[1] > hi) return 0;
  for (size_t i = 2; i < n; i++)
    if (s[i] < 0x80 || s[i] > 0xBF) return 0;
  return n;
}

static size_t utf8_len(unsigned long cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }

static size_t utf8_put(unsigned long cp, char *out) {
  unsigned char *o = (unsigned char *)out;
  if (cp < 0x80) {
    o[0] = (unsigned char)cp;
    return 1;
  }
  if (cp < 0x800) {
    o[0] = (unsigned char)(0xC0 | cp >> 6);
    o[1] = (unsigned char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    o[0] = (unsigned char)(0xE0 | cp >> 12);
    o[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
    o[2] = (unsigned char)(0x80 | (cp & 0x3F));
    return 3;
  }
  o[0] = (unsigned char)(0xF0 | cp >> 18);
  o[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3F));
  o[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
  o[3] = (unsigned char)(0x80 | (cp & 0x3F));
  return 4;
}

/* An escape at s (just after the backslash): how many bytes of text it takes,
 * its code point in *cp; 0 if it is not a valid one. \uD800-\uDBFF must be
 * followed by \uDC00-\uDFFF (a UTF-16 pair, RFC 8259 section 7); a lone
 * surrogate is refused, since it is no character and has no UTF-8 form. */
static size_t escape(const unsigned char *s, size_t avail, unsigned long *cp) {
  if (avail < 1) return 0;
  switch (s[0]) {
  case '"': *cp = '"'; return 1;
  case '\\': *cp = '\\'; return 1;
  case '/': *cp = '/'; return 1;
  case 'b': *cp = 0x08; return 1;
  case 'f': *cp = 0x0C; return 1;
  case 'n': *cp = 0x0A; return 1;
  case 'r': *cp = 0x0D; return 1;
  case 't': *cp = 0x09; return 1;
  case 'u': break;
  default: return 0;
  }
  long u = hex4(s + 1, avail - 1);
  if (u < 0 || (u >= 0xDC00 && u <= 0xDFFF)) return 0;
  if (u < 0xD800 || u > 0xDBFF) {
    *cp = (unsigned long)u;
    return 5;
  }
  if (avail < 11 || s[5] != '\\' || s[6] != 'u') return 0;
  long w = hex4(s + 7, avail - 7);
  if (w < 0xDC00 || w > 0xDFFF) return 0;
  *cp = 0x10000 + ((unsigned long)(u - 0xD800) << 10) + (unsigned long)(w - 0xDC00);
  return 11;
}

static JsonTokKind lex_string(JsonLexer *lx, JsonToken *tok) {
  const unsigned char *t = (const unsigned char *)lx->text;
  size_t start = lx->pos + 1, i = start, out = 0;
  for (;;) {
    if (i >= lx->len) return fail(lx, tok, lx->pos, "unterminated string");
    unsigned char c = t[i];
    if (c == '"') break;
    if (c == '\\') {
      unsigned long cp;
      size_t n = escape(t + i + 1, lx->len - i - 1, &cp);
      if (!n) return fail(lx, tok, i, "bad escape in a string");
      out += utf8_len(cp);
      i += 1 + n;
    } else if (c < 0x20) {
      return fail(lx, tok, i, "control character in a string");
    } else {
      size_t n = utf8_seq(t + i, lx->len - i);
      if (!n) return fail(lx, tok, i, "invalid UTF-8 in a string");
      out += n;
      i += n;
    }
  }
  tok->kind = JTOK_STRING;
  tok->at = start;
  tok->len = i - start;
  tok->out_len = out;
  lx->pos = i + 1;
  return JTOK_STRING;
}

/* a token must end at whitespace, a structural character, a quote or the end */
static bool ends_token(const JsonLexer *lx, size_t i) {
  if (i >= lx->len) return true;
  return strchr(" \t\n\r{}[]:,\"", lx->text[i]) != NULL && lx->text[i] != 0;
}

static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* the value of text[0..n), a number that matched the grammar: the C library's
 * conversion, with '.' swapped for the locale's decimal point if it differs */
static double number_value(const char *text, size_t n) {
  char small[64], *buf = n < sizeof small ? small : malloc(n + 1);
  if (!buf) return 0;
  memcpy(buf, text, n);
  buf[n] = 0;
  const char *dp = localeconv()->decimal_point;
  if (dp && dp[0] && dp[0] != '.' && !dp[1]) {
    char *p = strchr(buf, '.');
    if (p) *p = dp[0];
  }
  double v = strtod(buf, NULL);
  if (buf != small) free(buf);
  return v;
}

/* number = [ minus ] int [ frac ] [ exp ] (RFC 8259 section 6) */
static JsonTokKind lex_number(JsonLexer *lx, JsonToken *tok) {
  const unsigned char *t = (const unsigned char *)lx->text;
  size_t i = lx->pos, n = lx->len;
  if (t[i] == '-') i++;
  if (i < n && t[i] == '0') i++;
  else if (i < n && t[i] >= '1' && t[i] <= '9')
    while (i < n && is_digit(t[i])) i++;
  else return fail(lx, tok, i, "malformed number");
  if (i < n && t[i] == '.') {
    i++;
    if (i >= n || !is_digit(t[i])) return fail(lx, tok, i, "malformed number");
    while (i < n && is_digit(t[i])) i++;
  }
  if (i < n && (t[i] == 'e' || t[i] == 'E')) {
    i++;
    if (i < n && (t[i] == '+' || t[i] == '-')) i++;
    if (i >= n || !is_digit(t[i])) return fail(lx, tok, i, "malformed number");
    while (i < n && is_digit(t[i])) i++;
  }
  if (!ends_token(lx, i)) return fail(lx, tok, i, "malformed number");
  tok->kind = JTOK_NUMBER;
  tok->at = lx->pos;
  tok->len = i - lx->pos;
  tok->num = number_value(lx->text + lx->pos, tok->len);
  lx->pos = i;
  return JTOK_NUMBER;
}

static JsonTokKind lex_word(JsonLexer *lx, JsonToken *tok, const char *word, JsonTokKind kind) {
  size_t n = strlen(word);
  if (lx->len - lx->pos < n || memcmp(lx->text + lx->pos, word, n) != 0 || !ends_token(lx, lx->pos + n))
    return fail(lx, tok, lx->pos, "unknown word");
  tok->kind = kind;
  tok->at = lx->pos;
  tok->len = n;
  lx->pos += n;
  return kind;
}

JsonTokKind json_lex_next(JsonLexer *lx, JsonToken *tok) {
  if (lx->pos > lx->len) { /* stuck after an error */
    tok->kind = JTOK_ERROR;
    tok->at = lx->err_at;
    tok->len = 0;
    return JTOK_ERROR;
  }
  /* insignificant whitespace: space, tab, line feed, carriage return (section 2) */
  while (lx->pos < lx->len && strchr(" \t\n\r", lx->text[lx->pos]) && lx->text[lx->pos]) lx->pos++;
  if (lx->pos == lx->len) {
    tok->kind = JTOK_END;
    tok->at = lx->pos;
    tok->len = 0;
    return JTOK_END;
  }
  unsigned char c = (unsigned char)lx->text[lx->pos];
  JsonTokKind k;
  switch (c) {
  case '{': k = JTOK_OBJ_OPEN; break;
  case '}': k = JTOK_OBJ_CLOSE; break;
  case '[': k = JTOK_ARR_OPEN; break;
  case ']': k = JTOK_ARR_CLOSE; break;
  case ':': k = JTOK_COLON; break;
  case ',': k = JTOK_COMMA; break;
  case '"': return lex_string(lx, tok);
  case 't': return lex_word(lx, tok, "true", JTOK_TRUE);
  case 'f': return lex_word(lx, tok, "false", JTOK_FALSE);
  case 'n': return lex_word(lx, tok, "null", JTOK_NULL);
  default:
    if (c == '-' || is_digit(c)) return lex_number(lx, tok);
    return fail(lx, tok, lx->pos, "unexpected character");
  }
  tok->kind = k;
  tok->at = lx->pos++;
  tok->len = 1;
  return k;
}

void json_lex_string(const JsonLexer *lx, const JsonToken *tok, char *out) {
  const unsigned char *t = (const unsigned char *)lx->text;
  size_t i = tok->at, end = tok->at + tok->len;
  while (i < end) {
    if (t[i] == '\\') {
      unsigned long cp = 0;
      size_t n = escape(t + i + 1, end - i - 1, &cp);
      if (!n) return; /* not from json_lex_next */
      out += utf8_put(cp, out);
      i += 1 + n;
    } else *out++ = (char)t[i++];
  }
}

/* ---- the tree ------------------------------------------------------------- */

/* The arena: blocks of at least 64 KB, carved front to back, freed together. */
typedef struct Block {
  struct Block *next;
  size_t        used, cap;
  double        data[]; /* double: aligned for every member of Json */
} Block;

typedef struct {
  Block *head;
} Arena;

static void *arena_alloc(Arena *a, size_t n) {
  n = (n + sizeof(double) - 1) & ~(sizeof(double) - 1);
  Block *b = a->head;
  if (!b || b->cap - b->used < n) {
    size_t cap = n > 65536 ? n : 65536;
    b = malloc(sizeof *b + cap);
    if (!b) return NULL;
    b->next = a->head;
    b->used = 0;
    b->cap = cap;
    a->head = b;
  }
  void *p = (char *)b->data + b->used;
  b->used += n;
  return p;
}

static void arena_free(Arena *a) {
  for (Block *b = a->head, *next; b; b = next) {
    next = b->next;
    free(b);
  }
  a->head = NULL;
}

/* the root and its arena in one allocation: json_free finds the arena from the root */
typedef struct {
  Arena arena;
  Json  root;
} Doc;

void json_free(Json *root) {
  if (!root) return;
  Doc *d = (Doc *)((char *)root - offsetof(Doc, root));
  arena_free(&d->arena);
  free(d);
}

/* an open container while its members are read */
typedef struct {
  JsonType    type;
  size_t      base;     /* where its members start on the value stack */
  const char *name;     /* its own name, when it is an object's member */
  size_t      name_len;
  const char *key;      /* in an object: the name of the member being read */
  size_t      key_len;
} Frame;

enum { S_VALUE, S_VALUE_OR_CLOSE, S_KEY_OR_CLOSE, S_KEY, S_COLON, S_COMMA_OR_CLOSE, S_DONE };

typedef struct {
  Arena  arena;
  Json  *vals; /* finished values whose container is still open */
  size_t nvals, capvals;
  Frame *frames;
  int    depth;
} Parser;

static bool push_val(Parser *p, const Json *v) {
  if (p->nvals == p->capvals) {
    size_t cap = p->capvals ? p->capvals * 2 : 256;
    Json *nv = realloc(p->vals, cap * sizeof *nv);
    if (!nv) return false;
    p->vals = nv;
    p->capvals = cap;
  }
  p->vals[p->nvals++] = *v;
  return true;
}

static char *copy_string(Parser *p, const JsonLexer *lx, const JsonToken *t) {
  char *s = arena_alloc(&p->arena, t->out_len + 1);
  if (!s) return NULL;
  json_lex_string(lx, t, s);
  s[t->out_len] = 0;
  return s;
}

Json *json_parse(const char *text, size_t len, JsonError *err) {
  Parser p = { 0 };
  JsonError e = { NULL, 0 };
  Doc *doc = NULL;
  p.frames = malloc((JSON_MAX_DEPTH + 1) * sizeof *p.frames);
  JsonLexer lx;
  JsonToken t;
  json_lex_init(&lx, text, len);
  int state = S_VALUE;
  if (!p.frames) {
    e.what = "out of memory";
    goto fail;
  }
  for (;;) {
    JsonTokKind k = json_lex_next(&lx, &t);
    if (k == JTOK_ERROR) {
      e.what = lx.err;
      e.at = lx.err_at;
      goto fail;
    }
    Frame *top = p.depth ? &p.frames[p.depth - 1] : NULL;
    bool close = (k == JTOK_ARR_CLOSE && top && top->type == JSON_ARRAY && (state == S_VALUE_OR_CLOSE || state == S_COMMA_OR_CLOSE)) ||
                 (k == JTOK_OBJ_CLOSE && top && top->type == JSON_OBJECT && (state == S_KEY_OR_CLOSE || state == S_COMMA_OR_CLOSE));
    if (close) {
      Json c = { 0 };
      c.type = top->type;
      size_t n = p.nvals - top->base;
      if (n > 0x7FFFFFFF) {
        e.what = "too many members";
        e.at = t.at;
        goto fail;
      }
      c.count = (int)n;
      if (n) {
        c.items = arena_alloc(&p.arena, n * sizeof(Json));
        if (!c.items) {
          e.what = "out of memory";
          goto fail;
        }
        memcpy(c.items, p.vals + top->base, n * sizeof(Json));
      }
      c.name = top->name;
      c.name_len = top->name_len;
      p.nvals = top->base;
      p.depth--;
      if (!push_val(&p, &c)) {
        e.what = "out of memory";
        goto fail;
      }
      state = p.depth ? S_COMMA_OR_CLOSE : S_DONE;
      continue;
    }
    switch (state) {
    case S_VALUE:
    case S_VALUE_OR_CLOSE: {
      const char *name = top && top->type == JSON_OBJECT ? top->key : NULL;
      size_t name_len = top && top->type == JSON_OBJECT ? top->key_len : 0;
      if (k == JTOK_ARR_OPEN || k == JTOK_OBJ_OPEN) {
        if (p.depth >= JSON_MAX_DEPTH) {
          e.what = "nested too deep";
          e.at = t.at;
          goto fail;
        }
        Frame *f = &p.frames[p.depth++];
        f->type = k == JTOK_ARR_OPEN ? JSON_ARRAY : JSON_OBJECT;
        f->base = p.nvals;
        f->name = name;
        f->name_len = name_len;
        f->key = NULL;
        f->key_len = 0;
        state = k == JTOK_ARR_OPEN ? S_VALUE_OR_CLOSE : S_KEY_OR_CLOSE;
        continue;
      }
      Json v = { 0 };
      v.name = name;
      v.name_len = name_len;
      switch (k) {
      case JTOK_NULL: v.type = JSON_NULL; break;
      case JTOK_TRUE: v.type = JSON_TRUE; break;
      case JTOK_FALSE: v.type = JSON_FALSE; break;
      case JTOK_NUMBER:
        v.type = JSON_NUMBER;
        v.num = t.num;
        break;
      case JTOK_STRING:
        v.type = JSON_STRING;
        v.str = copy_string(&p, &lx, &t);
        v.len = t.out_len;
        if (!v.str) {
          e.what = "out of memory";
          goto fail;
        }
        break;
      default:
        e.what = k == JTOK_END ? "the text ends where a value should be" : "a value is missing";
        e.at = t.at;
        goto fail;
      }
      if (!push_val(&p, &v)) {
        e.what = "out of memory";
        goto fail;
      }
      state = p.depth ? S_COMMA_OR_CLOSE : S_DONE;
      continue;
    }
    case S_KEY_OR_CLOSE:
    case S_KEY:
      if (k != JTOK_STRING) {
        e.what = k == JTOK_END ? "the text ends inside an object" : "a member name is missing";
        e.at = t.at;
        goto fail;
      }
      top->key = copy_string(&p, &lx, &t);
      top->key_len = t.out_len;
      if (!top->key) {
        e.what = "out of memory";
        goto fail;
      }
      state = S_COLON;
      continue;
    case S_COLON:
      if (k != JTOK_COLON) {
        e.what = "a colon is missing after a member name";
        e.at = t.at;
        goto fail;
      }
      state = S_VALUE;
      continue;
    case S_COMMA_OR_CLOSE:
      if (k != JTOK_COMMA) {
        e.what = k == JTOK_END ? "the text ends inside an array or object" : "a comma or a closing bracket is missing";
        e.at = t.at;
        goto fail;
      }
      state = top->type == JSON_OBJECT ? S_KEY : S_VALUE;
      continue;
    default: /* S_DONE */
      if (k != JTOK_END) {
        e.what = "more text after the value";
        e.at = t.at;
        goto fail;
      }
      break;
    }
    break;
  }
  doc = malloc(sizeof *doc);
  if (!doc) {
    e.what = "out of memory";
    goto fail;
  }
  doc->arena = p.arena;
  doc->root = p.vals[0];
  free(p.vals);
  free(p.frames);
  if (err) *err = e;
  return &doc->root;
fail:
  arena_free(&p.arena);
  free(p.vals);
  free(p.frames);
  if (err) *err = e;
  return NULL;
}

/* ---- getters -------------------------------------------------------------- */

static bool container(const Json *v) { return v && (v->type == JSON_ARRAY || v->type == JSON_OBJECT); }

const Json *json_get(const Json *obj, const char *name) {
  if (!obj || obj->type != JSON_OBJECT || !name) return NULL;
  size_t n = strlen(name);
  for (int i = 0; i < obj->count; i++) {
    const Json *m = &obj->items[i];
    if (m->name_len == n && memcmp(m->name, name, n) == 0) return m;
  }
  return NULL;
}

const Json *json_at(const Json *v, int i) { return container(v) && i >= 0 && i < v->count ? &v->items[i] : NULL; }
int json_count(const Json *v) { return container(v) ? v->count : 0; }
bool json_is(const Json *v, JsonType t) { return v && v->type == t; }
bool json_is_true(const Json *v) { return v && v->type == JSON_TRUE; }
double json_number(const Json *v, double fallback) { return v && v->type == JSON_NUMBER ? v->num : fallback; }
const char *json_string(const Json *v, const char *fallback) { return v && v->type == JSON_STRING ? v->str : fallback; }
const Json *json_begin(const Json *v) { return container(v) ? v->items : NULL; }
const Json *json_end(const Json *v) { return container(v) && v->items ? v->items + v->count : NULL; }

int json_int(const Json *v, int fallback) {
  if (!v || v->type != JSON_NUMBER) return fallback;
  double d = v->num;
  if (d != d) return 0;
  if (d >= (double)INT_MAX) return INT_MAX;
  if (d <= (double)INT_MIN) return INT_MIN;
  return (int)d;
}
