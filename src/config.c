/* config.c - settings.ini in %APPDATA%\TailonLock or ~/.config/tailonlock (copied from the old Ace4Combat folder once)
 * (JS settings and localStorage). Plain key=value lines; unknown keys are
 * ignored so older and newer versions can share the file. */
#include "config.h"
#include "brand.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/jets.h"
#include "music.h"
#include "platform/plat.h"

void config_defaults(Settings *s) {
  s->mode = MODE_1P;
  s->coop = false;
  s->commander = true;
  s->diff = DIFF_NORMAL;
  s->invert = false;
  s->sound = true;
  s->music = true;
  s->intro = true;
  s->gforce = true;
  s->modern = true;
  s->gfx = 2;
  s->track = music_theme_index("fullburn"); /* the main theme, Full Burn, in the menu and the game */
  s->ground = true;
  s->crash = true;
  s->lives = 3;
  s->hits = 3;
  for (int i = 0; i < 2; i++) {
    s->load[i][0] = 6; /* 6 heat-seekers and an air-to-ground missile: the modern 6 too */
    s->load[i][1] = 0;
    s->load[i][2] = 1;
  }
  s->jets[0] = 0;
  s->jets[1] = NJETS > 1 ? 1 : 0;
  s->map = assets_map_count() > 0 ? 0 : -1;
  s->tod = TOD_DAY;
  s->weather = -1;
  s->net_join = false;
  s->net_via = NV_LAN;
  snprintf(s->net_name, sizeof s->net_name, "PILOT");
  s->net_addr[0] = 0;
  snprintf(s->net_relay, sizeof s->net_relay, "localhost");
  s->net_code[0] = 0;
}

static bool ini_path(char *buf, size_t n, bool create) {
  char dir[512];
  if (!plat_config_dir(dir, sizeof dir)) return false;
  if (create) SDL_CreateDirectory(dir);
  int k = snprintf(buf, n, "%s/settings.ini", dir);
  return k > 0 && (size_t)k < n;
}

void config_set(Settings *s, const char *key, const char *val) {
  if (!strcmp(key, "mode"))
    s->mode = !strcmp(val, "2p") ? MODE_2P : !strcmp(val, "defend") ? MODE_DEFEND : !strcmp(val, "drone") ? MODE_DRONE : MODE_1P;
  else if (!strcmp(key, "drone")) s->drone2 = !strcmp(val, "p2");
  else if (!strcmp(key, "net_drone")) s->net_drone = atoi(val) % 3; /* not saved: the next host picks again */
  else if (!strcmp(key, "crew")) s->commander = strcmp(val, "pilot") != 0;
  else if (!strcmp(key, "match")) s->coop = !strcmp(val, "coop");
  else if (!strcmp(key, "diff")) s->diff = !strcmp(val, "easy") ? DIFF_EASY : !strcmp(val, "hard") ? DIFF_HARD : DIFF_NORMAL;
  else if (!strcmp(key, "invert")) s->invert = !strcmp(val, "1");
  else if (!strcmp(key, "sound")) s->sound = strcmp(val, "0") != 0;
  else if (!strcmp(key, "music")) s->music = strcmp(val, "0") != 0;
  else if (!strcmp(key, "intro")) s->intro = strcmp(val, "0") != 0;
  else if (!strcmp(key, "gforce")) s->gforce = strcmp(val, "0") != 0;
  else if (!strcmp(key, "rules")) s->classic = !strcmp(val, "classic");
  else if (!strcmp(key, "size")) s->pairs = !strcmp(val, "2v2");
  else if (!strcmp(key, "start")) s->ground = strcmp(val, "air") != 0;
  else if (!strcmp(key, "handling")) s->realistic = !strcmp(val, "realistic");
  else if (!strcmp(key, "scenario")) s->dogfight = !strcmp(val, "dogfight");
  else if (!strcmp(key, "crash")) s->crash = strcmp(val, "0") != 0;
  else if (!strcmp(key, "planes")) s->lives = atoi(val) < 1 ? 1 : atoi(val) > 20 ? 20 : atoi(val);
  else if (!strcmp(key, "hits")) s->hits = atoi(val) < 1 ? 1 : atoi(val) > 3 ? 3 : atoi(val);
  else if (!strncmp(key, "load", 4) && (key[4] == '1' || key[4] == '2')) {
    int l[3];
    if (sscanf(val, "%d,%d,%d", &l[0], &l[1], &l[2]) == 3 && game_load_fits(l)) memcpy(s->load[key[4] - '1'], l, sizeof l);
  }
  else if (!strcmp(key, "renderer")) s->modern = strcmp(val, "retro") != 0;
  else if (!strcmp(key, "graphics")) s->gfx = !strcmp(val, "low") ? 0 : !strcmp(val, "medium") ? 1 : !strcmp(val, "ultra") ? 3 : 2;
  else if (!strcmp(key, "track")) s->track = music_theme_index(val);
  else if (!strcmp(key, "map")) s->map = assets_map_find(val);
  else if (!strcmp(key, "time")) s->tod = !strcmp(val, "dawn") ? TOD_DAWN : !strcmp(val, "dusk") ? TOD_DUSK : !strcmp(val, "night") ? TOD_NIGHT : TOD_DAY;
  else if (!strcmp(key, "weather"))
    s->weather = !strcmp(val, "clear") ? WX_CLEAR : !strcmp(val, "scattered") ? WX_SCATTERED : !strcmp(val, "overcast") ? WX_OVERCAST : !strcmp(val, "storm") ? WX_STORM : -1;
  else if (!strcmp(key, "jet1") && jets_find(val) >= 0) s->jets[0] = jets_find(val);
  else if (!strcmp(key, "jet2") && jets_find(val) >= 0) s->jets[1] = jets_find(val);
  else if (!strcmp(key, "net")) s->net_join = !strcmp(val, "join");
  else if (!strcmp(key, "net_via")) s->net_via = !strcmp(val, "address") ? NV_ADDRESS : !strcmp(val, "relay") ? NV_RELAY : NV_LAN;
  else if (!strcmp(key, "net_name")) snprintf(s->net_name, sizeof s->net_name, "%s", val);
  else if (!strcmp(key, "net_addr")) snprintf(s->net_addr, sizeof s->net_addr, "%s", val);
  else if (!strcmp(key, "net_relay")) snprintf(s->net_relay, sizeof s->net_relay, "%s", val);
  else if (!strcmp(key, "net_code")) snprintf(s->net_code, sizeof s->net_code, "%s", val);
}

void config_load(Settings *s) {
  config_defaults(s);
  char path[600], line[128];
  if (!ini_path(path, sizeof path, false)) return;
  FILE *f = fopen(path, "r");
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    char key[32], val[64];
    if (sscanf(line, " %31[^= ] = %63s", key, val) != 2) continue;
    config_set(s, key, val);
  }
  fclose(f);
}

void config_save(const Settings *s) {
  char path[600];
  if (!ini_path(path, sizeof path, true)) return;
  FILE *f = fopen(path, "w");
  if (!f) return;
  static const char *const diff[] = { "easy", "normal", "hard" };
  static const char *const tods[] = { "dawn", "day", "dusk", "night" };
  static const char *const wxs[] = { "clear", "scattered", "overcast", "storm" };
  fprintf(f, "# " GAME_TITLE " settings\nmode=%s\ncrew=%s\nmatch=%s\ndiff=%s\ninvert=%d\nsound=%d\nmusic=%d\ntrack=%s\njet1=%s\njet2=%s\nmap=%s\ntime=%s\nweather=%s\nintro=%d\ngforce=%d\nrules=%s\nsize=%s\nstart=%s\nhandling=%s\nscenario=%s\ncrash=%d\nplanes=%d\nhits=%d\nload1=%d,%d,%d\nload2=%d,%d,%d\nrenderer=%s\ngraphics=%s\n",
          s->mode == MODE_2P ? "2p" : s->mode == MODE_DEFEND ? "defend" : s->mode == MODE_DRONE ? "drone" : "1p", s->commander ? "commander" : "pilot", s->coop ? "coop" : "vs", diff[s->diff], s->invert, s->sound, s->music,
          THEMES[s->track].key, JETS[s->jets[0]].id, JETS[s->jets[1]].id, assets_map_id(s->map), tods[s->tod],
          s->weather < 0 ? "auto" : wxs[s->weather], s->intro, s->gforce, s->classic ? "classic" : "modern", s->pairs ? "2v2" : "1v1", s->ground ? "ground" : "air", s->realistic ? "realistic" : "arcade",
          s->dogfight ? "dogfight" : "full", s->crash, s->lives, s->hits, s->load[0][0], s->load[0][1], s->load[0][2],
          s->load[1][0], s->load[1][1], s->load[1][2], s->modern ? "modern" : "retro",
          (const char *[]){ "low", "medium", "high", "ultra" }[s->gfx & 3]);
  fprintf(f, "drone=%s\n", s->drone2 ? "p2" : "cpu");
  fprintf(f, "net=%s\nnet_via=%s\nnet_name=%s\nnet_relay=%s\n", s->net_join ? "join" : "host",
          (const char *[]){ "lan", "address", "relay" }[s->net_via % NV_COUNT], s->net_name[0] ? s->net_name : "PILOT", s->net_relay[0] ? s->net_relay : "localhost");
  if (s->net_addr[0]) fprintf(f, "net_addr=%s\n", s->net_addr);
  if (s->net_code[0]) fprintf(f, "net_code=%s\n", s->net_code);
  fclose(f);
}
