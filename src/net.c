/* net.c - network play over UDP (see net.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif
#include "net.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/sim.h"
#include "monocypher.h"
#include "platform/plat.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BADSOCK INVALID_SOCKET
#define closesock closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BADSOCK (-1)
#define closesock close
#endif

/* ---- sockets ---------------------------------------------------------------- */
typedef struct sockaddr_in Addr;

static sock_t udp_open(int port, bool broadcast, bool shared) {
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == BADSOCK) return BADSOCK;
  int one = 1;
  if (shared) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one); /* LAN beacons: every copy hears them */
  if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
  Addr a = { 0 };
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
    closesock(s);
    return BADSOCK;
  }
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  /* no "connection reset" errors from a peer's ICMP replies: UDP has no connection */
  BOOL no = FALSE;
  DWORD got;
  WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &no, sizeof no, NULL, 0, &got, NULL, NULL);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  return s;
}

static bool resolve(const char *text, int defport, Addr *out) {
  char host[128];
  int port = defport;
  snprintf(host, sizeof host, "%s", text);
  char *colon = strrchr(host, ':');
  if (colon) {
    port = atoi(colon + 1);
    *colon = 0;
  }
  struct addrinfo hints = { 0 }, *res = NULL;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (!host[0] || getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
  *out = *(Addr *)res->ai_addr;
  out->sin_port = htons((uint16_t)port);
  freeaddrinfo(res);
  return true;
}

static bool same(const Addr *a, const Addr *b) { return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port; }

static void addr_text(const Addr *a, char *buf, size_t n) {
  const uint8_t *ip = (const uint8_t *)&a->sin_addr.s_addr;
  snprintf(buf, n, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ntohs(a->sin_port));
}

/* ---- packets ------------------------------------------------------------------ */
/* Every packet starts "A4C", a version and a type. Numbers are written in
 * little-endian byte order; floats as their IEEE bits.
 *
 * Security: the host shows a 6-digit game code and the guest types it. The
 * two computers turn it into a shared secret key with a password handshake
 * (CPace on X25519: both start from a curve point made from the code, so
 * only someone who knows the code ends up with the same key, and someone
 * listening learns nothing they could test codes against offline). From
 * then on every match packet is encrypted and signed (XChaCha20-Poly1305,
 * Monocypher) and numbered, so it cannot be read, changed or replayed. A
 * wrong guess costs a full round trip, and the host gives up after 10. */
#define VERSION 3 /* relay/relay.c checks it too: change both together */
enum {
  T_BEACON = 1, T_HELLO, T_WELCOME, T_STATE, T_BYE, T_RHOST, T_RCODE, T_RJOIN, T_RFAIL, T_PING,
  T_CHALLENGE, T_JOIN, T_REJECT
};
/* the types that travel encrypted */
static bool sealed_type(int t) { return t == T_WELCOME || t == T_STATE || t == T_BYE || t == T_JOIN; }
#define SEAL_EXTRA (8 + 16) /* the packet number and the signature */
#define MAX_TRIES  10       /* wrong codes before the host stops */

typedef struct { uint8_t b[1400]; int n; bool bad; } Buf;

static void put8(Buf *p, int v) {
  if (p->n < (int)sizeof p->b) p->b[p->n++] = (uint8_t)v;
}
static void put32(Buf *p, uint32_t v) {
  for (int i = 0; i < 4; i++) put8(p, (int)(v >> (8 * i) & 255));
}
static void putf(Buf *p, float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  put32(p, u);
}
static void putv(Buf *p, vec3 v) {
  putf(p, v.x);
  putf(p, v.y);
  putf(p, v.z);
}
static void puts_(Buf *p, const char *s, int n) {
  for (int i = 0; i < n; i++) put8(p, s && i < (int)strlen(s) ? s[i] : 0);
}
static void patch8(Buf *p, int at, int v) {
  if (at >= 0 && at < p->n) p->b[at] = (uint8_t)v;
}
static int get8(Buf *p) {
  if (p->n >= (int)sizeof p->b) {
    p->bad = true;
    return 0;
  }
  return p->b[p->n++];
}
static uint32_t get32(Buf *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= (uint32_t)get8(p) << (8 * i);
  return v;
}
static float getf(Buf *p) {
  uint32_t u = get32(p);
  float f;
  memcpy(&f, &u, 4);
  return f == f ? f : 0;
}
static vec3 getv(Buf *p) {
  float x = getf(p), y = getf(p);
  return v3(x, y, getf(p));
}
static void gets_(Buf *p, char *s, int n) {
  for (int i = 0; i < n; i++) s[i] = (char)get8(p);
  s[n - 1] = 0;
}
static void getn(Buf *p, uint8_t *dst, int n) {
  if (p->n + n > (int)sizeof p->b) {
    p->bad = true;
    memset(dst, 0, (size_t)n);
    return;
  }
  memcpy(dst, p->b + p->n, (size_t)n);
  p->n += n;
}
static void putn(Buf *p, const uint8_t *src, int n) {
  for (int i = 0; i < n; i++) put8(p, src[i]);
}
static void head(Buf *p, int type) {
  p->n = 0;
  put8(p, 'A');
  put8(p, '4');
  put8(p, 'C');
  put8(p, VERSION);
  put8(p, type);
}

/* ---- state ---------------------------------------------------------------------- */
enum { R_NONE, R_HOST, R_GUEST };
enum { PH_IDLE, PH_WAIT, PH_PLAY, PH_GONE };

static struct {
  int      role, phase;
  sock_t   s, lan;
  Addr     peer, relay;
  bool     via_relay, have_peer;
  char     pin[7], name[16], status[96]; /* pin: the 6-digit game code */
  bool     registered; /* the relay has our code */
  NetMatch match;
  float    since_rx, beacon_t, send_t, hello_t, wait_t;
  /* the guest's half of the handshake */
  uint8_t  secret[32], nonce[16], y[32];
  bool     challenged;
  /* the host's handshakes in progress, one per caller */
  struct { bool used; Addr from; uint8_t nonce[16], y[32], k_hg[32], k_gh[32]; float age; } pend[4];
  int      tries; /* wrong codes so far */
  /* the session: a key each way, packet numbers, and which ones arrived */
  uint8_t  k_tx[32], k_rx[32];
  bool     keyed;
  uint64_t tx_ctr, rx_top, rx_seen;
  int      rx_ok, rx_refused; /* match packets taken and refused (tests) */
  /* events: the last 16 we made, repeated in every state packet */
  struct { uint32_t seq; Buf b; } ev[16];
  uint32_t ev_seq, ev_in, ev_ack; /* made, taken in, and the other side has */
  int      guest_jet, guest_load[3];
  /* LAN games heard */
  struct { char name[24], addr[32]; float age; } lan_games[8];
  int      nlan;
  bool     scanning, start; /* start: a new match to hand over (a rematch) */
} N = { .s = BADSOCK, .lan = BADSOCK };

static void say(const char *fmt, const char *arg) {
  snprintf(N.status, sizeof N.status, fmt, arg ? arg : "");
  SDL_Log("net: %s", N.status);
}

static void send_raw(const Addr *a, const uint8_t *b, int n) {
  if (N.s != BADSOCK) sendto(N.s, (const char *)b, n, 0, (const struct sockaddr *)a, sizeof *a);
}
static void send_to(const Addr *a, const Buf *p) { send_raw(a, p->b, p->n); }
static const Addr *peer_addr(void) { return N.via_relay ? &N.relay : N.have_peer ? &N.peer : NULL; }

/* ---- the handshake and the encryption ----------------------------------------- */
/* The starting point both sides make from the code and the guest's nonce. */
static void code_point(uint8_t g[32], const char *pin, const uint8_t nonce[16]) {
  uint8_t msg[11 + 6 + 16], h[32];
  memcpy(msg, "A4C-CPACE-1", 11);
  memcpy(msg + 11, pin, 6);
  memcpy(msg + 17, nonce, 16);
  crypto_blake2b(h, 32, msg, sizeof msg);
  crypto_elligator_map(g, h);
  crypto_wipe(h, sizeof h);
}

/* The two keys from the shared point and everything both sides sent. */
static bool derive(uint8_t k_hg[32], uint8_t k_gh[32], uint8_t shared[32], const uint8_t nonce[16], const uint8_t yg[32],
                   const uint8_t yh[32]) {
  static const uint8_t zero[32];
  if (crypto_verify32(shared, zero) == 0) return false; /* a broken point from the other side */
  uint8_t t[16 + 32 + 32], k[64];
  memcpy(t, nonce, 16);
  memcpy(t + 16, yg, 32);
  memcpy(t + 48, yh, 32);
  crypto_blake2b_keyed(k, 64, shared, 32, t, sizeof t);
  memcpy(k_hg, k, 32);
  memcpy(k_gh, k + 32, 32);
  crypto_wipe(k, sizeof k);
  crypto_wipe(shared, 32);
  return true;
}

static bool random_bytes(void *b, size_t n) {
  if (plat_random(b, n)) return true;
  say("NO SECURE RANDOM NUMBERS ON THIS COMPUTER%s", "");
  return false;
}

/* Encrypts p (header and body) under key with packet number ctr and sends it. */
static void seal_to(const Addr *a, const Buf *p, const uint8_t key[32], uint64_t ctr) {
  uint8_t out[sizeof p->b + SEAL_EXTRA], nonce[24] = { 0 };
  int body = p->n - 5;
  memcpy(out, p->b, 5);
  for (int i = 0; i < 8; i++) out[5 + i] = (uint8_t)(ctr >> (8 * i));
  memcpy(nonce, out + 5, 8);
  crypto_aead_lock(out + 13, out + 13 + body, key, nonce, out, 13, p->b + 5, (size_t)body);
  send_raw(a, out, 13 + body + 16);
}
/* to the other computer: straight, or through the relay */
static void send_peer(const Buf *p) {
  const Addr *a = peer_addr();
  if (a && N.keyed) seal_to(a, p, N.k_tx, ++N.tx_ctr);
}

/* Decrypts a sealed packet of n bytes in p under key; its number goes in ctr. */
static bool unseal(Buf *p, int n, const uint8_t key[32], uint64_t *ctr) {
  if (n < 13 + 16 || n > (int)sizeof p->b) return false;
  uint8_t plain[sizeof p->b], nonce[24] = { 0 };
  int body = n - 13 - 16;
  memcpy(nonce, p->b + 5, 8);
  if (crypto_aead_unlock(plain, p->b + 13 + body, key, nonce, p->b, 13, p->b + 13, (size_t)body)) return false;
  *ctr = 0;
  for (int i = 0; i < 8; i++) *ctr |= (uint64_t)p->b[5 + i] << (8 * i);
  memcpy(p->b + 5, plain, (size_t)body);
  p->n = 5;
  p->bad = false;
  return true;
}

/* A session packet: decrypts it and refuses one seen before (a replay). */
static bool unseal_session(Buf *p, int n) {
  uint64_t c;
  if (!N.keyed || !unseal(p, n, N.k_rx, &c)) return false;
  if (c > N.rx_top) {
    N.rx_seen = c - N.rx_top >= 64 ? 1 : N.rx_seen << (c - N.rx_top) | 1;
    N.rx_top = c;
    return true;
  }
  uint64_t back = N.rx_top - c;
  if (back >= 64 || (N.rx_seen >> back & 1)) return false;
  N.rx_seen |= 1ull << back;
  return true;
}

static void forget_keys(void) {
  crypto_wipe(N.secret, sizeof N.secret);
  crypto_wipe(N.k_tx, sizeof N.k_tx);
  crypto_wipe(N.k_rx, sizeof N.k_rx);
  crypto_wipe(N.pend, sizeof N.pend);
  N.keyed = N.challenged = false;
  N.tx_ctr = N.rx_top = N.rx_seen = 0;
}

static bool new_pin(void) {
  uint32_t v;
  do {
    if (!random_bytes(&v, sizeof v)) return false;
  } while (v >= 4294000000u); /* no bias toward low codes */
  snprintf(N.pin, sizeof N.pin, "%06u", (unsigned)(v % 1000000u));
  return true;
}

bool net_init(void) { return true; }

void net_close(void) {
  if (N.rx_ok + N.rx_refused && getenv("A4C_PLANES"))
    SDL_Log("net: %d packets taken, %d refused", N.rx_ok, N.rx_refused);
  N.rx_ok = N.rx_refused = 0;
  if (N.phase != PH_IDLE && N.s != BADSOCK) {
    Buf p;
    head(&p, T_BYE);
    send_peer(&p);
  }
  if (N.s != BADSOCK) closesock(N.s);
  N.s = BADSOCK;
  N.role = R_NONE;
  N.have_peer = N.via_relay = N.registered = false;
  N.pin[0] = 0;
  N.tries = 0;
  forget_keys();
  if (N.phase != PH_GONE) N.status[0] = 0; /* keep saying why the match ended */
  N.phase = PH_IDLE;
  N.start = false;
}

void net_rematch(const NetMatch *m) {
  if (N.role != R_HOST || N.phase != PH_PLAY) return;
  NetMatch k = N.match;
  N.match = *m;
  N.match.game = k.game + 1;
  N.match.jets[1] = k.jets[1];
  memcpy(N.match.load[1], k.load[1], sizeof k.load[1]);
  N.start = true;
}

void net_quit(void) {
  net_close();
  net_scan(false);
}

bool net_active(void) { return N.phase == PH_WAIT || N.phase == PH_PLAY; }
bool net_playing(void) { return N.phase == PH_PLAY; }
bool net_is_host(void) { return N.role == R_HOST; }
const char *net_status(void) { return N.status; }
const char *net_code(void) { return N.role == R_HOST && (!N.via_relay || N.registered) ? N.pin : ""; }

bool net_host(const NetMatch *m, const char *name, const char *relay) {
  net_close();
  N.match = *m;
  snprintf(N.name, sizeof N.name, "%s", name);
  N.via_relay = relay && *relay;
  N.s = udp_open(N.via_relay ? 0 : NET_PORT, true, false);
  if (N.s == BADSOCK) {
    say("CAN'T OPEN UDP PORT %s - IS ANOTHER GAME HOSTING?", "27960");
    return false;
  }
  if (N.via_relay && !resolve(relay, RELAY_PORT, &N.relay)) {
    say("CAN'T FIND THE RELAY %s", relay);
    net_close();
    return false;
  }
  if (!new_pin()) {
    net_close();
    return false;
  }
  N.role = R_HOST;
  N.phase = PH_WAIT;
  N.since_rx = N.beacon_t = 0;
  say(N.via_relay ? "REGISTERING THE GAME WITH THE RELAY...%s" : "WAITING FOR A PLAYER WITH THE CODE (LAN, OR UDP PORT 27960)%s", "");
  if (getenv("A4C_PLANES")) SDL_Log("net: game code %s", N.pin); /* tests only: the code is a secret */
  return true;
}

static bool join_common(const char *pin, int jet, const int *load, const char *name) {
  int digits = 0;
  for (int i = 0; pin[i]; i++) digits += pin[i] >= '0' && pin[i] <= '9';
  if (digits != 6 || strlen(pin) != 6) {
    say("TYPE THE 6-DIGIT GAME CODE THE HOST SEES%s", "");
    return false;
  }
  memcpy(N.pin, pin, 7);
  if (!random_bytes(N.nonce, sizeof N.nonce) || !random_bytes(N.secret, sizeof N.secret)) return false;
  uint8_t g[32];
  code_point(g, N.pin, N.nonce);
  crypto_x25519(N.y, N.secret, g);
  N.s = udp_open(0, false, false);
  if (N.s == BADSOCK) {
    say("CAN'T OPEN A UDP SOCKET%s", "");
    return false;
  }
  N.role = R_GUEST;
  N.phase = PH_WAIT;
  N.guest_jet = jet;
  memcpy(N.guest_load, load, sizeof N.guest_load);
  snprintf(N.name, sizeof N.name, "%s", name);
  N.since_rx = N.hello_t = N.wait_t = 0;
  return true;
}

bool net_join(const char *addr, const char *pin, int jet, const int *load, const char *name) {
  net_close();
  Addr a;
  if (!resolve(addr, NET_PORT, &a)) {
    say("CAN'T FIND %s", addr);
    return false;
  }
  if (!join_common(pin, jet, load, name)) return false;
  N.peer = a;
  N.have_peer = true;
  say("CALLING %s...", addr);
  return true;
}

bool net_join_code(const char *relay, const char *pin, int jet, const int *load, const char *name) {
  net_close();
  if (!resolve(relay, RELAY_PORT, &N.relay)) {
    say("CAN'T FIND THE RELAY %s", relay);
    return false;
  }
  if (!join_common(pin, jet, load, name)) return false;
  N.via_relay = true;
  say("LOOKING FOR GAME %s ON THE RELAY...", N.pin);
  return true;
}

/* ---- LAN beacons -------------------------------------------------------------------- */
void net_scan(bool on) {
  N.scanning = on;
  if (on && N.lan == BADSOCK) N.lan = udp_open(NET_LAN, true, true);
  if (!on && N.lan != BADSOCK) {
    closesock(N.lan);
    N.lan = BADSOCK;
    N.nlan = 0;
  }
}
int net_lan_count(void) { return N.nlan; }
const char *net_lan_name(int i) { return i >= 0 && i < N.nlan ? N.lan_games[i].name : ""; }
const char *net_lan_addr(int i) { return i >= 0 && i < N.nlan ? N.lan_games[i].addr : ""; }

static void lan_poll(float dt) {
  for (int i = 0; i < N.nlan;) {
    N.lan_games[i].age += dt;
    if (N.lan_games[i].age > 4) N.lan_games[i] = N.lan_games[--N.nlan];
    else i++;
  }
  if (N.lan == BADSOCK) return;
  Buf p;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while ((n = (int)recvfrom(N.lan, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0) {
    p.n = 0;
    p.bad = false;
    if (n < 6 || p.b[0] != 'A' || p.b[1] != '4' || p.b[2] != 'C' || p.b[3] != VERSION || p.b[4] != T_BEACON) continue;
    p.n = 5;
    char name[16], map[24], addr[32];
    gets_(&p, name, sizeof name);
    int port = (int)get32(&p), coop = get8(&p);
    gets_(&p, map, sizeof map);
    from.sin_port = htons((uint16_t)port);
    addr_text(&from, addr, sizeof addr);
    int k = 0;
    while (k < N.nlan && strcmp(N.lan_games[k].addr, addr)) k++;
    if (k == N.nlan) {
      if (N.nlan == 8) continue;
      N.nlan++;
    }
    snprintf(N.lan_games[k].name, sizeof N.lan_games[k].name, "%.12s %s", name, coop == 2 ? "DRONE" : coop ? "CO-OP" : "VS");
    snprintf(N.lan_games[k].addr, sizeof N.lan_games[k].addr, "%s", addr);
    N.lan_games[k].age = 0;
  }
}

/* ---- the match settings --------------------------------------------------------------- */
static void put_match(Buf *p, const NetMatch *m) {
  put32(p, m->game);
  put32(p, m->seed);
  put8(p, m->coop | m->pairs << 1 | m->classic << 2 | m->realistic << 3 | m->ground << 4 | m->gforce << 5 | m->dogfight << 6 |
              m->no_crash << 7);
  put8(p, m->diff);
  put8(p, m->hits);
  put8(p, m->lives);
  put8(p, m->tod);
  put8(p, m->weather + 1);
  put8(p, m->drone);
  puts_(p, m->map, sizeof m->map);
  for (int s = 0; s < 2; s++) {
    put8(p, m->jets[s]);
    for (int k = 0; k < 3; k++) put8(p, m->load[s][k] + 1);
  }
}
static void get_match(Buf *p, NetMatch *m) {
  m->game = get32(p);
  m->seed = get32(p);
  int f = get8(p);
  m->coop = f & 1;
  m->pairs = f >> 1 & 1;
  m->classic = f >> 2 & 1;
  m->realistic = f >> 3 & 1;
  m->ground = f >> 4 & 1;
  m->gforce = f >> 5 & 1;
  m->dogfight = f >> 6 & 1;
  m->no_crash = f >> 7 & 1;
  m->diff = get8(p);
  m->hits = get8(p);
  m->lives = get8(p);
  m->tod = get8(p);
  m->weather = get8(p) - 1;
  m->drone = get8(p) % 3;
  gets_(p, m->map, sizeof m->map);
  for (int s = 0; s < 2; s++) {
    m->jets[s] = get8(p);
    for (int k = 0; k < 3; k++) m->load[s][k] = get8(p) - 1;
  }
}

/* ---- polling: the lobby ------------------------------------------------------------------ */
static void send_welcome(void) {
  Buf w;
  head(&w, T_WELCOME);
  put_match(&w, &N.match);
  send_peer(&w);
}

static void start_play(void) {
  N.phase = PH_PLAY;
  N.since_rx = 0;
  N.ev_seq = N.ev_in = N.ev_ack = 0;
  crypto_wipe(N.secret, sizeof N.secret);
  crypto_wipe(N.pend, sizeof N.pend);
}

/* Host: a guest says hello with its half of the handshake; answer with ours. */
static void host_hello(Buf *p, const Addr *from) {
  uint8_t nonce[16], yg[32];
  getn(p, nonce, 16);
  getn(p, yg, 32);
  if (p->bad) return;
  int k = 0, oldest = 0;
  while (k < 4 && !(N.pend[k].used && !memcmp(N.pend[k].nonce, nonce, 16))) k++;
  if (k == 4) { /* a new caller: work out the keys it would share with us */
    for (int i = 0; i < 4; i++)
      if (!N.pend[i].used || N.pend[i].age > N.pend[oldest].age) oldest = i;
    k = oldest;
    uint8_t b[32], g[32], shared[32];
    if (!random_bytes(b, sizeof b)) return;
    code_point(g, N.pin, nonce);
    crypto_x25519(N.pend[k].y, b, g);
    crypto_x25519(shared, b, yg);
    crypto_wipe(b, sizeof b);
    if (!derive(N.pend[k].k_hg, N.pend[k].k_gh, shared, nonce, yg, N.pend[k].y)) return;
    N.pend[k].used = true;
    N.pend[k].from = *from;
    memcpy(N.pend[k].nonce, nonce, 16);
  }
  N.pend[k].age = 0;
  Buf c;
  head(&c, T_CHALLENGE);
  putn(&c, nonce, 16);
  putn(&c, N.pend[k].y, 32);
  send_to(from, &c);
}

/* Host: a sealed JOIN opens with one caller's keys only if it typed our code. */
static bool host_join(Buf *p, int n, const Addr *from) {
  for (int k = 0; k < 4; k++) {
    if (!N.pend[k].used || !same(&N.pend[k].from, from)) continue;
    Buf q = *p;
    uint64_t c;
    if (!unseal(&q, n, N.pend[k].k_gh, &c)) continue;
    memcpy(N.k_tx, N.pend[k].k_hg, 32);
    memcpy(N.k_rx, N.pend[k].k_gh, 32);
    N.keyed = true;
    N.tx_ctr = 0;
    N.rx_top = c;
    N.rx_seen = 1;
    *p = q;
    return true;
  }
  /* nobody's keys open it: a wrong code */
  for (int k = 0; k < 4; k++)
    if (N.pend[k].used && same(&N.pend[k].from, from)) {
      Buf r;
      head(&r, T_REJECT);
      putn(&r, N.pend[k].nonce, 16);
      send_to(from, &r);
      N.pend[k].used = false;
      if (++N.tries >= MAX_TRIES) {
        N.phase = PH_GONE;
        say("%s WRONG CODES TRIED - HOST AGAIN FOR A NEW CODE", "10");
      } else {
        char t[12];
        snprintf(t, sizeof t, "%d", N.tries);
        say("SOMEONE TRIED A WRONG CODE (%s OF 10)", t);
      }
      break;
    }
  return false;
}

NetEvent net_poll(float dt, NetMatch *out) {
  if (N.scanning) lan_poll(dt);
  if (N.phase == PH_IDLE || N.phase == PH_GONE || N.s == BADSOCK) return NETE_NONE;
  N.since_rx += dt;
  for (int k = 0; k < 4; k++)
    if (N.pend[k].used && (N.pend[k].age += dt) > 10) N.pend[k].used = false; /* gave up halfway */
  Buf p;
  /* host: beacons on the LAN, the code and keep-alives to the relay */
  if (N.role == R_HOST && N.phase == PH_WAIT) {
    N.beacon_t -= dt;
    if (N.beacon_t <= 0) {
      N.beacon_t = 1;
      if (N.via_relay) {
        head(&p, N.registered ? T_PING : T_RHOST);
        if (!N.registered) puts_(&p, N.pin, 6);
        send_to(&N.relay, &p);
      } else {
        head(&p, T_BEACON);
        puts_(&p, N.name, 16);
        put32(&p, NET_PORT);
        put8(&p, N.match.drone ? 2 : N.match.coop);
        puts_(&p, N.match.map, 24);
        Addr b = { 0 };
        b.sin_family = AF_INET;
        b.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        b.sin_port = htons(NET_LAN);
        send_to(&b, &p);
      }
    }
  }
  /* guest: find the game, say hello, then join, until welcomed */
  if (N.role == R_GUEST && N.phase == PH_WAIT) {
    N.hello_t -= dt;
    if (N.hello_t <= 0) {
      N.hello_t = 0.5f;
      if (N.via_relay && !N.have_peer) {
        head(&p, T_RJOIN);
        puts_(&p, N.pin, 6);
        send_to(&N.relay, &p);
      } else if (!N.challenged) {
        head(&p, T_HELLO);
        putn(&p, N.nonce, 16);
        putn(&p, N.y, 32);
        send_to(peer_addr(), &p);
      } else {
        head(&p, T_JOIN);
        put8(&p, N.guest_jet);
        for (int k = 0; k < 3; k++) put8(&p, N.guest_load[k] + 1);
        puts_(&p, N.name, 16);
        send_peer(&p);
      }
    }
    if (N.since_rx > 15 && !N.challenged) {
      say(N.via_relay ? "NO GAME WITH CODE %s ON THE RELAY YET - STILL LOOKING" : "NO ANSWER YET - STILL CALLING%s",
          N.via_relay ? N.pin : "");
      N.since_rx = 0;
    }
  }
  NetEvent ev = NETE_NONE;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while (N.phase == PH_WAIT && (n = (int)recvfrom(N.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0) {
    if (n < 5 || p.b[0] != 'A' || p.b[1] != '4' || p.b[2] != 'C') continue;
    if (p.b[3] != VERSION) {
      say("THE OTHER COMPUTER RUNS ANOTHER VERSION OF THE GAME%s", "");
      continue;
    }
    int type = p.b[4];
    p.n = 5;
    p.bad = false;
    bool from_relay = N.via_relay && same(&from, &N.relay);
    /* direct: only the host we called answers */
    if (N.role == R_GUEST && !N.via_relay && !same(&from, &N.peer)) continue;
    if (type == T_RCODE && from_relay && N.role == R_HOST) {
      char c[7];
      gets_(&p, c, 7);
      if (!N.registered && !strcmp(c, N.pin)) {
        N.registered = true;
        say("GAME CODE %s - TELL YOUR FRIEND TO JOIN WITH IT", N.pin);
      }
    } else if (type == T_RFAIL && from_relay) {
      if (N.role == R_HOST) { /* the relay has that code already: pick another */
        if (!N.registered) new_pin();
      } else {
        N.phase = PH_GONE; /* stop asking: the relay counts wrong codes */
        say("THE RELAY HAS NO GAME %s, OR SOMEONE IS PLAYING IT - CHECK THE CODE", N.pin);
      }
    } else if (type == T_RJOIN && from_relay && N.role == R_GUEST) {
      if (!N.have_peer) say("FOUND GAME %s, CHECKING THE CODE...", N.pin);
      N.have_peer = true; /* the relay found the host: now say hello through it */
      N.hello_t = 0;
    } else if (type == T_HELLO && N.role == R_HOST) {
      host_hello(&p, &from);
    } else if (type == T_JOIN && N.role == R_HOST) {
      if (!host_join(&p, n, &from)) continue;
      N.match.jets[1] = get8(&p);
      for (int k = 0; k < 3; k++) N.match.load[1][k] = get8(&p) - 1;
      char who[16];
      gets_(&p, who, sizeof who);
      if (p.bad) {
        forget_keys();
        continue;
      }
      if (!N.via_relay) {
        N.peer = from;
        N.have_peer = true;
      }
      N.match.game++;
      say("%s JOINED", who);
      *out = N.match;
      start_play();
      send_welcome();
      ev = NETE_START;
    } else if (type == T_CHALLENGE && N.role == R_GUEST && !N.challenged) {
      uint8_t nonce[16], yh[32], shared[32];
      getn(&p, nonce, 16);
      getn(&p, yh, 32);
      if (p.bad || memcmp(nonce, N.nonce, 16)) continue;
      crypto_x25519(shared, N.secret, yh);
      if (!derive(N.k_rx, N.k_tx, shared, N.nonce, N.y, yh)) continue;
      N.keyed = N.challenged = true;
      N.hello_t = 0; /* join straight away */
    } else if (type == T_REJECT && N.role == R_GUEST) {
      uint8_t nonce[16];
      getn(&p, nonce, 16);
      if (p.bad || memcmp(nonce, N.nonce, 16)) continue;
      N.phase = PH_GONE;
      say("WRONG GAME CODE - CHECK THE 6 DIGITS THE HOST SEES%s", "");
    } else if (type == T_WELCOME && N.role == R_GUEST && N.challenged) {
      if (!unseal_session(&p, n)) continue;
      get_match(&p, &N.match);
      if (p.bad) continue;
      if (!N.via_relay) N.peer = from;
      N.have_peer = true;
      say("JOINED - THE CONNECTION IS ENCRYPTED%s", "");
      *out = N.match;
      start_play();
      ev = NETE_START;
    } else continue;
    if (!from_relay || type != T_RCODE) N.since_rx = 0;
  }
  /* a host that answered but never let us in: most likely a wrong code */
  if (N.role == R_GUEST && N.phase == PH_WAIT && N.challenged && (N.wait_t += dt) > 8) {
    say("THE HOST DID NOT LET US IN - CHECK THE GAME CODE%s", "");
    N.wait_t = 0;
  }
  if (N.start) {
    N.start = false;
    *out = N.match;
    N.ev_seq = N.ev_in = N.ev_ack = 0;
    return NETE_START;
  }
  if (N.phase == PH_PLAY && N.since_rx > 6) {
    N.phase = PH_GONE;
    say("CONNECTION LOST - THE OTHER COMPUTER LEFT%s", "");
    return NETE_LOST;
  }
  return ev;
}

/* ---- the match: who flies what -------------------------------------------------------------- */
void net_setup(Game *g) {
  int mine = N.role == R_HOST ? 0 : 1;
  /* the drone is seat 1: when the host flies it, the seats swap */
  int seat = N.match.drone == 2 ? 1 - mine : mine;
  g->net = true;
  g->net_side = mine;
  g->nlocals = 0;
  for (int i = 0; i < g->nplanes; i++) {
    Plane *p = &g->planes[i];
    /* the host flies its seat and every CPU jet; the guest its own seat */
    bool own = p->hidx >= 0 ? p->hidx == seat : mine == 0;
    p->remote = !own;
    if (own && p->hidx >= 0) {
      p->hidx = 0; /* our keys and first pad fly it */
      g->locals[g->nlocals++] = i;
    } else if (p->hidx >= 0) p->hidx = -1;
  }
  g->my = g->nlocals ? g->locals[0] : 0;
  /* targets: in co-op the host's, in versus each side its own team's */
  for (int i = 0; i < g->ntargets; i++) g->targets[i].remote = g->coop ? mine != 0 : g->targets[i].team != mine;
  N.send_t = 0;
}

static const char *const DEATHS[] = { "SHOT DOWN", "CRASHED", "HIT THE TANKER", "G-LOC", "MID-AIR COLLISION" };

static void put_event(Buf *p, const Game *g, int k) {
  int kind = g->netev[k].kind;
  put8(p, kind);
  if (kind == NE_FLARES) {
    put8(p, g->netev[k].plane);
    put8(p, g->netev[k].chaff);
    return;
  }
  const Missile *m = &g->netev[k].m;
  put32(p, m->id);
  putv(p, m->pos);
  if (kind == NE_GONE) return;
  putv(p, m->dir);
  putf(p, m->speed);
  putf(p, m->maxSpeed);
  putf(p, m->turn);
  putf(p, m->life);
  putf(p, m->dmg);
  put8(p, m->team);
  put8(p, m->owner + 8);
  put8(p, m->target.kind);
  put8(p, m->target.idx + 1);
  put32(p, m->target.gen);
  put8(p, m->sam | m->ashm << 1);
  put8(p, m->kind);
}

static void take_event(Buf *p, Game *g, bool apply) {
  int kind = get8(p);
  if (kind == NE_FLARES) {
    int pi = get8(p), chaff = get8(p);
    if (apply && pi < g->nplanes && g->planes[pi].remote) sim_remote_flares(g, &g->planes[pi], chaff != 0);
    return;
  }
  Missile m = { 0 };
  m.id = get32(p);
  m.pos = getv(p);
  if (kind == NE_GONE) {
    if (apply) sim_missile_gone(g, m.id, m.pos);
    return;
  }
  m.dir = v3_norm(getv(p));
  m.speed = getf(p);
  m.maxSpeed = getf(p);
  m.turn = getf(p);
  m.life = getf(p);
  m.dmg = getf(p);
  m.team = get8(p) & 1;
  m.owner = get8(p) - 8;
  m.target.kind = (uint8_t)get8(p);
  m.target.idx = (int16_t)(get8(p) - 1);
  m.target.gen = get32(p);
  int f = get8(p);
  m.sam = f & 1;
  m.ashm = f >> 1 & 1;
  m.kind = get8(p);
  if (m.owner >= g->nplanes) m.owner = -1;
  if (m.target.kind == REF_PLANE && (m.target.idx < 0 || m.target.idx >= g->nplanes)) m.target = ref_none();
  if (apply && !p->bad) sim_remote_missile(g, &m);
}

static void send_state(Game *g) {
  Buf p;
  head(&p, T_STATE);
  put32(&p, N.match.game);
  int at = p.n, count = 0;
  put8(&p, 0);
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q->remote) continue;
    count++;
    put8(&p, i);
    putv(&p, q->pos);
    putv(&p, q->f);
    putv(&p, q->u);
    putv(&p, q->vel);
    putf(&p, q->speed);
    putf(&p, q->throttle);
    put8(&p, q->alive | (q->alive && (q->ctl.gun || (q->ctl.fire && q->wsel == W_CANNON)) && q->ammo >= 1) << 1 |
                 (q->invuln > 0) << 2);
    putf(&p, q->health);
    put8(&p, q->lives);
    put8(&p, q->missiles);
    put8(&p, q->flares);
    int d = 0;
    for (int k = 0; k < 5; k++)
      if (q->deathMsg && !strcmp(q->deathMsg, DEATHS[k])) d = k;
    put8(&p, d);
    for (int k = 0; k < MAX_PLANES; k++) put8(&p, q->kc[k]);
  }
  patch8(&p, at, count);
  /* our targets that have fallen */
  at = p.n;
  count = 0;
  put8(&p, 0);
  for (int i = 0; i < g->ntargets; i++)
    if (!g->targets[i].remote && !g->targets[i].alive) {
      put8(&p, i);
      put8(&p, g->targets[i].by + 1);
      count++;
    }
  patch8(&p, at, count);
  /* the score each jet earned here */
  put8(&p, g->nplanes);
  for (int i = 0; i < g->nplanes; i++) put32(&p, (uint32_t)g->planes[i].score);
  putf(&p, N.role == R_HOST ? g->tanker.th : -1);
  /* the events, the newest 16, numbered so each is used once */
  for (int k = 0; k < g->nnetev; k++) {
    int slot = (int)(N.ev_seq % 16);
    N.ev[slot].seq = ++N.ev_seq;
    N.ev[slot].b.n = 0;
    put_event(&N.ev[slot].b, g, k);
  }
  g->nnetev = 0;
  /* what we have of theirs, then theirs not yet had, oldest first, while they fit */
  put32(&p, N.ev_in);
  uint32_t first = N.ev_ack + 1;
  if (N.ev_seq > 16 && first < N.ev_seq - 15) first = N.ev_seq - 15;
  at = p.n;
  count = 0;
  put8(&p, 0);
  for (uint32_t s = first; s <= N.ev_seq; s++) {
    const Buf *e = &N.ev[(s - 1) % 16].b;
    if (p.n + 4 + e->n > (int)sizeof p.b - SEAL_EXTRA) break;
    put32(&p, s);
    for (int i = 0; i < e->n; i++) put8(&p, e->b[i]);
    count++;
  }
  patch8(&p, at, count);
  send_peer(&p);
}

static void take_state(Game *g, Buf *p) {
  if (get32(p) != N.match.game) return;
  int np = get8(p);
  for (int k = 0; k < np && !p->bad; k++) {
    int i = get8(p);
    vec3 pos = getv(p), f = getv(p), u = getv(p), vel = getv(p);
    float speed = getf(p), thr = getf(p);
    int flags = get8(p);
    float hp = getf(p);
    int lives = get8(p), msl = get8(p), flr = get8(p), d = get8(p);
    int kc[MAX_PLANES];
    for (int j = 0; j < MAX_PLANES; j++) kc[j] = get8(p);
    if (p->bad || i >= g->nplanes || !g->planes[i].remote) continue;
    Plane *q = &g->planes[i];
    q->snap.pos = pos;
    q->snap.f = v3_norm(f);
    q->snap.u = v3_norm(u);
    q->snap.vel = v3_len(vel) < 600 ? vel : v3_scale(q->snap.f, speed);
    q->snap.speed = speed;
    q->snap.thr = thr;
    q->snap.age = 0;
    q->snap.have = true;
    q->health = hp;
    q->lives = lives;
    q->missiles = msl;
    q->flares = flr;
    q->netFire = flags >> 1 & 1;
    q->invuln = flags >> 2 & 1 ? 1 : 0;
    memcpy(q->kc, kc, sizeof kc);
    q->ammo = MAX_AMMO;
    q->fuel = 100;
    if ((flags & 1) && !q->alive) sim_remote_up(g, q);
    else if (!(flags & 1) && q->alive) sim_remote_down(g, q, DEATHS[d < 5 ? d : 0]);
  }
  int nt = get8(p);
  for (int k = 0; k < nt && !p->bad; k++) {
    int i = get8(p), by = get8(p) - 1;
    if (i < g->ntargets && g->targets[i].remote && g->targets[i].alive) sim_target_fall(g, &g->targets[i], by);
  }
  int ns = get8(p);
  for (int i = 0; i < ns && !p->bad; i++) {
    int sc = (int)get32(p);
    if (i < g->nplanes && !p->bad) g->planes[i].netScore = sc;
  }
  float th = getf(p);
  if (N.role == R_GUEST && th >= 0) {
    g->tanker.th = th;
    sim_update_tanker(g, 0);
  }
  uint32_t ack = get32(p);
  if (ack > N.ev_ack && ack <= N.ev_seq) N.ev_ack = ack;
  int ne = get8(p);
  for (int k = 0; k < ne && !p->bad; k++) {
    uint32_t seq = get32(p);
    take_event(p, g, seq > N.ev_in);
    if (seq > N.ev_in) N.ev_in = seq;
  }
}

void net_tick(Game *g, float dt) {
  if (N.phase != PH_PLAY || N.s == BADSOCK) return;
  Buf p;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while ((n = (int)recvfrom(N.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0) {
    if (n < 5 || p.b[0] != 'A' || p.b[1] != '4' || p.b[2] != 'C' || p.b[3] != VERSION) continue;
    if (!N.via_relay && N.have_peer && !same(&from, &N.peer)) continue;
    int type = p.b[4];
    /* in a match only sealed packets count; anything else is dropped */
    if (!sealed_type(type) || !unseal_session(&p, n)) {
      N.rx_refused++;
      continue;
    }
    N.rx_ok++;
    N.since_rx = 0;
    if (type == T_STATE) take_state(g, &p);
    else if (type == T_BYE) N.since_rx = 99;
    else if (type == T_WELCOME && N.role == R_GUEST) {
      NetMatch m;
      get_match(&p, &m);
      if (!p.bad && m.game != N.match.game) { /* the host started a rematch */
        N.match = m;
        N.start = true;
        say("REMATCH%s", "");
        return;
      }
    } else if (type == T_JOIN && N.role == R_HOST) send_welcome(); /* the guest has not heard our welcome yet */
  }
  N.send_t -= dt;
  if (N.send_t <= 0) {
    N.send_t = 1.0f / 30;
    if (N.role == R_HOST && g->time < 3) send_welcome(); /* until the guest is surely in */
    send_state(g);
  }
}
