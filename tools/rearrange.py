#!/usr/bin/env python3
"""rearrange.py - plays every theme the way GHOST SQUADRON is played.

Each song keeps its own melody, chords and key; the band and the rules
around them come from GHOST SQUADRON (tools/write_synthwave.py, ghost()):

- the C64 frame clock (16ths of 5, 5 and 6 frames, a bar of 1.7067 s) and
  the C64 voice roles: the kick on 1 and the 'and' of 2, the bass pumping
  the root with its lower neighbour, the pulse ostinato in off-beat 16th
  pairs, SID leads with delayed vibrato;
- rock 'n' roll: snare on 2 and 4, palm-muted guitar under the verses,
  piano on the off-beats and open power chords in the choruses;
- synthwave colour: gated pads and a gated snare under the chorus crack;
- the jet builds an 8-bar intro (no whistle) and passes over the outro;
- sections lead into each other: the bass walks up to the next root, the
  drums ramp over the last bars, a lift swells before each chorus;
- a guitar and a soft arpeggio answer only in the gaps of the melody, and
  the last chorus adds a second voice a third below.

The sources are the earlier scores in tools/music_sources/ (the three
synthwave ones come from write_synthwave.py). From each it finds the beat
grid, the chord of every bar (bass root and the third above it), the key,
and the melody (the top line of the lead voices), then writes the new
score to assets/music/<key>.json.

    python3 tools/rearrange.py [key ...]
"""
import json
import math
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(__file__))
from write_synthwave import (Song, step_time, rock_beat, crash, chord_tones, gated_pad, long_pad, melody, chug, gap_line,
                             diatonic)

SRC = os.path.join(os.path.dirname(__file__), 'music_sources')
KEYS = ['horizon', 'pop', 'h2h', 'anthem', 'strike', 'cine', 'chip', 'overtone', 'neon', 'midnight', 'rocknroll']
BASSES = {'aceBass', 'bigBass', 'seqBass', 'bass', 'funkBass'}
LEADS = {'synthLead', 'sidLead', 'pulseLead', 'lead', 'lead80s', 'warmLead', 'wahGuitar', 'brassLead', 'guitar'}
# the synthwave sources open on their own 16-bar jet-and-whistle intro and close
# on an outro: the new arrangement brings its own, so those bars are left out
SKIP = {'neon': 16, 'midnight': 16, 'rocknroll': 4}
TAIL = {'neon': 4, 'midnight': 4, 'rocknroll': 6}


# ---- analysis -------------------------------------------------------------------------

C64_16TH = 16 * 16 / 3 / 50 / 16  # the C64 frame-clock 16th on average (5, 5, 6 frames)


def c64_index(t):
    """The fractional 16th index of time t on the C64 frame clock (from 0)."""
    n = int(t / C64_16TH)
    for k in (n - 1, n, n + 1, n + 2):
        a, b = step_time(k), step_time(k + 1)
        if a <= t < b:
            return k + (t - a) / (b - a)
    return t / C64_16TH


def grid(notes):
    """The beat grid: the C64 frame clock (uneven 16ths) or an even 16th. Returns
    (to_step, s16): to_step(t) gives the fractional 16th index from the first
    downbeat."""
    on = sorted(n[0] for n in notes if n[1] in BASSES or n[1] in LEADS or n[1] in ('piano', 'powerChord'))
    if not on:
        on = sorted(n[0] for n in notes)
    first = on[0]

    def fit(to_step):
        ok = 0
        for t in on:
            x = to_step(t)
            ok += abs(x - round(x)) < 0.14
        return ok / len(on)

    cands = []
    for k in range(16):  # the C64 frame clock, started at the first note, with its 16th phase
        base = first - step_time(k) - 0.0
        cands.append((fit(lambda t, b=base: c64_index(t - b + 1e-9)), 'c64', C64_16TH, base))
    best_even = None
    for k in range(900, 1700, 2):
        s16 = k / 10000
        f = fit(lambda t, s=s16: (t - first) / s)
        if best_even is None or f > best_even[0] + 0.004:
            best_even = (f, 'even', s16, first)
    # a coarser even grid that fits as well is the real one
    f, _, s16, _ = best_even
    for mul in (2,):
        if s16 * mul <= 0.17:
            f2 = fit(lambda t, s=s16 * mul: (t - first) / s)
            if f2 >= f - 0.03:
                best_even = (f2, 'even', s16 * mul, first)
    cands.append(best_even)
    f, kind, s16, base = max(cands, key=lambda c: c[0] + (0.02 if c[1] == 'c64' else 0))
    if kind == 'c64':
        to = lambda t: c64_index(t - base + 1e-9)
    else:
        to = lambda t: (t - base) / s16
    # the downbeat: where the bass lands most often
    bass_on = [n[0] for n in notes if n[1] in BASSES]
    ph = max(range(16), key=lambda p: sum(1 for t in bass_on if round(to(t) - p) % 16 == 0) + (0.3 if p == 0 else 0))
    return (lambda t: to(t) - ph), s16, kind


def roles(notes):
    """Which instruments carry the melody: the lead voices (by name) that sit
    in the singing range; a guitar only when there is no lead voice."""
    by = defaultdict(list)
    for n in notes:
        by[n[1]].append(n)

    def median(ns, i):
        return sorted(n[i] for n in ns)[len(ns) // 2]

    leads = [i for i, ns in by.items() if 'ead' in i and 60 <= median(ns, 2) <= 86 and len(ns) > 20
             and not (median(ns, 3) < 0.09 and len(ns) > 800)]
    if not leads and 'guitar' in by:
        leads = ['guitar']
    if not leads:
        cand = [(median(ns, 2), i) for i, ns in by.items() if i not in BASSES and len(ns) > 20]
        leads = [max(cand)[1]] if cand else []
    return leads


def analyse(key):
    d = json.load(open(os.path.join(SRC, key + '.json')))
    notes = d['notes']
    to_step, s16, kind = grid(notes)
    nbars = max(1, int(math.ceil(to_step(d['length'] - 1.5) / 16)))
    leads = roles(notes)

    def bar_of(t):
        return int(math.floor(to_step(t) / 16 + 0.02))

    roots, quals = [], []
    bassw = defaultdict(Counter)
    chordw = defaultdict(Counter)
    pcs = Counter()
    for n in notes:
        b = bar_of(n[0])
        if b < 0 or b >= nbars:
            continue
        w = n[3] * n[4]
        pcs[n[2] % 12] += w
        if n[1] in BASSES:
            bassw[b][n[2] % 12] += w * (2 if to_step(n[0]) % 16 < 1.5 else 1)
        elif n[1] not in leads:
            chordw[b][n[2] % 12] += w
    last = 4  # E
    for b in range(nbars):
        if bassw[b]:
            last = bassw[b].most_common(1)[0][0]
        r = last
        c = chordw[b]
        roots.append(r)
        quals.append('m' if c[(r + 3) % 12] > c[(r + 4) % 12] else '')
    # the key: the major scale that holds the most of the music, and its relative minor
    major = [0, 2, 4, 5, 7, 9, 11]
    tonic = max(range(12), key=lambda k: sum(pcs[(k + i) % 12] for i in major))
    scale = [(tonic + i) % 12 for i in major]
    # the melody: the top line of the lead voices, on the 16th grid
    mel = []
    taken = {}
    for n in notes:
        if n[1] not in leads:
            continue
        pos = round(to_step(n[0]))
        if pos < 0 or pos >= nbars * 16:
            continue
        ln = max(1, round(to_step(n[0] + n[3]) - to_step(n[0])))
        if pos not in taken or n[2] > taken[pos][1]:
            taken[pos] = (pos, n[2], ln)
    mel = sorted(taken.values())
    for i in range(len(mel) - 1):  # no overlaps: a note ends where the next begins
        if mel[i][0] + mel[i][2] > mel[i + 1][0]:
            mel[i] = (mel[i][0], mel[i][1], max(1, mel[i + 1][0] - mel[i][0]))
    # how busy each bar was: the sections
    busy = Counter()
    for n in notes:
        b = bar_of(n[0])
        if 0 <= b < nbars:
            busy[b] += n[4]
    for e in d.get('events', []):
        b = bar_of(e[0])
        if 0 <= b < nbars:
            busy[b] += 0.5
    return {'key': key, 's16': s16, 'clock': kind, 'bars': nbars, 'roots': roots, 'quals': quals, 'scale': scale,
            'melody': mel, 'busy': [busy[b] for b in range(nbars)], 'leads': leads, 'tonic': tonic}


def sections(busy):
    """8-bar blocks by how busy the original was: the top third are choruses,
    the quietest (when there are enough blocks) a break, the rest verses;
    the last 4 bars of a verse before a chorus are a lift."""
    blocks = [sum(busy[i:i + 8]) / len(busy[i:i + 8]) for i in range(0, len(busy), 8)]
    order = sorted(range(len(blocks)), key=lambda i: blocks[i])
    kinds = ['verse'] * len(blocks)
    for i in order[-max(1, len(blocks) // 3):]:
        kinds[i] = 'chorus'
    if len(blocks) >= 5:
        kinds[order[0]] = 'break' if kinds[order[0]] != 'chorus' else kinds[order[0]]
    if len(blocks) >= 3 and all(k == 'chorus' for k in kinds[:1]):
        kinds[0] = 'verse'  # open on a verse: the chorus is the arrival
    per_bar = []
    for i, k in enumerate(kinds):
        n = min(8, len(busy) - 8 * i)
        for j in range(n):
            kk = k
            if k == 'verse' and i + 1 < len(kinds) and kinds[i + 1] == 'chorus' and j >= n - 4:
                kk = 'lift'
            per_bar.append(kk)
    return per_bar


# ---- the band (as in ghost()) --------------------------------------------------------------

def bass_root(pc):
    """A bass root between B1 and A#2."""
    n = 36 + pc
    return n - 12 if n > 46 else n


def rearrange(key):
    a = analyse(key)
    sk, tl = SKIP.get(key, 0), TAIL.get(key, 0)
    if sk or tl:
        end = a['bars'] - tl
        for f in ('roots', 'quals', 'busy'):
            a[f] = a[f][sk:end]
        a['melody'] = [(st - 16 * sk, p, ln) for st, p, ln in a['melody'] if 16 * sk <= st < 16 * end]
        a['bars'] = end - sk
    s = Song(key, 7 + len(key))
    N = a['bars']
    prog = [(bass_root(r), q) for r, q in zip(a['roots'], a['quals'])]
    kinds = sections(a['busy'])
    scale = a['scale']
    # the melody, an octave that suits the leads (around C5-E5)
    mel = a['melody']
    if mel:
        med = sorted(p for _, p, _ in mel)[len(mel) // 2]
        shift = 12 * round((72 - med) / 12)
        mel = [(st, p + shift, ln) for st, p, ln in mel]
    INTRO = 8

    def bass(bar, root, nxt, vel=0.9):
        for st in (0, 2, 6, 8, 12, 14):
            n = root - 2 if st in (6, 14) else root
            if nxt != root and st >= 12:
                dd = nxt - root
                dd = dd - 12 if dd > 6 else dd + 12 if dd < -6 else dd
                n = root + (round(dd / 2) if st == 12 else dd - (1 if dd > 0 else -1))
            s.note(bar, st, 'funkBass', n, 2, vel * (1.0 if st in (0, 6) else 0.75))

    def pulse(bar, root, q, vel):
        tn = [t + 36 for t in chord_tones(root, q)]
        for i, st in enumerate((2, 3, 10, 11, 14, 15)):
            s.note(bar, st, 'softArp', tn[(i + 1) % 3], 1, vel * (1.0 if st % 2 == 0 else 0.7))

    def piano(bar, root, q, vel):
        for st in (2, 6, 10, 14):
            for tn in chord_tones(root, q):
                s.note(bar, st, 'piano', tn + 36, 1, vel)

    def phrase(src_bar, bars):
        """The melody notes of source bars [src_bar, src_bar + bars) as a phrase."""
        out = []
        for st, p, ln in mel:
            if src_bar * 16 <= st < (src_bar + bars) * 16:
                out.append((st - src_bar * 16, p, ln))
        return out

    def sing(at, src_bar, bars, inst, vel, third_below=False):
        ph = phrase(src_bar, bars)
        for st, p, ln in ph:
            b, s16 = divmod(st, 16)
            n = diatonic(p, -2, scale) if third_below else p
            s.note(at + b, s16, inst, n, ln, vel)
        return [(st, '', ln) for st, p, ln in ph]  # for gap_line: only the timing matters

    # ---- the jet intro over the first chords
    s.fx(0, 8, 'jet', 8.5, 0.7, 0.75)
    s.fx(2, 0, 'riser', 2 * 1.7067, 0.45)
    for i in range(INTRO):
        r, q = prog[i % N]
        nr = prog[(i + 1) % N][0] if i < INTRO - 1 else prog[0][0]
        if i >= 2:
            pulse(i, r, q, 0.12 + 0.03 * (i - 2))
        if i >= 4:
            bass(i, r, nr, 0.6 + 0.07 * (i - 4))
            for st in range(0, 16, 2):
                s.drum(i, st, 'h', 0.25 + 0.05 * (i - 4))
        if i >= 6:
            rock_beat(s, i, fill=i == 7, intensity=0.55 + 0.15 * (i - 6))
        if i % 4 == 0:
            long_pad(s, i, r + 24, q, bars=4, vel=0.3 + 0.05 * (i // 4))
    sing(6, 0, 2, 'sidLead', 0.45)

    # ---- the song, bar for bar
    last_chorus = max((i for i, k in enumerate(kinds) if k == 'chorus'), default=-1)
    last_block = last_chorus // 8 if last_chorus >= 0 else -1
    seen_verse = False
    for b in range(N):
        at = INTRO + b
        r, q = prog[b]
        nr = prog[b + 1][0] if b + 1 < N else prog[0][0]
        k = kinds[b]
        nk = kinds[b + 1] if b + 1 < N else 'outro'
        pk = kinds[b - 1] if b > 0 else 'verse'
        # the drums lean toward the next section over the last two bars
        lvl = {'verse': 0.78, 'lift': 0.9, 'chorus': 1.0, 'break': 0.55}[k]
        nl = {'verse': 0.78, 'lift': 0.9, 'chorus': 1.0, 'break': 0.55, 'outro': 0.7}[nk]
        pos = b % 8
        if nk != k and pos >= 6:
            lvl += (nl - lvl) * (pos - 5) / 3
        if k == 'chorus' and pk != 'chorus':
            crash(s, at)
        if k == 'break':
            s.drum(at, 0, 'k', lvl)
            if pos >= 2:
                s.drum(at, 8, 'k', lvl * 0.8)
                s.drum(at, 12, 's', lvl * 0.8)
            for st in range(0, 16, 4 if pos < 4 else 2):
                s.drum(at, st + (2 if pos < 4 else 0), 'h', 0.25 + 0.03 * pos)
            s.note(at, 0, 'funkBass', r, 8, 0.7)
            s.note(at, 8, 'funkBass', r if nr == r else nr - 1, 8, 0.6)
            pulse(at, r, q, 0.2)
            long_pad(s, at, r + 24, q, vel=0.5)
        else:
            chorus = k == 'chorus'
            rock_beat(s, at, chorus=chorus, fill=pos % 4 == 3, intensity=lvl)
            if chorus:
                for st in (4, 12):
                    s.drum(at, st, 'S', 0.45 * lvl)
            bass(at, r, nr, 0.95 if chorus else 0.85)
            pulse(at, r, q, 0.3 if k != 'verse' else 0.25)
            if chorus:
                gated_pad(s, at, r + 24, q, vel=0.45)
                for st in range(0, 16, 2):
                    for tn in (r + 12, r + 19, r + 24):
                        s.note(at, st, 'powerChord', tn, 2 if st % 4 == 0 else 1, 0.42 if st % 4 == 0 else 0.32)
                piano(at, r, q, 0.26)
            else:
                chug(s, at, r, vel=0.3)
                if k == 'lift':
                    long_pad(s, at, r + 24, q, inst='brassChord', vel=0.35 + 0.08 * (pos % 4))
    # the melody, phrase by phrase (4 bars), with the voice the section calls for
    for b0 in range(0, N, 4):
        k = kinds[b0]
        n = min(4, N - b0)
        if k == 'chorus':
            ph = sing(INTRO + b0, b0, n, 'lead80s', 0.9)
            gap_line(s, INTRO + b0, n, prog[b0:b0 + n], [(INTRO + b0, ph)], 'softArp', 0.2, step=1, octave=48)
            if b0 // 8 == last_block:
                sing(INTRO + b0, b0, n, 'warmLead', 0.55, third_below=True)
        elif k == 'break':
            ph = sing(INTRO + b0, b0, n, 'brassLead', 0.75)
            gap_line(s, INTRO + b0, n, prog[b0:b0 + n], [(INTRO + b0, ph)], 'softArp', 0.18, step=1, octave=48)
        elif k == 'lift':
            sing(INTRO + b0, b0, n, 'warmLead', 0.85)
        else:
            ph = sing(INTRO + b0, b0, n, 'sidLead' if (b0 // 8) % 2 == 0 else 'warmLead', 0.82)
            if seen_verse:
                gap_line(s, INTRO + b0, n, prog[b0:b0 + n], [(INTRO + b0, ph)], 'guitar', 0.3)
            seen_verse = True

    # ---- outro: the last four chords again, thinning, the jet passing over
    o = INTRO + N
    for i in range(4):
        r, q = prog[N - 4 + i] if N >= 4 else prog[i % N]
        nr = prog[N - 3 + i][0] if i < 3 and N >= 4 else prog[0][0]
        rock_beat(s, o + i, intensity=0.7 - 0.12 * i)
        bass(o + i, r, nr, 0.75 - 0.08 * i)
        pulse(o + i, r, q, 0.26 - 0.04 * i)
    r0, q0 = prog[0]
    long_pad(s, o + 2, r0 + 24, q0, bars=3, vel=0.45)
    s.fx(o, 0, 'jet', 7, 0.6, 0.5)
    s.drum(o + 4, 0, 'k', 0.6)
    s.drum(o + 4, 0, 'X', 0.5)
    s.note(o + 4, 0, 'funkBass', r0, 16, 0.6)
    s.write(os.path.join('assets', 'music', key + '.json'), loop_bar=INTRO, end_bar=o + 6)
    tonic = 'C C# D D# E F F# G G# A A# B'.split()[a['tonic']]
    print('   %s: %d bars, %s clock at %.0f BPM, %s major / relative minor, leads %s, sections %s' % (
        key, N, a['clock'], 60 / (4 * a['s16']), tonic, ','.join(a['leads']),
        ''.join({'verse': 'v', 'lift': 'l', 'chorus': 'C', 'break': 'b'}[k] for k in kinds)))


if __name__ == '__main__':
    for k in sys.argv[1:] or KEYS:
        rearrange(k)
