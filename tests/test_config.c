/* test_config.c - settings.ini read as if hostile: every value it sets is held to its range, whatever the file
 * says (a hand-edited file, or --set). Original code for this project. */
#include "../src/config.c"

/* the maps and the music the settings name */
int assets_map_count(void) { return 3; }
int assets_map_find(const char *id) { return !strcmp(id, "twin-isles") ? 1 : -1; }
const char *assets_map_id(int i) { return i == 1 ? "twin-isles" : "classic"; }
int music_theme_index(const char *key) { return (void)key, 0; }
const Theme THEMES[MUSIC_ALL];
/* no settings folder: nothing is read or written */
int plat_config_dir(char *buf, size_t size) { return (void)buf, (void)size, 0; }
bool plat_make_dir(const char *path) { return (void)path, false; }
bool plat_file_sync(FILE *f) { return (void)f, false; }
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }
void plat_log(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_config:%d: %s\n", __LINE__, #c);           \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static const char *const KEYS[] = { "net_drone", "volume", "planes", "hits", "mode", "diff", "graphics", "time",
                                    "weather", "net_via", "map", "track", "jet1", "jet2" };
static const char *const VALS[] = { "-1", "-2", "-3", "3", "4", "5", "-2147483648", "2147483647",
                                    "-9223372036854775808", "99999999999999999999", "0", "x", "", "1e9" };

static void in_range(const Settings *s) {
  CHECK(s->net_drone >= 0 && s->net_drone <= 2);
  CHECK(s->volume >= 1 && s->volume <= 10);
  CHECK(s->lives >= 1 && s->lives <= 20);
  CHECK(s->hits >= 1 && s->hits <= 3);
  CHECK(s->mode >= MODE_1P && s->mode <= MODE_DRONE);
  CHECK(s->diff >= DIFF_EASY && s->diff <= DIFF_HARD);
  CHECK(s->gfx >= 0 && s->gfx <= 3);
  CHECK(s->tod >= TOD_DAWN && s->tod <= TOD_NIGHT);
  CHECK(s->weather >= -1 && s->weather <= WX_STORM);
  CHECK(s->net_via >= NV_LAN && s->net_via <= NV_RELAY);
  CHECK(s->map >= -1 && s->map < assets_map_count());
  for (int i = 0; i < 2; i++) CHECK(s->jets[i] >= 0 && s->jets[i] < (NJETS > 0 ? NJETS : 1)); /* no jet files here */
}

int main(void) {
  for (size_t k = 0; k < sizeof KEYS / sizeof *KEYS; k++)
    for (size_t v = 0; v < sizeof VALS / sizeof *VALS; v++) {
      Settings s;
      config_defaults(&s);
      config_set(&s, KEYS[k], VALS[v]);
      in_range(&s);
    }
  Settings s;
  config_defaults(&s);
  config_set(&s, "net_drone", "2");
  CHECK(s.net_drone == 2); /* a real value stays */
  config_set(&s, "net_drone", "1");
  CHECK(s.net_drone == 1);
  if (fails) fprintf(stderr, "test_config: %d failed\n", fails);
  else printf("test_config: ok\n");
  return fails != 0;
}
