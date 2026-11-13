"""hi_models.py - the eight jets again, in detail, for the eye only at ULTRA: the same designs from the same stations in
jets.py, at a higher resolution, with the small things a low model leaves out. Flight and hits keep the plain models,
and every part stays within them. Original code for this project; Python standard library only.
usage: python3 hi_models.py OUT_DIR"""
import math
import os
import sys

import geom
import jets
from geom import add, avg, cross, length, loft_rings, mirx, mul, norm, sub, write_glb

SIDES = 36
FUSE_SUB = 5


def chaikin(pts, passes=2):
    for _ in range(passes):
        out = []
        for i, p in enumerate(pts):
            q = pts[(i + 1) % len(pts)]
            out += [(p[0] * 0.75 + q[0] * 0.25, p[1] * 0.75 + q[1] * 0.25), (p[0] * 0.25 + q[0] * 0.75, p[1] * 0.25 + q[1] * 0.75)]
        pts = out
    return pts


def resample(pts, n):
    """n points evenly along a closed outline, starting from its top"""
    d = [0.0]
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        d.append(d[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    out, k = [], 0
    for i in range(n):
        t = d[-1] * i / n
        while d[k + 1] < t:
            k += 1
        a, b = pts[k], pts[(k + 1) % len(pts)]
        f = (t - d[k]) / max(d[k + 1] - d[k], 1e-9)
        out.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
    return out


def outline(name, n):
    if name == 'round':
        return geom._plain_profile('round', n)
    return resample(chaikin(geom._plain_profile(name, 12), 1), n)


def catmull(a, b, c, d, t):
    return b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)))


def fine_stations(st, sub_):
    """between the design's stations, a smooth curve through them; never narrower than zero. A point stays a point."""
    out = []
    for i in range(len(st) - 1):
        a, b = st[max(i - 1, 0)], st[i + 1 + 0]
        s0, s1 = st[i], st[i + 1]
        s_1, s2 = st[max(i - 1, 0)], st[min(i + 2, len(st) - 1)]
        prof0 = s0[5] if len(s0) > 5 else None
        prof1 = s1[5] if len(s1) > 5 else None
        for k in range(sub_):
            t = k / sub_
            v = [s0[0] + (s1[0] - s0[0]) * t]
            for j in range(1, 5):
                x = catmull(s_1[j], s0[j], s1[j], s2[j], t)
                lo, hi = min(s0[j], s1[j]), max(s0[j], s1[j])
                span = hi - lo
                x = max(lo - span * 0.12, min(hi + span * 0.12, x))
                v.append(max(x, 0.0) if j < 4 else x)
            out.append(tuple(v) + ((prof0 if t < 0.5 else prof1,) if prof0 or prof1 else ()) + (i,))
    last = st[-1]
    out.append(tuple(last[:5]) + ((last[5],) if len(last) > 5 else ()) + (len(st) - 2,))
    return out


def hi_body(m, mat, stations, prof='round', sides=12, x=0.0, cap_front=None, cap_back=None, mats=None):
    n = SIDES if sides >= 12 else 16
    short = abs(stations[0][0] - stations[-1][0]) < 1.2
    fine = fine_stations(stations, 2 if short else FUSE_SUB)
    rings, skel, seg = [], [], []
    for row, st in enumerate(fine):
        z, w, ht, hb, y = st[:5]
        name = st[5] if len(st) == 7 and st[5] else prof
        pr = outline(name or prof, n)
        # the nozzle: petals round its last third
        pet = 1.0
        rel = row / max(len(fine) - 1, 1)
        rings.append([(x + w * px * (1 + (0.035 * math.cos(18 * math.atan2(py, px)) * max(0.0, rel - 0.55) * 2.2 if mat == 'nozzle' else 0)),
                       y + (ht if py >= 0 else hb) * py, z) for (px, py) in pr])
        skel.append((x, y, z))
        seg.append(st[-1])
    fm = [mats[seg[i]] for i in range(len(fine) - 1)] if mats else None
    loft_rings(m, mat, rings, skel, cap_front, cap_back, fm)
    if mat == 'glass' and len(stations) >= 3:
        # the canopy's frame: two thin hoops, standing a hair proud of the glass
        z0, z1 = stations[0][0], stations[-1][0]
        for f in (0.42, 0.68):
            zf = z0 + (z1 - z0) * f
            i = min(range(len(fine)), key=lambda q: abs(fine[q][0] - zf))
            _, w, ht, hb, y = fine[i][:5]
            if w > 0.05:
                rr = [[(x + w * 1.015 * px, y + (ht if py >= 0 else hb) * 1.015 * py, zf + dz) for (px, py) in outline('round', n)] for dz in (-0.018, 0.018)]
                loft_rings(m, 'dark', rr, [(x, y, zf - 0.018), (x, y, zf + 0.018)])
    return rings


AIRFOIL = [(0.0, 0.0), (0.012, 0.3), (0.05, 0.58), (0.12, 0.82), (0.25, 0.98), (0.4, 1.0), (0.6, 0.82), (0.8, 0.46), (1.0, 0.0),
           (0.8, -0.46), (0.6, -0.82), (0.4, -1.0), (0.25, -0.98), (0.12, -0.82), (0.05, -0.58), (0.012, -0.3)]


def hi_surface(m, mat, root_le, span_dir, thick_dir, pts, tc, mirror=True, mats=None, cap_tip=None, subdiv=1):
    geom.AIRFOIL = AIRFOIL
    PLAIN_SURFACE(m, mat, root_le, span_dir, thick_dir, pts, tc, mirror, mats, cap_tip, max(subdiv, 3))


def add_details(parts, mats):
    """a pitot probe at the nose and blade antennas along the spine of each airframe"""
    a = parts[0]['mesh']
    pts = [p for ts in a.tris.values() for t in ts for p in t]
    nose = max(pts, key=lambda p: p[2])
    tail = min(p[2] for p in pts)
    hi_body(a, 'dark', [(nose[2] + 0.75, 0, 0, 0, nose[1]), (nose[2] + 0.72, 0.012, 0.012, 0.012, nose[1]), (nose[2] + 0.02, 0.022, 0.022, 0.022, nose[1]),
                        (nose[2] - 0.08, 0.05, 0.05, 0.05, nose[1])], 'round', sides=6, x=nose[0])
    span = nose[2] - tail
    for f, h, c in ((0.36, 0.24, 0.3), (0.56, 0.16, 0.22)):
        z = nose[2] - span * f
        top = max(p[1] for p in pts if abs(p[2] - z) < 0.4 and abs(p[0]) < 0.12)
        geom.fin(a, 'dark', (0, top - 0.02, z), [(0, 0, c), (h, 0.5 * c, 0.5 * c)], 0.09, mirror=False)
    mats.setdefault('dark', jets.DARK)


PLAIN_BODY, PLAIN_SURFACE = geom.body, geom.surface
geom._plain_profile = geom.profile


def build(out):
    os.makedirs(out, exist_ok=True)
    jets.body = geom.body = hi_body
    geom.surface = hi_surface
    for k, fn in jets.BUILDERS.items():
        parts, mats = fn()
        parts = [p for p in parts if p['name'] != 'stores']  # the game hangs the real weapons on it (stores.c)
        add_details(parts, mats)
        n = sum(p['mesh'].count() for p in parts)
        size = write_glb(os.path.join(out, k + '.glb'), k, parts, mats, {'id': k, 'detail': 'hi'})
        print(k, n, 'tris', size, 'bytes')


if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else 'hi')
