/* x11_poke.c - brick P3's hands on X11: presses keys and clicks in a window
 * through the XTEST extension (libXtst, opened at run time), as a person
 * would, so the game's own X11 code is what reads them. Test tool only,
 * never part of the game. Original code for this project, written from the
 * XTEST extension and Xlib documentation.
 *
 * usage: x11_poke STEP...   each step: key:CODE (an X key code) | click:X:Y (in the game's window: the first one
 * whose name starts with "Tailon", given the keyboard first; the screen if none) | wait:MS */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

typedef struct _XDisplay Display;
typedef unsigned long XID;

static int (*query_tree)(Display *, XID, XID *, XID *, XID **, unsigned *);
static int (*fetch_name)(Display *, XID, char **);
static int (*xfree)(void *);

static XID find(Display *d, XID w) { /* depth first: the game's window */
  char *name = NULL;
  if (fetch_name(d, w, &name) && name) {
    bool hit = !strncmp(name, "Tailon", 6);
    xfree(name);
    if (hit) return w;
  }
  XID root, parent, *kids = NULL;
  unsigned n = 0;
  XID got = 0;
  if (query_tree(d, w, &root, &parent, &kids, &n))
    for (unsigned i = 0; i < n && !got; i++) got = find(d, kids[i]);
  if (kids) xfree(kids);
  return got;
}

int main(int argc, char **argv) {
  void *x11 = dlopen("libX11.so.6", RTLD_NOW), *xt = dlopen("libXtst.so.6", RTLD_NOW);
  if (!x11 || !xt) {
    fprintf(stderr, "x11_poke: no libX11 or libXtst\n");
    return 77;
  }
  Display *(*open_display)(const char *) = (Display * (*)(const char *)) dlsym(x11, "XOpenDisplay");
  int (*flush)(Display *) = (int (*)(Display *))dlsym(x11, "XFlush");
  int (*key)(Display *, unsigned, int, unsigned long) = (int (*)(Display *, unsigned, int, unsigned long))dlsym(xt, "XTestFakeKeyEvent");
  int (*button)(Display *, unsigned, int, unsigned long) = (int (*)(Display *, unsigned, int, unsigned long))dlsym(xt, "XTestFakeButtonEvent");
  int (*motion)(Display *, int, int, int, unsigned long) = (int (*)(Display *, int, int, int, unsigned long))dlsym(xt, "XTestFakeMotionEvent");
  XID (*root_of)(Display *) = (XID(*)(Display *))dlsym(x11, "XDefaultRootWindow");
  int (*translate)(Display *, XID, XID, int, int, int *, int *, XID *) =
      (int (*)(Display *, XID, XID, int, int, int *, int *, XID *))dlsym(x11, "XTranslateCoordinates");
  int (*set_focus)(Display *, XID, int, unsigned long) = (int (*)(Display *, XID, int, unsigned long))dlsym(x11, "XSetInputFocus");
  int (*raise)(Display *, XID) = (int (*)(Display *, XID))dlsym(x11, "XRaiseWindow");
  int (*sync)(Display *, int) = (int (*)(Display *, int))dlsym(x11, "XSync");
  *(void **)&query_tree = dlsym(x11, "XQueryTree");
  *(void **)&fetch_name = dlsym(x11, "XFetchName");
  *(void **)&xfree = dlsym(x11, "XFree");
  Display *d = open_display ? open_display(NULL) : NULL;
  if (!d || !flush || !key || !button || !motion || !root_of || !translate || !set_focus || !raise || !sync || !query_tree ||
      !fetch_name || !xfree) {
    fprintf(stderr, "x11_poke: no display\n");
    return 1;
  }
  XID root = root_of(d), game = find(d, root);
  if (game) {
    raise(d, game);
    set_focus(d, game, 1 /* RevertToPointerRoot */, 0);
    sync(d, 0);
  }
  fprintf(stderr, "x11_poke: %s\n", game ? "the game's window has the keyboard" : "no game window: the screen");
  for (int i = 1; i < argc; i++) {
    unsigned code;
    int x, y, ms;
    if (sscanf(argv[i], "key:%u", &code) == 1) {
      key(d, code, 1, 0), flush(d);
      struct timespec t = { 0, 60000000 };
      nanosleep(&t, NULL);
      key(d, code, 0, 0);
    } else if (sscanf(argv[i], "click:%d:%d", &x, &y) == 2) {
      int sx = x, sy = y;
      XID child;
      game = find(d, root); /* again: fullscreen may have moved it */
      if (game) translate(d, game, root, x, y, &sx, &sy, &child);
      motion(d, -1, sx, sy, 0), flush(d);
      struct timespec t = { 0, 60000000 };
      nanosleep(&t, NULL);
      button(d, 1, 1, 0), flush(d);
      nanosleep(&t, NULL);
      button(d, 1, 0, 0);
    } else if (sscanf(argv[i], "wait:%d", &ms) == 1) {
      flush(d);
      struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000 };
      nanosleep(&t, NULL);
    } else {
      fprintf(stderr, "x11_poke: what is %s?\n", argv[i]);
      return 2;
    }
    flush(d);
  }
  return 0;
}
