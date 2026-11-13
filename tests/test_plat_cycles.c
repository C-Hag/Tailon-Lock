/* test_plat_cycles.c - brick P2 of the platform ladder: the real backend
 * started and stopped many times without a screen or a sound card (the
 * window, the sound thread, the pads, and the GPU device on its offscreen
 * picture, each frame drawn and read back), so LeakSanitizer can see that
 * every start gives back all it took. Original code for this project.
 *
 * usage: test_plat_cycles [CYCLES [GPU_CYCLES]]   (defaults 1000 and 50; run with TL_VIDEO=offscreen
 * TL_AUDIO=dummy) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/gx.h"
#include "platform/plat.h"
#include "core/safe.h"

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_plat_cycles:%d: %s\n", __LINE__, #c);      \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static long samples;
static void fill(float *out, int n, void *ud) {
  (void)ud;
  memset(out, 0, sizeof(float) * 2 * (size_t)n);
  samples += n;
}

static void one_frame(void) {
  GxTex *swap;
  int w, h;
  if (!gx_frame_begin(&swap, &w, &h)) return;
  GxTex *t = gx_texture(GX_BGRA8, 64, 64, 1, GX_TEX_SAMPLED | GX_TEX_COLOR);
  GxBuf *b = gx_buffer(GX_BUF_VERTEX, 1024);
  static uint32_t px[64 * 64];
  static uint8_t data[1024];
  gx_upload_texture(t, px, 64, 64);
  gx_upload_buffer(b, data, sizeof data);
  GxPass p = { 0 };
  p.color = swap;
  p.color_load = GX_LOAD_CLEAR;
  p.clear[0] = 1;
  gx_pass_begin(&p);
  gx_pass_end();
  uint8_t *out = malloc((size_t)w * (size_t)h * 4);
  gx_readback(swap, out);
  gx_frame_end();
  CHECK(out && out[2] == 255); /* the clear colour, red, read back as BGRA */
  free(out);
  gx_buffer_free(b);
  gx_texture_free(t);
}

static bool on_init(int argc, char **argv, int *code) {
  int cycles = argc > 1 ? (int)safe_int(argv[1], 0, INT_MIN, INT_MAX) : 1000, gpu = argc > 2 ? (int)safe_int(argv[2], 0, INT_MIN, INT_MAX) : 50;
  for (int i = 0; i < cycles; i++) {
    CHECK(plat_init(PLAT_VIDEO | PLAT_AUDIO | PLAT_PADS, "cycles", "0", "test.cycles"));
    CHECK(plat_window_open("cycles", 320, 200));
    CHECK(plat_present_open(320, 200));
    static uint32_t fb[320 * 200];
    plat_present(fb, 320, 200);
    CHECK(plat_audio_open(48000, fill, NULL));
    PlatEvent e;
    while (plat_poll(&e)) {}
    if (i < gpu) {
      bool ok = gx_open(plat_native_window(), 320, 200, false);
      if (i == 0 && !ok) printf("test_plat_cycles: no GPU here (%s); the device is not cycled\n", gx_error());
      if (ok) {
        one_frame();
        one_frame();
        one_frame();
        gx_close();
      } else {
        gpu = 0;
      }
    }
    plat_audio_close();
    plat_present_close();
    plat_window_close();
    plat_quit();
  }
  printf("test_plat_cycles: %s (%d starts, %d with the GPU, %ld samples asked for)\n", fails ? "FAIL" : "ok", cycles, gpu,
         samples);
  *code = fails ? 1 : 0;
  return false;
}

static void on_event(const PlatEvent *ev) { (void)ev; }
static bool on_frame(void) { return false; }
static int on_quit(void) { return 0; }

const PlatApp *plat_app(void) {
  static const PlatApp A = { on_init, on_event, on_frame, on_quit };
  return &A;
}
