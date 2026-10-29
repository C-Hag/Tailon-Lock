# The effect song format (.fxs)

Every sound effect in the game is a tiny three-voice song for a SID-style chip, one row per PAL frame (50 a second).
The songs are in `assets/sfx/NAME.fxs`. Both games play them with the same engine, `assets/sfx/sidfx.js` in the
browser and `src/sidfx.c` in the C game. `tools/sfx_convert.py` writes them from the plain-text effect songs of our
own SID toolchain. The format is plain text; `#` at the start of a line, or after a space, starts a comment.
Numbers are hexadecimal unless they end in a dot (`12.` is twelve).

## Header

    a4c-sfx 1             the format and its version
    play loop 64          a loop of 64 frames, cut from the steady part after two passes
    play once 2.3         a one-shot, 2.3 seconds long
    volume f              the chip's main volume, 0-f

## Programs

A program steps once a frame from the moment its instrument's note starts.

    wave NAME             the waveform and the note, a row a frame
      41 +0                 waveform bits, then the note: +n semitones from the played note, or =NN an absolute
                            note number (hex; A-4 = 39, 440 Hz)
      loop N                jump back to row N
      end                   hold the last row

The waveform bits are the chip's: `01` gate (the envelope runs while the note is held and this bit is set), `04`
ring modulation (the triangle's sign follows the voice before), `08` test (the oscillator stops and is silent),
`10` triangle, `20` sawtooth, `40` pulse, `80` noise.

    pulse NAME            the pulse width, 12 bits
      set 800 01            set the width to 800 and hold it 1 frame
      add 020 40            add 020 a frame for 40 frames (negative: add -020 40)
      loop N | end

    filter NAME           the filter, shared by the three voices
      set lp 400 f 1        the type (lp, bp, hp), the 11-bit cutoff, the resonance 0-f, which voices go through it
                            (bit 0 voice 1, bit 1 voice 2, bit 2 voice 3)
      add 010 40            add to the cutoff each frame for a number of frames
      loop N | end

## Instruments and commands

    instrument NAME ad=09 sr=a9 wave=W [pulse=P] [filter=F] [flags=filteron]

`ad` and `sr` are the envelope's attack and decay, and sustain level and release, a hex digit each (the chip's
rates: attack 2 ms to 8 s, decay and release three times as long). `filteron` sends the voice through the filter.

    command NAME vib FF AA     vibrato: a cycle of 2 x FF frames, AA x 0.3 semitones each way
    command NAME slide SSSS    add SSSS (16 bits, signed) to the frequency every frame

## Tracks

    track 1
      C-4:inst:cmd + + - ...  one token a frame: a note starts (its name, its instrument, an optional command),
                              + holds it, - lets it go (the envelope releases)
      @loop ...               where the track starts again when it reaches its end

A track that ends without `@loop` holds its last frame.
