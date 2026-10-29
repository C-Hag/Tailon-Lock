/* net.h - network play over UDP, on a LAN or across the internet (JS NET,
 * netPublish, netIngest, netApplyRemote). As in the browser game, each
 * computer flies its own jets and sends their state about 30 times a
 * second; the other computer eases its copies toward it. Hits are decided
 * on the computer that owns the jet or target that is hit; missiles,
 * decoys and blasts travel as events. The host runs the CPU jets and picks
 * the map and the rules.
 *
 * Three ways to meet: on a LAN the host is found by a broadcast beacon; over
 * the internet the guest types the host's address (UDP NET_PORT forwarded
 * on the host's router), or both go through a relay server (relay/) with a
 * game code and need no port forwarding.
 *
 * Every way needs the host's 6-digit game code: it becomes a secret key the
 * two computers share, and the match travels encrypted under it (net.c). */
#ifndef A4C_NET_H
#define A4C_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "core/game.h"

#define NET_PORT   27960 /* the host's game port */
#define NET_LAN    27961 /* LAN beacons */
#define RELAY_PORT 27970 /* the relay server */

/* What the host decides for a match (and the guest's own jet and load). */
typedef struct {
  uint32_t game, seed;
  bool coop, pairs, classic, realistic, ground, gforce, dogfight, no_crash;
  int  diff, hits, lives, tod, weather;
  char map[24];
  int  jets[2], load[2][3]; /* [0] the host's seat, [1] the guest's */
} NetMatch;

typedef enum { NETE_NONE, NETE_START, NETE_LOST } NetEvent;

bool        net_init(void);
void        net_quit(void);
/* Host: wait for a guest on the LAN and on NET_PORT, or register with a relay;
 * makes a new game code (net_code). */
bool        net_host(const NetMatch *m, const char *name, const char *relay);
/* Guest: join a host by address ("1.2.3.4" or "host:port") or through a
 * relay, with the host's 6-digit code. */
bool        net_join(const char *addr, const char *code, int jet, const int *load, const char *name);
bool        net_join_code(const char *relay, const char *code, int jet, const int *load, const char *name);
void        net_close(void);
/* LAN games heard on the network, newest first. */
void        net_scan(bool on);
int         net_lan_count(void);
const char *net_lan_name(int i);
const char *net_lan_addr(int i);
/* Call every frame: answers, beacons, timeouts. NETE_START hands over the
 * match to start; NETE_LOST says the other computer is gone. */
NetEvent    net_poll(float dt, NetMatch *out);
/* After the game is made from the match: who flies what. */
void        net_setup(Game *g);
/* Every tick of a network match: take in the other side's state, and send ours. */
void        net_tick(Game *g, float dt);
/* Host, after a match: start another with the same guest. */
void        net_rematch(const NetMatch *m);
bool        net_active(void);  /* hosting, joining or playing */
bool        net_playing(void);
bool        net_is_host(void);
const char *net_status(void);
const char *net_code(void);    /* the game code while hosting, once it can be used */

#endif
