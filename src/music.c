/* music.c - the soundtrack, played live by the SID-style synth (synth.c)
 * from the scores in assets/music. Replaces the JS TRACKS table and
 * musicUpdate (the browser game, web/index.html). The rendered mp3s in
 * music/tracks stay in the repo as the reference for how each theme should
 * sound, but the game no longer ships them. */
#include "music.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "assets.h"
#include "sfx.h"
#include "synth.h"

/* Levels are the JS play/menu volumes; mix evens out the themes (measured
 * with --render-music: about 0.14 RMS at menu level). */
const Theme THEMES[MUSIC_ALL] = {
  { "horizon", "STEEL HORIZON", 0.17f, 0.6f, 1.13f },
  { "pop", "NOSE 2 NOSE POP", 0.17f, 0.6f, 1.23f },
  { "h2h", "NOSE 2 NOSE", 0.17f, 0.6f, 1.17f },
  { "anthem", "H2H ANTHEM", 0.17f, 0.6f, 1.15f },
  { "strike", "STRIKE", 0.17f, 0.6f, 1.19f },
  { "cine", "CINEMATIC", 0.17f, 0.6f, 1.12f },
  { "chip", "CHIPTUNE", 0.14f, 0.5f, 1.40f },
  { "overtone", "OVERTONE SID", 0.16f, 0.5f, 1.36f },
  { "neon", "NEON STRIKE", 0.17f, 0.6f, 1.13f },
  { "midnight", "MIDNIGHT ACE", 0.17f, 0.6f, 1.12f },
  { "rocknroll", "SIX O'CLOCK HIGH", 0.17f, 0.6f, 1.08f },
  { "ghost", "GHOST SQUADRON", 0.17f, 0.6f, 1.10f },
  /* the main theme, Full Burn "Arp sound DMX10", a recording (assets/music/fullburn.wav, looping as
   * fullburn.loop says; 0.13 RMS at menu level like the scores) */
  { "fullburn", "FULL BURN", 0.17f, 0.6f, 1.05f },
  /* not in the menu: the cut of Steel Horizon the opening film is timed to */
  { "horizon-intro", "STEEL HORIZON (FILM)", 0.17f, 0.6f, 0.97f },
  /* the main theme's film cut, a recording (assets/music/fullburn-intro.wav, played once; it starts half a
   * second before the song) */
  { "fullburn-intro", "FULL BURN (FILM)", 0.17f, 0.6f, 1.05f },
};

#define LEVEL_SCALE 1.6f

static Score *scores[MUSIC_ALL];
static bool   tried[MUSIC_ALL];
static int    current = -1;
static float  gain, target;
/* a recording: stereo float frames at the synth's rate; it loops from loop_a back at loop_b when its
 * assets/music/<key>.loop gives the two frames, and plays once otherwise */
static float *rec[MUSIC_ALL];
static size_t rec_n[MUSIC_ALL], rec_a[MUSIC_ALL], rec_b[MUSIC_ALL];
static bool   rec_tried[MUSIC_ALL];
static const float *stream;
static size_t stream_n, stream_pos, loop_a, loop_b;

static bool load_recording(int theme) {
  if (rec_tried[theme]) return rec[theme] != NULL;
  rec_tried[theme] = true;
  char rel[64];
  SDL_snprintf(rel, sizeof rel, "music/%s.wav", THEMES[theme].key);
  SDL_AudioSpec spec, want = { SDL_AUDIO_F32, 2, SYNTH_RATE };
  Uint8 *buf = NULL, *out = NULL;
  Uint32 len = 0;
  int outlen = 0;
  if (!SDL_LoadWAV(assets_path(rel), &spec, &buf, &len)) return false;
  bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len, &want, &out, &outlen);
  SDL_free(buf);
  if (!ok) return false;
  rec[theme] = (float *)out;
  rec_n[theme] = (size_t)outlen / (2 * sizeof(float));
  SDL_snprintf(rel, sizeof rel, "music/%s.loop", THEMES[theme].key);
  size_t sz = 0;
  char *txt = SDL_LoadFile(assets_path(rel), &sz);
  if (txt) {
    unsigned long a = 0, b = 0;
    if (SDL_sscanf(txt, "%lu %lu", &a, &b) == 2 && a < b && b <= rec_n[theme]) {
      rec_a[theme] = a;
      rec_b[theme] = b;
    }
    SDL_free(txt);
  }
  if (rec_b[theme])
    SDL_Log("music: %s, a recording, %.1f s, loop %.1f-%.1f s", THEMES[theme].key, rec_n[theme] / (double)SYNTH_RATE,
            rec_a[theme] / (double)SYNTH_RATE, rec_b[theme] / (double)SYNTH_RATE);
  else
    SDL_Log("music: %s, a recording, %.1f s, played once", THEMES[theme].key, rec_n[theme] / (double)SYNTH_RATE);
  return true;
}

int music_theme_index(const char *key) {
  for (int i = 0; i < MUSIC_ALL; i++)
    if (!strcmp(THEMES[i].key, key)) return i;
  return 0;
}

bool music_init(void) { return true; }

void music_quit(void) {
  sfx_lock();
  synth_stop();
  current = -1;
  sfx_unlock();
}

int music_current(void) { return current; }

double music_clock(void) {
  if (current < 0) return -1;
  if (stream) {
    sfx_lock();
    double t = (double)stream_pos / SYNTH_RATE;
    sfx_unlock();
    return t;
  }
  if (!synth_playing()) return -1;
  sfx_lock();
  double t = synth_time();
  sfx_unlock();
  return t;
}

void music_restart(int theme) {
  current = -2; /* force a restart */
  music_play(theme);
}

bool music_finished(void) {
  sfx_lock();
  bool done = stream && !loop_b && stream_pos >= stream_n;
  sfx_unlock();
  return done;
}

bool music_recorded(int theme) { return theme >= 0 && theme < MUSIC_ALL && load_recording(theme); }

Score *music_score(int theme) {
  if (theme < 0 || theme >= MUSIC_ALL) return NULL;
  if (!tried[theme]) {
    tried[theme] = true;
    scores[theme] = synth_load(THEMES[theme].key);
  }
  return scores[theme];
}

void music_play(int theme) {
  if (theme == current) return;
  bool recorded = music_recorded(theme);
  Score *s = recorded ? NULL : music_score(theme);
  sfx_lock();
  current = theme;
  gain = target;
  stream = recorded ? rec[theme] : NULL;
  stream_n = recorded ? rec_n[theme] : 0;
  loop_a = recorded ? rec_a[theme] : 0;
  loop_b = recorded ? rec_b[theme] : 0;
  stream_pos = 0;
  if (s) synth_start(s);
  else synth_stop();
  sfx_unlock();
}

void music_continue(int theme, double offset) {
  if (theme == current) return;
  if (!music_recorded(theme)) {
    music_play(theme);
    return;
  }
  sfx_lock();
  double t = stream ? stream_pos / (double)SYNTH_RATE + offset : 0;
  size_t pos = t > 0 ? (size_t)(t * SYNTH_RATE + 0.5) : 0;
  synth_stop();
  current = theme;
  stream = rec[theme];
  stream_n = rec_n[theme];
  loop_a = rec_a[theme];
  loop_b = rec_b[theme];
  stream_pos = pos < stream_n ? pos : 0;
  sfx_unlock();
}

void music_level(bool in_game) {
  if (current < 0) return;
  target = (in_game ? THEMES[current].play : THEMES[current].menu) * THEMES[current].mix * LEVEL_SCALE;
}

void music_render(float *out, int n) {
  /* test hook: A4C_MUSIC_DUMP=file.raw writes the music as it is played (float stereo, 48 kHz) */
  static SDL_IOStream *dump;
  static bool dump_tried;
  if (!dump_tried) {
    dump_tried = true;
    const char *p = SDL_getenv("A4C_MUSIC_DUMP");
    if (p) dump = SDL_IOFromFile(p, "wb");
  }
  float *start = out;
  int frames = n;
  /* glide the level per block; the synth renders straight into the mix */
  gain += (target - gain) * fminf(1, n / 9600.0f);
  if (stream) {
    for (int i = 0; i < n; i++, stream_pos++) {
      if (loop_b && stream_pos >= loop_b) stream_pos = loop_a; /* the asset has the crossfade in it */
      if (stream_pos >= stream_n) break;
      out[2 * i] += stream[2 * stream_pos] * gain;
      out[2 * i + 1] += stream[2 * stream_pos + 1] * gain;
    }
    if (dump) SDL_WriteIO(dump, start, (size_t)frames * 2 * sizeof(float));
    return;
  }
  synth_render(out, n, gain);
  if (dump) SDL_WriteIO(dump, start, (size_t)frames * 2 * sizeof(float));
}
