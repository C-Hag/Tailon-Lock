/* synth.c - the SID-style synthesizer (plan milestone 8). Every theme is a
 * score of timed notes and drum and effect hits, extracted from the browser
 * game's Web Audio engines, so the arrangements, voicings and grooves are
 * the same; only the sound is new: a pool of voices in the spirit of the
 * C64's SID chip (pulse with width sweeps, sawtooth, triangle, noise clocked
 * at a pitch, ADSR, a resonant state-variable filter, delayed vibrato),
 * plus a small stereo room so it does not sound bone dry. */
#include "synth.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/json.h"

#define TAU 6.2831853f

/* ---- patches: the defaults for each instrument ---------------------------- */
typedef struct { const char *name; Patch p; } NamedPatch;
static const NamedPatch PATCHES[] = {
  /*                     wave     filt  level  pan    a      d     s     r     pw    pwd   pwr   vdel  vrate vdep  cut0  cut1  cutT  q     sw sT oct */
  { "synthLead",  { W_PULSE, F_LP, 0.15f, 0.10f, 0.006f, 0.30f, 0.70f, 0.12f, 0.30f, 0.15f, 0.9f, 0.16f, 5.6f, 0.30f, 5200, 3000, 0.25f, 0.2f, 0, 0, 0.0f } },
  { "sidLead",    { W_PULSE, F_LP, 0.13f, -0.15f, 0.004f, 0.20f, 0.65f, 0.10f, 0.25f, 0.18f, 1.3f, 0.12f, 6.0f, 0.40f, 6000, 3600, 0.20f, 0.3f, 0, 0, 0.0f } },
  { "pulseLead",  { W_PULSE, F_LP, 0.16f, 0.00f, 0.005f, 0.25f, 0.70f, 0.08f, 0.30f, 0.16f, 0.9f, 0.16f, 5.6f, 0.25f, 5000, 3000, 0.25f, 0.2f, 0, 0, 0.0f } },
  { "lead",       { W_PULSE, F_LP, 0.15f, 0.00f, 0.005f, 0.30f, 0.70f, 0.10f, 0.30f, 0.15f, 0.8f, 0.18f, 5.4f, 0.30f, 5200, 3000, 0.25f, 0.2f, 0, 0, 0.0f } },
  { "brassLead",  { W_SAW,   F_LP, 0.12f, 0.00f, 0.030f, 0.40f, 0.75f, 0.12f, 0.50f, 0.00f, 0.0f, 0.22f, 5.2f, 0.20f, 1200, 3400, 0.08f, 0.4f, 0, 0, 0.0f } },
  { "guitar",     { W_PULSE, F_LP, 0.12f, -0.25f, 0.003f, 0.50f, 0.55f, 0.08f, 0.18f, 0.05f, 2.0f, 0.25f, 5.0f, 0.15f, 3600, 2200, 0.30f, 0.5f, 0, 0, 0.3f } },
  { "powerChord", { W_PULSE, F_LP, 0.07f, 0.25f, 0.002f, 0.25f, 0.40f, 0.06f, 0.42f, 0.04f, 3.0f, 9.0f,  0.0f, 0.00f, 2400, 1200, 0.15f, 0.6f, 0, 0, 0.0f } },
  { "piano",      { W_TRI,   F_NONE, 0.12f, 0.00f, 0.002f, 0.90f, 0.00f, 0.25f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 0, 0, 0, 0, 0, 0, 0.25f } },
  { "bellChord",  { W_TRI,   F_NONE, 0.07f, 0.30f, 0.002f, 1.80f, 0.00f, 0.40f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 0, 0, 0, 0, 0, 0, 0.4f } },
  { "bell",       { W_TRI,   F_NONE, 0.07f, -0.30f, 0.002f, 1.80f, 0.00f, 0.40f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 0, 0, 0, 0, 0, 0, 0.4f } },
  { "brassChord", { W_SAW,   F_LP, 0.045f, 0.00f, 0.120f, 0.40f, 0.85f, 0.25f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 700, 1700, 0.18f, 0.3f, 0, 0, 0.0f } },
  { "pad",        { W_SAW,   F_LP, 0.040f, 0.00f, 0.500f, 0.50f, 0.90f, 0.60f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 600, 1300, 0.60f, 0.2f, 0, 0, 0.0f } },
  { "choirChord", { W_TRI,   F_NONE, 0.06f, 0.00f, 0.450f, 0.50f, 0.90f, 0.30f, 0.50f, 0.00f, 0.0f, 0.10f, 4.8f, 0.12f, 0, 0, 0, 0, 0, 0, 0.0f } },
  { "bigBass",    { W_PULSE, F_LP, 0.20f, 0.00f, 0.003f, 0.30f, 0.60f, 0.05f, 0.32f, 0.04f, 0.5f, 9.0f, 0.0f, 0.00f, 2200, 600, 0.12f, 0.5f, 0, 0, 0.0f } },
  { "aceBass",    { W_PULSE, F_LP, 0.20f, 0.00f, 0.002f, 0.15f, 0.50f, 0.03f, 0.12f, 0.08f, 0.4f, 9.0f, 0.0f, 0.00f, 2600, 1100, 0.10f, 0.4f, 0, 0, 0.0f } },
  { "seqBass",    { W_PULSE, F_LP, 0.17f, 0.00f, 0.002f, 0.12f, 0.50f, 0.03f, 0.25f, 0.06f, 0.6f, 9.0f, 0.0f, 0.00f, 2000, 900, 0.10f, 0.5f, 0, 0, 0.0f } },
  { "bass",       { W_SAW,   F_LP, 0.17f, 0.00f, 0.004f, 0.25f, 0.25f, 0.04f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 2200, 380, 0.11f, 0.6f, 0, 0, 0.0f } },
  /* the synthwave themes */
  { "funkBass",   { W_PULSE, F_LP, 0.22f, 0.00f, 0.002f, 0.18f, 0.35f, 0.04f, 0.25f, 0.05f, 0.5f, 9.0f, 0.0f, 0.00f, 3400, 700, 0.09f, 0.7f, 0, 0, 0.0f } },
  { "synthBrass", { W_SAW,   F_LP, 0.13f, 0.00f, 0.010f, 0.30f, 0.60f, 0.12f, 0.50f, 0.00f, 0.0f, 0.25f, 5.2f, 0.10f, 900, 4200, 0.06f, 0.5f, 0, 0, 0.35f } },
  { "gatedPad",   { W_SAW,   F_LP, 0.08f, 0.00f, 0.005f, 0.20f, 0.80f, 0.05f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 2600, 1800, 0.20f, 0.3f, 0, 0, 0.3f } },
  { "lead80s",    { W_SAW,   F_LP, 0.20f, 0.05f, 0.010f, 0.40f, 0.75f, 0.20f, 0.50f, 0.00f, 0.0f, 0.22f, 5.5f, 0.15f, 3200, 2400, 0.30f, 0.3f, 0, 0, 0.12f } },
  { "wahGuitar",  { W_PULSE, F_BP, 0.14f, -0.20f, 0.002f, 0.60f, 0.70f, 0.12f, 0.30f, 0.08f, 3.0f, 0.20f, 6.2f, 0.35f, 500, 2400, 0.14f, 1.6f, 0, 0, 0.45f } },
  { "boogiePiano",{ W_PULSE, F_LP, 0.14f, 0.15f, 0.002f, 0.70f, 0.00f, 0.20f, 0.50f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 6000, 1600, 0.50f, 0.2f, 0, 0, 0.35f } },
  { "warmLead",   { W_PULSE, F_LP, 0.15f, -0.10f, 0.012f, 0.35f, 0.75f, 0.18f, 0.45f, 0.08f, 0.6f, 0.30f, 5.0f, 0.12f, 2400, 1700, 0.25f, 0.3f, 0, 0, 0.0f } },
  { "softArp",    { W_PULSE, F_LP, 0.14f, 0.20f, 0.003f, 0.25f, 0.30f, 0.06f, 0.40f, 0.05f, 0.5f, 9.0f, 0.0f, 0.00f, 2200, 1500, 0.10f, 0.3f, 0, 0, 0.0f } },
  { "whistle",    { W_SINE,  F_NONE, 0.12f, 0.10f, 0.030f, 0.10f, 1.00f, 0.08f, 0.50f, 0.00f, 0.0f, 0.30f, 9.0f, 0.06f, 0, 0, 0, 0, 0, 0, 0.0f } },
  { "clav",       { W_PULSE, F_BP, 0.20f, 0.25f, 0.001f, 0.14f, 0.00f, 0.05f, 0.18f, 0.00f, 0.0f, 9.0f, 0.0f, 0.00f, 2200, 2200, 0.10f, 1.0f, 0, 0, 0.0f } },
  { "arp",        { W_PULSE, F_NONE, 0.06f, 0.20f, 0.003f, 0.80f, 0.45f, 0.02f, 0.50f, 0.10f, 0.7f, 9.0f, 0.0f, 0.00f, 0, 0, 0, 0, 0, 0, 0.0f } },
};

bool synth_default_patch(const char *name, Patch *out) {
  for (size_t i = 0; i < sizeof PATCHES / sizeof PATCHES[0]; i++)
    if (!strcmp(PATCHES[i].name, name)) {
      *out = PATCHES[i].p;
      return true;
    }
  return false;
}

/* ---- songs ------------------------------------------------------------- */
static int note_cmp(const void *a, const void *b) {
  float x = ((const SongNote *)a)->t, y = ((const SongNote *)b)->t;
  return x < y ? -1 : x > y;
}
static int hit_cmp(const void *a, const void *b) {
  float x = ((const SongHit *)a)->t, y = ((const SongHit *)b)->t;
  return x < y ? -1 : x > y;
}

void synth_sort(Score *s) {
  qsort(s->notes, (size_t)s->nnotes, sizeof(SongNote), note_cmp);
  qsort(s->hits, (size_t)s->nhits, sizeof(SongHit), hit_cmp);
}

static float numi(const Json *a, int i, float def) {
  const Json *v = json_at(a, i);
  return json_is_number(v) ? (float)v->num : def;
}

int synth_inst(Score *s, const char *name) {
  for (int i = 0; i < s->ninst; i++)
    if (!strcmp(s->inst[i].name, name)) return i;
  if (s->ninst >= SONG_MAX_INST) return -1;
  SongInst *in = &s->inst[s->ninst];
  memset(in, 0, sizeof *in);
  snprintf(in->name, sizeof in->name, "%s", name);
  if (!synth_default_patch(name, &in->p)) return -1;
  return s->ninst++;
}

SongNote *synth_add_note(Score *s, float t, int inst, int midi, float dur, float vel) {
  if (s->nnotes == s->capn) {
    int cap = s->capn ? s->capn * 2 : 256;
    SongNote *n = realloc(s->notes, sizeof(SongNote) * (size_t)cap);
    if (!n) return NULL;
    s->notes = n;
    s->capn = cap;
  }
  SongNote *n = &s->notes[s->nnotes++];
  n->t = t;
  n->dur = dur;
  n->vel = vel;
  n->inst = (int16_t)inst;
  n->midi = (int8_t)midi;
  n->bend = 0;
  n->sel = false;
  return n;
}

/* Patch fields by name, for the score files and the editor. */
#define PATCH_FIELDS(X) X(level) X(pan) X(a) X(d) X(s) X(r) X(pw) X(pw_depth) X(pw_rate) X(vib_delay) X(vib_rate) \
  X(vib_depth) X(cut0) X(cut1) X(cut_t) X(q) X(octave)

Score *synth_load(const char *key) {
  char rel[64];
  snprintf(rel, sizeof rel, "music/%s.json", key);
  Json *j = assets_json(rel);
  if (!j) return NULL;
  const Json *notes = json_get(j, "notes"), *events = json_get(j, "events");
  Score *s = calloc(1, sizeof *s);
  snprintf(s->key, sizeof s->key, "%s", key);
  s->hits = calloc((size_t)json_count(events) + 1, sizeof(SongHit));
  s->caph = json_count(events) + 1;
  const Json *e;
  /* pitched notes: [t, instrument, midi, dur, vel, bend?] */
  JSON_FOR(e, notes) {
    const char *name = json_str(json_at(e, 1));
    int in = name ? synth_inst(s, name) : -1;
    if (in < 0) continue;
    SongNote *n = synth_add_note(s, numi(e, 0, 0), in, (int)numi(e, 2, 60), numi(e, 3, 0.2f), numi(e, 4, 1));
    if (n) n->bend = (int8_t)numi(e, 5, 0);
  }
  /* drums and effects: [t, function, vel, ...args] */
  JSON_FOR(e, events) {
    const char *fn = json_str(json_at(e, 1));
    if (!fn) continue;
    SongHit v = { 0 };
    v.t = numi(e, 0, 0);
    v.vel = numi(e, 2, 1);
    if (!strcmp(fn, "drum") || !strcmp(fn, "hit")) {
      const char *c = json_str(json_at(e, 3));
      if (!c) continue;
      v.effect = E_DRUM;
      snprintf(v.code, sizeof v.code, "%s", c);
      v.a1 = !strcmp(fn, "hit") ? 0.8f : numi(e, 4, 1); /* hit strength */
    } else if (!strcmp(fn, "jet")) {
      v.effect = E_JET;
      v.a1 = numi(e, 3, 6);
      v.a2 = numi(e, 4, 1);
      v.a3 = numi(e, 5, 0.58f);
    } else if (!strcmp(fn, "riser")) {
      v.effect = E_RISER;
      v.a1 = numi(e, 3, 4);
      v.a2 = numi(e, 4, 1);
    } else if (!strcmp(fn, "seaWave")) {
      v.effect = E_WAVE;
      v.a1 = numi(e, 3, 1);
    } else if (!strcmp(fn, "sonar")) {
      v.effect = E_SONAR;
    } else if (!strcmp(fn, "swell")) {
      v.effect = E_SWELL;
    } else continue;
    s->hits[s->nhits++] = v;
  }
  /* patches someone tuned in the editor */
  const Json *pt = json_get(j, "patches");
  JSON_FOR(e, pt) {
    int in = synth_inst(s, e->name);
    if (in < 0) continue;
    Patch *p = &s->inst[in].p;
    const Json *w = json_get(e, "wave"), *f = json_get(e, "filter");
    if (json_is_number(w)) p->wave = (uint8_t)json_int(w, 0);
    if (json_is_number(f)) p->filter = (uint8_t)json_int(f, 0);
#define READ(fld) { const Json *x = json_get(e, #fld); if (json_is_number(x)) p->fld = (float)x->num; }
    PATCH_FIELDS(READ)
#undef READ
  }
  synth_sort(s);
  float length = (float)json_num(json_get(j, "length"));
  s->end = length > 3.5f ? length - 3 : length;
  s->loop = (float)json_num(json_get(j, "loop"));
  if (!(s->loop >= 0 && s->loop < s->end)) s->loop = 0;
  json_free(j);
  SDL_Log("synth: %s, %d notes, %d hits, %.1f s, loop at %.1f s", key, s->nnotes, s->nhits, s->end, s->loop);
  return s;
}

void synth_free(Score *s) {
  if (!s) return;
  free(s->notes);
  free(s->hits);
  free(s);
}

static const char *const HIT_FN[] = { "drum", "jet", "riser", "seaWave", "sonar", "swell" };

static double r4(double v) { return round(v * 10000) / 10000; }

bool synth_save(const Score *s, const char *path) {
  JsonWriter w = { 0 };
  json_w_open_object(&w, NULL);
  json_w_string(&w, "key", s->key);
  json_w_number(&w, "length", r4(s->end + 3));
  json_w_number(&w, "loop", r4(s->loop));
  json_w_open_array(&w, "events");
  for (int i = 0; i < s->nhits; i++) {
    const SongHit *h = &s->hits[i];
    json_w_open_array(&w, NULL);
    json_w_number(&w, NULL, r4(h->t));
    json_w_string(&w, NULL, HIT_FN[h->effect]);
    json_w_number(&w, NULL, r4(h->vel));
    if (h->effect == E_DRUM) {
      json_w_string(&w, NULL, h->code);
      json_w_number(&w, NULL, r4(h->a1));
    } else if (h->effect == E_JET || h->effect == E_RISER || h->effect == E_WAVE) {
      json_w_number(&w, NULL, r4(h->a1));
      if (h->effect != E_WAVE) json_w_number(&w, NULL, r4(h->a2));
      if (h->effect == E_JET) json_w_number(&w, NULL, r4(h->a3));
    }
    json_w_close(&w);
  }
  json_w_close(&w);
  json_w_open_array(&w, "notes");
  for (int i = 0; i < s->nnotes; i++) {
    const SongNote *n = &s->notes[i];
    json_w_open_array(&w, NULL);
    json_w_number(&w, NULL, r4(n->t));
    json_w_string(&w, NULL, s->inst[n->inst].name);
    json_w_number(&w, NULL, n->midi);
    json_w_number(&w, NULL, r4(n->dur));
    json_w_number(&w, NULL, r4(n->vel));
    if (n->bend) json_w_number(&w, NULL, n->bend);
    json_w_close(&w);
  }
  json_w_close(&w);
  json_w_open_object(&w, "patches");
  for (int i = 0; i < s->ninst; i++) {
    Patch d;
    const Patch *p = &s->inst[i].p;
    if (synth_default_patch(s->inst[i].name, &d) && !memcmp(&d, p, sizeof d)) continue;
    json_w_open_object(&w, s->inst[i].name);
    json_w_number(&w, "wave", p->wave);
    json_w_number(&w, "filter", p->filter);
#define WRITE(fld) json_w_number(&w, #fld, r4(p->fld));
    PATCH_FIELDS(WRITE)
#undef WRITE
    json_w_close(&w);
  }
  json_w_close(&w);
  json_w_close(&w);
  char *text = json_w_finish(&w);
  if (!text) return false;
  SDL_IOStream *f = SDL_IOFromFile(path, "wb");
  bool ok = f && SDL_WriteIO(f, text, strlen(text)) == strlen(text);
  if (f) SDL_CloseIO(f);
  free(text);
  SDL_Log("saved %s: %s", path, ok ? "ok" : SDL_GetError());
  return ok;
}

/* ---- voices ----------------------------------------------------------- */
typedef struct {
  bool   on;
  uint8_t wave, filter;
  float  age, gate;            /* seconds since start, note length */
  float  freq, f_to, sweep_t;  /* Hz; pitch sweep target ratio over seconds */
  float  phase, nphase;
  uint32_t lfsr;
  float  noise;
  float  level, pan;
  float  a, d, s, r;
  float  pw, pw_depth, pw_rate;
  float  vib_delay, vib_rate, vib_depth;
  float  cut0, cut1, cut_t, cut_mid, cut_pk, q; /* cut_pk > 0: a 3-point path for effects */
  float  lp, bp;               /* filter state (the two integrators) */
  float  amp_pk;               /* > 0: an effect swell peaking at amp_pk of the gate */
  float  octave;
} Voice;

#define NVOICE 64
static Voice V[NVOICE];
static Score *cur;
static int    pos_n, pos_h;    /* next note and next hit */
static double t_song, t_total; /* seconds into the score, and since start */
static bool   playing, wrapped;
static uint32_t rnd = 0x9e3779b9u;

static float frand(void) {
  rnd ^= rnd << 13;
  rnd ^= rnd >> 17;
  rnd ^= rnd << 5;
  return (rnd >> 8) / 16777216.0f;
}

static Voice *alloc_voice(void) {
  Voice *best = &V[0];
  float score = -1;
  for (int i = 0; i < NVOICE; i++) {
    if (!V[i].on) return &V[i];
    /* steal the one deepest into its release, else the oldest */
    float sc = V[i].age - V[i].gate + (V[i].age > V[i].gate ? 100 : 0);
    if (sc > score) { score = sc; best = &V[i]; }
  }
  return best;
}

static Voice *voice(uint8_t wave, float freq, float gate, float level, float pan) {
  Voice *v = alloc_voice();
  memset(v, 0, sizeof *v);
  v->on = true;
  v->wave = wave;
  v->freq = freq;
  v->f_to = 1;
  v->gate = gate;
  v->level = level;
  v->pan = pan;
  v->a = 0.002f;
  v->d = 0.1f;
  v->s = 1;
  v->r = 0.05f;
  v->pw = 0.5f;
  v->vib_delay = 9;
  v->q = 0.3f;
  v->lfsr = 0x7ffff8u ^ (rnd & 0xffff);
  return v;
}

static void play_note(const Score *sc, const SongNote *e) {
  const SongInst *in = &sc->inst[e->inst];
  bool solo = false;
  for (int i = 0; i < sc->ninst; i++) solo |= sc->inst[i].solo;
  if (in->mute || (solo && !in->solo)) return;
  const Patch *p = &in->p;
  float f = 440.0f * powf(2.0f, (e->midi - 69) / 12.0f);
  for (int layer = 0; layer < (p->octave > 0 ? 2 : 1); layer++) {
    Voice *v = voice(p->wave, layer ? f * 2 : f, fmaxf(0.01f, e->dur), p->level * e->vel * (layer ? p->octave : 1), p->pan);
    v->filter = p->filter;
    v->a = p->a;
    v->d = p->d;
    v->s = p->s;
    v->r = p->r;
    v->pw = p->pw;
    v->pw_depth = p->pw_depth;
    v->pw_rate = p->pw_rate;
    v->vib_delay = p->vib_delay;
    v->vib_rate = p->vib_rate;
    v->vib_depth = p->vib_depth;
    v->cut0 = p->cut0;
    v->cut1 = p->cut1;
    v->cut_t = p->cut_t;
    v->q = p->q;
    v->phase = frand();
    if (e->bend) { /* a guitar bend: from below up to the note */
      float k = powf(2.0f, e->bend / 12.0f);
      v->freq /= k;
      v->f_to = k;
      v->sweep_t = fminf(0.16f, e->dur * 0.4f);
    }
  }
}

/* A decaying noise burst through a filter. */
static void noise_hit(float len, uint8_t filter, float cut, float q, float level, float rate) {
  Voice *v = voice(W_NOISE, rate, 0.001f, level, (frand() - 0.5f) * 0.3f);
  v->filter = filter;
  v->cut0 = v->cut1 = cut;
  v->q = q;
  v->a = 0.0005f;
  v->d = len;
  v->s = 0;
  v->r = len;
}

/* A pitched blip that sweeps down (SID drums: triangle or pulse). */
static void tone_hit(uint8_t wave, float f0, float f1, float sweep, float len, float level) {
  Voice *v = voice(wave, f0, 0.001f, level, 0);
  v->f_to = f1 / f0;
  v->sweep_t = sweep;
  v->a = 0.0005f;
  v->d = len;
  v->s = 0;
  v->r = len;
}

/* A big drum through gated reverb: a pitched body and a noise wash that
 * hold for the gate and then stop dead. */
static void gated_hit(float f0, float f1, float body, float cut, float gate, float level) {
  Voice *v = voice(W_TRI, f0, gate, body * level, (frand() - 0.5f) * 0.4f);
  v->f_to = f1 / f0;
  v->sweep_t = gate;
  v->a = 0.0005f;
  v->d = gate * 0.8f;
  v->s = 0.35f;
  v->r = 0.012f;
  v = voice(W_NOISE, 16000, gate, 0.45f * level, (frand() - 0.5f) * 0.6f);
  v->filter = F_BP;
  v->cut0 = cut * 1.6f;
  v->cut1 = cut;
  v->cut_t = gate;
  v->q = 0.4f;
  v->a = 0.001f;
  v->d = gate;
  v->s = 0.55f;
  v->r = 0.012f;
}

static void drum(const SongHit *e) {
  const char *d = e->code;
  float v = e->a1 * e->vel;
  switch (d[0]) {
  case 'k': tone_hit(W_TRI, 160, 46, 0.06f, 0.45f, 0.75f * v); noise_hit(0.012f, F_HP, 2500, 0.3f, 0.25f * v, 20000); break;
  case 's': noise_hit(0.22f, F_BP, 1900, 0.6f, 0.55f * v, 18000); tone_hit(W_TRI, 210, 175, 0.03f, 0.12f, 0.35f * v); noise_hit(0.13f, F_HP, 5000, 0.3f, 0.2f * v, 20000); break;
  case 'h': noise_hit(0.035f, F_HP, 8000, 0.3f, 0.13f * v, 22000); break;
  case 'G': { /* gated tom, G0 high to G3 low */
    float f = 190.0f - atoi(d + 1) * 30;
    gated_hit(f, f * 0.62f, 0.9f, 1100.0f - atoi(d + 1) * 150, 0.3f, 0.75f * v);
    break;
  }
  case 'S': /* gated snare */
    gated_hit(220, 180, 0.4f, 1900, 0.28f, 0.8f * v);
    noise_hit(0.05f, F_HP, 5000, 0.3f, 0.25f * v, 20000);
    break;
  case 'o': noise_hit(0.28f, F_HP, 7500, 0.3f, 0.1f * v, 22000); break;
  case 'c': noise_hit(0.16f, F_BP, 1250, 1.0f, 0.4f * v, 16000); break;
  case 'g': noise_hit(0.06f, F_BP, 2600, 0.7f, 0.12f * v, 16000); break;
  case 'X': noise_hit(2.0f, F_HP, 5000, 0.3f, 0.2f * v, 22000); tone_hit(W_TRI, 100, 45, 0.05f, 0.5f, 0.45f * v); break;
  case 'P': tone_hit(W_TRI, 100, 68, 0.1f, 1.4f, 0.8f * v); noise_hit(0.08f, F_LP, 700, 0.3f, 0.3f * v, 9000); break;
  case 'T': {
    int k = atoi(d + 1);
    tone_hit(W_TRI, 230.0f - k * 30, 90.0f - k * 8, 0.14f, 0.4f, 0.6f * v);
    noise_hit(0.03f, F_BP, 1200, 0.5f, 0.15f * v, 12000);
    break;
  }
  case 'r': noise_hit(0.1f, F_BP, 1800, 0.6f, (0.06f + atoi(d + 1) * 0.035f) * 1.4f * v, 18000); break;
  default: break;
  }
}

static void effect(const SongHit *e) {
  Voice *v;
  switch (e->effect) {
  case E_DRUM: drum(e); break;
  case E_JET: /* roar that swells to the pass and dies away, plus a low rumble */
    v = voice(W_NOISE, 20000, e->a1, 0.5f * e->a2 * e->vel, -0.6f);
    v->filter = F_BP;
    v->cut0 = 700; v->cut_mid = 2600; v->cut1 = 380; v->cut_pk = e->a3; v->cut_t = e->a1; v->q = 0.7f;
    v->amp_pk = e->a3; v->r = 0.05f;
    v = voice(W_NOISE, 6000, e->a1, 0.6f * e->a2 * e->vel, 0.2f);
    v->filter = F_LP;
    v->cut0 = v->cut1 = v->cut_mid = 180; v->cut_t = 1; v->q = 0.3f;
    v->amp_pk = fminf(0.98f, e->a3 + 0.05f); v->r = 0.05f;
    v = voice(W_SAW, 2550, e->a1, 0.025f * e->a2 * e->vel, 0.6f);
    v->f_to = 0.7f; v->sweep_t = e->a1; v->amp_pk = e->a3; v->r = 0.05f;
    break;
  case E_RISER:
    v = voice(W_NOISE, 20000, e->a1, 0.2f * e->a2 * e->vel, 0);
    v->filter = F_BP;
    v->cut0 = 300; v->cut1 = 7000; v->cut_t = e->a1; v->q = 1.5f;
    v->amp_pk = 0.99f; v->r = 0.05f;
    break;
  case E_WAVE: {
    float L = 5.5f + frand() * 1.5f;
    v = voice(W_NOISE, 16000, L, 0.3f * e->a1 * e->vel, frand() * 1.2f - 0.6f);
    v->filter = F_LP;
    v->cut0 = 250; v->cut_mid = 2200; v->cut1 = 350; v->cut_pk = 0.4f; v->cut_t = L; v->q = 0.2f;
    v->amp_pk = 0.4f; v->r = 0.1f;
    break;
  }
  case E_SONAR:
    v = voice(W_SINE, 1480, 0.01f, 0.12f * e->vel, 0.3f);
    v->a = 0.004f; v->d = 1.6f; v->s = 0; v->r = 1.6f;
    break;
  case E_SWELL:
    v = voice(W_NOISE, 20000, 4, 0.09f * e->vel, 0);
    v->filter = F_HP;
    v->cut0 = 9000; v->cut1 = 1500; v->cut_t = 4; v->q = 0.3f;
    v->amp_pk = 0.99f; v->r = 0.05f;
    break;
  }
}

/* ---- the room: two short comb reverbs, one per side -------------------- */
#define CMB1 1557
#define CMB2 1617
#define CMB3 1491
#define CMB4 1422
static float cb[4][CMB2];
static int   ci[4];
static const int CLEN[4] = { CMB1, CMB2, CMB3, CMB4 };
static float cfilt[4];

static float comb(int k, float x) {
  float y = cb[k][ci[k]];
  cfilt[k] = y * 0.6f + cfilt[k] * 0.4f;
  cb[k][ci[k]] = x + cfilt[k] * 0.78f;
  if (++ci[k] >= CLEN[k]) ci[k] = 0;
  return y;
}

/* ---- rendering -------------------------------------------------------- */
void synth_start(Score *s) {
  memset(V, 0, sizeof V);
  memset(cb, 0, sizeof cb);
  cur = s;
  pos_n = pos_h = 0;
  t_song = t_total = 0;
  wrapped = false;
  playing = s != NULL;
}

void synth_stop(void) {
  playing = false;
  cur = NULL;
  for (int i = 0; i < NVOICE; i++)
    if (V[i].on && V[i].age < V[i].gate) V[i].gate = V[i].age;
}

bool synth_playing(void) { return playing; }
bool synth_wrapped(void) { return wrapped; }
double synth_time(void) { return t_total; }
double synth_song_time(void) { return t_song; }

void synth_seek(double t) {
  if (!cur) return;
  t_song = t;
  pos_n = pos_h = 0;
  while (pos_n < cur->nnotes && cur->notes[pos_n].t < t_song) pos_n++;
  while (pos_h < cur->nhits && cur->hits[pos_h].t < t_song) pos_h++;
  for (int i = 0; i < NVOICE; i++)
    if (V[i].on && V[i].age < V[i].gate) V[i].gate = V[i].age;
}

static float env(const Voice *v) {
  float t = v->age;
  if (v->amp_pk > 0) { /* effect swell: rise to the peak, fall to the end of the gate */
    float pk = v->gate * v->amp_pk;
    if (t < pk) return 0.05f + 0.95f * (t / pk) * (t / pk);
    if (t < v->gate) {
      float u = (t - pk) / fmaxf(0.001f, v->gate - pk);
      return (1 - u) * (1 - u);
    }
    return 0;
  }
  float e;
  if (t < v->a) e = t / v->a;
  else {
    float u = t - v->a;
    e = v->s + (1 - v->s) * expf(-u / fmaxf(0.001f, v->d * 0.35f));
  }
  if (t > v->gate) {
    /* release from the level at the gate */
    float g = v->gate, eg;
    if (g < v->a) eg = g / v->a;
    else eg = v->s + (1 - v->s) * expf(-(g - v->a) / fmaxf(0.001f, v->d * 0.35f));
    e = eg * expf(-(t - g) / fmaxf(0.001f, v->r * 0.35f));
  }
  return e;
}

static float cutoff(const Voice *v) {
  if (v->cut_pk > 0) {
    float pk = v->cut_t * v->cut_pk, t = v->age;
    if (t < pk) return v->cut0 * powf(v->cut_mid / v->cut0, t / pk);
    float u = fminf(1, (t - pk) / fmaxf(0.001f, v->cut_t - pk));
    return v->cut_mid * powf(v->cut1 / v->cut_mid, u);
  }
  if (v->cut_t <= 0) return v->cut1;
  float u = fminf(1, v->age / v->cut_t);
  return v->cut0 * powf(v->cut1 / v->cut0, u);
}

/* Renders in blocks of BLK frames: events, envelopes, filter cutoffs and
 * pitch are worked out once per block, the waveforms per sample. */
#define BLK 32
void synth_render(float *out, int n, float gain) {
  const float dt = 1.0f / SYNTH_RATE;
  float L[BLK], R[BLK];
  for (int b0 = 0; b0 < n; b0 += BLK) {
    int m = n - b0 < BLK ? n - b0 : BLK;
    if (playing && cur) {
      while (pos_n < cur->nnotes && cur->notes[pos_n].t <= t_song) play_note(cur, &cur->notes[pos_n++]);
      while (pos_h < cur->nhits && cur->hits[pos_h].t <= t_song) {
        const SongHit *h = &cur->hits[pos_h++];
        if (!cur->mute_drums) effect(h);
      }
      t_song += m * dt;
      t_total += m * dt;
      if (t_song >= cur->end) {
        wrapped = true;
        double to = cur->loop;
        synth_seek(to);
      }
    }
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    for (int k = 0; k < NVOICE; k++) {
      Voice *v = &V[k];
      if (!v->on) continue;
      float e0 = env(v), e1;
      v->age += m * dt;
      e1 = env(v);
      if (v->age > v->gate && e0 < 0.0005f && e1 < 0.0005f) {
        v->on = false;
        continue;
      }
      float f = v->freq;
      if (v->sweep_t > 0) f *= powf(v->f_to, fminf(1, v->age / v->sweep_t));
      if (v->vib_depth > 0 && v->age > v->vib_delay) {
        float ramp = fminf(1, (v->age - v->vib_delay) / 0.25f);
        f *= powf(2.0f, v->vib_depth * ramp * sinf(TAU * v->vib_rate * v->age) / 12);
      }
      float inc = f * dt;
      float w = v->pw + v->pw_depth * sinf(TAU * v->pw_rate * v->age);
      w = w < 0.05f ? 0.05f : w > 0.95f ? 0.95f : w;
      /* trapezoidal state-variable filter (stable at any cutoff) */
      float a1 = 0, a2 = 0, a3 = 0, k2 = 0;
      if (v->filter != F_NONE) {
        float fc = fminf(cutoff(v), SYNTH_RATE * 0.45f);
        float gg = tanf(3.14159265f * fc / SYNTH_RATE);
        k2 = 2 - 2 * fminf(0.95f, v->q * 0.5f);
        a1 = 1 / (1 + gg * (gg + k2));
        a2 = gg * a1;
        a3 = gg * a2;
      }
      float de = (e1 - e0) / m, e = e0;
      float gl = v->level * (0.5f - 0.5f * v->pan) * 1.4f, gr = v->level * (0.5f + 0.5f * v->pan) * 1.4f;
      for (int i = 0; i < m; i++, e += de) {
        float x;
        if (v->wave == W_NOISE) {
          /* SID-style noise: a 23-bit LFSR clocked at the voice's rate */
          v->nphase += inc;
          while (v->nphase >= 1) {
            v->nphase -= 1;
            uint32_t bit = ((v->lfsr >> 22) ^ (v->lfsr >> 17)) & 1;
            v->lfsr = ((v->lfsr << 1) | bit) & 0x7fffff;
            v->noise = (float)(v->lfsr & 0xff) / 127.5f - 1;
          }
          x = v->noise;
        } else {
          v->phase += inc;
          if (v->phase >= 1) v->phase -= floorf(v->phase);
          float p = v->phase;
          switch (v->wave) {
          case W_PULSE: x = p < w ? 0.7f : -0.7f; break;
          case W_SAW:   x = 2 * p - 1; break;
          case W_TRI:   x = p < 0.5f ? 4 * p - 1 : 3 - 4 * p; break;
          default:      x = sinf(TAU * p); break;
          }
        }
        if (v->filter != F_NONE) {
          float v3 = x - v->lp, v1 = a1 * v->bp + a2 * v3, v2 = v->lp + a2 * v->bp + a3 * v3;
          v->bp = 2 * v1 - v->bp;
          v->lp = 2 * v2 - v->lp;
          x = v->filter == F_LP ? v2 : v->filter == F_HP ? x - k2 * v1 - v2 : v1;
        }
        x *= e;
        L[i] += x * gl;
        R[i] += x * gr;
      }
    }
    for (int i = 0; i < m; i++) {
      float wl = comb(0, L[i]) + comb(1, R[i]), wr = comb(2, R[i]) + comb(3, L[i]);
      out[2 * (b0 + i)] += (L[i] + wl * 0.09f) * gain;
      out[2 * (b0 + i) + 1] += (R[i] + wr * 0.09f) * gain;
    }
  }
}
