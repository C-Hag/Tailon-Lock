#!/usr/bin/env python3
"""fullburn_score.py - writes the main theme, Full Burn "Arp sound DMX10", as scores for the game's own synth
(assets/music/fullburn.json and fullburn-intro.json), so the game plays it live instead of from a recording.
Original code for this project; the MIDI file is read as the Standard MIDI File layout describes it.

    python3 tools/fullburn_score.py      # reads music/fullburn/, writes assets/music/fullburn*.json

The parts come from the song itself: every note from music/fullburn/fullburn-dmx10.mid (one track a part, on a
120 BPM grid of 2 s a bar) and the drummer's performance from music/fullburn/song/drum-hits.json (seconds in the
master). The SID tunes run on the PAL frame clock, 6 frames a 16th, so a bar is 96 / 50.1245 s and bar 1 starts
PH seconds into the master; bar 33 is the loop start A of the old recording (fullburn.loop).

The instruments are the synth's fb* patches (src/synth.c) and the drums its h2h-style kit (codes "f..."), as the
ear-safe mix (DMX10 ear) has them: the bell clock 4 dB down with a 2.9 kHz cut and a one-bar fade-in where it
enters, the buzz-arp lead dipped at 2.4 kHz. The game's own SID drums play the opening, as the SID chip did.

fullburn.json is the whole song; the game loops bars 33-72 (61.4-138.0 s, the old recording's loop), and
--render-music with A4C_MUSIC_FULL=1 plays it to the end. fullburn-intro.json is the opening film's cut, as the
old fullburn-intro.wav: half a second of silence, then the song from the top for 79.1 s (bars 1-40 and the first
of 41), the last 2.5 s faded out; it plays once, and the menu's Full Burn carries on from it at the same place.
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'music', 'fullburn')
OUT = os.path.join(ROOT, 'assets', 'music')

BAR = 96 / 50.124542                  # 1.91523 s: 16 sixteenths of 6 PAL frames
LOOP_A, LOOP_B = 2947080, 6624287     # the old recording's loop, frames at 48 kHz (bar 33 to the end of bar 72)
PH = LOOP_A / 48000 - 32 * BAR        # where bar 1 starts in the master
LENGTH = 198.957                      # the master's length, with the last chord's ring
EARLY = 0.04                          # the loop is taken this much early: the drummer plays the downbeat a touch ahead
INTRO_LEAD, INTRO_BODY, INTRO_FADE = 0.5, (2547470 - 16000) / 32000, 2.5
# The film's cut plays the opening's build longer, for the longer launch (version 9 of the film): bars 1-4, then the
# arpeggios of bars 3-4 twice more before the kicks come in, then bars 5-6 three times before the band hit on bar 7,
# then the song on from bar 7 as before. Each part is cut a sixteenth's ghost (60 ms) before its bar line, so the
# drummer's early downbeats stay with their bar. INTRO_SHIFT is how far the film's cut runs behind the song after
# the repeats: the menu's Full Burn takes over that much earlier in the song than the film's own clock.
INTRO_CUT = [(1, 5), (3, 5), (3, 5), (5, 7), (5, 7), (5, 7), (7, None)]
CUT_EARLY = 0.06
INTRO_SHIFT = sum(b - a for a, b in INTRO_CUT if b) * BAR - (INTRO_CUT[-1][0] - 1) * BAR

# MIDI track name (its first word) -> instrument, by part; None: handled on its own
PARTS = {'02': 'bass', '03': 'sidLead', '04': 'fbArp', '05': 'fbSidPad', '07': 'fbGtrL', '08': 'fbGtrR',
         '09': 'fbBuzz', '12': 'fbSyncl', '13': 'fbBell', '11': 'fbJuno'}
MOOG_FROM = 30.0                      # the bass is the SID's in the opening, the Moog from 0:30
# the opening's SID drums (GM numbers) -> the synth's own SID kit
SID_DRUMS = {35: 'k', 36: 'k', 38: 's', 40: 's', 42: 'h', 44: 'h', 46: 'o', 41: 'T3', 43: 'T3', 45: 'T2', 47: 'T2',
             48: 'T1', 50: 'T1', 49: 'c', 57: 'c'}
# the drummer's pieces -> the h2h-style kit
KIT = {'kick': 'fk', 'snare': 'fs', 'ghost': 'fg', 'xstick': 'fx', 'hat_c': 'fh', 'hat_p': 'fp', 'hat_o': 'fo',
       'tom1': 'ft1', 'tom2': 'ft2', 'tom3': 'ft3', 'crash1': 'fc1', 'crash2': 'fc2', 'ride': 'fh', 'bell': 'fh'}


def vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 127)
        if b < 128:
            return v, i


def read_midi(path):
    """[(track name, [(start beat, midi note, beats, velocity)])]"""
    d = open(path, 'rb').read()
    assert d[:4] == b'MThd'
    ntracks, div = int.from_bytes(d[10:12], 'big'), int.from_bytes(d[12:14], 'big')
    i, tracks = 14, []
    for _ in range(ntracks):
        assert d[i:i + 4] == b'MTrk'
        end = i + 8 + int.from_bytes(d[i + 4:i + 8], 'big')
        j, t, run, name, on, notes = i + 8, 0, 0, '', {}, []
        while j < end:
            dt, j = vlq(d, j)
            t += dt
            b = d[j]
            if b == 0xff:
                n, k = vlq(d, j + 2)
                if d[j + 1] == 3:
                    name = d[k:k + n].decode('latin-1')
                j = k + n
                continue
            if b in (0xf0, 0xf7):
                n, k = vlq(d, j + 1)
                j = k + n
                continue
            if b & 0x80:
                run = b
                j += 1
            kind, nd = run >> 4, 1 if run >> 4 in (0xc, 0xd) else 2
            a, v = d[j], d[j + 1] if nd == 2 else 0
            j += nd
            if kind == 9 and v:
                on.setdefault(a, []).append((t, v))
            elif kind == 8 or kind == 9:
                if on.get(a):
                    t0, v0 = on[a].pop(0)
                    notes.append((t0 / div, a, (t - t0) / div, v0))
        tracks.append((name, sorted(notes)))
        i = end
    return tracks


def sec(beat):
    """a beat of the MIDI grid (2 a second, 4 a bar) -> seconds in the master"""
    return PH + beat * BAR / 4


def r4(x):
    return round(x, 4)


def build():
    notes, events = [], []
    for name, ns in read_midi(os.path.join(SRC, 'fullburn-dmx10.mid')):
        part = name.split(' ')[0]
        if part == '01':                                    # the opening's SID drums
            for b, m, _, v in ns:
                events.append([r4(sec(b)), 'drum', 1, SID_DRUMS.get(m, 'h'), r4(0.37 * v / 127)])
        elif part == '06':                                  # the SID jet under the opening: one long pass
            events.append([r4(sec(ns[0][0])), 'jet', 1, r4(sec(ns[-1][0] + ns[-1][2]) - sec(ns[0][0])), 0.25, 0.85])
        elif part == '20':                                  # the jet fly-off at the end
            events.append([r4(sec(ns[0][0])), 'jet', 1, 6.0, 0.25, 0.15])
        elif part in PARTS:
            inst = PARTS[part]
            t_in = sec(ns[0][0]) if ns else 0
            for b, m, l, v in ns:
                t, vel = sec(b), v / 127
                ins = inst if inst != 'bass' else ('fbSidBass' if t < MOOG_FROM else 'fbMoog')
                if inst == 'fbBell' and t < t_in + BAR:     # the ear-safe mix's one-bar fade-in of the bell clock
                    u = (t - t_in) / BAR
                    vel *= 0.5 - 0.5 * math.cos(math.pi * max(0.05, u))
                notes.append([r4(t), ins, m, r4(l * BAR / 4 * 0.98), r4(vel)])
    hits = json.load(open(os.path.join(SRC, 'song', 'drum-hits.json')))['hits']
    for t, p, v in hits:
        events.append([r4(t), 'drum', 1, KIT[p], r4(v)])
    notes.sort(key=lambda n: (n[0], n[1], n[2]))
    events.sort(key=lambda e: (e[0], str(e[3])))
    return notes, events


def fade(t):
    """the film cut's level at song time t: full, then a raised-cosine fade over its last 2.5 s"""
    u = (t - (INTRO_BODY - INTRO_FADE)) / INTRO_FADE
    return 1.0 if u <= 0 else 0.0 if u >= 1 else 0.5 + 0.5 * math.cos(math.pi * u)


def write(path, song):
    with open(path, 'w') as f:
        json.dump(song, f, separators=(',', ':'))
    print('wrote %s: %d notes, %d events, %.0f KB' % (os.path.relpath(path, ROOT), len(song['notes']),
                                                      len(song['events']), os.path.getsize(path) / 1024))


def main():
    notes, events = build()
    write(os.path.join(OUT, 'fullburn.json'),
          dict(key='fullburn', length=LENGTH, loop=r4(LOOP_A / 48000 - EARLY), loop_end=r4(LOOP_B / 48000 - EARLY),
               events=events, notes=notes))
    n2, e2 = [], []
    out = 0.0                                               # where the part starts in the film's cut (song seconds)
    for k, (a, b) in enumerate(INTRO_CUT):
        s0 = 0.0 if a == 1 else PH + (a - 1) * BAR - CUT_EARLY
        s1 = INTRO_BODY if b is None else PH + (b - 1) * BAR - CUT_EARLY
        dt = out - s0                                       # song time to the film's cut, in this part
        for n in notes:
            g = fade(n[0])
            if s0 <= n[0] < s1 and n[0] < INTRO_BODY and g > 0.02:
                n2.append([r4(n[0] + dt + INTRO_LEAD), n[1], n[2], r4(min(n[3], s1 - n[0])), r4(n[4] * g) if g < 1 else n[4]])
        for e in events:
            g = fade(e[0])
            if s0 <= e[0] < s1 and e[0] < INTRO_BODY and g > 0.02:
                e = list(e)
                if e[1] == 'jet' and k == 0:                # the jet under the opening builds over the whole longer build
                    e[3] = r4(e[3] + INTRO_SHIFT)
                e[0] = r4(e[0] + dt + INTRO_LEAD)
                e[2] = r4(e[2] * g) if g < 1 else e[2]
                e2.append(e)
        out += s1 - s0
    n2.sort(key=lambda n: (n[0], n[1], n[2]))
    e2.sort(key=lambda e: (e[0], str(e[3])))
    write(os.path.join(OUT, 'fullburn-intro.json'),
          dict(key='fullburn-intro', length=r4(INTRO_LEAD + INTRO_BODY + INTRO_SHIFT + 3), loop=0, once=1, events=e2, notes=n2))
    print('film cut: %.4f s longer than the song (%d repeated bars)' % (INTRO_SHIFT, round(INTRO_SHIFT / BAR)))


if __name__ == '__main__':
    main()
