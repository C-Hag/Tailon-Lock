/* compose.c - the music editor's rewrite tools ("AI" without a network):
 *
 * Key: Krumhansl-Schmuckler key finding. Each pitch class is weighted by how
 * long it sounds; the key is the rotation of the major or minor profile with
 * the highest correlation.
 *
 * Melody model: a first-order Markov chain over (interval, length) pairs,
 * learned from the lead lines of all the game's themes (the highest note at
 * each onset of the lead instruments). Compose walks it from the note
 * before the range, then shapes each pick: it snaps to the key, prefers a
 * tone of the chord sounding underneath on strong beats, keeps the
 * instrument's range, and pulls back toward the middle after big leaps.
 *
 * The transforms (vary, harmonize, re-key, major/minor, humanize,
 * arpeggiate) work on the selected notes and stay in the detected key. */
#include "compose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float MAJOR[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
static const float MINOR[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
static const int SCALE_MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int SCALE_MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };

static uint32_t rs;
static float rnd(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return (rs >> 8) / 16777216.0f;
}

static float corr(const float *x, const float *p, int rot) {
  float mx = 0, mp = 0;
  for (int i = 0; i < 12; i++) mx += x[i] / 12, mp += p[i] / 12;
  float sxy = 0, sxx = 0, syy = 0;
  for (int i = 0; i < 12; i++) {
    float a = x[(i + rot) % 12] - mx, b = p[i] - mp;
    sxy += a * b;
    sxx += a * a;
    syy += b * b;
  }
  return sxy / sqrtf(fmaxf(1e-9f, sxx * syy));
}

Key compose_key(const Score *s) {
  float pc[12] = { 0 };
  for (int i = 0; i < s->nnotes; i++) pc[(s->notes[i].midi % 12 + 12) % 12] += fminf(s->notes[i].dur, 2);
  Key best = { 0, false };
  float bc = -2;
  for (int r = 0; r < 12; r++)
    for (int m = 0; m < 2; m++) {
      float c = corr(pc, m ? MINOR : MAJOR, r);
      if (c > bc) { bc = c; best.root = r; best.minor = m; }
    }
  return best;
}

const char *compose_key_name(Key k) {
  static char buf[16];
  static const char *const N[12] = { "C", "C#", "D", "EB", "E", "F", "F#", "G", "AB", "A", "BB", "B" };
  snprintf(buf, sizeof buf, "%s %s", N[k.root], k.minor ? "MINOR" : "MAJOR");
  return buf;
}

float compose_step(const Score *s) {
  int hist[60] = { 0 };
  for (int i = 1; i < s->nnotes; i++) {
    float d = s->notes[i].t - s->notes[i - 1].t;
    if (d > 0.05f && d < 0.6f) hist[(int)(d / 0.01f)]++;
  }
  int best = 12;
  for (int i = 5; i < 60; i++)
    if (hist[i] > hist[best]) best = i;
  /* the commonest gap may be an eighth; a sixteenth is what we grid on */
  float step = (best + 0.5f) * 0.01f;
  return step > 0.2f ? step / 2 : step;
}

static const int *scale_of(Key k) { return k.minor ? SCALE_MIN : SCALE_MAJ; }

static bool in_key(Key k, int midi) {
  int pc = ((midi - k.root) % 12 + 12) % 12;
  const int *sc = scale_of(k);
  for (int i = 0; i < 7; i++)
    if (sc[i] == pc) return true;
  return false;
}

static int snap_key(Key k, int midi) {
  if (in_key(k, midi)) return midi;
  return in_key(k, midi + 1) ? midi + 1 : midi - 1;
}

/* Moves a note by whole scale steps in the key. */
static int degree_shift(Key k, int midi, int steps) {
  int m = snap_key(k, midi), dir = steps > 0 ? 1 : -1;
  for (int n = 0; n != steps; n += dir) {
    m += dir;
    while (!in_key(k, m)) m += dir;
  }
  return m;
}

/* Pitch classes sounding at t on instruments other than skip. */
static int chord_at(const Score *s, float t, int skip) {
  int mask = 0;
  for (int i = 0; i < s->nnotes; i++) {
    const SongNote *n = &s->notes[i];
    if (n->t > t + 0.01f) break;
    if (n->inst != skip && n->t + n->dur > t) mask |= 1 << (n->midi % 12);
  }
  return mask;
}

/* ---- the melody model ----------------------------------------------------- */
#define IV 25 /* intervals -12..12 */
#define DV 8  /* lengths 1..8 sixteenths */
#define NS (IV * DV)
static uint16_t chain[NS][NS];
static bool learned;

static int st(int iv, int dv) {
  iv = iv < -12 ? -12 : iv > 12 ? 12 : iv;
  dv = dv < 1 ? 1 : dv > DV ? DV : dv;
  return (iv + 12) * DV + dv - 1;
}

static bool lead_like(const char *n) {
  return strstr(n, "Lead") || !strcmp(n, "lead") || !strcmp(n, "guitar");
}

void compose_learn(const Score *s) {
  float step = compose_step(s);
  for (int in = 0; in < s->ninst; in++) {
    if (!lead_like(s->inst[in].name)) continue;
    int prev = -1, pm = -1;
    float pt = -1;
    for (int i = 0; i < s->nnotes; i++) {
      const SongNote *n = &s->notes[i];
      if (n->inst != in) continue;
      if (pt >= 0 && fabsf(n->t - pt) < 0.01f) continue; /* chord: keep the first (melody) note */
      if (pm >= 0) {
        int dv = (int)lroundf((n->t - pt) / step);
        if (dv >= 1 && dv <= 16) {
          int cur = st(n->midi - pm, dv);
          if (prev >= 0 && chain[prev][cur] < 65000) chain[prev][cur]++;
          prev = cur;
        } else prev = -1;
      }
      pm = n->midi;
      pt = n->t;
    }
  }
  learned = true;
}

static int sample_next(int prev) {
  int total = 0;
  for (int j = 0; j < NS; j++) total += chain[prev][j];
  if (!total) { /* unseen: a step up or down, an eighth long */
    return st(rnd() < 0.5f ? 2 : -2, 2);
  }
  int r = (int)(rnd() * total);
  for (int j = 0; j < NS; j++) {
    r -= chain[prev][j];
    if (r < 0) return j;
  }
  return NS - 1;
}

static void delete_range(Score *s, int inst, float t0, float t1) {
  int w = 0;
  for (int i = 0; i < s->nnotes; i++) {
    const SongNote *n = &s->notes[i];
    if (n->inst == inst && n->t >= t0 - 0.001f && n->t < t1 - 0.001f) continue;
    s->notes[w++] = *n;
  }
  s->nnotes = w;
}

int compose_melody(Score *s, int inst, float t0, float t1, uint32_t seed) {
  if (inst < 0 || t1 <= t0) return 0;
  rs = seed | 1;
  Key k = compose_key(s);
  float step = compose_step(s);
  /* the instrument's range and the note leading in */
  int lo = 127, hi = 0, start = -1;
  float vel = 0.9f;
  for (int i = 0; i < s->nnotes; i++) {
    const SongNote *n = &s->notes[i];
    if (n->inst != inst) continue;
    lo = n->midi < lo ? n->midi : lo;
    hi = n->midi > hi ? n->midi : hi;
    if (n->t < t0) { start = n->midi; vel = n->vel; }
  }
  if (lo > hi) { lo = 60; hi = 84; }
  if (hi - lo < 12) { lo -= 3; hi += 3; }
  int mid = (lo + hi) / 2;
  if (start < 0) start = mid;
  delete_range(s, inst, t0, t1);
  int prev = st(0, 2), m = start, made = 0;
  for (float t = t0; t < t1 - step * 0.5f;) {
    int nx = sample_next(prev);
    int iv = nx / DV - 12, dv = nx % DV + 1;
    int cand = m + iv;
    /* shape: back toward the middle after drifting, inside the range */
    if (cand > hi || (cand > mid + 7 && iv > 0)) cand = m - abs(iv);
    if (cand < lo || (cand < mid - 7 && iv < 0)) cand = m + abs(iv);
    cand = snap_key(k, cand);
    int sixteenth = (int)lroundf((t - t0) / step);
    if (sixteenth % 4 == 0) {
      /* strong beat: a chord tone if one is within a third */
      int chord = chord_at(s, t, inst);
      if (chord && !(chord >> (cand % 12) & 1))
        for (int d = 1; d <= 4; d++) {
          if ((chord >> ((cand + d) % 12) & 1) && in_key(k, cand + d)) { cand += d; break; }
          if ((chord >> (((cand - d) % 12 + 12) % 12) & 1) && in_key(k, cand - d)) { cand -= d; break; }
        }
    }
    float len = dv * step;
    if (t + len > t1) len = t1 - t;
    SongNote *n = synth_add_note(s, t, inst, cand, len * 0.92f, vel * (sixteenth % 4 == 0 ? 1.0f : 0.88f));
    if (n) n->sel = true;
    made++;
    m = cand;
    prev = nx;
    t += dv * step;
  }
  synth_sort(s);
  return made;
}

/* ---- transforms on the selection ----------------------------------------- */
int compose_vary(Score *s, uint32_t seed) {
  rs = seed | 1;
  Key k = compose_key(s);
  float step = compose_step(s);
  int n0 = s->nnotes, changed = 0;
  for (int i = 0; i < n0; i++) {
    SongNote *n = &s->notes[i];
    if (!n->sel) continue;
    float r = rnd();
    if (r < 0.3f && n->dur > step * 1.6f) {
      /* ornament: a quick neighbour note, then back */
      float d = n->dur;
      n->dur = step * 0.9f;
      SongNote orn = *n;
      SongNote *o = synth_add_note(s, n->t + step, n->inst, degree_shift(k, orn.midi, rnd() < 0.5f ? 1 : -1), step * 0.9f, orn.vel * 0.9f);
      if (o) o->sel = true;
      SongNote *b = synth_add_note(s, orn.t + 2 * step, orn.inst, orn.midi, d - 2 * step, orn.vel);
      if (b) b->sel = true;
      n = &s->notes[i]; /* the array may have moved */
      changed++;
    } else if (r < 0.5f) {
      n->midi = (int8_t)degree_shift(k, n->midi, rnd() < 0.5f ? 1 : -1);
      changed++;
    } else if (r < 0.62f && n->dur > step * 1.5f) {
      n->t -= step * 0.5f; /* anticipate: push it ahead of the beat */
      n->dur += step * 0.5f;
      changed++;
    }
  }
  synth_sort(s);
  return changed;
}

int compose_harmonize(Score *s) {
  Key k = compose_key(s);
  int n0 = s->nnotes, made = 0;
  for (int i = 0; i < n0; i++) {
    SongNote n = s->notes[i];
    if (!n.sel) continue;
    int third = degree_shift(k, n.midi, -2), sixth = degree_shift(k, n.midi, -5);
    int chord = chord_at(s, n.t, n.inst);
    int h = chord && !(chord >> (third % 12) & 1) && (chord >> (sixth % 12) & 1) ? sixth : third;
    SongNote *o = synth_add_note(s, n.t, n.inst, h, n.dur, n.vel * 0.8f);
    if (o) o->sel = false;
    made++;
  }
  synth_sort(s);
  return made;
}

int compose_transpose(Score *s, int semis) {
  int c = 0;
  for (int i = 0; i < s->nnotes; i++)
    if (s->notes[i].sel) {
      int m = s->notes[i].midi + semis;
      s->notes[i].midi = (int8_t)(m < 12 ? 12 : m > 120 ? 120 : m);
      c++;
    }
  return c;
}

int compose_mode_swap(Score *s) {
  Key k = compose_key(s);
  int c = 0;
  for (int i = 0; i < s->nnotes; i++) {
    SongNote *n = &s->notes[i];
    if (!n->sel) continue;
    int pc = ((n->midi - k.root) % 12 + 12) % 12;
    /* major <-> minor: the third, sixth and seventh move a semitone */
    if (!k.minor && (pc == 4 || pc == 9 || pc == 11)) { n->midi--; c++; }
    else if (k.minor && (pc == 3 || pc == 8 || pc == 10)) { n->midi++; c++; }
  }
  return c;
}

int compose_humanize(Score *s, uint32_t seed) {
  rs = seed | 1;
  int c = 0;
  for (int i = 0; i < s->nnotes; i++) {
    SongNote *n = &s->notes[i];
    if (!n->sel) continue;
    n->t = fmaxf(0, n->t + (rnd() - 0.5f) * 0.016f);
    n->vel = fminf(1.3f, fmaxf(0.2f, n->vel * (0.9f + rnd() * 0.2f)));
    c++;
  }
  synth_sort(s);
  return c;
}

int compose_arpeggiate(Score *s) {
  /* group the selection by start time; each chord becomes a SID-style arpeggio */
  const float rate = 0.045f;
  int n0 = s->nnotes, made = 0;
  SongNote *copy = malloc(sizeof(SongNote) * (size_t)(n0 ? n0 : 1));
  if (!copy) return 0;
  memcpy(copy, s->notes, sizeof(SongNote) * (size_t)n0);
  bool *used = calloc((size_t)(n0 ? n0 : 1), 1);
  int w = 0;
  for (int i = 0; i < n0; i++)
    if (!copy[i].sel) s->notes[w++] = copy[i];
  s->nnotes = w;
  for (int i = 0; i < n0; i++) {
    if (!copy[i].sel || used[i]) continue;
    int tones[8], nt = 0;
    for (int j = i; j < n0 && copy[j].t - copy[i].t < 0.01f; j++)
      if (copy[j].sel && copy[j].inst == copy[i].inst && nt < 8) { tones[nt++] = copy[j].midi; used[j] = true; }
    for (int a = 1; a < nt; a++)
      for (int b = a; b > 0 && tones[b] < tones[b - 1]; b--) { int x = tones[b]; tones[b] = tones[b - 1]; tones[b - 1] = x; }
    if (nt < 2) { /* a single note: an octave flip */
      tones[1] = tones[0] + 12;
      nt = 2;
    }
    int k = 0;
    for (float t = copy[i].t; t < copy[i].t + copy[i].dur - 0.005f; t += rate, k++) {
      SongNote *o = synth_add_note(s, t, copy[i].inst, tones[k % nt], rate * 0.95f, copy[i].vel * 0.85f);
      if (o) o->sel = true;
      made++;
    }
  }
  free(copy);
  free(used);
  synth_sort(s);
  return made;
}
