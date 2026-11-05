/* test_plat_core.c - brick P1 of the platform ladder: plat_core.c and
 * plat_keys.c on their own, with a stand-in backend: the event queue's order
 * and its limits, keys held, the pad table and its events, the made-up pads,
 * the picture stretch, the BMP writers read back, the WAV reader on every
 * format it takes and on broken files, the resampler's length and level,
 * the XKB keymap reader, and fuzzing of the WAV and XKB readers (run it under
 * AddressSanitizer and UBSan). Original code for this project.
 *
 * usage: test_plat_core [SECONDS]   (the fuzzing time, default 5) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "platform/plat_core.h"
#include "platform/plat_linux.h"
#include "core/safe.h"

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_plat_core:%d: %s\n", __LINE__, #c);        \
      fails++;                                                         \
    }                                                                  \
  } while (0)

/* ---- the stand-in backend ---- */
static int pumps, opened, closed;
bool plat_os_init(unsigned parts) { return (void)parts, true; }
void plat_os_quit(void) {}
void plat_os_pump(void) { pumps++; }
void plat_os_pad_open(int id) { (void)id, opened++; }
void plat_os_pad_close(int id) { (void)id, closed++; }
uint64_t plat_counter(void) { return (uint64_t)clock(); }
uint64_t plat_counter_hz(void) { return CLOCKS_PER_SEC; }
const PlatApp *plat_app(void) { return NULL; }

static PlatEvent next(void) {
  PlatEvent e = { .type = PE_NONE };
  if (!plat_poll(&e)) e.type = PE_NONE;
  return e;
}

static void test_queue(void) {
  PlatEvent a = { .type = PE_MOUSE_MOVE }, b = { .type = PE_QUIT };
  a.mouse.x = 3;
  plat_push(&b);         /* the game's own */
  plat_core_event(&a);   /* the system's: comes first */
  PlatEvent e = next();
  CHECK(e.type == PE_MOUSE_MOVE && e.mouse.x == 3);
  CHECK(next().type == PE_QUIT);
  CHECK(next().type == PE_NONE);
  for (int i = 0; i < 2000; i++) plat_core_event(&a); /* past the ring's size: the rest are dropped, no harm */
  int n = 0;
  while (next().type != PE_NONE) n++;
  CHECK(n == 512);
}

static void test_keys(void) {
  PlatEvent k = { .type = PE_KEY_DOWN };
  k.key.code = PK_LSHIFT;
  plat_core_event(&k);
  k.key.code = PK_A;
  plat_core_event(&k);
  CHECK(plat_key_down(PK_A) && plat_key_down(PK_LSHIFT));
  CHECK(plat_mods() == PMOD_SHIFT);
  plat_key_hold(PK_B, true);
  CHECK(plat_key_down(PK_B));
  plat_key_hold(PK_B, false);
  CHECK(!plat_key_down(PK_B));
  CHECK(!plat_key_down(-1) && !plat_key_down(PK_COUNT) && !plat_key_down(100000));
  while (next().type != PE_NONE) {}
  plat_core_keys_up();
  int ups = 0;
  for (PlatEvent e; (e = next()).type != PE_NONE;) ups += e.type == PE_KEY_UP;
  CHECK(ups == 2 && !plat_key_down(PK_A) && plat_mods() == 0);
  k.key.code = 99999; /* out of range: queued, but no state */
  plat_core_event(&k);
  CHECK(next().type == PE_KEY_DOWN);
}

static void test_pads(void) {
  int id = plat_core_pad_add("Pad One", "serial 00:11");
  CHECK(id > 0);
  PlatEvent e = next();
  CHECK(e.type == PE_PAD_ADDED && e.pad.id == id);
  bool b[PPAD_BUTTONS] = { 0 };
  int a[PAXIS_COUNT] = { 0 };
  b[PPAD_SOUTH] = true;
  plat_core_pad_state(id, b, a); /* not opened: no button events */
  CHECK(next().type == PE_NONE);
  CHECK(!strcmp(plat_pad_name(id), "?")); /* a closed pad tells nothing */
  CHECK(plat_pad_open(id) && opened == 1);
  CHECK(!strcmp(plat_pad_name(id), "Pad One") && !strcmp(plat_pad_key(id), "serial 00:11"));
  CHECK(plat_pad_button(id, PPAD_SOUTH)); /* the state from before it was opened */
  b[PPAD_SOUTH] = false;
  b[PPAD_START] = true;
  a[PAXIS_LEFTX] = 999999;
  a[PAXIS_LTRIGGER] = -500;
  plat_core_pad_state(id, b, a);
  int ups = 0, downs = 0;
  for (; (e = next()).type != PE_NONE;) {
    ups += e.type == PE_PAD_UP && e.pad.button == PPAD_SOUTH;
    downs += e.type == PE_PAD_DOWN && e.pad.button == PPAD_START;
  }
  CHECK(ups == 1 && downs == 1);
  CHECK(plat_pad_axis(id, PAXIS_LEFTX) == 32767 && plat_pad_axis(id, PAXIS_LTRIGGER) == 0);
  CHECK(plat_pad_axis(id, -1) == 0 && plat_pad_axis(id, PAXIS_COUNT) == 0 && !plat_pad_button(id, PPAD_BUTTONS));
  int id2 = plat_core_pad_add("Pad Two", "path x");
  int ids[4], n = plat_pads(ids, 4);
  CHECK(n == 2 && ids[0] == id && ids[1] == id2);
  plat_core_pad_remove(id);
  while ((e = next()).type != PE_NONE) CHECK(e.type != PE_PAD_REMOVED || e.pad.id == id);
  CHECK(!plat_pad_open(id) && plat_pads(ids, 4) == 1);
  int id3 = plat_core_pad_add("Pad One", "serial 00:11"); /* back: a new id */
  CHECK(id3 != id && id3 > id2);
  plat_pad_close(id2);
  plat_core_pad_remove(id2);
  plat_core_pad_remove(id3);
  for (int i = 0; i < 40; i++) plat_core_pad_add("many", "k"); /* a full table refuses quietly */
  n = plat_pads(ids, 4);
  CHECK(n == 4);
  int all[64];
  n = plat_pads(all, 64);
  for (int i = 0; i < n; i++) plat_core_pad_remove(all[i]);
  while (next().type != PE_NONE) {}
  /* the made-up pads */
  int h = plat_vpad_attach("Xbox Wireless Controller", 0x045e, 0x0b13);
  CHECK(h > 0);
  CHECK((e = next()).type == PE_PAD_ADDED);
  int vid = e.pad.id;
  CHECK(plat_pad_open(vid) && opened == 1); /* no system pad opened for it */
  CHECK(strstr(plat_pad_key(vid), "045e:0b13") != NULL);
  memset(b, 0, sizeof b);
  b[PPAD_EAST] = true;
  plat_vpad_set(h, b, a);
  CHECK((e = next()).type == PE_PAD_DOWN && e.pad.button == PPAD_EAST);
  plat_vpad_set(99, b, a);
  plat_vpad_detach(h);
  CHECK((e = next()).type == PE_PAD_REMOVED && e.pad.id == vid);
  plat_vpad_detach(h);
  CHECK(next().type == PE_NONE);
}

static void test_stretch(void) {
  uint32_t src[4] = { 1, 2, 3, 4 }, dst[16];
  plat_core_stretch(src, 2, 2, dst, 4, 4, 4);
  CHECK((dst[0] & 0xffffff) == 1 && (dst[3] & 0xffffff) == 2 && (dst[12] & 0xffffff) == 3 && (dst[15] & 0xffffff) == 4);
  CHECK(dst[5] >> 24 == 0xff);
  plat_core_stretch(src, 2, 2, dst, 0, 0, 0);
  uint32_t one;
  plat_core_stretch(src, 2, 2, &one, 1, 1, 1);
}

static uint32_t le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_bmp(void) {
  uint32_t px[6] = { 0x112233, 0x445566, 0x778899, 0xaabbcc, 0xddeeff, 0x010203 };
  CHECK(plat_save_bmp("test_plat_core.bmp", px, 3, 2));
  size_t n;
  uint8_t *b = plat_load_file("test_plat_core.bmp", &n);
  CHECK(b && n == 54 + 12 * 2 && b[0] == 'B' && le(b + 18) == 3 && le(b + 22) == 2 && (b[28] | b[29] << 8) == 24);
  if (b) CHECK(b[54] == 0xcc && b[55] == 0xbb && b[56] == 0xaa); /* bottom row first: pixel (0, 1) */
  free(b);
  px[0] = 0x80112233;
  CHECK(plat_save_bmp32("test_plat_core.bmp", px, 3, 2));
  b = plat_load_file("test_plat_core.bmp", &n);
  CHECK(b && n == 138 + 24 && le(b + 10) == 138 && le(b + 30) == 3);
  if (b) CHECK(le(b + 138 + 12) == 0x80112233);
  free(b);
  remove("test_plat_core.bmp");
  CHECK(!plat_save_bmp("/nonexistent-dir/x.bmp", px, 3, 2));
}

/* ---- WAV ---- */
static size_t wav(uint8_t *o, unsigned fmt, unsigned ch, unsigned rate, unsigned bits, const void *data, size_t n, bool ext) {
  size_t fl = ext ? 40 : 16, at = 0;
  memcpy(o, "RIFF", 4), at = 8;
  memcpy(o + at, "WAVEfmt ", 8), at += 8;
  o[at] = (uint8_t)fl, o[at + 1] = o[at + 2] = o[at + 3] = 0, at += 4;
  uint8_t *f = o + at;
  memset(f, 0, fl);
  unsigned tag = ext ? 0xFFFE : fmt;
  f[0] = (uint8_t)tag, f[1] = (uint8_t)(tag >> 8), f[2] = (uint8_t)ch;
  f[4] = (uint8_t)rate, f[5] = (uint8_t)(rate >> 8), f[6] = (uint8_t)(rate >> 16);
  f[12] = (uint8_t)(ch * bits / 8), f[14] = (uint8_t)bits;
  if (ext) f[16] = 22, f[24] = (uint8_t)fmt;
  at += fl;
  memcpy(o + at, "LIST", 4), o[at + 4] = 2, o[at + 5] = o[at + 6] = o[at + 7] = 0, o[at + 8] = 'x', o[at + 9] = 'y',
      at += 10; /* a chunk to skip */
  memcpy(o + at, "data", 4);
  o[at + 4] = (uint8_t)n, o[at + 5] = (uint8_t)(n >> 8), o[at + 6] = (uint8_t)(n >> 16), o[at + 7] = 0;
  at += 8;
  memcpy(o + at, data, n);
  at += n;
  uint32_t riff = (uint32_t)at - 8;
  o[4] = (uint8_t)riff, o[5] = (uint8_t)(riff >> 8), o[6] = (uint8_t)(riff >> 16), o[7] = 0;
  return at;
}

static float *load_bytes(const uint8_t *b, size_t n, int rate, size_t *frames) {
  FILE *f = fopen("test_plat_core.wav", "wb");
  if (!f) return NULL;
  fwrite(b, 1, n, f);
  fclose(f);
  return plat_load_wav("test_plat_core.wav", rate, frames);
}

static uint8_t buf[1 << 20];

/* a file written whole or not at all: it replaces the old one; a save that cannot start leaves the old one */
static void test_save(void) {
  char tmp[64];
  FILE *f = fopen("test_plat_core.ini", "wb"); /* binary: the bytes compared are the bytes written, no CRLF */
  if (f) fputs("old\n", f), fclose(f);
  f = plat_save_begin("test_plat_core.ini", "wb", tmp, sizeof tmp);
  CHECK(f != NULL);
  if (f) fputs("new\n", f);
  CHECK(plat_save_end(f, tmp, "test_plat_core.ini"));
  size_t n;
  char *b = plat_load_file("test_plat_core.ini", &n);
  CHECK(b && n == 4 && !strcmp(b, "new\n"));
  free(b);
  f = fopen(tmp, "r");
  CHECK(!f); /* the .tmp is gone */
  if (f) fclose(f);
  CHECK(plat_save_begin("no-such-folder/x.ini", "w", tmp, sizeof tmp) == NULL);
  CHECK(!plat_save_end(NULL, tmp, "test_plat_core.ini"));
  char tiny[8];
  CHECK(plat_save_begin("test_plat_core.ini", "w", tiny, sizeof tiny) == NULL); /* no room for the .tmp name */
  b = plat_load_file("test_plat_core.ini", &n);
  CHECK(b && !strcmp(b, "new\n")); /* still the last good save */
  free(b);
#ifndef _WIN32
  /* the .tmp cannot be put in place (here it is taken away before the end): the old file stays, whole */
  f = plat_save_begin("test_plat_core.ini", "w", tmp, sizeof tmp);
  CHECK(f != NULL);
  if (f) fputs("newer\n", f);
  remove(tmp);
  CHECK(!plat_save_end(f, tmp, "test_plat_core.ini"));
  b = plat_load_file("test_plat_core.ini", &n);
  CHECK(b && !strcmp(b, "new\n"));
  free(b);
#endif
  remove("test_plat_core.ini");
  /* path is a folder with a file in it: the save fails, the folder and its file stay, no .tmp is left */
  CHECK(plat_make_dir("test_plat_core.d/sub"));
  FILE *in = fopen("test_plat_core.d/keep", "wb");
  if (in) fputs("kept\n", in), fclose(in);
  f = plat_save_begin("test_plat_core.d", "w", tmp, sizeof tmp);
  CHECK(f != NULL);
  if (f) fputs("x\n", f);
  CHECK(!plat_save_end(f, tmp, "test_plat_core.d"));
  b = plat_load_file("test_plat_core.d/keep", &n);
  CHECK(b && !strcmp(b, "kept\n"));
  free(b);
  CHECK(!plat_is_file(tmp));
  remove("test_plat_core.d/keep");
  remove("test_plat_core.d/sub");
  remove("test_plat_core.d");
}

static void test_wav(void) {
  int16_t s16[4] = { 16384, -16384, 32767, -32768 };
  size_t n = wav(buf, 1, 2, 48000, 16, s16, sizeof s16, false), fr = 0;
  float *o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 2 && o[0] == 0.5f && o[1] == -0.5f && o[3] == -1.0f);
  free(o);
  uint8_t u8[2] = { 128, 255 }; /* mono 8-bit: both sides the same */
  n = wav(buf, 1, 1, 48000, 8, u8, 2, false);
  o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 2 && o[0] == 0 && o[2] == o[3] && fabsf(o[2] - 127 / 128.0f) < 1e-6f);
  free(o);
  uint8_t s24[6] = { 0, 0, 0x40, 0, 0, 0xc0 };
  n = wav(buf, 1, 2, 48000, 24, s24, 6, true);
  o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 1 && o[0] == 0.5f && o[1] == -0.5f);
  free(o);
  float f32[2] = { 0.25f, -0.75f };
  n = wav(buf, 3, 2, 48000, 32, f32, 8, false);
  o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 1 && o[0] == 0.25f && o[1] == -0.75f);
  free(o);
  int32_t s32[2] = { 1 << 30, -(1 << 30) };
  n = wav(buf, 1, 2, 48000, 32, s32, 8, false);
  o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 1 && o[0] == 0.5f && o[1] == -0.5f);
  free(o);
  /* 32 kHz to 48 kHz: a 1 kHz tone keeps its length and its level */
  static int16_t tone[32000 * 2];
  for (int i = 0; i < 32000; i++) tone[i * 2] = tone[i * 2 + 1] = (int16_t)(16000 * sin(2 * M_PI * 1000 * i / 32000.0));
  n = wav(buf, 1, 2, 32000, 16, tone, sizeof tone, false);
  o = load_bytes(buf, n, 48000, &fr);
  CHECK(o && fr == 48000);
  if (o) {
    double sum = 0, err = 0;
    for (size_t i = 2000; i < fr - 2000; i++) {
      double want = 16000 / 32768.0 * sin(2 * M_PI * 1000 * i / 48000.0);
      sum += o[i * 2] * o[i * 2];
      err += (o[i * 2] - want) * (o[i * 2] - want);
    }
    double rms = sqrt(sum / (double)(fr - 4000)), e = sqrt(err / (double)(fr - 4000));
    CHECK(fabs(rms - 16000 / 32768.0 / sqrt(2)) < 0.005);
    CHECK(e < 0.002); /* about -54 dB under the tone */
  }
  free(o);
  /* broken files: no picture of a sound, no crash */
  CHECK(!plat_load_wav("/nonexistent.wav", 48000, &fr));
  CHECK(!load_bytes((const uint8_t *)"RIFF", 4, 48000, &fr));
  n = wav(buf, 1, 0, 48000, 16, s16, sizeof s16, false);
  CHECK(!load_bytes(buf, n, 48000, &fr));
  n = wav(buf, 2, 2, 48000, 16, s16, sizeof s16, false); /* ADPCM: not ours to read */
  CHECK(!load_bytes(buf, n, 48000, &fr));
  n = wav(buf, 1, 2, 48000, 12, s16, sizeof s16, false);
  CHECK(!load_bytes(buf, n, 48000, &fr));
}

/* ---- XKB ---- */
static const char KEYMAP[] =
  "xkb_keymap {\n"
  "xkb_keycodes \"evdev\" {\n minimum = 8;\n maximum = 255;\n <ESC> = 9;\n <AE01> = 10;\n <AE02> = 11;\n"
  " <AC01> = 38;\n <AC10> = 47;\n <AC11> = 48;\n <AD11> = 34;\n <LSGT> = 94;\n <SPCE> = 65;\n"
  " alias <AB11> = <LSGT>;\n indicator 1 = \"Caps Lock\";\n};\n"
  "xkb_types \"complete\" { type \"TWO_LEVEL\" { modifiers = Shift; }; };\n"
  "xkb_symbols \"pc+se\" {\n name[group1]=\"Swedish\";\n"
  " key <ESC> { [ Escape ] };\n"
  " key <AE01> { [ 1, exclam, exclamdown, onesuperior ] };\n"
  " key <AE02> { type= \"FOUR_LEVEL\", symbols[Group1]= [ 2, quotedbl, at, oneeighth ] };\n"
  " key <AC01> { [ a, A, ordfeminine, masculine ] };\n"
  " key <AC10> { [ odiaeresis, Odiaeresis, oslash, Oslash ] };\n"
  " key <AC11> { [ adiaeresis, Adiaeresis, ae, AE ] };\n"
  " key <AD11> { [ aring, Aring, dead_diaeresis ] };\n"
  " key <AB11> { [ less, greater, bar, brokenbar ] };\n"
  " key <SPCE> { [ space ] };\n"
  "};\n};\n";

static void test_xkb(void) {
  CHECK(plat_xkb_load(KEYMAP, sizeof KEYMAP - 1));
  CHECK(plat_xkb_char(2, 0) == '1' && plat_xkb_char(2, 1) == '!' && plat_xkb_char(2, 2) == 0xa1);
  CHECK(plat_xkb_char(3, 2) == '@' && plat_xkb_char(3, 1) == '"');
  CHECK(plat_xkb_char(30, 0) == 'a' && plat_xkb_char(30, 1) == 'A');
  CHECK(plat_xkb_char(39, 0) == 0xf6 && plat_xkb_char(39, 1) == 0xd6);
  CHECK(plat_xkb_char(40, 0) == 0xe4 && plat_xkb_char(26, 0) == 0xe5 && plat_xkb_char(26, 1) == 0xc5);
  CHECK(plat_xkb_char(86, 0) == '<' && plat_xkb_char(86, 2) == '|'); /* through the alias */
  CHECK(plat_xkb_char(57, 0) == ' ' && plat_xkb_char(1, 0) == 0);
  CHECK(plat_xkb_char(9999, 0) == 0 && plat_xkb_char(2, 7) == 0);
  CHECK(plat_hid_of_kernel(30) == PK_A && plat_hid_of_kernel(105) == PK_LEFT && plat_hid_of_kernel(5000) == 0);
  CHECK(plat_hid_of_scan(0x1e) == PK_A && plat_hid_of_scan(0x14b) == PK_LEFT && plat_hid_of_scan(0x11c) == PK_KP_ENTER);
  CHECK(plat_hid_of_scan(0x4b) == PK_KP_4 && plat_hid_of_scan(0x1ff) == 0);
  CHECK(plat_key_other(PK_RETURN) == '\r' && plat_key_other(PK_UP) == PKEY_OTHER + PK_UP);
  char u[4];
  CHECK(plat_utf8(0xe5, u) == 2 && (uint8_t)u[0] == 0xc3 && (uint8_t)u[1] == 0xa5);
  CHECK(plat_utf8(0x20ac, u) == 3 && plat_utf8(0x1f600, u) == 4);
  CHECK(plat_xkb_load("", 0) && plat_xkb_load("xkb_keycodes {", 14) && plat_xkb_load("xkb_symbols { key <", 19));
}

/* ---- fuzzing ---- */
static uint32_t rng = 12345;
static uint32_t rnd(void) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return rng;
}

static void fuzz(double seconds) {
  static int16_t base[256];
  for (int i = 0; i < 256; i++) base[i] = (int16_t)(i * 97);
  size_t n0 = wav(buf, 1, 2, 32000, 16, base, sizeof base, false);
  static uint8_t m[1 << 16];
  clock_t end = clock() + (clock_t)(seconds / 2 * CLOCKS_PER_SEC);
  long runs = 0;
  while (clock() < end) {
    size_t n = n0 + rnd() % 64;
    memcpy(m, buf, n0);
    for (size_t i = n0; i < n; i++) m[i] = (uint8_t)rnd();
    int k = 1 + (int)(rnd() % 8);
    for (int i = 0; i < k; i++) { /* the place, then the byte: one draw after the other */
      size_t at = rnd() % n;
      m[at] = (uint8_t)rnd();
    }
    if (rnd() % 4 == 0) n = rnd() % n;
    size_t fr;
    float *o = load_bytes(m, n, rnd() % 2 ? 48000 : 32000, &fr);
    free(o);
    runs++;
  }
  remove("test_plat_core.wav");
  end = clock() + (clock_t)(seconds / 2 * CLOCKS_PER_SEC);
  long xruns = 0;
  while (clock() < end) {
    size_t n = sizeof KEYMAP - 1;
    memcpy(m, KEYMAP, n);
    int k = 1 + (int)(rnd() % 6);
    for (int i = 0; i < k; i++) {
      size_t at = rnd() % n;
      m[at] = (uint8_t)"<>{}[];= ,\"aZ0\n"[rnd() % 16];
    }
    if (rnd() % 3 == 0) n = rnd() % n;
    plat_xkb_load((const char *)m, n);
    for (unsigned c = 0; c < 300; c += 7) plat_xkb_char(c, (int)(rnd() % 4));
    xruns++;
  }
  printf("test_plat_core: fuzzed the WAV reader %ld times, the keymap reader %ld times\n", runs, xruns);
}

/* evdev's bit masks, set as the kernel sets them (a word of unsigned long at a time): every bit read back, on a
 * system whose long is 64 bits or 32 (Windows; the old masks assumed 64) */
static void test_evdev_bits(void) {
  unsigned long m[PL_NBITS(0x300)];
  CHECK(sizeof m * 8 >= 0x300 && sizeof m * 8 < 0x300 + 8 * sizeof(unsigned long));
  for (int n = 0; n < 0x300; n++) {
    memset(m, 0, sizeof m);
    m[n / (8 * sizeof(unsigned long))] = 1ul << (n % (8 * sizeof(unsigned long)));
    int got = 0;
    for (int k = 0; k < 0x300; k++) got += (int)PL_BIT(m, k) * (k == n ? 1 : 100);
    if (got != 1) {
      CHECK(got == 1);
      break;
    }
  }
}

int main(int argc, char **argv) {
  double seconds = argc > 1 ? safe_num(argv[1], 0) : 5;
  test_queue();
  test_keys();
  test_pads();
  test_stretch();
  test_bmp();
  test_save();
  test_evdev_bits();
  test_wav();
  test_xkb();
  fuzz(seconds);
  CHECK(pumps > 0);
  printf("test_plat_core: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
