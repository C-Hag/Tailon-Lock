/* sfx.c - sound effects (JS sNoise, sTone, enginesStart/Update; lines
 * 308-362) as a small voice pool mixed on SDL's audio thread. A noise voice
 * is white noise through a swept two-pole low-pass; a tone voice is a square
 * or sine with an exponential pitch sweep; both decay exponentially to
 * 0.0008, like the Web Audio ramps. An engine drone is a sawtooth through a
 * 380 Hz low-pass. The streamed music (music.c) is added on top. */
#include "sfx.h"

#include "music.h"

#include <SDL3/SDL.h>
#include <string.h>

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

static SDL_AudioStream *stream;
static Voice voices[MAX_VOICE];
static Engine engines[2];
static bool enabled = true, music_on = true;
static uint32_t noise = 0x12345678u;

static float white(void) {
  noise ^= noise << 13;
  noise ^= noise >> 17;
  noise ^= noise << 5;
  return (float)(int32_t)noise / 2147483648.0f;
}

static void SDLCALL mix(void *ud, SDL_AudioStream *s, int additional, int total) {
  (void)ud;
  (void)total;
  float buf[512 * 2], mus[512 * 2];
  int frames = additional / (int)(sizeof(float) * 2);
  const float dt = 1.0f / RATE;
  while (frames > 0) {
    int n = frames > 512 ? 512 : frames;
    memset(mus, 0, sizeof mus);
    if (enabled && music_on) music_render(mus, n);
    for (int i = 0; i < n; i++) {
      float out = 0;
      for (int v = 0; v < MAX_VOICE; v++) {
        Voice *V = &voices[v];
        if (!V->active) continue;
        float k = V->t / V->dur;
        float f = V->f0 * powf(V->f1 / V->f0, k);
        float g = V->vol * powf(0.0008f / V->vol, k);
        float x;
        if (V->kind == SND_NOISE) {
          float a = 1 - expf(-6.2831853f * f / RATE);
          V->lp1 += (white() - V->lp1) * a;
          V->lp2 += (V->lp1 - V->lp2) * a;
          x = V->lp2 * 1.6f;
        } else {
          V->phase += f * dt;
          V->phase -= floorf(V->phase);
          x = V->kind == SND_SQUARE ? (V->phase < 0.5f ? 0.5f : -0.5f) : sinf(V->phase * 6.2831853f);
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
        E->lp += (saw - E->lp) * (1 - expf(-6.2831853f * 380 / RATE));
        out += E->lp * E->gain;
      }
      out *= enabled ? MASTER : 0;
      for (int c = 0; c < 2; c++) {
        float v = out + mus[2 * i + c];
        buf[2 * i + c] = v > 1 ? 1 : v < -1 ? -1 : v;
      }
    }
    SDL_PutAudioStreamData(s, buf, n * (int)sizeof(float) * 2);
    frames -= n;
  }
}

bool sfx_init(void) {
  SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix, NULL);
  if (!stream) {
    SDL_Log("audio: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream);
  return true;
}

void sfx_quit(void) {
  if (stream) SDL_DestroyAudioStream(stream);
  stream = NULL;
}

void sfx_enable(bool on) { enabled = on; }
void sfx_music(bool on) { music_on = on; }
void sfx_lock(void) { if (stream) SDL_LockAudioStream(stream); }
void sfx_unlock(void) { if (stream) SDL_UnlockAudioStream(stream); }

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
    V->f1 = r->kind == SND_NOISE ? fmaxf(40, r->f1) : fmaxf(20, r->f1);
    V->vol = r->vol > 0.001f ? r->vol : 0.001f;
}

void sfx_drain(Game *g) {
  if (!stream || !enabled) {
    g->nsounds = 0;
    return;
  }
  SDL_LockAudioStream(stream);
  for (int i = 0; i < g->nsounds; i++) start_voice(&g->sounds[i]);
  SDL_UnlockAudioStream(stream);
  g->nsounds = 0;
}

void sfx_play(SoundKind kind, float f0, float f1, float dur, float vol) {
  if (!stream || !enabled) return;
  SoundReq r = { kind, f0, f1, dur, vol };
  SDL_LockAudioStream(stream);
  start_voice(&r);
  SDL_UnlockAudioStream(stream);
}

void sfx_engines(const Game *g, bool paused) {
  if (!stream) return;
  SDL_LockAudioStream(stream);
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
  SDL_UnlockAudioStream(stream);
}
