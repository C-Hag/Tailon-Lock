/* lf_config.c - a libFuzzer target for settings.ini (src/config.c): the
 * input is the file, which config_load reads line by line into config_set;
 * the settings are then saved (where every value picks a name from a table)
 * and read back. Every value must stay in the range config.h gives it, after
 * the first read and after the second; one outside aborts. Original code for
 * this project. Build with -DTL_LIBFUZZER=ON (clang); run it under ASan and
 * UBSan.
 *
 * The file is TL_CONFIG_DIR/settings.ini in the fuzzer's own folder (never
 * the player's). The maps' index lists the four real maps, so a map setting
 * can find one. */
#include "lf_common.h" /* first: it asks for POSIX */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "config.h"
#include "core/jets.h"
#include "core/world.h"
#include "music.h"

/* ---- stand-ins for the sound side music.c calls ---- */
void sfx_lock(void) {}
void sfx_unlock(void) {}
float *plat_load_wav(const char *path, int rate, size_t *frames) { return (void)path, (void)rate, (void)frames, NULL; }

static void in_range(const Settings *s, const char *when) {
  const char *bad = NULL;
  if (s->mode < MODE_1P || s->mode > MODE_DRONE) bad = "mode";
  else if (s->diff < DIFF_EASY || s->diff > DIFF_HARD) bad = "diff";
  else if (s->volume < 1 || s->volume > 10) bad = "volume";
  else if (s->lives < 1 || s->lives > 20) bad = "planes";
  else if (s->hits < 1 || s->hits > 3) bad = "hits";
  else if (!game_load_fits(s->load[0]) || !game_load_fits(s->load[1])) bad = "load";
  else if (s->gfx < 0 || s->gfx > 3) bad = "graphics";
  else if (s->track < 0 || s->track >= MUSIC_ALL) bad = "track";
  else if (s->jets[0] < 0 || s->jets[0] >= NJETS || s->jets[1] < 0 || s->jets[1] >= NJETS) bad = "jet";
  else if (s->map < -1 || s->map >= assets_map_count()) bad = "map";
  else if (s->tod < 0 || s->tod >= TOD_COUNT) bad = "time";
  else if (s->weather < -1 || s->weather >= WX_COUNT) bad = "weather";
  else if (s->net_via < 0 || s->net_via >= NV_COUNT) bad = "net_via";
  else if (s->net_drone < 0 || s->net_drone > 2) bad = "net_drone";
  if (bad) {
    fprintf(stderr, "lf_config: %s out of its range %s\n", bad, when);
    abort();
  }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n) {
  static bool ready;
  if (!ready) {
    static const char *const subs[] = { "assets", "assets/maps", NULL };
    static const char INDEX[] = "[{\"id\":\"dune-sea\",\"name\":\"Dune Sea\"},{\"id\":\"high-valley\",\"name\":\"High "
                                "Valley\"},{\"id\":\"iron-coast\",\"name\":\"Iron Coast\"},{\"id\":\"twin-isles\","
                                "\"name\":\"Twin Isles\"}]";
    char d[600];
    snprintf(d, sizeof d, "%s/assets/", lf_folder(subs));
    lf_write("assets/maps/maps_index.json", INDEX, sizeof INDEX - 1);
    assets_fuzz_dir(d);
    assets_fuzz_map(); /* the index; the map itself is not there, and need not be */
    jets_finish();     /* as the game starts: the built-in jet when no jets were loaded */
    ready = true;
  }
  lf_write("settings.ini", data, n);
  Settings s, again;
  config_load(&s);
  in_range(&s, "after reading the file");
  config_save(&s);
  config_load(&again);
  in_range(&again, "after saving and reading again");
  return 0;
}
