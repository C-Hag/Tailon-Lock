/* config.h - settings.ini in the per-user config folder (JS settings/localStorage). */
#ifndef A4C_CONFIG_H
#define A4C_CONFIG_H

#include <stdbool.h>

#include "core/game.h"

typedef struct {
  GameMode   mode;   /* MODE_1P, MODE_2P or MODE_DEFEND */
  bool       coop;
  bool       commander; /* defend: player 2 commands the carrier */
  bool       drone2;    /* beat the drone: player 2 flies the drone (else the CPU) */
  Difficulty diff;
  bool       invert, sound, music, intro, gforce;
  bool       classic; /* classic keys and rules */
  bool       pairs;   /* 2 vs 2 */
  bool       ground;  /* start on the carrier and the runway rather than in the air */
  bool       realistic; /* handling: realistic (g-limited) or arcade */
  bool       dogfight, crash; /* classic rules: close range dogfight, crash detection */
  int        lives, hits;     /* classic rules: planes each (1-20), missile hits to kill (1-3) */
  int        load[2][3];      /* each seat's arming: heat, radar, air-to-ground */
  bool       modern;  /* GPU renderer (takes effect at the next start) */
  int        gfx;     /* GQ_* */
  int        track;   /* index into THEMES (music.h) */
  int        jets[2]; /* each local seat's jet, index into JETS */
  int        map;     /* index into the assets' maps, -1 for the classic arena */
  int        tod;     /* TOD_* */
  int        weather; /* WX_*, or -1 for the map's usual weather */
  /* network play: host or join, by LAN, address or relay */
  bool       net_join;
  int        net_drone;      /* host a drone match: 0 no, 1 the guest flies the drone, 2 the host does */
  int        net_via;        /* NV_* */
  char       net_name[16];   /* shown to the other player */
  char       net_addr[48];   /* the host to join, "1.2.3.4" or "name:port" */
  char       net_relay[48];  /* the relay server */
  char       net_code[8];    /* the relay game code to join */
} Settings;

enum { NV_LAN, NV_ADDRESS, NV_RELAY, NV_COUNT };

void config_defaults(Settings *s);
void config_load(Settings *s);
/* One settings.ini line: key and value (also --set key=value). */
void config_set(Settings *s, const char *key, const char *val);
void config_save(const Settings *s);

#endif
