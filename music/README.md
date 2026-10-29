# Music

The Ace4Combat soundtrack: rendered tracks and the Web Audio engines that
made them, copied from the browser game. There is more music work to come, so
this folder is the home for it.

- `fullburn/`: the main theme, Full Burn "Arp sound DMX10": the mix, the MIDI
  and the song data (see `fullburn/README.md`).
- `lib/h2h-drum/`: our own drum machine, synthesised, with no samples.
- `lib/h2h-solo/`: our own solo engine, which elaborates a song's melody.

All music is original: no melodies, recordings or samples from other works are used. Real-world tunes were
style references only; keep it that way.

## How the C port uses it

The game plays every theme live on its own SID-style synthesizer
(`src/synth.c`): pulse waves with width sweeps, saw, triangle and clocked
noise, ADSR, a resonant filter and delayed vibrato. The notes come from the
engines here: `node tools/extract_scores.js music/engines assets/music` runs
each engine headless through its `renderAll()` against a stub AudioContext
and writes `assets/music/<key>.json`, a list of timed notes per instrument
plus the drum and effect hits. Re-run it after changing an engine.

The rendered mp3s in `tracks/` are no longer shipped with the game. They stay
here as the reference for how each theme sounds in the browser;
`ace4combat --render-music KEY out.wav` renders the synth's version to
compare.

## Tracks

| Theme key | Track | Engine | File |
|-----------|-------|--------|------|
| `horizon` (default) | Steel Horizon (C64 style) | `music_horizon.js` | `tracks/steel-horizon.mp3` |
| `pop` | Nose 2 Nose (Pop, C64 style) | `music_pop.js` | `tracks/nose-2-nose-pop.mp3` |
| `anthem` | Nose 2 Nose Anthem | `music_anthem.js` | `tracks/nose-2-nose-anthem.mp3` |
| `h2h` | Nose 2 Nose | `music_h2h.js` | `tracks/nose-2-nose.mp3` |
| `strike` | Carrier Dawn (Strike) | `music_strike.js` | `tracks/carrier-dawn-strike.mp3` |
| `cine` | Carrier Dawn (Cinematic) | `music_cine.js` | `tracks/carrier-dawn-cinematic.mp3` |
| `overtone` | Carrier Dawn (Overtone) | `music_overtone.js` | `tracks/carrier-dawn-overtone.mp3` |
| `chip` | Carrier Dawn (SID original) | `Music` inside `../reference/ace4combat.html` | `tracks/carrier-dawn-theme.mp3` |
| `neon` | Neon Strike (80s synthwave pop, guitar solo) | `../tools/write_synthwave.py` (synth score only) | `tracks/neon-strike.mp3` |
| `midnight` | Midnight Ace (80s synthwave pop/rock, guitar and synth duet) | `../tools/write_synthwave.py` (synth score only) | `tracks/midnight-ace.mp3` |
| `rocknroll` | Six O'Clock High (1980s rock 'n' roll, smooth transitions) | `../tools/write_synthwave.py` (synth score only) | `tracks/six-oclock-high.mp3` |
| `ghost` | Ghost Squadron (rock 'n' roll synthwave pop, C64 voice roles) | `../tools/write_synthwave.py` (synth score only) | `tracks/ghost-squadron.mp3` |

**Rearranged** (`../tools/rearrange.py`): every theme is now played the
way Ghost Squadron is, keeping its own melody, chords and key: the C64
frame clock and voice roles, a rock 'n' roll band with synthwave colour, a jet
intro, sections that lead into each other and answers in the melody's gaps.
The earlier scores it reads are in `../tools/music_sources/`; the renders
of the older themes are in `tracks/band/` (the tracks above for them are the
JS engines' versions). The opening film keeps its own cut of Steel Horizon
(`assets/music/horizon-intro.json`), which its cues are timed to.

Kept for reference, not used in the game:

- `tracks/nose-2-nose-classical.mp3`: the earlier classical render of Nose 2 Nose.
- `tracks/nose-2-nose-pop-before-overtone.mp3`: Pop before the overtone experiment.
- `engines/music_pop_overtone_mix.js`: the overtone-merged Pop variant.

## C64 style (Steel Horizon and Pop)

Steel Horizon and Pop now play with a classic C64 groove and sound, designed after the
classic C64 sound. Melodies are ours and no samples are used. The jet build-up keeps
its timing, with a blues-rock opening under it in place of the old minor-key march. After the jet passes:

- tempo locked to the 50 Hz frame clock, sixteenths of 5, 5 and 6 frames (about 140 BPM);
- the C64 beat, kick on 1 and the "and" of 2, one snare on 3, with SID-style drums layered under the kit;
- a pumping pulse-wave bass (root, root, scale tone below) and a SID pulse layer on the leads;
- one-bar breaks where only the bass walks into the next section.

The engines default to the heavier mix (`music.heavy = true`). Set it to `false` for the full chiptune mix with the
chirping off-beat ostinato. Steps now carry their own length (`steps[i].dur`), so a player has to advance by that instead
of a fixed `STEP`. The previous versions are kept as `engines/music_horizon_classic.js`, `engines/music_pop_classic.js`,
`tracks/steel-horizon-classic.mp3` and `tracks/nose-2-nose-pop-classic.mp3`.
