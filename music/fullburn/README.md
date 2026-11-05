# Full Burn, "Arp sound DMX10": the main theme

The game's menu and in-game theme, and the opening film's score. The game plays it live on its own synth from
`assets/music/fullburn.json` (looping bars 33-72, 61.4-138.0 s) and `assets/music/fullburn-intro.json` (the film's
cut), which `../../tools/fullburn_score.py` writes from `fullburn-dmx10.mid` and `song/drum-hits.json` here. The
composition, the parts, the synths and the drum machine are all ours.

Until the owner has heard the live version against the recording (`fullburn-dmx10.mp3` against `A4C_MUSIC_FULL=1 tailonlock --render-music fullburn ab-b.wav`), the recordings stay in
`assets/music` (`fullburn.wav`, `fullburn.loop`, `fullburn-intro.wav`), but the game plays the score: a theme with
a score never loads its recording.

| file | what |
|---|---|
| `fullburn-dmx10.mp3` | the full mix, 3:19: the ear-safe mix (below) |
| `fullburn-dmx10.mid` | every part as MIDI, one track per instrument, with section markers |
| `song/chip-a.sf2txt` ... `chip-e.sf2txt` | the five SID chips' parts: drums, bass and lead of the opening (A), arpeggios, pad and jet (B), the two guitars (C, D), the buzz-arp lead with the solo (E) |
| `song/clock.sf2txt`, `song/jetoff.sf2txt` | the bell clock and the jet fly-off |
| `song/drum-hits.json` | the drum performance: every hit as `[time s, piece, velocity]` |
| `song/kit-dmx7.json` | the drum kit's settings for `../lib/h2h-drum` |
| `song/mix.json`, `song/mix-jet.json`, `song/pads.json` | the mix: levels, pans, the hall, the pad chords |

How it was made:

- **The composition:** our own generator scripts, writing every part as text.
  - The `.sf2txt` files are the SID tunes in a readable text form: instruments,
    tables and one line of notes and commands a bar.
  - The two guitar parts were transcribed from the guitarist's own recorded
    takes onto SID voices (root, fifth, octave).
- **The SID parts** are rendered on an emulated MOS 6581 chip.
- **The 80s layers** (the Moog-style bass, the Juno-style pads, the
  Synclavier-style harmony and the TX816-style bell clock) come from our own
  synths in Python.
- **The drums** come from our own drum machine, `../lib/h2h-drum`. It uses no
  samples.
- **The solo** (bars 41-44) is an h2h-solo take (`../lib/h2h-solo`) that
  elaborates the song's own melody.
- **The mix:** our own mixer, with a hall reverb.

## The ear-safe mix

Since the ear check (`../../docs/ear/`), the game and this MP3 use DMX10's
ear-safe mix: the same parts and renders, with the TX816 bell clock 4 dB
down, a -6 dB cut at 2.9 kHz on it, 3 dB less of it in the hall and a
one-bar fade-in where it enters (2:25.7); a -3 dB dip at 2.4 kHz on the
buzz-arp lead; and the master 5.1 dB down to -20 LUFS, the level of the
other themes. `song/mix.json` is the mix before these changes.

## Played live (the game's own synth)

`tools/fullburn_score.py` turns the song into a score for `src/synth.c`, in the way Ghost Squadron is written:

| part | in the score | the synth's sound |
|---|---|---|
| SID drums (the opening) | the synth's own SID kit (`k`, `s`, `h`, toms) | as the other themes |
| bass: the SID's to 0:30, then the Moog | `fbSidBass`, `fbMoog` | pulse; a resonant saw low-passed down to 280 Hz |
| SID arps | `fbArp` | pulse with a slow width sweep, the A5s 3 dB down, a 3 dB dip at 2.4 kHz |
| SID lead and pad (the opening) | `sidLead`, `fbSidPad` | |
| the two guitars | `fbGtrL`, `fbGtrR` | power chords hard left and right |
| buzz-arp lead (the melody and the solo) | `fbBuzz` | a narrow pulse, the ear-safe dip at 2.45 kHz (6 dB, for its C#7 and E7 overtones) |
| Synclavier harmony, Juno pads | `fbSyncl`, `fbJuno` | |
| TX816 bell clock (2:25.7) | `fbBell` | 4 dB under the old bell patch, a 6 dB cut at 2.9 kHz, the one-bar fade-in |
| the drummer (h2h-drum, DMX10's performance) | codes `fk fs fg fx fh fp fo ft1-3 fc1-2` | an h2h-drum style kit: a sine kick down to E1, the snare into a gated room on the backbeat, dark hats, Simmons-style toms, choked crashes |
| the jets | `jet` events | |

The timing is the SID tunes' own: 16ths of 6 PAL frames, bar 1 at 0.110 s; the drummer's hits keep their times
in the master. The loop is taken 40 ms before bar 33 so the drummer's slightly early downbeat crash is in it.
Against the recording (0-138 s): onsets line up at 0 ms; the octave bands from 63 Hz to 2 kHz are within 1.6 dB
of it, and 2-4 dB softer above 4 kHz (the dark, ear-safe side). The ear check of every render is in
`../../docs/ear/`.
