/* plat_wayland.c - the window on Wayland: libwayland-client opened at run
 * time, with our own glue for the xdg-shell and xdg-decoration protocols, a
 * title bar of our own when the compositor draws none, the software picture
 * in shared-memory buffers, the keyboard read through its XKB keymap, the
 * pointer, the clipboard and the outputs' scale and refresh rate.
 * Original code for this project, written from the Wayland protocol
 * specification (wayland.xml), the xdg-shell and xdg-decoration-unstable-v1
 * protocol specifications, and libwayland-client's documented C API. */
#define _GNU_SOURCE /* memfd_create */
#include "platform/plat_linux.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* ---- libwayland-client's types and calls --------------------------------------------------- */
struct wl_message { const char *name, *signature; const struct wl_interface **types; };
struct wl_interface {
  const char *name;
  int version, method_count;
  const struct wl_message *methods;
  int event_count;
  const struct wl_message *events;
};
struct wl_array { size_t size, alloc; void *data; };
typedef struct wl_proxy P; /* every object is a proxy */
typedef int32_t wl_fixed;

static struct wl_display *(*wl_display_connect)(const char *);
static void (*wl_display_disconnect)(struct wl_display *);
static int (*wl_display_roundtrip)(struct wl_display *);
static int (*wl_display_dispatch_pending)(struct wl_display *);
static int (*wl_display_flush)(struct wl_display *);
static int (*wl_display_prepare_read)(struct wl_display *);
static int (*wl_display_read_events)(struct wl_display *);
static void (*wl_display_cancel_read)(struct wl_display *);
static int (*wl_display_get_fd)(struct wl_display *);
static int (*wl_display_get_error)(struct wl_display *);
static P *(*wl_proxy_marshal_flags)(P *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
static int (*wl_proxy_add_listener)(P *, void (**)(void), void *);
static void (*wl_proxy_destroy)(P *);
static uint32_t (*wl_proxy_get_version)(P *);
static const struct wl_interface *I_registry, *I_compositor, *I_subcompositor, *I_subsurface, *I_surface, *I_shm,
    *I_shm_pool, *I_buffer, *I_seat, *I_pointer, *I_keyboard, *I_output, *I_callback, *I_ddm, *I_data_device,
    *I_data_offer;
#define DESTROY 1 /* WL_MARSHAL_FLAG_DESTROY */
#define M(obj, op, ...) wl_proxy_marshal_flags((P *)(obj), op, NULL, wl_proxy_get_version((P *)(obj)), 0, ##__VA_ARGS__)
#define NEW(obj, op, iface, ...) wl_proxy_marshal_flags((P *)(obj), op, iface, wl_proxy_get_version((P *)(obj)), 0, ##__VA_ARGS__)
#define KILL(obj, op) wl_proxy_marshal_flags((P *)(obj), op, NULL, wl_proxy_get_version((P *)(obj)), DESTROY)
#define LISTEN(obj, fns, data) wl_proxy_add_listener((P *)(obj), (void (**)(void))(fns), data)

/* ---- xdg-shell and xdg-decoration, from their protocol files --------------------------------- */
static const struct wl_interface xdg_wm_base_i, xdg_positioner_i, xdg_surface_i, xdg_toplevel_i, xdg_popup_i,
    deco_mgr_i, deco_i;
static const struct wl_interface *t_wm[1], *t_get_xs[2], *t_xs_get_toplevel[1], *t_xs_popup[3], *t_tl_parent[1], *t_tl_seat[4],
    *t_tl_output[1], *t_deco[2], *t_none[8];
static const struct wl_message wm_req[] = {
  { "destroy", "", t_none }, { "create_positioner", "n", t_wm }, { "get_xdg_surface", "no", t_get_xs }, { "pong", "u", t_none } };
static const struct wl_message wm_ev[] = { { "ping", "u", t_none } };
static const struct wl_message pos_req[] = { { "destroy", "", t_none } };
static const struct wl_message xs_req[] = { { "destroy", "", t_none }, { "get_toplevel", "n", t_xs_get_toplevel },
                                            { "get_popup", "n?oo", t_xs_popup }, { "set_window_geometry", "iiii", t_none },
                                            { "ack_configure", "u", t_none } };
static const struct wl_message xs_ev[] = { { "configure", "u", t_none } };
static const struct wl_message tl_req[] = {
  { "destroy", "", t_none }, { "set_parent", "?o", t_tl_parent }, { "set_title", "s", t_none },
  { "set_app_id", "s", t_none }, { "show_window_menu", "ouii", t_tl_seat }, { "move", "ou", t_tl_seat },
  { "resize", "ouu", t_tl_seat }, { "set_max_size", "ii", t_none }, { "set_min_size", "ii", t_none },
  { "set_maximized", "", t_none }, { "unset_maximized", "", t_none }, { "set_fullscreen", "?o", t_tl_output },
  { "unset_fullscreen", "", t_none }, { "set_minimized", "", t_none } };
static const struct wl_message tl_ev[] = { { "configure", "iia", t_none }, { "close", "", t_none },
                                           { "configure_bounds", "4ii", t_none }, { "wm_capabilities", "5a", t_none } };
static const struct wl_message popup_req[] = { { "destroy", "", t_none } };
static const struct wl_message dm_req[] = { { "destroy", "", t_none }, { "get_toplevel_decoration", "no", t_deco } };
static const struct wl_message dc_req[] = { { "destroy", "", t_none }, { "set_mode", "u", t_none }, { "unset_mode", "", t_none } };
static const struct wl_message dc_ev[] = { { "configure", "u", t_none } };
static const struct wl_interface xdg_wm_base_i = { "xdg_wm_base", 5, 4, wm_req, 1, wm_ev };
static const struct wl_interface xdg_positioner_i = { "xdg_positioner", 5, 1, pos_req, 0, NULL };
static const struct wl_interface xdg_surface_i = { "xdg_surface", 5, 5, xs_req, 1, xs_ev };
static const struct wl_interface xdg_toplevel_i = { "xdg_toplevel", 5, 14, tl_req, 4, tl_ev };
static const struct wl_interface xdg_popup_i = { "xdg_popup", 5, 1, popup_req, 0, NULL };
static const struct wl_interface deco_mgr_i = { "zxdg_decoration_manager_v1", 1, 2, dm_req, 0, NULL };
static const struct wl_interface deco_i = { "zxdg_toplevel_decoration_v1", 1, 3, dc_req, 1, dc_ev };

static bool load(void) {
  static void *lib;
  if (lib) return true;
  lib = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return false;
#define F(n) (*(void **)&n = dlsym(lib, #n)) != NULL
#define D(v, n) (v = dlsym(lib, n)) != NULL
  bool ok = F(wl_display_connect) && F(wl_display_disconnect) && F(wl_display_roundtrip) &&
            F(wl_display_dispatch_pending) && F(wl_display_flush) && F(wl_display_prepare_read) &&
            F(wl_display_read_events) && F(wl_display_cancel_read) && F(wl_display_get_fd) &&
            F(wl_display_get_error) && F(wl_proxy_marshal_flags) && F(wl_proxy_add_listener) && F(wl_proxy_destroy) &&
            F(wl_proxy_get_version) && D(I_registry, "wl_registry_interface") &&
            D(I_compositor, "wl_compositor_interface") && D(I_subcompositor, "wl_subcompositor_interface") &&
            D(I_subsurface, "wl_subsurface_interface") && D(I_surface, "wl_surface_interface") &&
            D(I_shm, "wl_shm_interface") && D(I_shm_pool, "wl_shm_pool_interface") &&
            D(I_buffer, "wl_buffer_interface") && D(I_seat, "wl_seat_interface") &&
            D(I_pointer, "wl_pointer_interface") && D(I_keyboard, "wl_keyboard_interface") &&
            D(I_output, "wl_output_interface") && D(I_callback, "wl_callback_interface") &&
            D(I_ddm, "wl_data_device_manager_interface") && D(I_data_device, "wl_data_device_interface") &&
            D(I_data_offer, "wl_data_offer_interface");
#undef F
#undef D
  if (!ok) {
    dlclose(lib);
    lib = NULL;
    return false;
  }
  /* the object types the requests name; libwayland's own interfaces are known only now */
  t_wm[0] = &xdg_positioner_i;
  t_get_xs[0] = &xdg_surface_i, t_get_xs[1] = I_surface;
  t_xs_get_toplevel[0] = &xdg_toplevel_i;
  t_xs_popup[0] = &xdg_popup_i, t_xs_popup[1] = &xdg_surface_i, t_xs_popup[2] = &xdg_positioner_i;
  t_tl_parent[0] = &xdg_toplevel_i;
  t_tl_seat[0] = I_seat;
  t_tl_output[0] = I_output;
  t_deco[0] = &deco_i, t_deco[1] = &xdg_toplevel_i;
  return true;
}

/* ---- the state ----------------------------------------------------------------------------- */
#define BAR 30 /* our own title bar's height, in window units */
#define MAXOUT 8

static struct wl_display *dpy;
static P *registry, *compositor, *subcompositor, *shm, *seat, *wm, *deco_mgr, *ddm;
static P *surface, *xsurface, *toplevel, *deco, *pointer, *keyboard, *data_device, *selection_offer;
static P *bar_surface, *bar_sub, *cursor_surface;
static struct { P *o; uint32_t name; int scale; float hz; bool on; } outs[MAXOUT];
static int content_w, content_h, scale = 1, want_w, want_h;
static bool configured, fullscreen, maximized, own_bar, closing, focused, text_on;
static uint32_t pointer_serial, ptr_enter_serial;
static P *pointer_on; /* the surface the pointer is over */
static float px, py;
static int wheel_discrete;
static float wheel_value;
static bool wheel_any;
static PlatNative nat;
static int64_t repeat_rate = 25, repeat_delay = 400; /* per second, ms */
static struct { unsigned code; int64_t next; } rep;
static int level_mods; /* bit 1 shift, bit 2 AltGr */
static uint64_t last_click_ms;

static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- shared-memory buffers ------------------------------------------------------------------------ */
typedef struct { P *buf; uint32_t *px; size_t bytes; int w, h; bool busy; } Shm;
static Shm pic[2], bar_buf, cur_buf;

static void buffer_release(void *d, P *b) {
  (void)b;
  ((Shm *)d)->busy = false;
}
static void (*const buffer_ls[])(void *, P *) = { buffer_release };

static void shm_free(Shm *s) {
  if (s->buf) KILL(s->buf, 0);
  if (s->px) munmap(s->px, s->bytes);
  memset(s, 0, sizeof *s);
}

static bool shm_make(Shm *s, int w, int h) {
  shm_free(s);
  size_t bytes = (size_t)w * (size_t)h * 4;
  int fd = memfd_create("tailonlock", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, (off_t)bytes) < 0) {
    if (fd >= 0) close(fd);
    return false;
  }
  s->px = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (s->px == MAP_FAILED) {
    s->px = NULL;
    close(fd);
    return false;
  }
  P *pool = NEW(shm, 0, I_shm_pool, NULL, fd, (int32_t)bytes);
  s->buf = NEW(pool, 0, I_buffer, NULL, 0, w, h, w * 4, 1 /* XRGB8888 */);
  KILL(pool, 1);
  close(fd);
  s->bytes = bytes, s->w = w, s->h = h, s->busy = false;
  LISTEN(s->buf, buffer_ls, s);
  return true;
}

/* ---- our own title bar and pointer --------------------------------------------------------------- */
enum { HIT_NONE, HIT_BAR, HIT_CLOSE, HIT_MAX, HIT_MIN };

static int bar_hit(float x) {
  int bw = BAR + 16;
  if (x >= content_w - bw) return HIT_CLOSE;
  if (x >= content_w - 2 * bw) return HIT_MAX;
  if (x >= content_w - 3 * bw) return HIT_MIN;
  return HIT_BAR;
}

static void fill(Shm *s, int x0, int y0, int x1, int y1, uint32_t c) {
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < s->h; y++)
    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < s->w; x++) s->px[y * s->w + x] = c;
}

static void draw_bar(void) {
  if (!own_bar || fullscreen || !content_w) {
    if (bar_surface) {
      M(bar_surface, 1, NULL, 0, 0); /* attach nothing: hidden */
      M(bar_surface, 6);
    }
    return;
  }
  int w = content_w * scale, h = BAR * scale, s = scale;
  if ((bar_buf.w != w || bar_buf.h != h) && !shm_make(&bar_buf, w, h)) return;
  fill(&bar_buf, 0, 0, w, h, focused ? 0xff26282b : 0xff3a3c40);
  fill(&bar_buf, 0, h - s, w, h, 0xff101010);
  int bw = (BAR + 16) * s, c = h / 2, r = 5 * s;
  for (int i = -r; i <= r; i++) /* close: a cross */
    for (int t = -s / 2 - 1; t <= s / 2; t++) {
      int x = w - bw / 2;
      fill(&bar_buf, x + i, c + i + t, x + i + 1, c + i + t + 1, 0xffe0e0e0);
      fill(&bar_buf, x + i, c - i + t, x + i + 1, c - i + t + 1, 0xffe0e0e0);
    }
  int mx = w - bw - bw / 2; /* maximise: a square */
  fill(&bar_buf, mx - r, c - r, mx + r, c - r + s, 0xffe0e0e0);
  fill(&bar_buf, mx - r, c + r - s, mx + r, c + r, 0xffe0e0e0);
  fill(&bar_buf, mx - r, c - r, mx - r + s, c + r, 0xffe0e0e0);
  fill(&bar_buf, mx + r - s, c - r, mx + r, c + r, 0xffe0e0e0);
  int nx = w - 2 * bw - bw / 2; /* minimise: a line */
  fill(&bar_buf, nx - r, c + r - s, nx + r, c + r, 0xffe0e0e0);
  /* the game's mark at the left: three chevrons */
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < 6 * s; i++) {
      int x0 = 10 * s + k * 7 * s;
      fill(&bar_buf, x0 + i / 2, c - 6 * s + i, x0 + i / 2 + 2 * s, c - 6 * s + i + 1, 0xffc8a040);
      fill(&bar_buf, x0 + i / 2, c + 6 * s - i, x0 + i / 2 + 2 * s, c + 6 * s - i + 1, 0xffc8a040);
    }
  M(bar_surface, 8, scale); /* set_buffer_scale */
  M(bar_surface, 1, bar_buf.buf, 0, 0);
  M(bar_surface, 2, 0, 0, content_w, BAR);
  M(bar_surface, 6);
}

/* the arrow pointer: we draw it, as the compositor has none for a client without a cursor theme */
static void set_cursor(uint32_t serial) {
  if (!pointer) return;
  int s = scale, n = 20 * s;
  if (cur_buf.w != n && !shm_make(&cur_buf, n, n)) return;
  memset(cur_buf.px, 0, cur_buf.bytes);
  for (int y = 0; y < 17 * s; y++)
    for (int x = 0; x <= y * 11 / 17 && x < n; x++) {
      int edge = x == 0 || x >= y * 11 / 17 - s || y >= 17 * s - s;
      cur_buf.px[y * n + x] = edge ? 0xff000000 : 0xffffffff;
    }
  if (!cursor_surface) cursor_surface = NEW(compositor, 0, I_surface, NULL);
  M(cursor_surface, 8, scale);
  M(cursor_surface, 1, cur_buf.buf, 0, 0);
  M(cursor_surface, 2, 0, 0, 20, 20);
  M(cursor_surface, 6);
  M(pointer, 0, serial, cursor_surface, 0, 0);
}

/* ---- events ---------------------------------------------------------------------------------- */
static void send_resized(void) {
  PlatEvent e = { .type = PE_RESIZED };
  plat_core_event(&e);
}

static int preferred_scale; /* what the compositor says for our surface (wl_surface 6), 0 until it does */

static void apply_scale(void) {
  /* the compositor's word for the surface; else the outputs it is on; else (not shown yet) the biggest output's */
  int s = 1, any = 0;
  for (int i = 0; i < MAXOUT; i++) any |= outs[i].o && outs[i].on;
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o && (outs[i].on || !any) && outs[i].scale > s) s = outs[i].scale;
  if (preferred_scale > 0) s = preferred_scale;
  if (s != scale) {
    scale = s;
    if (surface) M(surface, 8, scale);
    draw_bar();
    send_resized();
  }
}

static void surface_enter(void *d, P *s, P *o) {
  (void)d, (void)s;
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o == o) outs[i].on = true;
  apply_scale();
}
static void surface_leave(void *d, P *s, P *o) {
  (void)d, (void)s;
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o == o) outs[i].on = false;
  apply_scale();
}
static void surface_scale(void *d, P *s, int32_t f) {
  (void)d, (void)s;
  preferred_scale = f;
  apply_scale();
}
static void surface_transform(void *d, P *s, uint32_t t) { (void)d, (void)s, (void)t; }
static void (*const surface_ls[])(void) = { (void (*)(void))surface_enter, (void (*)(void))surface_leave,
                                            (void (*)(void))surface_scale, (void (*)(void))surface_transform };

static void out_geometry(void *d, P *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub, const char *mk, const char *md, int32_t tr) {
  (void)d, (void)o, (void)x, (void)y, (void)pw, (void)ph, (void)sub, (void)mk, (void)md, (void)tr;
}
static void out_mode(void *d, P *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) {
  (void)w, (void)h, (void)o;
  if (flags & 1) outs[(intptr_t)d].hz = refresh / 1000.0f; /* the current mode */
}
static void out_done(void *d, P *o) { (void)d, (void)o; }
static void out_scale(void *d, P *o, int32_t f) {
  (void)o;
  outs[(intptr_t)d].scale = f;
  apply_scale();
}
static void (*const output_ls[])(void) = { (void (*)(void))out_geometry, (void (*)(void))out_mode, (void (*)(void))out_done,
                                           (void (*)(void))out_scale };

static void wm_ping(void *d, P *w, uint32_t serial) {
  (void)d;
  M(w, 3, serial);
}
static void (*const wm_ls[])(void) = { (void (*)(void))wm_ping };

static void set_geometry(void) {
  if (!xsurface || !content_w) return;
  if (own_bar && !fullscreen) M(xsurface, 3, 0, -BAR, content_w, content_h + BAR);
  else M(xsurface, 3, 0, 0, content_w, content_h);
}

static void xs_configure(void *d, P *x, uint32_t serial) {
  (void)d;
  M(x, 4, serial); /* ack */
  int w = want_w > 0 ? want_w : content_w, h = want_h > 0 ? want_h : content_h;
  if (own_bar && !fullscreen && want_h > 0) h -= BAR; /* the compositor's size holds our bar too */
  if (w < 64) w = 64;
  if (h < 48) h = 48;
  bool changed = w != content_w || h != content_h;
  content_w = w, content_h = h;
  set_geometry();
  if (bar_sub) M(bar_sub, 1, 0, -BAR); /* set_position */
  draw_bar();
  if (changed && configured) send_resized();
  configured = true;
}
static void (*const xs_ls[])(void) = { (void (*)(void))xs_configure };

static void tl_configure(void *d, P *t, int32_t w, int32_t h, struct wl_array *states) {
  (void)d, (void)t;
  bool fs = false, mx = false;
  for (uint32_t *s = states->data; (char *)s < (char *)states->data + states->size; s++) {
    if (*s == 2) fs = true;
    if (*s == 1) mx = true;
  }
  if (fs != fullscreen) {
    fullscreen = fs;
    if (!fs) want_w = want_h = 0;
  }
  maximized = mx;
  if (w > 0 && h > 0) want_w = w, want_h = h;
  else want_w = want_h = 0; /* we choose */
}
static void tl_close(void *d, P *t) {
  (void)d, (void)t;
  closing = true;
  PlatEvent e = { .type = PE_QUIT };
  plat_core_event(&e);
}
static void tl_bounds(void *d, P *t, int32_t w, int32_t h) { (void)d, (void)t, (void)w, (void)h; }
static void tl_caps(void *d, P *t, struct wl_array *a) { (void)d, (void)t, (void)a; }
static void (*const tl_ls[])(void) = { (void (*)(void))tl_configure, (void (*)(void))tl_close, (void (*)(void))tl_bounds,
                                       (void (*)(void))tl_caps };

static void deco_configure(void *d, P *x, uint32_t mode) {
  (void)d, (void)x;
  bool own = mode != 2; /* 2: the compositor draws them */
  if (own != own_bar) {
    own_bar = own;
    set_geometry();
    draw_bar();
  }
}
static void (*const deco_ls[])(void) = { (void (*)(void))deco_configure };

/* pointer */
static void ptr_enter(void *d, P *p, uint32_t serial, P *s, wl_fixed x, wl_fixed y) {
  (void)d, (void)p;
  pointer_on = s;
  ptr_enter_serial = serial;
  px = x / 256.0f, py = y / 256.0f;
  set_cursor(serial);
}
static void ptr_leave(void *d, P *p, uint32_t serial, P *s) {
  (void)d, (void)p, (void)serial, (void)s;
  pointer_on = NULL;
}
static void ptr_motion(void *d, P *p, uint32_t t, wl_fixed x, wl_fixed y) {
  (void)d, (void)p, (void)t;
  px = x / 256.0f, py = y / 256.0f;
  if (pointer_on != surface) return;
  PlatEvent e = { .type = PE_MOUSE_MOVE };
  e.mouse.x = px, e.mouse.y = py;
  plat_core_event(&e);
}
static void ptr_button(void *d, P *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state) {
  (void)d, (void)p, (void)t;
  pointer_serial = serial;
  if (pointer_on == bar_surface && bar_surface) {
    if (button != 0x110 || !state) return; /* BTN_LEFT, pressed */
    int hit = bar_hit(px);
    uint64_t now = (uint64_t)now_ms();
    if (hit == HIT_CLOSE) tl_close(NULL, NULL);
    else if (hit == HIT_MAX || (hit == HIT_BAR && now - last_click_ms < 400)) M(toplevel, maximized ? 10 : 9);
    else if (hit == HIT_MIN) M(toplevel, 13);
    else M(toplevel, 5, seat, serial); /* move */
    last_click_ms = now;
    return;
  }
  if (pointer_on != surface) return;
  int b = button == 0x110 ? PBTN_LEFT : button == 0x111 ? PBTN_RIGHT : button == 0x112 ? PBTN_MIDDLE : 0;
  PlatEvent e = { .type = state ? PE_MOUSE_DOWN : PE_MOUSE_UP };
  e.mouse.x = px, e.mouse.y = py, e.mouse.button = b;
  plat_core_event(&e);
}
static void ptr_axis(void *d, P *p, uint32_t t, uint32_t axis, wl_fixed v) {
  (void)d, (void)p, (void)t;
  if (axis == 0) wheel_value += v / 256.0f, wheel_any = true;
}
static void ptr_frame(void *d, P *p) {
  (void)d, (void)p;
  if (wheel_any && pointer_on == surface) {
    PlatEvent e = { .type = PE_WHEEL };
    e.wheel.x = px, e.wheel.y = py;
    e.wheel.dy = wheel_discrete ? (float)-wheel_discrete : -wheel_value / 10.0f; /* up is positive */
    if (e.wheel.dy != 0) plat_core_event(&e);
  }
  wheel_any = false, wheel_value = 0, wheel_discrete = 0;
}
static void ptr_source(void *d, P *p, uint32_t s) { (void)d, (void)p, (void)s; }
static void ptr_stop(void *d, P *p, uint32_t t, uint32_t a) { (void)d, (void)p, (void)t, (void)a; }
static void ptr_discrete(void *d, P *p, uint32_t a, int32_t n) {
  (void)d, (void)p;
  if (a == 0) wheel_discrete += n, wheel_any = true;
}
static void (*const ptr_ls[])(void) = { (void (*)(void))ptr_enter, (void (*)(void))ptr_leave, (void (*)(void))ptr_motion,
                                        (void (*)(void))ptr_button, (void (*)(void))ptr_axis, (void (*)(void))ptr_frame,
                                        (void (*)(void))ptr_source, (void (*)(void))ptr_stop, (void (*)(void))ptr_discrete };

/* keyboard */
static void key_event(unsigned code, bool down, bool repeat) {
  int hid = plat_hid_of_kernel(code);
  if (!hid) return;
  PlatEvent e = { .type = down ? PE_KEY_DOWN : PE_KEY_UP };
  e.key.code = hid;
  uint32_t c = plat_xkb_char(code, 0);
  int other = plat_key_other(hid);
  bool kp = hid >= 84 && hid <= 99;
  if (other < PKEY_OTHER || kp || !c) e.key.key = other;
  else e.key.key = c >= 'A' && c <= 'Z' ? (int)c + 32 : (int)c;
  e.key.mod = plat_mods();
  e.key.repeat = repeat;
  plat_core_event(&e);
  if (down && text_on) {
    int level = (level_mods & 1 ? 1 : 0) + (level_mods & 2 ? 2 : 0);
    uint32_t t = plat_xkb_char(code, level);
    if (kp) {
      static const char KP[] = "/*-+\r1234567890.";
      t = (uint32_t)KP[hid - 84];
    }
    if (t >= 0x20 && t != 0x7f) {
      PlatEvent te = { .type = PE_TEXT };
      te.text.text[plat_utf8(t, te.text.text)] = 0;
      plat_core_event(&te);
    }
  }
}

static void kb_keymap(void *d, P *k, uint32_t format, int32_t fd, uint32_t size) {
  (void)d, (void)k;
  if (format == 1 && size) {
    char *m = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m != MAP_FAILED) {
      plat_xkb_load(m, size);
      munmap(m, size);
    }
  }
  close(fd);
}
static void kb_enter(void *d, P *k, uint32_t serial, P *s, struct wl_array *keys) {
  (void)d, (void)k, (void)serial, (void)s, (void)keys;
  focused = true;
  draw_bar();
  PlatEvent e = { .type = PE_FOCUS_GAINED };
  plat_core_event(&e);
}
static void kb_leave(void *d, P *k, uint32_t serial, P *s) {
  (void)d, (void)k, (void)serial, (void)s;
  focused = false;
  rep.code = 0;
  level_mods = 0;
  plat_core_keys_up();
  draw_bar();
  PlatEvent e = { .type = PE_FOCUS_LOST };
  plat_core_event(&e);
}
static void kb_key(void *d, P *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state) {
  (void)d, (void)k, (void)serial, (void)t;
  int hid = plat_hid_of_kernel(key);
  if (hid == PK_LSHIFT || hid == PK_RSHIFT) level_mods = state ? level_mods | 1 : level_mods & ~1;
  if (hid == PK_RALT) level_mods = state ? level_mods | 2 : level_mods & ~2;
  bool held = hid > 0 && hid < PK_COUNT && plat_key_down(hid);
  key_event(key, state != 0, state && held);
  bool mod = hid >= 224;
  if (state && !mod && repeat_rate > 0) rep.code = key, rep.next = now_ms() + repeat_delay;
  else if (!state && rep.code == key) rep.code = 0;
}
static void kb_mods(void *d, P *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
  (void)d, (void)k, (void)serial, (void)dep, (void)lat, (void)lock, (void)group;
}
static void kb_repeat(void *d, P *k, int32_t rate, int32_t delay) {
  (void)d, (void)k;
  repeat_rate = rate, repeat_delay = delay;
}
static void (*const kb_ls[])(void) = { (void (*)(void))kb_keymap, (void (*)(void))kb_enter, (void (*)(void))kb_leave,
                                       (void (*)(void))kb_key, (void (*)(void))kb_mods, (void (*)(void))kb_repeat };

static void seat_caps(void *d, P *s, uint32_t caps) {
  (void)d;
  if ((caps & 1) && !pointer) {
    pointer = NEW(s, 0, I_pointer, NULL);
    LISTEN(pointer, ptr_ls, NULL);
  }
  if ((caps & 2) && !keyboard) {
    keyboard = NEW(s, 1, I_keyboard, NULL);
    LISTEN(keyboard, kb_ls, NULL);
  }
}
static void seat_name(void *d, P *s, const char *n) { (void)d, (void)s, (void)n; }
static void (*const seat_ls[])(void) = { (void (*)(void))seat_caps, (void (*)(void))seat_name };

/* the clipboard: the offer the compositor gives for the selection */
static void offer_offer(void *d, P *o, const char *mime) { (void)d, (void)o, (void)mime; }
static void offer_actions(void *d, P *o, uint32_t a) { (void)d, (void)o, (void)a; }
static void (*const offer_ls[])(void) = { (void (*)(void))offer_offer, (void (*)(void))offer_actions, (void (*)(void))offer_actions };
static void dd_offer(void *d, P *dd, P *o) {
  (void)d, (void)dd;
  LISTEN(o, offer_ls, NULL);
}
static void dd_enter(void *d, P *dd, uint32_t serial, P *s, wl_fixed x, wl_fixed y, P *o) {
  (void)d, (void)dd, (void)serial, (void)s, (void)x, (void)y;
  if (o && o != selection_offer) KILL(o, 2);
}
static void dd_noop(void *d, P *dd) { (void)d, (void)dd; }
static void dd_motion(void *d, P *dd, uint32_t t, wl_fixed x, wl_fixed y) { (void)d, (void)dd, (void)t, (void)x, (void)y; }
static void dd_selection(void *d, P *dd, P *o) {
  (void)d, (void)dd;
  if (selection_offer && selection_offer != o) KILL(selection_offer, 2);
  selection_offer = o;
}
static void (*const dd_ls[])(void) = { (void (*)(void))dd_offer, (void (*)(void))dd_enter, (void (*)(void))dd_noop,
                                       (void (*)(void))dd_motion, (void (*)(void))dd_noop, (void (*)(void))dd_selection };

static void reg_global(void *d, P *r, uint32_t name, const char *iface, uint32_t v) {
  (void)d;
#define BIND(i, ver) wl_proxy_marshal_flags(r, 0, i, (ver), 0, name, (i)->name, (ver), NULL)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
  if (!strcmp(iface, "wl_compositor")) compositor = BIND(I_compositor, MIN(v, 6u));
  else if (!strcmp(iface, "wl_subcompositor")) subcompositor = BIND(I_subcompositor, 1u);
  else if (!strcmp(iface, "wl_shm")) shm = BIND(I_shm, 1u);
  else if (!strcmp(iface, "wl_seat") && !seat) {
    seat = BIND(I_seat, MIN(v, 7u));
    LISTEN(seat, seat_ls, NULL);
  } else if (!strcmp(iface, "xdg_wm_base")) {
    wm = BIND(&xdg_wm_base_i, MIN(v, 5u));
    LISTEN(wm, wm_ls, NULL);
  } else if (!strcmp(iface, "zxdg_decoration_manager_v1")) deco_mgr = BIND(&deco_mgr_i, 1u);
  else if (!strcmp(iface, "wl_data_device_manager")) ddm = BIND(I_ddm, MIN(v, 3u));
  else if (!strcmp(iface, "wl_output")) {
    for (intptr_t i = 0; i < MAXOUT; i++)
      if (!outs[i].o) {
        outs[i].o = BIND(I_output, MIN(v, 2u));
        outs[i].name = name, outs[i].scale = 1, outs[i].hz = 60;
        LISTEN(outs[i].o, output_ls, (void *)i);
        break;
      }
  }
#undef BIND
#undef MIN
}
static void reg_remove(void *d, P *r, uint32_t name) {
  (void)d, (void)r;
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o && outs[i].name == name) {
      wl_proxy_destroy(outs[i].o);
      outs[i].o = NULL;
      apply_scale();
    }
}
static void (*const reg_ls[])(void) = { (void (*)(void))reg_global, (void (*)(void))reg_remove };

/* ---- the window ------------------------------------------------------------------------------------ */
static void pump_wait(int timeout_ms) {
  if (!dpy) return;
  /* after a protocol error dispatch_pending fails without emptying the queue, so prepare_read would never
   * succeed: stop there, and close as below */
  while (wl_display_prepare_read(dpy) != 0)
    if (wl_display_dispatch_pending(dpy) < 0) {
      if (!closing) tl_close(NULL, NULL);
      return;
    }
  wl_display_flush(dpy);
  struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };
  if (poll(&p, 1, timeout_ms) > 0 && (p.revents & POLLIN)) wl_display_read_events(dpy);
  else wl_display_cancel_read(dpy);
  wl_display_dispatch_pending(dpy);
  if (wl_display_get_error(dpy) && !closing) tl_close(NULL, NULL);
}

static void wl_pump(void) {
  pump_wait(0);
  if (rep.code && repeat_rate > 0) {
    int64_t t = now_ms();
    for (int n = 0; t >= rep.next && n < 8; n++) {
      key_event(rep.code, true, true);
      rep.next += 1000 / repeat_rate;
    }
  }
}

static bool wl_open(const char *title, int w, int h) {
  content_w = w, content_h = h;
  preferred_scale = 0;
  apply_scale();
  surface = NEW(compositor, 0, I_surface, NULL);
  LISTEN(surface, surface_ls, NULL);
  xsurface = NEW(wm, 2, &xdg_surface_i, NULL, surface);
  LISTEN(xsurface, xs_ls, NULL);
  toplevel = NEW(xsurface, 1, &xdg_toplevel_i, NULL);
  LISTEN(toplevel, tl_ls, NULL);
  M(toplevel, 2, title);
  M(toplevel, 3, "tailonlock");
  M(toplevel, 8, 320, 200); /* min size */
  own_bar = !deco_mgr || !subcompositor;
  if (deco_mgr) {
    deco = NEW(deco_mgr, 1, &deco_i, NULL, toplevel);
    LISTEN(deco, deco_ls, NULL);
    M(deco, 1, 2); /* ask for the compositor's */
    own_bar = true; /* until it says it draws them */
  }
  if (subcompositor) {
    bar_surface = NEW(compositor, 0, I_surface, NULL);
    bar_sub = NEW(subcompositor, 1, I_subsurface, NULL, bar_surface, surface);
    M(bar_sub, 1, 0, -BAR);
    M(bar_sub, 5); /* desync */
  } else {
    own_bar = false;
  }
  if (ddm && seat) {
    data_device = NEW(ddm, 1, I_data_device, NULL, seat);
    LISTEN(data_device, dd_ls, NULL);
  }
  set_geometry();
  M(surface, 6);
  for (int i = 0; i < 100 && !configured && !closing; i++) wl_display_roundtrip(dpy);
  if (!configured) return plat_core_fail("wayland: the window was never configured"), false;
  wl_display_roundtrip(dpy); /* the decoration mode and the outputs */
  nat = (PlatNative){ PNAT_WAYLAND, dpy, surface };
  return true;
}

static void wl_close_window(void) {
  shm_free(&pic[0]);
  shm_free(&pic[1]);
  shm_free(&bar_buf);
  shm_free(&cur_buf);
  if (selection_offer) KILL(selection_offer, 2);
  selection_offer = NULL;
  if (cursor_surface) KILL(cursor_surface, 0);
  if (bar_sub) KILL(bar_sub, 0);
  if (bar_surface) KILL(bar_surface, 0);
  if (deco) KILL(deco, 0);
  if (toplevel) KILL(toplevel, 0);
  if (xsurface) KILL(xsurface, 0);
  if (surface) KILL(surface, 0);
  cursor_surface = bar_sub = bar_surface = deco = toplevel = xsurface = surface = NULL;
  if (dpy) wl_display_roundtrip(dpy);
}

static void wl_shutdown(void) {
  if (!dpy) return;
  wl_close_window();
  if (data_device) wl_proxy_destroy(data_device);
  if (pointer) wl_proxy_destroy(pointer);
  if (keyboard) wl_proxy_destroy(keyboard);
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o) wl_proxy_destroy(outs[i].o), outs[i].o = NULL;
  P *all[] = { seat, ddm, deco_mgr, wm, shm, subcompositor, compositor, registry };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (all[i]) wl_proxy_destroy(all[i]);
  data_device = pointer = keyboard = seat = ddm = deco_mgr = wm = shm = subcompositor = compositor = registry = NULL;
  wl_display_disconnect(dpy);
  dpy = NULL;
}

static void wl_size(int *w, int *h) { *w = content_w, *h = content_h; }
static void wl_pixels(int *w, int *h) { *w = content_w * scale, *h = content_h * scale; }

static void wl_fullscreen(bool on) {
  if (on) M(toplevel, 11, NULL);
  else M(toplevel, 12);
  wl_display_flush(dpy);
}
static bool wl_is_fullscreen(void) { return fullscreen; }

static int cur_pic;
static P *frame_cb;
static bool frame_done = true;
static int64_t frame_sent;
static void cb_done(void *d, P *cb, uint32_t t) {
  (void)d, (void)t;
  wl_proxy_destroy(cb);
  frame_cb = NULL;
  frame_done = true;
}
static void (*const cb_ls[])(void) = { (void (*)(void))cb_done };

static uint32_t *wl_frame_buffer(int *w, int *h, int *stride) {
  /* paced to the display: wait for the last picture's frame callback, but never long (a hidden window gets none) */
  while (!frame_done && now_ms() - frame_sent < 40) pump_wait(5);
  int pw = content_w * scale, ph = content_h * scale;
  Shm *s = &pic[cur_pic];
  if (s->busy) s = &pic[cur_pic ^= 1];
  for (int i = 0; i < 20 && s->busy; i++) pump_wait(2);
  if (s->w != pw || s->h != ph) shm_make(s, pw, ph);
  if (!s->px) return NULL;
  *w = s->w, *h = s->h, *stride = s->w;
  return s->px;
}

static void wl_show(void) {
  Shm *s = &pic[cur_pic];
  if (!s->buf) return;
  s->busy = true;
  M(surface, 8, scale);
  M(surface, 1, s->buf, 0, 0);
  M(surface, 9, 0, 0, s->w, s->h); /* damage_buffer */
  if (frame_cb) wl_proxy_destroy(frame_cb);
  frame_cb = NEW(surface, 3, I_callback, NULL);
  LISTEN(frame_cb, cb_ls, NULL);
  frame_done = false;
  frame_sent = now_ms();
  M(surface, 6);
  wl_display_flush(dpy);
  cur_pic ^= 1;
}

static const PlatNative *wl_native(void) { return &nat; }

static float wl_refresh(void) {
  float hz = 0;
  for (int i = 0; i < MAXOUT; i++)
    if (outs[i].o && outs[i].on && outs[i].hz > hz) hz = outs[i].hz;
  for (int i = 0; i < MAXOUT && hz <= 0; i++)
    if (outs[i].o) hz = outs[i].hz;
  return hz > 20 ? hz : 60;
}

static char *wl_clipboard(void) {
  if (!selection_offer) return NULL;
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) < 0) return NULL;
  M(selection_offer, 1, "text/plain;charset=utf-8", fds[1]);
  close(fds[1]);
  wl_display_flush(dpy);
  size_t n = 0, cap = 256;
  char *s = malloc(cap);
  for (int64_t until = now_ms() + 500; s && now_ms() < until;) {
    struct pollfd p = { fds[0], POLLIN, 0 };
    if (poll(&p, 1, 20) <= 0) {
      wl_display_flush(dpy);
      continue;
    }
    if (n + 128 > cap) {
      char *ns = realloc(s, cap *= 2);
      if (!ns) break;
      s = ns;
    }
    ssize_t got = read(fds[0], s + n, cap - n - 1);
    if (got <= 0) break;
    n += (size_t)got;
    if (n > 65536) break;
  }
  close(fds[0]);
  if (s) s[n] = 0;
  return s;
}

static void wl_text_input(bool on) { text_on = on; }

bool plat_wayland_window(LinuxWindow *w) {
  if (!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")) return false;
  if (!load()) return false;
  dpy = wl_display_connect(NULL);
  if (!dpy) return false;
  registry = wl_proxy_marshal_flags((P *)dpy, 1, I_registry, 1, 0, NULL);
  LISTEN(registry, reg_ls, NULL);
  wl_display_roundtrip(dpy);
  wl_display_roundtrip(dpy); /* the seat's capabilities, the outputs */
  if (!compositor || !shm || !wm) {
    wl_shutdown();
    return false;
  }
  *w = (LinuxWindow){ "wayland", wl_open, wl_close_window, wl_pump, wl_size, wl_pixels, wl_fullscreen, wl_is_fullscreen,
                      wl_frame_buffer, wl_show, wl_native, wl_refresh, wl_clipboard, wl_text_input };
  atexit(wl_shutdown);
  return true;
}
