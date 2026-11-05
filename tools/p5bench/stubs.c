/* stubs.c - stand-ins for the platform backend, the GPU renderer and the
 * recorded-music player, so the software renderer, sim, intro and synth
 * run as a plain static program for the Pentium benchmark. Test tool only,
 * never shipped. Original code for this project. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpu.h"
#include "music.h"
#include "platform/plat.h"

void plat_logv(const char *fmt, va_list ap) {
  if (!getenv("P5_VERBOSE")) return;
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}
void plat_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  plat_logv(fmt, ap);
  va_end(ap);
}
const char *plat_error(void) { return "stub"; }
uint64_t plat_ms(void) { return 0; }

/* the sound-effect mixer's callback, kept so the benchmark can pull from it */
static PlatAudioFn audio_fn;
static void *audio_ud;
bool plat_audio_open(int rate, PlatAudioFn fn, void *ud) {
  (void)rate;
  audio_fn = fn;
  audio_ud = ud;
  return true;
}
void plat_audio_close(void) {}
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
void p5_pull_audio(int frames) {
  static float buf[512 * 2];
  for (; audio_fn && frames > 0; frames -= 512) audio_fn(buf, frames > 512 ? 512 : frames, audio_ud);
}

/* no GPU: the software renderer draws everything */
bool gpu_init(void *win, int quality) {
  (void)win;
  (void)quality;
  return false;
}
void gpu_quit(void) {}
bool gpu_active(void) { return false; }
const char *gpu_driver(void) { return "none"; }
void gpu_set_quality(int q) { (void)q; }
void gpu_view_begin(vec3 pos, vec3 r, vec3 u, vec3 f, float vx, float vy, float vw, float vh, float F, float near, const GpuEnv *env) {
  (void)pos, (void)r, (void)u, (void)f, (void)vx, (void)vy, (void)vw, (void)vh, (void)F, (void)near, (void)env;
}
void gpu_mesh(const Mesh *m, vec3 pos, vec3 r, vec3 u, vec3 f, float scale, float dim, float gloss, bool cull_island) {
  (void)m, (void)pos, (void)r, (void)u, (void)f, (void)scale, (void)dim, (void)gloss, (void)cull_island;
}
void gpu_sprite(vec3 pos, float radius, float min_px, uint32_t rgb, float alpha, int kind) {
  (void)pos, (void)radius, (void)min_px, (void)rgb, (void)alpha, (void)kind;
}
void gpu_line(vec3 a, vec3 b, uint32_t rgb, float alpha, float intensity) { (void)a, (void)b, (void)rgb, (void)alpha, (void)intensity; }
void gpu_quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t rgb) { (void)a, (void)b, (void)c, (void)d, (void)rgb; }
void gpu_view_end(void) {}
void gpu_present(const Framebuffer *overlay) { (void)overlay; }
void gpu_view_layer(int layer) { (void)layer; }
void gpu_set_blend(float current, float smear_uv) { (void)current, (void)smear_uv; }
void gpu_set_post(float warm, float vignette, float grain, float haze_y0, float haze_y1, float haze, float time) {
  (void)warm, (void)vignette, (void)grain, (void)haze_y0, (void)haze_y1, (void)haze, (void)time;
}
void gpu_screenshot(const char *path) { (void)path; }

/* no recorded music: the intro runs on the wall clock */
bool music_init(void) { return false; }
void music_quit(void) {}
void music_play(int theme) { (void)theme; }
void music_restart(int theme) { (void)theme; }
double music_clock(void) { return -1; }
int music_current(void) { return -1; }
bool music_finished(void) { return false; }
bool music_recorded(int theme) { (void)theme; return false; }
void music_continue(int theme, double offset) { (void)theme, (void)offset; }
void music_level(bool in_game) { (void)in_game; }
int music_theme_index(const char *key) { (void)key; return -1; }
struct Score *music_score(int theme) { (void)theme; return NULL; }
void music_render(float *out, int n) { (void)out, (void)n; }
