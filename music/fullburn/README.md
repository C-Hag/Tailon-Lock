# Full Burn, "Arp sound DMX10": the main theme

The game's menu and in-game theme (`assets/music/fullburn.wav`, looping as
`assets/music/fullburn.loop` says), and the opening film's score
(`assets/music/fullburn-intro.wav`, a cut of the same recording). The
composition, the parts, the synths and the drum machine are all ours.

| file | what |
|---|---|
| `fullburn-dmx10.mp3` | the full mix, 3:19 |
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
