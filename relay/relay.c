/* relay.c - matchmaking and forwarding server for online play.
 * A host registers its 6-digit game code; a guest joins with the code; from
 * then on the relay forwards each one's UDP packets to the other, so neither
 * needs to forward a port on their router. Runs on any small Linux box with
 * UDP port 27970 open:
 *
 *   a4c_relay [port]
 *
 * The match itself is encrypted end to end with a key made from the code
 * (src/net.c), so the relay only sees sealed packets. The code is also that
 * key's secret, so the relay must not help anyone sweep for live codes: an
 * address gets 10 wrong codes a minute, a code 10 wrong tries a minute (from
 * anyone), and the relay as a whole 600 join tries a minute. Past a limit it
 * answers nothing, for a live code as for any other, and a wrong code and a
 * game somebody is playing get the same T_RFAIL.
 *
 * Packets start "A4C", a version and a type (see src/net.c). */
#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define VERSION 10 /* the same as src/net.c: the relay answers a packet of any other version with one of its own,
                     so that game tells its player the versions differ, and passes nothing on */
enum { T_RHOST = 6, T_RCODE, T_RJOIN, T_RFAIL, T_PING };
#define MAX_GAMES  1024
#define IDLE_S     60  /* a game nobody has sent anything for is dropped */
#define GUEST_S    10  /* a guest silent this long can be replaced by a new one */
#define HOSTS_PER  4   /* games one address may host at once */
#define MISS_PER   10  /* wrong codes one address may try a minute */
#define MISS_CODE  10  /* wrong tries one code may get a minute, from everyone */
#define JOIN_ALL   600 /* join tries everyone together may make a minute */

typedef struct sockaddr_in Addr;
typedef struct {
  bool   used, has_guest;
  char   code[7];
  Addr   host, guest;
  double host_t, guest_t;
} Game;

static Game games[MAX_GAMES];
/* wrong guesses this minute, per address: each address has its own counter, found by its hash and the next few
 * slots (MISS_PROBE); per code (the 6 digits as a number); and the join tries of everyone together */
#define MISS_SLOTS 4096
#define MISS_PROBE 16
static struct { bool used; uint32_t ip; int n; double t; } misses[MISS_SLOTS];
static uint8_t code_misses[1000000];
static int join_all;

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static bool same(const Addr *a, const Addr *b) { return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port; }

static const char *text(const Addr *a) {
  static char buf[32];
  snprintf(buf, sizeof buf, "%s:%d", inet_ntoa(a->sin_addr), ntohs(a->sin_port));
  return buf;
}

static bool valid_code(const uint8_t *c) {
  for (int i = 0; i < 6; i++)
    if (c[i] < '0' || c[i] > '9') return false;
  return true;
}

static Game *find_code(const char *code) {
  for (int i = 0; i < MAX_GAMES; i++)
    if (games[i].used && !strncmp(games[i].code, code, 6)) return &games[i];
  return NULL;
}

/* The miss counter of an address, or NULL when it has used its tries. Never shared: an address that is not
 * there yet takes a free slot, or when all are taken the one that was first used longest ago (t is now). */
static int *miss_slot(const Addr *a, double t) {
  uint32_t ip = a->sin_addr.s_addr, h = (ip * 2654435761u) >> 20;
  int take = -1;
  for (int i = 0; i < MISS_PROBE; i++) {
    int k = (int)((h + (uint32_t)i) % MISS_SLOTS);
    if (misses[k].used && misses[k].ip == ip) return misses[k].n >= MISS_PER ? NULL : &misses[k].n;
    if (take < 0 || (misses[take].used && (!misses[k].used || misses[k].t < misses[take].t))) take = k;
  }
  misses[take].used = true;
  misses[take].ip = ip;
  misses[take].n = 0;
  misses[take].t = t;
  return &misses[take].n;
}

/* The answer to a guest's T_RJOIN for code: T_RJOIN (it is the game's guest now), T_RFAIL (no such game, or
 * someone else is playing it), or 0 to say nothing. Whether it says anything depends only on the limits, never on
 * whether the code is live, and a live code's T_RFAIL is the same as a wrong one's. */
static int join_reply(const Addr *from, const char *code, double t) {
  Game *h = find_code(code);
  /* its own guest asking again (its answer was lost) is no new guess */
  if (h && h->has_guest && same(&h->guest, from)) {
    h->guest_t = t;
    return T_RJOIN;
  }
  int c = 0;
  for (int i = 0; i < 6; i++) c = c * 10 + (code[i] - '0');
  int *m = miss_slot(from, t);
  if (!m || code_misses[c] >= MISS_CODE || join_all >= JOIN_ALL) return 0; /* out of guesses: say nothing */
  join_all++;
  if (h && (!h->has_guest || t - h->guest_t > GUEST_S)) {
    printf("game joined by %s\n", text(from));
    fflush(stdout);
    h->guest = *from;
    h->has_guest = true;
    h->guest_t = t;
    return T_RJOIN;
  }
  (*m)++; /* a wrong code, or a game someone is playing: the same answer */
  code_misses[c]++;
  return T_RFAIL;
}

static void reply(int s, const Addr *to, int type, const char *code) {
  uint8_t p[16] = { 'A', '4', 'C', VERSION, (uint8_t)type };
  int n = 5;
  if (code) {
    memcpy(p + 5, code, 6);
    n = 11;
  }
  sendto(s, p, (size_t)n, 0, (const struct sockaddr *)to, sizeof *to);
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 27970;
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  Addr a = { 0 };
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
    perror("a4c_relay: bind");
    return 1;
  }
  struct timeval tv = { 1, 0 };
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  printf("a4c_relay: listening on UDP %d\n", port);
  fflush(stdout);
  uint8_t p[1500];
  double last_sweep = now(), last_minute = now();
  for (;;) {
    Addr from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s, p, sizeof p, 0, (struct sockaddr *)&from, &fl);
    double t = now();
    if (t - last_minute > 60) { /* a new minute of guesses */
      last_minute = t;
      memset(misses, 0, sizeof misses);
      memset(code_misses, 0, sizeof code_misses);
      join_all = 0;
    }
    if (t - last_sweep > 5) { /* drop idle games */
      last_sweep = t;
      for (int i = 0; i < MAX_GAMES; i++)
        if (games[i].used && t - games[i].host_t > IDLE_S && (!games[i].has_guest || t - games[i].guest_t > IDLE_S)) {
          printf("game ended\n");
          games[i].used = false;
        }
      fflush(stdout);
    }
    if (n < 5 || p[0] != 'A' || p[1] != '4' || p[2] != 'C') continue;
    if (p[3] != VERSION) {
      /* another build: five bytes back (no bigger than what came), its game says why. Only to a host's or a guest's
       * first request (11 bytes; our answer is 5, so another relay's answer is never answered again: two relays
       * of different versions can't keep a spoofed exchange going), and at most 20 a second in all */
      static double version_t;
      static int version_n;
      if (t - version_t >= 1) version_t = t, version_n = 0;
      if (n >= 11 && (p[4] == T_RHOST || p[4] == T_RJOIN) && version_n++ < 20) reply(s, &from, p[4], NULL);
      continue;
    }
    int type = p[4];
    /* who is this? */
    Game *g = NULL;
    bool is_host = false;
    int hosted = 0;
    for (int i = 0; i < MAX_GAMES; i++) {
      if (!games[i].used) continue;
      if (games[i].host.sin_addr.s_addr == from.sin_addr.s_addr) hosted++;
      if (g) continue;
      if (same(&games[i].host, &from)) g = &games[i], is_host = true;
      else if (games[i].has_guest && same(&games[i].guest, &from)) g = &games[i];
    }
    if (type == T_RHOST) {
      if (n < 11 || !valid_code(p + 5)) continue;
      char code[7] = { 0 };
      memcpy(code, p + 5, 6);
      if (g && is_host) { /* already ours: confirm it again */
        g->host_t = t;
        reply(s, &from, T_RCODE, g->code);
        continue;
      }
      Game *other = find_code(code);
      if (other || hosted >= HOSTS_PER) { /* taken (the host picks another), or too many */
        reply(s, &from, T_RFAIL, NULL);
        continue;
      }
      int k = 0;
      while (k < MAX_GAMES && games[k].used) k++;
      if (k == MAX_GAMES) continue;
      g = &games[k];
      memset(g, 0, sizeof *g);
      memcpy(g->code, code, 7);
      g->used = true;
      g->host = from;
      g->host_t = t;
      printf("game hosted by %s\n", text(&from)); /* codes are secrets: never logged */
      fflush(stdout);
      reply(s, &from, T_RCODE, g->code);
      continue;
    }
    if (type == T_RJOIN) {
      if (n < 11 || !valid_code(p + 5)) continue;
      char code[7] = { 0 };
      memcpy(code, p + 5, 6);
      int r = join_reply(&from, code, t);
      if (r) reply(s, &from, r, r == T_RJOIN ? code : NULL);
      continue;
    }
    if (!g) continue;
    if (is_host) g->host_t = t;
    else g->guest_t = t;
    if (type == T_PING) continue;
    /* everything else goes to the other one */
    if (is_host && g->has_guest) sendto(s, p, (size_t)n, 0, (const struct sockaddr *)&g->guest, sizeof g->guest);
    else if (!is_host) sendto(s, p, (size_t)n, 0, (const struct sockaddr *)&g->host, sizeof g->host);
  }
}
