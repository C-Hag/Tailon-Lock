#!/usr/bin/env python3
"""h2h-solo: an improvisation engine that elaborates a written SID melody, in a rock/pop feel.

usage: h2h-solo SONG.sf2txt --bars 65-68 --chords qa-spec.json [--bass score.sf2txt:2] [--drums drum-hits.json]
                [--n 200] [--pick 3] [--seed 1] [--out takes.json]

It reads the melody (track 1 of SONG.sf2txt, 16 rows a bar, 16ths), the chords (a qa spec's "chords": [row0, row1,
root pc, quality]), the key (its "keys"), the bass line and the kick/snare hits, and plays the melody back the way
a rock guitarist or synth soloist would on a second chorus: the same phrases, starting and ending where the written
ones do, the same long target notes, the contour kept, and the space between them elaborated.

Each take applies a few moves per bar (more towards the climax: the energy builds):
- rhythm: two 16ths merged into an 8th, or an 8th split into a 16th pair with a scale neighbour;
- a scale run (major scale / pentatonic) into the bar's target note, replacing the arpeggio before it, same length;
- neighbour tones: a written note turned into note, upper/lower scale neighbour, note;
- a sequence: the previous bar's figure moved through the scale onto this bar's chord;
- an anticipation: a target note one 16th early (only where the note before is long enough to give it up);
- a pentatonic sequence: the bar's 16th arpeggio replayed as the rock lick, groups of 3 or 4 up or down the
  E major / C# minor pentatonic from the written first note, landing on the target by step;
- a pedal lick: the target pitch alternated with the arpeggio notes in 16ths before it lands;
- a fill: the tail of a long note turned into a short pentatonic pickup into the next bar;
- the climax (the last bar): a rock bend into the target, a whole (or half) step up from the scale note below
  (a tied note with portamento), then the written short vibrato;
- harmony: some long target notes doubled a diatonic 3rd or 6th below, only where that note is a chord tone
  (written on track 2, for a second, softer voice).
All on the straight 16th grid (no swing); the human touch is at most one or two notes laid back one SID frame
(delay=1, 20 ms).

Rules every take must keep: a chord tone on every strong beat (or the written note); every note in the key (no
chromatic or blue notes at all); within the written range and at most A5; no leaps over a 5th that the original
doesn't have; no minor 2nd / 9th against the bass on a beat; the written phrase starts and ends.
Score (0-1 each, weighted): motif (interval contour and pitch at the written onsets), chord fit (chord-tone time),
sync (note onsets on the rows the bass and kick/snare accent), singability (stepwise motion, few leaps), energy build
(density and height rising bar by bar), and elaboration (about 30 % of the rows changed: an elaboration, not a
new tune). Writes the best takes that differ from each other (at least 20 % of the rows), with their scores.
"""
import argparse, json, random, re
import numpy as np

NOTE = {'C-': 0, 'C#': 1, 'D-': 2, 'D#': 3, 'E-': 4, 'F-': 5, 'F#': 6, 'G-': 7, 'G#': 8, 'A-': 9, 'A#': 10, 'B-': 11}
NAMES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-']
CT = {'maj': (0, 4, 7), 'min': (0, 3, 7), 'pow': (0, 7), 'dom7': (0, 4, 7, 10), 'min7': (0, 3, 7, 10)}
STRONG = (0, 4, 8, 12)
TOP = 81
DIFF = 0.2                                                           # picks differ in at least 20 % of the rows
W = dict(motif=0.30, fit=0.15, sync=0.15, sing=0.15, energy=0.10, elab=0.15)


def midi_of(tok):
    m = re.match(r'~?([A-G][-#])(\d)', tok)
    return NOTE[m.group(1)] + 12 * (int(m.group(2)) + 1) if m else None


def name(m):
    return '%s%d' % (NAMES[m % 12], m // 12 - 1)


def read_bars(path, track=1):
    t = open(path).read()
    seg = t.split('track %d\n' % track)[1].split('\ntrack ')[0]
    bars = {}
    for line in seg.splitlines():
        m = re.search(r'\s# bar (\d+)\s*$', line)
        if m:
            bars[int(m.group(1))] = line[:m.start()].split()
    return bars


def notes_of(tokens):
    """[(row, length, midi, vib_row or None)] of one bar's 16 tokens (a note held into the next bar ends at 16)"""
    out = []
    for r, tok in enumerate(tokens):
        if tok == '-':
            if out and out[-1][0] + out[-1][1] > r:
                out[-1][1] = r - out[-1][0]
            continue
        if tok.startswith('+'):
            if out and '::vib' in tok and out[-1][3] is None:
                out[-1][3] = r
            continue
        m = midi_of(tok)
        if out and out[-1][0] + out[-1][1] > r:
            out[-1][1] = r - out[-1][0]
        out.append([r, 16 - r, m, None])
    for i in range(len(out) - 1):
        out[i][1] = min(out[i][1], out[i + 1][0] - out[i][0])
    # a note ended by a rest keeps its end
    for r, tok in enumerate(tokens):
        if tok == '-':
            for n in out:
                if n[0] < r < n[0] + n[1]:
                    n[1] = r - n[0]
    return [list(n) for n in out]


class Ctx:
    def __init__(self, a, bars, chords, keys, bass, accents):
        self.bars, self.chords, self.keys, self.bass, self.accents = bars, chords, keys, bass, accents
        self.lo = min(n[2] for b in bars.values() for n in b)
        self.hi = min(TOP, max(n[2] for b in bars.values() for n in b) + 2)
        # no leap wider than a 5th, or than the written melody's own widest
        self.leap = max(7, max((abs(b_[i + 1][2] - b_[i][2]) for b_ in bars.values() for i in range(len(b_) - 1)), default=7))

    def chord(self, bar, row=0):
        r = (bar - 1) * 16 + row
        for c in self.chords:
            if c[0] <= r < c[1]:
                return c[2], c[3]
        return 4, 'pow'

    def cts(self, bar, row=0):
        root, q = self.chord(bar, row)
        return {(root + i) % 12 for i in CT.get(q, (0, 7))}

    def scale(self, bar):
        r = (bar - 1) * 16
        for k in self.keys:
            if k[0] <= r < k[1]:
                return sorted(set(k[2]))
        return [1, 3, 4, 6, 8, 9, 11]

    def steps(self, bar, m, k):
        """k scale steps from m (k may be negative), in the bar's key"""
        sc = self.scale(bar)
        pool = [x for x in range(self.lo - 3, TOP + 1) if x % 12 in sc]
        if m not in pool:
            pool = sorted(pool + [m])
        i = pool.index(m) + k
        return pool[max(0, min(len(pool) - 1, i))]

    def penta(self, bar):
        root = self.scale(bar)
        tonic = 4 if 4 in root else root[0]                            # E major pentatonic = C# minor pentatonic
        return {(tonic + i) % 12 for i in (0, 2, 4, 7, 9)}


# ------------------------------------------------------------------ the moves (each edits one bar's notes in place)
def targets(ns):
    return [i for i, n in enumerate(ns) if n[1] >= 3 and len(n) < 5]


def mv_rhythm(ns, bar, cx, rng):
    shorts = [i for i, n in enumerate(ns[:-1]) if n[1] == 1 and ns[i + 1][1] == 1 and n[0] % 2 == 0 and n[0] not in STRONG[1:]
              and ns[i + 1][0] not in STRONG]
    two = [i for i, n in enumerate(ns) if n[1] == 2 and n[0] % 2 == 0]
    if shorts and (not two or rng.random() < 0.5):
        i = rng.choice(shorts)                                         # 16th pair -> 8th
        ns[i][1] = 2
        del ns[i + 1]
        return 'rhythm: a 16th pair to an 8th (row %d)' % ns[i][0]
    if two:
        i = rng.choice(two)                                            # 8th -> 16th pair with a neighbour
        n = ns[i]
        nxt = ns[i + 1][2] if i + 1 < len(ns) else None
        nbs = [cx.steps(bar, n[2], d) for d in (1, -1)]
        nbs = [x for x in nbs if x != nxt and x != n[2]]
        if not nbs:
            return None
        n[1] = 1
        ns.insert(i + 1, [n[0] + 1, 1, rng.choice(nbs), None])
        return 'rhythm: an 8th split into 16ths (row %d)' % n[0]
    return None


def mv_run(ns, bar, cx, rng):
    ts = [i for i in targets(ns) if i >= 2 and all(ns[j][1] <= 2 for j in range(max(0, i - 4), i))]
    if not ts:
        return None
    i = rng.choice(ts)
    k = min(i, rng.choice((3, 4)))
    tgt = ns[i][2]
    start = ns[i - k]
    rows = sum(ns[j][1] for j in range(i - k, i))
    direction = 1 if start[2] < tgt else (-1 if start[2] > tgt else rng.choice((1, -1)))
    # a stepwise line into the target, one note per 16th, built backwards from a step away (so it lands on it)
    def build(dirn):
        line = [cx.steps(bar, tgt, -dirn * (rows - r)) for r in range(rows)]
        return line if len(set(line)) == rows and tgt not in line else None
    line = build(direction) or build(-direction)
    if not line:
        return None
    new = [[start[0] + r, 1, line[r], None] for r in range(rows)]
    ns[i - k:i] = new
    return 'scale run into %s (row %d)' % (name(tgt), ns[i - k + rows][0] if i - k + rows < len(ns) else 16)


def mv_neighbour(ns, bar, cx, rng):
    c = [i for i, n in enumerate(ns) if n[1] >= 3 and n[0] + 3 <= 16 and len(n) < 5]
    if not c:
        return None
    i = rng.choice(c)
    n = ns[i]
    nb = cx.steps(bar, n[2], rng.choice((1, -1)))
    tail = n[1] - 2
    n[1] = 1
    ns.insert(i + 1, [n[0] + 1, 1, nb, None])
    ns.insert(i + 2, [n[0] + 2, tail, n[2], n[3] if n[3] and n[3] > n[0] + 2 else None])
    return 'neighbour tone %s-%s-%s (row %d)' % (name(n[2]), name(nb), name(n[2]), n[0])


def mv_sequence(ns, bar, cx, rng, prev):
    if not prev:
        return None
    fig = [n for n in prev if n[1] <= 2 and n[0] < 8]
    mine = [i for i, n in enumerate(ns) if n[1] <= 2 and n[0] < 8]
    if len(fig) < 3 or len(mine) < 3 or len(fig) != len(mine):
        return None
    # the previous bar's figure in scale steps, started on this bar's first written note
    sc = cx.scale(bar)
    pool = [x for x in range(cx.lo - 3, TOP + 1) if x % 12 in sc]
    try:
        deg = [pool.index(n[2]) for n in fig]
    except ValueError:
        return None
    base = ns[mine[0]][2]
    if base not in pool:
        return None
    b0 = pool.index(base)
    for j, i in enumerate(mine):
        k = b0 + deg[j] - deg[0]
        if not 0 <= k < len(pool):
            return None
        ns[i][2] = pool[k]
    return 'sequence of bar %d\'s figure' % (bar - 1)


def mv_anticipate(ns, bar, cx, rng):
    c = [i for i in targets(ns) if i > 0 and ns[i - 1][1] >= 2 and ns[i][0] in STRONG and len(ns[i - 1]) < 5]
    if not c:
        return None
    i = rng.choice(c)
    ns[i - 1][1] -= 1
    ns[i][0] -= 1
    ns[i][1] += 1
    if ns[i][3] is not None:
        ns[i][3] -= 1
    return 'anticipation: %s a 16th early (row %d)' % (name(ns[i][2]), ns[i][0])


def mv_pedal(ns, bar, cx, rng):
    ts = [i for i in targets(ns) if i >= 3 and all(ns[j][1] == 1 for j in range(i - 3, i))]
    if not ts:
        return None
    i = rng.choice(ts)
    tgt = ns[i][2]
    for j in (i - 3, i - 1):                                           # target, arp, target, arp -> lands on target
        ns[j][2] = tgt
    return 'pedal lick on %s' % name(tgt)


def mv_fill(ns, bar, cx, rng, nxt):
    c = [i for i, n in enumerate(ns) if n[1] >= 6 and n[0] + n[1] >= 14 and i == len(ns) - 1 and len(n) < 5]
    if not c or nxt is None:
        return None
    n = ns[c[0]]
    k = rng.choice((2, 3))
    pen = cx.penta(bar) | cx.cts(bar)
    m, line = nxt, []
    for _ in range(k):                                                 # backwards from the next bar's first note
        cand = [x for x in range(m - 4, m + 5) if x % 12 in pen and x != m and cx.lo <= x <= TOP]
        m = min(cand, key=lambda x: (abs(x - m) > 2, -abs(x - m) if rng.random() < 0.3 else abs(x - m)))
        line.append(m)
    line.reverse()
    end = n[0] + n[1]
    n[1] -= k
    if n[3] is not None and n[3] >= n[0] + n[1]:
        n[3] = None
    for j, m in enumerate(line):
        ns.append([end - k + j, 1, m, None])
    return 'fill: %s into the next bar' % ' '.join(name(x) for x in line)


def mv_penta(ns, bar, cx, rng):
    """the rock lick: the bar's 16th arpeggio (rows 0-7) replayed as a pentatonic sequence in groups of 3 or 4,
    from the written first note, moving towards the bar's target, landing on it by step"""
    head = [n for n in ns if n[0] < 8]
    if len(head) != 8 or any(n[1] != 1 for n in head):
        return None
    tgt = next((n[2] for n in ns if n[0] == 8), None)
    if tgt is None:
        return None
    pen = sorted(cx.penta(bar) | cx.cts(bar))
    pool = [x for x in range(cx.lo - 2, cx.hi + 1) if x % 12 in pen]
    if head[0][2] not in pool:
        return None
    g = rng.choice((3, 4))
    i0 = pool.index(head[0][2])
    up = tgt > head[0][2] if tgt != head[0][2] else rng.random() < 0.5
    seq, k = [], i0
    while len(seq) < 8:                                               # e.g. 1 2 3, 2 3 4, ... (or down)
        for j in range(g):
            idx = k + (j if up else -j)
            seq.append(pool[max(0, min(len(pool) - 1, idx))])
        k += 1 if up else -1
    seq = seq[:8]
    seq[0] = head[0][2]
    for j in range(8):
        ns[j][2] = seq[j]
    if abs(seq[7] - tgt) > 3 or seq[7] == tgt or seq[7] == seq[6]:   # land by step into the target
        ns[7][2] = cx.steps(bar, tgt, -1 if up else 1)
    return 'pentatonic sequence in %ds %s, into %s' % (g, 'up' if up else 'down', name(tgt))


def mv_bend(ns, bar, cx, rng):
    ts = [i for i in targets(ns) if not (i > 0 and ns[i - 1][2] == ns[i][2] - 0 and ns[i - 1][0] == ns[i][0] - 1)
          and not (i >= 2 and ns[i - 2][2] == ns[i][2] and ns[i - 1][1] == 1)]
    if not ts:
        return None
    i = max(ts, key=lambda j: (ns[j][2], ns[j][1]))
    n = ns[i]
    below = cx.steps(bar, n[2], -1)
    if n[2] - below > 2 or n[1] < 4:
        return None
    n.append(('bend', below))                                          # rendered as below, then ~target::bend
    return 'rock bend %s up to %s' % (name(below), name(n[2]))


def harmony(ns, bar, cx, rng):
    out = []
    for n in ns:
        if n[1] >= 4 and rng.random() < 0.75:
            for iv in ((3, 4), (8, 9)):                                 # a diatonic 3rd or 6th below
                c = [n[2] - k for k in iv if (n[2] - k) % 12 in cx.cts(bar, n[0]) and (n[2] - k) % 12 in cx.scale(bar)]
                if c:
                    out.append([n[0], n[1], c[0], None])
                    break
    return out


# ------------------------------------------------------------------ one take
def take(cx, bars, rng):
    nums = sorted(bars)
    out, harm, log = {}, {}, []
    prev = None
    for k, b in enumerate(nums):
        ns = [list(n) for n in notes_of(bars[b])]
        nxt = notes_of(bars[b + 1])[0][2] if b + 1 in bars and notes_of(bars[b + 1]) else None
        intensity = k / max(1, len(nums) - 1)                           # 0 .. 1: the energy builds
        moves = ['penta', 'penta', 'rhythm', 'run', 'run', 'neighbour', 'sequence', 'anticipate', 'pedal', 'fill']
        nmov = rng.choice((0, 1, 1)) if k == 0 else 1 + int(rng.random() < 0.5 + 0.4 * intensity) + \
            int(rng.random() < 0.3 * intensity)
        rng.shuffle(moves)
        done = 0
        if k == len(nums) - 1:                                          # the climax: its bend first, that note locked
            r = mv_bend(ns, b, cx, rng)
            if r:
                log.append('bar %d: %s' % (b, r))
        for mv in moves:
            if done >= nmov:
                break
            f = {'rhythm': mv_rhythm, 'run': mv_run, 'neighbour': mv_neighbour, 'anticipate': mv_anticipate,
                 'pedal': mv_pedal, 'penta': mv_penta}.get(mv)
            r = f(ns, b, cx, rng) if f else (mv_sequence(ns, b, cx, rng, prev) if mv == 'sequence' else
                                            mv_fill(ns, b, cx, rng, nxt))
            if r:
                log.append('bar %d: %s' % (b, r))
                done += 1
            ns.sort(key=lambda n: n[0])
        # strong beats: a new note there that isn't a chord tone moves to the nearest chord tone
        written = {n[0]: n[2] for n in notes_of(bars[b])}
        for n in ns:
            if n[0] in STRONG and n[2] % 12 not in cx.cts(b, n[0]) and written.get(n[0]) != n[2]:
                cand = [x for x in range(n[2] - 4, n[2] + 5) if x % 12 in cx.cts(b, n[0]) and cx.lo - 2 <= x <= cx.hi]
                if cand:
                    n[2] = min(cand, key=lambda x: (abs(x - n[2]), x))
        if intensity >= 0.3 and rng.random() < 0.85:
            h = harmony(ns, b, cx, rng)
            if h:
                harm[b] = h
                log.append('bar %d: harmony %s' % (b, ', '.join('%s under %s' % (name(x[2]), name(y[2]))
                                                               for x, y in zip(h, [n for n in ns if n[1] >= 4]))))
        out[b] = ns
        prev = ns
    # the human touch: one or two weak-row notes laid back one frame
    lay = []
    for b in nums:
        for n in out[b]:
            if n[0] % 4 == 3 and n[1] == 1 and rng.random() < 0.12 and len(lay) < 2:
                lay.append((b, n[0]))
    return out, harm, log, lay


# ------------------------------------------------------------------ checks and score
def flat(bars_notes, nums):
    """per row (absolute): sounding midi or None, plus onsets"""
    rows, ons = [], []
    for b in nums:
        cur = [None] * 16
        for n in bars_notes[b]:
            for r in range(n[0], min(16, n[0] + n[1])):
                cur[r] = n[2]
            ons.append(((b - nums[0]) * 16 + n[0], n[2], n[1]))
        rows += cur
    return rows, ons


def legal(cx, tk, orig, nums):
    if not any(len(n) > 4 for n in tk[nums[-1]]):
        return 'no climax bend'
    seq = [n[2] for b in nums for n in sorted(tk[b], key=lambda n: n[0])]
    if any(seq[i] == seq[i + 1] == seq[i + 2] for i in range(len(seq) - 2)):
        return 'stutter (a note three times)'
    for b in nums:
        cts, sc = cx.cts(b), cx.scale(b)
        on = {n[0]: n for n in orig[b]}
        last = None
        for n in sorted(tk[b], key=lambda n: n[0]):
            if n[1] < 1 or n[0] < 0 or n[0] + n[1] > 16:
                return 'bad length'
            if n[2] % 12 not in sc:
                return 'out of key'
            if not (cx.lo - 2 <= n[2] <= cx.hi):
                return 'range'
            if n[0] in STRONG and n[2] % 12 not in cx.cts(b, n[0]) and not (n[0] in on and on[n[0]][2] == n[2]):
                return 'strong beat %d.%d not a chord tone' % (b, n[0])
            if last is not None and abs(n[2] - last) > cx.leap:
                return 'leap'
            bp = cx.bass.get((b, n[0]))
            if n[0] in STRONG and bp is not None and (n[2] - bp) % 12 in (1, 11) and not (n[0] in on and on[n[0]][2] == n[2]):
                return 'minor 2nd on the bass'
            last = n[2]
        # the phrase: same first onset, same last note end
        if orig[b] and tk[b]:
            if min(n[0] for n in tk[b]) != min(n[0] for n in orig[b]):
                return 'phrase start'
            if max(n[0] + n[1] for n in tk[b]) != max(n[0] + n[1] for n in orig[b]):
                return 'phrase end'
    return None


def score(cx, tk, orig, nums):
    ro, oo = flat(orig, nums)
    rt, ot = flat(tk, nums)
    # motif: pitch at the written onsets, and the interval contour
    d = [abs((rt[r] or p) - p) for r, p, l in oo]
    pitch = float(np.exp(-np.mean(d) / 3))
    ivo = np.sign(np.diff([p for r, p, l in oo]))
    ivt = np.sign(np.diff([rt[r] if rt[r] is not None else p for r, p, l in oo]))
    contour = float((ivo == ivt).mean()) if len(ivo) else 1
    motif = 0.5 * pitch + 0.5 * contour
    # chord fit: chord-tone share of the sounding rows
    tot = fit = 0
    for i, m in enumerate(rt):
        if m is None:
            continue
        b, r = nums[0] + i // 16, i % 16
        tot += 1
        fit += m % 12 in cx.cts(b, r)
    fit = fit / max(1, tot)
    # sync: onsets of 2+ row notes on accented rows
    acc = cx.accents
    long_on = [(r, l) for r, p, l in ot if l >= 2]
    sync = sum(1 for r, l in long_on if (nums[0] + r // 16, r % 16) in acc or r % 4 == 0) / max(1, len(long_on))
    # singability
    ps = [p for r, p, l in ot]
    iv = np.abs(np.diff(ps)) if len(ps) > 1 else np.array([0])
    rep_ = sum(1 for (r1, p1, l1), (r2, p2, l2) in zip(ot, ot[1:]) if p1 == p2 and l1 == 1 and
               (nums[0] * 16 + r1 + 1, p1) not in {(nums[0] * 16 + r, p) for r, p, l in oo})
    sing = float(np.mean(iv <= 2) * 0.6 + np.mean(iv <= 5) * 0.4) - 0.06 * rep_   # repeated 16ths: a stutter
    # energy build: onsets and top note per bar rising
    e = [len(tk[b]) + max(n[2] for n in tk[b]) / 6 for b in nums]
    energy = float(np.clip(np.corrcoef(range(len(e)), e)[0, 1], -1, 1) * 0.5 + 0.5) if np.std(e) > 0 else 0.5
    # elaboration: changed rows
    changed = np.mean([a != b for a, b in zip(ro, rt)])
    elab = float(np.exp(-((changed - 0.3) / 0.12) ** 2))
    parts = dict(motif=motif, fit=fit, sync=sync, sing=sing, energy=energy, elab=elab, changed=float(changed))
    total = sum(W[k] * parts[k] for k in W)
    return total, parts


def tokens(cx, tk, harm, lay, orig_tokens, nums, bends):
    """16 tokens a bar for track 1 (the lead) and track 2 (the harmony)"""
    t1, t2 = {}, {}
    for b in nums:
        row = ['+'] * 16
        ns = sorted(tk[b], key=lambda n: n[0])
        first = ns[0][0] if ns else 16
        for r in range(first):
            row[r] = orig_tokens[b][r] if orig_tokens[b][r] in ('-', '+') else '-'
        end = max(n[0] + n[1] for n in ns) if ns else 0
        for r in range(end, 16):
            row[r] = '-'
        for n in ns:
            extra = n[4] if len(n) > 4 else None
            cmd = ':sw1' if (b, n[0]) in lay else ''
            if extra and extra[0] == 'bend':
                row[n[0]] = '%s:bz' % name(extra[1])
                sp = bends(extra[1], n[2])
                row[n[0] + 1] = '~%s::%s' % (name(n[2]), sp)
                if n[1] >= 6:
                    row[n[0] + 5] = '+::vib'
            else:
                row[n[0]] = '%s:bz%s' % (name(n[2]), cmd)
                if n[3] is not None and n[0] < n[3] < n[0] + n[1]:
                    row[n[3]] = '+::vib'
        t1[b] = row
        h = ['-'] * 16
        for n in harm.get(b, []):
            h[n[0]] = '%s:bzh' % name(n[2])
            for r in range(n[0] + 1, min(16, n[0] + n[1])):
                h[r] = '+'
        t2[b] = h
    return t1, t2


# ------------------------------------------------------------------ refit: move a take onto other bars
def refit(song, take_tokens, src_bars, dst_bars, spec, scale, guitars, bass_src=None):
    """take_tokens: {src bar: (track1 '16 tokens', track2 '16 tokens')}. Each source bar is moved onto its destination
    bar through the scale (a diatonic shift: the figure keeps its shape and rhythm), the shift chosen per bar for the
    best chord fit on the strong beats (then the smallest move); strong-beat notes that still aren't chord tones move
    to the nearest one; the last bar's tail is rewritten as a stepwise line into the next written note. The engine's
    rules are then checked (key, strong beats, leaps, stutters, no minor 2nd / 9th against the guitars or the bass).
    Returns ({dst bar: track1 tokens}, {dst bar: track2 tokens}, log, why-not or None)."""
    allb = read_bars(song)
    pool = [x for x in range(40, 90) if x % 12 in scale]
    cx = Ctx(None, {b: notes_of(allb[b]) for b in dst_bars}, spec['chords'], [[0, 10 ** 6, scale]], {}, set())
    cx.lo, cx.hi = 52, TOP
    def cts(b):
        root, q = cx.chord(b)
        c = {(root + i) % 12 for i in CT.get(q, (0, 7))}
        if q == 'pow':                                                  # over a power chord the major 3rd is a chord tone
            c.add((root + 4) % 12)
        return c
    sounding = {}                                                       # (bar, row) -> pitches the guitars and bass hold
    for src in guitars + ([bass_src] if bass_src else []):
        path, voices = src
        for v in voices:
            bb = read_bars(path, v)
            for b in dst_bars:
                for n in notes_of(bb.get(b, ['-'] * 16)):
                    for r in range(n[0], n[0] + n[1]):
                        sounding.setdefault((b, r), set()).add(n[2])
    def shift(m, k):
        if m not in pool:
            m = min(pool, key=lambda x: abs(x - m))
        i = pool.index(m) + k
        return pool[max(0, min(len(pool) - 1, i))]
    out1, out2, log = {}, {}, []
    def move(tokens, b, k, harm=False):
        toks = []
        for r, tok in enumerate(tokens.split()):
            m = midi_of(tok)
            if m is None:
                toks.append(tok)
                continue
            q = shift(m, k)
            if r in STRONG or harm:
                if q % 12 not in cts(b):
                    c = [x for x in range(q - 4, q + 5) if x % 12 in cts(b) and x in pool]
                    if harm and not c:
                        toks.append('-')
                        continue
                    q = min(c, key=lambda x: (abs(x - q), x)) if c else q
            toks.append(re.sub(r'([A-G][-#]\d)', name(q), tok, count=1))
        return toks
    for sb, db in zip(src_bars, dst_bars):
        t1, t2 = take_tokens[sb]
        best = None
        src_ns = notes_of(t1.split())
        for k in range(-4, 5):
            # the fit before any snapping: how many strong-beat notes the shift itself puts on chord tones
            fit = sum(1 for n in src_ns if n[0] in STRONG and shift(n[2], k) % 12 in cts(db))
            toks = move(t1, db, k)
            ns = notes_of(toks)
            ps = [n[2] for n in ns]
            reps = sum(1 for a_, b_ in zip(ps, ps[1:]) if a_ == b_)
            span = max(ps) if ps else 0
            key = (fit - reps, -abs(k))
            if span <= TOP and (best is None or key > best[0]):
                best = (key, k, toks)
        k = best[1]
        out1[db] = best[2]
        out2[db] = move(t2, db, k, harm=True)
        log.append('bar %d -> %d: moved %+d scale steps' % (sb, db, k))
    # the tail of the last bar: a stepwise line from its last long note into the next written note
    last = dst_bars[-1]
    nxt = next((n[2] for n in notes_of(allb.get(last + 1, ['-'] * 16))), None)
    ns = notes_of(out1[last])
    longs = [n for n in ns if n[1] >= 4]
    if nxt is not None and longs:
        import itertools
        hold = longs[0]
        start = hold[0] + hold[1]
        tail = [n for n in ns if n[0] >= start]
        if tail:
            # every line of scale steps (1 or 2 at a time) from the held note: chord tones on the strong beats, no
            # repeated note, ending a step from the next written note; the smoothest wins
            best = None
            for steps in itertools.product((-2, -1, 1, 2), repeat=len(tail)):
                m, seq, ok = hold[2], [], True
                for n, d in zip(tail, steps):
                    m = shift(m, d)
                    rub = n[1] >= 2 and any((m - g) % 12 in (1, 11) for r in range(n[0], n[0] + n[1])
                                            for g in sounding.get((last, r), ()))
                    if (n[0] in STRONG and m % 12 not in cts(last)) or (seq and m == seq[-1]) or m > TOP or rub:
                        ok = False
                        break
                    seq.append(m)
                if not ok or not (0 < abs(seq[-1] - nxt) <= 2):
                    continue
                cost = sum(abs(d) for d in steps) + 2 * sum(1 for a_, b_ in zip(steps, steps[1:]) if (a_ > 0) != (b_ > 0))
                if best is None or cost < best[0]:
                    best = (cost, seq)
            if best:
                toks = out1[last]
                for n, q in zip(tail, best[1]):
                    toks[n[0]] = re.sub(r'([A-G][-#]\d)', name(q), toks[n[0]], count=1)
                log.append('bar %d: tail rewritten into %s: %s' % (last, name(nxt), ' '.join(name(q) for q in best[1])))
    # the rules
    why = None
    seq = []
    for b in dst_bars:
        for n in notes_of(out1[b]):
            seq.append(n[2])
            if n[2] % 12 not in scale:
                why = 'out of key at bar %d' % b
            if n[0] in STRONG and n[2] % 12 not in cts(b):
                why = 'strong beat %d.%d not a chord tone' % (b, n[0])
            if n[2] > TOP:
                why = 'above A5'
            for r in range(n[0], n[0] + n[1]):
                for g in sounding.get((b, r), ()):
                    if (n[2] - g) % 12 in (1, 11) and n[1] >= 2:
                        why = 'minor 2nd against the band at bar %d row %d (%s / %s)' % (b, r, name(n[2]), name(g))
    for a, b_ in zip(seq, seq[1:]):                                     # no leap over an octave
        if abs(a - b_) > 12:
            why = why or 'leap %s-%s' % (name(a), name(b_))
    if any(seq[i] == seq[i + 1] == seq[i + 2] for i in range(len(seq) - 2)):
        why = why or 'stutter'
    return {b: ' '.join(out1[b]) for b in dst_bars}, {b: ' '.join(out2[b]) for b in dst_bars}, log, why


def sid_freq(m):
    return 440 * 2 ** ((m - 69) / 12) * 16777216 / 985248


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('song')
    ap.add_argument('--bars', required=True)
    ap.add_argument('--chords', required=True)
    ap.add_argument('--bass')
    ap.add_argument('--drums')
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--pick', type=int, default=3)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--out', default='takes.json')
    a = ap.parse_args()
    b0, b1 = (int(x) for x in a.bars.split('-'))
    allb = read_bars(a.song)
    nums = list(range(b0, b1 + 1))
    otok = {b: allb[b] for b in nums}
    orig = {b: notes_of(otok[b]) for b in nums}
    spec = json.load(open(a.chords))
    bass, accents = {}, set()
    if a.bass:
        src, v = a.bass.split(':')
        bb = read_bars(src, int(v))
        for b in nums:
            for n in notes_of(bb.get(b, ['-'] * 16)):
                for r in range(n[0], n[0] + n[1]):
                    bass[(b, r)] = n[2]
                accents.add((b, n[0]))
    if a.drums:
        ROW0, ROW = 0.0966, 0.478803 / 4
        for t, p, v in json.load(open(a.drums))['hits']:
            if p in ('kick', 'snare') and v >= 0.5:
                r = int(round((t - ROW0) / ROW))
                accents.add((r // 16 + 1, r % 16))
    cx = Ctx(a, orig, spec['chords'], spec.get('keys', []), bass, accents)
    bend_speeds = {}

    def bends(m0, m1):
        sp = int(round((sid_freq(m1) - sid_freq(m0)) / 12))           # about 12 frames for the bend (~240 ms)
        key = 'bnd%03x' % sp
        bend_speeds[key] = sp
        return key
    rng = random.Random(a.seed)
    takes, rejected = [], {}
    tries = 0
    while len(takes) < a.n and tries < a.n * 40:
        tries += 1
        tk, harm, log, lay = take(cx, otok, random.Random(rng.random()))
        why = legal(cx, tk, orig, nums)
        if why:
            rejected[why.split(' ')[0]] = rejected.get(why.split(' ')[0], 0) + 1
            continue
        s, parts = score(cx, tk, orig, nums)
        rows = flat(tk, nums)[0]
        if any(rows == t['rows'] for t in takes):
            continue
        takes.append(dict(score=round(s, 4), parts={k: round(v, 3) for k, v in parts.items()}, log=log, tk=tk, harm=harm,
                          lay=lay, rows=rows))
    takes.sort(key=lambda t: -t['score'])
    picks = []
    for t in takes:
        if all(np.mean([x != y for x, y in zip(t['rows'], p['rows'])]) >= DIFF for p in picks):
            picks.append(t)
        if len(picks) == a.pick:
            break
    out = dict(bars=nums, generated=len(takes), tried=tries, rejected=rejected, weights=W, takes=[])
    for i, t in enumerate(picks):
        t1, t2 = tokens(cx, t['tk'], t['harm'], t['lay'], otok, nums, bends)
        out['takes'].append(dict(id='abcdefgh'[i], score=t['score'], parts=t['parts'], moves=t['log'],
                                 laid_back=t['lay'], track1={b: ' '.join(t1[b]) for b in nums},
                                 track2={b: ' '.join(t2[b]) for b in nums}))
    out['bend_commands'] = bend_speeds
    out['original'] = {b: ' '.join(otok[b]) for b in nums}
    json.dump(out, open(a.out, 'w'), indent=1)
    print('%d takes generated (%d tried; rejected: %s); picked %d' % (len(takes), tries, rejected, len(picks)))
    for t in out['takes']:
        print('take %s: score %.3f %s' % (t['id'], t['score'], t['parts']))
        for m in t['moves']:
            print('   ', m)
        for b in nums:
            print('    bar %d  %s' % (b, t['track1'][b]))


if __name__ == '__main__':
    main()
