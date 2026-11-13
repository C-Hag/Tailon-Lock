/* tlpk.c - the floppy forge's packer (TLPK): LZ77 with an optimal parse over the coding model of
 * src/ff/tlpk_model.h, which the game's unpacker (src/ff/tlpk.c) reads with the very same code. Original code for
 * this project; C99 and the standard library.
 *
 *   tlpk c [-f none|x86|arm64|delta:N|auto] [-d DEPTH] IN OUT   pack (auto: every filter, the smallest kept)
 *   tlpk d IN OUT                                               unpack
 *   tlpk t FILE...                                              pack, unpack, compare; one line each
 *   tlpk fuzz N [SEED]                                          N made-up buffers round trip with every filter, and
 *                                                               damaged packed files must fail cleanly
 *
 * The parse: a hash chain finds, at each position, the matches of every length that the longest-so-far search
 * gives (each longer one further back); within a block of 4096 positions a shortest-path search over the
 * model's prices (in 16ths of a bit, from its probabilities as they stand at the block's start; literals priced by
 * an order-1 count) picks literals, matches of every length up to the longest, and repeats of the last distance;
 * the chosen tokens are then coded with the adaptive model itself. A match of 128 bytes or more is taken at once. */
#define TLPK_ENCODER
#include "ff/tlpk_model.h"

#include <stdio.h>

#include "ff/tlpk.h"

#define BLOCK 4096
#define NICE  128
#define MAXC  24 /* match candidates kept per position */
#define HBITS 20

static uint16_t price1_t[4097]; /* price of a 1 at a chance of p/4096, in 16ths of a bit */
static void prices_init(void) {
  for (int p = 1; p <= 4096; p++) {
    double v = -16.0 * __builtin_log2((double)p / 4096.0);
    price1_t[p] = (uint16_t)(v + 0.5);
  }
  price1_t[0] = price1_t[1];
}
static unsigned price_bit(Prob p, int bit) {
  int q = p12(p);
  return price1_t[bit ? q : 4096 - q];
}
static unsigned price_tree(const Prob *t, int nbits, int sym) {
  unsigned c = 0;
  int m = 1;
  for (int i = nbits - 1; i >= 0; i--) {
    int b = (sym >> i) & 1;
    c += price_bit(t[m], b);
    m = m << 1 | b;
  }
  return c;
}
static unsigned price_tree_rev(const Prob *t, int nbits, int sym) {
  unsigned c = 0;
  int m = 1;
  for (int i = 0; i < nbits; i++) {
    int b = (sym >> i) & 1;
    c += price_bit(t[m], b);
    m = m << 1 | b;
  }
  return c;
}
static unsigned price_len(const LenModel *m, int ps, int len) {
  int v = len - TLPK_MIN_MATCH;
  if (v < 8) return price_bit(m->choice, 0) + price_tree(m->low[ps], 3, v);
  if (v < 16) return price_bit(m->choice, 1) + price_bit(m->choice2, 0) + price_tree(m->mid[ps], 3, v - 8);
  return price_bit(m->choice, 1) + price_bit(m->choice2, 1) + price_tree(m->high, 8, v - 16);
}

typedef struct {
  const uint8_t *b;
  uint32_t n;
  int32_t *head, *prev;
  int depth;
} Finder;

static uint32_t hash3(const uint8_t *p) { return ((uint32_t)(p[0] << 16 | p[1] << 8 | p[2]) * 2654435761u) >> (32 - HBITS); }

static void insert(Finder *f, uint32_t pos) {
  if (pos + 3 > f->n) return;
  uint32_t h = hash3(f->b + pos);
  f->prev[pos] = f->head[h];
  f->head[h] = (int32_t)pos;
}

/* the matches at pos of strictly growing length (and distance); returns how many */
static int find(Finder *f, uint32_t pos, uint16_t *lens, uint32_t *dists) {
  if (pos + 3 > f->n) return 0;
  uint32_t maxl = f->n - pos < TLPK_MAX_MATCH ? f->n - pos : TLPK_MAX_MATCH;
  int32_t c = f->head[hash3(f->b + pos)];
  uint32_t best = 2;
  int k = 0;
  for (int d = 0; c >= 0 && d < f->depth && k < MAXC; d++, c = f->prev[c]) {
    const uint8_t *a = f->b + c, *p = f->b + pos;
    if (a[best] != p[best] || a[0] != p[0]) continue;
    uint32_t l = 0;
    while (l < maxl && a[l] == p[l]) l++;
    if (l > best) {
      best = l;
      lens[k] = (uint16_t)l;
      dists[k++] = pos - (uint32_t)c;
      if (l == maxl) break;
    }
  }
  return k;
}

typedef struct {
  uint32_t cost;
  int from, len, state;   /* len 1 with kind K_LIT: a literal */
  int kind, k;            /* the token: K_LIT, K_MATCH, K_REP (repeat k), K_SHORT */
  uint32_t dist;          /* the distance it copies from (0 for a literal) */
  uint32_t rep[4];        /* the last four distances after it */
} Node;

typedef struct {
  Model m;
  Rc r;
  uint16_t f1[256][256];
  uint32_t t1[256];
  unsigned slotp[4][64];
} Enc;

static unsigned price_lit(const Enc *e, int prev, int byte) {
  uint32_t q = ((uint32_t)e->f1[prev][byte] * 2 + 1) * 4096 / (e->t1[prev] * 2 + 256);
  q = q < 1 ? 1 : q > 4095 ? 4095 : q;
  return price1_t[q];
}
static void count_lit(Enc *e, int prev, int byte) {
  e->f1[prev][byte]++;
  if (++e->t1[prev] > 30000) {
    e->t1[prev] = 0;
    for (int i = 0; i < 256; i++) e->t1[prev] += e->f1[prev][i] = (uint16_t)((e->f1[prev][i] + 1) / 2);
  }
}
static unsigned price_dist(const Enc *e, int len, uint32_t dist) {
  uint32_t dv = dist - 1;
  int ls = len - TLPK_MIN_MATCH < 3 ? len - TLPK_MIN_MATCH : 3, slot = dist_slot(dv);
  unsigned c = e->slotp[ls][slot];
  if (slot < 4) return c;
  int nb = (slot >> 1) - 1;
  uint32_t base = (uint32_t)(2 | (slot & 1)) << nb, extra = dv - base;
  if (slot < 14) return c + price_tree_rev(e->m.dist.spec[slot], nb, (int)extra);
  return c + (unsigned)(nb - 4) * 16 + price_tree_rev(e->m.dist.align, 4, (int)(extra & 15));
}

static void emit(Enc *e, const uint8_t *b, uint32_t pos, const Node *t) {
  Model *m = &e->m;
  int ps = pos & 3;
  if (t->kind == K_LIT) {
    bit_ad(&e->r, &m->is_match[m->state][ps], 0);
    int mb = pos >= m->rep[0] ? b[pos - m->rep[0]] : -1, prev1 = pos ? b[pos - 1] : 0;
    lit_code(&e->r, &m->lit, prev1, pos > 1 ? b[pos - 2] : 0, mb, m->mrun, b[pos]);
    m->mrun = mb == b[pos] ? m->mrun + 1 : 0;
    count_lit(e, prev1, b[pos]);
    m->state = next_state(m->state, K_LIT);
    return;
  }
  bit_ad(&e->r, &m->is_match[m->state][ps], 1);
  int kind = t->kind, k = t->k;
  uint32_t len = (uint32_t)t->len, dist = t->dist;
  token_code(&e->r, m, ps, &kind, &k, &len, &dist);
  m->mrun = 0;
  m->state = next_state(m->state, kind);
}

/* the price of choosing repeat k (0..3) after "match?" was 1 and "repeat?" was 1 */
static unsigned price_rep_pick(const Model *m, int st, int ps, int k) {
  if (k == 0) return price_bit(m->is_rep0[st], 0) + price_bit(m->is_rep0_long[st][ps], 1);
  unsigned c = price_bit(m->is_rep0[st], 1);
  if (k == 1) return c + price_bit(m->is_rep1[st], 0);
  return c + price_bit(m->is_rep1[st], 1) + price_bit(m->is_rep2[st], k == 3);
}

static uint8_t *pack(const uint8_t *src, uint32_t n, int kind, int param, int depth, size_t *outn) {
  uint8_t *b = malloc((size_t)n + 1);
  memcpy(b, src, n);
  filter(b, n, kind, param, 1);
  int o2 = o2_bits_for(n);
  Enc *e = calloc(1, sizeof *e);
  if (!model_init(&e->m, o2)) {
    fprintf(stderr, "tlpk: out of memory\n");
    exit(1);
  }
  rc_init(&e->r);
  for (int i = 0; i < TLPK_HEADER; i++) rc_put(&e->r, 0); /* the header, filled in below */
  e->r.cache_size = 1;
  Finder f = { b, n, malloc(sizeof(int32_t) << HBITS), malloc(sizeof(int32_t) * ((size_t)n + 1)), depth };
  memset(f.head, 0xFF, sizeof(int32_t) << HBITS);
  Node *node = malloc(sizeof(Node) * (BLOCK + 1));
  uint16_t *cl = malloc(sizeof(uint16_t) * BLOCK * MAXC);
  uint32_t *cd = malloc(sizeof(uint32_t) * BLOCK * MAXC);
  int *nc = malloc(sizeof(int) * BLOCK);
  Node *path = malloc(sizeof(Node) * (BLOCK + 1));
  uint32_t pos = 0;
  while (pos < n) {
    uint32_t end = n - pos < BLOCK ? n : pos + BLOCK, len = end - pos;
    /* the matches of every position in the block (a long one: none inside it) */
    for (uint32_t i = 0; i < len; i++) {
      uint32_t p = pos + i;
      nc[i] = find(&f, p, cl + (size_t)i * MAXC, cd + (size_t)i * MAXC);
      insert(&f, p);
      if (nc[i] && cl[(size_t)i * MAXC + nc[i] - 1] >= NICE) {
        uint32_t l = cl[(size_t)i * MAXC + nc[i] - 1];
        for (uint32_t j = 1; j < l && i + j < len; j++) {
          nc[i + j] = 0;
          insert(&f, p + j);
        }
        i += l - 1;
      }
    }
    /* the block's prices as the model stands now */
    for (int ls = 0; ls < 4; ls++)
      for (int s = 0; s < 64; s++) e->slotp[ls][s] = price_tree(e->m.dist.slot[ls], 6, s);
    for (uint32_t i = 0; i <= len; i++) node[i].cost = UINT32_MAX;
    node[0] = (Node){ 0, -1, 0, e->m.state, K_LIT, 0, 0, { e->m.rep[0], e->m.rep[1], e->m.rep[2], e->m.rep[3] } };
    for (uint32_t i = 0; i < len; i++) {
      const Node *cur = &node[i];
      if (cur->cost == UINT32_MAX) continue;
      uint32_t p = pos + i;
      int ps = p & 3, st = cur->state;
      unsigned base = cur->cost;
      Node t;
      /* a literal */
      unsigned c = base + price_bit(e->m.is_match[st][ps], 0) + price_lit(e, p ? b[p - 1] : 0, b[p]);
      if (c < node[i + 1].cost) {
        t = *cur;
        t.cost = c, t.from = (int)i, t.len = 1, t.state = next_state(st, K_LIT), t.kind = K_LIT, t.k = 0, t.dist = 0;
        node[i + 1] = t;
      }
      unsigned mbit = base + price_bit(e->m.is_match[st][ps], 1), rbit = mbit + price_bit(e->m.is_rep[st], 1);
      uint32_t room = len - i < TLPK_MAX_MATCH ? len - i : TLPK_MAX_MATCH;
      /* one byte at the last distance */
      if (cur->rep[0] <= p && b[p] == b[p - cur->rep[0]]) {
        unsigned cc = rbit + price_bit(e->m.is_rep0[st], 0) + price_bit(e->m.is_rep0_long[st][ps], 0);
        if (cc < node[i + 1].cost) {
          t = *cur;
          t.cost = cc, t.from = (int)i, t.len = 1, t.state = next_state(st, K_SHORT), t.kind = K_SHORT, t.k = 0;
          t.dist = cur->rep[0];
          node[i + 1] = t;
        }
      }
      /* a repeat of one of the last four distances */
      for (int k = 0; k < 4; k++) {
        uint32_t d = cur->rep[k];
        if (d > p || (k && d == cur->rep[0])) continue;
        uint32_t l = 0;
        while (l < room && b[p + l] == b[p + l - d]) l++;
        if (l < TLPK_MIN_MATCH) continue;
        unsigned rb = rbit + price_rep_pick(&e->m, st, ps, k);
        uint32_t r2[4] = { cur->rep[0], cur->rep[1], cur->rep[2], cur->rep[3] };
        rep_use(r2, k);
        for (uint32_t q = TLPK_MIN_MATCH; q <= l; q++) {
          unsigned cc = rb + price_len(&e->m.rep_len, ps, (int)q);
          if (cc < node[i + q].cost)
            node[i + q] = (Node){ cc, (int)i, (int)q, next_state(st, K_REP), K_REP, k, d, { r2[0], r2[1], r2[2], r2[3] } };
        }
      }
      /* the matches, each length once, at the nearest distance that has it */
      unsigned nb = mbit + price_bit(e->m.is_rep[st], 0);
      uint32_t from = TLPK_MIN_MATCH + 1;
      for (int k = 0; k < nc[i]; k++) {
        uint32_t L = cl[(size_t)i * MAXC + k], d = cd[(size_t)i * MAXC + k];
        if (L > room) L = room;
        for (uint32_t l = from; l <= L; l++) {
          unsigned cc = nb + price_len(&e->m.len, ps, (int)l) + price_dist(e, (int)l, d);
          if (cc < node[i + l].cost)
            node[i + l] = (Node){ cc, (int)i, (int)l, next_state(st, K_MATCH), K_MATCH, 0, d,
                                  { d, cur->rep[0], cur->rep[1], cur->rep[2] } };
        }
        if (L + 1 > from) from = L + 1;
      }
    }
    /* back from the block's end: the tokens, then coded in order */
    int np = 0;
    for (int i = (int)len; i > 0; i = node[i].from) path[np++] = node[i];
    for (int k = np - 1; k >= 0; k--) {
      emit(e, b, pos, &path[k]);
      pos += (uint32_t)path[k].len;
    }
  }
  rc_flush(&e->r);
  uint8_t *out = e->r.out;
  size_t total = e->r.n;
  memcpy(out, "TLPK", 4);
  out[4] = TLPK_VERSION, out[5] = (uint8_t)kind, out[6] = (uint8_t)param, out[7] = (uint8_t)o2;
  wr32(out + 8, n);
  wr32(out + 12, crc32_of(src, n));
  *outn = total;
  lit_free(&e->m.lit);
  free(e), free(b), free(f.head), free(f.prev), free(node), free(cl), free(cd), free(nc), free(path);
  return out;
}

/* ---- the command line ---- */

static uint8_t *load(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)len + 1);
  if (b && fread(b, 1, (size_t)len, f) != (size_t)len) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *n = (size_t)len;
  return b;
}

static bool save(const char *path, const uint8_t *b, size_t n) {
  FILE *f = fopen(path, "wb");
  bool ok = f && fwrite(b, 1, n, f) == n;
  if (f) fclose(f);
  return ok;
}

static const struct { const char *name; int kind, param; } FILTERS[] = {
  { "none", TLPK_F_NONE, 0 }, { "x86", TLPK_F_X86, 0 }, { "arm64", TLPK_F_ARM64, 0 },
  { "delta:1", TLPK_F_DELTA, 1 }, { "delta:2", TLPK_F_DELTA, 2 }, { "delta:4", TLPK_F_DELTA, 4 },
};
#define NFILTERS (int)(sizeof FILTERS / sizeof FILTERS[0])

/* pack with one filter, or with each (-1) keeping the smallest */
static uint8_t *pack_best(const uint8_t *b, size_t n, int which, int depth, size_t *outn, int *used) {
  uint8_t *best = NULL;
  for (int k = 0; k < NFILTERS; k++) {
    if (which >= 0 && k != which) continue;
    size_t on;
    uint8_t *o = pack(b, (uint32_t)n, FILTERS[k].kind, FILTERS[k].param, depth, &on);
    if (!best || on < *outn) {
      free(best);
      best = o, *outn = on, *used = k;
    } else {
      free(o);
    }
  }
  return best;
}

static int filter_named(const char *s) {
  if (!strcmp(s, "auto")) return -1;
  for (int k = 0; k < NFILTERS; k++)
    if (!strcmp(s, FILTERS[k].name)) return k;
  return -2;
}

static uint32_t rnd_state = 1;
static uint32_t rnd(void) {
  rnd_state ^= rnd_state << 13, rnd_state ^= rnd_state >> 17, rnd_state ^= rnd_state << 5;
  return rnd_state;
}

/* a made-up buffer of one of several kinds: random, constant, short repeats, text-like, code-like, mixed */
static uint8_t *made_up(size_t *n) {
  static const size_t sizes[] = { 0, 1, 2, 3, 4, 5, 7, 16, 100, 273, 274, 1000, 4095, 4096, 4097, 10000, 70000, 200000 };
  *n = sizes[rnd() % (sizeof sizes / sizeof sizes[0])];
  uint8_t *b = malloc(*n + 1);
  int kind = (int)(rnd() % 6);
  for (size_t i = 0; i < *n; i++) {
    switch (kind) {
    case 0: b[i] = (uint8_t)rnd(); break;
    case 1: b[i] = 0x55; break;
    case 2: b[i] = i >= 7 && rnd() % 16 ? b[i - 1 - rnd() % 7] : (uint8_t)rnd(); break;
    case 3: b[i] = (uint8_t)" etaoinshrdlu\n,.{}\"0123"[rnd() % 23]; break;
    case 4: b[i] = (i & 3) == 3 ? (uint8_t)(rnd() % 2 ? 0x94 : 0x90 + (rnd() & 0x60)) : (i & 3) == 0 && rnd() % 3 == 0 ? 0xE8 : (uint8_t)rnd(); break;
    default: b[i] = i > 300 && rnd() % 4 ? b[i - 256 - rnd() % 40] : (uint8_t)(rnd() % 8); break;
    }
  }
  return b;
}

int main(int argc, char **argv) {
  prices_init();
  stretch_init();
  int which = 0, depth = 128, a = 2;
  if (argc >= 2 && !strcmp(argv[1], "fuzz")) {
    long iters = argc > 2 ? strtol(argv[2], NULL, 10) : 200;
    rnd_state = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) | 1 : 12345;
    long bad = 0, damaged = 0, caught = 0;
    for (long it = 0; it < iters; it++) {
      size_t n;
      uint8_t *b = made_up(&n);
      for (int k = 0; k < NFILTERS; k++) {
        size_t pn, un = 0;
        uint8_t *p = pack(b, (uint32_t)n, FILTERS[k].kind, FILTERS[k].param, 32, &pn);
        uint8_t *u = tlpk_unpack(p, pn, 1u << 30, &un);
        if (!u || un != n || memcmp(u, b, n)) {
          bad++;
          fprintf(stderr, "fuzz: round trip failed: iteration %ld, %zu bytes, filter %s\n", it, n, FILTERS[k].name);
        }
        free(u);
        if (pn > TLPK_HEADER + 2) { /* damaged: one byte changed, or cut short; it must fail, not crash */
          for (int t = 0; t < 3; t++) {
            uint8_t *q = malloc(pn);
            memcpy(q, p, pn);
            size_t qn = pn;
            if (t == 2) qn = TLPK_HEADER + rnd() % (pn - TLPK_HEADER);
            else q[TLPK_HEADER + rnd() % (pn - TLPK_HEADER)] ^= (uint8_t)(1 + rnd() % 255);
            size_t dn;
            uint8_t *d = tlpk_unpack(q, qn, 1u << 30, &dn);
            damaged++;
            caught += d == NULL || (dn == n && !memcmp(d, b, n));
            free(d), free(q);
          }
        }
        free(p);
      }
      free(b);
    }
    printf("fuzz: %ld buffers x %d filters, %ld round trips failed; %ld damaged files, %ld failed cleanly or came "
           "back right\n", iters, NFILTERS, bad, damaged, caught);
    return bad || caught != damaged ? 1 : 0;
  }
  while (a < argc && argv[a][0] == '-') {
    if (!strcmp(argv[a], "-f") && a + 1 < argc) which = filter_named(argv[++a]);
    else if (!strcmp(argv[a], "-d") && a + 1 < argc) depth = atoi(argv[++a]);
    a++;
  }
  if (which < -1) return fprintf(stderr, "tlpk: unknown filter\n"), 2;
  if (argc >= 2 && !strcmp(argv[1], "t")) {
    int fails = 0;
    for (int i = a; i < argc; i++) {
      size_t n, pn, un = 0;
      int used = 0;
      uint8_t *b = load(argv[i], &n);
      if (!b) {
        fprintf(stderr, "tlpk: cannot read %s\n", argv[i]);
        fails++;
        continue;
      }
      uint8_t *p = pack_best(b, n, which, depth, &pn, &used);
      uint8_t *u = tlpk_unpack(p, pn, 1u << 30, &un);
      bool ok = u && un == n && !memcmp(u, b, n);
      fails += !ok;
      printf("%s %zu %zu %s %s\n", ok ? "ok" : "FAIL", n, pn, FILTERS[used].name, argv[i]);
      free(b), free(p), free(u);
    }
    return fails ? 1 : 0;
  }
  if (argc - a != 2 || argc < 2) {
    fprintf(stderr, "usage: tlpk c [-f none|x86|arm64|delta:N|auto] [-d DEPTH] IN OUT | d IN OUT | t FILE... | fuzz N [SEED]\n");
    return 2;
  }
  size_t n;
  uint8_t *b = load(argv[a], &n);
  if (!b) return fprintf(stderr, "tlpk: cannot read %s\n", argv[a]), 2;
  if (!strcmp(argv[1], "d")) {
    size_t un;
    uint8_t *u = tlpk_unpack(b, n, 1u << 30, &un);
    if (!u) return fprintf(stderr, "tlpk: %s is not a sound TLPK file\n", argv[a]), 1;
    return save(argv[a + 1], u, un) ? 0 : 2;
  }
  size_t pn;
  int used = 0;
  uint8_t *p = pack_best(b, n, which, depth, &pn, &used);
  printf("%zu -> %zu (%s)\n", n, pn, FILTERS[used].name);
  return save(argv[a + 1], p, pn) ? 0 : 2;
}
