/* p5bench.c - how much work the game's software renderer, flight sim and
 * synth do per frame and per second of audio, built as a plain static
 * 32-bit x87 program (-march=pentium, no MMX or SSE) and run under
 * valgrind's callgrind, to estimate what an old Pentium would make of it.
 * Test tool only, never shipped. Original code for this project.
 *
 *   p5bench scene dogfight|deck|cic W H full|retro FRAMES
 *   p5bench sfx SECONDS
 *   p5bench synth h2h|sid3 SECONDS
 *
 * Every measured piece of work runs inside its own bench_* function, so
 * callgrind --toggle-collect=bench_* counts only that: the setup (loading
 * assets, flying the match or the film up to the moment) is left out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "assets.h"
#include "core/game.h"
#include "core/map.h"
#include "core/sim.h"
#include "core/world.h"
#include "intro.h"
#include "raster.h"
#include "render3d.h"
#include "sfx.h"
#include "synth.h"

static Framebuffer fb;
static Game game;

__attribute__((noinline)) void bench_sim(Game *g) { sim_update(g, 1.0f / 60); }
__attribute__((noinline)) void bench_render(Game *g) { render_game(&fb, g); }
__attribute__((noinline)) void bench_intro_update(void) { intro_update(1.0f / 60); }
__attribute__((noinline)) void bench_intro_render(void) { intro_render(&fb); }
__attribute__((noinline)) void bench_sfx(int frames) { p5_pull_audio(frames); }
__attribute__((noinline)) void bench_synth(float *buf, int frames) { synth_render(buf, frames, 1); }

/* the retro detail: clear sky with no clouds at all (the "clear" preset keeps a few cumulus; the bench
 * empties its list, the game is not changed) */
static void no_clouds(const Map *map, int tod) {
  ((Map *)map)->weather[WX_CLEAR].nclouds = 0;
  world_set_map(map, tod, WX_CLEAR);
}

static int scene(const char *name, int w, int h, bool retro, int frames) {
  if (!fb_resize(&fb, w, h)) return 1;
  if (!strcmp(name, "dogfight")) {
    int m = assets_map_find("twin-isles");
    const Map *map = assets_map(m);
    if (retro) no_clouds(map, TOD_DAY);
    else world_set_map(map, TOD_DAY, map->default_weather);
    game_new(&game, MODE_1P, DIFF_NORMAL, false, NULL, 1);
    for (int i = 0; i < game.nlocals; i++) { /* the player's seat flies itself, so the fight happens */
      Plane *p = &game.planes[game.locals[i]];
      p->is_ai = true;
      p->ai.diff = DIFF_NORMAL;
      p->ai.gt = -1;
    }
    for (int i = 0; i < 600; i++) sim_update(&game, 1.0f / 60);
    for (int i = 0; i < frames; i++) {
      bench_sim(&game);
      game.nsounds = 0;
      bench_render(&game);
    }
  } else {
    float at = !strcmp(name, "deck") ? 9.6f : 7.5f; /* the deck as Viper 1 runs up; the control room's wide frame */
    if (!intro_init()) return 1;
    intro_start(false);
    if (retro) no_clouds(assets_map(assets_map_find("twin-isles")), TOD_DAWN);
    for (int i = 0; i < (int)(at * 60); i++) intro_update(1.0f / 60);
    for (int i = 0; i < frames; i++) {
      bench_intro_update();
      bench_intro_render();
    }
  }
  const char *shot = getenv("P5_SHOT"); /* the last frame as a 24-bit BMP, to see what was measured */
  if (shot) {
    FILE *f = fopen(shot, "wb");
    if (f) {
      int row = (w * 3 + 3) & ~3;
      uint32_t size = 54 + (uint32_t)row * h;
      uint8_t hd[54] = { 'B', 'M' };
      memcpy(hd + 2, &size, 4);
      hd[10] = 54, hd[14] = 40, hd[26] = 1, hd[28] = 24;
      memcpy(hd + 18, &w, 4);
      memcpy(hd + 22, &h, 4);
      fwrite(hd, 1, 54, f);
      uint8_t *line = calloc(1, (size_t)row);
      for (int y = h - 1; line && y >= 0; y--) {
        for (int x = 0; x < w; x++) {
          uint32_t c = fb.px[y * w + x];
          line[x * 3] = (uint8_t)c, line[x * 3 + 1] = (uint8_t)(c >> 8), line[x * 3 + 2] = (uint8_t)(c >> 16);
        }
        fwrite(line, 1, (size_t)row, f);
      }
      free(line);
      fclose(f);
    }
  }
  uint64_t sum = 0; /* touch the picture so nothing is optimised away */
  for (int i = 0; i < w * h; i += 97) sum += fb.px[i];
  printf("%s %dx%d %s: %d frames, checksum %llu\n", name, w, h, retro ? "retro" : "full", frames, (unsigned long long)sum);
  return 0;
}

static int sfx(float seconds) {
  if (!sfx_init()) return 1;
  /* a busy moment of a dogfight: guns, a missile, flares, an explosion over the two engine drones */
  game_new(&game, MODE_1P, DIFF_NORMAL, false, NULL, 1);
  sfx_engines(&game, false);
  sfx_play(SND_NOISE, 3400, 600, 1.1f, 0.22f);
  sfx_play(SND_NOISE, 2400, 900, 0.5f, 0.12f);
  sfx_play(SND_SQUARE, 1300, 500, 0.3f, 0.03f);
  sfx_play(SND_NOISE, 1100, 50, 1.6f, 0.42f);
  sfx_play(SND_SINE, 90, 30, 0.7f, 0.3f);
  int n = (int)(seconds * 48000);
  for (int i = 0; i < n; i += 4800) {
    if (i % 48000 == 0) { /* fresh hits every second, so voices are always playing */
      sfx_play(SND_NOISE, 3400, 600, 1.1f, 0.22f);
      sfx_play(SND_NOISE, 1100, 50, 1.6f, 0.42f);
      sfx_play(SND_SQUARE, 1300, 500, 0.3f, 0.03f);
    }
    bench_sfx(4800);
  }
  printf("sfx: %.1f s at 48 kHz\n", seconds);
  return 0;
}

/* three SID-style voices playing all the time: a pulse lead with vibrato and a pulse-width sweep, a sawtooth
 * bass through the low-pass filter, a triangle arpeggio */
static Score *sid3(float seconds) {
  Score *s = calloc(1, sizeof *s);
  snprintf(s->key, sizeof s->key, "sid3");
  s->end = s->loop = seconds;
  s->ninst = 3;
  Patch lead = { W_PULSE, F_NONE, 0.3f, 0, 0.01f, 0.1f, 0.7f, 0.1f, 0.4f, 0.3f, 0.5f, 0.2f, 5.5f, 0.15f, 0, 0, 0, 0, 0, 0, 0 };
  Patch bass = { W_SAW, F_LP, 0.3f, 0, 0.005f, 0.15f, 0.5f, 0.05f, 0.5f, 0, 0, 0, 0, 0, 2400, 500, 0.2f, 0.4f, 0, 0, 0 };
  Patch arp = { W_TRI, F_NONE, 0.25f, 0, 0.002f, 0.05f, 0.6f, 0.05f, 0.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  s->inst[0].p = lead;
  s->inst[1].p = bass;
  s->inst[2].p = arp;
  snprintf(s->inst[0].name, 24, "lead");
  snprintf(s->inst[1].name, 24, "bass");
  snprintf(s->inst[2].name, 24, "arp");
  static const int ARP[4] = { 0, 4, 7, 12 };
  for (float t = 0; t < seconds; t += 0.125f) {
    int k = (int)(t * 8);
    synth_add_note(s, t, 2, 69 + ARP[k % 4], 0.12f, 0.8f);
    if (k % 4 == 0) synth_add_note(s, t, 1, 33 + (k / 16 % 2) * 5, 0.5f, 0.9f);
    if (k % 8 == 0) synth_add_note(s, t, 0, 76 + (k / 8 % 3) * 2, 1.0f, 0.8f);
  }
  synth_sort(s);
  return s;
}

static int synth(const char *key, float seconds) {
  Score *s = !strcmp(key, "sid3") ? sid3(seconds) : synth_load(key);
  if (!s) return 1;
  synth_start(s);
  static float buf[4800 * 2];
  int n = (int)(seconds * SYNTH_RATE);
  for (int i = 0; i < n; i += 4800) {
    memset(buf, 0, sizeof buf);
    bench_synth(buf, 4800);
  }
  printf("synth %s: %.1f s at %d Hz\n", key, seconds, SYNTH_RATE);
  return 0;
}

int main(int argc, char **argv) {
  world_init();
  if (!assets_init()) {
    fprintf(stderr, "no assets (set A4C_ROOT to the repository)\n");
    return 1;
  }
  if (argc >= 7 && !strcmp(argv[1], "scene"))
    return scene(argv[2], atoi(argv[3]), atoi(argv[4]), !strcmp(argv[5], "retro"), atoi(argv[6]));
  if (argc >= 3 && !strcmp(argv[1], "sfx")) return sfx((float)atof(argv[2]));
  if (argc >= 4 && !strcmp(argv[1], "synth")) return synth(argv[2], (float)atof(argv[3]));
  fprintf(stderr, "usage: p5bench scene dogfight|deck|cic W H full|retro FRAMES | sfx SECONDS | synth h2h|sid3 SECONDS\n");
  return 2;
}
