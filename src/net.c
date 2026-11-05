/* net.c - network play over UDP (see net.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* SO_REUSEPORT */
#endif
#include "net.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "assets.h"
#include "core/jets.h"
#include "core/sim.h"
#include "core/world.h"
#include "core/crypto.h"
#include "platform/plat.h"
#include "core/safe.h"

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
#ifdef SO_REUSEPORT
  if (shared) setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof one);   /* macOS and the BSDs want this too */
#endif
  if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
  Addr a = { 0 };
  a.sin_family = AF_INET;
  /* test hook: A4C_PLANES with A4C_NET_LOOPBACK=1 listens on this computer only (two copies of the game on one) */
  a.sin_addr.s_addr = htonl(getenv("A4C_PLANES") && getenv("A4C_NET_LOOPBACK") ? INADDR_LOOPBACK : INADDR_ANY);
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
    port = (int)safe_int(colon + 1, 0, INT_MIN, INT_MAX);
    *colon = 0;
  }
  struct addrinfo hints = { 0 }, *res = NULL;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (!host[0] || getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
  if (res->ai_addrlen < sizeof *out) { /* not an IPv4 address after all */
    freeaddrinfo(res);
    return false;
  }
  memcpy(out, res->ai_addr, sizeof *out); /* a copy: ai_addr's bytes need not be aligned for sockaddr_in */
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
 * then on every match packet is encrypted and signed (ChaCha20-Poly1305,
 * our own, core/crypto.c) and numbered, so it cannot be read, changed or replayed. A
 * wrong guess costs a full round trip, and the host gives up after 10. */
#define VERSION 10 /* relay/relay.c checks it too: change both together (4: our own crypto, 5: the host's verdict,
                     6: modern rules fly the hangar's loadout, and the sy26 has no gun; 7: the two sixes met, so a game
                     of either six would play the same match differently; 8: CPU pilots pick their own loadout and use
                     radar and air-to-ground missiles; 9: realistic handling's gear and flaps, in the snapshots and
                     the watch stream; 10: each side's called tanker, the host's to fly) */
enum {
  T_BEACON = 1, T_HELLO, T_WELCOME, T_STATE, T_BYE, T_RHOST, T_RCODE, T_RJOIN, T_RFAIL, T_PING,
  T_CHALLENGE, T_JOIN, T_REJECT
};
/* the types that travel encrypted */
static bool sealed_type(int t) { return t == T_WELCOME || t == T_STATE || t == T_BYE || t == T_JOIN; }
#define SEAL_EXTRA (8 + 16) /* the packet number and the signature */
#define MAX_TRIES  10       /* wrong codes before the host stops */

/* A packet: written up to n, or read from n up to end (what came in); a read past end sets bad and gives 0s,
 * never the bytes an earlier packet left in b. */
typedef struct { uint8_t b[1400]; int n, end; bool bad; } Buf;

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
  if (p->n >= p->end || p->n >= (int)sizeof p->b) {
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
static void getn(Buf *p, uint8_t *dst, int n) {
  if (p->n + n > p->end || p->n + n > (int)sizeof p->b) {
    p->bad = true;
    memset(dst, 0, (size_t)n);
    return;
  }
  memcpy(dst, p->b + p->n, (size_t)n);
  p->n += n;
}
static void gets_(Buf *p, char *s, int n) {
  getn(p, (uint8_t *)s, n);
  s[n - 1] = 0;
}
static void putn(Buf *p, const uint8_t *src, int n) {
  for (int i = 0; i < n; i++) put8(p, src[i]);
}
static void head(Buf *p, int type) {
  p->n = p->end = 0;
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
  struct { bool used; Addr from; uint8_t nonce[16], y[32], k_hg[32], k_gh[32]; float age; int fails; } pend[4];
  int      tries; /* wrong codes so far */
  int      refused; /* a relay host: codes the relay refused in a row */
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
  /* keeping pace (net_tick): each side sends a state packet every 1/30 s of its own game time, so the packets
   * each side has sent in this match measure how far its game has run. pace_sent: ours; pace_base: the other
   * side's packet number when the match began here; pace_welc: the host's welcomes since, which are not states;
   * hold: the host waits for a guest too far behind; behind: the guest knows it is (both show it) */
  uint32_t pace_sent, pace_welc;
  uint64_t pace_base;
  bool     hold, behind;
  float    hold_t, told_t; /* told_t: the guest heard the host is waiting for it, this long ago at most */
  int      holds;   /* times the host waited, and for how long in all (the test log) */
  float    held_s;
} N = { .s = BADSOCK, .lan = BADSOCK };

A4C_PRINTF(1, 2) static void say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(N.status, sizeof N.status, fmt, ap);
  va_end(ap);
  plat_log("net: %s", N.status);
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
  memcpy(msg, "A4C-CPACE-2", 11); /* 2: our own crypto (core/crypto.c) */
  memcpy(msg + 11, pin, 6);
  memcpy(msg + 17, nonce, 16);
  cr_blake2b(h, 32, NULL, 0, msg, sizeof msg);
  cr_elligator2(g, h);
  cr_wipe(h, sizeof h);
}

/* The two keys from the shared point and everything both sides sent. */
static bool derive(uint8_t k_hg[32], uint8_t k_gh[32], uint8_t shared[32], const uint8_t nonce[16], const uint8_t yg[32],
                   const uint8_t yh[32]) {
  static const uint8_t zero[32];
  if (cr_same32(shared, zero)) return false; /* a broken point from the other side */
  uint8_t t[16 + 32 + 32], k[64];
  memcpy(t, nonce, 16);
  memcpy(t + 16, yg, 32);
  memcpy(t + 48, yh, 32);
  cr_blake2b(k, 64, shared, 32, t, sizeof t);
  memcpy(k_hg, k, 32);
  memcpy(k_gh, k + 32, 32);
  cr_wipe(k, sizeof k);
  cr_wipe(shared, 32);
  return true;
}

static bool random_bytes(void *b, size_t n) {
  if (plat_random(b, n)) return true;
  say("NO SECURE RANDOM NUMBERS ON THIS COMPUTER%s", "");
  return false;
}

/* Encrypts p (header and body) under key with packet number ctr and sends it. */
static void seal_to(const Addr *a, const Buf *p, const uint8_t key[32], uint64_t ctr) {
  uint8_t out[sizeof p->b + SEAL_EXTRA], nonce[12] = { 0 };
  int body = p->n - 5;
  memcpy(out, p->b, 5);
  for (int i = 0; i < 8; i++) out[5 + i] = (uint8_t)(ctr >> (8 * i));
  memcpy(nonce + 4, out + 5, 8); /* the 96-bit nonce: 4 zero bytes and the packet number (each direction has its own key) */
  cr_aead_seal(out + 13, out + 13 + body, key, nonce, out, 13, p->b + 5, (size_t)body);
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
  uint8_t plain[sizeof p->b], nonce[12] = { 0 };
  int body = n - 13 - 16;
  memcpy(nonce + 4, p->b + 5, 8);
  if (!cr_aead_open(plain, p->b + 13 + body, key, nonce, p->b, 13, p->b + 13, (size_t)body)) return false;
  *ctr = 0;
  for (int i = 0; i < 8; i++) *ctr |= (uint64_t)p->b[5 + i] << (8 * i);
  memcpy(p->b + 5, plain, (size_t)body);
  p->n = 5;
  p->end = 5 + body;
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
  cr_wipe(N.secret, sizeof N.secret);
  cr_wipe(N.k_tx, sizeof N.k_tx);
  cr_wipe(N.k_rx, sizeof N.k_rx);
  cr_wipe(N.pend, sizeof N.pend);
  N.keyed = N.challenged = false;
  N.tx_ctr = N.rx_top = N.rx_seen = 0;
}

static bool new_pin(void) {
  /* test hook (tools/playtest): A4C_PLANES with A4C_TEST_CODE=NNNNNN hosts with that code, so a test on two
   * computers knows it in advance */
  const char *t = getenv("A4C_PLANES") ? getenv("A4C_TEST_CODE") : NULL;
  if (t && strlen(t) == 6 && strspn(t, "0123456789") == 6) {
    memcpy(N.pin, t, 7);
    return true;
  }
  uint32_t v;
  do {
    if (!random_bytes(&v, sizeof v)) return false;
  } while (v >= 4294000000u); /* no bias toward low codes */
  snprintf(N.pin, sizeof N.pin, "%06u", (unsigned)(v % 1000000u));
  return true;
}

bool net_init(void) { return true; }

/* A new match here: from now on each side's packets count its game time in it. */
static void pace_reset(void) {
  N.pace_sent = N.pace_welc = 0;
  N.pace_base = N.rx_top;
  N.hold = N.behind = false;
  N.hold_t = N.told_t = 0;
}

void net_close(void) {
  if (N.rx_ok + N.rx_refused && getenv("A4C_PLANES"))
    plat_log("net: %d packets taken, %d refused", N.rx_ok, N.rx_refused);
  if (N.holds && getenv("A4C_PLANES"))
    plat_log("net: waited %d times for the other computer, %.1f s in all", N.holds, (double)N.held_s);
  N.rx_ok = N.rx_refused = 0;
  N.holds = 0;
  N.held_s = 0;
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
  N.tries = N.refused = 0;
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

/* Tests: A4C_NET_PORT=N hosts on UDP N instead of NET_PORT and sends no LAN beacons, so test matches stay
 * off the ports real games and other test runs use. */
static int host_port(void) {
  const char *e = getenv("A4C_NET_PORT");
  int p = e ? (int)safe_int(e, 0, INT_MIN, INT_MAX) : 0;
  return p > 1024 && p < 65536 ? p : NET_PORT;
}

bool net_host(const NetMatch *m, const char *name, const char *relay) {
  net_close();
  N.match = *m;
  snprintf(N.name, sizeof N.name, "%s", name);
  N.via_relay = relay && *relay;
  N.s = udp_open(N.via_relay ? 0 : host_port(), true, false);
  if (N.s == BADSOCK) {
    char port[8];
    snprintf(port, sizeof port, "%d", host_port());
    say("CAN'T OPEN UDP PORT %s - IS ANOTHER GAME HOSTING?", port);
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
  if (getenv("A4C_PLANES")) plat_log("net: game code %s", N.pin); /* tests only: the code is a secret */
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
  cr_x25519(N.y, N.secret, g);
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

/* One packet on the LAN port: a game's beacon goes in the list of games heard. */
static void lan_packet(Buf *p, int n, const Addr *sender) {
  Addr from = *sender;
  p->n = 0;
  p->end = n;
  p->bad = false;
  if (n < 6 || p->b[0] != 'A' || p->b[1] != '4' || p->b[2] != 'C' || p->b[3] != VERSION || p->b[4] != T_BEACON) return;
  p->n = 5;
  char name[16], map[24], addr[32];
  gets_(p, name, sizeof name);
  int port = (int)get32(p), coop = get8(p);
  gets_(p, map, sizeof map);
  if (p->bad) return; /* a short beacon */
  from.sin_port = htons((uint16_t)port);
  addr_text(&from, addr, sizeof addr);
  int k = 0;
  while (k < N.nlan && strcmp(N.lan_games[k].addr, addr)) k++;
  if (k == N.nlan) {
    if (N.nlan == 8) return;
    N.nlan++;
  }
  snprintf(N.lan_games[k].name, sizeof N.lan_games[k].name, "%.12s %s", name, coop == 2 ? "DRONE" : coop ? "CO-OP" : "VS");
  snprintf(N.lan_games[k].addr, sizeof N.lan_games[k].addr, "%s", addr);
  N.lan_games[k].age = 0;
}

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
  while ((n = (int)recvfrom(N.lan, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0) lan_packet(&p, n, &from);
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
  pace_reset();
  cr_wipe(N.secret, sizeof N.secret);
  cr_wipe(N.pend, sizeof N.pend);
}

/* Host: a guest says hello with its half of the handshake; answer with ours. */
static void host_hello(Buf *p, const Addr *from) {
  uint8_t nonce[16], yg[32];
  getn(p, nonce, 16);
  getn(p, yg, 32);
  if (p->bad) return;
  int k = 0;
  while (k < 4 && !(N.pend[k].used && !memcmp(N.pend[k].nonce, nonce, 16))) k++;
  if (k == 4) { /* a new caller: work out the keys it would share with us */
    /* its own slot if it has one (a caller holds one handshake), else a free one, else the oldest that has had
     * two seconds to finish; none: dropped before any work, so a flood neither costs two X25519 a packet nor
     * pushes out a guest half way in */
    int take = -1;
    for (int i = 0; i < 4 && take < 0; i++)
      if (N.pend[i].used && same(&N.pend[i].from, from)) take = i;
    for (int i = 0; i < 4 && take < 0; i++)
      if (!N.pend[i].used) take = i;
    for (int i = 0; i < 4 && take < 0; i++)
      if (N.pend[i].age >= 2) take = i;
    for (int i = 0; i < 4 && take >= 0 && N.pend[take].used && !same(&N.pend[take].from, from); i++)
      if (N.pend[i].age > N.pend[take].age) take = i;
    if (take < 0) return;
    k = take;
    uint8_t b[32], g[32], shared[32];
    if (!random_bytes(b, sizeof b)) return;
    code_point(g, N.pin, nonce);
    cr_x25519(N.pend[k].y, b, g);
    cr_x25519(shared, b, yg);
    cr_wipe(b, sizeof b);
    if (!derive(N.pend[k].k_hg, N.pend[k].k_gh, shared, nonce, yg, N.pend[k].y)) return;
    N.pend[k].used = true;
    N.pend[k].fails = 0;
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
      /* through the relay every caller has the relay's address, and the JOIN may be a stale one from the guest
       * before: only a second failure in this handshake (a wrong-code guest sends one every half second) is
       * this caller's */
      if (N.via_relay && ++N.pend[k].fails < 2) break;
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

/* One packet while hosting or joining: the relay's answers, the handshake, and the welcome. */
static void wait_packet(Buf *p, int n, const Addr *from, NetMatch *out, NetEvent *ev) {
  if (n < 5 || p->b[0] != 'A' || p->b[1] != '4' || p->b[2] != 'C') return;
  if (p->b[3] != VERSION) { /* a game (or a relay) from another build: say so, and which is newer */
    char m[16];
    snprintf(m, sizeof m, "%d", p->b[3]);
    char was[sizeof N.status];
    memcpy(was, N.status, sizeof was);
    if (p->b[3] > VERSION) snprintf(N.status, sizeof N.status, "THE OTHER SIDE HAS A NEWER VERSION (%s) - UPDATE THIS GAME TO PLAY", m);
    else snprintf(N.status, sizeof N.status, "THE OTHER SIDE HAS AN OLDER VERSION (%s) - IT NEEDS THE UPDATE TO PLAY", m);
    if (strcmp(was, N.status)) plat_log("net: %s", N.status); /* once, not every packet */
    /* and five bytes of ours back, at most twice a second, so that game tells its player too */
    static uint64_t last;
    if (plat_ms() - last >= 500) {
      last = plat_ms();
      const uint8_t r[5] = { 'A', '4', 'C', VERSION, p->b[4] };
      send_raw(from, r, 5);
    }
    return;
  }
  int type = p->b[4];
  p->n = 5;
  p->end = n;
  p->bad = false;
  bool from_relay = N.via_relay && same(from, &N.relay);
  /* direct: only the host we called answers */
  if (N.role == R_GUEST && !N.via_relay && !same(from, &N.peer)) return;
  if (type == T_RCODE && from_relay && N.role == R_HOST) {
    char c[7]; /* the relay sends the 6 digits and no 0 after them */
    getn(p, (uint8_t *)c, 6);
    c[6] = 0;
    if (!p->bad && !N.registered && !strcmp(c, N.pin)) {
      N.registered = true;
      N.refused = 0;
      say("GAME CODE %s - TELL YOUR FRIEND TO JOIN WITH IT", N.pin);
    }
  } else if (type == T_RFAIL && from_relay) {
    if (N.role == R_HOST) { /* the relay has that code already (after it lost ours, too): pick another */
      if (N.registered) say("THE RELAY LOST THE GAME - REGISTERING AGAIN%s", "");
      N.registered = false;
      new_pin();
      /* a code taken by chance is one in a thousand or less: several in a row is the relay's limit of games
       * from one address (it answers both the same way), so say so and ask less often */
      if (++N.refused >= 4) {
        say("THE RELAY TAKES %s GAMES FROM ONE ADDRESS - WAITING FOR ONE TO END", "4");
        N.beacon_t = 5;
      }
    } else {
      N.phase = PH_GONE; /* stop asking: the relay counts wrong codes */
      say("THE RELAY HAS NO GAME %s, OR SOMEONE IS PLAYING IT - CHECK THE CODE", N.pin);
    }
  } else if (type == T_RJOIN && from_relay && N.role == R_GUEST) {
    if (!N.have_peer) say("FOUND GAME %s, CHECKING THE CODE...", N.pin);
    N.have_peer = true; /* the relay found the host: now say hello through it */
    N.hello_t = 0;
  } else if (type == T_HELLO && N.role == R_HOST) {
    host_hello(p, from);
  } else if (type == T_JOIN && N.role == R_HOST) {
    if (!host_join(p, n, from)) return;
    N.match.jets[1] = get8(p);
    for (int k = 0; k < 3; k++) N.match.load[1][k] = get8(p) - 1;
    char who[16];
    gets_(p, who, sizeof who);
    if (p->bad) {
      forget_keys();
      return;
    }
    if (!N.via_relay) {
      N.peer = *from;
      N.have_peer = true;
    }
    N.match.game++;
    say("%s JOINED", who);
    *out = N.match;
    start_play();
    send_welcome();
    *ev = NETE_START;
  } else if (type == T_CHALLENGE && N.role == R_GUEST && !N.challenged) {
    uint8_t nonce[16], yh[32], shared[32];
    getn(p, nonce, 16);
    getn(p, yh, 32);
    if (p->bad || memcmp(nonce, N.nonce, 16)) return;
    cr_x25519(shared, N.secret, yh);
    if (!derive(N.k_rx, N.k_tx, shared, N.nonce, N.y, yh)) return;
    N.keyed = N.challenged = true;
    N.hello_t = 0; /* join straight away */
  } else if (type == T_REJECT && N.role == R_GUEST) {
    uint8_t nonce[16];
    getn(p, nonce, 16);
    if (p->bad || memcmp(nonce, N.nonce, 16)) return;
    N.phase = PH_GONE;
    say("WRONG GAME CODE - CHECK THE 6 DIGITS THE HOST SEES%s", "");
  } else if (type == T_WELCOME && N.role == R_GUEST && N.challenged) {
    if (!unseal_session(p, n)) return;
    get_match(p, &N.match);
    if (p->bad) return;
    if (!N.via_relay) N.peer = *from;
    N.have_peer = true;
    say("JOINED - THE CONNECTION IS ENCRYPTED%s", "");
    *out = N.match;
    start_play();
    *ev = NETE_START;
  } else return;
  if (!from_relay || type != T_RCODE) N.since_rx = 0;
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
        /* the code every second, not a ping: a relay that restarted or dropped the game (a minute without our
         * packets) ignores pings from a game it doesn't know, and takes the code again */
        head(&p, T_RHOST);
        puts_(&p, N.pin, 6);
        send_to(&N.relay, &p);
      } else if (host_port() == NET_PORT) {
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
  while (N.phase == PH_WAIT && (n = (int)recvfrom(N.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0)
    wait_packet(&p, n, &from, out, &ev);
  /* a host that answered but never let us in: most likely a wrong code */
  if (N.role == R_GUEST && N.phase == PH_WAIT && N.challenged && (N.wait_t += dt) > 8) {
    say("THE HOST DID NOT LET US IN - CHECK THE GAME CODE%s", "");
    N.wait_t = 0;
  }
  if (N.start) {
    N.start = false;
    *out = N.match;
    N.ev_seq = N.ev_in = N.ev_ack = 0;
    pace_reset();
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
  if (kind == NE_TANKER) { /* the guest's call: the host flies it */
    put8(p, g->netev[k].plane);
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

static bool finite3(vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

/* One of the other computer's events. Every field is checked against what it can be here (a jet, a target or a
 * decoy that is there, a missile kind, a finite number); an event that fails is dropped. */
static void take_event(Buf *p, Game *g, bool apply) {
  int kind = get8(p);
  if (kind == NE_FLARES) {
    int pi = get8(p), chaff = get8(p);
    if (apply && !p->bad && pi < g->nplanes && g->planes[pi].remote) sim_remote_flares(g, &g->planes[pi], chaff != 0);
    return;
  }
  if (kind != NE_MISSILE && kind != NE_GONE) { /* not one we know: its length is unknown too, so nothing after it */
    p->bad = true;
    return;
  }
  if (kind == NE_TANKER) {
    int pi = get8(p);
    if (apply && N.role == R_HOST && pi < g->nplanes && g->planes[pi].remote) sim_call_tanker(g, &g->planes[pi]);
    return;
  }
  Missile m = { 0 };
  m.id = get32(p);
  m.pos = getv(p);
  if (kind == NE_GONE) {
    if (apply && !p->bad && finite3(m.pos)) sim_missile_gone(g, m.id, m.pos);
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
  if (m.owner >= g->nplanes || m.owner < BY_CARRIER) m.owner = -1; /* a jet, the SAMs or the carrier */
  if (m.target.kind == REF_PLANE && (m.target.idx < 0 || m.target.idx >= g->nplanes)) m.target = ref_none();
  if (m.target.kind == REF_GROUND && (m.target.idx < 0 || m.target.idx >= g->ntargets)) m.target = ref_none();
  if (m.target.kind == REF_FLARE && (m.target.idx < 0 || m.target.idx >= MAX_FLARES)) m.target = ref_none();
  if (m.target.kind > REF_CARRIER) m.target = ref_none();
  bool ok = m.kind >= MK_HEAT && m.kind <= MK_GROUND && finite3(m.pos) && finite3(m.dir) && isfinite(m.speed) &&
            isfinite(m.maxSpeed) && isfinite(m.turn) && isfinite(m.life) && isfinite(m.dmg);
  if (apply && !p->bad && ok) sim_remote_missile(g, &m);
}

/* Our state packet: our jets, our fallen targets, the scores, the verdict and the events not yet had. */
static void state_packet(Game *g, Buf *p) {
  head(p, T_STATE);
  put32(p, N.match.game);
  int at = p->n, count = 0;
  put8(p, 0);
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    if (q->remote) continue;
    count++;
    put8(p, i);
    putv(p, q->pos);
    putv(p, q->f);
    putv(p, q->u);
    putv(p, q->vel);
    putf(p, q->speed);
    putf(p, q->throttle);
    put8(p, q->alive | (q->alive && (q->ctl.gun || (q->ctl.fire && q->wsel == W_CANNON)) && q->ammo >= 1) << 1 |
                 (q->invuln > 0) << 2);
    put8(p, q->gearDown | q->flaps << 1); /* the gear and flap levers: the wheels and flaps move here too */
    putf(p, q->health);
    put8(p, q->lives);
    put8(p, q->missiles);
    put8(p, q->flares);
    int d = 0;
    for (int k = 0; k < 5; k++)
      if (q->deathMsg && !strcmp(q->deathMsg, DEATHS[k])) d = k;
    put8(p, d);
    for (int k = 0; k < MAX_PLANES; k++) put8(p, q->kc[k]);
  }
  patch8(p, at, count);
  /* our targets that have fallen */
  at = p->n;
  count = 0;
  put8(p, 0);
  for (int i = 0; i < g->ntargets; i++)
    if (!g->targets[i].remote && !g->targets[i].alive) {
      put8(p, i);
      put8(p, g->targets[i].by + 1);
      count++;
    }
  patch8(p, at, count);
  /* the score each jet earned here */
  put8(p, g->nplanes);
  for (int i = 0; i < g->nplanes; i++) put32(p, (uint32_t)g->planes[i].score);
  /* the tankers, the host's to fly: each one's state, time in it, damage and caller, and whose calls are spent */
  put8(p, N.role == R_HOST);
  if (N.role == R_HOST) {
    for (int t = 0; t < 2; t++) {
      const Tanker *T = &g->tankers[t];
      put8(p, T->state);
      put8(p, T->owner + 1);
      putf(p, T->t);
      putf(p, T->th);
      putf(p, T->hp);
      putv(p, T->pos);
    }
    for (int i = 0; i < g->nplanes; i++) put8(p, g->planes[i].tcalled | g->planes[i].tlost << 1);
  }
  /* the host decides how a match ends (two last jets can go down on the two computers within a packet of
   * each other, and each would see the other side win): 0 while it runs, then the winner + 2 and the reason */
  bool verdict = N.role == R_HOST && g->over;
  put8(p, verdict ? g->winner + 2 : 0);
  if (verdict) puts_(p, g->reason, (int)sizeof g->reason);
  /* the events, the newest 16, numbered so each is used once */
  for (int k = 0; k < g->nnetev; k++) {
    int slot = (int)(N.ev_seq % 16);
    N.ev[slot].seq = ++N.ev_seq;
    N.ev[slot].b.n = 0;
    put_event(&N.ev[slot].b, g, k);
  }
  g->nnetev = 0;
  /* what we have of theirs, then theirs not yet had, oldest first, while they fit */
  put32(p, N.ev_in);
  uint32_t first = N.ev_ack + 1;
  if (N.ev_seq > 16 && first < N.ev_seq - 15) first = N.ev_seq - 15;
  at = p->n;
  count = 0;
  put8(p, 0);
  for (uint32_t s = first; s <= N.ev_seq; s++) {
    const Buf *e = &N.ev[(s - 1) % 16].b;
    if (p->n + 4 + e->n > (int)sizeof p->b - SEAL_EXTRA) break;
    put32(p, s);
    for (int i = 0; i < e->n; i++) put8(p, e->b[i]);
    count++;
  }
  patch8(p, at, count);
}

static void send_state(Game *g) {
  Buf p;
  state_packet(g, &p);
  send_peer(&p);
  N.pace_sent++;
}

static void take_state(Game *g, Buf *p) {
  if (get32(p) != N.match.game) return;
  int np = get8(p);
  for (int k = 0; k < np && !p->bad; k++) {
    int i = get8(p);
    vec3 pos = getv(p), f = getv(p), u = getv(p), vel = getv(p);
    float speed = getf(p), thr = getf(p);
    int flags = get8(p), gf = get8(p);
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
    q->gearDown = gf & 1;
    q->flaps = (int8_t)((gf >> 1 & 3) % 3);
    q->health = hp;
    q->lives = lives;
    if (msl > q->missiles) { /* rearming: its radar and air-to-ground missiles come back with the heat-seekers */
      int full[4];
      game_full_load(g, q, full);
      if (q->radar < full[1]) q->radar++;
      if (q->ground < full[2]) q->ground++;
    }
    q->missiles = msl;
    q->flares = flr;
    q->netFire = flags >> 1 & 1;
    q->invuln = flags >> 2 & 1 ? 1 : 0;
    memcpy(q->kc, kc, sizeof kc);
    q->ammo = plane_has_gun(q) ? MAX_AMMO : 0; /* a jet without a gun carries no rounds */
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
  if (get8(p)) {
    for (int t = 0; t < 2 && !p->bad; t++) {
      Tanker *T = &g->tankers[t];
      int st = get8(p), ow = get8(p) - 1;
      float tt = getf(p), th = getf(p), hp = getf(p);
      vec3 pos = getv(p);
      /* a state, caller and numbers the game can hold: anything else (a forged or broken packet) is dropped */
      bool ok = st <= TK_DOWN && ow >= -1 && ow < MAX_PLANES && isfinite(tt) && isfinite(th) && isfinite(hp) && finite3(pos);
      if (N.role != R_GUEST || p->bad || !ok) continue;
      T->state = (uint8_t)st;
      T->owner = (int8_t)ow;
      T->t = tt;
      T->th = th;
      T->hp = hp;
      T->pos = pos; /* a tanker on its way home moves on from where it is */
    }
    for (int i = 0; i < ns && !p->bad; i++) {
      int f = get8(p);
      if (N.role == R_GUEST && i < g->nplanes) g->planes[i].tcalled = f & 1, g->planes[i].tlost = f >> 1 & 1;
    }
    if (N.role == R_GUEST) sim_update_tanker(g, 0);
  }
  int verdict = get8(p);
  if (verdict) {
    char why[sizeof g->reason];
    gets_(p, why, (int)sizeof why);
    if (N.role == R_GUEST && !g->over && !p->bad && verdict <= 3) {
      g->over = true;
      g->overT = 3.2f;
      g->winner = verdict - 2;
      memcpy(g->reason, why, sizeof why);
    }
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

/* One packet in a match; false when the host started a rematch (nothing more this tick). */
static bool play_packet(Game *g, Buf *p, int n, const Addr *from) {
  if (n < 5 || p->b[0] != 'A' || p->b[1] != '4' || p->b[2] != 'C' || p->b[3] != VERSION) return true;
  if (!N.via_relay && N.have_peer && !same(from, &N.peer)) return true;
  int type = p->b[4];
  /* in a match only sealed packets count; anything else is dropped */
  if (!sealed_type(type) || !unseal_session(p, n)) {
    N.rx_refused++;
    return true;
  }
  N.rx_ok++;
  N.since_rx = 0;
  if (type == T_STATE) take_state(g, p);
  else if (type == T_BYE) N.since_rx = 99;
  else if (type == T_WELCOME && N.role == R_GUEST) {
    NetMatch m;
    get_match(p, &m);
    if (!p->bad && m.game != N.match.game) { /* the host started a rematch */
      N.match = m;
      N.start = true;
      say("REMATCH%s", "");
      return false;
    }
    N.pace_welc++; /* the same match: it moves the host's packet numbers on, but not its game */
    if (g->time > 5) N.told_t = 1; /* past the match's first welcomes: the host is waiting for us */
  } else if (type == T_JOIN && N.role == R_HOST) send_welcome(); /* the guest has not heard our welcome yet */
  return true;
}

/* How far, in state packets (1/30 s of game time each), the other side's game in this match is behind ours. */
static long pace_lag(void) {
  uint64_t theirs = N.rx_top < N.pace_base ? 0 : N.rx_top - N.pace_base;
  if (theirs > (1u << 30)) theirs = 1u << 30;
  return (long)N.pace_sent - ((long)theirs - (long)N.pace_welc);
}

/* A slow computer runs several ticks a frame to keep up (main.c). One that still falls behind: the host waits
 * for a guest more than a second behind, until it is within 0.4 s, and both screens say so (net_pace_note). A
 * slow host needs nothing: its guest follows its verdicts and rematches whenever they come. */
#define HOLD_AT   30
#define HOLD_TILL 12

bool net_holding(void) { return N.phase == PH_PLAY && N.hold; }

const char *net_pace_note(void) {
  if (N.phase != PH_PLAY) return NULL;
  if (N.hold) return "WAITING FOR THE OTHER PLAYER";
  return N.behind || N.told_t > 0 ? "THE OTHER PLAYER IS WAITING - CATCHING UP" : NULL;
}

void net_tick(Game *g, float dt) {
  if (N.phase != PH_PLAY || N.s == BADSOCK) return;
  Buf p;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while ((n = (int)recvfrom(N.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0)
    if (!play_packet(g, &p, n, &from)) return;
  long lag = pace_lag();
  if (N.role == R_HOST) {
    if (!N.hold && lag > HOLD_AT) {
      N.hold = true;
      N.holds++;
      if (N.holds == 1) plat_log("net: the other computer is %.1f s behind: waiting for it", (double)lag / 30);
    } else if (N.hold && lag <= HOLD_TILL) N.hold = false;
  } else if (N.role == R_GUEST) {
    if (-lag > HOLD_AT) N.behind = true;
    else if (-lag <= HOLD_TILL) N.behind = false;
    if (N.told_t > 0) N.told_t -= dt;
  }
  if (N.hold) { /* the game stands still here (main.c): a welcome now and then says we are still here; not a state,
                  * which would count as game time we haven't run, and the guest counts welcomes out too */
    N.held_s += dt;
    N.hold_t -= dt;
    if (N.hold_t <= 0) {
      N.hold_t = 0.5f;
      send_welcome();
    }
    return;
  }
  N.send_t -= dt;
  if (N.send_t <= 0) {
    N.send_t = 1.0f / 30;
    if (N.role == R_HOST && g->time < 3) send_welcome(); /* until the guest is surely in */
    send_state(g);
  }
}

/* ---- the watch stream ------------------------------------------------------------ */
/* A test match shown live on other computers (--watch --broadcast on the one
 * running it, --spectate on the viewers). One way: a viewer only receives.
 *
 * The broadcaster makes a 16-letter watch code (80 bits, so it cannot be
 * guessed offline from a captured packet) and both sides turn it into two
 * keys, one each way. A viewer says hello in the clear, gets a fresh random
 * challenge back (no bigger than the hello, so a forged sender address
 * gains nothing), and returns it sealed under the code's key: only then,
 * and for five seconds after each such answer, is the stream sent to its
 * address. Stream packets are sealed and numbered; a viewer takes only
 * numbers above the last it took. Every tick: STATE (the jets) and SHOTS
 * (tanker, targets, missiles, bullets); twice a second: INFO (the match
 * and who flies each jet). */
enum { W_HELLO = 30, W_CHAL, W_SUB, W_INFO, W_STATE, W_SHOTS };
#define WATCH_VIEWERS 4

static struct {
  sock_t   s;
  bool     serving, viewing;
  char     code[17];
  uint8_t  k_bv[32], k_vb[32]; /* broadcaster to viewer, and back */
  uint64_t tx, rx_top;
  float    info_t, hello_t, quiet;
  /* the broadcaster's challenges out, and the viewers it sends to */
  struct { Addr a; uint8_t c[16]; bool used; } chal[8];
  uint32_t nchal; /* hellos so far: unsigned, so the count wraps and % 8 stays 0..7 */
  struct { Addr a; float age; bool on; } v[WATCH_VIEWERS];
  /* the viewer: where the stream comes from, and the newest of it */
  Addr     from;
  uint32_t match;
  Buf      state, shots;
  bool     have_state, have_shots;
} WS = { .s = BADSOCK };

static void watch_keys(const char *code) {
  uint8_t msg[32], k[64];
  memcpy(msg, "A4C-WATCH-1", 11);
  memset(msg + 11, 0, sizeof msg - 11);
  memcpy(msg + 11, code, 16);
  cr_blake2b(k, 64, NULL, 0, msg, 27);
  memcpy(WS.k_bv, k, 32);
  memcpy(WS.k_vb, k + 32, 32);
  cr_wipe(k, sizeof k);
}

static void watch_seal(const Addr *a, const Buf *p, const uint8_t key[32], uint64_t ctr) {
  sock_t keep = N.s;
  N.s = WS.s; /* seal_to sends on the match socket */
  seal_to(a, p, key, ctr);
  N.s = keep;
}

bool net_watch_serve(int port) {
  static const char LETTERS[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"; /* 32: five bits each, nothing to misread */
  uint8_t r[16];
  if (!random_bytes(r, sizeof r)) return false;
  WS.s = udp_open(port ? port : NET_WATCH, false, false);
  if (WS.s == BADSOCK) {
    plat_log("watch stream: cannot open UDP %d", port ? port : NET_WATCH);
    return false;
  }
  for (int i = 0; i < 16; i++) WS.code[i] = LETTERS[r[i] & 31];
  WS.code[16] = 0;
  watch_keys(WS.code);
  /* numbers that keep rising across restarts, so a viewer never takes an old packet as new */
  WS.tx = (uint64_t)plat_ms() << 16 | (uint64_t)time(NULL) << 32;
  WS.serving = true;
  plat_log("watch stream on UDP %d, watch code %s", port ? port : NET_WATCH, WS.code);
  return true;
}

const char *net_watch_code(void) { return WS.serving ? WS.code : NULL; }

static void watch_info(const Game *g, const WatchMatch *m, Buf *p, const char *const *who, const char *const *said) {
  const GameRules *R = &m->rules;
  head(p, W_INFO);
  put32(p, m->seed);
  put8(p, g->mode);
  put8(p, g->diff);
  put8(p, g->coop | R->classic << 1 | R->realistic << 2 | R->g_effects << 3 | R->ground_start << 4 | R->no_crash << 5 |
              R->dogfight << 6 | R->pairs << 7);
  put8(p, R->hits);
  put8(p, R->lives);
  put8(p, m->tod);
  put8(p, m->weather + 1);
  puts_(p, MAP ? MAP->id : "", 24);
  put8(p, g->nplanes);
  for (int i = 0; i < g->nplanes; i++) {
    put8(p, g->planes[i].jet);
    puts_(p, who[i], 16);
    puts_(p, said[i] ? said[i] : "", 40);
  }
}

static void watch_state(const Game *g, uint32_t id, Buf *p) {
  head(p, W_STATE);
  put32(p, id);
  putf(p, g->time);
  put8(p, g->over);
  put8(p, g->winner + 2);
  put8(p, g->nplanes);
  for (int i = 0; i < g->nplanes; i++) {
    const Plane *q = &g->planes[i];
    put8(p, q->alive | q->taxi << 1 | q->onGround << 2 | q->refuel << 3 | q->stall << 4 | q->locked << 5 | q->out << 6 |
                q->oncat << 7);
    put8(p, q->warn | q->drone << 1 | q->vecOn << 2 | q->autopilot << 3 | q->inactive << 4 | (q->invuln > 0) << 5 |
                q->hasDeath << 6);
    put8(p, q->jet);
    put8(p, q->gearDown | q->flaps << 1);
    putv(p, q->pos);
    putv(p, q->f);
    putv(p, q->u);
    putv(p, q->vel);
    putf(p, q->speed);
    putf(p, q->throttle);
    putf(p, q->fuel);
    putf(p, q->health);
    putf(p, q->ammo);
    putf(p, q->gload);
    putf(p, q->gmeter);
    putf(p, q->gstress);
    putf(p, q->gneg);
    putf(p, q->gloc);
    putf(p, q->lockT);
    putf(p, q->respawnT);
    putf(p, q->aoa);
    putf(p, q->hitFlash);
    putv(p, q->deathPos);
    put8(p, q->missiles);
    put8(p, q->flares);
    put8(p, q->radar);
    put8(p, q->ground);
    put8(p, q->chaff);
    put8(p, q->lives + 1);
    put8(p, q->kills);
    put8(p, q->ai.bfm);
    put8(p, q->ai.gt + 1);
    put8(p, q->ai.refuel);
    put32(p, (uint32_t)q->score);
    put8(p, q->lockTarget.kind);
    put8(p, q->lockTarget.idx + 1);
    int d = 0;
    for (int k = 0; k < 5; k++)
      if (q->deathMsg && !strcmp(q->deathMsg, DEATHS[k])) d = k;
    put8(p, d);
  }
}

static void put_dir(Buf *p, vec3 d) { /* a unit vector in three bytes */
  put8(p, (int)lroundf(clampf(d.x, -1, 1) * 127) + 128);
  put8(p, (int)lroundf(clampf(d.y, -1, 1) * 127) + 128);
  put8(p, (int)lroundf(clampf(d.z, -1, 1) * 127) + 128);
}
static vec3 get_dir(Buf *p) {
  float x = (float)(get8(p) - 128) / 127, y = (float)(get8(p) - 128) / 127;
  vec3 d = v3(x, y, (float)(get8(p) - 128) / 127);
  return v3_len(d) > 0.1f ? v3_norm(d) : v3(0, 0, 1);
}

static void watch_shots(const Game *g, uint32_t id, Buf *p) {
  head(p, W_SHOTS);
  put32(p, id);
  for (int t = 0; t < 2; t++) {
    const Tanker *T = &g->tankers[t];
    putv(p, T->pos);
    putv(p, T->f);
    putv(p, T->u);
    putv(p, T->drogue);
    put8(p, T->gone);
  }
  put8(p, g->ntargets);
  for (int i = 0; i < g->ntargets; i++) {
    put8(p, g->targets[i].alive | (g->targets[i].by + 3) << 1);
    put8(p, (int)clampf(g->targets[i].hp, 0, 255));
  }
  int nm = g->nmissiles < 24 ? g->nmissiles : 24;
  put8(p, nm);
  for (int i = 0; i < nm; i++) {
    putv(p, g->missiles[i].pos);
    put_dir(p, g->missiles[i].dir);
    put8(p, g->missiles[i].team | g->missiles[i].sam << 1 | g->missiles[i].kind << 2);
  }
  int nb = g->nbullets < 30 ? g->nbullets : 30;
  put8(p, nb);
  for (int i = g->nbullets - nb; i < g->nbullets; i++) { /* the newest */
    const Bullet *b = &g->bullets[i];
    putv(p, b->pos);
    put_dir(p, v3_norm(b->vel));
    put8(p, (int)clampf(v3_len(b->vel) / 8, 0, 255));
    put8(p, b->team);
  }
}

/* The broadcaster: one packet from a viewer, a hello (a challenge goes back) or its sealed answer. */
static void watch_serve_packet(Buf *p, int n, const Addr *from) {
  if (n < 5 || p->b[0] != 'A' || p->b[1] != '4' || p->b[2] != 'C' || p->b[3] != VERSION) return;
  if (p->b[4] == W_HELLO && n >= 21) {
    /* a challenge, no bigger than the hello, to the address it came from */
    int k = (int)(WS.nchal++ % 8);
    WS.chal[k].a = *from;
    WS.chal[k].used = random_bytes(WS.chal[k].c, 16);
    if (!WS.chal[k].used) return;
    Buf c;
    head(&c, W_CHAL);
    putn(&c, WS.chal[k].c, 16);
    sendto(WS.s, (const char *)c.b, c.n, 0, (const struct sockaddr *)from, sizeof *from);
  } else if (p->b[4] == W_SUB) {
    uint64_t ctr;
    uint8_t  got[16];
    if (!unseal(p, n, WS.k_vb, &ctr)) return;
    getn(p, got, 16);
    for (int k = 0; k < 8 && !p->bad; k++) {
      if (!WS.chal[k].used || !same(&WS.chal[k].a, from) || memcmp(WS.chal[k].c, got, 16) != 0) continue;
      WS.chal[k].used = false; /* each challenge once */
      int slot = -1;
      for (int v = 0; v < WATCH_VIEWERS; v++)
        if (WS.v[v].on && same(&WS.v[v].a, from)) slot = v;
      for (int v = 0; v < WATCH_VIEWERS && slot < 0; v++)
        if (!WS.v[v].on) slot = v;
      if (slot < 0) break; /* full */
      if (!WS.v[slot].on) {
        char t[32];
        addr_text(from, t, sizeof t);
        plat_log("watch stream: viewer at %s", t);
      }
      WS.v[slot].a = *from;
      WS.v[slot].on = true;
      WS.v[slot].age = 0;
      WS.info_t = 0;
    }
  }
}

void net_watch_send(const Game *g, const WatchMatch *m, float dt, const char *const *who, const char *const *said) {
  if (!WS.serving) return;
  Buf p;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while ((n = (int)recvfrom(WS.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0)
    watch_serve_packet(&p, n, &from);
  bool any = false;
  for (int v = 0; v < WATCH_VIEWERS; v++) {
    if (WS.v[v].on && (WS.v[v].age += dt) > 5) WS.v[v].on = false;
    any = any || WS.v[v].on;
  }
  if (!any) return;
  Buf b[3];
  int nb = 0;
  if ((WS.info_t -= dt) <= 0) {
    WS.info_t = 0.5f;
    watch_info(g, m, &b[nb++], who, said);
  }
  watch_state(g, m->seed, &b[nb++]);
  watch_shots(g, m->seed, &b[nb++]);
  for (int k = 0; k < nb; k++)
    for (int v = 0; v < WATCH_VIEWERS; v++)
      if (WS.v[v].on) watch_seal(&WS.v[v].a, &b[k], WS.k_bv, ++WS.tx);
}

bool net_watch_join(const char *addr, const char *code) {
  if (!code || strlen(code) != 16) {
    plat_log("watch stream: the watch code has 16 letters");
    return false;
  }
  if (!resolve(addr, NET_WATCH, &WS.from)) {
    plat_log("watch stream: cannot find %s", addr);
    return false;
  }
  WS.s = udp_open(0, false, false);
  if (WS.s == BADSOCK) return false;
  char c[17];
  for (int i = 0; i < 16; i++) c[i] = (char)(code[i] >= 'a' && code[i] <= 'z' ? code[i] - 32 : code[i]);
  c[16] = 0;
  watch_keys(c);
  WS.viewing = true;
  plat_log("watch stream: asking %s", addr);
  return true;
}

/* The viewer's copy of the match from the newest INFO: made again when the match changes. */
static bool watch_take_info(Game *g, Buf *p, char who[MAX_PLANES][16], char said[MAX_PLANES][40]) {
  uint32_t match = get32(p);
  int mode = get8(p), diff = get8(p), fl = get8(p), hits = get8(p), lives = get8(p), tod = get8(p), wx = get8(p) - 1;
  char map[24];
  gets_(p, map, 24);
  int np = get8(p), jets[MAX_PLANES] = { 0 };
  for (int i = 0; i < np && i < MAX_PLANES; i++) {
    jets[i] = get8(p);
    gets_(p, who[i], 16);
    gets_(p, said[i], 40);
  }
  if (p->bad || mode < MODE_1P || mode > MODE_DRONE || diff > DIFF_HARD || np > MAX_PLANES) return false;
  if (match == WS.match && g->nplanes == np) return false;
  GameRules R = { 0 };
  R.classic = fl >> 1 & 1;
  R.realistic = fl >> 2 & 1;
  R.g_effects = fl >> 3 & 1;
  R.ground_start = fl >> 4 & 1;
  R.no_crash = fl >> 5 & 1;
  R.dogfight = fl >> 6 & 1;
  R.pairs = fl >> 7 & 1;
  R.hits = hits;
  R.lives = lives;
  R.load[0][0] = R.load[1][0] = -1;
  int mi = assets_map_find(map);
  const Map *mp = mi >= 0 ? assets_map(mi) : NULL;
  world_set_map(mp, tod % TOD_COUNT, mp ? (wx >= 0 && wx < WX_COUNT ? wx : mp->default_weather) : WX_CLEAR); /* -1: the map's own */
  int seat[2] = { jets[0] < NJETS ? jets[0] : 0, jets[1] < NJETS ? jets[1] : 0 };
  game_new_rules(g, (GameMode)mode, (Difficulty)diff, fl & 1, seat, match, &R);
  for (int i = 0; i < g->nplanes; i++) {
    g->planes[i].remote = true;
    g->planes[i].is_ai = false;
  }
  for (int i = 0; i < g->ntargets; i++) g->targets[i].remote = true;
  WS.match = match;
  WS.have_state = WS.have_shots = false;
  plat_log("watch stream: match %u, %d jets", match, g->nplanes);
  return true;
}

/* The viewer: one packet from the broadcaster, a challenge to answer or the stream; true when INFO set up a
 * new match in g. */
static bool watch_view_packet(Game *g, Buf *p, int n, const Addr *from, char who[MAX_PLANES][16], char said[MAX_PLANES][40]) {
  if (n < 5 || p->b[0] != 'A' || p->b[1] != '4' || p->b[2] != 'C' || p->b[3] != VERSION || !same(from, &WS.from)) return false;
  int type = p->b[4];
  if (type == W_CHAL && n >= 21) {
    Buf s;
    head(&s, W_SUB);
    putn(&s, p->b + 5, 16);
    uint64_t ctr;
    if (random_bytes(&ctr, sizeof ctr)) watch_seal(&WS.from, &s, WS.k_vb, ctr);
    return false;
  }
  uint64_t ctr;
  if ((type != W_INFO && type != W_STATE && type != W_SHOTS) || !unseal(p, n, WS.k_bv, &ctr) || ctr <= WS.rx_top) return false;
  WS.rx_top = ctr;
  WS.quiet = 0;
  if (type == W_INFO) return watch_take_info(g, p, who, said);
  else if (type == W_STATE) WS.state = *p, WS.have_state = true;
  else WS.shots = *p, WS.have_shots = true;
  return false;
}

/* The planes' snapshots from the newest STATE, so the game's own update moves them between packets. */
static void watch_snaps(Game *g) {
  if (WS.have_state && WS.match) {
    Buf s = WS.state;
    if (get32(&s) == WS.match) {
      getf(&s);
      get8(&s);
      get8(&s);
      int np = get8(&s);
      for (int i = 0; i < np && i < g->nplanes && !s.bad; i++) {
        for (int k = 0; k < 4; k++) get8(&s);
        Plane *q = &g->planes[i];
        q->snap.pos = getv(&s);
        q->snap.f = v3_norm(getv(&s));
        q->snap.u = v3_norm(getv(&s));
        q->snap.vel = getv(&s);
        q->snap.speed = getf(&s);
        q->snap.thr = getf(&s);
        q->snap.age = 0;
        q->snap.have = true;
        for (int k = 0; k < 12; k++) getf(&s); /* fuel to hitFlash, as watch_state writes them */
        getv(&s);
        for (int k = 0; k < 10; k++) get8(&s);
        get32(&s);
        for (int k = 0; k < 3; k++) get8(&s);
      }
    }
  }
}

int net_watch_poll(Game *g, float dt, char who[MAX_PLANES][16], char said[MAX_PLANES][40]) {
  if (!WS.viewing) return 0;
  int fresh = 0;
  Buf p;
  Addr from;
  socklen_t fl = sizeof from;
  int n;
  while ((n = (int)recvfrom(WS.s, (char *)p.b, sizeof p.b, 0, (struct sockaddr *)&from, &fl)) > 0)
    fresh |= watch_view_packet(g, &p, n, &from, who, said);
  /* hello again every two seconds: the broadcaster forgets a viewer after five */
  if ((WS.hello_t -= dt) <= 0) {
    WS.hello_t = 2;
    Buf h;
    head(&h, W_HELLO);
    for (int i = 0; i < 16; i++) put8(&h, 0); /* as big as the challenge it asks for */
    sendto(WS.s, (const char *)h.b, h.n, 0, (const struct sockaddr *)&WS.from, sizeof WS.from);
  }
  if ((WS.quiet += dt) > 5 && WS.match) {
    plat_log("watch stream: nothing for five seconds");
    WS.quiet = -1000;
  }
  /* the planes' snapshots, so the game's own update moves them between packets */
  watch_snaps(g);
  return fresh;
}

void net_watch_apply(Game *g) {
  if (!WS.viewing || !WS.match) return;
  if (WS.have_state) {
    Buf s = WS.state;
    if (get32(&s) != WS.match) return;
    g->time = getf(&s);
    g->over = get8(&s) != 0;
    g->winner = get8(&s) - 2;
    g->overT = 1;
    int np = get8(&s);
    for (int i = 0; i < np && i < g->nplanes && !s.bad; i++) {
      Plane *q = &g->planes[i];
      int a = get8(&s), b = get8(&s), jet = get8(&s), gf = get8(&s);
      q->gearDown = gf & 1;
      q->flaps = (int8_t)((gf >> 1 & 3) % 3);
      bool alive = a & 1;
      q->taxi = a >> 1 & 1;
      q->onGround = a >> 2 & 1;
      q->refuel = a >> 3 & 1;
      q->stall = a >> 4 & 1;
      q->locked = a >> 5 & 1;
      q->out = a >> 6 & 1;
      q->oncat = a >> 7 & 1;
      q->warn = b & 1;
      q->drone = b >> 1 & 1;
      q->vecOn = b >> 2 & 1;
      q->autopilot = b >> 3 & 1;
      q->inactive = b >> 4 & 1;
      q->invuln = b >> 5 & 1 ? 1 : 0;
      if (jet < NJETS || jet == DRONE_JET) q->jet = jet;
      q->pos = getv(&s);
      q->f = v3_norm(getv(&s));
      q->u = v3_norm(getv(&s));
      q->r = v3_norm(v3_cross(q->u, q->f));
      q->u = v3_cross(q->f, q->r);
      q->vel = getv(&s);
      q->speed = getf(&s);
      q->throttle = getf(&s);
      q->fuel = getf(&s);
      q->health = getf(&s);
      q->ammo = getf(&s);
      q->gload = getf(&s);
      q->gmeter = getf(&s);
      q->gstress = getf(&s);
      q->gneg = getf(&s);
      q->gloc = getf(&s);
      q->lockT = getf(&s);
      q->respawnT = getf(&s);
      q->aoa = getf(&s);
      q->hitFlash = getf(&s);
      vec3 dp = getv(&s);
      q->missiles = get8(&s);
      q->flares = get8(&s);
      q->radar = get8(&s);
      q->ground = get8(&s);
      q->chaff = get8(&s);
      q->lives = get8(&s) - 1;
      q->kills = get8(&s);
      q->ai.bfm = get8(&s) % BFM_COUNT;
      q->ai.gt = get8(&s) - 1;
      q->ai.refuel = get8(&s) != 0;
      q->score = (int)get32(&s);
      q->lockTarget.kind = (uint8_t)get8(&s);
      q->lockTarget.idx = (int16_t)(get8(&s) - 1);
      q->lockTarget.gen = 0;
      if (q->lockTarget.kind == REF_PLANE && (q->lockTarget.idx < 0 || q->lockTarget.idx >= g->nplanes)) q->lockTarget = ref_none();
      if (q->lockTarget.kind != REF_PLANE) q->lockTarget = ref_none(); /* only jets can be pointed at from here */
      int d = get8(&s);
      if (s.bad) break;
      if (alive && !q->alive) q->alive = true, q->hasDeath = false;
      else if (!alive && q->alive) {
        q->pos = dp;
        sim_remote_down(g, q, DEATHS[d < 5 ? d : 0]);
      }
      q->deathPos = dp;
    }
  }
  if (WS.have_shots) {
    Buf s = WS.shots;
    if (get32(&s) != WS.match) return;
    for (int t = 0; t < 2; t++) {
      Tanker *T = &g->tankers[t];
      T->pos = getv(&s);
      T->f = v3_norm(getv(&s));
      T->u = v3_norm(getv(&s));
      T->r = v3_norm(v3_cross(T->u, T->f));
      T->drogue = getv(&s);
      T->gone = get8(&s) != 0;
    }
    int nt = get8(&s);
    for (int i = 0; i < nt && !s.bad; i++) {
      int a = get8(&s), hp = get8(&s);
      if (i >= g->ntargets) continue;
      Target *t = &g->targets[i];
      t->hp = (float)hp;
      if (!(a & 1) && t->alive) sim_target_fall(g, t, (a >> 1) - 3);
    }
    int nm = get8(&s);
    g->nmissiles = 0;
    for (int i = 0; i < nm && !s.bad && g->nmissiles < MAX_MISSILES; i++) {
      Missile *m = &g->missiles[g->nmissiles++];
      memset(m, 0, sizeof *m);
      m->pos = m->prev = getv(&s);
      m->dir = get_dir(&s);
      int k = get8(&s);
      m->team = k & 1;
      m->sam = k >> 1 & 1;
      m->kind = k >> 2 & 3;
      m->life = 1;
      m->owner = -1;
      m->remote = true;
      m->target = ref_none();
    }
    int nb = get8(&s);
    g->nbullets = 0;
    for (int i = 0; i < nb && !s.bad && g->nbullets < MAX_BULLETS; i++) {
      Bullet *b = &g->bullets[g->nbullets++];
      b->pos = b->prev = getv(&s);
      vec3 d = get_dir(&s);
      b->vel = v3_scale(d, (float)get8(&s) * 8);
      b->team = get8(&s) & 1;
      b->life = 0.1f;
      b->owner = -1;
    }
    if (s.bad) g->nmissiles = g->nbullets = 0;
  }
}

/* ---- fuzzing (tests/lf_net.c) ------------------------------------------------------ */
#ifdef A4C_FUZZ
/* One input: a phase to set up, an option byte, then the packets that arrive in it, each handed to the handler
 * the game's own receive loop calls (no socket is opened). Each packet is a flags byte (1: sealed; 2, 4: from
 * the relay or from a stranger rather than the other player), a step for its packet number (up to 127 on,
 * 128 and up that much less 128 back), a 16-bit length and its bytes, a header and a body as the game builds
 * them. A sealed one is encrypted here, as seal_to does, under the key the other side would use, so what lies
 * behind the signature is fuzzed too. */
enum { NF_LAN, NF_HOST, NF_HOST_RELAY, NF_GUEST, NF_GUEST_RELAY, NF_GUEST_KEYED, NF_PLAY_HOST, NF_PLAY_GUEST,
       NF_WATCH_SERVE, NF_WATCH_VIEW, NF_COUNT };

static Addr fuzz_addr(uint32_t ip, int port) {
  Addr a = { 0 };
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  a.sin_port = htons((uint16_t)port);
  return a;
}
#define FUZZ_PEER  fuzz_addr(0x0A000002, NET_PORT)
#define FUZZ_RELAY fuzz_addr(0x0A000009, RELAY_PORT)
#define FUZZ_OTHER fuzz_addr(0x0A000007, 5555)

/* plain (a header and a body) sealed into p under key with number ctr; the length on the wire */
static int fuzz_seal(Buf *p, const uint8_t *plain, int n, const uint8_t key[32], uint64_t ctr) {
  uint8_t nonce[12] = { 0 };
  if (n > (int)sizeof p->b - SEAL_EXTRA) n = (int)sizeof p->b - SEAL_EXTRA;
  int body = n - 5;
  memcpy(p->b, plain, 5);
  for (int i = 0; i < 8; i++) p->b[5 + i] = (uint8_t)(ctr >> (8 * i));
  memcpy(nonce + 4, p->b + 5, 8);
  cr_aead_seal(p->b + 13, p->b + 13 + body, key, nonce, p->b, 13, plain + 5, (size_t)body);
  return 13 + body + 16;
}

/* the key a sealed packet from `from` is checked with in this phase */
static const uint8_t *fuzz_key(int phase, const Addr *from) {
  static const uint8_t junk[32] = { 1 };
  if (phase == NF_WATCH_SERVE) return WS.k_vb;
  if (phase == NF_WATCH_VIEW) return WS.k_bv;
  if (N.keyed) return N.k_rx;
  for (int k = 0; k < 4; k++)
    if (N.pend[k].used && same(&N.pend[k].from, from)) return N.pend[k].k_gh;
  return junk;
}

/* the state a phase starts from: what net_host, join_common and the welcome would leave, without the socket */
static void fuzz_setup(Game *g, int phase, int opt) {
  memset(&N, 0, sizeof N);
  N.s = N.lan = BADSOCK;
  memset(&WS, 0, sizeof WS);
  WS.s = BADSOCK;
  memcpy(N.pin, "123456", 7);
  snprintf(N.name, sizeof N.name, "FUZZ");
  N.match.game = 7;
  N.match.seed = 1;
  N.match.drone = (opt >> 4 & 3) % 3;
  N.match.coop = opt & 1;
  snprintf(N.match.map, sizeof N.match.map, "classic");
  N.via_relay = phase == NF_HOST_RELAY || phase == NF_GUEST_RELAY || (phase >= NF_PLAY_HOST && (opt & 2));
  N.relay = FUZZ_RELAY;
  if (phase == NF_HOST || phase == NF_HOST_RELAY || phase == NF_PLAY_HOST) N.role = R_HOST;
  if (phase == NF_GUEST || phase == NF_GUEST_RELAY || phase == NF_GUEST_KEYED || phase == NF_PLAY_GUEST) N.role = R_GUEST;
  if (N.role) N.phase = PH_WAIT;
  if (N.role == R_GUEST) N.peer = FUZZ_PEER, N.have_peer = !N.via_relay;
  if (phase == NF_GUEST || phase == NF_GUEST_RELAY) { /* join_common's half of the handshake */
    random_bytes(N.nonce, sizeof N.nonce);
    random_bytes(N.secret, sizeof N.secret);
    uint8_t pt[32];
    code_point(pt, N.pin, N.nonce);
    cr_x25519(N.y, N.secret, pt);
  }
  if (phase == NF_GUEST_KEYED || phase >= NF_PLAY_HOST) { /* past the handshake: a key each way */
    for (int i = 0; i < 32; i++) N.k_rx[i] = (uint8_t)(0x11 + i), N.k_tx[i] = (uint8_t)(0x77 + i);
    N.keyed = N.challenged = true;
  }
  world_set_map(NULL, TOD_DAY, WX_CLEAR);
  if (phase == NF_PLAY_HOST || phase == NF_PLAY_GUEST) {
    GameRules R = { 0 };
    R.classic = opt >> 2 & 1;
    R.realistic = opt >> 3 & 1;
    R.pairs = opt >> 6 & 1;
    R.load[0][0] = R.load[1][0] = -1;
    int seats[2] = { 0, NJETS > 1 ? 1 : 0 };
    game_new_rules(g, N.match.drone ? MODE_DRONE : MODE_2P, DIFF_NORMAL, N.match.coop, seats, N.match.seed, &R);
    N.peer = FUZZ_PEER;
    N.have_peer = true;
    start_play();
    net_setup(g);
  }
  if (phase == NF_WATCH_SERVE || phase == NF_WATCH_VIEW) {
    memcpy(WS.code, "23456789ABCDEFGH", 17);
    watch_keys(WS.code);
    WS.serving = phase == NF_WATCH_SERVE;
    WS.viewing = phase == NF_WATCH_VIEW;
    WS.from = FUZZ_PEER;
    game_new(g, MODE_2P, DIFF_NORMAL, false, NULL, 1);
  }
}

void net_fuzz(Game *g, const uint8_t *data, size_t size) {
  if (size < 2) return;
  int phase = data[0] % NF_COUNT;
  fuzz_setup(g, phase, data[1]);
  data += 2, size -= 2;
  const Addr peer = FUZZ_PEER, relay = FUZZ_RELAY, other = FUZZ_OTHER;
  static char who[MAX_PLANES][16], said[MAX_PLANES][40];
  uint64_t ctr = 1000;
  NetMatch out;
  NetEvent ev = NETE_NONE;
  while (size >= 4) {
    int flags = data[0], step = data[1], len = data[2] | data[3] << 8;
    data += 4, size -= 4;
    if ((size_t)len > size) len = (int)size;
    if (len > (int)sizeof(((Buf *)0)->b)) len = (int)sizeof(((Buf *)0)->b);
    const Addr *from = (flags >> 1 & 3) == 1 ? &relay : (flags >> 1 & 3) == 2 ? &other : &peer;
    ctr = step < 0x80 ? ctr + (uint64_t)step : ctr - (uint64_t)(step - 0x80);
    Buf p;
    memset(&p, 0, sizeof p);
    int n = len;
    if ((flags & 1) && len >= 5) n = fuzz_seal(&p, data, len, fuzz_key(phase, from), ctr);
    else if (len) memcpy(p.b, data, (size_t)len);
    data += len, size -= (size_t)len;
    if (n <= 0) continue;
    switch (phase) {
    case NF_LAN: lan_packet(&p, n, from); break;
    case NF_PLAY_HOST:
    case NF_PLAY_GUEST:
      if (N.phase == PH_PLAY) play_packet(g, &p, n, from);
      break;
    case NF_WATCH_SERVE: watch_serve_packet(&p, n, from); break;
    case NF_WATCH_VIEW: /* and the frame the game runs after it */
      watch_view_packet(g, &p, n, from, who, said);
      watch_snaps(g);
      sim_update(g, 1.0f / 60);
      net_watch_apply(g);
      break;
    default:
      if (N.phase == PH_WAIT) wait_packet(&p, n, from, &out, &ev);
    }
  }
  if (N.phase == PH_PLAY && (phase == NF_PLAY_HOST || phase == NF_PLAY_GUEST)) { /* the rest of the tick */
    sim_update(g, 1.0f / 60);
    send_state(g);
  }
}

/* ---- seeds: packets from the game's own builders, in net_fuzz's layout ---- */
typedef struct { uint8_t *b; size_t n, cap; } FuzzOut;
static void fuzz_put(FuzzOut *o, int v) {
  if (o->n < o->cap) o->b[o->n++] = (uint8_t)v;
}
static void fuzz_packet(FuzzOut *o, int flags, int step, const Buf *p) {
  fuzz_put(o, flags);
  fuzz_put(o, step);
  fuzz_put(o, p->n & 255);
  fuzz_put(o, p->n >> 8);
  for (int i = 0; i < p->n; i++) fuzz_put(o, p->b[i]);
}
/* a guest's half of the handshake as join_common makes it, from a fixed secret */
static void fuzz_hello(Buf *p, uint8_t nonce[16]) {
  uint8_t secret[32], pt[32], y[32];
  for (int i = 0; i < 16; i++) nonce[i] = (uint8_t)(0xA0 + i);
  for (int i = 0; i < 32; i++) secret[i] = (uint8_t)(0x40 + i);
  code_point(pt, "123456", nonce);
  cr_x25519(y, secret, pt);
  head(p, T_HELLO);
  putn(p, nonce, 16);
  putn(p, y, 32);
}
/* a game's state as the given side sends it (its own jets, a fallen target, three events) */
static void fuzz_state(Game *g, int phase, int opt, bool over, Buf *p) {
  fuzz_setup(g, phase, opt);
  sim_update(g, 1.0f / 60);
  if (g->ntargets) g->targets[0].alive = false, g->targets[0].remote = false, g->targets[0].by = 0;
  g->nnetev = 3;
  g->netev[0].kind = NE_FLARES, g->netev[0].plane = g->my, g->netev[0].chaff = true;
  memset(&g->netev[1].m, 0, sizeof g->netev[1].m);
  g->netev[1].kind = NE_MISSILE, g->netev[1].m.id = 5, g->netev[1].m.pos = g->planes[g->my].pos;
  g->netev[1].m.dir = g->planes[g->my].f, g->netev[1].m.speed = 300, g->netev[1].m.life = 8;
  g->netev[1].m.owner = g->my, g->netev[1].m.target = ref_none();
  g->netev[2] = g->netev[1];
  g->netev[2].kind = NE_GONE;
  if (over) {
    g->over = true;
    g->winner = 1;
    snprintf(g->reason, sizeof g->reason, "THE GUEST WINS");
  }
  state_packet(g, p);
}

/* Seed number k (from 0) written into out; its length, 0 when there are no more. The caller starts the random
 * numbers as net_fuzz's caller does, so a challenge made here is the one the run makes. */
size_t net_fuzz_seed(Game *g, int k, uint8_t *out, size_t cap) {
  FuzzOut o = { out, 0, cap };
  Buf p, q;
  uint8_t nonce[16];
  int phase = k < 10 ? k : k == 10 ? NF_GUEST : k == 11 ? NF_PLAY_GUEST : k == 12 ? NF_WATCH_VIEW : -1;
  if (phase < 0) return 0;
  int opt = k == 11 ? 0x05 : 0; /* the second play seed: co-op, classic rules */
  fuzz_setup(g, phase, opt);
  fuzz_put(&o, phase);
  fuzz_put(&o, opt);
  switch (k) {
  case NF_LAN: /* a beacon, as the host's net_poll sends it */
    head(&p, T_BEACON);
    puts_(&p, "HOSTPILOT", 16);
    put32(&p, NET_PORT);
    put8(&p, 1);
    puts_(&p, "twin-isles", 24);
    fuzz_packet(&o, 0, 1, &p);
    break;
  case NF_HOST: /* hello, then the sealed join under the keys the hello made */
  case NF_HOST_RELAY:
    if (k == NF_HOST_RELAY) {
      head(&p, T_RCODE);
      puts_(&p, "123456", 6);
      fuzz_packet(&o, 2, 1, &p);
    }
    fuzz_hello(&p, nonce);
    fuzz_packet(&o, k == NF_HOST_RELAY ? 2 : 0, 1, &p);
    head(&p, T_JOIN);
    put8(&p, 1);
    for (int i = 0; i < 3; i++) put8(&p, 1);
    puts_(&p, "GUESTPILOT", 16);
    fuzz_packet(&o, k == NF_HOST_RELAY ? 3 : 1, 1, &p);
    break;
  case NF_GUEST: /* the host's challenge, then its sealed welcome */
  case NF_GUEST_RELAY:
  case 10: {
    if (k == NF_GUEST_RELAY) {
      head(&p, T_RJOIN);
      puts_(&p, "123456", 6);
      fuzz_packet(&o, 2, 1, &p);
    }
    uint8_t b[32], pt[32], yh[32];
    for (int i = 0; i < 32; i++) b[i] = (uint8_t)(0x90 + i);
    code_point(pt, N.pin, N.nonce);
    cr_x25519(yh, b, pt);
    if (k == 10) { /* a wrong code: the reject */
      head(&p, T_REJECT);
      putn(&p, N.nonce, 16);
      fuzz_packet(&o, 0, 1, &p);
      break;
    }
    head(&p, T_CHALLENGE);
    putn(&p, N.nonce, 16);
    putn(&p, yh, 32);
    fuzz_packet(&o, k == NF_GUEST_RELAY ? 2 : 0, 1, &p);
  }
    A4C_FALLTHROUGH; /* the welcome */
  case NF_GUEST_KEYED:
    head(&p, T_WELCOME);
    put_match(&p, &N.match);
    fuzz_packet(&o, k == NF_GUEST_RELAY ? 3 : 1, 1, &p);
    break;
  case NF_PLAY_HOST: /* the guest's state, and its join again */
  case NF_PLAY_GUEST:
  case 11: {
    bool host_gets = k == NF_PLAY_HOST;
    fuzz_state(g, host_gets ? NF_PLAY_GUEST : NF_PLAY_HOST, opt, !host_gets, &q);
    fuzz_setup(g, phase, opt);
    fuzz_packet(&o, 1, 1, &q);
    fuzz_packet(&o, 1, 1, &q);
    if (host_gets) {
      head(&p, T_JOIN);
      put8(&p, 1);
      for (int i = 0; i < 3; i++) put8(&p, 1);
      puts_(&p, "GUESTPILOT", 16);
      fuzz_packet(&o, 1, 1, &p);
    } else {
      NetMatch m = N.match;
      m.game++;
      head(&p, T_WELCOME); /* a rematch */
      put_match(&p, &m);
      fuzz_packet(&o, 1, 1, &p);
    }
    head(&p, T_BYE);
    fuzz_packet(&o, 1, 1, &p);
    break;
  }
  case NF_WATCH_SERVE: { /* a viewer's hello, then its sealed answer to the challenge it made */
    head(&p, W_HELLO);
    for (int i = 0; i < 16; i++) put8(&p, 0);
    fuzz_packet(&o, 0, 1, &p);
    Addr peer = FUZZ_PEER;
    Buf h = p;
    watch_serve_packet(&h, h.n, &peer);
    head(&p, W_SUB);
    putn(&p, WS.chal[0].c, 16);
    fuzz_packet(&o, 1, 1, &p);
    break;
  }
  case NF_WATCH_VIEW: /* a challenge, then INFO, STATE and SHOTS of a match */
  case 12: {
    WatchMatch wm = { 0 };
    wm.seed = 42;
    wm.tod = TOD_DAY;
    wm.weather = -1;
    wm.rules.load[0][0] = wm.rules.load[1][0] = -1;
    game_new_rules(g, k == 12 ? MODE_DRONE : MODE_2P, DIFF_HARD, k == 12, NULL, 42, &wm.rules);
    for (int i = 0; i < 40; i++) sim_update(g, 1.0f / 60);
    const char *who[MAX_PLANES] = { "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT" };
    const char *said[MAX_PLANES] = { "FOX TWO", NULL, "", "SPLASH" };
    head(&p, W_CHAL);
    for (int i = 0; i < 16; i++) put8(&p, i);
    fuzz_packet(&o, 0, 1, &p);
    watch_info(g, &wm, &p, who, said);
    fuzz_packet(&o, 1, 1, &p);
    watch_state(g, wm.seed, &p);
    fuzz_packet(&o, 1, 1, &p);
    watch_shots(g, wm.seed, &p);
    fuzz_packet(&o, 1, 1, &p);
    watch_state(g, wm.seed, &p);
    fuzz_packet(&o, 1, 0, &p); /* the same number again: a replay, refused */
    break;
  }
  }
  return o.n;
}
#endif
