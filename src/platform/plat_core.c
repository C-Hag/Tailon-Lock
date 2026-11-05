/* plat_core.c - the part of the platform layer that is the same on every
 * system: the event queue, the keys held, the pad table and the made-up test
 * pads, the main loop, WAV files (RIFF/WAVE, Microsoft's multimedia format
 * documentation) resampled to the game's rate, BMP files (Microsoft's
 * BITMAPINFOHEADER and BITMAPV5HEADER documentation), and the log.
 * Original code for this project. */
#include "platform/plat_core.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- switches and errors ---------------------------------------------------------- */

static char err[256];

void plat_core_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, sizeof err, fmt, ap);
  va_end(ap);
}
const char *plat_error(void) { return err; }

static bool env_is(const char *name, const char *a, const char *b) {
  const char *v = getenv(name);
  return v && (!strcmp(v, a) || (b && !strcmp(v, b)));
}
bool plat_window_shown(void) { return !plat_core_video_off(); }

/* a wait for the display longer than 40 ms is more than two refreshes even at 50 Hz: a real display never takes
 * that every frame, a compositor without a GPU does (about 250 ms a frame in a VM) */
#define PACE_SLOW_MS  40
#define PACE_SLOW_RUN 10  /* slow waits in a row before the loop paces itself */
#define PACE_PROBE    600 /* then one frame in this many waits for the display again (10 s at 60 fps) */
#define PACE_GOOD_RUN 5   /* and this many quick probes in a row hand the pacing back (about 50 s) */
static bool self_paced;
static int pace_run, pace_frame;

bool plat_display_paces(void) { return plat_window_shown() && !self_paced; }

bool plat_core_pace_probe(void) {
  if (!self_paced) return true;
  if (++pace_frame < PACE_PROBE) return false;
  pace_frame = 0;
  return true;
}

void plat_core_pace_note(uint64_t waited) {
  uint64_t hz = plat_counter_hz();
  bool slow = hz > 0 && waited > hz / 1000 * PACE_SLOW_MS;
  if (!self_paced) {
    pace_run = slow ? pace_run + 1 : 0;
    if (pace_run >= PACE_SLOW_RUN) {
      self_paced = true;
      pace_run = pace_frame = 0;
      plat_log("video: the display took over %d ms a frame %d times in a row: the game paces its own frames",
               PACE_SLOW_MS, PACE_SLOW_RUN);
    }
  } else {
    pace_run = slow ? 0 : pace_run + 1;
    if (pace_run >= PACE_GOOD_RUN) {
      self_paced = false;
      pace_run = 0;
      plat_log("video: the display is quick again: it paces the frames");
    }
  }
}

bool plat_core_video_off(void) {
  static int off = -1;
  if (off < 0) off = env_is("A4C_VIDEO", "offscreen", "dummy");
  return off;
}
bool plat_core_audio_off(void) {
  static int off = -1;
  if (off < 0) off = env_is("A4C_AUDIO", "dummy", "none");
  return off;
}
bool plat_headless(void) { return plat_core_video_off(); }

/* ---- start-up and the log ------------------------------------------------------------ */

static uint64_t t_start;

bool plat_init(unsigned parts, const char *title, const char *version, const char *app_id) {
  (void)title, (void)version, (void)app_id;
  t_start = plat_counter();
  return plat_os_init(parts);
}

void plat_quit(void) { plat_os_quit(); }

uint64_t plat_ms(void) {
  if (!t_start) t_start = plat_counter();
  return (plat_counter() - t_start) * 1000 / plat_counter_hz();
}

void plat_logv(const char *fmt, va_list ap) {
  /* A4C_LOG=FILE: the log also goes to that file (a Windows program has no console to write to) */
  static FILE *file;
  static bool tried;
  if (!tried) {
    tried = true;
    const char *path = getenv("A4C_LOG");
    if (path && *path) file = fopen(path, "a");
  }
  if (file) {
    va_list cp;
    va_copy(cp, ap);
    fprintf(file, "%.3f ", plat_ms() / 1000.0);
    vfprintf(file, fmt, cp);
    fputc('\n', file);
    fflush(file);
    va_end(cp);
  }
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}
void plat_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  plat_logv(fmt, ap);
  va_end(ap);
}

/* ---- events and keys -------------------------------------------------------------- */

/* the system's events, then the ones the game pushed (tests), as two rings */
#define QN 512
typedef struct { PlatEvent ev[QN]; int head, n; } Queue;
static Queue sysq, pushq;

static void enqueue(Queue *q, const PlatEvent *ev) {
  if (q->n == QN) return;
  q->ev[(q->head + q->n++) % QN] = *ev;
}
static bool dequeue(Queue *q, PlatEvent *ev) {
  if (!q->n) return false;
  *ev = q->ev[q->head];
  q->head = (q->head + 1) % QN;
  q->n--;
  return true;
}

static bool down[PK_COUNT]; /* the system's keys */
static bool held[PK_COUNT]; /* keys a test holds down */

void plat_core_event(const PlatEvent *ev) {
  if ((ev->type == PE_KEY_DOWN || ev->type == PE_KEY_UP) && ev->key.code > 0 && ev->key.code < PK_COUNT)
    down[ev->key.code] = ev->type == PE_KEY_DOWN;
  enqueue(&sysq, ev);
}

void plat_core_keys_up(void) {
  for (int k = 1; k < PK_COUNT; k++)
    if (down[k]) {
      PlatEvent e = { .type = PE_KEY_UP };
      e.key.code = k;
      e.key.key = PKEY_OTHER + k;
      plat_core_event(&e);
    }
}

void plat_push(const PlatEvent *ev) { enqueue(&pushq, ev); }

bool plat_poll(PlatEvent *ev) {
  if (sysq.n == 0) plat_os_pump();
  return dequeue(&sysq, ev) || dequeue(&pushq, ev);
}

void plat_key_hold(int code, bool on) {
  if (code > 0 && code < PK_COUNT) held[code] = on;
}
bool plat_key_down(int code) { return code > 0 && code < PK_COUNT && (down[code] || held[code]); }
int plat_mods(void) {
  return (down[PK_LSHIFT] || down[PK_RSHIFT] ? PMOD_SHIFT : 0) | (down[PK_LCTRL] || down[PK_RCTRL] ? PMOD_CTRL : 0) |
         (down[PK_LALT] || down[PK_RALT] ? PMOD_ALT : 0);
}

/* ---- the main loop ---------------------------------------------------------------- */

int plat_run(int argc, char **argv) {
  const PlatApp *app = plat_app();
  int code = 0;
  if (!app->init(argc, argv, &code)) return code;
  for (;;) {
    PlatEvent ev;
    while (plat_poll(&ev)) app->event(&ev);
    if (!app->frame()) break;
  }
  return app->quit();
}

/* ---- pads ----------------------------------------------------------------------------- */

#define MAXPADS 16
typedef struct {
  int id, opened;
  bool live, virt;
  char name[96], key[160];
  bool b[PPAD_BUTTONS];
  int a[PAXIS_COUNT];
} Pad;
static Pad pads[MAXPADS];
static int next_id = 1;

static Pad *pad(int id) {
  for (int i = 0; i < MAXPADS; i++)
    if (pads[i].live && pads[i].id == id) return &pads[i];
  return NULL;
}

int plat_core_pad_add(const char *name, const char *key) {
  for (int i = 0; i < MAXPADS; i++)
    if (!pads[i].live) {
      Pad *p = &pads[i];
      memset(p, 0, sizeof *p);
      p->live = true;
      p->id = next_id++; /* never reused, so a pad that comes back is a new one */
      snprintf(p->name, sizeof p->name, "%s", name && *name ? name : "?");
      snprintf(p->key, sizeof p->key, "%s", key ? key : "");
      PlatEvent e = { .type = PE_PAD_ADDED };
      e.pad.id = p->id;
      plat_core_event(&e);
      return p->id;
    }
  return 0;
}

void plat_core_pad_state(int id, const bool buttons[PPAD_BUTTONS], const int axes[PAXIS_COUNT]) {
  Pad *p = pad(id);
  if (!p) return;
  for (int a = 0; a < PAXIS_COUNT; a++) {
    int v = axes[a] < -32768 ? -32768 : axes[a] > 32767 ? 32767 : axes[a];
    if (a >= PAXIS_LTRIGGER && v < 0) v = 0;
    p->a[a] = v;
  }
  for (int b = 0; b < PPAD_BUTTONS; b++)
    if (p->b[b] != buttons[b]) {
      p->b[b] = buttons[b];
      if (!p->opened) continue; /* a closed pad's buttons reach nobody, as before */
      PlatEvent e = { .type = buttons[b] ? PE_PAD_DOWN : PE_PAD_UP };
      e.pad.id = id;
      e.pad.button = b;
      plat_core_event(&e);
    }
}

void plat_core_pad_remove(int id) {
  Pad *p = pad(id);
  if (!p) return;
  p->live = false;
  PlatEvent e = { .type = PE_PAD_REMOVED };
  e.pad.id = id;
  plat_core_event(&e);
}

int plat_pads(int *ids, int max) {
  int k = 0;
  for (int i = 0; i < MAXPADS && k < max; i++)
    if (pads[i].live) ids[k++] = pads[i].id;
  /* in the order they came */
  for (int i = 1; i < k; i++)
    for (int j = i; j > 0 && ids[j - 1] > ids[j]; j--) {
      int t = ids[j];
      ids[j] = ids[j - 1];
      ids[j - 1] = t;
    }
  return k;
}

bool plat_pad_open(int id) {
  Pad *p = pad(id);
  if (!p) return false;
  if (!p->opened++ && !p->virt) plat_os_pad_open(id);
  return true;
}

void plat_pad_close(int id) {
  Pad *p = pad(id);
  if (!p || !p->opened) return;
  p->opened = 0;
  if (!p->virt) plat_os_pad_close(id);
}

const char *plat_pad_name(int id) {
  Pad *p = pad(id);
  return p && p->opened ? p->name : "?";
}
int plat_pad_axis(int id, int axis) {
  Pad *p = pad(id);
  return p && p->opened && axis >= 0 && axis < PAXIS_COUNT ? p->a[axis] : 0;
}
bool plat_pad_button(int id, int button) {
  Pad *p = pad(id);
  return p && p->opened && button >= 0 && button < PPAD_BUTTONS && p->b[button];
}
const char *plat_pad_key(int id) {
  static char key[192];
  Pad *p = pad(id);
  snprintf(key, sizeof key, "%s", p && p->opened ? p->key : "");
  return key;
}

/* the made-up pads (tests): a pad of our own table, without a system device behind it */
static int vh[8]; /* handle - 1 -> pad id */

int plat_vpad_attach(const char *name, unsigned vendor, unsigned product) {
  for (int i = 0; i < 8; i++)
    if (!vh[i]) {
      char key[160];
      /* a made-up pad has no serial number and no device path: its kind and name, as a plain pad's would be */
      snprintf(key, sizeof key, "kind %04x:%04x %s", vendor & 0xffff, product & 0xffff, name ? name : "?");
      int id = plat_core_pad_add(name, key);
      Pad *q = id ? pad(id) : NULL;
      if (!q) return 0;
      q->virt = true;
      vh[i] = id;
      return i + 1;
    }
  return 0;
}

void plat_vpad_set(int h, const bool buttons[PPAD_BUTTONS], const int axes[PAXIS_COUNT]) {
  if (h >= 1 && h <= 8 && vh[h - 1]) plat_core_pad_state(vh[h - 1], buttons, axes);
}

void plat_vpad_detach(int h) {
  if (h < 1 || h > 8 || !vh[h - 1]) return;
  plat_core_pad_remove(vh[h - 1]);
  vh[h - 1] = 0;
}

/* ---- the software picture ------------------------------------------------------------ */

void plat_core_stretch(const uint32_t *px, int w, int h, uint32_t *dst, int dw, int dh, int dstride) {
  if (dw <= 0 || dh <= 0 || w <= 0 || h <= 0) return;
  int *xs = malloc(sizeof(int) * (size_t)dw);
  if (!xs) return;
  for (int x = 0; x < dw; x++) xs[x] = (int)(((int64_t)x * 2 + 1) * w / (2 * (int64_t)dw)); /* the pixel centre's source */
  for (int y = 0; y < dh; y++) {
    const uint32_t *s = px + (size_t)((((int64_t)y * 2 + 1) * h) / (2 * (int64_t)dh)) * (size_t)w;
    uint32_t *d = dst + (size_t)y * (size_t)dstride;
    for (int x = 0; x < dw; x++) d[x] = s[xs[x]] | 0xff000000u;
  }
  free(xs);
}

/* ---- BMP files -------------------------------------------------------------------------- */

static void le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v & 0xffff), le16(p + 2, v >> 16); }

/* 24 bits per pixel, rows bottom first, BITMAPINFOHEADER */
bool plat_save_bmp(const char *path, const uint32_t *px, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3, size = stride * (size_t)h;
  uint8_t hd[54] = { 'B', 'M' };
  le32(hd + 2, (uint32_t)(54 + size));
  le32(hd + 10, 54);
  le32(hd + 14, 40);
  le32(hd + 18, (uint32_t)w);
  le32(hd + 22, (uint32_t)h);
  le16(hd + 26, 1);
  le16(hd + 28, 24);
  le32(hd + 34, (uint32_t)size);
  bool ok = fwrite(hd, 1, sizeof hd, f) == sizeof hd;
  uint8_t *row = calloc(1, stride);
  for (int y = h - 1; ok && row && y >= 0; y--) {
    const uint32_t *s = px + (size_t)y * (size_t)w;
    for (int x = 0; x < w; x++) row[x * 3] = (uint8_t)s[x], row[x * 3 + 1] = (uint8_t)(s[x] >> 8), row[x * 3 + 2] = (uint8_t)(s[x] >> 16);
    ok = fwrite(row, 1, stride, f) == stride;
  }
  free(row);
  return fclose(f) == 0 && ok && row;
}

/* 32 bits per pixel with alpha (0xAARRGGBB), rows bottom first, BITMAPV5HEADER with the channel masks: the GPU
 * renderer's screenshots */
bool plat_save_bmp32(const char *path, const uint32_t *px, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t size = (size_t)w * 4 * (size_t)h;
  uint8_t hd[14 + 124] = { 'B', 'M' };
  le32(hd + 2, (uint32_t)(sizeof hd + size));
  le32(hd + 10, sizeof hd);
  uint8_t *v = hd + 14;
  le32(v, 124);
  le32(v + 4, (uint32_t)w);
  le32(v + 8, (uint32_t)h);
  le16(v + 12, 1);
  le16(v + 14, 32);
  le32(v + 16, 3); /* BI_BITFIELDS */
  le32(v + 20, (uint32_t)size);
  le32(v + 40, 0x00ff0000), le32(v + 44, 0x0000ff00), le32(v + 48, 0x000000ff), le32(v + 52, 0xff000000);
  le32(v + 56, 0x73524742); /* 'sRGB' */
  bool ok = fwrite(hd, 1, sizeof hd, f) == sizeof hd;
  for (int y = h - 1; ok && y >= 0; y--) {
    uint8_t row[4096 * 4];
    const uint32_t *s = px + (size_t)y * (size_t)w;
    for (int x0 = 0; ok && x0 < w; x0 += 4096) {
      int n = w - x0 < 4096 ? w - x0 : 4096;
      for (int x = 0; x < n; x++) le32(row + x * 4, s[x0 + x]);
      ok = fwrite(row, 4, (size_t)n, f) == (size_t)n;
    }
  }
  return fclose(f) == 0 && ok;
}

/* ---- WAV files ------------------------------------------------------------------------- */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }

/* the windowed-sinc kernel, PH phases of TAPS taps (Blackman window), cut off at fc of the input rate */
#define TAPS 32
#define PH 256
static float *kernel(double fc) {
  float *k = malloc(sizeof(float) * (PH + 1) * TAPS);
  if (!k) return NULL;
  for (int p = 0; p <= PH; p++) {
    double fr = (double)p / PH, sum = 0;
    for (int t = 0; t < TAPS; t++) {
      double x = t - (TAPS / 2 - 1) - fr; /* taps at -15..16 around the point */
      double s = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
      double wpos = (x + TAPS / 2.0) / TAPS;
      double win = wpos <= 0 || wpos >= 1 ? 0 : 0.42 - 0.5 * cos(2 * M_PI * wpos) + 0.08 * cos(4 * M_PI * wpos);
      k[p * TAPS + t] = (float)(s * win);
      sum += s * win;
    }
    for (int t = 0; t < TAPS; t++) k[p * TAPS + t] = (float)(k[p * TAPS + t] / sum); /* unity gain at every phase */
  }
  return k;
}

float *plat_load_wav(const char *path, int rate, size_t *frames) {
  size_t n = 0;
  uint8_t *b = plat_load_file(path, &n);
  if (!b) return NULL;
  float *out = NULL;
  if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) goto done;
  unsigned fmt = 0, ch = 0, bits = 0, srate = 0;
  const uint8_t *data = NULL;
  size_t dlen = 0;
  for (size_t at = 12; at + 8 <= n;) {
    uint32_t len = rd32(b + at + 4);
    const uint8_t *c = b + at + 8;
    size_t avail = n - at - 8 < len ? n - at - 8 : len;
    if (!memcmp(b + at, "fmt ", 4) && avail >= 16) {
      fmt = rd16(c), ch = rd16(c + 2), srate = rd32(c + 4), bits = rd16(c + 14);
      if (fmt == 0xFFFE && avail >= 26) fmt = rd16(c + 24); /* WAVE_FORMAT_EXTENSIBLE: the sub-format's first word */
    } else if (!memcmp(b + at, "data", 4)) {
      data = c, dlen = avail;
    }
    at += 8 + (size_t)len + (len & 1);
  }
  unsigned bps = bits / 8;
  if (!data || !ch || !srate || bits % 8 || !((fmt == 1 && bits >= 8 && bits <= 32) || (fmt == 3 && bits == 32))) goto done;
  size_t nin = dlen / (bps * ch);
  float *in = malloc(sizeof(float) * 2 * (nin ? nin : 1));
  if (!in) goto done;
  for (size_t i = 0; i < nin; i++)
    for (unsigned c = 0; c < 2; c++) {
      const uint8_t *s = data + (i * ch + (c < ch ? c : ch - 1)) * bps;
      float v;
      if (fmt == 3) {
        uint32_t u = rd32(s);
        memcpy(&v, &u, 4);
      } else if (bits == 8) {
        v = (s[0] - 128) / 128.0f;
      } else if (bits == 16) {
        v = (int16_t)rd16(s) / 32768.0f;
      } else if (bits == 24) {
        v = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) / 8388608.0f;
      } else {
        v = (float)((int32_t)rd32(s) / 2147483648.0);
      }
      in[i * 2 + c] = v;
    }
  if ((int)srate == rate) {
    out = in;
    *frames = nin;
    goto done;
  }
  /* to the game's rate: each output frame from the TAPS input frames around its time */
  double step = (double)srate / rate;
  size_t nout = (size_t)((double)nin * rate / srate);
  float *k = kernel(0.5 * (step > 1 ? 1 / step : 1) * 0.97);
  out = k ? malloc(sizeof(float) * 2 * (nout ? nout : 1)) : NULL;
  for (size_t o = 0; out && o < nout; o++) {
    double t = o * step;
    long i0 = (long)floor(t);
    double fr = (t - i0) * PH;
    int p = (int)fr;
    float w1 = (float)(fr - p), w0 = 1 - w1;
    const float *k0 = k + p * TAPS, *k1 = k0 + TAPS;
    float l = 0, r = 0;
    for (int tp = 0; tp < TAPS; tp++) {
      long j = i0 + tp - (TAPS / 2 - 1);
      if (j < 0 || (size_t)j >= nin) continue;
      float kw = k0[tp] * w0 + k1[tp] * w1;
      l += in[j * 2] * kw;
      r += in[j * 2 + 1] * kw;
    }
    out[o * 2] = l;
    out[o * 2 + 1] = r;
  }
  if (out) *frames = nout;
  free(k);
  free(in);
done:
  free(b);
  return out;
}
