/* stubs.c - stand-ins for SDL, the GPU renderer and the recorded-music
 * player, so the software renderer, sim, intro and synth run as a plain
 * static program for the Pentium benchmark. Test tool only, never shipped.
 * Original code for this project. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>

#include "gpu.h"
#include "music.h"

void SDL_Log(const char *fmt, ...) {
  if (!getenv("P5_VERBOSE")) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
void SDL_free(void *p) { free(p); }
void *SDL_LoadFile(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *p = malloc((size_t)n + 1);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
    free(p);
    p = NULL;
  }
  fclose(f);
  if (p) p[n] = 0;
  if (size) *size = (size_t)n;
  return p;
}
const char *SDL_GetBasePath(void) {
  static char b[1024];
  const char *r = getenv("A4C_ROOT");
  snprintf(b, sizeof b, "%s/", r ? r : ".");
  return b;
}
const char *SDL_GetError(void) { return "stub"; }
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info) {
  struct stat st;
  if (stat(path, &st)) return false;
  info->type = S_ISREG(st.st_mode) ? SDL_PATHTYPE_FILE : S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_OTHER;
  info->size = (uint64_t)st.st_size;
  return true;
}
SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode) { return (SDL_IOStream *)fopen(path, mode); }
size_t SDL_WriteIO(SDL_IOStream *f, const void *p, size_t n) { return fwrite(p, 1, n, (FILE *)f); }
bool SDL_CloseIO(SDL_IOStream *f) { return fclose((FILE *)f) == 0; }

/* the sound-effect mixer's callback, kept so the benchmark can pull from it */
static SDL_AudioStreamCallback audio_cb;
static void *audio_ud;
static int audio_dummy;
SDL_AudioStream *SDL_OpenAudioDeviceStream(int dev, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *ud) {
  (void)dev;
  (void)spec;
  audio_cb = cb;
  audio_ud = ud;
  return (SDL_AudioStream *)&audio_dummy;
}
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *s) { return s != NULL; }
void SDL_DestroyAudioStream(SDL_AudioStream *s) { (void)s; }
bool SDL_LockAudioStream(SDL_AudioStream *s) { return s != NULL; }
bool SDL_UnlockAudioStream(SDL_AudioStream *s) { return s != NULL; }
bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *p, int n) {
  (void)s;
  (void)p;
  (void)n;
  return true;
}
void p5_pull_audio(int frames) {
  if (audio_cb) audio_cb(audio_ud, (SDL_AudioStream *)&audio_dummy, frames * (int)sizeof(float) * 2, frames * (int)sizeof(float) * 2);
}

/* no GPU: the software renderer draws everything */
bool gpu_init(SDL_Window *win, int quality) {
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
