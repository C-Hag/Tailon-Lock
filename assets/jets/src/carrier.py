"""CVN-90 Stormhaven: an original nuclear aircraft carrier for Tailon Lock.
Same conventions as the jets: +Y up, +Z bow, +X = PORT (left), metres. Waterline at y = 0."""
import math
from geom import Mesh, body, avg, norm, sub, cross, dot, length

DECK_Y = 20.5            # flight deck surface above the waterline
ANGLE = 9.0              # angled deck, degrees to port of the ship's centreline
A_DIR = (math.sin(math.radians(ANGLE)), math.cos(math.radians(ANGLE)))  # (x, z) unit vector along the landing area
A_ORIGIN = (4.0, -157.0)  # where the angled deck centreline meets the stern round-down
A_PERP = (A_DIR[1], -A_DIR[0])  # across the angled deck, toward port

def a_pt(s, off=0.0):
    """Point on the angled deck: s metres from the stern along its centreline, off metres to port."""
    return (A_ORIGIN[0] + A_DIR[0] * s + A_PERP[0] * off, A_ORIGIN[1] + A_DIR[1] * s + A_PERP[1] * off)

# flight deck outline (x, z), counter-clockwise seen from above
DECK = [(-30, -157), (18, -157), (33, -140), (40, -60), (45, 30), (39, 52), (25, 70), (24, 118), (14, 150), (4, 163),
        (-6, 163), (-18, 148), (-28, 120), (-31, 60), (-31, -100)]

CATS = [  # name, start (x, z), end (x, z)
    ('cat1', (-8.0, 56.0), (-8.0, 151.0)),
    ('cat2', (8.0, 56.0), (8.0, 151.0)),
    ('cat3', a_pt(110, 4), a_pt(190, 4)),
]
WIRES = [45.0, 57.0, 69.0]   # metres from the round-down along the angled deck; target wire = 2
WIRE_HALF = 14.0


def earcut(poly):
    pts = list(range(len(poly)))
    def area2(a, b, c): return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    tris = []
    while len(pts) > 3:
        for i in range(len(pts)):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            if area2(poly[a], poly[b], poly[c]) <= 0: continue
            if any(area2(poly[a], poly[b], poly[p]) >= 0 and area2(poly[b], poly[c], poly[p]) >= 0 and area2(poly[c], poly[a], poly[p]) >= 0
                   for p in pts if p not in (a, b, c)): continue
            tris.append((a, b, c)); pts.pop(i); break
        else:
            raise ValueError('earcut failed')
    tris.append(tuple(pts))
    return tris


def slab(m, mat, poly, y0, y1, side=None):
    """Extrude a simple polygon (x, z) between heights y0 and y1."""
    cx, cz = sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly)
    ref = (cx, (y0 + y1) / 2, cz)
    for (a, b, c) in earcut(poly):
        P = [poly[a], poly[b], poly[c]]
        m.tri(mat, *[(p[0], y1, p[1]) for p in P], (cx, y1 - 1, cz))
        m.tri(side or mat, *[(p[0], y0, p[1]) for p in P], (cx, y0 + 1, cz))
    for i in range(len(poly)):
        p, q = poly[i], poly[(i + 1) % len(poly)]
        mid = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        inward = (mid[0] + (cx - mid[0]) * 0.01, (y0 + y1) / 2, mid[1] + (cz - mid[1]) * 0.01)
        m.quad(side or mat, (p[0], y0, p[1]), (q[0], y0, q[1]), (q[0], y1, q[1]), (p[0], y1, p[1]), inward)


def box(m, mat, x0, x1, y0, y1, z0, z1, top=None):
    c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    V = lambda i, j, k: ((x0, x1)[i], (y0, y1)[j], (z0, z1)[k])
    for (a, b, cc, d), mt in [((V(0, 0, 0), V(1, 0, 0), V(1, 0, 1), V(0, 0, 1)), mat), ((V(0, 1, 0), V(1, 1, 0), V(1, 1, 1), V(0, 1, 1)), top or mat),
                              ((V(0, 0, 0), V(0, 1, 0), V(0, 1, 1), V(0, 0, 1)), mat), ((V(1, 0, 0), V(1, 1, 0), V(1, 1, 1), V(1, 0, 1)), mat),
                              ((V(0, 0, 0), V(1, 0, 0), V(1, 1, 0), V(0, 1, 0)), mat), ((V(0, 0, 1), V(1, 0, 1), V(1, 1, 1), V(0, 1, 1)), mat)]:
        m.quad(mt, a, b, cc, d, c)


def strip(m, mat, p, q, width, y=DECK_Y + 0.06, dash=None):
    """Flat painted line on the deck from p to q (x, z)."""
    dx, dz = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dz); ux, uz = dx / L, dz / L; nx, nz = -uz * width / 2, ux * width / 2
    segs = [(0, L)] if not dash else [(s, min(L, s + dash[0])) for s in [i * (dash[0] + dash[1]) for i in range(int(L // (dash[0] + dash[1])) + 1)]]
    for (s0, s1) in segs:
        a = (p[0] + ux * s0, p[1] + uz * s0); b = (p[0] + ux * s1, p[1] + uz * s1)
        quad = [(a[0] + nx, y, a[1] + nz), (b[0] + nx, y, b[1] + nz), (b[0] - nx, y, b[1] - nz), (a[0] - nx, y, a[1] - nz)]
        ref = (a[0], y - 1, a[1])
        m.quad(mat, *quad, ref)


SEG = {'0': 'abcdef', '1': 'bc', '2': 'abdeg', '3': 'abcdg', '4': 'bcfg', '5': 'acdfg', '6': 'acdefg', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg'}
def digits(m, mat, text, x, z, h=12.0):
    """Seven-segment deck numbers read from the stern (bottom of the digit toward -Z). x is the left edge seen from astern (port = +X)."""
    w = h * 0.55; t = h * 0.12
    for k, ch in enumerate(text):
        x0 = x - k * (w + h * 0.25)  # successive digits go to starboard
        P = {'a': ((x0, z + h), (x0 - w, z + h)), 'b': ((x0 - w, z + h), (x0 - w, z + h / 2)), 'c': ((x0 - w, z + h / 2), (x0 - w, z)),
             'd': ((x0, z), (x0 - w, z)), 'e': ((x0, z + h / 2), (x0, z)), 'f': ((x0, z + h), (x0, z + h / 2)), 'g': ((x0, z + h / 2), (x0 - w, z + h / 2))}
        for sgm in SEG[ch]:
            strip(m, mat, P[sgm][0], P[sgm][1], t)


def build():
    hull, deck, isl, mk = Mesh(), Mesh(), Mesh(), Mesh()
    # hull: flared sides, round bilge, flat transom
    st = [(-158, 22, 19, 4, 0), (-140, 24, 19, 10, 0), (-80, 25, 19, 12, 0), (0, 25, 19, 12, 0), (60, 24, 19, 12, 0),
          (110, 18, 19, 11, 0), (140, 10, 19, 9, 0), (156, 3.5, 19.5, 6, 0), (161, 0.6, 19.5, 2, 0)]
    from geom import _HALF
    _HALF['hull'] = [(0, 1), (0.75, 1), (1, 1), (0.97, 0.35), (0.93, 0.0), (0.88, -0.25), (0.6, -0.8), (0, -1)]
    body(hull, 'hull', st, 'hull', cap_back='hull', cap_front='hull')
    hull.retint(lambda m, c, n: 'boot' if (m == 'hull' and c[1] < -0.01) else m)
    # the flight deck covers the hull top, so drop those faces (saves triangles, avoids hidden overdraw)
    hull.tris = {k: [t for t in v if not (min(p[1] for p in t) > 18.9)] for k, v in hull.tris.items()}
    # flight deck and its edge
    slab(deck, 'deck', DECK, 19.0, DECK_Y, side='hull')
    # island (starboard), bridge windows, mast and fixed radar faces
    box(isl, 'island', -33, -24, DECK_Y, 29, -58, -26)
    box(isl, 'island', -32.5, -25, 29, 32, -54, -30)
    box(isl, 'glass', -32.6, -24.9, 32, 34, -54.1, -29.9)
    box(isl, 'island', -32, -25.5, 34, 37, -52, -32)
    for (x0, x1, z0, z1) in [(-24.05, -23.9, -40, -32), (-33.1, -32.95, -40, -32)]:
        box(isl, 'radar', x0, x1, 29.5, 33.5, z0, z1)
    box(isl, 'radar', -31, -27, 29.5, 33.5, -26.05, -25.9)
    box(isl, 'mast', -29.2, -28.2, 37, 50, -44, -43)
    box(isl, 'mast', -31.5, -26, 46, 46.6, -44.2, -42.8)
    box(isl, 'radar', -29.8, -27.6, 50, 51.2, -44.6, -42.4)
    # deck-edge elevators (two starboard, one port) and close-in weapon sponsons
    for (x0, x1, z0, z1) in [(-31, -20, -10, 10), (-31, -20, -95, -75), (34, 41.5, -40, -20)]:
        box(mk, 'elev', x0, x1, DECK_Y - 0.02, DECK_Y + 0.04, z0, z1)
    for (x, z) in [(-33, 140), (24, 132), (-33, -150), (30, -150)]:
        box(isl, 'island', x - 2, x + 2, 16, 19, z - 2, z + 2)
        box(isl, 'mast', x - 0.8, x + 0.8, 19, 22.5, z - 0.8, z + 0.8)
    # landing area: edge lines, dashed centreline, foul line, round-down stripes
    for off in (-11.5, 11.5):
        strip(mk, 'white', a_pt(0, off), a_pt(205, off), 0.6)
    strip(mk, 'white', a_pt(4), a_pt(205), 0.6, dash=(6, 6))
    strip(mk, 'yellow', a_pt(80, -16), a_pt(80, 13), 0.5)
    for k in range(-5, 6):
        strip(mk, 'red' if k % 2 else 'white', a_pt(0.5, k * 2.2), a_pt(0.5, k * 2.2 + 2.2), 1.0)
    # arresting wires (cross-deck pendants), slightly raised
    wires = []
    for i, s in enumerate(WIRES):
        p, q = a_pt(s, -WIRE_HALF), a_pt(s, WIRE_HALF)
        strip(mk, 'wire', p, q, 0.35, y=DECK_Y + 0.12)
        for e in (p, q):
            box(mk, 'mast', e[0] - 0.6, e[0] + 0.6, DECK_Y, DECK_Y + 0.4, e[1] - 0.6, e[1] + 0.6)
        wires.append({'n': i + 1, 's': s, 'p1': [p[0], DECK_Y, p[1]], 'p2': [q[0], DECK_Y, q[1]]})
    # catapults: track, jet blast deflector behind the shuttle start
    cats = []
    for (nm, a, b) in CATS:
        strip(mk, 'track', a, b, 0.9)
        strip(mk, 'white', (a[0] - 3, a[1]), (a[0] + 3, a[1]), 0.4)
        dx, dz = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dz); ux, uz = dx / L, dz / L
        c = (a[0] - ux * 14, a[1] - uz * 14)
        hw, px, pz = 7.0, uz, -ux
        jb = Mesh()
        P = [(c[0] - px * hw, DECK_Y + 0.05, c[1] - pz * hw), (c[0] + px * hw, DECK_Y + 0.05, c[1] + pz * hw),
             (c[0] + px * hw - ux * 1.5, DECK_Y + 3.2, c[1] + pz * hw - uz * 1.5), (c[0] - px * hw - ux * 1.5, DECK_Y + 3.2, c[1] - pz * hw - uz * 1.5)]
        mk.quad('jbd', *P, (c[0] + ux * 3, DECK_Y + 1, c[1] + uz * 3))
        mk.quad('jbd', *P, (c[0] - ux * 3, DECK_Y + 1, c[1] - uz * 3))
        hd = math.degrees(math.atan2(ux, uz))
        cats.append({'name': nm, 'start': [a[0], DECK_Y, a[1]], 'end': [b[0], DECK_Y, b[1]], 'stroke_m': round(L, 1), 'heading_offset_deg': round(hd, 2)})
    # bow cat centrelines and hull number
    digits(mk, 'white', '90', 7, 112, 13)
    # Fresnel lens landing aid (optical landing system) on the port deck edge, abeam the target wire
    ols = a_pt(WIRES[1] + 30, 20)
    box(isl, 'mast', ols[0] - 1, ols[0] + 1, DECK_Y, DECK_Y + 3.2, ols[1] - 1.5, ols[1] + 1.5)
    box(isl, 'ols', ols[0] - 1.05, ols[0] + 1.05, DECK_Y + 2.0, DECK_Y + 3.0, ols[1] - 1.55, ols[1] + 1.55)
    parts = [{'name': 'hull', 'mesh': hull}, {'name': 'flight_deck', 'mesh': deck}, {'name': 'island', 'mesh': isl}, {'name': 'markings', 'mesh': mk}]
    mats = {'hull': {'color': '#6f777e', 'rough': 0.8}, 'boot': {'color': '#5a2b26', 'rough': 0.9}, 'deck': {'color': '#4a4d50', 'rough': 0.95},
            'island': {'color': '#7a8188', 'rough': 0.8}, 'glass': {'color': '#1d2a36', 'metal': 0.5, 'rough': 0.2}, 'radar': {'color': '#3c4247', 'rough': 0.6},
            'mast': {'color': '#5e656b'}, 'elev': {'color': '#3e4144', 'rough': 0.95}, 'white': {'color': '#e8e8e2'}, 'yellow': {'color': '#e8c23a'},
            'red': {'color': '#c8352b'}, 'wire': {'color': '#c9c9c4', 'metal': 0.8, 'rough': 0.4}, 'track': {'color': '#2a2b2c'}, 'jbd': {'color': '#55595d', 'metal': 0.5},
            'ols': {'color': '#ffb020'}}
    td = a_pt(WIRES[1])
    extras = {
        'id': 'cvn90', 'deck_height_m': DECK_Y, 'waterline_y': 0.0,
        'angled_deck': {'heading_offset_deg': ANGLE, 'note': 'positive = toward +X (port in glTF axes); same sign rule as catapults', 'origin': [A_ORIGIN[0], DECK_Y, A_ORIGIN[1]],
                        'width_m': 23.0, 'length_m': 205.0},
        'landing': {'glide_slope_deg': 3.5, 'hook_touchdown': [td[0], DECK_Y, td[1]], 'target_wire': 2, 'ols': [ols[0], DECK_Y + 2.5, ols[1]]},
        'catapults': cats, 'wires': wires,
        'spawn_parking': [[-22, DECK_Y, 120], [-22, DECK_Y, 95], [-22, DECK_Y, 70], [16, DECK_Y, 100], [-20, DECK_Y, -110], [-20, DECK_Y, -130]],
    }
    return parts, mats, extras


SPEC = dict(
    id='cvn90', designation='CVN-90', name='Stormhaven', role='Nuclear-powered aircraft carrier (CATOBAR)',
    blurb='A large-deck nuclear carrier built for the NF-24 air wing: three electromagnetic catapults, three arresting wires on a 9° angled deck, and an island set far aft so two bow catapults and the landing area can work at the same time.',
    length=320, deck_width=86, beam_wl=41, draft=12, displacement=94000, speed_kn=32, reactors=2, shafts=4, power='2 pressurised-water reactors, 4 shafts, about 210 MW',
    air_wing=64, crew=4300, cat_energy_mj=110, cat_max_end_kt=165, arrest_energy_mj=47, arrest_max_kt=150, wire_runout_m=105,
    design=['Angled deck (9° to port): a jet that misses every wire (a bolter) simply goes to full power and flies off the angled end instead of hitting parked aircraft on the bow.',
            'Three wires 12 m apart; the lens sets a 3.5° glide slope aimed between wires 2 and 3, so the hook catches wire 2 on a normal pass.',
            'Two bow catapults plus one waist catapult on the angled deck: launch from the bow while recovering on the angle.',
            'Island far aft on the starboard side keeps the bow clear for the catapults and gives the air boss a view of the landing area.',
            'Three deck-edge elevators (two starboard, one port) move aircraft to the hangar deck without blocking the flight deck.',
            'Ship steams into the wind at up to 32 kn, which adds about 16 m/s of wind over the deck to every launch and recovery.'],
)
