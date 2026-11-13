/* sfx.c - sound effects (JS sNoise, sTone, enginesStart/Update; lines
 * 308-362) as a small voice pool mixed on the sound thread. A noise voice
 * is white noise through a swept two-pole low-pass; a tone voice is a square
 * or sine with an exponential pitch sweep; both decay exponentially to
 * 0.0008, like the Web Audio ramps. An engine is a jet's sound built up in layers
 * (engine_sample below). The streamed music (music.c) is added on top. The sum goes
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

/* A jet engine, as the acoustics of a real one have it: no pitch of its own to speak of, but the roar of the exhaust
 * (broadband noise, brighter and louder with power), over the whine of the compressor (thin tones at the blade
 * passing frequency, which climb with the engine's speed), and in afterburner a deep rumble with crackle on top.
 * The engine's speed lags the throttle, so it spools. */
typedef struct {
  float spool, tspool, ab, tab, speed, tspeed, gain, tgain; /* where it is, and where it is heading */
  float ph[3];                     /* the whine's partials */
  float roar[2][3];                /* the exhaust, per channel: two low-pass poles and the high-pass's state */
  float hiss[2], wind[2][2];       /* the intake's and the airflow's high bands */
  float rumble[3], turb, env, crack[2], thump;
  float k_roar, k_whine, g_roar, g_whine, g_hiss, g_wind, g_rumble, g_crack, rate;   /* set once a chunk */
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
static float duck = 1, duck_to = 1; /* the music's duck (the film's kills only): stays exactly 1 everywhere else */
#define DUCK_RATE   0.0005f       /* per sample: a 40 ms time constant, no step in the music, so no click */
static uint32_t noise = 0x12345678u;

static float white(void) {
  noise ^= noise << 13;
  noise ^= noise >> 17;
  noise ^= noise << 5;
  return (float)(int32_t)noise / 2147483648.0f;
}

#define MIX_CHUNK 512 /* frames mixed at a time: mus[] holds this many */

static float one_pole(float hz) { return 1 - fm_expf(-6.2831853f * hz / RATE); }

/* what changes slowly, once a chunk: the spool towards the throttle, and the levels and filters that follow it */
static void engine_chunk(Engine *E, int n) {
  float dt = (float)n / RATE;
  E->spool += (E->tspool - E->spool) * fminf(1, dt / (E->tspool > E->spool ? 1.6f : 1.1f));
  E->ab += (E->tab - E->ab) * fminf(1, dt / (E->tab > E->ab ? 0.35f : 0.5f));
  E->speed += (E->tspeed - E->speed) * fminf(1, dt / 0.5f);
  E->gain += (E->tgain - E->gain) * fminf(1, dt / 0.15f);
  if (E->tab > 0.05f && E->ab < 0.05f) E->thump = 1; /* the burner catches: a thump */
  E->thump *= fm_expf(-dt / 0.22f);
  float s = E->spool, s2 = s * s, w = fminf(E->speed / 300, 1.5f);
  float fc = 260 + 1150 * s + 500 * E->ab;
  E->k_roar = one_pole(fc);
  E->k_whine = (850 + 2150 * s) / RATE;
  /* the noise through two poles loses level as its cut-off falls: the root of the cut-off puts it back */
  E->g_roar = (0.20f + 0.30f * s + 0.30f * s2 + 0.75f * E->ab) * sqrtf(1400 / fc);
  E->g_whine = 0.016f + 0.026f * s * (1 - 0.5f * E->ab);
  E->g_hiss = 0.03f + 0.10f * s2;
  E->g_wind = 0.16f * w * w;
  E->g_rumble = (1.4f * E->ab + 2.0f * E->thump * E->tab) * (0.8f + 0.2f * s);
  E->g_crack = 0.8f * E->ab;
  E->rate = 34 * E->ab / RATE;
}

/* one frame of an engine into l, r */
static void engine_sample(Engine *E, float *l, float *r) {
  const float kl = 0.0052f /* 40 Hz */, kt = 0.0013f /* 10 Hz */, kh = 0.26f /* 2.4 kHz */, kr = 0.021f /* 160 Hz */;
  float c = white();
  E->turb += (white() - E->turb) * kt;
  float wob = 1 + 6 * E->turb; /* the slow, uneven breathing of a real flame */
  for (int ch = 0; ch < 2; ch++) {
    float *q = E->roar[ch], x = 0.72f * c + 0.7f * white();
    q[0] += (x - q[0]) * E->k_roar;
    q[1] += (q[0] - q[1]) * E->k_roar;
    q[2] += (q[1] - q[2]) * kl;
    float y = (q[1] - q[2]) * E->g_roar * wob;
    float n = white();
    E->hiss[ch] += (n - E->hiss[ch]) * kh;
    y += (n - E->hiss[ch]) * E->g_hiss;
    float m = white(), *a = E->wind[ch];
    a[0] += (m - a[0]) * 0.32f;
    a[1] += (a[0] - a[1]) * 0.06f;
    y += (a[0] - a[1]) * E->g_wind;
    *(ch ? r : l) += y;
  }
  float tone = 0;
  static const float PART[3] = { 1, 2.01f, 1.37f }, AMP[3] = { 0.6f, 0.26f, 0.14f };
  for (int i = 0; i < 3; i++) {
    E->ph[i] += E->k_whine * PART[i];
    E->ph[i] -= floorf(E->ph[i]);
    tone += AMP[i] * fm_sinf(E->ph[i] * 6.2831853f);
  }
  tone *= E->g_whine * (1 + 3 * E->turb);
  float z = 0;
  if (E->g_rumble > 1e-4f) {
    E->rumble[0] += (white() - E->rumble[0]) * kr;
    E->rumble[1] += (E->rumble[0] - E->rumble[1]) * kr;
    E->rumble[2] += (E->rumble[1] - E->rumble[2]) * kl; /* no rumble below what a speaker can play */
    z = (E->rumble[1] - E->rumble[2]) * E->g_rumble * 3.2f * wob;
  }
  if (E->g_crack > 1e-4f) {
    /* a pop now and then: a few milliseconds of band-limited noise, each its own strength */
    if ((white() + 1) * 0.5f < E->rate) E->env = 0.35f + 0.65f * fabsf(white());
    E->env *= 0.9925f;
    float b = white() * E->env;
    E->crack[0] += (b - E->crack[0]) * 0.23f;
    E->crack[1] += (E->crack[0] - E->crack[1]) * 0.04f;
    z += (E->crack[0] - E->crack[1]) * E->g_crack * 2.6f;
  }
  *l += tone + z;
  *r += tone + z;
}

/* n stereo frames, n at most MIX_CHUNK */
static void mix_chunk(float *buf, int n) {
  float mus[MIX_CHUNK * 2];
  const float dt = 1.0f / RATE;
  memset(mus, 0, sizeof mus);
  if (enabled && music_on) music_render(mus, n);
  if (enabled) listened += (double)n / RATE;
  bool eng[2];
  for (int e = 0; e < 2; e++) {
    engine_chunk(&engines[e], n);
    eng[e] = engines[e].gain >= 1e-5f || engines[e].tgain > 0;
  }
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
    float el = 0, er = 0;
    for (int e = 0; e < 2; e++) {
      if (!eng[e]) continue;
      float a = 0, b = 0;
      engine_sample(&engines[e], &a, &b);
      el += a * engines[e].gain;
      er += b * engines[e].gain;
    }
    const float mast = enabled ? MASTER : 0;
    out *= mast;
    el = out + el * mast;
    er = out + er * mast;
    if (duck != duck_to) {
      duck += (duck_to - duck) * DUCK_RATE;
      if (fabsf(duck_to - duck) < 1e-4f) duck = duck_to;
    }
    /* before the volume and the limiter, so VOLUME, the children's cap and the ceiling hold as ever; it only lowers */
    float l = (el + mus[2 * i] * duck) * volume, r = (er + mus[2 * i + 1] * duck) * volume;
    /* peak limiter: the gain drops at once to keep the louder channel under the ceiling and recovers slowly */
    float pk = fmaxf(fabsf(l), fabsf(r));
    lim_gain = 1 - (1 - lim_gain) * LIM_RELEASE;
    if (pk * lim_gain > LIM_CEIL) lim_gain = LIM_CEIL / pk;
    buf[2 * i] = l * lim_gain;
    buf[2 * i + 1] = r * lim_gain;
  }
}

void sfx_duck_music(float gain) {
  sfx_lock();
  duck_to = gain < 1 ? gain : 1;
  sfx_unlock();
}

/* The sound thread asks for n stereo frames, as many as its device wants (CoreAudio can ask for 2048 or more). */
static void mix(float *buf, int n, void *ud) {
  (void)ud;
  for (int at = 0; at < n; at += MIX_CHUNK) mix_chunk(buf + 2 * at, n - at < MIX_CHUNK ? n - at : MIX_CHUNK);
}

/* test hook: TL_MIX_DUMP=file.raw with --frames writes the whole mix (effects and music, float stereo, 48 kHz)
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
  const char *dump = getenv("TL_MIX_DUMP");
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
    /* throttle 0.9 is full dry power, the rest the afterburner (game.c) */
    E->tspool = fminf(p->throttle / 0.9f, 1);
    E->tab = p->throttle > 0.9f ? fminf((p->throttle - 0.9f) / 0.1f, 1) : 0;
    E->tspeed = p->speed;
    E->tgain = on ? (n > 1 ? 0.3f : 0.42f) : 0;
  }
  plat_audio_unlock();
}
