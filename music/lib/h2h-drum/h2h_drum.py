#!/usr/bin/env python3
"""h2h-drum: the Tailon Lock drum machine, an 80s synthwave kit synthesised in numpy (no samples, no third-party sounds).

usage: h2h-drum hits.json out.wav [--kit kit.json] [--length S] [--only kick,snare,...] [--gain G]
       h2h-drum --demo out.wav [--kit kit.json]        (one bar of each piece at three velocities)

hits.json: {"hits": [[t_s, piece, velocity 0-1], ...]} (the session drummer's format, as kitplay.py reads it);
pieces: kick snare ghost xstick hat_c hat_o hat_p ride bell crash1 crash2 tom1 tom2 tom3.
Writes a stereo float32 WAV at 48 kHz: the dry kit, its room and the gated reverb, times --gain (no normalising, so
a part and its per-piece stems share one scale and sum exactly to the full kit). --kit overrides any of the KIT
settings below (a JSON with the same keys).

The voices (each one answers velocity in level, brightness and decay, so the drummer's dynamics survive):
- kick: a sine body sweeping from ~160 Hz down to E1 (41.2 Hz, the song's root, under the bass) with a fast pitch
  envelope, soft-clipped for punch, a tight decay, and on top a short click (band-passed, kept under 6 kHz) and a 1 kHz
  blip on the attack;
- snare: a tone body (two detuned sines with a small pitch drop) plus a band-passed noise layer, into a big gated
  reverb: a dense 1.8 s plate-like hall that a gate keyed by the backbeat snares and the toms opens over 2 ms, holds
  open for 250-340 ms (by velocity) and closes with a 40 ms cosine (the classic 80s gated room, no click at either end);
- ghost: the same snare, soft, barely into the gate; cross-stick: a short wood-and-rim click (two damped sines and a
  noise tick);
- hats: three FM operator pairs at inharmonic ratios (a metallic but dark spectrum), a little noise, band-passed and
  low-passed at 8 kHz (6th order: nothing sizzles above ~10 kHz); closed, half-open and pedal by decay and level;
- toms: Simmons-style: a sine with a bit of second harmonic, the pitch dropping ~45 % over the hit, a stick tick,
  into the gated reverb too;
- crash / ride / ride bell: ring-modulated FM partial clusters (crash), an FM ping over a soft wash (ride), a few
  bell partials plus the ping (bell); all low-passed at 9 kHz, the bright part decaying faster than the body, so
  they bloom and settle instead of hissing; crash "choke" (s) grabs the crash short.
Noise is seeded per hit, so any --only subset renders exactly the samples it has in the full kit.
"""
import argparse, json, sys
import numpy as np
import scipy.signal as sg

SR = 48000
PIECES = ('kick', 'snare', 'ghost', 'xstick', 'hat_c', 'hat_o', 'hat_p', 'ride', 'bell', 'crash1', 'crash2',
          'tom1', 'tom2', 'tom3')
KIT = {
    'seed': 1986,
    'kick': dict(f_start=160.0, f_end=41.2, pitch_tau=0.032, decay=0.14, click=0.35, blip=0.12, drive=1.8, level=1.0,
                 pan=0.0, room=0.04),
    'snare': dict(f1=182.0, f2=287.0, body=0.55, body_decay=0.085, noise=0.85, noise_decay=0.11, lp_lo=4500.0,
                  lp_hi=8000.0, level=0.62, pan=0.0, room=0.08, gate_send=0.9),
    'xstick': dict(level=0.34, pan=0.05, room=0.10),
    'hat': dict(carriers=[2840.0, 3810.0, 4990.0], ratios=[1.37, 1.61, 1.19], index=2.2, noise=0.3, bp=6200.0,
                lp=8000.0, level=0.22, decay_c=0.034, decay_o=0.17, decay_p=0.028, pan=0.35, room=0.10),
    'tom': dict(f={'tom1': 212.0, 'tom2': 158.0, 'tom3': 117.0}, drop=0.45, drop_tau=0.11, decay=0.22, level=0.62,
                pan={'tom1': 0.3, 'tom2': -0.1, 'tom3': -0.4}, room=0.10, gate_send=0.5),
    'crash': dict(level=0.30, decay=1.15, bright_decay=0.35, lp=9000.0, pan={'crash1': -0.5, 'crash2': 0.55},
                  room=0.15),
    'ride': dict(ping_f=3150.0, ping_ratio=1.414, level=0.24, ping_decay=0.22, wash_decay=0.9, pan=-0.4, room=0.12),
    'bell': dict(partials=[812.0, 1219.0, 2041.0, 2867.0], level=0.26, decay=0.55, pan=-0.4, room=0.12),
    'gate': dict(t60=1.8, wet=0.55, hold_lo=0.25, hold_hi=0.34, open=0.002, close=0.04, key_min=0.45, predelay=0.008),
    'room': dict(t60=0.35, wet=1.0),
    'bus': dict(hp=30.0, lp=12000.0),
}


def merge(a, b):
    for k, v in b.items():
        if isinstance(v, dict) and isinstance(a.get(k), dict):
            merge(a[k], v)
        else:
            a[k] = v
    return a


def sos(kind, f, order=2):
    return sg.butter(order, f, kind, fs=SR, output='sos')


def filt(x, *stages):
    for kind, f, order in stages:
        x = sg.sosfilt(sos(kind, f, order), x)
    return x


def tt(dur):
    return np.arange(int(dur * SR)) / SR


def att(t, a):
    return 1 - np.exp(-t / a)


# ------------------------------------------------------------------ the voices: (mono dry, pan, room send, gate send)
def kick(v, rng, P):
    t = tt(0.7)
    f = P['f_end'] + (P['f_start'] - P['f_end']) * np.exp(-t / P['pitch_tau'])
    ph = 2 * np.pi * np.cumsum(f) / SR
    dec = P['decay'] * (0.75 + 0.35 * v)
    body = np.sin(ph) * att(t, 0.0012) * np.exp(-t / dec)
    n = rng.standard_normal(len(t))
    body = np.tanh(P['drive'] * body) / np.tanh(P['drive'])         # the body soft-clipped for punch
    click = filt(n, ('bandpass', [2200, 5200], 2), ('lowpass', 6000, 4)) * np.exp(-t / 0.0011) * att(t, 0.0003)
    blip = np.sin(2 * np.pi * 1050 * t) * np.exp(-t / 0.004) * att(t, 0.0003) * P['blip'] * v
    x = body + click * P['click'] * (0.4 + 0.6 * v) + blip
    return x * P['level'] * v ** 1.4, P['pan'], P['room'], 0.0


def snare(v, rng, P, ghost=False):
    t = tt(0.6)
    drop = 1 - 0.08 * (1 - np.exp(-t / 0.03))                             # the head settles a little flat
    ph1 = 2 * np.pi * np.cumsum(P['f1'] * drop) / SR
    ph2 = 2 * np.pi * np.cumsum(P['f2'] * drop) / SR
    bd = P['body_decay'] * (0.8 + 0.3 * v)
    body = (np.sin(ph1) + 0.6 * np.sin(ph2)) * np.exp(-t / bd) * P['body']
    n = rng.standard_normal(len(t))
    lp = P['lp_lo'] + (P['lp_hi'] - P['lp_lo']) * v                         # harder hits: brighter
    noise = filt(n, ('highpass', 900, 2), ('lowpass', lp, 4)) * np.exp(-t / (P['noise_decay'] * (0.75 + 0.35 * v)))
    crack = filt(n, ('bandpass', [1500, 4500], 2)) * np.exp(-t / 0.002) * 0.6 * v
    x = (body + noise * P['noise'] + crack) * att(t, 0.0006)
    send = P['gate_send'] * (0.15 if ghost else 1.0)
    return x * P['level'] * v ** 1.5, P['pan'], P['room'], send


def xstick(v, rng, P):
    t = tt(0.15)
    n = rng.standard_normal(len(t))
    x = (np.sin(2 * np.pi * 1720 * t) * np.exp(-t / 0.010) + 0.7 * np.sin(2 * np.pi * 510 * t) * np.exp(-t / 0.028) +
         filt(n, ('bandpass', [1800, 5000], 2)) * np.exp(-t / 0.003) * (0.3 + 0.4 * v)) * att(t, 0.0004)
    return x * P['level'] * v ** 1.3, P['pan'], P['room'], 0.0


def metal(t, carriers, ratios, index):
    return sum(np.sin(2 * np.pi * c * t + index * np.sin(2 * np.pi * c * r * t)) for c, r in zip(carriers, ratios))


def hat(v, rng, P, kind):
    dec = {'hat_c': P['decay_c'], 'hat_o': P['decay_o'], 'hat_p': P['decay_p']}[kind] * (0.8 + 0.4 * v)
    t = tt(min(0.9, dec * 7))
    m = metal(t, P['carriers'], P['ratios'], P['index'] * (0.6 + 0.4 * v)) / 3
    n = rng.standard_normal(len(t)) * P['noise']
    bp = P['bp'] * (0.85 + 0.2 * v)
    x = filt(m + n, ('highpass', 2500, 2), ('bandpass', [bp * 0.55, min(bp * 1.5, 9500)], 1), ('lowpass', P['lp'], 6))
    env = att(t, 0.0008 if kind != 'hat_p' else 0.004) * np.exp(-t / dec)
    lvl = 0.6 if kind == 'hat_p' else (1.15 if kind == 'hat_o' else 1.0)
    return x * env * P['level'] * lvl * v ** 1.6, P['pan'], P['room'], 0.0


def tom(v, rng, P, kind):
    t = tt(0.9)
    f0 = P['f'][kind]
    f = f0 * (1 - P['drop'] * (1 - np.exp(-t / P['drop_tau'])))
    ph = 2 * np.pi * np.cumsum(f) / SR
    dec = P['decay'] * (0.8 + 0.4 * v)
    body = (np.sin(ph) + 0.18 * np.sin(2 * ph)) * np.exp(-t / dec)
    n = rng.standard_normal(len(t))
    stick = filt(n, ('bandpass', [900, 3500], 2)) * np.exp(-t / 0.004) * 0.35 * v
    x = (body + stick) * att(t, 0.0008)
    return x * P['level'] * v ** 1.4, P['pan'][kind], P['room'], P['gate_send']


def crash(v, rng, P, kind):
    t = tt(3.2)
    sh = 1.0 if kind == 'crash1' else 1.07
    a = metal(t, [s * sh for s in (431.0, 677.0, 1093.0, 1571.0)], [1.41, 1.83, 1.27, 2.13], 1.6)
    b = metal(t, [s * sh for s in (523.0, 859.0, 1307.0)], [1.73, 1.19, 1.53], 1.2)
    ring = a * b / 12                                                   # ring modulation: a dense inharmonic cluster
    n = rng.standard_normal(len(t)) * 0.35
    raw = filt(ring + n, ('highpass', 600, 2))
    bright = filt(raw, ('highpass', 3500, 2), ('lowpass', P['lp'], 6))
    body = filt(raw, ('lowpass', 4500, 4))
    bd = P['bright_decay'] * (0.7 + 0.5 * v)
    dec = P['decay'] * (0.7 + 0.5 * v)
    x = (body * np.exp(-t / dec) + bright * np.exp(-t / bd) * (0.5 + 0.6 * v)) * att(t, 0.003)
    if P.get('choke'):                                                  # a short crash: grabbed after `choke` s
        i, f = int(P['choke'] * SR), int(0.12 * SR)
        x = x[:i + f] * np.concatenate([np.ones(i), 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, f))])
    return x * P['level'] * v ** 1.3, P['pan'][kind], P['room'], 0.0


def ride(v, rng, P):
    t = tt(1.8)
    ping = np.sin(2 * np.pi * P['ping_f'] * t + 1.1 * v * np.sin(2 * np.pi * P['ping_f'] * P['ping_ratio'] * t))
    ping = filt(ping, ('lowpass', 8500, 4)) * np.exp(-t / P['ping_decay'])
    wash = metal(t, [701.0, 1033.0, 1499.0], [1.37, 1.71, 1.23], 1.4) * metal(t, [911.0, 1373.0], [1.29, 1.61], 1.0) / 6
    wash = filt(wash + 0.2 * rng.standard_normal(len(t)), ('highpass', 1500, 2), ('lowpass', 8000, 6))
    x = (ping * (0.6 + 0.4 * v) + 0.45 * wash * np.exp(-t / (P['wash_decay'] * (0.8 + 0.3 * v)))) * att(t, 0.0015)
    return x * P['level'] * v ** 1.4, P['pan'], P['room'], 0.0


def bell(v, rng, P, Pr):
    t = tt(2.0)
    x = sum(np.sin(2 * np.pi * f * t) * np.exp(-t / (P['decay'] * (1.2 - 0.15 * k))) / (1 + 0.5 * k)
            for k, f in enumerate(P['partials']))
    ping = np.sin(2 * np.pi * Pr['ping_f'] * t + 0.8 * np.sin(2 * np.pi * Pr['ping_f'] * Pr['ping_ratio'] * t))
    x = x + 0.4 * filt(ping, ('lowpass', 8500, 4)) * np.exp(-t / 0.15) * v
    x = filt(x, ('lowpass', 9000, 4)) * att(t, 0.001)
    return x * P['level'] * v ** 1.3, P['pan'], P['room'], 0.0


def voice(piece, v, rng, K):
    if piece == 'kick':
        return kick(v, rng, K['kick'])
    if piece in ('snare', 'ghost'):
        return snare(v, rng, K['snare'], ghost=piece == 'ghost')
    if piece == 'xstick':
        return xstick(v, rng, K['xstick'])
    if piece.startswith('hat'):
        return hat(v, rng, K['hat'], piece)
    if piece.startswith('tom'):
        return tom(v, rng, K['tom'], piece)
    if piece.startswith('crash'):
        return crash(v, rng, K['crash'], piece)
    if piece == 'ride':
        return ride(v, rng, K['ride'])
    if piece == 'bell':
        return bell(v, rng, K['bell'], K['ride'])
    raise ValueError(piece)


# ------------------------------------------------------------------ rooms
def ir(t60, seed, lo=250.0, hi=6000.0, predelay=0.0):
    rng = np.random.default_rng(seed)
    t = tt(t60 * 1.1)
    out = []
    for ch in range(2):
        n = rng.standard_normal(len(t)) * np.exp(-6.91 * t / t60) * att(t, 0.004)
        n = filt(n, ('highpass', lo, 2), ('lowpass', hi, 2))
        n = np.concatenate([np.zeros(int(predelay * SR)), n])
        out.append(n / np.sqrt((n ** 2).sum()))
    return np.stack(out, 1)


def gate_env(hits, n, K):
    """the gate on the big reverb: opened by the backbeat snares and the toms (all of them, whatever --only says)"""
    G = K['gate']
    g = np.zeros(n)
    cl, op = int(G['close'] * SR), int(G['open'] * SR)
    fall = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, cl))
    rise = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, op))             # a 2 ms cosine: never a step on a ringing tail
    for t, p, v in hits:
        if p in ('snare', 'tom1', 'tom2', 'tom3') and v >= G['key_min']:
            i0 = int(t * SR)
            hold = int((G['hold_lo'] + (G['hold_hi'] - G['hold_lo']) * min(1.0, v)) * SR)
            seg = np.concatenate([rise, np.ones(hold), fall])
            i1 = min(n, i0 + len(seg))
            if i1 > i0:
                g[i0:i1] = np.maximum(g[i0:i1], seg[:i1 - i0])
    return g


def render(hits, length, K, only=None, gain=1.0):
    n = int(length * SR)
    dry = np.zeros((n, 2))
    room_send = np.zeros(n)
    gate_send = np.zeros(n)
    hits = sorted(hits)
    for k, (t, p, v) in enumerate(hits):
        rng = np.random.default_rng([K['seed'], k])
        if only and p not in only:
            continue
        x, pan, room, gs = voice(p, float(v), rng, K)
        i = int(round(t * SR))
        x = x[:max(0, n - i)]
        a = (pan + 1) * np.pi / 4
        dry[i:i + len(x), 0] += x * np.cos(a)
        dry[i:i + len(x), 1] += x * np.sin(a)
        room_send[i:i + len(x)] += x * room
        gate_send[i:i + len(x)] += x * gs
    G = K['gate']
    big = sg.oaconvolve(gate_send[:, None], ir(G['t60'], K['seed'] + 1, 300.0, 7000.0, G['predelay']), axes=0)[:n]
    big *= gate_env(hits, n, K)[:, None] * G['wet']
    small = sg.oaconvolve(room_send[:, None], ir(K['room']['t60'], K['seed'] + 2, 200.0, 8000.0), axes=0)[:n] * K['room']['wet']
    st = dry + big + small
    B = K['bus']
    st = np.stack([filt(st[:, c], ('highpass', B['hp'], 2), ('lowpass', B['lp'], 2)) for c in (0, 1)], 1)
    return st * gain


def demo(K):
    hits, t = [], 0.3
    for p in PIECES:
        for v in (0.35, 0.7, 1.0):
            hits.append([t, p, v])
            t += 0.6 if not p.startswith(('crash', 'ride', 'bell')) else 1.6
        t += 0.4
    return hits, t + 3


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('hits', nargs='?')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--kit')
    ap.add_argument('--length', type=float, default=0)
    ap.add_argument('--only', default='')
    ap.add_argument('--gain', type=float, default=1.0)
    ap.add_argument('--demo')
    a = ap.parse_args()
    K = json.loads(json.dumps(KIT))
    if a.kit:
        merge(K, json.load(open(a.kit)))
    import scipy.io.wavfile as w
    if a.demo:
        hits, length = demo(K)
        st = render(hits, length, K)
        w.write(a.demo, SR, (st * 0.89 / np.abs(st).max()).astype(np.float32))
        print('%s: demo, %d hits' % (a.demo, len(hits)))
        return
    H = json.load(open(a.hits))['hits']
    length = a.length or max(t for t, _, _ in H) + 4
    only = set(filter(None, a.only.split(','))) or None
    st = render(H, length, K, only, a.gain)
    w.write(a.out, SR, st.astype(np.float32))
    print('%s: %d hits%s, %.1f s, peak %.3f' % (a.out, len(H), ' (only %s)' % ','.join(sorted(only)) if only else '', length,
                                                 np.abs(st).max()))


if __name__ == '__main__':
    main()
