/* test_relay_limits.c - the relay's limits on guessing game codes, on relay.c's own functions: every address
 * has its own count, even two whose hashes land in one slot, and a full neighbourhood gives up its oldest; a code
 * that many addresses get wrong goes quiet, and so does the whole relay after its join tries for the minute,
 * a live code as much as any other. Original code for this project. */
#define main relay_main
#include "../relay/relay.c"
#undef main

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_relay_limits:%d: %s\n", __LINE__, #c);     \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static Addr ip(uint32_t host_order) {
  Addr a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(host_order);
  return a;
}
static uint32_t slot_of(const Addr *a) { return (a->sin_addr.s_addr * 2654435761u) >> 20; }

/* n addresses after base whose hash is the same as base's */
static int colliding(Addr base, Addr *out, int n) {
  int got = 0;
  for (uint32_t k = 1; got < n && k < 50000000u; k++) {
    Addr b = ip(ntohl(base.sin_addr.s_addr) + k);
    if (slot_of(&b) == slot_of(&base)) out[got++] = b;
  }
  return got;
}

/* the same test main() makes for a wrong code */
static bool guess(const Addr *a, double t) {
  int *m = miss_slot(a, t);
  if (!m) return false;
  (*m)++;
  return true;
}

static void test_collide(void) {
  memset(misses, 0, sizeof misses);
  Addr A = ip(0xC6336401u), B[1]; /* 198.51.100.1 */
  CHECK(colliding(A, B, 1) == 1);
  int answered = 0;
  for (int i = 0; i < 50; i++) answered += guess(i % 2 ? &B[0] : &A, i);
  CHECK(answered == 2 * MISS_PER); /* 10 each, not one counter the two keep clearing */
  CHECK(miss_slot(&A, 50) == NULL && miss_slot(&B[0], 50) == NULL);
  Addr C = ip(0x0A000001u);
  CHECK(miss_slot(&C, 50) != NULL); /* an address that has not guessed still may */
}

static void test_full(void) {
  memset(misses, 0, sizeof misses);
  Addr A = ip(0xCB007101u), more[MISS_PROBE];
  CHECK(colliding(A, more, MISS_PROBE) == MISS_PROBE);
  for (int i = 0; i < MISS_PER; i++) guess(&A, 1);
  CHECK(miss_slot(&A, 2) == NULL);
  /* 15 more in the neighbourhood (with A, all 16 slots), each out of guesses too */
  for (int k = 0; k < MISS_PROBE - 1; k++)
    for (int i = 0; i < MISS_PER; i++) guess(&more[k], 10 + k);
  CHECK(miss_slot(&A, 30) == NULL);
  for (int k = 0; k < MISS_PROBE - 1; k++) CHECK(miss_slot(&more[k], 30) == NULL);
  /* one more: it takes the oldest slot (A's, first used at t 1), and nobody else's */
  CHECK(miss_slot(&more[MISS_PROBE - 1], 40) != NULL);
  for (int k = 0; k < MISS_PROBE - 1; k++) CHECK(miss_slot(&more[k], 41) == NULL);
}

static void reset(void) {
  memset(misses, 0, sizeof misses);
  memset(code_misses, 0, sizeof code_misses);
  memset(games, 0, sizeof games);
  join_all = 0;
}
static Game *host(const char *code) {
  for (int i = 0; i < MAX_GAMES; i++)
    if (!games[i].used) {
      games[i].used = true;
      memcpy(games[i].code, code, 7);
      games[i].host = ip(0x0A0A0A0Au);
      return &games[i];
    }
  return NULL;
}

/* a code being played that 10 addresses got wrong says nothing more, to anyone */
static void test_per_code(void) {
  reset();
  host("004711");
  Addr guest = ip(0x0B000001u);
  CHECK(join_reply(&guest, "004711", 0) == T_RJOIN);
  for (int k = 0; k < MISS_CODE; k++) {
    Addr a = ip(0x0C000000u + (uint32_t)k * 977u);
    CHECK(join_reply(&a, "004711", 1) == T_RFAIL);
  }
  Addr late = ip(0x0D000001u);
  CHECK(join_reply(&late, "004711", 2) == 0);
  CHECK(join_reply(&guest, "004711", 3) == T_RJOIN); /* its own guest still may */
  CHECK(join_reply(&late, "123456", 4) == T_RFAIL);   /* and the same address another code */
  /* a wrong code that 10 addresses tried is quiet too */
  for (int k = 0; k < MISS_CODE; k++) {
    Addr a = ip(0x0E000000u + (uint32_t)k * 977u);
    CHECK(join_reply(&a, "222222", 5) == T_RFAIL);
  }
  CHECK(join_reply(&late, "222222", 6) == 0);
}

/* a sweep from many addresses: after the relay's join tries for the minute nobody hears anything, a live code
 * included */
static void test_all(void) {
  reset();
  host("004711");
  int answered = 0, found = 0;
  for (int c = 0; c < 5000; c++) {
    Addr a = ip(0x14000000u + (uint32_t)c * 131u);
    char code[8];
    snprintf(code, sizeof code, "%06d", c < 4999 ? c * 7 + 1 : 4711);
    int r = join_reply(&a, code, 1);
    answered += r != 0;
    found += r == T_RJOIN;
  }
  CHECK(answered == JOIN_ALL);
  CHECK(found == 0);
}

int main(void) {
  test_collide();
  test_full();
  test_per_code();
  test_all();
  if (fails) fprintf(stderr, "test_relay_limits: %d failed\n", fails);
  else printf("test_relay_limits: ok\n");
  return fails != 0;
}
