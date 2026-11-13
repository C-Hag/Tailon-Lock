/* test_editor.c - the song editor's mouse and keys, on editor.c's own functions: a key that changes the notes
 * ends a drag (the drag's saved start is for the notes it began with), and a note added by double-click is
 * held to the notes a song can play. Original code for this project. */
#include "../src/editor.c"

/* what editor.c calls of the sound, the music and the platform */
void sfx_lock(void) {}
void sfx_unlock(void) {}
uint64_t plat_ms(void) { return 1000; } /* clicks in the same moment: a double click */
int plat_mods(void) { return 0; }
const char *plat_error(void) { return ""; }
void plat_log(const char *fmt, ...) { (void)fmt; }
bool plat_file_sync(FILE *f) { return (void)f, false; }
bool plat_file_replace(const char *from, const char *to) { return (void)from, (void)to, false; }
/* every theme named: the editor prints the name (an all-zero table gives printf a null %s) */
#define T_ { "test", "TEST", 1, 1, 1 }
const Theme THEMES[MUSIC_ALL] = { T_, T_, T_, T_, T_, T_ };
_Static_assert(MUSIC_ALL == 6, "one T_ per theme");
void music_play(int t) { (void)t; }
void music_restart(int t) { (void)t; }
void music_level(bool in_game) { (void)in_game; }
struct Score *music_score(int t) { return (void)t, NULL; }
const char *assets_path(const char *rel) { return rel; }
struct Json *assets_json(const char *rel) { return (void)rel, NULL; }

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_editor:%d: %s\n", __LINE__, #c);           \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static Score S;

static void fresh(void) {
  free(S.notes);
  memset(&S, 0, sizeof S);
  S.ninst = 1;
  S.end = S.loop = 8;
  synth_add_note(&S, 0, 0, 60, 0.5f, 0.9f);
  synth_add_note(&S, 1, 0, 64, 0.5f, 0.9f);
  sc = &S;
  track = 0;
  last_click = 0; /* no click just before */
}

static void test_drag_ends(void) {
  static const int KEYS[] = { 'z', 127, 'c' };
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
    fresh();
    S.notes[0].sel = true;
    mouse_down(PBTN_LEFT, x_of(S.notes[0].t) + 1, y_of(S.notes[0].midi) + 2, false);
    CHECK(drag == DRAG_MOVE);
    PlatKey k = { 0 };
    k.key = KEYS[i];
    k.mod = KEYS[i] == 'z' ? PMOD_CTRL : 0;
    key_down(&k);
    CHECK(drag == DRAG_NONE); /* the notes changed under it: the drag is over */
    mouse_move(x_of(3), y_of(70));
  }
}

static void test_add_clamped(void) {
  fresh();
  midi_top = -200; /* scrolled far down with the arrow keys */
  for (int c = 0; c < 2; c++) { /* a double click on an empty place: a new note there */
    mouse_down(PBTN_LEFT, x_of(2.3f), y_of(midi_top - 5) + 2, false);
    mouse_up();
  }
  int added = -1;
  for (int k = 0; k < S.nnotes; k++)
    if (S.notes[k].midi != 60 && S.notes[k].midi != 64) added = S.notes[k].midi;
  CHECK(S.nnotes == 3 && added == 12); /* the lowest a song plays, not a pitch wrapped round through int8_t */
  midi_top = 60;
}

int main(void) {
  test_drag_ends();
  test_add_clamped();
  if (fails) fprintf(stderr, "test_editor: %d failed\n", fails);
  else printf("test_editor: ok\n");
  return fails != 0;
}
