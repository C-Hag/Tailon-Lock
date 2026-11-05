# h2h-drum

The Tailon Lock drum machine: an 80s synthwave kit synthesised in numpy and scipy. It uses no samples and no
third-party sounds, so there's nothing to credit. It plays the session drummer's hit lists
(`{"hits": [[t_s, piece, velocity 0-1], ...]}`) and writes a stereo float32
WAV at 48 kHz.

```
python3 h2h_drum.py hits.json out.wav --kit kit-dmx7.json --length 198.957
python3 h2h_drum.py hits.json kick.wav --kit kit-dmx7.json --only kick     # one stem; the stems sum exactly to the kit
python3 h2h_drum.py --demo demo.wav                                         # every piece at three velocities
```

The voices and their settings are documented at the top of `h2h_drum.py` (the `KIT` table). In short:
- **Kick:** a sine body swept down to E1, soft-clipped, with a click on the attack.
- **Snare:** a tone body plus noise, into a gated reverb held open for 250-340 ms.
- **Hats:** FM partials, dark, nothing above about 10 kHz.
- **Toms:** Simmons-style, with a pitch drop.
- **Crash, ride and bell:** FM and ring-mod clusters.

Every piece answers velocity in level, brightness and decay.

`kit-dmx7.json` is the kit of Full Burn "Arp sound DMX7" to DMX10: each piece's level, with the crashes choked
short.
