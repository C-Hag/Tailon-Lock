# Could Tailon Lock run on a Pentium 120?

Measured on 2026-10-29 with `tools/p5bench` (a test tool, not shipped). The game's own software renderer,
flight sim, film and synth were built as a 32-bit x87 program (`-march=pentium -mno-mmx -mno-sse
-mfpmath=387`, no SDL). Instructions were counted with valgrind's callgrind, using a Pentium-like cache:
8 KB two-way L1 for code and for data, 32-byte lines, 256 KB of L2. Each scene was measured over 4 frames;
the setup is not counted. The frame rates assume a 120 MHz P54C. The `+mem` column adds L1 line fills (about
12 clocks from L2, 30 from memory) and write-buffer stalls to the 0.8-IPC estimate.

"Retro" here means no clouds at all. The game has no lower-detail mode yet, so this is the only reduction
measured; the film's grade and blur stay on.

| Scene | Res | Detail | M instr/frame | fps @0.5 IPC | @0.8 | @1.1 | @0.8 +mem |
|---|---|---|---|---|---|---|---|
| dogfight (cockpit, 1p) | 320x200 | full | 23.0 | 2.6 | 4.2 | 5.8 | 3.9 |
| | 320x200 | retro | 15.0 | 4.0 | 6.4 | 8.8 | 6.0 |
| | 640x480 | full | 59.6 | 1.0 | 1.6 | 2.2 | 1.5 |
| | 640x480 | retro | 28.4 | 2.1 | 3.4 | 4.7 | 3.1 |
| | 800x600 | full | 84.4 | 0.7 | 1.1 | 1.6 | 1.0 |
| | 800x600 | retro | 36.9 | 1.6 | 2.6 | 3.6 | 2.4 |
| deck (film, 9.6 s) | 320x200 | full | 67.3 | 0.9 | 1.4 | 2.0 | 1.3 |
| | 320x200 | retro | 62.3 | 1.0 | 1.5 | 2.1 | 1.5 |
| | 640x480 | full | 223 | 0.3 | 0.4 | 0.6 | 0.4 |
| | 800x600 | full | 324 | 0.2 | 0.3 | 0.4 | 0.3 |
| control room (film, 7.5 s) | 320x200 | full | 325 | 0.2 | 0.3 | 0.4 | 0.3 |
| | 640x480 | full | 1543 | 0.04 | 0.06 | 0.09 | 0.06 |
| | 800x600 | full | 2368 | 0.03 | 0.04 | 0.06 | 0.04 |

Memory stalls are small, 5-9 % of the time: the working set fits the caches well. The flight sim is cheap,
about 65,000 instructions per tick.

**Sound**, in millions of instructions per second of audio. At 22.05 kHz the cost is scaled by sample count.

| | at 48 kHz | at 22.05 kHz | share of a P120 at 0.8 IPC |
|---|---|---|---|
| sound effects, a busy moment (5 voices and 2 engines) | 104 | 48 | 50 % |
| music: 3 SID-style voices | 25 | 11 | 12 % |
| music: the h2h score | 36 | 16 | 17 % |

## Measured in FreeDOS (R0, 2026-10-29)

The same benchmark built as a DOS program (`tools/p5bench/build_dos.sh`: the DJGPP cross-compiler, gcc 12.2,
`-march=pentium`, 32-bit protected mode under the CWSDPMI extender) and run in FreeDOS 1.3, with DOSLFN for
the assets' long names, under the Bochs emulator with its own free BIOS (`tools/p5bench/freedos/run.sh`). Bochs runs
at a fixed 96 million instructions a second, which is a 120 MHz Pentium at 0.8 instructions a clock, and the
frame is timed in DOS with `uclock()`, so the times are what the 0.8-IPC column predicts for the DOS build. The
emulated PC has 256 MB: with 64 MB the extender swapped to the disk. All tools are test tools only.

The estimate is the callgrind count of today's Linux build (the control room now draws its people through the
depth-buffered path, so its numbers are new) at 96 million instructions a second.

| Scene | Res | callgrind, M instr/frame | estimate | FreeDOS in Bochs | fps |
|---|---|---|---|---|---|
| dogfight | 320x200 | 24.9 | 259 ms | 212 ms | 4.7 |
| | 640x480 | 61.6 | 641 ms | 512 ms | 2.0 |
| | 800x600 | 86.5 | 901 ms | 752 ms | 1.3 |
| deck (film, 9.6 s) | 320x200 | 69.3 | 722 ms | 585 ms | 1.7 |
| | 640x480 | 225 | 2347 ms | 1867 ms | 0.5 |
| | 800x600 | 325 | 3383 ms | 2735 ms | 0.4 |
| control room (film, 7.5 s) | 320x200 | 405 | 4215 ms | 4042 ms | 0.25 |
| | 640x480 | 1211 | 12619 ms | 12128 ms | 0.08 |
| | 800x600 | 1771 | 18448 ms | 17764 ms | 0.06 |

The DOS build needs 4-20 % fewer instructions than the Linux one. Its maths library is smaller (DJGPP's x87
`sin`, `exp` and `pow` against glibc's), and that matters most where the renderer calls it per pixel or per
vertex. The control room, almost all fill loops of our own, agrees to 4 %.

**The pictures.** Every DOS frame was compared with the same frame from the Linux build. 0.02-1.6 % of the bytes
differ, almost all by 1-3 levels out of 255 (the two maths libraries round differently). Fewer than 0.07 % differ
by more than 8, at a few edge pixels of the terrain and the deck's steam.

**DOSBox** was the planned second check, but it did not work headless here. DOSBox 0.74 never got FreeDOS past
its boot into the test, and DOSBox's own DOS has no long file names for the assets. Bochs, which counts
instructions rather than following the host's clock, is the better measure of the two anyway.

So the earlier estimate holds: a Pentium 120 draws the dogfight at about 5 fps at 320x200 today, and the speed
work in R1 is what makes it playable.

## Where the time goes

- **Dogfight.** `span_z`, the per-pixel depth-tested span fill, takes 32-58 % (more at higher
  resolutions). `r3d_begin`, which draws the sky, terrain and sea, takes 21-38 %. `fb_poly_z` takes 7-15 %.
- **Deck.** The span fill and the polygon setup take 40-45 %. The steam puffs and the grade with heat haze
  (`intro_render`) take 13-14 %.
- **Control room.** The painter's anti-aliased coverage fill (`fill_row`, `rasterize`) takes about 80 %. The
  blur takes 8 % and `hypotf` 4 %.
- **Sound effects.** `powf` and `expf` run for every sample of every voice: half of the cost is the maths
  library.
- **Float to int.** Every float-to-int cast on the x87 switches the FPU control word (`fldcw`) twice, at about
  8 clocks a switch on the P5. The sky, terrain and sea code (`r3d_begin`) does 40 of them, and they also
  sit in the polygon and painter code.

## What a retro build would need for 15-25 fps at 320x200

15-25 fps is about 4-6 M instructions a frame, a third of today's 15 M at retro detail:

1. **Sky, terrain and sea.** The sky as per-row bands, a coarser terrain grid at low resolution, no
   whitecaps, and the terrain drawn back to front without a depth test.
2. **Depth.** An integer depth buffer only where meshes overlap (or a painter's sort for the few jets), and
   no float compare per pixel.
3. **Rounding.** Round with `lrintf` and no `fldcw`, and fixed-point maths in the span and edge loops.
4. **Framebuffer.** 8-bit colour with a palette and blend tables (mode 13h or VESA 256 colours). That is a
   quarter of the memory traffic, and the copy to the card is 64 KB a frame.
5. **Sound.** Envelopes and pitch computed every 32 samples instead of every sample, with table-based
   exponentials. That makes the effects 5-10x cheaper, and both the music and the effects run at 22 kHz.
6. **The film.** The control room drawn as flat polygons with no anti-aliasing and no blur (about 20x
   cheaper), or shown as a held picture. The deck needs fewer, cheaper steam puffs and no grade.

The 640x480 and 800x600 VESA modes would also need the same work, plus copying 300-480 KB a frame over PCI
(about 15-25 ms at 20-30 MB/s). A Pentium 120 is realistic at 320x200 for play. 640x480 would run at about
5-8 fps at best.
