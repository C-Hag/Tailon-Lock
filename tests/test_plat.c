/* test_plat.c - the brick 9 audit of plat.h: the platform owns the loop and
 * drives this program through its callbacks (init, event, frame, quit), and
 * touch comes through as several fingers, each with its own id, down, move
 * and up. The backend's own events are put in at the system's level (the
 * core's queue, as a backend reports them), ahead of the ones the game pushes.
 * Original code for this project. */
#include <stdio.h>
#include <string.h>

#include "platform/plat_core.h"

static int fails, nframes, ninits, nquits;
static PlatEvent got[32];
static int ngot;

#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_plat:%d: %s\n", __LINE__, #c);             \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static void finger(PlatEventType type, uint64_t id, float x, float y) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = type;
  e.touch.id = id;
  e.touch.x = x;
  e.touch.y = y;
  e.touch.pressure = 1;
  plat_core_event(&e);
}

static bool on_init(int argc, char **argv, int *code) {
  (void)argc;
  (void)argv;
  ninits++;
  if (!plat_init(PLAT_VIDEO, "test", "0", "test.plat")) {
    fprintf(stderr, "test_plat: no video: %s\n", plat_error());
    *code = 1;
    return false;
  }
  /* two fingers down, both move, the first lifts, then the second */
  finger(PE_TOUCH_DOWN, 11, 0.25f, 0.5f);
  finger(PE_TOUCH_DOWN, 22, 0.75f, 0.5f);
  finger(PE_TOUCH_MOVE, 11, 0.30f, 0.45f);
  finger(PE_TOUCH_MOVE, 22, 0.70f, 0.55f);
  finger(PE_TOUCH_UP, 11, 0.30f, 0.45f);
  finger(PE_TOUCH_UP, 22, 0.70f, 0.55f);
  /* a key, and an event the game pushes itself, which comes after the system's */
  PlatEvent k;
  memset(&k, 0, sizeof k);
  k.type = PE_KEY_DOWN;
  k.key.code = PK_A;
  k.key.key = 'a';
  plat_core_event(&k);
  PlatEvent mine = { 0 };
  mine.type = PE_QUIT;
  plat_push(&mine);
  return true;
}

static void on_event(const PlatEvent *ev) {
  if (ngot < 32 && ev->type != PE_FOCUS_GAINED && ev->type != PE_FOCUS_LOST && ev->type != PE_RESIZED) got[ngot++] = *ev;
}

static bool on_frame(void) { return ++nframes < 3; }

static int on_quit(void) {
  nquits++;
  CHECK(ninits == 1 && nframes == 3 && nquits == 1); /* the loop: init once, frames until one says stop, quit once */
  CHECK(ngot == 8);
  static const PlatEventType T[6] = { PE_TOUCH_DOWN, PE_TOUCH_DOWN, PE_TOUCH_MOVE, PE_TOUCH_MOVE, PE_TOUCH_UP, PE_TOUCH_UP };
  static const uint64_t ID[6] = { 11, 22, 11, 22, 11, 22 };
  static const float X[6] = { 0.25f, 0.75f, 0.30f, 0.70f, 0.30f, 0.70f };
  for (int i = 0; i < 6 && i < ngot; i++) {
    CHECK(got[i].type == T[i]);
    CHECK(got[i].touch.id == ID[i]);
    CHECK(got[i].touch.x == X[i]);
    CHECK(got[i].touch.pressure == 1);
  }
  if (ngot >= 8) {
    CHECK(got[6].type == PE_KEY_DOWN && got[6].key.code == PK_A && got[6].key.key == 'a');
    CHECK(got[7].type == PE_QUIT);
  }
  plat_quit();
  printf("test_plat: %s (%d events, %d frames)\n", fails ? "FAIL" : "ok", ngot, nframes);
  return fails ? 1 : 0;
}

const PlatApp *plat_app(void) {
  static const PlatApp A = { on_init, on_event, on_frame, on_quit };
  return &A;
}
