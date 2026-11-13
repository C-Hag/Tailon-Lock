/* test_net.c - what net.c takes from the other computer, on its own functions: an event naming a jet, a target,
 * a decoy, a missile kind or an owner that is not there is dropped or loses that field, a number that is not
 * finite drops it (in a STATE snapshot too, and a huge one), the guest's tanker call reaches the host, and a real
 * event comes through as it was sent; a packet shorter than what it should hold is refused, never filled in from the
 * bytes an earlier packet left. Original code for this project. */
#include "../src/net.c"

/* the little of the platform net.c and assets.c call */
void plat_log(const char *fmt, ...) { (void)fmt; }
uint64_t plat_ms(void) { return (uint64_t)clock() * 1000u / CLOCKS_PER_SEC; }
int plat_random(void *buf, size_t size) { return memset(buf, 0x5a, size), 1; }
const char *plat_base_path(void) { return NULL; }
bool plat_is_file(const char *path) { return (void)path, false; }
bool plat_file_sync(FILE *f) { return (void)f, false; } /* nothing is saved here */
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }


static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_net:%d: %s\n", __LINE__, #c);              \
      fails++;                                                         \
    }                                                                  \
  } while (0)

/* a missile event as put_event writes it */
typedef struct { int owner, tkind, tidx, kind; float speed; } Ev;
static void put_missile(Buf *p, Ev e) {
  p->n = 0;
  put8(p, NE_MISSILE);
  put32(p, 77);
  putv(p, v3(1, 2, 3));
  putv(p, v3(0, 0, 1));
  putf(p, e.speed);
  putf(p, 600);
  putf(p, 2);
  putf(p, 9);
  putf(p, 40);
  put8(p, 1);
  put8(p, e.owner + 8);
  put8(p, e.tkind);
  put8(p, e.tidx + 1);
  put32(p, 5);
  put8(p, 0);
  put8(p, e.kind);
}
/* read back what was written */
static void rewind_buf(Buf *p) {
  p->end = p->n;
  p->n = 0;
  p->bad = false;
}

static Game *g;
static Missile *take(Ev e) {
  Buf p;
  put_missile(&p, e);
  rewind_buf(&p);
  g->nmissiles = 0;
  take_event(&p, g, true);
  return g->nmissiles ? &g->missiles[0] : NULL;
}
static bool target_ok(const Missile *m) {
  const Ref *r = &m->target;
  if (r->kind == REF_PLANE) return r->idx >= 0 && r->idx < g->nplanes;
  if (r->kind == REF_GROUND) return r->idx >= 0 && r->idx < g->ntargets;
  if (r->kind == REF_FLARE) return r->idx >= 0 && r->idx < MAX_FLARES;
  return r->kind <= REF_CARRIER;
}

static void test_events(void) {
  g = calloc(1, sizeof *g);
  if (!g) return;
  g->nplanes = 2;
  g->ntargets = 12;
  g->planes[1].remote = true;
  g->planes[1].idx = 1;
  /* a real one: as it was sent */
  Missile *m = take((Ev){ 1, REF_PLANE, 0, MK_RADAR, 300 });
  CHECK(m && m->id == 77 && m->owner == 1 && m->target.kind == REF_PLANE && m->target.idx == 0 && m->target.gen == 5 &&
        m->kind == MK_RADAR && m->speed == 300 && m->team == 1 && m->remote);
  m = take((Ev){ BY_CARRIER, REF_GROUND, 11, MK_GROUND, 300 });
  CHECK(m && m->owner == BY_CARRIER && m->target.idx == 11);
  /* indexes past their arrays: the field goes, the missile flies on without it */
  m = take((Ev){ 1, REF_GROUND, 199, MK_HEAT, 300 });
  CHECK(m && target_ok(m));
  m = take((Ev){ 1, REF_GROUND, 12, MK_HEAT, 300 });
  CHECK(m && target_ok(m));
  m = take((Ev){ 1, REF_FLARE, 200, MK_HEAT, 300 });
  CHECK(m && target_ok(m));
  m = take((Ev){ 1, REF_PLANE, 2, MK_HEAT, 300 });
  CHECK(m && target_ok(m));
  m = take((Ev){ 1, 9, 0, MK_HEAT, 300 });
  CHECK(m && target_ok(m));
  m = take((Ev){ 2, REF_NONE, -1, MK_HEAT, 300 });
  CHECK(m && m->owner == -1);
  m = take((Ev){ -5, REF_NONE, -1, MK_HEAT, 300 });
  CHECK(m && m->owner == -1);
  /* no such missile kind, or a number that is not finite: dropped */
  CHECK(!take((Ev){ 1, REF_NONE, -1, 3, 300 }));
  CHECK(!take((Ev){ 1, REF_NONE, -1, MK_HEAT, INFINITY }));
  /* decoys from a jet that is not there, or not the other computer's */
  Buf p;
  for (int pi = 0; pi < 256; pi++) {
    p.n = 0;
    put8(&p, NE_FLARES);
    put8(&p, pi);
    put8(&p, 0);
    rewind_buf(&p);
    take_event(&p, g, true);
  }
  int live = 0, theirs = 0;
  for (int i = 0; i < MAX_FLARES; i++) live += g->flares[i].live, theirs += g->flares[i].live && g->flares[i].owner == 1;
  CHECK(live == 2 && theirs == 2); /* one pair, plane 1's */
  /* an event kind we don't know */
  p.n = 0;
  put8(&p, 7);
  for (int i = 0; i < 60; i++) put8(&p, 1);
  rewind_buf(&p);
  g->nmissiles = 0;
  take_event(&p, g, true);
  CHECK(g->nmissiles == 0);
  /* the guest's tanker call reaches the host, and an event after it in the same packet still counts */
  memset(g->flares, 0, sizeof g->flares);
  N.role = R_HOST;
  p.n = 0;
  put8(&p, NE_TANKER);
  put8(&p, 1);
  put8(&p, NE_FLARES);
  put8(&p, 1);
  put8(&p, 0);
  rewind_buf(&p);
  take_event(&p, g, true);
  CHECK(!p.bad && g->planes[1].tcalled && g->tankers[g->planes[1].team & 1].state == TK_INBOUND);
  take_event(&p, g, true);
  live = 0;
  for (int i = 0; i < MAX_FLARES; i++) live += g->flares[i].live;
  CHECK(!p.bad && live == 2);
  /* a call for a jet that is not the other computer's, or on the guest's side: nothing */
  g->planes[0].tcalled = false;
  p.n = 0;
  put8(&p, NE_TANKER);
  put8(&p, 0);
  rewind_buf(&p);
  take_event(&p, g, true);
  CHECK(!p.bad && !g->planes[0].tcalled);
  N.role = R_NONE;
  free(g);
  g = NULL;
}

/* a STATE snapshot for plane 1 with the given place, direction and speed */
static void put_state(Buf *p, vec3 pos, vec3 f, float speed) {
  p->n = 0;
  put32(p, N.match.game);
  put8(p, 1);
  put8(p, 1);
  putv(p, pos);
  putv(p, f);
  putv(p, v3(0, 1, 0));
  putv(p, v3_scale(f, speed));
  putf(p, speed);
  putf(p, 1);
  put8(p, 1);
  put8(p, 0);
  putf(p, 100);
  for (int i = 0; i < 4; i++) put8(p, 1);
  for (int j = 0; j < MAX_PLANES; j++) put8(p, 0);
  put8(p, 0); /* no targets */
  put8(p, 0); /* no scores */
  put8(p, 0); /* no tankers */
}

/* a snapshot with infinite, huge or zero numbers never reaches the plane; a real one does */
static void test_state_values(void) {
  g = calloc(1, sizeof *g);
  if (!g) return;
  g->nplanes = 2;
  g->planes[1].remote = true;
  g->planes[1].idx = 1;
  g->planes[1].alive = true;
  Buf p;
  const vec3 bad_pos[] = { v3(INFINITY, 100, 0), v3(0, -INFINITY, 0), v3(3e38f, 100, 0) };
  for (int k = 0; k < 3; k++) {
    g->planes[1].snap.have = false;
    put_state(&p, bad_pos[k], v3(0, 0, 1), 200);
    rewind_buf(&p);
    take_state(g, &p);
    CHECK(!g->planes[1].snap.have);
  }
  const vec3 bad_f[] = { v3(INFINITY, 0, 0), v3(0, 0, 0), v3(3e38f, 3e38f, 0) };
  for (int k = 0; k < 3; k++) {
    g->planes[1].snap.have = false;
    put_state(&p, v3(0, 100, 0), bad_f[k], 200);
    rewind_buf(&p);
    take_state(g, &p);
    CHECK(!g->planes[1].snap.have);
  }
  g->planes[1].snap.have = false;
  put_state(&p, v3(0, 100, 0), v3(0, 0, 1), 3e38f);
  rewind_buf(&p);
  take_state(g, &p);
  CHECK(!g->planes[1].snap.have);
  put_state(&p, v3(10, 100, 20), v3(0, 0, 2), 200);
  rewind_buf(&p);
  take_state(g, &p);
  const Plane *q = &g->planes[1];
  CHECK(q->snap.have && q->snap.pos.x == 10 && q->snap.f.z == 1 && q->snap.speed == 200 && isfinite(q->snap.vel.z));
  free(g);
  g = NULL;
}

/* a test's socket: none (out of descriptors, no network stack) stops the test here, not in a later call */
static sock_t open_or_fail(void) {
  sock_t s = udp_open(0, false, false);
  if (s == BADSOCK) {
    fprintf(stderr, "test_net: no socket\n");
    exit(1);
  }
  return s;
}

static void wait_a_little(void) { /* for loopback to deliver */
  clock_t t0 = clock();
  while ((double)(clock() - t0) < 0.05 * CLOCKS_PER_SEC) {}
}

/* the LAN list: a 64-byte packet it skips, then a 6-byte beacon. The beacon is short: refused, not read on into
 * the first packet's bytes (which made a game called "XZZZ..." on port 0x5A5A). A whole one is listed. */
static void test_short(void) {
  sock_t s = open_or_fail();
  CHECK(s != BADSOCK);
  if (s == BADSOCK) return;
  Addr a;
  socklen_t al = sizeof a;
  getsockname(s, (struct sockaddr *)&a, &al);
  a.sin_addr.s_addr = htonl(0x7F000001u); /* 127.0.0.1 */
  N.lan = s;
  uint8_t junk[64];
  memset(junk, 'Z', sizeof junk);
  memcpy(junk, "TL", 3);
  junk[3] = VERSION;
  junk[4] = T_HELLO;
  sendto(s, (const char *)junk, sizeof junk, 0, (const struct sockaddr *)&a, sizeof a);
  const uint8_t b6[6] = { 'A', '4', 'C', VERSION, T_BEACON, 'X' };
  sendto(s, (const char *)b6, sizeof b6, 0, (const struct sockaddr *)&a, sizeof a);
  wait_a_little();
  lan_poll(0);
  CHECK(N.nlan == 0);
  Buf p;
  head(&p, T_BEACON);
  puts_(&p, "ANNA", 16);
  put32(&p, NET_PORT);
  put8(&p, 1);
  puts_(&p, "twin-isles", 24);
  sendto(s, (const char *)p.b, p.n, 0, (const struct sockaddr *)&a, sizeof a);
  wait_a_little();
  lan_poll(0);
  CHECK(N.nlan == 1 && !strcmp(net_lan_name(0), "ANNA CO-OP"));
  closesock(s);
  N.lan = BADSOCK;
  /* and on a Buf: a read past what came is bad and gives 0s */
  memset(p.b, 'Z', sizeof p.b);
  p.n = 5;
  p.end = 6;
  p.bad = false;
  char name[16];
  CHECK(get8(&p) == 'Z' && !p.bad);
  gets_(&p, name, sizeof name);
  CHECK(p.bad && name[0] == 0 && get8(&p) == 0);
}

/* the spectator's snapshots between packets: every jet's, from the watch stream's state as watch_state writes it */
static void test_watch_snap(void) {
  static Game src, view;
  src.nplanes = view.nplanes = 3;
  for (int i = 0; i < 3; i++) {
    src.planes[i].pos = v3(100.0f * (float)(i + 1), 50, -7);
    src.planes[i].speed = 200 + (float)i;
  }
  Buf s;
  watch_state(&src, 5, &s);
  s.end = s.n; /* as unseal leaves it */
  s.n = 5;
  s.bad = false;
  WS.state = s;
  WS.have_state = true;
  WS.match = 5;
  WS.viewing = true;
  WS.hello_t = 1000;
  WS.s = open_or_fail();
  static char who[MAX_PLANES][16], said[MAX_PLANES][40];
  net_watch_poll(&view, 0, who, said);
  for (int i = 0; i < 3; i++)
    CHECK(view.planes[i].snap.have && view.planes[i].snap.pos.x == src.planes[i].pos.x && view.planes[i].snap.speed == src.planes[i].speed);
  closesock(WS.s);
  WS.s = BADSOCK;
  WS.viewing = false;
  WS.match = 0;
}

static Addr loop_addr(int port) {
  Addr a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(0x7F000001u);
  a.sin_port = htons((uint16_t)port);
  return a;
}
static void hello_from(const Addr *a, uint8_t tag) {
  Buf h;
  head(&h, T_HELLO);
  for (int i = 0; i < 16; i++) put8(&h, tag + i);
  for (int i = 0; i < 32; i++) put8(&h, i ? 0 : 9); /* the base point: a real one */
  h.end = h.n;
  h.n = 5;
  h.bad = false;
  host_hello(&h, a);
}
static int pend_of(const Addr *a) {
  for (int k = 0; k < 4; k++)
    if (N.pend[k].used && same(&N.pend[k].from, a)) return k;
  return -1;
}

/* a HELLO flood (anyone can reach a direct or LAN host) neither evicts the guest that is half way in nor costs
 * two X25519 a packet: a caller holds one handshake, a new one waits for a slot older than two seconds */
static void test_hello_flood(void) {
  N.role = R_HOST;
  snprintf(N.pin, sizeof N.pin, "123456");
  N.s = open_or_fail();
  memset(N.pend, 0, sizeof N.pend);
  Addr guest = loop_addr(40001);
  hello_from(&guest, 1);
  int k = pend_of(&guest);
  CHECK(k >= 0);
  uint8_t keys[32];
  if (k >= 0) memcpy(keys, N.pend[k].k_gh, 32);
  int made = 0;
  for (int i = 0; i < 2000; i++) {
    Addr a = loop_addr(41000 + i % 3000);
    hello_from(&a, (uint8_t)(i * 7 + 3));
    made += pend_of(&a) >= 0;
  }
  k = pend_of(&guest);
  CHECK(k >= 0 && !memcmp(N.pend[k].k_gh, keys, 32)); /* the guest's handshake is still there */
  CHECK(made <= 3);                                   /* 3 slots left, and none old enough to give up */
  /* one caller's new hellos take its own slot, not another's */
  for (int i = 0; i < 4; i++) N.pend[i].age = 5;
  N.pend[pend_of(&guest) & 3].age = 0; /* the guest has just said hello */
  Addr b = loop_addr(42000);
  for (int i = 0; i < 20; i++) hello_from(&b, (uint8_t)(100 + i));
  int mine = 0;
  for (int i = 0; i < 4; i++) mine += N.pend[i].used && same(&N.pend[i].from, &b);
  CHECK(mine == 1 && pend_of(&guest) >= 0);
  closesock(N.s);
  N.s = BADSOCK;
  memset(N.pend, 0, sizeof N.pend);
  N.role = R_NONE;
}

/* through the relay every caller has the relay's address, so a JOIN no key opens can't say whose it is: a stale one
 * from the guest before must not reject the guest after. A pend is rejected on its second failed JOIN (a wrong-code
 * guest sends one every half second), so the right JOIN in between still gets in */
static void test_relay_join(void) {
  N.role = R_HOST;
  N.via_relay = true;
  snprintf(N.pin, sizeof N.pin, "123456");
  N.s = open_or_fail();
  N.relay = loop_addr(40999);
  memset(N.pend, 0, sizeof N.pend);
  N.tries = 0;
  hello_from(&N.relay, 1);
  int k = pend_of(&N.relay);
  CHECK(k >= 0);
  Buf j;
  head(&j, T_JOIN);
  for (int i = 0; i < 40; i++) put8(&j, i);
  int n = j.n;
  CHECK(!host_join(&j, n, &N.relay));
  CHECK(pend_of(&N.relay) == k && N.tries == 0); /* not rejected yet */
  CHECK(!host_join(&j, n, &N.relay));
  CHECK(pend_of(&N.relay) < 0 && N.tries == 1);   /* the second: a wrong code */
  closesock(N.s);
  N.s = BADSOCK;
  N.via_relay = false;
  N.tries = 0;
  N.role = R_NONE;
}

/* a host keeps telling the relay its code, not just pinging: a relay that restarted, or dropped the game after a
 * minute without its packets, knows nothing of it, ignores a ping from it and would answer every guest "no game" */
static void test_relay_keepalive(void) {
  sock_t r = open_or_fail();
  Addr ra;
  socklen_t al = sizeof ra;
  getsockname(r, (struct sockaddr *)&ra, &al);
  N.role = R_HOST;
  N.via_relay = true;
  N.registered = true;
  N.phase = PH_WAIT;
  snprintf(N.pin, sizeof N.pin, "123456");
  N.s = open_or_fail();
  N.relay = loop_addr(ntohs(ra.sin_port));
  N.beacon_t = 0;
  NetMatch m;
  net_poll(0.1f, &m);
  wait_a_little();
  uint8_t b[64];
  int n = (int)recv(r, (char *)b, sizeof b, 0);
  CHECK(n == 11 && b[4] == T_RHOST && !memcmp(b + 5, "123456", 6));
  closesock(N.s);
  closesock(r);
  N.s = BADSOCK;
  N.via_relay = N.registered = false;
  N.phase = PH_IDLE;
  N.role = R_NONE;
}

/* the relay hosts 4 games per address; a fifth host behind the same router hears T_RFAIL for every code it picks:
 * after 4 in a row it says why, and asks less often, instead of drawing new codes every second forever */
static void test_relay_full(void) {
  sock_t r = open_or_fail();
  Addr ra;
  socklen_t al = sizeof ra;
  getsockname(r, (struct sockaddr *)&ra, &al);
  N.role = R_HOST;
  N.via_relay = true;
  N.registered = false;
  N.phase = PH_WAIT;
  snprintf(N.pin, sizeof N.pin, "123456");
  N.s = open_or_fail();
  Addr hs;
  al = sizeof hs;
  getsockname(N.s, (struct sockaddr *)&hs, &al);
  hs.sin_addr.s_addr = htonl(0x7F000001u);
  N.relay = loop_addr(ntohs(ra.sin_port));
  N.status[0] = 0;
  NetMatch m;
  for (int i = 0; i < 4; i++) {
    const uint8_t f[5] = { 'A', '4', 'C', VERSION, T_RFAIL };
    sendto(r, (const char *)f, sizeof f, 0, (const struct sockaddr *)&hs, sizeof hs);
    wait_a_little();
    N.beacon_t = 1;
    net_poll(0.01f, &m);
  }
  CHECK(strstr(N.status, "4 GAMES") != NULL);
  CHECK(N.beacon_t > 2); /* and asks again in a few seconds, not every one */
  closesock(N.s);
  closesock(r);
  N.s = BADSOCK;
  N.via_relay = false;
  N.phase = PH_IDLE;
  N.role = R_NONE;
}

/* the watch stream's challenge slot: the hello count past 2^31 (a broadcast server after hours of hellos) still
 * picks a slot 0..7, never one before the array */
static void test_watch_chal(void) {
  WS.serving = true;
  WS.s = open_or_fail();
  Addr ws;
  socklen_t al = sizeof ws;
  getsockname(WS.s, (struct sockaddr *)&ws, &al);
  ws.sin_addr.s_addr = htonl(0x7F000001u);
  sock_t v = open_or_fail();
  memset(WS.chal, 0, sizeof WS.chal);
  WS.nchal = 0x80000001u; /* 2^31 + 1 hellos so far */
  uint8_t h[21] = { 'A', '4', 'C', VERSION, W_HELLO };
  sendto(v, (const char *)h, sizeof h, 0, (const struct sockaddr *)&ws, sizeof ws);
  wait_a_little();
  static Game wg;
  WatchMatch m;
  memset(&m, 0, sizeof m);
  net_watch_send(&wg, &m, 0, NULL, NULL);
  CHECK(WS.chal[1].used);
  closesock(v);
  closesock(WS.s);
  WS.s = BADSOCK;
  WS.serving = false;
}

/* keeping pace: a host whose guest runs behind (a slow computer) waits for it, more than a second behind, until it
 * is within 0.4 s, and says so; the guest knows it is behind; the host's welcomes don't count as its game time,
 * and a packet number far ahead or before the match never makes the host wait for ever */
static void test_pace(void) {
  g = calloc(1, sizeof *g); /* a game of its own: test_events freed its one */
  if (!g) return;
  N.s = open_or_fail();
  N.phase = PH_PLAY;
  N.role = R_HOST;
  N.rx_top = 100;
  pace_reset();
  for (int i = 0; i < 60; i++) net_tick(g, 1.0f / 60); /* 30 packets of ours, none of theirs: not yet */
  CHECK(N.pace_sent == 30 && !net_holding() && net_pace_note() == NULL);
  for (int i = 0; i < 4; i++) net_tick(g, 1.0f / 60);
  CHECK(net_holding() && net_pace_note() && !strcmp(net_pace_note(), "WAITING FOR THE OTHER PLAYER"));
  uint32_t sent = N.pace_sent;
  sock_t r = open_or_fail(); /* the guest */
  Addr ra;
  socklen_t al = sizeof ra;
  getsockname(r, (struct sockaddr *)&ra, &al);
  N.peer = loop_addr(ntohs(ra.sin_port));
  N.have_peer = N.keyed = true;
  uint64_t tx = N.tx_ctr;
  for (int i = 0; i < 60; i++) net_tick(g, 1.0f / 60); /* holding: a welcome every half second, no game time */
  wait_a_little();
  uint8_t b[1500];
  int got = 0, welcomes = 0;
  for (int n; (n = (int)recv(r, (char *)b, sizeof b, 0)) > 0; got++) welcomes += n > 5 && b[4] == T_WELCOME;
  CHECK(net_holding() && N.pace_sent == sent && N.tx_ctr == tx + 2 && got == 2 && welcomes == 2);
  N.have_peer = N.keyed = false;
  closesock(r);
  N.rx_top = 100 + N.pace_sent - 13;
  net_tick(g, 1.0f / 60);
  CHECK(net_holding());
  N.rx_top++;
  net_tick(g, 1.0f / 60);
  CHECK(!net_holding() && net_pace_note() == NULL);
  N.rx_top = UINT64_MAX; /* far ahead */
  for (int i = 0; i < 200; i++) net_tick(g, 1.0f / 60);
  CHECK(!net_holding());
  N.rx_top = 50; /* before the match began here */
  pace_reset();
  N.pace_base = 60;
  for (int i = 0; i < 64; i++) net_tick(g, 1.0f / 60); /* counts as none of theirs: ours alone make it wait */
  CHECK(net_holding());
  /* the guest: the host's states ahead of ours, less its welcomes */
  N.role = R_GUEST;
  N.rx_top = 100;
  pace_reset();
  N.rx_top = 131;
  N.pace_welc = 1;
  net_tick(g, 1.0f / 60);
  CHECK(!net_holding() && net_pace_note() == NULL);
  N.rx_top = 133;
  net_tick(g, 1.0f / 60);
  CHECK(!net_holding() && net_pace_note() && strstr(net_pace_note(), "CATCHING UP"));
  N.pace_sent += 20;
  net_tick(g, 1.0f / 60);
  CHECK(net_pace_note() == NULL);
  /* and a welcome of this match past its first seconds: the host's sign it is waiting, shown for a second */
  sock_t r2 = open_or_fail();
  Addr ra2;
  socklen_t al2 = sizeof ra2;
  getsockname(r2, (struct sockaddr *)&ra2, &al2);
  ra2 = loop_addr(ntohs(ra2.sin_port));
  Buf w;
  head(&w, T_WELCOME);
  put_match(&w, &N.match);
  memset(N.k_rx, 7, sizeof N.k_rx);
  seal_to(&ra2, &w, N.k_rx, N.rx_top + 1);
  wait_a_little();
  Addr from;
  socklen_t fl = sizeof from;
  int wn = (int)recvfrom(r2, (char *)w.b, sizeof w.b, 0, (struct sockaddr *)&from, &fl);
  N.keyed = true;
  g->time = 60;
  uint32_t welc = N.pace_welc;
  CHECK(wn > 0 && play_packet(g, &w, wn, &from) && N.pace_welc == welc + 1);
  net_tick(g, 0.5f);
  CHECK(net_pace_note() && strstr(net_pace_note(), "CATCHING UP"));
  net_tick(g, 0.6f);
  CHECK(net_pace_note() == NULL);
  N.keyed = false;
  g->time = 0;
  closesock(r2);
  closesock(N.s);
  N.s = BADSOCK;
  N.phase = PH_IDLE;
  N.role = R_NONE;
  N.rx_top = 0;
  free(g);
  g = NULL;
}

int main(void) {
#ifdef _WIN32
  WSADATA w; /* before any test opens a socket */
  WSAStartup(MAKEWORD(2, 2), &w);
#endif
  test_events();
  test_state_values();
  test_pace();
  test_watch_chal();
  test_relay_full();
  test_relay_keepalive();
  test_relay_join();
  test_hello_flood();
  test_short();
  test_watch_snap();
  if (fails) fprintf(stderr, "test_net: %d failed\n", fails);
  else printf("test_net: ok\n");
  return fails != 0;
}
