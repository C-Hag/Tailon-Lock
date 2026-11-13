/* test_ui.c - the menus' keys, on ui.c's own functions: the pause for a lost controller has one button, QUIT TO
 * MENU, and Enter takes it. Original code for this project. */
#include "../src/ui.c"

/* what ui.c asks of the rest of the game (nothing of it is shown here) */
/* every theme named: the settings page prints the name (an all-zero table gives printf a null %s) */
#define T_ { "test", "TEST", 1, 1, 1 }
const Theme THEMES[MUSIC_ALL] = { T_, T_, T_, T_, T_, T_ };
_Static_assert(MUSIC_ALL == 6, "one T_ per theme");
const JetSpec *assets_jet_spec(int jet) { return (void)jet, NULL; }
int assets_map_count(void) { return 0; }
const char *assets_map_name(int i) { return (void)i, ""; }
bool gpu_active(void) { return false; }
const char *gpu_driver(void) { return ""; }
bool intro_available(void) { return false; }
bool net_active(void) { return false; }
bool net_playing(void) { return false; }
bool net_is_host(void) { return false; }
const char *net_status(void) { return ""; }
const char *net_code(void) { return ""; }
int net_lan_count(void) { return 0; }
const char *net_lan_name(int i) { return (void)i, ""; }
const char *net_lan_addr(int i) { return (void)i, ""; }
double sfx_listened(void) { return 0; }
bool plat_file_sync(FILE *f) { return (void)f, false; }
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }
void plat_log(const char *fmt, ...) { (void)fmt; } /* plat_common.c's TL_FILES line (tools/ff/inventory) */

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_ui:%d: %s\n", __LINE__, #c);               \
      fails++;                                                         \
    }                                                                  \
  } while (0)

int main(void) {
  static Settings st;
  /* a pad went away: the pause opens, then says so */
  ui_open(SCREEN_PAUSE);
  ui_pad_wait(1, 0);
  CHECK(ui_nav(SCREEN_PAUSE, &st, NAV_OK) == UI_TO_MENU); /* Enter: QUIT TO MENU, the one button */
  CHECK(ui_nav(SCREEN_PAUSE, &st, NAV_DOWN) == UI_NONE);
  CHECK(ui_nav(SCREEN_PAUSE, &st, NAV_OK) == UI_TO_MENU); /* the arrows don't move off it */
  /* the pad came back: the plain pause, RESUME first */
  ui_pad_wait(0, 0);
  ui_open(SCREEN_PAUSE);
  CHECK(ui_nav(SCREEN_PAUSE, &st, NAV_OK) == UI_RESUME);
  if (fails) fprintf(stderr, "test_ui: %d failed\n", fails);
  else printf("test_ui: ok\n");
  return fails != 0;
}
