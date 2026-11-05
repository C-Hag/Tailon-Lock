# h2h-solo

Our own improvisation engine. It takes a written SID melody (an `.sf2txt` track, 16ths) and plays it back the way a
rock or pop soloist would on a second time round. The phrases are the same, and they start and end where the written
ones do. The long target notes and the contour are kept. In between, the engine elaborates:
- pentatonic sequences in 3s or 4s, and scale runs into the targets;
- pedal licks and neighbour tones;
- 8th/16th changes, anticipations and fills;
- a rock bend into the climax (a tied note with portamento);
- some long notes harmonised a 3rd or 6th below with chord tones, for a second voice.

It stays straight on the grid, with no swing. At most two notes are laid back one SID frame (20 ms).

Every take must pass these rules, or it's rejected:
- a chord tone on every strong beat;
- every note in the key;
- no leap wider than the melody's own widest;
- no minor 2nd against the bass on a beat;
- no stutters (the same note three times in a row);
- the written phrase starts and ends;
- a bend into the climax.

Takes are scored on:
- motif (interval contour, and pitch at the written onsets);
- chord fit;
- sync (onsets on the bass and kick/snare accents);
- singability;
- energy build;
- elaboration (about 30 % of the rows changed).

The engine then picks the best takes that differ from each other.

```
python3 h2h_solo.py chip-e.sf2txt --bars 65-68 --chords qa-spec.json --bass score-a.sf2txt:2 \
        --drums drum-hits.json --n 200 --pick 3 --out solo-takes.json
```

`solo-takes.json` holds the run behind "Arp sound DMX8" a/b/c (Full Burn's solo): each take's moves, its scores, and
its tokens for track 1 (the lead) and track 2 (the harmony).

**Refit** (`refit()`) moves a take onto other bars. Each bar is shifted through the scale for the best chord fit,
with strong beats snapped to chord tones, and the last bar's tail becomes a stepwise line into the next written note.
The same rules are then checked, plus no minor 2nds against the guitars or bass. `solo-refit-dmx9.json` is take A
refit onto riff 2 (bars 41-44) for DMX9.
