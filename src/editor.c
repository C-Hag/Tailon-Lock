/* editor.c - the music editor. A piano roll for one instrument at a time
 * (the others ghosted behind it), a drum lane, the instrument's SID patch
 * as knobs you scroll, and the rewrite tools: compose, vary, harmonize,
 * re-key, major/minor, humanize, arpeggiate. It edits the very score the
 * game plays, live, and saves it back to assets/music.
 *
 * Mouse: double-click to add a note, drag a note to move it (its end to
 * lengthen it), drag on empty space to box-select (that box is also the
 * range Compose writes into), right-click to delete, wheel to scroll
 * (shift: time, ctrl: zoom), click the ruler to move the playhead.
 * Keys: space play/stop, arrows move or transpose the selection (shift:
 * octave), Delete, Ctrl+Z undo, Ctrl+S save, Ctrl+A select all, Tab next
 * instrument, [ ] other theme, G grid, Esc back to the menu. */
#include "editor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "compose.h"
#include "music.h"
#include "sfx.h"
#include "synth.h"

#define INK_C   0xFFe8e4ff
#define DIM_C   0xFF8a83c9
#define BG_C    0xFF0e0c1c
#define PANEL_C 0xFF16122e
#define HUD_C   0xFFa8f08c
#define WARN_C  0xFFff5a48
#define AMB_C   0xFFffd25a

static int    theme;
static Score *sc;
static int    track;        /* instrument index, or -1 for the drum lane */
static float  pps = 60;     /* pixels per second */
static float  t_left;       /* time at the left edge of the roll */
static int    midi_top = 90;
static const int ROW = 5;   /* pixels per semitone */
static float  grid, playhead;
static bool   playing, dirty;
static float  sel_t0 = -1, sel_t1 = -1;
static char   status[96];
static float  status_t;
static uint32_t seed = 1;
static Key    key;
static int    W = 640, H = 360;

/* layout */
#define TOP 24
#define LIST_W 104
#define PANEL_H 76
static int roll_x(void) { return LIST_W + 14; }
static int roll_y(void) { return TOP; }
static int roll_w(void) { return W - roll_x(); }
static int roll_h(void) { return H - PANEL_H - TOP; }

static void say(const char *msg) {
  snprintf(status, sizeof status, "%s", msg);
  status_t = 4;
}

/* ---- undo ---------------------------------------------------------------- */
#define UNDO_MAX 40
typedef struct { SongNote *notes; int n; SongHit *hits; int nh; } Snap;
static Snap undo[UNDO_MAX];
static int nundo;

static void push_undo(void) {
  if (!sc) return;
  if (nundo == UNDO_MAX) {
    free(undo[0].notes);
    free(undo[0].hits);
    memmove(undo, undo + 1, sizeof(Snap) * (UNDO_MAX - 1));
    nundo--;
  }
  Snap *s = &undo[nundo++];
  s->n = sc->nnotes;
  s->nh = sc->nhits;
  s->notes = malloc(sizeof(SongNote) * (size_t)(s->n ? s->n : 1));
  s->hits = malloc(sizeof(SongHit) * (size_t)(s->nh ? s->nh : 1));
  memcpy(s->notes, sc->notes, sizeof(SongNote) * (size_t)s->n);
  memcpy(s->hits, sc->hits, sizeof(SongHit) * (size_t)s->nh);
  dirty = true;
}

static void resync(void) {
  synth_sort(sc);
  if (playing) synth_seek(synth_song_time());
}

static void pop_undo(void) {
  if (!nundo) {
    say("NOTHING TO UNDO");
    return;
  }
  Snap *s = &undo[--nundo];
  sfx_lock();
  while (sc->capn < s->n) synth_add_note(sc, 0, 0, 60, 0.1f, 1);
  memcpy(sc->notes, s->notes, sizeof(SongNote) * (size_t)s->n);
  sc->nnotes = s->n;
  if (sc->caph < s->nh) {
    sc->hits = realloc(sc->hits, sizeof(SongHit) * (size_t)s->nh);
    sc->caph = s->nh;
  }
  memcpy(sc->hits, s->hits, sizeof(SongHit) * (size_t)s->nh);
  sc->nhits = s->nh;
  resync();
  sfx_unlock();
  free(s->notes);
  free(s->hits);
  say("UNDONE");
}

/* ---- opening ------------------------------------------------------------- */
static void load_theme(int t) {
  if (playing) music_play(-1);
  playing = false;
  for (int i = 0; i < nundo; i++) { free(undo[i].notes); free(undo[i].hits); }
  nundo = 0;
  theme = t;
  sc = music_score(t);
  dirty = false;
  if (!sc) {
    say("NO SCORE FOR THIS THEME");
    return;
  }
  /* learn the melody model from every theme once */
  static bool learned;
  if (!learned) {
    learned = true;
    for (int i = 0; i < MUSIC_THEMES; i++) {
      Score *o = music_score(i);
      if (o) compose_learn(o);
    }
  }
  key = compose_key(sc);
  grid = compose_step(sc);
  track = 0;
  for (int i = 0; i < sc->ninst; i++)
    if (strstr(sc->inst[i].name, "Lead") || !strcmp(sc->inst[i].name, "lead")) { track = i; break; }
  playhead = t_left = 0;
  sel_t0 = sel_t1 = -1;
  int hi = 0;
  for (int i = 0; i < sc->nnotes; i++)
    if (sc->notes[i].inst == track && sc->notes[i].midi > hi) hi = sc->notes[i].midi;
  midi_top = hi ? hi + 6 : 90;
  char m[80];
  snprintf(m, sizeof m, "%s: %d NOTES, KEY %s", THEMES[t].name, sc->nnotes, compose_key_name(key));
  say(m);
}

void editor_open(int t) { load_theme(t); }

/* ---- coordinates ------------------------------------------------------------ */
static float x_of(float t) { return roll_x() + (t - t_left) * pps; }
static float t_of(float x) { return t_left + (x - roll_x()) / pps; }
static float y_of(int midi) { return (float)(roll_y() + 10 + (midi_top - midi) * ROW); }
static int   midi_of(float y) { return midi_top - (int)floorf((y - roll_y() - 10) / ROW); }
static float snap(float t) { return roundf(t / grid) * grid; }

static const char *DRUMS[] = { "k", "s", "h", "o", "c", "X", "P", "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "g", "r3" };
#define NDRUMS ((int)(sizeof DRUMS / sizeof DRUMS[0]))
static int drum_row(const SongHit *h) {
  if (h->effect != E_DRUM) return NDRUMS + h->effect;
  for (int i = 0; i < NDRUMS; i++)
    if (!strcmp(DRUMS[i], h->code) || (h->code[0] == 'r' && DRUMS[i][0] == 'r')) return i;
  return 0;
}
static float y_drum(int row) { return (float)(roll_y() + 10 + row * 9); }

/* ---- the patch knobs ---------------------------------------------------------- */
typedef struct { const char *label; size_t off; float lo, hi, step; bool expo; } Knob;
#define K(l, f, lo, hi, st, e) { l, offsetof(Patch, f), lo, hi, st, e }
static const Knob KNOBS[] = {
  K("LEVEL", level, 0, 0.4f, 0.005f, false), K("PAN", pan, -1, 1, 0.05f, false), K("ATTACK", a, 0.001f, 2, 0.08f, true),
  K("DECAY", d, 0.01f, 3, 0.08f, true), K("SUSTAIN", s, 0, 1, 0.05f, false), K("RELEASE", r, 0.01f, 2, 0.08f, true),
  K("PULSE W", pw, 0.05f, 0.95f, 0.02f, false), K("PW SWEEP", pw_depth, 0, 0.45f, 0.01f, false), K("PW RATE", pw_rate, 0, 8, 0.1f, false),
  K("VIB DELAY", vib_delay, 0, 2, 0.02f, false), K("VIB RATE", vib_rate, 0, 10, 0.2f, false), K("VIB DEPTH", vib_depth, 0, 1, 0.02f, false),
  K("CUT START", cut0, 50, 12000, 0.06f, true), K("CUT END", cut1, 50, 12000, 0.06f, true), K("CUT TIME", cut_t, 0, 2, 0.02f, false),
  K("RESONANCE", q, 0, 1.9f, 0.05f, false), K("OCTAVE", octave, 0, 1, 0.05f, false),
};
#define NKNOBS ((int)(sizeof KNOBS / sizeof KNOBS[0]))
static const char *const WAVES[] = { "PULSE", "SAW", "TRIANGLE", "NOISE", "SINE" };
static const char *const FILTS[] = { "NO FILTER", "LOW PASS", "HIGH PASS", "BAND PASS" };

/* panel cells: 0 wave, 1 filter, then the knobs, 6 columns */
static void cell_rect(int i, int *x, int *y, int *w) {
  int cols = 7, cw = (W - 8) / cols;
  *x = 4 + (i % cols) * cw;
  *y = H - PANEL_H + 14 + (i / cols) * 11;
  *w = cw - 3;
}

static void knob_turn(int i, int dir) {
  if (!sc || track < 0) return;
  Patch *p = &sc->inst[track].p;
  sfx_lock();
  if (i == 0) p->wave = (uint8_t)((p->wave + dir + 5) % 5);
  else if (i == 1) p->filter = (uint8_t)((p->filter + dir + 4) % 4);
  else {
    const Knob *k = &KNOBS[i - 2];
    float *v = (float *)((char *)p + k->off);
    if (k->expo) *v = *v <= k->lo ? k->lo * (dir > 0 ? 1 + k->step : 1) : *v * (1 + k->step * dir);
    else *v += k->step * dir;
    *v = *v < k->lo ? k->lo : *v > k->hi ? k->hi : *v;
  }
  sfx_unlock();
  dirty = true;
}

/* ---- tool buttons -------------------------------------------------------------- */
enum { B_COMPOSE, B_VARY, B_HARM, B_KDOWN, B_KUP, B_MODE, B_HUMAN, B_ARP, B_UNDO, B_PLAY, B_SAVE, NBUT };
static const char *const BUT[NBUT] = { "COMPOSE", "VARY", "HARMONIZE", "KEY -", "KEY +", "MAJ/MIN", "HUMANIZE", "ARPEGGIO", "UNDO", "PLAY", "SAVE" };
static void but_rect(int i, int *x, int *y, int *w) {
  int bw = (W - 8) / NBUT;
  *x = 4 + i * bw;
  *y = H - 14;
  *w = bw - 3;
}

static int nsel(void) {
  int c = 0;
  for (int i = 0; i < sc->nnotes; i++) c += sc->notes[i].sel;
  return c;
}

static void play_toggle(void) {
  if (!sc) return;
  if (playing) {
    music_play(-1);
    playing = false;
    return;
  }
  music_restart(theme);
  music_level(false);
  sfx_lock();
  synth_seek(playhead);
  sfx_unlock();
  playing = true;
}

static void save(void) {
  char path[1400], m[96];
  int ok = 0;
  const char *rt = assets_path("music/");
  if (rt) {
    snprintf(path, sizeof path, "%s%s.json", rt, sc->key);
    ok += synth_save(sc, path);
  }
#ifdef A4C_SOURCE_DIR
  snprintf(path, sizeof path, "%s/assets/music/%s.json", A4C_SOURCE_DIR, sc->key);
  SDL_PathInfo info;
  if (SDL_GetPathInfo(path, &info)) ok += synth_save(sc, path);
#endif
  dirty = !ok;
  snprintf(m, sizeof m, ok ? "SAVED %s" : "COULD NOT SAVE %s", sc->key);
  say(m);
}

static void tool(int b) {
  if (!sc) return;
  char m[96];
  int n = 0;
  if (b == B_UNDO) { pop_undo(); return; }
  if (b == B_PLAY) { play_toggle(); return; }
  if (b == B_SAVE) { save(); return; }
  if (b == B_COMPOSE) {
    float t0 = sel_t0, t1 = sel_t1;
    if (t0 < 0 || t1 - t0 < grid) {
      say("DRAG A BOX OVER THE BARS TO COMPOSE INTO");
      return;
    }
    if (track < 0) { say("PICK AN INSTRUMENT, NOT THE DRUMS"); return; }
    push_undo();
    sfx_lock();
    for (int i = 0; i < sc->nnotes; i++) sc->notes[i].sel = false;
    n = compose_melody(sc, track, t0, t1, seed++ * 2654435761u);
    resync();
    sfx_unlock();
    snprintf(m, sizeof m, "COMPOSED %d NOTES IN %s (PRESS AGAIN FOR ANOTHER TAKE)", n, compose_key_name(key));
    say(m);
    return;
  }
  if (!nsel()) {
    say("SELECT SOME NOTES FIRST");
    return;
  }
  push_undo();
  sfx_lock();
  switch (b) {
  case B_VARY:  n = compose_vary(sc, seed++ * 2654435761u); break;
  case B_HARM:  n = compose_harmonize(sc); break;
  case B_KDOWN: n = compose_transpose(sc, -1); break;
  case B_KUP:   n = compose_transpose(sc, 1); break;
  case B_MODE:  n = compose_mode_swap(sc); break;
  case B_HUMAN: n = compose_humanize(sc, seed++ * 2654435761u); break;
  case B_ARP:   n = compose_arpeggiate(sc); break;
  default: break;
  }
  resync();
  sfx_unlock();
  key = compose_key(sc);
  snprintf(m, sizeof m, "%s: %d NOTES", BUT[b], n);
  say(m);
}

/* ---- mouse ------------------------------------------------------------------------ */
enum { DRAG_NONE, DRAG_MOVE, DRAG_SIZE, DRAG_BOX, DRAG_KNOB, DRAG_HEAD };
static int   drag;
static float mx0, my0, mx, my;
static float *orig;      /* t, midi, dur per note at the drag start */
static int   knob_drag, knob_acc;
static Uint64 last_click;

static int note_at(float x, float y) {
  for (int i = sc->nnotes - 1; i >= 0; i--) {
    const SongNote *n = &sc->notes[i];
    if (n->inst != track) continue;
    float nx = x_of(n->t), ny = y_of(n->midi), nw = fmaxf(2, n->dur * pps);
    if (x >= nx && x <= nx + nw && y >= ny && y < ny + ROW) return i;
  }
  return -1;
}

static int hit_at(float x, float y) {
  for (int i = sc->nhits - 1; i >= 0; i--) {
    const SongHit *h = &sc->hits[i];
    float hx = x_of(h->t), hy = y_drum(drum_row(h));
    if (x >= hx - 1 && x <= hx + 4 && y >= hy && y < hy + 8) return i;
  }
  return -1;
}

static bool in_roll(float x, float y) { return x >= roll_x() && y >= roll_y() + 10 && y < roll_y() + roll_h(); }

static void clear_sel(void) {
  for (int i = 0; i < sc->nnotes; i++) sc->notes[i].sel = false;
}

static void mouse_down(int button, float x, float y, bool shift) {
  mx0 = mx = x;
  my0 = my = y;
  Uint64 now = SDL_GetTicks();
  bool dbl = now - last_click < 350;
  last_click = now;
  /* tool buttons */
  for (int b = 0; b < NBUT; b++) {
    int bx, by, bw;
    but_rect(b, &bx, &by, &bw);
    if (x >= bx && x < bx + bw && y >= by && y < by + 11) { tool(b); return; }
  }
  /* patch knobs: click cycles, drag turns */
  for (int i = 0; i < NKNOBS + 2 && track >= 0; i++) {
    int cx, cy, cw;
    cell_rect(i, &cx, &cy, &cw);
    if (x >= cx && x < cx + cw && y >= cy && y < cy + 10) {
      if (i < 2) knob_turn(i, button == SDL_BUTTON_RIGHT ? -1 : 1);
      else {
        drag = DRAG_KNOB;
        knob_drag = i;
        knob_acc = 0;
      }
      return;
    }
  }
  /* the instrument list: name selects, M mutes, S solos */
  if (x < LIST_W && y >= TOP && y < H - PANEL_H) {
    int row = (int)((y - TOP - 10) / 10);
    if (row >= 0 && row <= sc->ninst) {
      int idx = row == sc->ninst ? -1 : row;
      if (x > LIST_W - 22 && idx >= 0) {
        sfx_lock();
        if (x > LIST_W - 11) sc->inst[idx].solo = !sc->inst[idx].solo;
        else sc->inst[idx].mute = !sc->inst[idx].mute;
        sfx_unlock();
      } else if (x > LIST_W - 22 && idx < 0) {
        sc->mute_drums = !sc->mute_drums;
      } else {
        track = idx;
        sfx_lock();
        clear_sel();
        sfx_unlock();
      }
    }
    return;
  }
  /* the ruler moves the playhead */
  if (y >= roll_y() && y < roll_y() + 10 && x >= roll_x()) {
    playhead = fmaxf(0, t_of(x));
    drag = DRAG_HEAD;
    if (playing) {
      sfx_lock();
      synth_seek(playhead);
      sfx_unlock();
    }
    return;
  }
  if (!in_roll(x, y)) return;
  float t = fmaxf(0, snap(t_of(x)));
  if (track < 0) { /* drum lane */
    int h = hit_at(x, y);
    if (button == SDL_BUTTON_RIGHT && h >= 0) {
      push_undo();
      sfx_lock();
      sc->hits[h] = sc->hits[--sc->nhits];
      resync();
      sfx_unlock();
    } else if (button == SDL_BUTTON_LEFT && h < 0 && dbl) {
      int row = (int)((y - roll_y() - 10) / 9);
      if (row >= 0 && row < NDRUMS) {
        push_undo();
        sfx_lock();
        if (sc->nhits == sc->caph) {
          sc->caph = sc->caph * 2 + 16;
          sc->hits = realloc(sc->hits, sizeof(SongHit) * (size_t)sc->caph);
        }
        SongHit *nh = &sc->hits[sc->nhits++];
        memset(nh, 0, sizeof *nh);
        nh->t = t;
        nh->vel = 1;
        nh->a1 = 1;
        nh->effect = E_DRUM;
        snprintf(nh->code, sizeof nh->code, "%s", DRUMS[row]);
        resync();
        sfx_unlock();
      }
    }
    return;
  }
  int i = note_at(x, y);
  if (button == SDL_BUTTON_RIGHT) {
    if (i >= 0) {
      push_undo();
      sfx_lock();
      sc->notes[i] = sc->notes[--sc->nnotes];
      resync();
      sfx_unlock();
    }
    return;
  }
  if (i >= 0) {
    sfx_lock();
    if (!sc->notes[i].sel && !shift) clear_sel();
    sc->notes[i].sel = true;
    sfx_unlock();
    push_undo();
    float right = x_of(sc->notes[i].t + sc->notes[i].dur);
    drag = right - x < 3 ? DRAG_SIZE : DRAG_MOVE;
    free(orig);
    orig = malloc(sizeof(float) * 3 * (size_t)(sc->nnotes ? sc->nnotes : 1));
    for (int k = 0; k < sc->nnotes; k++) {
      orig[3 * k] = sc->notes[k].t;
      orig[3 * k + 1] = sc->notes[k].midi;
      orig[3 * k + 2] = sc->notes[k].dur;
    }
    return;
  }
  if (dbl) {
    push_undo();
    sfx_lock();
    clear_sel();
    SongNote *n = synth_add_note(sc, t, track, midi_of(y), grid * 2 * 0.92f, 0.9f);
    if (n) n->sel = true;
    resync();
    sfx_unlock();
    return;
  }
  if (!shift) {
    sfx_lock();
    clear_sel();
    sfx_unlock();
  }
  drag = DRAG_BOX;
}

static void mouse_move(float x, float y) {
  mx = x;
  my = y;
  if (drag == DRAG_HEAD) playhead = fmaxf(0, t_of(x));
  if (drag == DRAG_KNOB) {
    int steps = (int)((my0 - y) / 3) - knob_acc;
    for (; steps > 0; steps--, knob_acc++) knob_turn(knob_drag, 1);
    for (; steps < 0; steps++, knob_acc--) knob_turn(knob_drag, -1);
  }
  if ((drag == DRAG_MOVE || drag == DRAG_SIZE) && orig) {
    float dt = snap((x - mx0) / pps);
    int dm = (int)lroundf((my0 - y) / ROW);
    sfx_lock();
    for (int k = 0; k < sc->nnotes; k++) {
      SongNote *n = &sc->notes[k];
      if (!n->sel) continue;
      if (drag == DRAG_MOVE) {
        n->t = fmaxf(0, orig[3 * k] + dt);
        int m = (int)orig[3 * k + 1] + dm;
        n->midi = (int8_t)(m < 12 ? 12 : m > 120 ? 120 : m);
      } else {
        n->dur = fmaxf(grid * 0.5f, orig[3 * k + 2] + dt);
      }
    }
    sfx_unlock();
  }
}

static void mouse_up(void) {
  if (drag == DRAG_BOX && sc) {
    float t0 = t_of(fminf(mx0, mx)), t1 = t_of(fmaxf(mx0, mx));
    int m0 = midi_of(fmaxf(my0, my)), m1 = midi_of(fminf(my0, my));
    sel_t0 = fmaxf(0, snap(t0));
    sel_t1 = snap(t1);
    sfx_lock();
    int c = 0;
    for (int i = 0; i < sc->nnotes; i++) {
      SongNote *n = &sc->notes[i];
      if (n->inst == track && n->t + n->dur > t0 && n->t < t1 && n->midi >= m0 && n->midi <= m1) {
        n->sel = true;
        c++;
      }
    }
    sfx_unlock();
    char m[80];
    snprintf(m, sizeof m, "%d NOTES SELECTED, RANGE %.2f-%.2f S", c, sel_t0, sel_t1);
    say(m);
  }
  if ((drag == DRAG_MOVE || drag == DRAG_SIZE) && sc) {
    sfx_lock();
    resync();
    sfx_unlock();
  }
  drag = DRAG_NONE;
}

/* ---- keys ------------------------------------------------------------------------- */
static void key_down(const SDL_KeyboardEvent *k) {
  bool ctrl = k->mod & SDL_KMOD_CTRL, shift = k->mod & SDL_KMOD_SHIFT;
  if (!sc) return;
  switch (k->key) {
  case SDLK_SPACE: play_toggle(); break;
  case SDLK_HOME: playhead = t_left = 0; break;
  case SDLK_TAB:
    track = track + 1 >= sc->ninst ? -1 : track + 1;
    sfx_lock();
    clear_sel();
    sfx_unlock();
    break;
  case SDLK_LEFTBRACKET:
  case SDLK_RIGHTBRACKET: load_theme((theme + (k->key == SDLK_RIGHTBRACKET ? 1 : MUSIC_THEMES - 1)) % MUSIC_THEMES); break;
  case SDLK_G: {
    float base = compose_step(sc);
    grid = grid > base * 1.5f ? base / 2 : grid < base * 0.75f ? base : base * 2;
    say(grid > base * 1.5f ? "GRID 1/8" : grid < base * 0.75f ? "GRID 1/32" : "GRID 1/16");
    break;
  }
  case SDLK_Z: if (ctrl) pop_undo(); break;
  case SDLK_S: if (ctrl) save(); break;
  case SDLK_A:
    if (ctrl) {
      sfx_lock();
      for (int i = 0; i < sc->nnotes; i++) sc->notes[i].sel = sc->notes[i].inst == track;
      sfx_unlock();
    }
    break;
  case SDLK_DELETE:
  case SDLK_BACKSPACE:
    if (nsel()) {
      push_undo();
      sfx_lock();
      int w = 0;
      for (int i = 0; i < sc->nnotes; i++)
        if (!sc->notes[i].sel) sc->notes[w++] = sc->notes[i];
      sc->nnotes = w;
      resync();
      sfx_unlock();
    }
    break;
  case SDLK_UP:
  case SDLK_DOWN:
    if (nsel()) {
      push_undo();
      sfx_lock();
      compose_transpose(sc, (k->key == SDLK_UP ? 1 : -1) * (shift ? 12 : 1));
      sfx_unlock();
    } else midi_top += k->key == SDLK_UP ? 3 : -3;
    break;
  case SDLK_LEFT:
  case SDLK_RIGHT:
    if (nsel()) {
      push_undo();
      sfx_lock();
      for (int i = 0; i < sc->nnotes; i++)
        if (sc->notes[i].sel) sc->notes[i].t = fmaxf(0, sc->notes[i].t + (k->key == SDLK_RIGHT ? grid : -grid));
      resync();
      sfx_unlock();
    } else t_left = fmaxf(0, t_left + (k->key == SDLK_RIGHT ? 2 : -2));
    break;
  case SDLK_C: tool(B_COMPOSE); break;
  case SDLK_V: if (!ctrl) tool(B_VARY); break;
  case SDLK_H: tool(B_HARM); break;
  case SDLK_M: tool(B_MODE); break;
  case SDLK_U: tool(B_HUMAN); break;
  case SDLK_R: tool(B_ARP); break;
  default: break;
  }
}

bool editor_event(const SDL_Event *ev, float kx, float ky) {
  switch (ev->type) {
  case SDL_EVENT_KEY_DOWN:
    if (ev->key.key == SDLK_ESCAPE) {
      if (playing) music_play(-1);
      playing = false;
      if (dirty) say("UNSAVED CHANGES STAY UNTIL YOU QUIT (CTRL+S SAVES)");
      return false;
    }
    key_down(&ev->key);
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (sc) mouse_down(ev->button.button, ev->button.x * kx, ev->button.y * ky, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
    break;
  case SDL_EVENT_MOUSE_MOTION:
    if (sc) mouse_move(ev->motion.x * kx, ev->motion.y * ky);
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    mouse_up();
    break;
  case SDL_EVENT_MOUSE_WHEEL: {
    SDL_Keymod mod = SDL_GetModState();
    float wy = ev->wheel.y;
    float x = ev->wheel.mouse_x * kx, y = ev->wheel.mouse_y * ky;
    if (sc && y >= H - PANEL_H && track >= 0) {
      for (int i = 0; i < NKNOBS + 2; i++) {
        int cx, cy, cw;
        cell_rect(i, &cx, &cy, &cw);
        if (x >= cx && x < cx + cw && y >= cy && y < cy + 10) knob_turn(i, wy > 0 ? 1 : -1);
      }
    } else if (mod & SDL_KMOD_CTRL) {
      float t = t_of(x);
      pps = fminf(800, fmaxf(4, pps * (wy > 0 ? 1.25f : 0.8f)));
      t_left = fmaxf(0, t - (x - roll_x()) / pps);
    } else if (mod & SDL_KMOD_SHIFT) {
      t_left = fmaxf(0, t_left - wy * 40 / pps);
    } else {
      midi_top += wy > 0 ? 2 : -2;
    }
    break;
  }
  default: break;
  }
  return true;
}

void editor_update(float dt) {
  if (status_t > 0) status_t -= dt;
  if (playing && sc) {
    playhead = (float)synth_song_time();
    /* follow the playhead */
    if (x_of(playhead) > W - 20) t_left = playhead - 10 / pps * 4;
    if (x_of(playhead) < roll_x()) t_left = fmaxf(0, playhead - 1);
  }
}

/* ---- drawing -------------------------------------------------------------------------- */
static uint32_t inst_col(int i) {
  static const uint32_t C[] = { 0xFF7aa8ff, 0xFFff7a5c, 0xFFa8f08c, 0xFFffd25a, 0xFFd08cff, 0xFF7af0ff, 0xFFff9ad0, 0xFFf0f0a0 };
  return C[i % 8];
}

static void txt(Framebuffer *fb, const char *s, float x, float y, uint32_t c, int al) { fb_text(fb, s, x, y, 1, c, al, 0); }

void editor_draw(Framebuffer *fb) {
  W = fb->w;
  H = fb->h;
  fb_clip_reset(fb);
  fb_clear(fb, BG_C);
  char buf[128];
  if (!sc) {
    txt(fb, "NO SCORES FOUND IN ASSETS/MUSIC", W / 2.0f, H / 2.0f, WARN_C, ALIGN_CENTER);
    return;
  }
  /* top bar */
  fb_rect(fb, 0, 0, W, TOP - 2, PANEL_C);
  snprintf(buf, sizeof buf, "MUSIC EDITOR  [ %s ]  %s  %.1fS%s", THEMES[theme].name, compose_key_name(key), playhead, dirty ? "  *" : "");
  txt(fb, buf, 4, 6, INK_C, ALIGN_LEFT);
  txt(fb, playing ? "PLAYING" : "SPACE PLAY", W - 4.0f, 6, playing ? HUD_C : DIM_C, ALIGN_RIGHT);
  txt(fb, status_t > 0 ? status : "DBL-CLICK ADD  DRAG MOVE  BOX SELECT  RCLICK DELETE  WHEEL SCROLL  CTRL+WHEEL ZOOM  ESC BACK",
      4, 16, status_t > 0 ? AMB_C : DIM_C, ALIGN_LEFT);

  /* instrument list */
  fb_rect(fb, 0, TOP, LIST_W, H - PANEL_H - TOP, PANEL_C);
  txt(fb, "INSTRUMENTS", 4, TOP + 4.0f, DIM_C, ALIGN_LEFT);
  for (int i = 0; i <= sc->ninst; i++) {
    int y = TOP + 10 + i * 10;
    if (y > H - PANEL_H - 10) break;
    bool on = (i == sc->ninst ? -1 : i) == track;
    if (on) fb_rect(fb, 0, y, LIST_W, 10, 0xFF2a2350);
    const char *nm = i == sc->ninst ? "DRUMS + FX" : sc->inst[i].name;
    char up[24];
    snprintf(up, sizeof up, "%.13s", nm);
    for (char *c = up; *c; c++)
      if (*c >= 'a' && *c <= 'z') *c -= 32;
    if (i < sc->ninst) fb_rect(fb, 2, y + 3, 3, 4, inst_col(i));
    txt(fb, up, 8, y + 5.0f, on ? 0xFFffffff : INK_C, ALIGN_LEFT);
    bool mute = i == sc->ninst ? sc->mute_drums : sc->inst[i].mute, solo = i < sc->ninst && sc->inst[i].solo;
    txt(fb, "M", LIST_W - 17.0f, y + 5.0f, mute ? WARN_C : 0xFF4d4580, ALIGN_LEFT);
    if (i < sc->ninst) txt(fb, "S", LIST_W - 7.0f, y + 5.0f, solo ? AMB_C : 0xFF4d4580, ALIGN_LEFT);
  }

  /* the roll */
  int rx = roll_x(), ry = roll_y(), rw = roll_w(), rh = roll_h();
  fb_clip(fb, LIST_W, ry, W - LIST_W, rh);
  fb_rect(fb, rx, ry, rw, rh, 0xFF121026);
  if (track >= 0) {
    /* rows: shaded black keys and out-of-key notes, a line at each C */
    for (int m = midi_top; y_of(m) < ry + rh; m--) {
      int pc = ((m % 12) + 12) % 12;
      bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
      int in = 0;
      const int *scl = key.minor ? (const int[]){ 0, 2, 3, 5, 7, 8, 10 } : (const int[]){ 0, 2, 4, 5, 7, 9, 11 };
      for (int q = 0; q < 7; q++) in |= ((m - key.root) % 12 + 12) % 12 == scl[q];
      fb_rect(fb, rx, (int)y_of(m), rw, ROW, in ? 0xFF181530 : 0xFF100e20);
      fb_rect(fb, LIST_W + 2, (int)y_of(m), 11, ROW - 1, black ? 0xFF202020 : 0xFFd8d8d8);
      if (pc == 0) {
        fb_rect(fb, rx, (int)y_of(m) + ROW - 1, rw, 1, 0xFF2a2350);
        snprintf(buf, sizeof buf, "C%d", m / 12 - 1);
        fb_text(fb, buf, rx + 2.0f, y_of(m) + 2, 1, 0xFF4d4580, ALIGN_LEFT, 0);
      }
    }
  } else {
    for (int r = 0; r < NDRUMS + 6; r++) {
      static const char *const FX[] = { "DRUM", "JET", "RISER", "SEA", "SONAR", "SWELL" };
      fb_rect(fb, rx, (int)y_drum(r), rw, 8, r % 2 ? 0xFF181530 : 0xFF141228);
      txt(fb, r < NDRUMS ? DRUMS[r] : FX[r - NDRUMS], LIST_W + 3.0f, y_drum(r) + 4, DIM_C, ALIGN_LEFT);
    }
  }
  /* bar lines: every 16 sixteenths of the detected step, beats dimmer */
  float base = compose_step(sc);
  for (int b = (int)(t_left / base); x_of(b * base) < W; b++) {
    float x = x_of(b * base);
    if (b % 16 == 0) fb_rect(fb, (int)x, ry + 10, 1, rh - 10, 0xFF3b335f);
    else if (b % 4 == 0 && pps * base > 3) fb_rect(fb, (int)x, ry + 10, 1, rh - 10, 0xFF221d40);
  }
  /* the loop point and the end */
  fb_rect(fb, (int)x_of(sc->loop), ry + 10, 1, rh - 10, 0xFF2f8a5a);
  fb_rect(fb, (int)x_of(sc->end), ry + 10, 1, rh - 10, 0xFF8a2f3a);
  /* compose range */
  if (sel_t0 >= 0 && sel_t1 > sel_t0) fb_rect_a(fb, (int)x_of(sel_t0), ry + 10, (int)((sel_t1 - sel_t0) * pps), rh - 10, 0xFFffd25a, 0.08f);
  if (track >= 0) {
    /* the other instruments, ghosted */
    for (int i = 0; i < sc->nnotes; i++) {
      const SongNote *n = &sc->notes[i];
      if (n->inst == track) continue;
      float x = x_of(n->t);
      if (x > W || x + n->dur * pps < rx) continue;
      fb_rect_a(fb, (int)x, (int)y_of(n->midi) + 1, (int)fmaxf(1, n->dur * pps), ROW - 2, inst_col(n->inst), 0.18f);
    }
    for (int i = 0; i < sc->nnotes; i++) {
      const SongNote *n = &sc->notes[i];
      if (n->inst != track) continue;
      float x = x_of(n->t);
      if (x > W || x + n->dur * pps < rx) continue;
      int w = (int)fmaxf(2, n->dur * pps - 1);
      fb_rect(fb, (int)x, (int)y_of(n->midi), w, ROW - 1, n->sel ? 0xFFffffff : inst_col(track));
      if (w > 4) fb_rect(fb, (int)x, (int)y_of(n->midi), 1, ROW - 1, 0xFF000000);
    }
  } else {
    for (int i = 0; i < sc->nhits; i++) {
      const SongHit *h = &sc->hits[i];
      float x = x_of(h->t);
      if (x > W || x < rx - 4) continue;
      int r = drum_row(h);
      int w = h->effect == E_DRUM ? 3 : (int)fmaxf(3, h->a1 * pps);
      fb_rect(fb, (int)x, (int)y_drum(r) + 1, w, 6, h->effect == E_DRUM ? 0xFFffd25a : 0xFF7af0ff);
    }
  }
  if (drag == DRAG_BOX) fb_rect_stroke(fb, fminf(mx0, mx), fminf(my0, my), fabsf(mx - mx0), fabsf(my - my0), 0xFFffffff, 1);
  /* ruler and playhead */
  fb_rect(fb, rx, ry, rw, 10, 0xFF221d40);
  for (int sec = (int)t_left; x_of((float)sec) < W; sec++) {
    if (pps < 12 && sec % 5) continue;
    snprintf(buf, sizeof buf, "%d", sec);
    txt(fb, buf, x_of((float)sec) + 2, ry + 5.0f, DIM_C, ALIGN_LEFT);
    fb_rect(fb, (int)x_of((float)sec), ry, 1, 10, 0xFF4d4580);
  }
  fb_rect(fb, (int)x_of(playhead), ry, 1, rh, 0xFFff5a48);
  fb_clip_reset(fb);

  /* patch knobs */
  fb_rect(fb, 0, H - PANEL_H, W, PANEL_H, PANEL_C);
  if (track >= 0) {
    const Patch *p = &sc->inst[track].p;
    snprintf(buf, sizeof buf, "%s  -  CLICK OR SCROLL TO CHANGE, PLAYS LIVE", sc->inst[track].name);
    for (char *c = buf; *c; c++)
      if (*c >= 'a' && *c <= 'z') *c -= 32;
    txt(fb, buf, 4, H - PANEL_H + 6.0f, INK_C, ALIGN_LEFT);
    for (int i = 0; i < NKNOBS + 2; i++) {
      int cx, cy, cw;
      cell_rect(i, &cx, &cy, &cw);
      fb_rect(fb, cx, cy, cw, 10, 0xFF221d40);
      if (i == 0) snprintf(buf, sizeof buf, "WAVE %s", WAVES[p->wave % 5]);
      else if (i == 1) snprintf(buf, sizeof buf, "%s", FILTS[p->filter % 4]);
      else {
        const Knob *k = &KNOBS[i - 2];
        float v = *(const float *)((const char *)p + k->off);
        snprintf(buf, sizeof buf, v >= 100 ? "%s %.0f" : "%s %.2f", k->label, v);
      }
      txt(fb, buf, cx + 2.0f, cy + 5.0f, i < 2 ? AMB_C : INK_C, ALIGN_LEFT);
    }
  } else {
    txt(fb, "DRUMS + FX: DOUBLE-CLICK A ROW TO ADD A HIT, RIGHT-CLICK TO DELETE", 4, H - PANEL_H + 6.0f, INK_C, ALIGN_LEFT);
  }
  for (int b = 0; b < NBUT; b++) {
    int bx, by, bw;
    but_rect(b, &bx, &by, &bw);
    bool main = b == B_COMPOSE || b == B_SAVE;
    fb_rect(fb, bx, by, bw, 11, main ? 0xFFB8C76F : 0xFF2a2350);
    const char *l = b == B_PLAY && playing ? "STOP" : BUT[b];
    txt(fb, l, bx + bw / 2.0f, by + 6.0f, main ? 0xFF1b1640 : INK_C, ALIGN_CENTER);
  }
}
