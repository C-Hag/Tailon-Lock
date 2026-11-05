/* plat_x11.c - the window on X11 (the fallback when there is no Wayland):
 * libX11 opened at run time, the software picture through XPutImage, the
 * keyboard, the pointer, fullscreen through the window manager's
 * _NET_WM_STATE, and the clipboard. Original code for this project, written
 * from the Xlib C Language X Interface manual, the X Window System protocol,
 * the ICCCM and the Extended Window Manager Hints (EWMH) specification. */
#define _POSIX_C_SOURCE 200809L
#include "platform/plat_linux.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned long XID, Atom, Time;
typedef struct _XDisplay Display;
typedef struct _XGC *GC;
typedef struct _XVisual Visual;
typedef struct XImage {
  int width, height, xoffset, format;
  char *data;
  int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
  unsigned long red_mask, green_mask, blue_mask;
  char *obdata;
  struct {
    struct XImage *(*create_image)(void);
    int (*destroy_image)(struct XImage *);
    void *get_pixel, *put_pixel, *sub_image, *add_pixel;
  } f;
} XImage;
typedef struct { int type; unsigned long serial; int send_event; Display *display; XID window; } XAny;
typedef struct {
  int type; unsigned long serial; int send_event; Display *display; XID window, root, subwindow; Time time;
  int x, y, x_root, y_root; unsigned int state, code; int same_screen;
} XKeyOrButton;
typedef struct {
  int type; unsigned long serial; int send_event; Display *display; XID event, window; int x, y, width, height, border;
  XID above; int override_redirect;
} XConfigure;
typedef struct {
  int type; unsigned long serial; int send_event; Display *display; XID window; Atom message_type; int format;
  union { char b[20]; short s[10]; long l[5]; } data;
} XClient;
typedef struct {
  int type; unsigned long serial; int send_event; Display *display; XID requestor; Atom selection, target, property; Time time;
} XSelection;
typedef union { int type; XAny any; XKeyOrButton key; XConfigure conf; XClient client; XSelection sel; long pad[24]; } XEv;

enum { KeyPress = 2, KeyRelease, ButtonPress, ButtonRelease, MotionNotify, FocusIn = 9, FocusOut = 10,
       ConfigureNotify = 22, SelectionNotify = 31, ClientMessage = 33 };

static int (*XInitThreads)(void);
static Display *(*XOpenDisplay)(const char *);
static int (*XCloseDisplay)(Display *);
static int (*XDefaultScreen)(Display *);
static XID (*XRootWindow)(Display *, int);
static Visual *(*XDefaultVisual)(Display *, int);
static int (*XDefaultDepth)(Display *, int);
static XID (*XCreateSimpleWindow)(Display *, XID, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
static int (*XDestroyWindow)(Display *, XID);
static int (*XSelectInput)(Display *, XID, long);
static int (*XStoreName)(Display *, XID, const char *);
static int (*XMapWindow)(Display *, XID);
static Atom (*XInternAtom)(Display *, const char *, int);
static int (*XSetWMProtocols)(Display *, XID, Atom *, int);
static int (*XPending)(Display *);
static int (*XNextEvent)(Display *, XEv *);
static int (*XCheckTypedWindowEvent)(Display *, XID, int, XEv *);
static int (*XLookupString)(XKeyOrButton *, char *, int, XID *, void *);
static XID (*XLookupKeysym)(XKeyOrButton *, int);
static GC (*XCreateGC)(Display *, XID, unsigned long, void *);
static int (*XFreeGC)(Display *, GC);
static XImage *(*XCreateImage)(Display *, Visual *, unsigned, int, int, char *, unsigned, unsigned, int, int);
static int (*XPutImage)(Display *, XID, GC, XImage *, int, int, int, int, unsigned, unsigned);
static int (*XSendEvent)(Display *, XID, int, long, XEv *);
static int (*XFlush)(Display *);
static int (*XSync)(Display *, int);
static int (*XChangeProperty)(Display *, XID, Atom, Atom, int, int, const unsigned char *, int);
static int (*XConvertSelection)(Display *, Atom, Atom, Atom, XID, Time);
static int (*XGetWindowProperty)(Display *, XID, Atom, long, long, int, Atom, Atom *, int *, unsigned long *,
                                 unsigned long *, unsigned char **);
static int (*XFree)(void *);
static int (*XkbSetDetectableAutoRepeat)(Display *, int, int *);

static bool load(void) {
  static void *lib;
  if (lib) return true;
  lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return false;
#define F(n) (*(void **)&n = dlsym(lib, #n)) != NULL
  bool ok = F(XInitThreads) && F(XOpenDisplay) && F(XCloseDisplay) && F(XDefaultScreen) && F(XRootWindow) &&
            F(XDefaultVisual) && F(XDefaultDepth) && F(XCreateSimpleWindow) && F(XDestroyWindow) && F(XSelectInput) &&
            F(XStoreName) && F(XMapWindow) && F(XInternAtom) && F(XSetWMProtocols) && F(XPending) && F(XNextEvent) &&
            F(XCheckTypedWindowEvent) &&
            F(XLookupString) && F(XLookupKeysym) && F(XCreateGC) && F(XFreeGC) && F(XCreateImage) && F(XPutImage) &&
            F(XSendEvent) && F(XFlush) && F(XSync) && F(XChangeProperty) && F(XConvertSelection) &&
            F(XGetWindowProperty) && F(XFree) && F(XkbSetDetectableAutoRepeat);
#undef F
  if (!ok) dlclose(lib), lib = NULL;
  return ok;
}

static Display *dpy;
static int screen;
static XID win;
static GC gc;
static XImage *img;
static uint32_t *img_px;
static int win_w, win_h, img_w, img_h;
static bool fullscreen, text_on;
static Atom A_DELETE, A_PROTOCOLS, A_STATE, A_FULLSCREEN, A_CLIPBOARD, A_UTF8, A_TARGET, A_NAME;
static PlatNative nat;

static uint32_t keysym_char(XID ks) {
  if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff)) return (uint32_t)ks;
  if (ks >= 0x1000100 && ks <= 0x110ffff) return (uint32_t)(ks - 0x1000000);
  if (ks == 0x20ac) return 0x20ac;
  return 0;
}

static void x_key(XEv *e, bool down) {
  unsigned code = e->key.code - 8; /* X key codes are the kernel's plus 8 */
  int hid = plat_hid_of_kernel(code);
  if (!hid) return;
  bool held = plat_key_down(hid);
  PlatEvent o = { .type = down ? PE_KEY_DOWN : PE_KEY_UP };
  o.key.code = hid;
  int other = plat_key_other(hid);
  uint32_t c = keysym_char(XLookupKeysym(&e->key, 0));
  bool kp = hid >= 84 && hid <= 99;
  o.key.key = other < PKEY_OTHER || kp || !c ? other : c >= 'A' && c <= 'Z' ? (int)c + 32 : (int)c;
  o.key.mod = plat_mods();
  o.key.repeat = down && held;
  plat_core_event(&o);
  if (down && text_on) {
    char buf[8];
    XID ks;
    int n = XLookupString(&e->key, buf, sizeof buf, &ks, NULL);
    uint32_t t = keysym_char(ks);
    if (!t && n == 1) t = (unsigned char)buf[0];
    if (t >= 0x20 && t != 0x7f) {
      PlatEvent te = { .type = PE_TEXT };
      te.text.text[plat_utf8(t, te.text.text)] = 0;
      plat_core_event(&te);
    }
  }
}

static void x_pump(void) {
  while (dpy && XPending(dpy)) {
    XEv e;
    XNextEvent(dpy, &e);
    switch (e.type) {
    case KeyPress: x_key(&e, true); break;
    case KeyRelease: x_key(&e, false); break;
    case ButtonPress:
    case ButtonRelease: {
      unsigned b = e.key.code;
      if (b == 4 || b == 5) { /* the wheel */
        if (e.type == ButtonPress) {
          PlatEvent o = { .type = PE_WHEEL };
          o.wheel.x = (float)e.key.x, o.wheel.y = (float)e.key.y, o.wheel.dy = b == 4 ? 1.0f : -1.0f;
          plat_core_event(&o);
        }
        break;
      }
      PlatEvent o = { .type = e.type == ButtonPress ? PE_MOUSE_DOWN : PE_MOUSE_UP };
      o.mouse.x = (float)e.key.x, o.mouse.y = (float)e.key.y;
      o.mouse.button = b == 1 ? PBTN_LEFT : b == 3 ? PBTN_RIGHT : b == 2 ? PBTN_MIDDLE : 0;
      plat_core_event(&o);
      break;
    }
    case MotionNotify: {
      PlatEvent o = { .type = PE_MOUSE_MOVE };
      o.mouse.x = (float)e.key.x, o.mouse.y = (float)e.key.y;
      plat_core_event(&o);
      break;
    }
    case FocusIn:
    case FocusOut: {
      if (e.type == FocusOut) plat_core_keys_up();
      PlatEvent o = { .type = e.type == FocusIn ? PE_FOCUS_GAINED : PE_FOCUS_LOST };
      plat_core_event(&o);
      break;
    }
    case ConfigureNotify:
      if (e.conf.width != win_w || e.conf.height != win_h) {
        win_w = e.conf.width, win_h = e.conf.height;
        PlatEvent o = { .type = PE_RESIZED };
        plat_core_event(&o);
      }
      break;
    case ClientMessage:
      if ((Atom)e.client.data.l[0] == A_DELETE) {
        PlatEvent o = { .type = PE_QUIT };
        plat_core_event(&o);
      }
      break;
    default: break;
    }
  }
}

static bool x_open(const char *title, int w, int h) {
  XID root = XRootWindow(dpy, screen);
  win = XCreateSimpleWindow(dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, 0, 0);
  if (!win) return plat_core_fail("x11: no window"), false;
  XSelectInput(dpy, win, 1 | 2 | 4 | 8 | (1L << 6) | (1L << 17) | (1L << 21));
  XStoreName(dpy, win, title);
  XChangeProperty(dpy, win, A_NAME, A_UTF8, 8, 0 /* replace */, (const unsigned char *)title, (int)strlen(title));
  XSetWMProtocols(dpy, win, &A_DELETE, 1);
  int dummy;
  XkbSetDetectableAutoRepeat(dpy, 1, &dummy); /* a held key repeats its press without releases in between */
  XMapWindow(dpy, win);
  gc = XCreateGC(dpy, win, 0, NULL);
  XSync(dpy, 0);
  win_w = w, win_h = h;
  nat = (PlatNative){ PNAT_X11, dpy, (void *)(uintptr_t)win };
  return true;
}

static void free_image(void) {
  if (img) img->f.destroy_image(img); /* frees img_px too */
  img = NULL;
  img_px = NULL;
}

static void x_close(void) {
  if (!dpy) return;
  free_image();
  if (gc) XFreeGC(dpy, gc);
  if (win) XDestroyWindow(dpy, win);
  gc = NULL;
  win = 0;
  XSync(dpy, 0);
}

static void x_size(int *w, int *h) { *w = win_w, *h = win_h; }

static void x_fullscreen(bool on) {
  XEv e;
  memset(&e, 0, sizeof e);
  e.client.type = ClientMessage;
  e.client.window = win;
  e.client.message_type = A_STATE;
  e.client.format = 32;
  e.client.data.l[0] = on ? 1 : 0; /* _NET_WM_STATE_ADD / _REMOVE */
  e.client.data.l[1] = (long)A_FULLSCREEN;
  e.client.data.l[3] = 1;
  XSendEvent(dpy, XRootWindow(dpy, screen), 0, (1L << 19) | (1L << 20), &e);
  XFlush(dpy);
  fullscreen = on;
}
static bool x_is_fullscreen(void) { return fullscreen; }

static uint32_t *x_frame_buffer(int *w, int *h, int *stride) {
  if (!img || img_w != win_w || img_h != win_h) {
    free_image();
    img_px = malloc((size_t)win_w * (size_t)win_h * 4);
    if (!img_px) return NULL;
    img = XCreateImage(dpy, XDefaultVisual(dpy, screen), (unsigned)XDefaultDepth(dpy, screen), 2 /* ZPixmap */, 0,
                       (char *)img_px, (unsigned)win_w, (unsigned)win_h, 32, 0);
    if (!img) {
      free(img_px);
      img_px = NULL;
      return NULL;
    }
    img_w = win_w, img_h = win_h;
  }
  *w = img_w, *h = img_h, *stride = img_w;
  return img_px;
}

static void x_show(void) {
  if (!img) return;
  XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)img_w, (unsigned)img_h);
  XSync(dpy, 0); /* one picture at a time, as the display takes them */
}

static const PlatNative *x_native(void) { return &nat; }
static float x_refresh(void) { return 60; }

static char *x_clipboard(void) {
  XConvertSelection(dpy, A_CLIPBOARD, A_UTF8, A_TARGET, win, 0);
  XFlush(dpy);
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    XEv e;
    /* only the answer is taken out of the queue: the other events of the wait (a resize, a close) stay for the
     * next pump */
    if (XCheckTypedWindowEvent(dpy, win, SelectionNotify, &e)) {
      if (!e.sel.property) return NULL;
      Atom type;
      int fmt;
      unsigned long n, after;
      unsigned char *data = NULL;
      char *s = NULL;
      if (XGetWindowProperty(dpy, win, A_TARGET, 0, 65536, 1, 0 /* any type */, &type, &fmt, &n, &after, &data) == 0 && data) {
        s = malloc(n + 1);
        if (s) memcpy(s, data, n), s[n] = 0;
        XFree(data);
      }
      return s;
    }
    clock_gettime(CLOCK_MONOTONIC, &t);
    if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 > 500) return NULL;
    struct timespec nap = { 0, 5000000 };
    nanosleep(&nap, NULL);
  }
}

static void x_text_input(bool on) { text_on = on; }

static void x_shutdown(void) {
  x_close();
  if (dpy) XCloseDisplay(dpy);
  dpy = NULL;
}

bool plat_x11_window(LinuxWindow *w) {
  if (!getenv("DISPLAY") || !load()) return false;
  XInitThreads();
  dpy = XOpenDisplay(NULL);
  if (!dpy) return false;
  screen = XDefaultScreen(dpy);
  A_DELETE = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
  A_PROTOCOLS = XInternAtom(dpy, "WM_PROTOCOLS", 0);
  A_STATE = XInternAtom(dpy, "_NET_WM_STATE", 0);
  A_FULLSCREEN = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", 0);
  A_CLIPBOARD = XInternAtom(dpy, "CLIPBOARD", 0);
  A_UTF8 = XInternAtom(dpy, "UTF8_STRING", 0);
  A_TARGET = XInternAtom(dpy, "TAILONLOCK_PASTE", 0);
  A_NAME = XInternAtom(dpy, "_NET_WM_NAME", 0);
  (void)A_PROTOCOLS;
  *w = (LinuxWindow){ "x11", x_open, x_close, x_pump, x_size, x_size, x_fullscreen, x_is_fullscreen,
                      x_frame_buffer, x_show, x_native, x_refresh, x_clipboard, x_text_input };
  atexit(x_shutdown);
  return true;
}
