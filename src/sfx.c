/* sfx.c - sound effects (JS sNoise, sTone, enginesStart/Update; lines
 * 308-362) as a small voice pool mixed on the sound thread. A noise voice
 * is white noise through a swept two-pole low-pass; a tone voice is a square
 * or sine with an exponential pitch sweep; both decay exponentially to
 * 0.0008, like the Web Audio ramps. An engine drone is a sawtooth through a
 * 380 Hz low-pass. The streamed music (music.c) is added on top. The sum goes
 * through the master volume and a peak limiter, so a loud effect on a loud
 * bar is turned down smoothly instead of hard-clipped (clipping is harsh on
 * the ears). */
#include "sfx.h"

#include "music.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/fmath.h"
#include "platform/plat.h"

#define RATE      48000
#define MAX_VOICE 40
#define MASTER    0.45f

typedef struct {
  bool active;
  SoundKind kind;
  float t, dur, f0, f1, vol, phase, lp1, lp2;
} Voice;

typedef struct {
  float freq, gain, tfreq, tgain, phase, lp;
} Engine;

static bool stream; /* the sound device is open */
static Voice voices[MAX_VOICE];
static Engine engines[2];
static bool enabled = true, music_on = true;
static float volume = 0.354813f;  /* level 7: -9 dB */
static double listened;          /* seconds of sound played */
static float lim_gain = 1;        /* the limiter's gain, 1 when nothing is near the ceiling */
#define LIM_CEIL    0.891f        /* -1 dBFS */
#define LIM_RELEASE 0.99979f      /* back towards 1 with a 100 ms time constant at 48 kHz */
static uint32_t noise = 0x12345678u;

static float white(void) {
  noise ^= noise << 13;
  noise ^= noise >> 17;
  noise ^= noise << 5;
  return (float)(int32_t)noise / 2147483648.0f;
}

#define MIX_CHUNK 512 /* frames mixed at a time: mus[] holds this many */

/* n stereo frames, n at most MIX_CHUNK */
static void mix_chunk(float *buf, int n) {
  float mus[MIX_CHUNK * 2];
  const float dt = 1.0f / RATE;
  memset(mus, 0, sizeof mus);
  if (enabled && music_on) music_render(mus, n);
  if (enabled) listened += (double)n / RATE;
  for (int i = 0; i < n; i++) {
    float out = 0;
    for (int v = 0; v < MAX_VOICE; v++) {
      Voice *V = &voices[v];
      if (!V->active) continue;
      float k = V->t / V->dur;
      float f = V->f0 * fm_powf(V->f1 / V->f0, k);
      float g = V->vol * fm_powf(0.0008f / V->vol, k);
      float x;
      if (V->kind == SND_NWAVE) {
        /* an N-wave: the pressure jumps up, falls straight through to as far below and jumps back after f1
         * seconds (the bow and the tail shock: "ba-boom"); rounded by a lowpass at f0 (the farther off, the
         * softer), and the slow ramp between the shocks taken out (a 20 Hz highpass), so each shock is a thump */
        float p = V->t < V->f1 ? 1 - 2 * V->t / V->f1 : 0, a = 1 - fm_expf(-6.2831853f * V->f0 / RATE);
        V->lp1 += (p - V->lp1) * a;
        V->lp2 += (V->lp1 - V->lp2) * a;
        V->phase += (V->lp2 - V->phase) * (1 - fm_expf(-6.2831853f * 20 / RATE));
        x = V->lp2 - V->phase;
        g = V->vol;
      } else if (V->kind == SND_NOISE) {
        float a = 1 - fm_expf(-6.2831853f * f / RATE);
        V->lp1 += (white() - V->lp1) * a;
        V->lp2 += (V->lp1 - V->lp2) * a;
        x = V->lp2 * 1.6f;
      } else {
        V->phase += f * dt;
        V->phase -= floorf(V->phase);
        x = V->kind == SND_SQUARE ? (V->phase < 0.5f ? 0.5f : -0.5f) : fm_sinf(V->phase * 6.2831853f);
      }
      out += x * g;
      V->t += dt;
      if (V->t >= V->dur) V->active = false;
    }
    for (int e = 0; e < 2; e++) {
      Engine *E = &engines[e];
      E->freq += (E->tfreq - E->freq) * (dt / 0.1f);
      E->gain += (E->tgain - E->gain) * (dt / 0.15f);
      if (E->gain < 1e-5f && E->tgain == 0) continue;
      E->phase += E->freq * dt;
      E->phase -= floorf(E->phase);
      float saw = E->phase * 2 - 1;
      E->lp += (saw - E->lp) * (1 - fm_expf(-6.2831853f * 380 / RATE));
      out += E->lp * E->gain;
    }
    out *= enabled ? MASTER : 0;
    float l = (out + mus[2 * i]) * volume, r = (out + mus[2 * i + 1]) * volume;
    /* peak limiter: the gain drops at once to keep the louder channel under the ceiling and recovers slowly */
    float pk = fmaxf(fabsf(l), fabsf(r));
    lim_gain = 1 - (1 - lim_gain) * LIM_RELEASE;
    if (pk * lim_gain > LIM_CEIL) lim_gain = LIM_CEIL / pk;
    buf[2 * i] = l * lim_gain;
    buf[2 * i + 1] = r * lim_gain;
  }
}

/* The sound thread asks for n stereo frames, as many as its device wants (CoreAudio can ask for 2048 or more). */
static void mix(float *buf, int n, void *ud) {
  (void)ud;
  for (int at = 0; at < n; at += MIX_CHUNK) mix_chunk(buf + 2 * at, n - at < MIX_CHUNK ? n - at : MIX_CHUNK);
}

/* test hook: A4C_MIX_DUMP=file.raw with --frames writes the whole mix (effects and music, float stereo, 48 kHz)
 * instead of playing it, one tick's worth per tick, so a film of the intro gets its sound in step */
static FILE *mix_dump;

void sfx_dump_tick(int frames) {
  float buf[512 * 2];
  for (; mix_dump && frames > 0; frames -= 512) {
    int n = frames > 512 ? 512 : frames;
    mix(buf, n, NULL);
    fwrite(buf, sizeof(float) * 2, (size_t)n, mix_dump);
  }
}

void sfx_volume(int level) {
  if (level < 1) level = 1;
  if (level > 10) level = 10;
  float v = fm_powf(10, (float)(level - 10) * 3 / 20);
  if (stream) plat_audio_lock(); /* the mixer reads it on the sound thread */
  volume = v;
  if (stream) plat_audio_unlock();
}

double sfx_listened(void) {
  if (stream) plat_audio_lock(); /* the sound thread adds to it */
  double t = listened;
  if (stream) plat_audio_unlock();
  return t;
}

bool sfx_dumping(void) { return mix_dump != NULL; }

static void start_voice(const SoundReq *r);

/* the effects as the game plays them (src/core/sim.c, defend.c; the player's own, not the CPU pilots' quieter
 * copies): one request, played count times every so many seconds */
typedef struct {
  const char *name;
  SoundReq r;
  int count;
  float every;
} EffectRun;
static const EffectRun EFFECTS[] = {
  { "none", { SND_NOISE, 100, 100, 0.01f, 0.001f }, 0, 0 }, /* the music alone, to measure the effects against */
  { "explosion", { SND_NOISE, 900, 60, 1.1f, 0.3f }, 1, 0 },
  { "explosion-big", { SND_NOISE, 900, 60, 1.54f, 0.3f }, 1, 0 },
  { "hit", { SND_NOISE, 900, 200, 0.12f, 0.12f }, 1, 0 },
  { "gun", { SND_NOISE, 2400, 600, 0.06f, 0.09f }, 20, 0.075f },
  { "missile-launch", { SND_NOISE, 1600, 200, 0.7f, 0.14f }, 1, 0 },
  { "flares", { SND_NOISE, 5000, 2000, 0.25f, 0.07f }, 3, 0.16f },
  { "chaff", { SND_NOISE, 3000, 1500, 0.25f, 0.07f }, 3, 0.16f },
  { "lock-acquired", { SND_SQUARE, 1400, 1400, 0.08f, 0.05f }, 1, 0 },
  { "catapult", { SND_NOISE, 3200, 160, 1.6f, 0.3f }, 1, 0 },
  { "missile-warning", { SND_SQUARE, 1800, 1100, 0.12f, 0.06f }, 12, 0.22f },
  { "locked-beep", { SND_SINE, 1250, 1250, 0.05f, 0.035f }, 20, 0.12f },
  { "seeking-beep", { SND_SINE, 700, 700, 0.05f, 0.025f }, 6, 0.4f },
  { "sam-launch", { SND_NOISE, 1200, 150, 0.8f, 0.08f }, 1, 0 },
  { "ship-missile", { SND_NOISE, 1400, 200, 0.9f, 0.08f }, 1, 0 },
  { "ciws", { SND_NOISE, 3000, 900, 0.04f, 0.035f }, 30, 0.05f },
};

static bool write_wav_f32(const char *path, const float *x, uint32_t frames) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  uint8_t h[44] = { 'R', 'I', 'F', 'F' };
  uint32_t v32, data = frames * 8;
  uint16_t v16;
#define P32(o, v) (v32 = (v), memcpy(h + (o), &v32, 4))
#define P16(o, v) (v16 = (v), memcpy(h + (o), &v16, 2))
  P32(4, 36 + data);
  memcpy(h + 8, "WAVEfmt ", 8);
  P32(16, 16); P16(20, 3); P16(22, 2); P32(24, RATE); P32(28, RATE * 8); P16(32, 8); P16(34, 32);
  memcpy(h + 36, "data", 4);
  P32(40, data);
#undef P32
#undef P16
  bool ok = fwrite(h, 1, 44, f) == 44 && fwrite(x, 8, frames, f) == frames;
  return fclose(f) == 0 && ok;
}

bool sfx_render_effects(const char *dir) {
  enum { SECS = 5, LEAD = 1, BLOCK = 480 };
  const uint32_t frames = SECS * RATE;
  float *out = malloc((size_t)frames * 2 * sizeof(float)), skip[512 * 2];
  if (!out) return false;
  int fb = music_theme_index("fullburn");
  bool ok = true;
  for (size_t e = 0; e < sizeof EFFECTS / sizeof EFFECTS[0]; e++) {
    const EffectRun *R = &EFFECTS[e];
    memset(voices, 0, sizeof voices);
    memset(engines, 0, sizeof engines);
    lim_gain = 1;
    enabled = music_on = true;
    music_restart(fb);
    music_level(true);
    for (int i = 0; i < 61 * RATE / 512; i++) music_render(skip, 512); /* to the first chorus (bar 33) */
    int next = 0;
    for (uint32_t i = 0; i < frames; i += BLOCK) {
      while (next < R->count && i >= (uint32_t)((LEAD + next * R->every) * RATE)) {
        start_voice(&R->r);
        next++;
      }
      mix(out + 2 * i, BLOCK, NULL);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/sfx-%s.wav", dir, R->name);
    ok &= write_wav_f32(path, out, frames);
  }
  free(out);
  return ok;
}

bool sfx_init(void) {
  const char *dump = getenv("A4C_MIX_DUMP");
  if (dump) {
    mix_dump = fopen(dump, "wb");
    stream = mix_dump != NULL;
    return stream;
  }
  stream = plat_audio_open(RATE, mix, NULL);
  if (!stream) plat_log("audio: %s", plat_error());
  return stream;
}

void sfx_quit(void) {
  if (mix_dump) fclose(mix_dump);
  else if (stream) plat_audio_close();
  mix_dump = NULL;
  stream = false;
}

/* the mixer reads these on the sound thread: they change under its lock */
void sfx_enable(bool on) {
  if (stream) plat_audio_lock();
  enabled = on;
  if (stream) plat_audio_unlock();
}
void sfx_music(bool on) {
  if (stream) plat_audio_lock();
  music_on = on;
  if (stream) plat_audio_unlock();
}
void sfx_lock(void) { if (stream) plat_audio_lock(); }
void sfx_unlock(void) { if (stream) plat_audio_unlock(); }

static void start_voice(const SoundReq *r) {
  int slot = -1;
  float oldest = -1;
  for (int v = 0; v < MAX_VOICE; v++) {
    if (!voices[v].active) { slot = v; break; }
      if (voices[v].t > oldest) { oldest = voices[v].t; slot = v; }
    }
    Voice *V = &voices[slot];
    memset(V, 0, sizeof *V);
    V->active = true;
    V->kind = r->kind;
    V->dur = r->dur > 0.01f ? r->dur : 0.01f;
    V->f0 = r->f0;
    V->f1 = r->kind == SND_NOISE ? fmaxf(40, r->f1) : r->kind == SND_NWAVE ? fmaxf(0.01f, r->f1) : fmaxf(20, r->f1);
    V->vol = r->vol > 0.001f ? r->vol : 0.001f;
}

void sfx_drain(Game *g) {
  if (!stream || !enabled) {
    g->nsounds = 0;
    return;
  }
  plat_audio_lock();
  for (int i = 0; i < g->nsounds; i++) start_voice(&g->sounds[i]);
  plat_audio_unlock();
  g->nsounds = 0;
}

void sfx_play(SoundKind kind, float f0, float f1, float dur, float vol) {
  if (!stream || !enabled) return;
  SoundReq r = { kind, f0, f1, dur, vol };
  plat_audio_lock();
  start_voice(&r);
  plat_audio_unlock();
}

void sfx_engines(const Game *g, bool paused) {
  if (!stream) return;
  plat_audio_lock();
  int n = g ? g->nlocals : 0;
  for (int e = 0; e < 2; e++) {
    Engine *E = &engines[e];
    if (e >= n) {
      E->tgain = 0;
      continue;
    }
    const Plane *p = &g->planes[g->locals[e]];
    bool on = !paused && p->alive && p->fuel > 0;
    E->tfreq = 48 + p->throttle * 62 + p->speed * 0.05f;
    if (E->freq == 0) E->freq = E->tfreq;
    E->tgain = on ? 0.05f / n + 0.015f : 0;
  }
  plat_audio_unlock();
}
