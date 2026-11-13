#!/usr/bin/env python3
"""tools/film_song_blend.py - the opening film's song as pieces of recordings joined end to end: each piece is levelled to
the first one's loudness, and each one comes in over the last XF seconds of the one before with an equal-power crossfade,
slid by up to half a beat so its onsets fall on the outgoing piece's. Original code for this project; Python standard
library only. Every input is 48 kHz 16-bit stereo WAV.

usage: tools/film_song_blend.py OUT.wav TOTAL_S XF_S FILE:START_S:LEN_S [FILE:START_S:LEN_S ...]
  START_S may be 'end' for the last piece: it then runs to its file's end and the result ends with it.
Then: ear_check, a level to -20 LUFS, and afconvert -d LEI16@32000 to assets/music/heartbeat-intro.wav.

The film's song in 0.2.1, the cut chosen:
  OUT 82 4.2 "Analog Horizons (Remastered).wav:0:46" "Heartbeat in E Minor.wav:end:0"
then -1.6 dB to -20 LUFS (ear_check) and afconvert -d LEI16@32000."""
import array, math, sys, wave

FR = 48000

def load(p):
    w = wave.open(p)
    assert w.getframerate() == FR and w.getnchannels() == 2 and w.getsampwidth() == 2, p
    a = array.array('h'); a.frombytes(w.readframes(w.getnframes())); return a

def env(a, t0, t1, hop=240):
    out, prev = [], 0.0
    for i in range(max(0, int(t0 * FR)), int(t1 * FR) - hop, hop):
        s = a[i * 2:(i + hop) * 2:6]
        if not s: break
        e = sum(abs(x) for x in s) / len(s); out.append(max(0.0, e - prev)); prev = e
    m = sum(out) / max(len(out), 1); return [x - m for x in out]

def rms(a, t0, t1):
    s = a[int(t0 * FR) * 2:int(t1 * FR) * 2:16]; return math.sqrt(sum(x * x for x in s) / max(len(s), 1)) or 1

out_path, TOTAL, XF = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
pieces = []
for spec in sys.argv[4:]:
    f, st, ln = spec.rsplit(':', 2)
    pieces.append([load(f), st, float(ln)])
# where each piece starts in the film (each overlaps the one before by XF), and where in its own file
at, plan = 0.0, []
for k, (a, st, ln) in enumerate(pieces):
    flen = len(a) / 2 / FR
    if st == 'end':
        ln = TOTAL - at; s0 = flen - ln
    else:
        s0 = float(st)
    if k > 0:  # beat-match the entry
        pa, ps, pt = plan[-1]
        ea = env(pa, ps + (at - pt), ps + (at - pt) + XF)
        best = max(range(-55, 56), key=lambda d: sum(x * y for x, y in zip(ea, env(a, s0 + d * 0.005, s0 + d * 0.005 + XF))))
        s0 += best * 0.005
        print('piece %d enters at %.2f s of the film, %.2f s of its file (shift %+d ms)' % (k + 1, at, s0, best * 5))
    plan.append((a, s0, at))
    at += ln - XF
ref = rms(plan[0][0], plan[0][1] + 2, plan[0][1] + 20)
gains = [ref / rms(a, s0 + XF, s0 + XF + 15) for a, s0, t in plan]
n = int(TOTAL * FR); out = array.array('f', [0.0]) * (n * 2)
for k, (a, s0, t0) in enumerate(plan):
    t1 = plan[k + 1][2] + XF if k + 1 < len(plan) else TOTAL
    for i in range(int(t0 * FR), min(n, int(t1 * FR))):
        t = i / FR
        f = 1.0
        if k > 0 and t < t0 + XF: f = math.sin(0.5 * math.pi * (t - t0) / XF)
        if k + 1 < len(plan) and t > t1 - XF: f *= math.cos(0.5 * math.pi * (t - (t1 - XF)) / XF)
        j = int((s0 + t - t0) * FR) * 2
        if 0 <= j and j + 1 < len(a):
            out[2 * i] += a[j] * gains[k] * f; out[2 * i + 1] += a[j + 1] * gains[k] * f
for i in range(n):
    t = i / FR
    k = min(1.0, t / 0.4, (TOTAL - t) / 1.5)
    out[2 * i] *= k; out[2 * i + 1] *= k
peak = max(abs(x) for x in out[::7]) or 1
w = wave.open(out_path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(FR)
w.writeframes(array.array('h', [int(max(-32767, min(32767, x / peak * 16000))) for x in out]).tobytes()); w.close()
