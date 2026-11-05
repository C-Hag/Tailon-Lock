#!/usr/bin/env python3
"""write_synthwave.py - writes the two synthwave themes, NEON STRIKE and
MIDNIGHT ACE, as scores for the game's SID-style synth (assets/music/*.json).

Everything here is written from scratch: 80s synthwave pop/rock on a
simple, physical backbeat (rock_beat: the C64 kick, snare on 2 and 4, the
bass locked to the kick, a guitar chugging under the verse; verses hold
back and choruses open up), a bent, wah-style guitar solo (NEON STRIKE) and a
guitar and synth duet (MIDNIGHT ACE). Only the styles are borrowed. The
parts are played like a band (Song.band): shared push and pull of the time,
a laid-back bass, strummed chords, phrased leads and players answering each
other. Nothing sits in the shrill top of the SID range.

The timing is the C64 frame clock: a 50 Hz frame counter with 16th notes of
5, 5 and 6 frames, so a bar is 85.33 frames (1.7067 s, about 140 BPM), and
the song is built from 8-bar blocks that end on a half bar without drums
and a bass pickup. Both open the same way and build slowly, one layer at a
time over a half-time beat: the jet alone, then the bosun's whistle (all
hands on deck) over a held bass, then the beat, the pad and the hook. Sections are 8 bars with 8-bar lifts,
a breathing space after the first chorus and a long breakdown.

    python3 tools/write_synthwave.py   # writes assets/music/neon.json, midnight.json, rocknroll.json

SIX O'CLOCK HIGH (rocknroll) is a third song, 1980s rock 'n' roll, written
for smooth transitions: see rocknroll().
"""
import json
import math
import os
import random

FRAME = 1 / 50


def step_time(n):
    """Start of 16th step n on the 5-5-6 frame clock, in seconds."""
    whole, part = divmod(n, 3)
    return (whole * 16 + part * 5) * FRAME


NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def m(name):
    """'E4', 'F#3', 'Bb2' -> midi."""
    n = NOTE[name[0]]
    i = 1
    while i < len(name) and name[i] in '#b':
        n += 1 if name[i] == '#' else -1
        i += 1
    return n + 12 * (int(name[i:]) + 1)


class Song:
    def __init__(self, key, seed):
        self.key = key
        self.notes = []
        self.events = []
        self.bar = 0  # current bar
        self.rnd = random.Random(seed)

    def t(self, bar, step):
        return 0.05 + step_time(bar * 16 + step)

    def note(self, bar, step, inst, midi, steps, vel=1.0, bend=0):
        t0 = self.t(bar, step)
        dur = self.t(bar, step + steps) - t0
        n = [round(t0, 4), inst, int(midi), round(dur * 0.94, 4), round(vel, 3)]
        if bend:
            n.append(bend)
        self.notes.append(n)

    def drum(self, bar, step, code, vel=1.0):
        # the drum machine sits under the synths: 0.65 of the SID kit's level
        self.events.append([round(self.t(bar, step), 4), 'drum', 1, code, round(vel * 0.65, 3)])

    def fx(self, bar, step, fn, *args):
        self.events.append([round(self.t(bar, step), 4), fn, 1, *args])

    def band(self):
        """Plays the parts like people playing together rather than a
        sequencer: the whole band breathes (pushes and pulls the time a
        little over each 8 bars), the bass sits a touch behind the beat,
        chords are strummed upward, leads phrase with their own small
        timing, every part shapes its loudness over a 4-bar phrase, and the
        pads and arp lean back while a lead is singing."""
        rnd = self.rnd
        BAR = 16 * 16 / 3 * FRAME
        LAID_BACK = {'funkBass': 0.007, 'bigBass': 0.006}
        CHORDS = {'pad', 'gatedPad', 'brassChord', 'synthBrass', 'powerChord', 'clav', 'guitar'}
        LEADS = {'lead80s', 'warmLead', 'wahGuitar'}
        SUPPORT = {'pad', 'gatedPad', 'softArp', 'brassChord'}
        lead_on = [(n[0], n[0] + n[3]) for n in self.notes if n[1] in LEADS]
        lead_on.sort()

        def singing(t):
            lo, hi = 0, len(lead_on)
            while lo < hi:
                mid = (lo + hi) // 2
                if lead_on[mid][0] <= t:
                    lo = mid + 1
                else:
                    hi = mid
            return any(a <= t < b for a, b in lead_on[max(0, lo - 4):lo])

        def breathe(t):
            return t + 0.006 * math.sin(2 * math.pi * t / (8 * BAR)) + 0.003 * math.sin(2 * math.pi * t / (2 * BAR) + 1.3)

        def shape(t):  # loudness over a 4-bar phrase: lift into bar 3, ease off at the end
            u = (t / (4 * BAR)) % 1
            return 0.92 + 0.1 * math.sin(math.pi * u)

        chords = {}
        for n in self.notes:
            if n[1] in CHORDS:
                chords.setdefault((n[1], n[0]), []).append(n)
        for group in chords.values():  # strum upward, 9-14 ms a string
            gap = 0.009 + 0.005 * rnd.random()
            for i, n in enumerate(sorted(group, key=lambda n: n[2])):
                n[0] += i * gap
        for n in self.notes:
            t = n[0]
            n[0] += LAID_BACK.get(n[1], 0)
            if n[1] in LEADS:
                n[0] += (rnd.random() - 0.5) * 0.016
                n[4] *= 0.9 + 0.2 * rnd.random()
            else:
                n[0] += (rnd.random() - 0.5) * 0.008
                n[4] *= 0.95 + 0.1 * rnd.random()
            if n[1] in SUPPORT and singing(t):
                n[4] *= 0.78
            n[4] = round(n[4] * shape(t), 3)
            n[0] = round(max(0.0, breathe(n[0])), 4)
        for e in self.events:
            if e[1] == 'drum' and e[3] in ('h', 'o', 'r1'):
                e[4] = round(e[4] * (0.88 + 0.24 * rnd.random()), 3)
                e[0] += (rnd.random() - 0.5) * 0.004
            e[0] = round(max(0.0, breathe(e[0])), 4)

    def write(self, path, loop_bar, end_bar):
        self.band()
        self.notes.sort(key=lambda n: n[0])
        self.events.sort(key=lambda e: e[0])
        out = {'key': self.key, 'length': round(self.t(end_bar, 0) + 3, 4), 'loop': round(self.t(loop_bar, 0), 4),
               'events': self.events, 'notes': self.notes}
        with open(path, 'w') as f:
            json.dump(out, f, separators=(',', ':'))
        print(f'{path}: {len(self.notes)} notes, {len(self.events)} hits, {self.t(end_bar, 0):.1f} s, loop at {self.t(loop_bar, 0):.1f} s')


# ---- shared parts --------------------------------------------------------------

def rock_beat(s, bar, chorus=False, half_break=False, fill=False, intensity=1.0):
    """The pop/rock backbeat on the C64 frame clock. The kick keeps the C64
    drum's pattern, 1 and the 'and' of 2 (steps 0 and 6), with beat 3 added
    in the chorus for drive; the snare cracks on 2 and 4 (steps 4 and 12).
    Verses hold back: closed 8th hats, no claps. Choruses open up: claps on
    the backbeat, an open hat on the 'and' of 4 and a crash every 4 bars.
    A fill on the fourth bar of a phrase keeps it from repeating."""
    last = 8 if half_break else 16
    for st in ((0, 6, 8) if chorus else (0, 6)):
        if st < last:
            s.drum(bar, st, 'k', (1.0 if st == 0 else 0.85) * intensity)
    for st in (4, 12):
        if st < last:
            s.drum(bar, st, 's', (1.0 if chorus else 0.8) * intensity)
            if chorus:
                s.drum(bar, st, 'c', 0.7 * intensity)
    for st in range(0, last, 2):
        if chorus and st == 14:
            s.drum(bar, st, 'o', 0.6 * intensity)
        else:
            s.drum(bar, st, 'h', (0.6 if st % 4 == 0 else 0.42) * (1.0 if chorus else 0.8) * intensity)
    if fill and not half_break:
        if chorus:  # snare 16ths into the next phrase
            for st in (13, 14, 15):
                s.drum(bar, st, 's', (0.55 + 0.1 * (st - 13)) * intensity)
        else:  # two low toms
            s.drum(bar, 14, 'T2', 0.6 * intensity)
            s.drum(bar, 15, 'T3', 0.65 * intensity)


def crash(s, bar):
    s.drum(bar, 0, 'X', 0.9)


def pickup(s, bar, inst, root, scale):
    """Bass pickup in the drumless half bar that closes a block."""
    run = [root + scale[i % len(scale)] + 12 * (i // len(scale)) for i in range(6)]
    for i, st in enumerate((8, 10, 11, 12, 14, 15)):
        s.note(bar, st, inst, run[i], 1, 0.9)


def chord_tones(root, quality):
    third = 3 if quality == 'm' else 4
    return [root, root + third, root + 7]


def gated_pad(s, bar, root, quality, inst='gatedPad', vel=0.8):
    """Chord chopped into 8ths, the gated synthwave pad."""
    tones = chord_tones(root, quality) + [root + 12]
    for st in range(0, 16, 2):
        for tn in tones:
            s.note(bar, st, inst, tn, 1, vel * (1.0 if st % 4 == 0 else 0.75))


def long_pad(s, bar, root, quality, bars=1, inst='pad', vel=0.9):
    for tn in chord_tones(root, quality) + [root + 12]:
        s.note(bar, 0, inst, tn, 16 * bars, vel)


def arp(s, bar, root, quality, inst='softArp', vel=0.7, up_oct=1):
    """16th arpeggio over two octaves, up and down, soft and filtered, and
    never above C6 so it stays out of the shrill SID register."""
    tones = chord_tones(root, quality)
    seq = tones + [t + 12 for t in tones] + [root + 24] + [t + 12 for t in reversed(tones)]
    seq = [n + 12 * (up_oct - 1) for n in seq]
    while max(seq) > m('C6'):
        seq = [n - 12 for n in seq]
    for st in range(16):
        s.note(bar, st, inst, seq[st % len(seq)], 1, 0.9 * vel * (1.0 if st % 4 == 0 else 0.75))


def ostinato(s, bar, root, quality, inst='softArp', vel=0.6):
    """The C64 pulse ostinato: off-beat 16th pairs."""
    tones = chord_tones(root, quality)
    for i, st in enumerate((2, 3, 10, 11, 14, 15)):
        s.note(bar, st, inst, tones[(i + 1) % 3] + 12, 1, vel)


def pump_bass(s, bar, root, inst='bigBass', half_break=False):
    """Synthwave 8th pump in octaves, with the C64 neighbour note."""
    last = 8 if half_break else 16
    for st in range(0, last, 2):
        n = root + (12 if st % 4 == 2 else 0)
        if st in (6, 14):
            n = root - 2 + 12  # the lower whole-step neighbour, an octave up
        s.note(bar, st, inst, n, 1, 0.95 if st % 4 == 0 else 0.8)


def c64_bass(s, bar, root, inst='funkBass', half_break=False):
    """The C64 bass: the root pumped on steps 0, 2, 6, 8, 12 and 14, the
    lower whole-step neighbour on 6 and 14, leaning on the kick (0 and 6)."""
    for st in (0, 2, 6, 8, 12, 14):
        if half_break and st >= 8:
            break
        n = root - 2 if st in (6, 14) else root
        s.note(bar, st, inst, n, 2, 0.95 if st in (0, 6) else 0.72)


def chug(s, bar, root, half_break=False, vel=0.32):
    """A quiet palm-muted guitar chugging 8ths on root and fifth under the
    verse: the rhythm engine that sits under the melody without crowding it."""
    for st in range(0, 8 if half_break else 16, 2):
        for tn in (root + 12, root + 19):
            s.note(bar, st, 'powerChord', tn, 1, vel * (1.0 if st % 4 == 0 else 0.7))


def stab(s, bar, steps, root, quality, inst='synthBrass', length=1, vel=0.9):
    for st in steps:
        for tn in chord_tones(root, quality) + [root + 12]:
            s.note(bar, st, inst, tn + 12, length, vel)


def answer(s, bar, phrase, prog, inst='guitar', vel=0.7):
    """Where the lead leaves a gap of half a bar or more, a second player
    answers with a short phrase from the chord under it, as players do."""
    ends = sorted((it[0] + it[2], it[0]) for it in phrase)
    starts = sorted(it[0] for it in phrase)
    for end, _ in ends:
        nxt = min([st for st in starts if st >= end], default=None)
        if nxt is None or nxt - end < 6:
            continue
        b, st = divmod(end + 1, 16)
        r, q = prog[b % len(prog)]
        tones = chord_tones(r + 24, q)
        for i, n in enumerate((tones[2], tones[1], tones[0] + 12)):  # fifth, third, up to the root
            if end + 1 + i * 2 >= nxt - 1:
                break
            bb, ss = divmod(end + 1 + i * 2, 16)
            s.note(bar + bb, ss, inst, n, 2, vel * (1 - 0.1 * i))


def rhythm_guitar(s, bar, prog, bars, vel=0.55):
    """A second guitarist under the chorus: root and fifth in 8ths, palm
    muted, leaning on the beat and opening up on the last 8th of each bar."""
    for i in range(bars):
        r, q = prog[i % len(prog)]
        for st in range(0, 16, 2):
            accent = 1.0 if st % 4 == 0 else 0.8
            ln = 2 if st == 14 else 1
            for tn in (r + 12, r + 19):
                s.note(bar + i, st, 'powerChord', tn, ln, vel * accent)


def melody(s, bar, inst, phrase, vel=1.0, transpose=0):
    """phrase: list of (step, note name, steps[, bend]) across bars (step may exceed 16)."""
    for item in phrase:
        st, nm, ln = item[0], item[1], item[2]
        bend = item[3] if len(item) > 3 else 0
        b, s16 = divmod(st, 16)
        s.note(bar + b, s16, inst, m(nm) + transpose, ln, vel, bend)


def bosun_whistle(s, bar):
    """'All hands on deck': the bosun's pipe. A low note that slides up to a
    long shrill hold, a quick drop, then the trilled call, twice."""
    W = 'whistle'
    s.note(bar, 0, W, m('C6'), 10, 0.9, bend=7)
    s.note(bar, 10, W, m('F5'), 2, 0.8, bend=-7)
    for i in range(6):
        s.note(bar, 12 + i, W, m('C6') if i % 2 == 0 else m('D6'), 1, 0.75)
    s.note(bar + 1, 2, W, m('C6'), 8, 0.85, bend=7)
    s.note(bar + 1, 10, W, m('C5'), 4, 0.7, bend=-12)


def intro(s, build, hook_inst, hook, root):
    """Sixteen steady bars that add one thing at a time, over a slow
    half-time beat (a kick on 1, one big gated snare on 3, in the manner of
    a slow 80s drum entrance) so the opening never crowds:
      bars 0-3   the jet alone, a soft heartbeat kick
      bars 4-7   the bosun's whistle over a held bass and a deep boom
      bars 8-11  the half-time beat with quarter hats; the bass starts to move
      bars 12-15 the pad, then the hook, a soft arp and a long riser
    Only bar 15 has a fill, slow and in 8ths, into the verse."""
    s.fx(0, 0, 'jet', 7.5, 1, 0.75)
    # bars 0-3: the jet, and a heartbeat on 1
    for i in range(4):
        s.drum(i, 0, 'k', 0.45)
    # bars 4-7: all hands on deck. A deep boom on 1, the bass held under it
    bosun_whistle(s, 4)
    for i in range(4, 8):
        s.drum(i, 0, 'P', 0.85 if i == 4 else 0.6)
        s.drum(i, 8, 'k', 0.5)
        s.note(i, 0, 'bigBass', root, 16, 0.85)
    # bars 8-15: the half-time beat
    for i in range(8, 16):
        r, q = build[i % 4]
        s.drum(i, 0, 'k', 0.85)
        if i % 4 == 0:
            s.drum(i, 0, 'P', 0.6)
        s.drum(i, 10, 'k', 0.55)
        if not (i == 15):
            s.drum(i, 8, 'S', 0.8)
        for st in range(0, 16, 4):  # quarter hats, a touch louder bar by bar
            s.drum(i, st + 2, 'h', 0.25 + 0.02 * (i - 8))
        if i < 12:
            s.note(i, 0, 'bigBass', r, 8, 0.85)
            s.note(i, 8, 'bigBass', r, 8, 0.7)
        else:
            pump_bass(s, i, r, half_break=i == 15)
    # bars 12-15: the pad, then the hook, a quiet arp and a long riser
    s.fx(12, 0, 'riser', 4 * 1.7067, 0.6)
    for i in range(12, 16):
        r, q = build[i % 4]
        long_pad(s, i, r + 24, q, vel=0.5 + 0.05 * (i - 12))
        if i >= 13:
            arp(s, i, r + 36, q, vel=0.1 + 0.03 * (i - 13))
    melody(s, 12, hook_inst, hook, 0.65)
    melody(s, 14, hook_inst, hook, 0.75)
    # the one fill: slow gated toms in 8ths into the verse
    for st, k in ((8, 0), (10, 1), (12, 2), (14, 3)):
        s.drum(15, st, 'G%d' % k, 0.6)
    crash(s, 16)


def interlude(s, b0, prog, root):
    """Four bars that let the chorus go: the beat thins to kick and hats,
    the pad holds, a bass pickup leads into the verse."""
    for i in range(4):
        r, q = prog[i % 4]
        long_pad(s, b0 + i, r + 24, q, vel=0.7 - 0.05 * i)
        arp(s, b0 + i, r + 36, q, vel=0.25)
        for st in range(0, 16 if i < 3 else 8, 4):
            s.drum(b0 + i, st, 'k', 0.8)
        for st in range(2, 16 if i < 3 else 8, 4):
            s.drum(b0 + i, st, 'h', 0.5)
        s.note(b0 + i, 0, 'bigBass', r, 8, 0.8)
    pickup(s, b0 + 3, 'funkBass', root, [0, 3, 5, 7])


def outro(s, b0, prog):
    for i in range(4):
        r, q = prog[i % 4]
        long_pad(s, b0 + i, r + 24, q, vel=0.6 - 0.1 * i)
        arp(s, b0 + i, r + 36, q, vel=0.25 - 0.05 * i)
    s.fx(b0, 0, 'jet', 6, 0.8, 0.5)


# ---- NEON STRIKE: E minor, rock verse, synthwave chorus, guitar solo -------------

def neon():
    s = Song('neon', 41)
    E, C, G, D, A, B = m('E2'), m('C2'), m('G2'), m('D2'), m('A2'), m('B1')
    CHORUS = [(E, 'm'), (C, ''), (G, ''), (D, '')]
    VERSE = [(E, 'm'), (E, 'm'), (A, ''), (A, '')]  # Em7 to A: a dorian funk vamp

    # Intro hook: a syncopated two-bar call on the lead, answered by brass
    HOOK = [(0, 'E5', 3), (3, 'E5', 3), (6, 'G5', 2), (8, 'F#5', 4), (12, 'E5', 2), (14, 'D5', 2),
            (16, 'B4', 6), (22, 'D5', 2), (24, 'E5', 8)]
    # Chorus melody, four bars over Em C G D, then an answer that climbs
    CH_A = [(0, 'B4', 4), (4, 'G4', 2), (6, 'A4', 2), (8, 'B4', 6), (14, 'D5', 2),
            (16, 'E5', 6), (22, 'D5', 2), (24, 'C5', 4), (28, 'B4', 4),
            (32, 'D5', 4), (36, 'B4', 2), (38, 'G4', 2), (40, 'A4', 6), (46, 'B4', 2),
            (48, 'A4', 6), (54, 'F#4', 2), (56, 'D4', 4), (60, 'E4', 4)]
    CH_B = CH_A[:14] + [(48, 'A4', 4), (52, 'B4', 4), (56, 'D5', 4), (60, 'E5', 4)]
    # Verse counter line: short syncopated phrases with space around them
    VL = [(2, 'E4', 1), (3, 'G4', 1), (6, 'A4', 2), (10, 'B4', 2), (12, 'A4', 1), (14, 'G4', 2),
          (34, 'E4', 1), (35, 'G4', 1), (38, 'A4', 2), (42, 'C#5', 2), (44, 'B4', 4)]

    intro(s, CHORUS, 'lead80s', HOOK, E)

    def verse(b0):
        for i in range(8):
            r, q = VERSE[i % 4]
            last = i == 7
            rock_beat(s, b0 + i, half_break=last, fill=i == 3, intensity=0.85)
            # the C64 bass pump, locked to the kick on 1 and the 'and' of 2
            c64_bass(s, b0 + i, r, half_break=last)
            chug(s, b0 + i, r, half_break=last)
            # clav stabs on the off-beats from the second half, dorian colour
            for st in (2, 7, 10, 15) if i >= 4 else ():
                if last and st >= 8:
                    break
                for tn in (r + 24 + 3 if q == 'm' else r + 24 + 4, r + 24 + 7, r + 24 + 10):
                    s.note(b0 + i, st, 'clav', tn, 1, 0.75)
            ostinato(s, b0 + i, r + 24, q, vel=0.3)
        melody(s, b0 + 4, 'warmLead', VL, 0.75)
        answer(s, b0 + 4, VL, VERSE)
        pickup(s, b0 + 7, 'funkBass', E, [0, 3, 5, 7])

    def pre(b0):
        """Eight bars of slow lift: half-time drums, a brass pad swelling
        bar by bar, the arp climbing, a long riser and a snare roll."""
        s.fx(b0 + 4, 0, 'riser', 4 * 1.7067, 0.8)
        for i, (r, q) in enumerate([(C, ''), (D, ''), (C, ''), (B, 'm')] * 2):
            k = i / 7
            for st in (0, 8):
                s.drum(b0 + i, st, 'k', 0.8)
            s.drum(b0 + i, 8 if i < 4 else 4, 's', 0.45 + 0.3 * k)
            if i >= 4:
                s.drum(b0 + i, 12, 's', 0.45 + 0.3 * k)
            for st in range(0, 16, 4 if i < 4 else 2):
                s.drum(b0 + i, st, 'h', 0.35 + 0.3 * k)
            long_pad(s, b0 + i, r + 24, q, inst='brassChord', vel=0.45 + 0.35 * k)
            s.note(b0 + i, 0, 'bigBass', r, 8, 0.8)
            s.note(b0 + i, 8, 'bigBass', r, 8, 0.7)
            arp(s, b0 + i, r + 24, q, vel=0.2 + 0.2 * k, up_oct=2 if i >= 4 else 1)
        for st in range(8, 16):  # snare roll into the chorus
            s.drum(b0 + 7, st, 's', 0.4 + 0.05 * (st - 8))

    def chorus(b0, last_block=True, big=False):
        crash(s, b0)
        for i in range(8):
            r, q = CHORUS[i % 4]
            end = i == 7 and last_block
            rock_beat(s, b0 + i, chorus=True, half_break=end, fill=i == 3)
            if i == 4:
                crash(s, b0 + i)
            pump_bass(s, b0 + i, r, half_break=end)
            gated_pad(s, b0 + i, r + 24, q, vel=0.6)
            arp(s, b0 + i, r + 36, q, vel=0.25)
            if i % 4 == 0:
                stab(s, b0 + i, (0,), r + 12, q, length=4, vel=0.5)
        melody(s, b0, 'lead80s', CH_A, 0.85)
        melody(s, b0 + 4, 'lead80s', CH_B, 0.85)
        rhythm_guitar(s, b0, CHORUS, 8)
        if last_block:
            pickup(s, b0 + 7, 'bigBass', E, [0, 3, 5, 7])

    def guitar_solo(b0):
        """Eight bars of a bent, wah-style blues-rock solo over Em D C B7:
        call and answer phrases, bends at the phrase ends, a fast run up
        to the climax and a long held bend."""
        prog = [(E, 'm'), (D, ''), (C, ''), (B, ''), (E, 'm'), (D, ''), (C, ''), (B, '')]
        for i, (r, q) in enumerate(prog):
            end = i == 7
            rock_beat(s, b0 + i, chorus=True, half_break=end, fill=i == 3, intensity=0.85)
            pump_bass(s, b0 + i, r, inst='funkBass', half_break=end)
            long_pad(s, b0 + i, r + 24, 'm' if r == B else q, inst='pad', vel=0.55)
        G_ = 'wahGuitar'
        # sung rather than shredded: long bent notes, space between the
        # phrases, one short climb at the top and a held bend to finish
        solo = [
            (0, 'B4', 8, 2), (10, 'A4', 2), (12, 'G4', 4),
            (16, 'A4', 6), (24, 'B4', 2), (26, 'A4', 2), (28, 'G4', 4),
            (32, 'E5', 8, 2), (42, 'D5', 2), (44, 'B4', 4),
            (48, 'A4', 4), (52, 'B4', 4), (56, 'F#4', 8, 1),
            (64, 'G5', 8, 2), (72, 'E5', 4), (76, 'D5', 4),
            (80, 'E5', 6), (88, 'D5', 2), (90, 'B4', 2), (92, 'A4', 4),
            (96, 'B4', 4), (100, 'D5', 4), (104, 'E5', 8, 2),
            (112, 'F#5', 12, 2),
        ]
        melody(s, b0, G_, solo, 0.8, transpose=-12)
        pickup(s, b0 + 7, 'funkBass', E, [0, 3, 5, 7])

    def breakdown(b0):
        """Eight bars: the drums drop out, pad and arp alone, the hook comes
        back softly, then the kick returns and builds under a riser."""
        for i in range(8):
            r, q = CHORUS[i % 4]
            long_pad(s, b0 + i, r + 24, q, vel=0.7)
            arp(s, b0 + i, r + 24, q, vel=0.3)
            if i >= 4:
                for st in range(0, 16, 4 if i < 6 else 2):
                    s.drum(b0 + i, st, 'k', 0.5 + 0.08 * (i - 4))
            if i >= 6:
                s.note(b0 + i, 0, 'bigBass', r, 16, 0.7)
        melody(s, b0 + 2, 'warmLead', HOOK, 0.6)
        melody(s, b0 + 6, 'lead80s', HOOK, 0.7)
        s.fx(b0 + 4, 0, 'riser', 4 * 1.7067, 0.7)

    verse(16)
    pre(24)
    chorus(32)
    interlude(s, 40, CHORUS, E)
    verse(44)
    pre(52)
    chorus(60)
    guitar_solo(68)
    breakdown(76)
    chorus(84, big=True)
    outro(s, 92, CHORUS)
    s.write(os.path.join('tools', 'music_sources', 'neon.json'), loop_bar=16, end_bar=96)


# ---- MIDNIGHT ACE: A major, rock verse, synthwave chorus, guitar and synth duet ----

def midnight():
    s = Song('midnight', 77)
    A, G, D, E, Fs, Cs, B = m('A1'), m('G1'), m('D2'), m('E2'), m('F#1'), m('C#2'), m('B1')
    VERSE = [(A, ''), (G, ''), (D, ''), (A, '')]       # mixolydian rock turn
    CHORUS = [(Fs, 'm'), (D, ''), (A, ''), (E, '')]    # the synthwave lift

    HOOK = [(0, 'E5', 2), (2, 'C#5', 1), (3, 'E5', 3), (6, 'F#5', 2), (8, 'E5', 4), (12, 'C#5', 2), (14, 'B4', 2),
            (16, 'A4', 6), (22, 'B4', 2), (24, 'C#5', 4), (28, 'E5', 4)]
    CH_A = [(0, 'C#5', 4), (4, 'A4', 2), (6, 'C#5', 2), (8, 'F#5', 6), (14, 'E5', 2),
            (16, 'D5', 6), (22, 'C#5', 2), (24, 'B4', 4), (28, 'A4', 4),
            (32, 'C#5', 4), (36, 'E5', 2), (38, 'A5', 2), (40, 'G#5', 6), (46, 'E5', 2),
            (48, 'B4', 8), (56, 'C#5', 4), (60, 'B4', 4)]
    CH_B = CH_A[:14] + [(48, 'B4', 4), (52, 'C#5', 4), (56, 'E5', 4), (60, 'A5', 4)]
    VL = [(0, 'A4', 2), (2, 'C#5', 1), (3, 'D5', 1), (4, 'E5', 4), (10, 'D5', 2), (12, 'B4', 2), (14, 'A4', 2),
          (32, 'G4', 2), (34, 'A4', 1), (35, 'B4', 1), (36, 'D5', 4), (42, 'C#5', 2), (44, 'A4', 4)]

    def rock_bass(bar, root, inst='funkBass', half_break=False):
        """Driving pop/rock bass: 8ths on the root, an octave jump on the
        'and' of 2 and a fifth leading into the next bar."""
        offs = [0, 0, 0, 12, 0, 0, 7, 10]
        for i, st in enumerate(range(0, 8 if half_break else 16, 2)):
            s.note(bar, st, inst, root + 12 + offs[i], 2, 0.95 if st in (0, 6) else 0.75)

    intro(s, CHORUS, 'lead80s', HOOK, A + 12)

    def verse(b0):
        for i in range(8):
            r, q = VERSE[i % 4]
            last = i == 7
            rock_beat(s, b0 + i, half_break=last, fill=i == 3, intensity=0.85)
            rock_bass(b0 + i, r, half_break=last)
            chug(s, b0 + i, r, half_break=last)
            for st in (2, 6, 10, 14) if i >= 4 else ():  # rock guitar chops from the second half
                if last and st >= 8:
                    break
                for tn in (r + 24, r + 31, r + 36):
                    s.note(b0 + i, st, 'powerChord', tn, 1, 0.7)
            ostinato(s, b0 + i, r + 24, q, vel=0.25)
        melody(s, b0 + 4, 'warmLead', VL, 0.75)
        answer(s, b0 + 4, VL, VERSE)
        pickup(s, b0 + 7, 'funkBass', A + 12, [0, 4, 7, 9])

    def pre(b0):
        """Eight bars of slow lift, as in Neon Strike."""
        s.fx(b0 + 4, 0, 'riser', 4 * 1.7067, 0.8)
        for i, (r, q) in enumerate([(D, ''), (E, ''), (Cs, 'm'), (E, '')] * 2):
            k = i / 7
            for st in (0, 8):
                s.drum(b0 + i, st, 'k', 0.8)
            s.drum(b0 + i, 8 if i < 4 else 4, 's', 0.45 + 0.3 * k)
            if i >= 4:
                s.drum(b0 + i, 12, 's', 0.45 + 0.3 * k)
            for st in range(0, 16, 4 if i < 4 else 2):
                s.drum(b0 + i, st, 'h', 0.35 + 0.3 * k)
            long_pad(s, b0 + i, r + 24, q, inst='brassChord', vel=0.45 + 0.35 * k)
            s.note(b0 + i, 0, 'bigBass', r + 12, 8, 0.8)
            s.note(b0 + i, 8, 'bigBass', r + 12, 8, 0.7)
            arp(s, b0 + i, r + 36, q, vel=0.2 + 0.2 * k)
        for st in range(8, 16):
            s.drum(b0 + 7, st, 's', 0.4 + 0.05 * (st - 8))

    def chorus(b0, last_block=True, big=False):
        crash(s, b0)
        for i in range(8):
            r, q = CHORUS[i % 4]
            end = i == 7 and last_block
            rock_beat(s, b0 + i, chorus=True, half_break=end, fill=i == 3)
            if i == 4:
                crash(s, b0 + i)
            pump_bass(s, b0 + i, r + 12, half_break=end)
            gated_pad(s, b0 + i, r + 24, q, vel=0.6)
            arp(s, b0 + i, r + 36, q, vel=0.25)
            if i % 4 == 0:
                stab(s, b0 + i, (0,), r + 12, q, length=4, vel=0.5)
        melody(s, b0, 'lead80s', CH_A, 0.85)
        melody(s, b0 + 4, 'lead80s', CH_B, 0.85)
        rhythm_guitar(s, b0, CHORUS, 8)
        if last_block:
            pickup(s, b0 + 7, 'bigBass', A + 12, [0, 4, 7, 9])

    def duet_solo(b0):
        """Eight bars where the guitar and the synth lead trade two-bar
        phrases over the chorus chords, then play the last two together
        a third apart, as two players who have been listening to each other."""
        for i in range(8):
            r, q = CHORUS[i % 4]
            end = i == 7
            rock_beat(s, b0 + i, chorus=True, half_break=end, fill=i == 3, intensity=0.85)
            rock_bass(b0 + i, r, half_break=end)
            long_pad(s, b0 + i, r + 24, q, inst='pad', vel=0.5)
        GTR = [(0, 'C#5', 6, 2), (8, 'B4', 2), (10, 'A4', 2), (12, 'F#4', 4),
               (16, 'A4', 4), (20, 'B4', 4), (24, 'C#5', 6, 1)]
        SYN = [(0, 'E5', 4), (4, 'D5', 2), (6, 'C#5', 2), (8, 'B4', 6), (14, 'A4', 2),
               (16, 'G#4', 8), (24, 'E4', 6)]
        melody(s, b0, 'wahGuitar', GTR, 1.0, transpose=-12)
        melody(s, b0 + 2, 'lead80s', SYN, 0.75)
        melody(s, b0 + 4, 'wahGuitar', GTR[:4] + [(16, 'E5', 4, 2), (20, 'C#5', 4), (24, 'B4', 6)], 1.0, transpose=-12)
        TOG = [(0, 'C#5', 4), (4, 'D5', 4), (8, 'E5', 8), (16, 'D5', 4), (20, 'C#5', 4), (24, 'B4', 8)]
        melody(s, b0 + 6, 'lead80s', TOG, 0.75)
        THIRDS = [(0, 'A4', 4), (4, 'B4', 4), (8, 'C#5', 8), (16, 'B4', 4), (20, 'A4', 4), (24, 'G#4', 8)]
        melody(s, b0 + 6, 'wahGuitar', THIRDS, 0.6, transpose=-12)
        pickup(s, b0 + 7, 'funkBass', A + 12, [0, 4, 7, 9])

    def breakdown(b0):
        for i in range(8):
            r, q = CHORUS[i % 4]
            long_pad(s, b0 + i, r + 24, q, vel=0.7)
            arp(s, b0 + i, r + 36, q, vel=0.3)
            if i >= 4:
                for st in range(0, 16, 4 if i < 6 else 2):
                    s.drum(b0 + i, st, 'k', 0.5 + 0.08 * (i - 4))
            if i >= 6:
                s.note(b0 + i, 0, 'bigBass', r + 12, 16, 0.7)
        melody(s, b0 + 2, 'warmLead', HOOK, 0.6)
        melody(s, b0 + 6, 'lead80s', HOOK, 0.7)
        s.fx(b0 + 4, 0, 'riser', 4 * 1.7067, 0.7)

    verse(16)
    pre(24)
    chorus(32)
    interlude(s, 40, CHORUS, A + 12)
    verse(44)
    pre(52)
    chorus(60)
    duet_solo(68)
    breakdown(76)
    chorus(84, big=True)
    outro(s, 92, CHORUS)
    s.write(os.path.join('tools', 'music_sources', 'midnight.json'), loop_bar=16, end_bar=96)


# ---- SIX O'CLOCK HIGH: A major, 1980s rock 'n' roll -------------------------------

A_MAJOR = [9, 11, 1, 2, 4, 6, 8]  # pitch classes A B C# D E F# G#


def diatonic(midi, steps, scale=A_MAJOR):
    """Moves a note by scale steps (a third below is -2)."""
    pcs = sorted(scale)
    pc, octv = midi % 12, midi // 12
    if pc not in pcs:
        return midi - 3 if steps < 0 else midi + 4
    i = pcs.index(pc) + steps
    octv += i // len(pcs)
    return octv * 12 + pcs[i % len(pcs)]


def rocknroll():
    """A whole song in the 80s rock 'n' roll manner, written so nothing
    announces itself: each section ends on the chord that leads into the
    next (V into I, IV into V), the bass walks up to the next root instead
    of jumping, the drums ramp their weight over a bar or two rather than
    switch, the pad and the guitars carry across the bar line, and the
    melody's pickups start the new section before its downbeat. On the
    C64 frame clock (5-5-6 frame 16ths, 8-bar blocks), with the C64 kick on 1
    and the 'and' of 2 and the snare on 2 and 4; the C64 stop-time (a
    drumless half bar with a bass pickup) is kept for two big moments."""
    s = Song('rocknroll', 1985)
    A, B, Cs, D, E, Fs, G, C = m('A1'), m('B1'), m('C#2'), m('D2'), m('E2'), m('F#2'), m('G2'), m('C2')
    VERSE = [(A, ''), (D, ''), (A, ''), (E, '')]
    PRE = [(Fs, 'm'), (D, ''), (B, 'm'), (E, '')]
    CHORUS = [(A, ''), (E, ''), (Fs, 'm'), (D, '')]
    BRIDGE = [(B, 'm'), (D, ''), (Fs, 'm'), (E, '')]

    def bass(bar, root, nxt, bounce=False, vel=0.9, half=False):
        """Rock 'n' roll bass on the kick: 8ths on the root (bouncing to the
        octave in the chorus); when the chord changes, the last two 8ths walk
        toward the next root, so the change is heard coming."""
        last = 8 if half else 16
        for st in range(0, last, 2):
            n = root + (12 if bounce and st % 4 == 2 else 0)
            if nxt is not None and nxt != root and st >= 12 and not half:
                d = nxt - root
                if d > 6:
                    d -= 12
                if d < -6:
                    d += 12
                n = root + (round(d / 3) if st == 12 else d - (1 if d > 0 else -1))
            s.note(bar, st, 'funkBass', n + 12, 2, vel * (1.0 if st in (0, 6) else 0.8))

    def chug(bar, root, vel, open_=False, half=False):
        """Palm-muted 8ths on root and fifth; open chords ring in the chorus."""
        for st in range(0, 8 if half else 16, 2):
            ln = 2 if open_ and st % 4 == 0 else 1
            for tn in (root + 12, root + 19, root + 24):
                s.note(bar, st, 'powerChord', tn, ln, vel * (1.0 if st % 4 == 0 else 0.72))

    def piano(bar, root, q, vel, push=False):
        """Rock piano: the chord on the off-beats, a push into the next bar."""
        tones = [t + 36 for t in chord_tones(root, q)]
        for st in (2, 6, 10, 14) if not push else (2, 6, 10, 13, 14):
            for tn in tones:
                s.note(bar, st, 'piano', tn, 1, vel)

    def riff(bar, vel=0.75):
        """The hook riff: A on palm-muted 8ths, then C and D, then G and D."""
        for i in range(2):
            for st in range(0, 12, 2):
                for tn in (A + 12, A + 19):
                    s.note(bar + i, st, 'powerChord', tn, 1, vel * (1.0 if st % 4 == 0 else 0.7))
            for st, r in ((12, C if i == 0 else G), (14, D)):
                for tn in (r + 12, r + 19, r + 24):
                    s.note(bar + i, st, 'powerChord', tn, 2, vel)

    def drums(bar, chorus, k, fill=None):
        """An MJ-style dance-rock groove (tight kick on 1 and 3, a crisp snare
        doubled by a hand clap on 2 and 4, busy but quiet 16th hats with a
        lilt, a shaker on the off-beats), with the C64 kick ghost on the
        'and' of 2. Verses keep it dry: 8th hats, no claps."""
        for st in (0, 8):
            s.drum(bar, st, 'k', (1.0 if st == 0 else 0.92) * k)
        s.drum(bar, 6, 'k', 0.45 * k)  # the C64 kick, as a ghost
        if chorus:
            s.drum(bar, 14, 'k', 0.5 * k)
        for st in (4, 12):
            s.drum(bar, st, 's', 0.9 * k)
            s.drum(bar, st, 'c', (0.75 if chorus else 0.4) * k)
        for st in range(0, 16, 1 if chorus else 2):
            acc = (0.55, 0.22, 0.4, 0.28)[st % 4] if chorus else (0.5 if st % 4 == 0 else 0.36)
            if chorus and st == 10:
                s.drum(bar, st, 'o', 0.45 * k)
            else:
                s.drum(bar, st, 'h', acc * k)
        for st in (2, 6, 10, 14):  # shaker on the off-beats
            s.drum(bar, st, 'g', (0.5 if chorus else 0.35) * k)
        if fill == 'small':
            for st in (13, 14, 15):
                s.drum(bar, st, 's', (0.45 + 0.1 * (st - 13)) * k)
        if fill == 'build':  # 8th snares rising into the next section
            for st in range(8, 16, 2):
                s.drum(bar, st, 's', k * (0.45 + 0.06 * (st - 8)))
        if fill == 'toms':
            for st, t in ((10, 'T1'), (12, 'T2'), (14, 'T3')):
                s.drum(bar, st, t, 0.65 * k)

    def stop_time(bar, next_root, root):
        """The C64 stop: the band hits beat 1 and lets go for the second
        half bar; the bass walks up alone into the next section."""
        s.drum(bar, 0, 'k', 0.9)
        s.drum(bar, 0, 'X', 0.55)
        run = [next_root - 5, next_root - 3, next_root - 1]
        for i, st in enumerate((10, 12, 14)):
            s.note(bar, st, 'funkBass', run[i] + 12, 2, 0.9)

    # ---- intro: the jet builds alone, and the riff lands as it passes (bars 0-7)
    s.fx(0, 8, 'jet', 8.5, 0.7, 0.75)  # loudest just as the riff comes in
    s.fx(2, 0, 'riser', 2 * 1.7067, 0.5)
    riff(4, 0.8)
    riff(6, 0.85)
    for i in range(4, 8):
        for st in range(0, 16, 2):
            s.drum(i, st, 'h', 0.3 + 0.05 * (i - 4))
        if i >= 5:
            drums(i, False, 0.6 + 0.1 * (i - 5), fill='toms' if i == 7 else None)
        bass(i, A, A if i < 7 else E, vel=0.6 + 0.06 * (i - 4))

    def arp_gaps(b0, bars, prog, phrases, vel):
        """A soft arpeggio that only plays where the melody rests, as an
        answer in the gaps rather than a layer on top."""
        busy = set()
        for at, ph in phrases:
            for item in ph:
                st0, ln = at * 16 + item[0], item[2]
                busy.update(range(st0, st0 + min(ln, 3)))  # free once a note is held
        for i in range(bars):
            rr, q = prog[i % len(prog)]
            tones = [t + 48 for t in chord_tones(rr, q)]
            seq = tones + [tones[0] + 12, tones[2], tones[1]]
            for st in range(16):  # a quick 16th figure, only in the holds and rests
                if (b0 + i) * 16 + st in busy:
                    continue
                s.note(b0 + i, st, 'softArp', seq[st % len(seq)], 1, vel * (1.0 if st % 4 == 0 else 0.7))

    # ---- a verse: holds back, the melody on top (16 bars)
    V1 = [(0, 'E4', 2), (2, 'A4', 2), (4, 'A4', 2), (6, 'B4', 2), (8, 'C#5', 4), (12, 'B4', 2), (14, 'A4', 2),
          (16, 'F#4', 4), (20, 'A4', 2), (22, 'F#4', 2), (24, 'E4', 6),
          (32, 'E4', 2), (34, 'A4', 2), (36, 'C#5', 2), (38, 'E5', 4), (42, 'C#5', 2), (44, 'B4', 4),
          (48, 'B4', 4), (52, 'G#4', 2), (54, 'E4', 2), (56, 'B4', 4), (60, 'A4', 2), (62, 'G#4', 2)]
    V2 = V1[:18] + [(48, 'B4', 4), (52, 'C#5', 2), (54, 'B4', 2), (56, 'E4', 8)]

    def verse(b0, second=False):
        for i in range(16):
            r, q = VERSE[i % 4]
            nr = VERSE[(i + 1) % 4][0] if i < 15 else PRE[0][0]
            ramp = 0.78 + (0.1 * (i - 13) if i >= 14 else 0)
            drums(b0 + i, False, ramp, fill='small' if i % 4 == 3 and i < 15 else ('build' if i == 15 else None))
            bass(b0 + i, r, nr)
            chug(b0 + i, r, 0.3 + (0.04 if i >= 8 else 0))
            if second or i >= 8:
                piano(b0 + i, r, q, 0.28, push=i % 4 == 3)
        for k in range(4):
            melody(s, b0 + 4 * k, 'warmLead', V2 if k % 2 else V1, 0.8)
        if second:
            arp_gaps(b0 + 8, 8, VERSE, [(b0 + 8, V1), (b0 + 12, V2)], 0.13)
        # the pad comes in under the last bars, already on the pre-chorus chord
        long_pad(s, b0 + 15, PRE[0][0] + 24, 'm', vel=0.3)

    # ---- the pre-chorus: rising, every bar a little fuller (4 bars)
    PRE_M = [(0, 'C#5', 6), (6, 'A4', 2), (8, 'C#5', 4), (12, 'F#5', 4),
             (16, 'F#5', 6), (22, 'E5', 2), (24, 'D5', 8),
             (32, 'D5', 4), (36, 'C#5', 2), (38, 'B4', 2), (40, 'D5', 8),
             (48, 'E5', 8), (56, 'D5', 2), (58, 'B4', 2), (60, 'G#4', 4)]

    def pre(b0):
        for i in range(4):
            r, q = PRE[i]
            nr = PRE[i + 1][0] if i < 3 else A
            k = 0.85 + 0.05 * i
            drums(b0 + i, i >= 2, k, fill='build' if i == 3 else None)
            bass(b0 + i, r, nr, vel=0.85 + 0.03 * i)
            chug(b0 + i, r, 0.34 + 0.03 * i, open_=i >= 2)
            long_pad(s, b0 + i, r + 24, q, inst='brassChord', vel=0.35 + 0.08 * i)
            piano(b0 + i, r, q, 0.3)
        melody(s, b0, 'warmLead', PRE_M, 0.85)
        arp_gaps(b0, 4, PRE, [(b0, PRE_M)], 0.16)

    # ---- the chorus: the band opens up (8 bars)
    CHORUS_A = [(0, 'A4', 2), (2, 'C#5', 2), (4, 'E5', 6), (10, 'C#5', 2), (12, 'E5', 4),
          (16, 'E5', 4), (20, 'D5', 2), (22, 'C#5', 2), (24, 'B4', 8),
          (32, 'C#5', 4), (36, 'C#5', 2), (38, 'D5', 2), (40, 'C#5', 4), (44, 'A4', 4),
          (48, 'A4', 4), (52, 'B4', 2), (54, 'A4', 2), (56, 'F#4', 4), (60, 'E4', 2), (62, 'G#4', 2)]
    CH2 = CHORUS_A[:14] + [(48, 'A4', 4), (52, 'F#4', 2), (54, 'A4', 2), (56, 'B4', 4), (60, 'C#5', 4)]

    def chorus(b0, then_root, harmony=False, stop=False):
        crash(s, b0)
        for i in range(8):
            r, q = CHORUS[i % 4]
            nr = CHORUS[(i + 1) % 4][0] if i < 7 else then_root
            last = i == 7
            if last and stop:
                stop_time(b0 + i, then_root, r)
                for tn in chord_tones(r, q):
                    s.note(b0 + i, 0, 'powerChord', tn + 12, 6, 0.8)
                continue
            drums(b0 + i, True, 1.0 if not last else 0.95, fill='small' if i == 3 else ('build' if last else None))
            bass(b0 + i, r, nr, bounce=True)
            chug(b0 + i, r, 0.45, open_=True)
            piano(b0 + i, r, q, 0.32, push=i % 4 == 3)
            long_pad(s, b0 + i, r + 24, q, inst='brassChord', vel=0.5)
        melody(s, b0, 'lead80s', CHORUS_A, 0.9)
        melody(s, b0 + 4, 'lead80s', CH2, 0.9)
        arp_gaps(b0, 8, CHORUS, [(b0, CHORUS_A), (b0 + 4, CH2)], 0.18)
        if harmony:  # the last chorus: a second voice a third below
            for ph, at in ((CHORUS_A, b0), (CH2, b0 + 4)):
                for st, nm, ln in ph:
                    bb, s16 = divmod(st, 16)
                    s.note(at + bb, s16, 'warmLead', diatonic(m(nm), -2), ln, 0.55)

    # ---- after the chorus: the riff carries the energy back down (4 bars)
    def riff_link(b0):
        for i in range(4):
            drums(b0 + i, False, 0.9 - 0.04 * i, fill='small' if i == 3 else None)
            bass(b0 + i, A, A if i < 3 else A, vel=0.85)
        riff(b0, 0.8)
        riff(b0 + 2, 0.75)

    # ---- the guitar solo over the verse chords (8 bars)
    SOLO = [(0, 'E5', 4, 2), (4, 'C#5', 2), (6, 'A4', 2), (8, 'B4', 4), (12, 'A4', 4),
            (16, 'F#4', 6), (22, 'A4', 2), (24, 'B4', 4), (28, 'C5', 2), (30, 'C#5', 2),
            (32, 'E5', 8, 2), (40, 'F#5', 4), (44, 'E5', 4),
            (48, 'D5', 4), (52, 'C#5', 4), (56, 'B4', 8, 1),
            (64, 'A5', 8, 2), (72, 'F#5', 4), (76, 'E5', 4),
            (80, 'F#5', 6), (86, 'E5', 2), (88, 'C#5', 4), (92, 'A4', 4),
            (96, 'B4', 4), (100, 'C#5', 4), (104, 'E5', 8, 2),
            (112, 'G#5', 6, 1), (118, 'F#5', 2), (120, 'D5', 8)]

    def solo(b0):
        for i in range(8):
            r, q = VERSE[i % 4]
            nr = VERSE[(i + 1) % 4][0] if i < 7 else BRIDGE[0][0]
            drums(b0 + i, True, 0.9 - (0.06 * (i - 6) if i >= 6 else 0), fill='small' if i == 3 else None)
            bass(b0 + i, r, nr, bounce=True, vel=0.85)
            chug(b0 + i, r, 0.36)
            long_pad(s, b0 + i, r + 24, q, inst='pad', vel=0.4)
        melody(s, b0, 'guitar', SOLO, 0.85, transpose=-12)

    # ---- the bridge: the floor drops softly, then builds back (8 bars)
    BR = [(0, 'D5', 8), (8, 'C#5', 4), (12, 'B4', 4), (16, 'A4', 8), (24, 'F#4', 8),
          (32, 'C#5', 8), (40, 'A4', 8), (48, 'B4', 8), (56, 'G#4', 8),
          (64, 'F#5', 8), (72, 'E5', 4), (76, 'D5', 4), (80, 'D5', 8), (88, 'A4', 8),
          (96, 'C#5', 12), (108, 'E5', 4), (112, 'E5', 8), (120, 'B4', 4), (124, 'G#4', 4)]

    def bridge(b0):
        for i in range(8):
            r, q = BRIDGE[i % 4]
            nr = BRIDGE[(i + 1) % 4][0] if i < 7 else A
            k = 0.55 + 0.06 * i
            s.drum(b0 + i, 0, 'k', k)
            s.drum(b0 + i, 8, 'k', k * 0.8)
            if i >= 2:
                s.drum(b0 + i, 12, 's', k * 0.8)
            for st in range(0, 16, 4 if i < 4 else 2):
                s.drum(b0 + i, st + (2 if i < 4 else 0), 'h', 0.3 + 0.03 * i)
            if i == 7:
                for st in range(8, 16, 2):
                    s.drum(b0 + i, st, 's', 0.5 + 0.06 * (st - 8))
            s.note(b0 + i, 0, 'funkBass', r + 12, 8, 0.75)
            s.note(b0 + i, 8, 'funkBass', (r if i < 7 else nr - 1) + 12, 8, 0.65)
            long_pad(s, b0 + i, r + 24, q, vel=0.55)
            for j, st in enumerate(range(0, 16, 2)):  # piano broken chords
                tones = [t + 36 for t in chord_tones(r, q)]
                s.note(b0 + i, st, 'piano', tones[j % 3] + (12 if j % 4 == 3 else 0), 2, 0.3)
        melody(s, b0, 'warmLead', BR, 0.8)
        arp_gaps(b0 + 4, 4, BRIDGE, [(b0, BR)], 0.15)

    def outro(b0):
        for i in range(4):
            drums(b0 + i, False, 0.85 - 0.12 * i)
            bass(b0 + i, A, A, vel=0.8 - 0.1 * i)
        riff(b0, 0.75)
        riff(b0 + 2, 0.6)
        arp_gaps(b0, 4, [(A, '')], [], 0.14 )
        crash(s, b0 + 4)
        for tn in (A + 12, A + 19, A + 24, A + 28):
            s.note(b0 + 4, 0, 'powerChord', tn, 16, 0.7)
        s.note(b0 + 4, 0, 'funkBass', A + 12, 16, 0.8)
        s.fx(b0 + 4, 0, 'jet', 6, 0.7, 0.5)

    verse(8)
    pre(24)
    chorus(28, A)
    riff_link(36)
    verse(40, second=True)
    pre(56)
    chorus(60, A, stop=True)
    solo(68)
    bridge(76)
    chorus(84, A)
    chorus(92, A, harmony=True)
    outro(100)
    s.write(os.path.join('tools', 'music_sources', 'rocknroll.json'), loop_bar=8, end_bar=106)


# ---- GHOST SQUADRON: E minor, rock 'n' roll synthwave pop -----------------------------

E_MINOR = [7, 9, 11, 0, 2, 4, 6]  # pitch classes of E natural minor


def gap_line(s, b0, bars, prog, phrases, inst, vel, step=2, octave=36):
    """A second voice that plays chord tones only where the melody holds a
    note or rests, so it answers rather than covers."""
    busy = set()
    for at, ph in phrases:
        for item in ph:
            st0 = at * 16 + item[0]
            busy.update(range(st0, st0 + min(item[2], 3)))
    for i in range(bars):
        r, q = prog[i % len(prog)]
        tones = [t + octave for t in chord_tones(r, q)]
        for j, st in enumerate(range(0, 16, step)):
            a = (b0 + i) * 16 + st
            if a in busy or a + 1 in busy:
                continue
            s.note(b0 + i, st, inst, tones[j % 3] + (12 if j % 4 == 3 else 0), step, vel * (1.0 if st % 4 == 0 else 0.75))


def ghost():
    """The tune first written for a sample kit (
    GhostSquadron), played by our own band as rock 'n' roll synthwave pop.
    Our C64 voice roles: the drum's kick on 1 and
    the 'and' of 2, the bass pumping the root with its lower neighbour, the
    pulse ostinato in off-beat 16th pairs, and SID leads with delayed
    vibrato. Over that, a rock band (snare on 2 and 4, palm-muted guitar,
    piano on the off-beats) and synthwave colour (gated pads, gated snare in
    the chorus). Sections lead into each other: each ends on the chord that
    resolves into the next, the bass walks up, the drums ramp."""
    s = Song('ghost', 2026)
    E, G, A, B, C, D = m('E2'), m('G2'), m('A2'), m('B1'), m('C2'), m('D2')
    VERSE = [(E, 'm'), (E, 'm'), (C, ''), (D, ''), (E, 'm'), (E, 'm'), (C, ''), (B, '')]
    LIFT = [(A, 'm'), (C, ''), (G, ''), (B, '')]
    CHORUS = [(C, ''), (D, ''), (E, 'm'), (E, 'm'), (C, ''), (D, ''), (B, ''), (B, '')]
    BREAK = [(A, 'm'), (A, 'm'), (E, 'm'), (E, 'm'), (C, ''), (C, ''), (B, ''), (B, '')]

    VA = [(0, 'B4', 4), (4, 'E5', 4), (8, 'D5', 2), (10, 'B4', 2), (12, 'G4', 4),
          (16, 'A4', 2), (18, 'B4', 2), (20, 'G4', 4), (24, 'E4', 8),
          (32, 'E4', 2), (34, 'G4', 2), (36, 'C5', 6), (42, 'B4', 2), (44, 'A4', 4),
          (48, 'D5', 4), (52, 'C5', 2), (54, 'B4', 2), (56, 'A4', 8),
          (64, 'B4', 4), (68, 'E5', 4), (72, 'F#5', 2), (74, 'E5', 2), (76, 'D5', 4),
          (80, 'B4', 8), (88, 'G4', 4), (92, 'A4', 4),
          (96, 'C5', 4), (100, 'B4', 2), (102, 'A4', 2), (104, 'G4', 4), (108, 'E4', 4),
          (112, 'D#4', 4), (116, 'F#4', 4), (120, 'B4', 8)]
    LM = [(0, 'C5', 6), (6, 'B4', 2), (8, 'A4', 4), (12, 'E5', 4), (16, 'E5', 6), (22, 'D5', 2), (24, 'C5', 4), (28, 'G4', 4),
          (32, 'B4', 4), (36, 'D5', 4), (40, 'G5', 8), (48, 'F#5', 8), (56, 'D#5', 4), (60, 'B4', 4)]
    CHM = [(0, 'G5', 6), (6, 'E5', 2), (8, 'G5', 4), (12, 'A5', 4), (16, 'F#5', 8), (24, 'D5', 4), (28, 'E5', 4),
           (32, 'B4', 8), (40, 'E5', 4), (44, 'G5', 4), (48, 'F#5', 12), (60, 'E5', 4),
           (64, 'G5', 6), (70, 'E5', 2), (72, 'G5', 4), (76, 'A5', 4), (80, 'B5', 8), (88, 'A5', 4), (92, 'F#5', 4),
           (96, 'D#5', 8), (104, 'F#5', 8), (112, 'B4', 16)]
    BRM = [(0, 'E5', 12), (12, 'C5', 4), (16, 'B4', 16), (32, 'G4', 8), (40, 'B4', 8), (48, 'E5', 16),
           (64, 'G5', 12), (76, 'E5', 4), (80, 'C5', 16), (96, 'D#5', 8), (104, 'F#5', 8), (112, 'B4', 16)]

    def bass(bar, root, nxt, vel=0.9, inst='funkBass'):
        """The C64 pump (root on 0, 2, 6, 8, 12, 14, the lower neighbour on 6
        and 14); on a chord change the last two notes walk to the next root."""
        for st in (0, 2, 6, 8, 12, 14):
            n = root - 2 if st in (6, 14) else root
            if nxt != root and st >= 12:
                d = nxt - root
                d = d - 12 if d > 6 else d + 12 if d < -6 else d
                n = root + (round(d / 2) if st == 12 else d - (1 if d > 0 else -1))
            s.note(bar, st, inst, n, 2, vel * (1.0 if st in (0, 6) else 0.75))

    def pulse(bar, root, q, vel):
        """The C64 pulse ostinato: off-beat 16th pairs, kept in the background."""
        tn = [t + 36 for t in chord_tones(root, q)]
        for i, st in enumerate((2, 3, 10, 11, 14, 15)):
            s.note(bar, st, 'softArp', tn[(i + 1) % 3], 1, vel * (1.0 if st % 2 == 0 else 0.7))

    def piano(bar, root, q, vel):
        for st in (2, 6, 10, 14):
            for tn in chord_tones(root, q):
                s.note(bar, st, 'piano', tn + 36, 1, vel)

    def band(b0, prog, nxt_root, k, chorus=False, ramp_to=None, ost=0.25):
        for i, (r, q) in enumerate(prog):
            nr = prog[i + 1][0] if i + 1 < len(prog) else nxt_root
            kk = k if ramp_to is None or i < len(prog) - 2 else k + (ramp_to - k) * (i - len(prog) + 3) / 3
            rock_beat(s, b0 + i, chorus=chorus, fill=i % 4 == 3, intensity=kk)
            if chorus:
                for st in (4, 12):
                    s.drum(b0 + i, st, 'S', 0.45 * kk)  # the synthwave gated snare under the crack
            bass(b0 + i, r, nr, 0.95 if chorus else 0.85)
            pulse(b0 + i, r, q, ost)
            if chorus:
                gated_pad(s, b0 + i, r + 24, q, vel=0.45)
                for st in range(0, 16, 2):  # open power chords ringing in 8ths
                    for tn in (r + 12, r + 19, r + 24):
                        s.note(b0 + i, st, 'powerChord', tn, 2 if st % 4 == 0 else 1, 0.42 if st % 4 == 0 else 0.32)
                piano(b0 + i, r, q, 0.26)
            else:
                chug(s, b0 + i, r, vel=0.3)

    # ---- intro (0-7): the jet builds alone; the pulse, bass and beat join under it
    s.fx(0, 8, 'jet', 8.5, 0.7, 0.75)
    s.fx(2, 0, 'riser', 2 * 1.7067, 0.45)
    for i in range(8):
        r, q = VERSE[i]
        nr = VERSE[i + 1][0] if i < 7 else E
        if i >= 2:
            pulse(i, r, q, 0.12 + 0.03 * (i - 2))
        if i >= 4:
            bass(i, r, nr, 0.6 + 0.07 * (i - 4))
            for st in range(0, 16, 2):
                s.drum(i, st, 'h', 0.25 + 0.05 * (i - 4))
        if i >= 6:
            rock_beat(s, i, fill=i == 7, intensity=0.55 + 0.15 * (i - 6))
    long_pad(s, 0, E + 24, 'm', bars=4, vel=0.3)
    long_pad(s, 4, C + 24, '', bars=4, vel=0.35)
    melody(s, 6, 'sidLead', [(0, 'B4', 4), (4, 'E5', 4), (8, 'D5', 2), (10, 'B4', 2), (12, 'G4', 4), (16, 'A4', 4), (20, 'B4', 12)], 0.5)

    # ---- verse, lift, chorus
    band(8, VERSE, E, 0.75)
    band(16, VERSE, A, 0.78, ramp_to=0.9)
    melody(s, 8, 'sidLead', VA, 0.8)
    melody(s, 16, 'warmLead', VA, 0.8)
    gap_line(s, 16, 8, VERSE, [(16, VA)], 'guitar', 0.3)
    band(24, LIFT, C, 0.88, ramp_to=1.0, ost=0.3)
    for i, (r, q) in enumerate(LIFT):
        long_pad(s, 24 + i, r + 24, q, inst='brassChord', vel=0.35 + 0.08 * i)
    melody(s, 24, 'warmLead', LM, 0.85)
    crash(s, 28)
    band(28, CHORUS, A, 1.0, chorus=True, ramp_to=0.65, ost=0.3)
    melody(s, 28, 'lead80s', CHM, 0.9)
    gap_line(s, 28, 8, CHORUS, [(28, CHM)], 'softArp', 0.2, step=1, octave=48)

    # ---- break (36-43): the floor drops softly; a brass voice sings, then it builds
    for i, (r, q) in enumerate(BREAK):
        nr = BREAK[i + 1][0] if i < 7 else E
        k = 0.5 + 0.06 * i
        s.drum(36 + i, 0, 'k', k)
        if i >= 2:
            s.drum(36 + i, 8, 'k', k * 0.8)
            s.drum(36 + i, 12, 's', k * 0.8)
        for st in range(0, 16, 4 if i < 4 else 2):
            s.drum(36 + i, st + (2 if i < 4 else 0), 'h', 0.25 + 0.03 * i)
        if i == 7:
            for st in range(8, 16, 2):
                s.drum(43, st, 's', 0.5 + 0.06 * (st - 8))
        s.note(36 + i, 0, 'funkBass', r, 8, 0.7)
        s.note(36 + i, 8, 'funkBass', r if i < 7 else nr - 1, 8, 0.6)
        pulse(36 + i, r, q, 0.2)
        long_pad(s, 36 + i, r + 24, q, vel=0.5)
    melody(s, 36, 'brassLead', BRM, 0.75, transpose=-12)
    gap_line(s, 40, 4, BREAK[4:], [(36, BRM)], 'softArp', 0.18, step=1, octave=48)

    # ---- verse, lift, two choruses
    band(44, VERSE, A, 0.8, ramp_to=0.9)
    melody(s, 44, 'sidLead', VA, 0.85)
    gap_line(s, 44, 8, VERSE, [(44, VA)], 'guitar', 0.32)
    band(52, LIFT, C, 0.9, ramp_to=1.0, ost=0.3)
    for i, (r, q) in enumerate(LIFT):
        long_pad(s, 52 + i, r + 24, q, inst='brassChord', vel=0.38 + 0.08 * i)
    melody(s, 52, 'warmLead', LM, 0.88)
    crash(s, 56)
    band(56, CHORUS, C, 1.0, chorus=True, ost=0.32)
    crash(s, 64)
    band(64, CHORUS, E, 1.0, chorus=True, ramp_to=0.7, ost=0.34)
    for b in (56, 64):
        melody(s, b, 'lead80s', CHM, 0.92)
        gap_line(s, b, 8, CHORUS, [(b, CHM)], 'softArp', 0.2, step=1, octave=48)
    for st, nm, ln in CHM:  # the last chorus: a second voice a third below
        bb, s16 = divmod(st, 16)
        s.note(64 + bb, s16, 'warmLead', diatonic(m(nm), -2, E_MINOR), ln, 0.55)

    # ---- outro (72-79): the verse chords, the beat thinning, the jet passing over
    for i, (r, q) in enumerate(VERSE):
        nr = VERSE[i + 1][0] if i < 7 else E
        if i < 6:
            rock_beat(s, 72 + i, intensity=0.75 - 0.1 * i)
        bass(72 + i, r, nr, 0.75 - 0.06 * i)
        pulse(72 + i, r, q, 0.28 - 0.025 * i)
    melody(s, 72, 'sidLead', VA[:18], 0.6)
    long_pad(s, 76, E + 24, 'm', bars=4, vel=0.45)
    s.fx(76, 0, 'jet', 7, 0.6, 0.5)
    s.drum(80, 0, 'k', 0.6)
    s.drum(80, 0, 'X', 0.5)
    s.note(80, 0, 'funkBass', E, 16, 0.6)
    s.write(os.path.join('assets', 'music', 'ghost.json'), loop_bar=8, end_bar=82)


# ---- STEEL HORIZON (FILM): C minor, dark heavy rock, timed to the opening film -------------

C_MINOR = [0, 2, 3, 5, 7, 8, 10]


def horizon_film():
    """The opening film's music (assets/intro/src/build_script.js times its
    cuts to these bars): darker and heavier than the menu's Steel Horizon,
    in C minor, on the C64 pattern (kick on 1 and the 'and' of 2, snare on
    2 and 4, the bass pumping the root with its lower neighbour, off-beat
    pulse pairs).
      bars 0-6    the jet builds over a low drone and a tom heartbeat
      bar 7       the band hits as the jets leave the bow (11.99 s)
      bars 7-12   the low run: the band holds back, a dark guitar riff
      bar 13      the merge: the dogfight theme, the band at full weight
      bar 25      a snare build; bar 26 the last kill, a huge hit
      bars 26-29  half-time anthem while the smoke spells the title
      bars 30-31  the title dissolves; the band falls away
      bars 32-47  the menu groove, looped"""
    s = Song('horizon-intro', 1987)
    C, Ab, Bb, G, Eb, F = m('C2'), m('Ab1'), m('Bb1'), m('G1'), m('Eb2'), m('F2')
    RUN = [(C, 'm'), (C, 'm'), (Ab, ''), (Bb, ''), (C, 'm'), (G, '')]
    FIGHT = [(C, 'm'), (Bb, ''), (Ab, ''), (G, '')]
    ANTHEM = [(Ab, ''), (Bb, ''), (C, 'm'), (C, 'm')]
    MENU = [(C, 'm'), (Ab, ''), (Eb, ''), (Bb, '')]

    def bass(bar, root, nxt, vel=0.95):
        for st in (0, 2, 6, 8, 12, 14):
            n = root - 2 if st in (6, 14) else root
            if nxt != root and st >= 12:
                d = nxt - root
                d = d - 12 if d > 6 else d + 12 if d < -6 else d
                n = root + (round(d / 2) if st == 12 else d - (1 if d > 0 else -1))
            s.note(bar, st, 'funkBass', n, 2, vel * (1.0 if st in (0, 6) else 0.75))

    def pulse(bar, root, q, vel):
        tn = [t + 36 for t in chord_tones(root, q)]
        for i, st in enumerate((2, 3, 10, 11, 14, 15)):
            s.note(bar, st, 'softArp', tn[(i + 1) % 3], 1, vel * (1.0 if st % 2 == 0 else 0.7))

    def heavy(bar, root, vel, open_=True):
        """Low power chords, root-fifth-octave, ringing on the beat."""
        for st in range(0, 16, 2):
            for tn in (root + 12, root + 19, root + 24):
                s.note(bar, st, 'powerChord', tn, 2 if open_ and st % 4 == 0 else 1, vel if st % 4 == 0 else vel * 0.75)

    def band(b0, prog, nxt_root, k, chorus, ramp_to=None):
        for i, (r, q) in enumerate(prog):
            nr = prog[i + 1][0] if i + 1 < len(prog) else nxt_root
            kk = k if ramp_to is None or i < len(prog) - 2 else k + (ramp_to - k) * (i - len(prog) + 3) / 3
            rock_beat(s, b0 + i, chorus=chorus, fill=i % 4 == 3, intensity=kk)
            if chorus:
                for st in (4, 12):
                    s.drum(b0 + i, st, 'S', 0.5 * kk)
            bass(b0 + i, r, nr)
            pulse(b0 + i, r, q, 0.22 if chorus else 0.18)
            heavy(b0 + i, r, 0.48 if chorus else 0.34, open_=chorus)
            if chorus:
                gated_pad(s, b0 + i, r + 24, q, vel=0.42)

    # ---- the build (0-6): a low drone, a heartbeat on the toms, the jet
    s.fx(0, 0, 'jet', 12.2, 1, 0.92)
    s.fx(4, 0, 'riser', 3 * 1.7067, 0.6)
    for i in range(7):
        s.note(i, 0, 'bigBass', C, 16, 0.5 + 0.05 * i)
        if i % 2 == 0:
            s.drum(i, 0, 'P', 0.5 + 0.07 * i)
        if i >= 2:
            for st in (0, 6):
                s.drum(i, st, 'G3', 0.35 + 0.07 * (i - 2))
        if i >= 4:
            s.drum(i, 8, 'G2', 0.3 + 0.08 * (i - 4))
    long_pad(s, 0, C + 24, 'm', bars=4, vel=0.35)
    long_pad(s, 4, Ab + 24, '', bars=2, inst='brassChord', vel=0.4)
    long_pad(s, 6, G + 24, '', bars=1, inst='brassChord', vel=0.5)
    for st in range(0, 16):  # the snare roll into the hit
        s.drum(6, st, 's', 0.25 + 0.04 * st)

    # ---- the hit and the low run (7-12)
    crash(s, 7)
    s.drum(7, 0, 'P', 1.0)
    band(7, RUN, C, 0.82, False, ramp_to=0.95)
    RIFF = [(0, 'C4', 2), (2, 'C4', 1), (3, 'Eb4', 1), (4, 'C4', 2), (6, 'Bb3', 2), (8, 'C4', 2), (10, 'G3', 2),
            (12, 'Bb3', 2), (14, 'B3', 2), (16, 'C4', 4), (20, 'Eb4', 2), (22, 'F4', 2), (24, 'G4', 4), (28, 'F4', 2),
            (30, 'Eb4', 2)]
    for b in (7, 9, 11):
        melody(s, b, 'wahGuitar', RIFF, 0.9, transpose=-12)

    # ---- the merge and the dogfight (13-24)
    TH = [(0, 'G4', 6), (6, 'C5', 2), (8, 'Eb5', 4), (12, 'D5', 4), (16, 'D5', 6), (22, 'Bb4', 2), (24, 'C5', 4),
          (28, 'D5', 4), (32, 'Eb5', 6), (38, 'D5', 2), (40, 'C5', 4), (44, 'Ab4', 4), (48, 'G4', 8), (56, 'B4', 4),
          (60, 'D5', 4)]
    TH2 = [(0, 'G5', 6), (6, 'F5', 2), (8, 'Eb5', 4), (12, 'D5', 4), (16, 'F5', 6), (22, 'Eb5', 2), (24, 'D5', 4),
           (28, 'Bb4', 4), (32, 'C5', 6), (38, 'D5', 2), (40, 'Eb5', 4), (44, 'F5', 4), (48, 'G5', 8), (56, 'F5', 4),
           (60, 'D5', 4)]
    crash(s, 13)
    band(13, FIGHT * 3, C, 1.0, True)
    for b, ph in ((13, TH), (17, TH2), (21, TH)):
        melody(s, b, 'lead80s', ph, 0.92)
        gap_line(s, b, 4, FIGHT, [(b, ph)], 'softArp', 0.2, step=1, octave=48)

    # ---- the build and the last kill (25-29)
    rock_beat(s, 25, chorus=True, intensity=0.9)
    for st in range(8, 16):
        s.drum(25, st, 's', 0.45 + 0.06 * (st - 8))
    bass(25, G, Ab)
    heavy(25, G, 0.5)
    long_pad(s, 25, G + 24, '', inst='brassChord', vel=0.55)
    crash(s, 26)
    s.drum(26, 0, 'P', 1.0)
    for i, (r, q) in enumerate(ANTHEM):  # half time: kick on 1, the gated snare on 3
        b = 26 + i
        s.drum(b, 0, 'k', 1.0)
        s.drum(b, 10, 'k', 0.6)
        s.drum(b, 8, 'S', 0.9)
        for st in range(0, 16, 4):
            s.drum(b, st + 2, 'h', 0.35)
        s.note(b, 0, 'funkBass', r, 8, 0.9)
        s.note(b, 8, 'funkBass', r, 8, 0.8)
        for tn in (r + 12, r + 19, r + 24):
            s.note(b, 0, 'powerChord', tn, 16, 0.5)
        long_pad(s, b, r + 24, q, inst='brassChord', vel=0.6)
        gated_pad(s, b, r + 36, q, vel=0.3)
    melody(s, 26, 'lead80s', [(0, 'C5', 8), (8, 'Eb5', 8), (16, 'D5', 8), (24, 'F5', 8), (32, 'G5', 16), (48, 'G5', 16)], 0.95)
    for st, nm, ln in [(0, 'C5', 8), (8, 'Eb5', 8), (16, 'D5', 8), (24, 'F5', 8), (32, 'G5', 16), (48, 'G5', 16)]:
        bb, s16 = divmod(st, 16)
        s.note(26 + bb, s16, 'warmLead', diatonic(m(nm), -2, C_MINOR), ln, 0.5)

    # ---- the title dissolves (30-31): the band falls away to the drone and the pulse
    for i, (r, q) in enumerate([(Ab, ''), (Bb, '')]):
        b = 30 + i
        s.drum(b, 0, 'k', 0.6 - 0.15 * i)
        s.drum(b, 8, 'G3', 0.4)
        s.note(b, 0, 'funkBass', r, 16, 0.65)
        long_pad(s, b, r + 24, q, vel=0.5)
        pulse(b, r, q, 0.2)

    # ---- the menu groove (32-47), looped
    ML = [(0, 'G4', 4), (4, 'C5', 4), (8, 'Bb4', 2), (10, 'G4', 2), (12, 'Ab4', 4), (16, 'Ab4', 4), (20, 'C5', 4),
          (24, 'Eb5', 4), (28, 'C5', 4), (32, 'Bb4', 6), (38, 'G4', 2), (40, 'Eb5', 4), (44, 'D5', 4), (48, 'D5', 8),
          (56, 'Bb4', 4), (60, 'F4', 4)]
    band(32, MENU * 2, C, 0.8, False, ramp_to=0.95)
    band(40, MENU * 2, C, 0.95, True, ramp_to=0.8)
    for b in (32, 36):
        melody(s, b, 'sidLead', ML, 0.8)
    for b in (40, 44):
        melody(s, b, 'lead80s', ML, 0.88, transpose=12 if b == 44 else 0)
        gap_line(s, b, 4, MENU, [(b, ML)], 'softArp', 0.18, step=1, octave=48)
    s.write(os.path.join('assets', 'music', 'horizon-intro.json'), loop_bar=32, end_bar=48)


if __name__ == '__main__':
    neon()
    midnight()
    rocknroll()
    ghost()
    horizon_film()
