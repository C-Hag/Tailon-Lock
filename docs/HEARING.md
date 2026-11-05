# Hearing

The game's sound follows the safe-listening guides of the health
authorities. This page is what they say and what the game and its tools do
about it.

## What the guides say

| Source | What it says |
|---|---|
| WHO-ITU H.870, *Safe listening devices and systems* | A weekly sound allowance: 80 dB(A) for 40 hours a week for adults, 75 dB(A) for 40 hours in a children's mode. Each 3 dB louder halves the safe time. Devices should track the dose, warn, and limit the volume. |
| WHO-ITU H.872, *Safe listening for video gameplay and esports* (2025) | The same dose for games, shown as a percentage over a rolling week. A warning at 100 %, then a choice to turn the volume down. Adult and children's modes. |
| WHO, *Global standard for safe listening venues and events* | At most 100 dB LAeq over 15 minutes as an upper limit, not a target; lower levels for children. Peaks at most 140 dB LCpeak (120 dB for children), with a fast limiter where a system could go over. Quiet places to rest the ears. |
| NIOSH (US) | At most 85 dB(A) over an 8-hour day, with the time halved for every 3 dB more. Ringing ears or dull sound after noise are a warning sign of too much. |
| EU music player standard (EN 50332 / EN 62368-1, Decision 2009/490/EC) | 85 dB(A) by default. The user may go up to 100 dB(A) after a warning, repeated every 20 hours of listening. |
| NHS | See a doctor if tinnitus is constant, gets worse, or affects sleep or mood. Get urgent help if it comes with sudden hearing loss or dizziness. Loud noise makes it worse. |

## What the game does

- **No hard clipping.** Music and effects go through a peak limiter with a
  -1 dBFS ceiling. A loud effect on a loud bar is turned down smoothly
  instead of being distorted.
- **A master volume**, VOLUME on the settings page, from 1 to 10 in 3 dB
  steps. It starts at 7, 9 dB under full level. A step never jumps from 1 to
  10.
- **A children's mode**, HEARING ADULT or CHILD. CHILD caps the volume at 5,
  6 dB under the default, matching the 5 dB lower children's dose.
- **Breaks.** After every hour of sound, the menu and the pause screen ask
  you to rest your ears for a few minutes.
- **Even levels.** Every theme is mastered to -20 LUFS, the main theme
  included (it plays live with the ear-safe mix's cuts in its patches), so
  changing themes never jumps in level. The synth has a DC
  blocker, softens the high tones the ear is most sensitive to (2-4 kHz) and
  fades a loop back in when a theme has faded out.
- **Effects under the music.** In play, every effect stays under the music:
  a busy dogfight measures -39 LUFS and -25 dBFS peaks at the default volume,
  and no effect is piercing, shrill or a sudden jump (`tools/ear_scan`
  checks each one over the main theme). The opening film keeps its effects
  18 dB under its music.

The game cannot know how loud a computer's speakers or headphones are, so it
cannot measure a dose in dB(A) as a phone with known earphones does. The
system volume still matters most.

## What the tools check

`ear_check` (tests/ear_check.c) reports each song against these limits at a
given listening level (`--cal`, default 95 dB SPL for a full-scale sine):

- the average dB(A) against 80 (WHO) and 85 (the EU default);
- the loudest moment against 100 dB(A) (the EU maximum);
- the peaks in dB SPL against 120 (children) and 140 (instant damage);
- the safe hours a week for an adult and a child;
- the things that tire the ears at any level: sharp (shrill) passages,
  roughness, piercing high tones, sudden jumps, clicks and clipping.

`tools/ear_scan` runs it on every theme and every sound effect (the game's
`--render-sfx` hook renders each effect over the main theme's chorus at its
in-game level). See `music/README.md` and `docs/ear/`.
