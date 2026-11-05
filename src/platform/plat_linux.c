/* plat_linux.c - the Linux backend of plat.h: the clock, the window through
 * Wayland or X11 (plat_wayland.c, plat_x11.c) or none at all (the headless
 * tests), sound through ALSA, and gamepads through evdev with hotplug.
 * Original code for this project, written from the Linux kernel's evdev and
 * gamepad documentation (input.rst, event-codes.rst, gamepad.rst), the
 * inotify(7) and clock_nanosleep(2) manual pages, and the ALSA library's PCM
 * API documentation. */
#define _GNU_SOURCE
#include "platform/plat_linux.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv) { return plat_run(argc, argv); }

/* ---- time ------------------------------------------------------------------------------- */
uint64_t plat_counter(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
uint64_t plat_counter_hz(void) { return 1000000000ull; }
void plat_wait_until(uint64_t t) {
  struct timespec ts = { (time_t)(t / 1000000000ull), (long)(t % 1000000000ull) };
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

/* ---- the window ------------------------------------------------------------------------ */
static LinuxWindow ws; /* the window system, when there is one */
static bool have_ws, window_open, text_on;
static int off_w, off_h; /* the window without a screen */

static void pads_start(void);
static void pads_pump(void);
static void pads_stop(void);

bool plat_os_init(unsigned parts) {
  if ((parts & PLAT_VIDEO) && !plat_core_video_off() && !have_ws) {
    const char *want = getenv("A4C_VIDEO");
    bool x_only = want && !strcmp(want, "x11"), wl_only = want && !strcmp(want, "wayland");
    have_ws = (!x_only && plat_wayland_window(&ws)) || (!wl_only && plat_x11_window(&ws));
    if (!have_ws) return plat_core_fail("no display: neither Wayland nor X11 could be opened"), false;
  }
  if (parts & PLAT_PADS) pads_start();
  return true;
}

void plat_os_quit(void) {
  pads_stop();
  plat_audio_close();
}

void plat_os_pump(void) {
  if (have_ws && window_open) ws.pump();
  pads_pump();
}

bool plat_window_open(const char *title, int w, int h) {
  off_w = w, off_h = h;
  if (!have_ws) return window_open = true;
  window_open = ws.open(title, w, h);
  if (window_open) ws.text_input(text_on);
  return window_open;
}

void plat_window_close(void) {
  if (have_ws && window_open) ws.close();
  window_open = false;
}

void plat_window_size(int *w, int *h) {
  if (have_ws) ws.size(w, h);
  else *w = off_w, *h = off_h;
}
void plat_window_pixels(int *w, int *h) {
  if (have_ws) ws.pixels(w, h);
  else *w = off_w, *h = off_h;
}
void plat_fullscreen_toggle(void) {
  if (have_ws) ws.fullscreen(!ws.is_fullscreen());
}
const PlatNative *plat_native_window(void) { return have_ws && window_open ? ws.native() : NULL; }
float plat_refresh_hz(void) { return have_ws ? ws.refresh() : 60; }
const char *plat_video_driver(void) { return have_ws ? ws.name : "offscreen"; }
const char *plat_renderer_name(void) { return "software"; }

bool plat_present_open(int w, int h) {
  (void)w, (void)h;
  return true;
}
void plat_present_close(void) {}

static uint64_t pace_next;
void plat_present(const uint32_t *px, int w, int h) {
  if (!have_ws || !window_open) return; /* headless: the picture stays in the game's framebuffer */
  int bw, bh, stride;
  uint32_t *dst = ws.frame_buffer(&bw, &bh, &stride);
  if (!dst) return;
  plat_core_stretch(px, w, h, dst, bw, bh, stride);
  ws.show();
  if (ws.name[0] == 'x') { /* X11 has no frame callback: one picture per refresh */
    uint64_t step = (uint64_t)(1e9 / ws.refresh()), now = plat_counter();
    if (pace_next > now && pace_next - now <= step) plat_wait_until(pace_next);
    else pace_next = now;
    pace_next += step;
  }
}

void plat_text_input(bool on) {
  text_on = on;
  if (have_ws && window_open) ws.text_input(on);
}
bool plat_text_input_on(void) { return text_on; }
char *plat_clipboard(void) { return have_ws && window_open ? ws.clipboard() : NULL; }

/* ---- sound ------------------------------------------------------------------------------------ */
typedef struct snd_pcm snd_pcm_t;
static int (*snd_pcm_open)(snd_pcm_t **, const char *, int, int);
static int (*snd_pcm_set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
static long (*snd_pcm_writei)(snd_pcm_t *, const void *, unsigned long);
static int (*snd_pcm_recover)(snd_pcm_t *, int, int);
static int (*snd_pcm_close)(snd_pcm_t *);
static const char *(*snd_strerror)(int);
static void *alsa;

static pthread_mutex_t audio_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t audio_thread;
static atomic_bool audio_run; /* the sound thread's go, set and cleared by the game's */
static bool audio_started;
static snd_pcm_t *pcm;
static PlatAudioFn audio_fn;
static void *audio_ud;
static int audio_rate;
static long underruns;

#define CHUNK 512

/* the go flag, read under audio_mx: it is atomic already, but Valgrind's helgrind and drd do not see C11 atomics,
 * and through the mutex they can check the rest of the sound thread instead of stopping at this flag */
static bool running(void) {
  pthread_mutex_lock(&audio_mx);
  bool r = audio_run;
  pthread_mutex_unlock(&audio_mx);
  return r;
}

static snd_pcm_t *open_pcm(int *err);

static void *audio_main(void *arg) {
  (void)arg;
  float buf[CHUNK * 2];
  uint64_t next = plat_counter(), retry = 0;
  bool lost = false; /* the device went away: tried again every second */
  while (running()) {
    pthread_mutex_lock(&audio_mx);
    audio_fn(buf, CHUNK, audio_ud);
    pthread_mutex_unlock(&audio_mx);
    if (lost && plat_counter() >= retry) {
      int err;
      retry = plat_counter() + 1000000000ull;
      if ((pcm = open_pcm(&err)) != NULL) {
        lost = false;
        plat_log("sound: the device is back");
      }
    }
    if (!pcm) { /* no sound card: the samples go nowhere, at the rate they would play */
      next += (uint64_t)CHUNK * 1000000000ull / (uint64_t)audio_rate;
      plat_wait_until(next);
      continue;
    }
    for (int done = 0; done < CHUNK && running();) {
      long n = snd_pcm_writei(pcm, buf + done * 2, (unsigned long)(CHUNK - done));
      if (n < 0) {
        if (n == -EPIPE) underruns++;
        if (snd_pcm_recover(pcm, (int)n, 1) < 0) {
          /* not recoverable (-ENODEV: unplugged): closed, and on as with no card until it opens again, instead
           * of trying the dead one every 10 ms for ever */
          plat_log("sound: the device went away (%s)", snd_strerror((int)n));
          snd_pcm_close(pcm);
          pcm = NULL;
          lost = true;
          retry = plat_counter() + 1000000000ull;
          next = plat_counter();
          break;
        }
        continue;
      }
      done += (int)n;
    }
  }
  return NULL;
}

static bool load_alsa(void) {
  if (alsa) return true;
  alsa = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
  if (!alsa) return false;
#define F(n) (*(void **)&n = dlsym(alsa, #n)) != NULL
  bool ok = F(snd_pcm_open) && F(snd_pcm_set_params) && F(snd_pcm_writei) && F(snd_pcm_recover) && F(snd_pcm_close) &&
            F(snd_strerror);
#undef F
  if (!ok) dlclose(alsa), alsa = NULL;
  return ok;
}

/* the device (A4C_ALSA_DEVICE: another one than the default; the tests use "null"), set up for audio_rate */
static snd_pcm_t *open_pcm(int *err) {
  snd_pcm_t *p = NULL;
  const char *dev = getenv("A4C_ALSA_DEVICE");
  int r = snd_pcm_open(&p, dev && *dev ? dev : "default", 0 /* playback */, 0);
  /* 32-bit float, interleaved, stereo; about 40 ms of buffer */
  if (r >= 0) r = snd_pcm_set_params(p, 14 /* FLOAT_LE */, 3 /* RW_INTERLEAVED */, 2, (unsigned)audio_rate, 1, 40000);
  if (r < 0 || !p) { /* a success with no device is a failure too, and *err always says one */
    if (p) snd_pcm_close(p);
    *err = r < 0 ? r : -ENODEV;
    return NULL;
  }
  return p;
}

bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) {
  if (audio_started) plat_audio_close();
  audio_fn = fn, audio_ud = ud, audio_rate = rate;
  pcm = NULL;
  underruns = 0;
  if (!plat_core_audio_off()) {
    if (!load_alsa()) return plat_core_fail("no ALSA library"), false;
    int r = 0;
    if (!(pcm = open_pcm(&r))) return plat_core_fail("alsa: %s", snd_strerror(r)), false;
  }
  audio_run = true;
  if (pthread_create(&audio_thread, NULL, audio_main, NULL)) {
    audio_run = false;
    if (pcm) snd_pcm_close(pcm);
    pcm = NULL;
    return plat_core_fail("no sound thread"), false;
  }
  audio_started = true;
  return true;
}

void plat_audio_close(void) {
  if (!audio_started) return;
  pthread_mutex_lock(&audio_mx);
  audio_run = false;
  pthread_mutex_unlock(&audio_mx);
  pthread_join(audio_thread, NULL);
  audio_started = false;
  if (pcm) {
    snd_pcm_close(pcm);
    if (underruns) plat_log("sound: %ld underruns", underruns);
  }
  pcm = NULL;
}

void plat_audio_lock(void) { pthread_mutex_lock(&audio_mx); }
void plat_audio_unlock(void) { pthread_mutex_unlock(&audio_mx); }

/* ---- gamepads ------------------------------------------------------------------------------------ */
#define NBITS(n) PL_NBITS(n)
#define BIT(a, n) PL_BIT(a, n)
#define MAXDEV 16

typedef struct {
  int fd, id;
  char node[300];
  bool b[PPAD_BUTTONS];
  int a[PAXIS_COUNT];
  int hatx, haty;
  struct { int code, min, max; } ax[PAXIS_COUNT]; /* which evdev axis gives each of ours */
  int btn[KEY_MAX + 1 > 0x300 ? 0x300 : KEY_MAX + 1]; /* evdev button -> PPAD_ + 1 */
  bool trig_btn;  /* triggers only as buttons */
  bool dirty;
  bool dropped;   /* the kernel dropped events (SYN_DROPPED): skip to the next report, then read the state anew */
} Dev;
static Dev *devs[MAXDEV];
static int ino = -1;
static bool pads_on;
static uint64_t next_scan;

static int norm(int v, int mn, int mx, bool trigger) {
  if (mx <= mn) return 0;
  double f = (double)(v - mn) / (mx - mn);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return trigger ? (int)(f * 32767 + 0.5) : (int)(f * 65535 + 0.5) - 32768;
}

/* The kernel driver of an input node (/dev/input/eventN): the last part of /sys/class/input/eventN/device/device/
 * driver, or "" */
static void driver_of(const char *node, char *out, size_t size) {
  out[0] = 0;
  const char *base = strrchr(node, '/');
  char link[400], target[400];
  int n = snprintf(link, sizeof link, "/sys/class/input/%s/device/device/driver", base ? base + 1 : node);
  if (n <= 0 || (size_t)n >= sizeof link) return;
  ssize_t k = readlink(link, target, sizeof target - 1);
  if (k <= 0) return;
  target[k] = 0;
  const char *name = strrchr(target, '/');
  name = name ? name + 1 : target;
  size_t len = strlen(name);
  if (len < size) memcpy(out, name, len + 1); /* a name too long stays "": cut short it could match another driver */
}

static Dev *probe(const char *node) {
  int fd = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return NULL;
  unsigned long ev[NBITS(EV_MAX + 1)] = { 0 }, keys[NBITS(KEY_MAX + 1)] = { 0 }, abs[NBITS(ABS_MAX + 1)] = { 0 };
  ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
  ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
  ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
  /* a gamepad: the south face button and a stick */
  if (!BIT(ev, EV_KEY) || !BIT(ev, EV_ABS) || !BIT(keys, BTN_SOUTH) || !BIT(abs, ABS_X) || !BIT(abs, ABS_Y)) {
    close(fd);
    return NULL;
  }
  Dev *d = calloc(1, sizeof *d);
  if (!d) return close(fd), NULL;
  d->fd = fd;
  snprintf(d->node, sizeof d->node, "%s", node);
  char name[128] = "", uniq[64] = "", phys[128] = "";
  ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
  ioctl(fd, EVIOCGUNIQ(sizeof uniq - 1), uniq);
  ioctl(fd, EVIOCGPHYS(sizeof phys - 1), phys);
  struct input_id iid = { 0 };
  ioctl(fd, EVIOCGID, &iid);
  /* the face buttons by position: the xpad driver (every Xbox-style pad on USB, whoever made it) and Microsoft's
   * own pads send X and Y the other way round from the names */
  char drv[64];
  driver_of(node, drv, sizeof drv);
  bool ms = iid.vendor == 0x045e || !strcmp(drv, "xpad");
  struct { int code, b; } map[] = {
    { BTN_SOUTH, PPAD_SOUTH }, { BTN_EAST, PPAD_EAST }, { BTN_NORTH, ms ? PPAD_WEST : PPAD_NORTH },
    { BTN_WEST, ms ? PPAD_NORTH : PPAD_WEST }, { BTN_TL, PPAD_LSHOULDER }, { BTN_TR, PPAD_RSHOULDER },
    { BTN_SELECT, PPAD_BACK }, { BTN_START, PPAD_START }, { BTN_MODE, PPAD_GUIDE }, { BTN_THUMBL, PPAD_LSTICK },
    { BTN_THUMBR, PPAD_RSTICK }, { BTN_DPAD_UP, PPAD_DUP }, { BTN_DPAD_DOWN, PPAD_DDOWN },
    { BTN_DPAD_LEFT, PPAD_DLEFT }, { BTN_DPAD_RIGHT, PPAD_DRIGHT }, { KEY_BACK, PPAD_BACK }, { KEY_HOMEPAGE, PPAD_GUIDE },
  };
  for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
    if (map[i].code < (int)(sizeof d->btn / sizeof d->btn[0])) d->btn[map[i].code] = map[i].b + 1;
  /* the right stick on RX/RY with the triggers on Z/RZ (the usual), or on Z/RZ with the triggers on brake and gas
   * (a pad through the generic HID driver, as some wireless ones are) */
  int ax[PAXIS_COUNT] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ };
  if (!BIT(abs, ABS_RX)) ax[2] = ABS_Z, ax[3] = ABS_RZ, ax[4] = ABS_BRAKE, ax[5] = ABS_GAS;
  else if (!BIT(abs, ABS_Z) && BIT(abs, ABS_BRAKE)) ax[4] = ABS_BRAKE, ax[5] = ABS_GAS;
  for (int a = 0; a < PAXIS_COUNT; a++) {
    d->ax[a].code = -1;
    struct input_absinfo ai;
    if (BIT(abs, ax[a]) && ioctl(fd, EVIOCGABS(ax[a]), &ai) == 0) {
      d->ax[a].code = ax[a], d->ax[a].min = ai.minimum, d->ax[a].max = ai.maximum;
      d->a[a] = norm(ai.value, ai.minimum, ai.maximum, a >= PAXIS_LTRIGGER);
    }
  }
  d->trig_btn = d->ax[4].code < 0;
  char key[200];
  if (uniq[0]) snprintf(key, sizeof key, "serial %s", uniq); /* a wireless pad's carries its address */
  else if (phys[0]) snprintf(key, sizeof key, "path %s", phys);
  else snprintf(key, sizeof key, "kind %04x:%04x %s", iid.vendor, iid.product, name);
  d->id = plat_core_pad_add(name, key);
  if (!d->id) {
    close(fd);
    free(d);
    return NULL;
  }
  plat_core_pad_state(d->id, d->b, d->a);
  return d;
}

static void drop(int i) {
  plat_core_pad_remove(devs[i]->id);
  close(devs[i]->fd);
  free(devs[i]);
  devs[i] = NULL;
}

static void scan(void) {
  DIR *dir = opendir("/dev/input");
  if (!dir) return;
  for (struct dirent *e; (e = readdir(dir));) {
    if (strncmp(e->d_name, "event", 5)) continue;
    char node[300];
    snprintf(node, sizeof node, "/dev/input/%s", e->d_name);
    bool known = false;
    for (int i = 0; i < MAXDEV; i++) known |= devs[i] && !strcmp(devs[i]->node, node);
    if (known) continue;
    for (int i = 0; i < MAXDEV; i++)
      if (!devs[i]) {
        devs[i] = probe(node);
        break;
      }
  }
  closedir(dir);
}

static void pads_start(void) {
  if (pads_on) return;
  pads_on = true;
  ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE);
  scan();
}

static void pads_stop(void) {
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i]) drop(i);
  if (ino >= 0) close(ino);
  ino = -1;
  pads_on = false;
}

static void resync(Dev *d);

static void pads_pump(void) {
  if (!pads_on) return;
  bool rescan = false;
  if (ino >= 0) {
    char buf[4096];
    while (read(ino, buf, sizeof buf) > 0) rescan = true;
  }
  uint64_t now = plat_counter();
  if (ino < 0 && now >= next_scan) rescan = true, next_scan = now + 1000000000ull; /* no inotify: every second */
  if (rescan) {
    struct timespec nap = { 0, 2000000 }; /* the device node's permissions come a moment after it */
    nanosleep(&nap, NULL);
    scan();
  }
  for (int i = 0; i < MAXDEV; i++) {
    Dev *d = devs[i];
    if (!d) continue;
    struct input_event ev[64];
    for (;;) {
      ssize_t n = read(d->fd, ev, sizeof ev);
      if (n < 0) {
        if (errno == ENODEV || errno == EBADF) drop(i), d = NULL;
        break;
      }
      for (ssize_t k = 0; k < n / (ssize_t)sizeof ev[0]; k++) {
        const struct input_event *e = &ev[k];
        if (e->type == EV_SYN && e->code == SYN_DROPPED) {
          d->dropped = true;
          continue;
        }
        if (d->dropped) { /* up to the next report the events are a broken part: the kernel's state instead */
          if (e->type != EV_SYN || e->code != SYN_REPORT) continue;
          d->dropped = false;
          resync(d);
        }
        if (e->type == EV_KEY && e->code < (int)(sizeof d->btn / sizeof d->btn[0])) {
          if (d->btn[e->code]) d->b[d->btn[e->code] - 1] = e->value != 0, d->dirty = true;
          if (d->trig_btn && (e->code == BTN_TL2 || e->code == BTN_TR2))
            d->a[e->code == BTN_TL2 ? PAXIS_LTRIGGER : PAXIS_RTRIGGER] = e->value ? 32767 : 0, d->dirty = true;
        } else if (e->type == EV_ABS) {
          for (int a = 0; a < PAXIS_COUNT; a++)
            if (d->ax[a].code == e->code) d->a[a] = norm(e->value, d->ax[a].min, d->ax[a].max, a >= PAXIS_LTRIGGER), d->dirty = true;
          if (e->code == ABS_HAT0X) d->hatx = e->value, d->dirty = true;
          if (e->code == ABS_HAT0Y) d->haty = e->value, d->dirty = true;
        } else if (e->type == EV_SYN && e->code == SYN_REPORT && d->dirty) {
          bool b[PPAD_BUTTONS];
          memcpy(b, d->b, sizeof b);
          b[PPAD_DLEFT] |= d->hatx < 0, b[PPAD_DRIGHT] |= d->hatx > 0;
          b[PPAD_DUP] |= d->haty < 0, b[PPAD_DDOWN] |= d->haty > 0;
          plat_core_pad_state(d->id, b, d->a);
          d->dirty = false;
        }
      }
      if (n < (ssize_t)sizeof ev) break;
    }
  }
}

/* After SYN_DROPPED: what the pad holds now, asked of the kernel, since the events that said so are gone (a
 * release lost there would leave a button down until it is pressed again). */
static void resync(Dev *d) {
  unsigned long keys[NBITS(KEY_MAX + 1)] = { 0 };
  if (ioctl(d->fd, EVIOCGKEY(sizeof keys), keys) >= 0) {
    int nb = (int)(sizeof d->btn / sizeof d->btn[0]);
    for (int c = 0; c < nb; c++)
      if (d->btn[c]) d->b[d->btn[c] - 1] = false;
    for (int c = 0; c < nb; c++)
      if (d->btn[c] && BIT(keys, c)) d->b[d->btn[c] - 1] = true;
    if (d->trig_btn) {
      d->a[PAXIS_LTRIGGER] = BIT(keys, BTN_TL2) ? 32767 : 0;
      d->a[PAXIS_RTRIGGER] = BIT(keys, BTN_TR2) ? 32767 : 0;
    }
  }
  struct input_absinfo ai;
  for (int a = 0; a < PAXIS_COUNT; a++)
    if (d->ax[a].code >= 0 && ioctl(d->fd, EVIOCGABS(d->ax[a].code), &ai) == 0)
      d->a[a] = norm(ai.value, d->ax[a].min, d->ax[a].max, a >= PAXIS_LTRIGGER);
  if (ioctl(d->fd, EVIOCGABS(ABS_HAT0X), &ai) == 0) d->hatx = ai.value;
  if (ioctl(d->fd, EVIOCGABS(ABS_HAT0Y), &ai) == 0) d->haty = ai.value;
  d->dirty = true;
}

void plat_os_pad_open(int id) { (void)id; }
void plat_os_pad_close(int id) { (void)id; }
