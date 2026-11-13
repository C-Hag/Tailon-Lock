# Music

The Tailon Lock soundtrack: rendered tracks and the Web Audio engines that
made them (the browser edition in `web/` plays them too).

- `fullburn/`: the main theme, Full Burn "Arp sound DMX10": the mix, the MIDI
  and the song data (see `fullburn/README.md`).
- `lib/h2h-drum/`: our own drum machine, synthesised, with no samples.
- `lib/h2h-solo/`: our own solo engine, which elaborates a song's melody.

All music is original: no melodies, recordings or samples from other works are used. Real-world tunes were
style references only; keep it that way.

## How the game uses it

The game plays every theme live on its own SID-style synthesizer
(`src/synth.c`): pulse waves with width sweeps, saw, triangle and clocked
noise, ADSR, a resonant filter and delayed vibrato. The notes come from the
engines here: `node tools/extract_scores.js music/engines assets/music` runs
each engine headless through its `renderAll()` against a stub AudioContext
and writes `assets/music/<key>.json`, a list of timed notes per instrument
plus the drum and effect hits. Re-run it after changing an engine.

The rendered mp3s in `tracks/` are not part of the game. They are the game
synth's own renders
(`tailonlock --render-music KEY out.wav`), one pass at -20 LUFS, so they sound
as the game plays; the JS engines' louder renders, some with piercing high
tones, were replaced. The versions kept for reference are at -20 LUFS
too, with a cut at 2.3 and 2.9 kHz where their lead's top notes pierced.

## Tracks

The game has three themes:

| Theme key | Track | Score from | File |
|-----------|-------|------------|------|
| `fullburn` (default) | Full Burn, "Arp sound DMX10", the main theme | `../tools/fullburn_score.py` (the song data in `fullburn/`) | `fullburn/fullburn-dmx10.mp3` (the ear-safe mix) |
| `ghost` | Ghost Squadron (rock 'n' roll synthwave pop, C64 voice roles) | `../tools/write_synthwave.py` (synth score only) | `tracks/ghost-squadron.mp3` |
| `horizon` | Steel Horizon (C64 style) | `music_horizon.js` | `tracks/steel-horizon.mp3` |

Full Burn is played live from its score like the others (it was a 36 MB recording before): every note of the
song's MIDI, the drummer's hits on an h2h-drum style kit in the synth, and the ear-safe mix's cuts in its patches
(`fb*` in `src/synth.c`). The game loops bars 33-72 (61.4-138.0 s), as the recording did; the score holds the
whole song, and the opening film plays its own cut (`assets/music/fullburn-intro.json`, from the same script).

Retired from the game on 2026-11-01 (their scores are gone from `assets/music`; the engines, the sources in
`../tools/music_sources/` stay here; their mp3 renders were taken out of the repository at release, and any of
them can be made again from its engine):

| Theme key | Track | Engine | File |
|-----------|-------|--------|------|
| `pop` | Nose 2 Nose (Pop, C64 style) | `music_pop.js` | (not kept) |
| `anthem` | Nose 2 Nose Anthem | `music_anthem.js` | (not kept) |
| `h2h` | Nose 2 Nose | `music_h2h.js` | (not kept) |
| `strike` | Carrier Dawn (Strike) | `music_strike.js` | (not kept) |
| `cine` | Carrier Dawn (Cinematic) | `music_cine.js` | (not kept) |
| `overtone` | Carrier Dawn (Overtone) | `music_overtone.js` | (not kept) |
| `chip` | Carrier Dawn (SID original) | `Music` inside `../web/index.html` | (not kept) |
| `neon` | Neon Strike (80s synthwave pop, guitar solo) | `../tools/write_synthwave.py` (synth score only) | (not kept) |
| `midnight` | Midnight Ace (80s synthwave pop/rock, guitar and synth duet) | `../tools/write_synthwave.py` (synth score only) | (not kept) |
| `rocknroll` | Six O'Clock High (1980s rock 'n' roll, smooth transitions) | `../tools/write_synthwave.py` (synth score only) | (not kept) |

**Rearranged** (`../tools/rearrange.py`): every theme is now played the
way Ghost Squadron is, keeping its own melody, chords and key: the C64
frame clock and voice roles, a rock 'n' roll band with synthwave colour, a jet
intro, sections that lead into each other and answers in the melody's gaps.
The scores it reads are in `../tools/music_sources/`. The opening film plays its own song, a recording
(`assets/music/heartbeat-intro.wav`).

Kept for reference, not used in the game:

- `engines/music_pop_overtone_mix.js`: the overtone-merged Pop variant.

## C64 style (Steel Horizon and Pop)

Steel Horizon and Pop play with a classic C64 groove and sound, designed after the
classic C64 sound. Melodies are ours and no samples are used. The jet build-up keeps
its timing, with a blues-rock opening under it. After the jet passes:

- tempo locked to the 50 Hz frame clock, sixteenths of 5, 5 and 6 frames (about 140 BPM);
- the C64 beat, kick on 1 and the "and" of 2, one snare on 3, with SID-style drums layered under the kit;
- a pumping pulse-wave bass (root, root, scale tone below) and a SID pulse layer on the leads;
- one-bar breaks where only the bass walks into the next section.

The engines default to the heavier mix (`music.heavy = true`). Set it to `false` for the full chiptune mix with the
chirping off-beat ostinato. Steps carry their own length (`steps[i].dur`), so a player has to advance by that instead
of a fixed `STEP`. The straight versions are `engines/music_horizon_classic.js` and `engines/music_pop_classic.js`.

## The ear check

`tests/ear_check.c` (built as `ear_check`) listens to a song the way an ear
does and flags what could hurt or tire it: clipping and true peaks,
loudness (BS.1770), the A-weighted level and the safe hours a week at a
given listening level, loudness in sone, sharpness in acum (shrill highs),
roughness in asper (buzz), piercing high tones that carry most of what is
heard, sudden jumps in level, clicks and bad loop seams. It is our own code,
calibrated at start against the standard reference sounds; `ear_check
--selftest` checks it (ctest runs that as `ear`).

`tools/ear_scan BUILD_DIR` renders every theme through the game's synth and
player and checks each one (one pass, round its loop through the player, and
Full Burn's whole song too), plus any recordings in `assets/music` with their
loop seams. The report and a one-line-a-song table go to `build/ear/`.
A loop seam is judged by its click and by the level step it adds over the
song's own lead-in to the loop point; digital silence at the end of a file
(a one-shot rendered longer than it plays) is left out of the averages; a
shrill passage counts only while the song is at least a quarter as loud as
usual.

The game plays everything through a master volume (VOLUME on the settings
page, 1-10 in 3 dB steps, 7 by default) and a peak limiter, so music and
effects together are turned down smoothly rather than clipped. HEARING
CHILD caps the volume at 5, and after every hour of sound the menu and the
pause screen ask for a break. The guides this follows are in
`../docs/HEARING.md`.
