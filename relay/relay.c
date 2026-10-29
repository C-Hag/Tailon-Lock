/* relay.c - matchmaking and forwarding server for online play.
 * A host registers its 6-digit game code; a guest joins with the code; from
 * then on the relay forwards each one's UDP packets to the other, so neither
 * needs to forward a port on their router. Runs on any small Linux box with
 * UDP port 27970 open:
 *
 *   a4c_relay [port]
 *
 * The match itself is encrypted end to end with a key made from the code
 * (src/net.c), so the relay only sees sealed packets. Because a guest can
 * ask whether a code exists, each address gets few wrong guesses: 10 a
 * minute, and the relay as a whole 600 a minute.
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

#define VERSION 4 /* the same as src/net.c: the relay drops packets of any other version */
enum { T_RHOST = 6, T_RCODE, T_RJOIN, T_RFAIL, T_PING };
#define MAX_GAMES  1024
#define IDLE_S     60  /* a game nobody has sent anything for is dropped */
#define GUEST_S    10  /* a guest silent this long can be replaced by a new one */
#define HOSTS_PER  4   /* games one address may host at once */
#define MISS_PER   10  /* wrong codes one address may try a minute */
#define MISS_ALL   600 /* wrong codes everyone together may try a minute */

typedef struct sockaddr_in Addr;
typedef struct {
  bool   used, has_guest;
  char   code[7];
  Addr   host, guest;
  double host_t, guest_t;
} Game;

static Game games[MAX_GAMES];
/* wrong guesses this minute, per address (hashed) and in all */
static struct { uint32_t ip; int n; } misses[4096];
static int miss_all;

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

/* The miss counter slot of an address, or NULL when it has used its tries. */
static int *miss_slot(const Addr *a) {
  uint32_t ip = a->sin_addr.s_addr, h = (ip * 2654435761u) >> 20;
  if (misses[h].ip != ip) {
    misses[h].ip = ip;
    misses[h].n = 0;
  }
  return &misses[h].n;
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
      miss_all = 0;
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
    if (n < 5 || p[0] != 'A' || p[1] != '4' || p[2] != 'C' || p[3] != VERSION) continue;
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
      Game *h = find_code(code);
      if (!h) {
        int *m = miss_slot(&from);
        if (*m >= MISS_PER || miss_all >= MISS_ALL) continue; /* out of guesses: say nothing */
        (*m)++;
        miss_all++;
        reply(s, &from, T_RFAIL, NULL);
        continue;
      }
      if (!h->has_guest || same(&h->guest, &from) || t - h->guest_t > GUEST_S) {
        if (!h->has_guest || !same(&h->guest, &from)) printf("game joined by %s\n", text(&from));
        fflush(stdout);
        h->guest = from;
        h->has_guest = true;
        h->guest_t = t;
        reply(s, &from, T_RJOIN, h->code);
      } else reply(s, &from, T_RFAIL, NULL); /* someone else is playing it */
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
