/* wl_poke.c - brick P3's hands on Wayland: presses keys and moves and clicks
 * the pointer through KWin's fake-input protocol (org_kde_kwin_fake_input,
 * from its protocol file), as a person would, so the game's own Wayland code
 * reads them. Test tool only, never part of the game. Original code for this
 * project, written from the Wayland protocol specification and the
 * fake-input protocol file.
 *
 * usage: wl_poke STEP...   each step: key:CODE (a Linux key code) | click:X:Y (on the output, in its units) | wait:MS */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct wl_message { const char *name, *signature; const struct wl_interface **types; };
struct wl_interface { const char *name; int version, method_count; const struct wl_message *methods; int event_count; const struct wl_message *events; };
typedef struct wl_proxy P;

static const struct wl_interface *none[4];
static const struct wl_message fake_req[] = {
  { "authenticate", "ss", none }, { "pointer_motion", "ff", none }, { "button", "uu", none }, { "axis", "uf", none },
  { "touch_down", "2uff", none }, { "touch_motion", "2uff", none }, { "touch_up", "2u", none }, { "touch_cancel", "2", none },
  { "touch_frame", "2", none }, { "pointer_motion_absolute", "3ff", none }, { "keyboard_key", "4uu", none },
};
static const struct wl_interface fake_i = { "org_kde_kwin_fake_input", 4, 11, fake_req, 0, NULL };

static P *(*marshal)(P *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
static int (*add_listener)(P *, void (**)(void), void *);
static const struct wl_interface *reg_i;
static P *fake;
static uint32_t fake_v;

static void global(void *d, P *r, uint32_t name, const char *iface, uint32_t v) {
  (void)d;
  if (!strcmp(iface, "org_kde_kwin_fake_input")) {
    fake_v = v < 4 ? v : 4;
    fake = marshal(r, 0, &fake_i, fake_v, 0, name, fake_i.name, fake_v, NULL);
  }
}
static void global_remove(void *d, P *r, uint32_t n) { (void)d, (void)r, (void)n; }
static void (*const reg_ls[])(void) = { (void (*)(void))global, (void (*)(void))global_remove };

static void nap(int ms) {
  struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000 };
  nanosleep(&t, NULL);
}

int main(int argc, char **argv) {
  void *lib = dlopen("libwayland-client.so.0", RTLD_NOW);
  if (!lib) return 77;
  void *(*connect)(const char *) = (void *(*)(const char *))dlsym(lib, "wl_display_connect");
  int (*roundtrip)(void *) = (int (*)(void *))dlsym(lib, "wl_display_roundtrip");
  int (*flush)(void *) = (int (*)(void *))dlsym(lib, "wl_display_flush");
  *(void **)&marshal = dlsym(lib, "wl_proxy_marshal_flags");
  *(void **)&add_listener = dlsym(lib, "wl_proxy_add_listener");
  reg_i = dlsym(lib, "wl_registry_interface");
  void *d = connect ? connect(NULL) : NULL;
  if (!d || !marshal || !add_listener || !reg_i) return fprintf(stderr, "wl_poke: no display\n"), 1;
  P *reg = marshal((P *)d, 1, reg_i, 1, 0, NULL);
  add_listener(reg, (void (**)(void))reg_ls, NULL);
  roundtrip(d);
  if (!fake || fake_v < 4) return fprintf(stderr, "wl_poke: the compositor has no fake input (v4)\n"), 77;
  marshal(fake, 0, NULL, fake_v, 0, "wl_poke", "platform brick P3");
  roundtrip(d);
  for (int i = 1; i < argc; i++) {
    unsigned code;
    int x, y, ms;
    if (sscanf(argv[i], "key:%u", &code) == 1) {
      marshal(fake, 10, NULL, fake_v, 0, code, 1u), flush(d), nap(60);
      marshal(fake, 10, NULL, fake_v, 0, code, 0u);
    } else if (sscanf(argv[i], "click:%d:%d", &x, &y) == 2) {
      marshal(fake, 9, NULL, fake_v, 0, (int32_t)(x * 256), (int32_t)(y * 256)), flush(d), nap(60);
      marshal(fake, 2, NULL, fake_v, 0, 0x110u, 1u), flush(d), nap(60); /* BTN_LEFT */
      marshal(fake, 2, NULL, fake_v, 0, 0x110u, 0u);
    } else if (sscanf(argv[i], "wait:%d", &ms) == 1) {
      flush(d), nap(ms);
    }
    roundtrip(d);
  }
  return 0;
}
