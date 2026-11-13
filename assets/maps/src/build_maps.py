"""Tailon Lock map generator.

Builds the four selectable maps as engine-neutral data (see FORMAT.md):
  twin-isles   Sea and land: two islands in open ocean, a carrier group each
  dune-sea     Desert: dunes, mesas, a dry wadi, salt flat and oases
  iron-coast   Sea with a coastline: land in the west, open sea in the east
  high-valley  Green valleys under a snowy mountain range

Usage: python3 build_maps.py <outdir>
Writes <id>.json, <id>.h16, <id>.mat for each map, plus maps_index.json and maps_game.js.
Everything is deterministic (fixed seeds), so a rebuild gives identical files.
"""
import base64, json, math, os, sys
import numpy as np
import weather

# ---------------------------------------------------------------- grid
N = 129                      # samples per side
SP = 200.0                   # metres between samples
X0 = -12800.0                # world x/z of sample 0
W = SP * (N - 1)             # 25600 m
HSCALE = 0.1                 # stored int16 * HSCALE = metres
ARENA = {'x': 9500, 'z': 11500}
xs = X0 + SP * np.arange(N)
X, Z = np.meshgrid(xs, xs)   # H[j, i]: row j = z, column i = x

def smooth(t): t = np.clip(t, 0, 1); return t * t * (3 - 2 * t)

def noise(seed, cells):
    """Value noise in 0..1 on the grid with `cells` lattice cells across the map."""
    rng = np.random.default_rng(seed)
    L = rng.random((cells + 2, cells + 2))
    u = (X - X0) / W * cells; v = (Z - X0) / W * cells
    i0 = np.floor(u).astype(int); j0 = np.floor(v).astype(int)
    fu = smooth(u - i0); fv = smooth(v - j0)
    a = L[j0, i0]; b = L[j0, i0 + 1]; c = L[j0 + 1, i0]; d = L[j0 + 1, i0 + 1]
    return (a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv

def fbm(seed, cells, octaves=4, gain=0.5):
    s, amp, tot = 0, 1.0, 0
    for o in range(octaves):
        s = s + noise(seed + o * 101, cells * 2 ** o) * amp; tot += amp; amp *= gain
    return s / tot

def ridged(seed, cells, octaves=4, gain=0.5):
    s, amp, tot = 0, 1.0, 0
    for o in range(octaves):
        n = 1 - np.abs(noise(seed + o * 131, cells * 2 ** o) * 2 - 1)
        s = s + n * n * amp; tot += amp; amp *= gain
    return s / tot

def rect_w(cx, cz, hx, hz, blend):
    """1 inside the rectangle, smooth falloff to 0 over `blend` metres outside."""
    d = np.maximum(np.maximum(np.abs(X - cx) - hx, np.abs(Z - cz) - hz), 0)
    return 1 - smooth(d / blend)

def disc_w(cx, cz, r, blend):
    d = np.maximum(np.hypot(X - cx, Z - cz) - r, 0)
    return 1 - smooth(d / blend)

def flatten(H, w, elev): return H * (1 - w) + elev * w

def sample(H, x, z):
    """Surface height at a world point: the same two-triangle rule engines use (FORMAT.md)."""
    u = (x - X0) / SP; v = (z - X0) / SP
    i = min(max(int(math.floor(u)), 0), N - 2); j = min(max(int(math.floor(v)), 0), N - 2)
    fu = min(max(u - i, 0), 1); fv = min(max(v - j, 0), 1)
    a, b, c, d = H[j, i], H[j, i + 1], H[j + 1, i + 1], H[j + 1, i]
    if fu >= fv: return a + (b - a) * fu + (c - b) * fv
    return a + (c - d) * fu + (d - a) * fv

# ---------------------------------------------------------------- materials (shared table, ids are stable)
MATS = [
    ('deep_sea',    '#1f4a7a', 'water', 0.02), ('sea',        '#2c639a', 'water', 0.02),
    ('shallows',    '#3f8fae', 'water', 0.03), ('lake',       '#3a75a0', 'water', 0.02),
    ('beach',       '#d9c58f', 'land', 0.05),  ('grass',      '#5f8f3e', 'land', 0.07),
    ('meadow',      '#7fa84b', 'land', 0.07),  ('forest',     '#3d6b2e', 'land', 0.08),
    ('pine_forest', '#2f5634', 'land', 0.08),  ('field_gold', '#b8a457', 'land', 0.05),
    ('field_green', '#88a94c', 'land', 0.05),  ('scrub',      '#93904f', 'land', 0.07),
    ('rock',        '#7c7466', 'land', 0.08),  ('cliff',      '#5d564c', 'land', 0.08),
    ('snow',        '#eef1f6', 'land', 0.03),  ('sand',       '#d8b47a', 'land', 0.05),
    ('dune_crest',  '#e8c98f', 'land', 0.04),  ('red_rock',   '#b5653e', 'land', 0.08),
    ('mesa_top',    '#9c6a45', 'land', 0.05),  ('salt_flat',  '#ece4cf', 'land', 0.02),
    ('wadi_gravel', '#a99272', 'land', 0.06),  ('apron',      '#8a8f80', 'land', 0.02),
    ('town',        '#9a8f86', 'land', 0.10),  ('oasis',      '#4f8a3a', 'land', 0.06),
]
M = {name: i for i, (name, *_ ) in enumerate(MATS)}

# ---------------------------------------------------------------- time of day
# Each map tints these four presets (see map 'tod_tint'). Colours are hex, sun is the unit
# vector pointing TOWARD the sun in game axes (+X right, +Y up, +Z toward the red base).
def sunvec(az_deg, el_deg):
    az, el = math.radians(az_deg), math.radians(el_deg)
    return [round(math.cos(el) * math.sin(az), 4), round(math.sin(el), 4), round(math.cos(el) * math.cos(az), 4)]

TOD = {
    'dawn':  dict(label='Dawn',  sky_top='#3b4f8f', sky_horizon='#f2a774', fog='#d9a88e', fog_density=1.0,
                  sun=sunvec(80, 7),   sun_color='#ffb36b', sun_intensity=0.9, ambient='#8a86a8', ambient_intensity=0.72,
                  water_tint='#e09a78', stars=0.15, lights=0.6),
    'day':   dict(label='Day',   sky_top='#3d5cc0', sky_horizon='#b6cce6', fog='#b6cce4', fog_density=0.8,
                  sun=sunvec(155, 52), sun_color='#fff6e4', sun_intensity=0.95, ambient='#a9bbdc', ambient_intensity=0.58,
                  water_tint='#ffffff', stars=0.0, lights=0.0),
    'dusk':  dict(label='Dusk',  sky_top='#2a2860', sky_horizon='#e8705a', fog='#a8657a', fog_density=1.1,
                  sun=sunvec(-95, 5),  sun_color='#ff7f4a', sun_intensity=0.85, ambient='#83709a', ambient_intensity=0.68,
                  water_tint='#d0706a', stars=0.3, lights=0.8),
    'night': dict(label='Night', sky_top='#05070f', sky_horizon='#18233f', fog='#101a30', fog_density=1.4,
                  sun=sunvec(-140, 38), sun_color='#9fb4e8', sun_intensity=0.30, ambient='#3a4870', ambient_intensity=0.42,
                  water_tint='#3a4a78', stars=1.0, lights=1.0, moon=True),
}

LIGHT_COLORS = {'runway_edge': '#ffe6a8', 'runway_end': '#5cff7a', 'approach': '#ffffff', 'deck': '#7fd0ff',
                'deck_edge': '#ffd27a', 'ols': '#ffb000', 'mast': '#ff4040', 'town': '#ffc46b', 'beacon': '#ff4040', 'lighthouse': '#fff4c0'}

# ---------------------------------------------------------------- common placement
TARGET_PADS = {'sam': 40, 'fuel': 40, 'radar': 30, 'hangar': 60}
GAME_OFFSETS = [(-1500, 1100, 'sam'), (1500, 1100, 'sam'), (-800, -600, 'fuel'), (800, -600, 'fuel'), (-2000, 200, 'radar'), (2000, 200, 'hangar')]

class Map:
    def __init__(self, mid, name, request, blurb, seed):
        self.id, self.name, self.request, self.blurb, self.seed = mid, name, request, blurb, seed
        self.H = np.zeros((N, N)); self.water_level = None
        self.bases, self.carriers, self.targets, self.props, self.lights = [], [], [], [], []
        self.tanker = None; self.sky_far = '#a7bba4'; self.tint = {}
        self.extra_water = np.zeros((N - 1, N - 1), bool)  # lakes/ponds (flat cells at their own level)
        self.rng = np.random.default_rng(seed)

    def add_base(self, team, x, z, elev):
        heading = 0 if team == 0 else 180
        self.H = flatten(self.H, rect_w(x, z, 260, 1250, 900), elev)
        self.bases.append(dict(team=team, kind='runway', x=x, z=z, elev=round(elev, 1), heading=heading, length=2000, width=90))

    def add_target(self, team, typ, x, z, elev=None, pad=True):
        if pad:
            e = elev if elev is not None else max(sample(self.H, x, z), 12)
            self.H = flatten(self.H, disc_w(x, z, 120, 420), e)
        self.targets.append(dict(team=team, type=typ, x=round(x), z=round(z), heading=0 if team == 0 else 180))

    def add_carrier(self, team, x, z):
        heading = 0 if team == 0 else 180
        self.carriers.append(dict(team=team, x=x, z=z, heading=heading, speed=0, length=320, beam=86, deck_height=20.5,
                                  model='cvn90', model_files=['../jets/carrier_game.js', '../jets/cvn90.glb', '../jets/carrier_data.json']))
        d = 1 if team == 0 else -1
        # two escort frigates count as this team's naval targets
        for sx in (-1, 1):
            self.targets.append(dict(team=team, type='ship', x=round(x + sx * 900), z=round(z - d * 700), heading=heading))

    def base_targets(self, b, offsets=GAME_OFFSETS, skip=()):
        d = 1 if b['team'] == 0 else -1
        for dx, dz, typ in offsets:
            if typ in skip: continue
            self.add_target(b['team'], typ, b['x'] + dx, b['z'] + dz * d, elev=b['elev'])

# ---------------------------------------------------------------- 1. Twin Isles (sea and land)
def twin_isles():
    m = Map('twin-isles', 'Twin Isles', 'Sea and land',
            'Two green islands face each other across open ocean. Each side flies from an island airfield or its carrier, '
            'with a volcanic peak beside every base and rocky islets in the strait.', 4101)
    m.water_level = 0.0; m.sky_far = '#2c639a'
    warp = (fbm(11, 3) - 0.5) * 2600
    H = -380 + fbm(12, 2) * 160          # seabed
    for t, (cx, cz) in enumerate([(-600, -8300), (600, 8300)]):
        dx = (X - cx + warp) / 5600; dz = (Z - cz + warp * 0.6) / 3700
        d = np.sqrt(dx * dx + dz * dz)
        land = (1 - d) * 520 + fbm(20 + t, 6) * 260 - 60
        peak_x = cx + (3200 if t == 0 else -3200); peak_z = cz + (-900 if t == 0 else 900)
        pk = np.hypot(X - peak_x, Z - peak_z)
        land += np.maximum(0, 1 - pk / 2600) ** 1.6 * 1350 * (0.8 + 0.4 * ridged(30 + t, 8))
        # crater on the volcano
        land -= np.maximum(0, 1 - pk / 360) * 160
        H = np.maximum(H, np.where(d < 1.25, land, -400))
    # islets in the strait
    for k, (ix, iz, r, h) in enumerate([(-5200, -1600, 900, 210), (4600, 1900, 1000, 260), (300, 300, 650, 140),
                                        (-7400, 3400, 700, 160), (7600, -3200, 760, 190), (-1800, 4300, 420, 90), (-2900, -1200, 460, 110)]):
        di = np.hypot(X - ix + warp * 0.3, Z - iz) / r
        H = np.maximum(H, (1 - di) * h + ridged(40 + k, 10) * h * 0.5 - 30)
    m.H = H
    for t, (bx, bz) in enumerate([(-600, -9000), (600, 9000)]):
        elev = max(28, float(np.median(H[(np.abs(X - bx) < 400) & (np.abs(Z - bz) < 1200)])))
        m.add_base(t, bx, bz, min(elev, 70))
    for b in m.bases: m.base_targets(b, skip=('sam',))
    m.add_carrier(0, 4800, -2300); m.add_carrier(1, -4800, 2300)
    m.tanker = dict(x=0, z=0, radius=3200, alt=2300)
    return m

# ---------------------------------------------------------------- 2. Dune Sea (desert)
def dune_sea():
    m = Map('dune-sea', 'Dune Sea', 'Desert',
            'A sand sea of long dune ridges broken by flat-topped mesas, with a dry wadi winding across the line, '
            'a white salt flat, and a palm oasis by each airfield.', 4202)
    m.sky_far = '#d8b47a'
    m.tint = {'sky_horizon': '#e8d2b0', 'fog': '#e2cba6'}
    # long transverse dunes: noise stretched along x, warped
    warp = (fbm(51, 3) - 0.5) * 3000
    zz = Z + warp
    dunes = 0.5 + 0.5 * np.sin(zz / 520 + fbm(52, 4) * 6)
    dunes = dunes ** 2.2 * (40 + 70 * fbm(53, 3))
    H = 90 + fbm(54, 2, 3) * 140 + dunes
    # rocky ranges on the flanks
    flank = np.maximum(0, (np.abs(X) - 7600) / 2400)
    H += np.minimum(flank, 1.6) * ridged(55, 6) * 700
    # mesas: flat tops with steep terraced sides
    mesas = []
    rng = np.random.default_rng(56)
    while len(mesas) < 11:
        mx, mz = rng.uniform(-8000, 8000), rng.uniform(-6800, 6800)
        if abs(mz) < 900: continue
        if any(math.hypot(mx - a, mz - b) < 2600 for a, b, _, _ in mesas): continue
        mesas.append((mx, mz, rng.uniform(600, 1400), rng.uniform(240, 520)))
    mesa_mask = np.zeros_like(H)
    for k, (mx, mz, r, h) in enumerate(mesas):
        d = np.hypot(X - mx, (Z - mz) * 1.3) / r + (fbm(60 + k, 8) - 0.5) * 0.5
        top = 1 - smooth((d - 0.85) / 0.35)
        step = np.where(d < 1.35, np.round(top * 3) / 3, 0)   # terraces
        H = np.maximum(H, 90 + h * np.maximum(top, step * 0.9))
        mesa_mask = np.maximum(mesa_mask, (d < 0.85).astype(float))
    # wadi: dry meandering riverbed crossing x at z near 0
    wz = 600 * np.sin(X / 1900) + 300 * np.sin(X / 760 + 1)
    wd = np.abs(Z - wz)
    H -= np.maximum(0, 1 - wd / 650) ** 1.5 * 110
    m.wadi = wd < 260
    # salt flat
    sw = disc_w(-4800, -3600, 1300, 700); H = flatten(H, sw, 72); m.salt = (np.hypot(X + 4800, Z + 3600) < 1400)
    m.H = H; m.mesa_mask = mesa_mask
    for t, (bx, bz) in enumerate([(0, -9000), (0, 9000)]):
        m.add_base(t, bx, bz, 110)
    for b in m.bases: m.base_targets(b)
    # oasis pond and palm grove beside each base
    m.oases = []
    for t, (ox, oz) in enumerate([(2900, -7400), (-2900, 7400)]):
        m.H = flatten(m.H, disc_w(ox, oz, 500, 600), 96); m.oases.append((ox, oz))
    m.tanker = dict(x=0, z=0, radius=3200, alt=2300)
    return m

# ---------------------------------------------------------------- 3. Iron Coast (sea with a coastline)
def coast_x(z): return -900 + 1700 * np.sin(z / 5200 + 0.6) + 600 * np.sin(z / 1700 + 2.1)

def iron_coast():
    m = Map('iron-coast', 'Iron Coast', 'Sea with a coastline',
            'A long green coastline runs between the two bases, with cliffs, beaches, a fjord and a harbour town. '
            'Both sides have a coastal airfield and a carrier group out at sea.', 4303)
    m.water_level = 0.0; m.sky_far = '#2c639a'
    warp = (fbm(71, 4) - 0.5) * 1400
    cx = coast_x(Z) + warp
    d = cx - X                              # >0 on land (west), <0 at sea (east)
    cliffy = smooth((fbm(72, 3) - 0.48) / 0.12)
    land = 6 + np.maximum(d, 0) * 0.05 + smooth(d / 500) * (25 + cliffy * 130) + fbm(73, 5) * 340 * smooth(d / 2500)
    # inland ridge in the far west, high enough for snow on its peaks
    ridge = smooth((-X - 7000) / 3000)
    land += ridge * ridged(74, 5) * 1500
    sea = -8 + d * 0.06 - fbm(75, 3) * 60
    H = np.where(d > 0, land, np.minimum(sea, 4))
    # fjord cutting inland at z ~ 0
    fz = 400 * np.sin(X / 1300)
    fj = np.clip(1.8 * (1 - np.abs(Z - fz) / 650), 0, 1) * smooth((X - (-7200)) / 1200)
    H = H * (1 - fj) + np.minimum(H, -30) * fj
    # offshore islets
    for k, (ix, iz, r, h) in enumerate([(5200, -2200, 700, 150), (8400, 1500, 800, 210), (3600, 6400, 500, 90), (8200, -6200, 600, 120)]):
        di = np.hypot(X - ix, Z - iz) / r
        H = np.maximum(H, (1 - di) * h + ridged(80 + k, 10) * h * 0.4 - 25)
    m.H = H; m.coast_d = d
    for t, bz in enumerate([-9000, 9000]):
        bx = float(coast_x(bz)) - 3000
        m.add_base(t, round(bx), bz, 60)
    for b in m.bases: m.base_targets(b, skip=('sam',))
    m.add_carrier(0, 6400, -5200); m.add_carrier(1, 6400, 5200)
    # harbour town at the fjord mouth
    m.town = (float(coast_x(0)) - 900, -700)
    m.H = flatten(m.H, disc_w(m.town[0], m.town[1], 450, 500), 14)
    m.tanker = dict(x=1500, z=0, radius=3200, alt=2300)
    return m

# ---------------------------------------------------------------- 4. High Valley (green with snowy mountains)
def high_valley():
    m = Map('high-valley', 'High Valley', 'Green landscapes and snowy mountains',
            'Green valleys of farms, forest and lakes, split by a snow-capped range with one low pass. '
            'Fly the pass under the peaks, or climb over them.', 4404)
    m.sky_far = '#8fa6b8'
    m.tint = {'sky_horizon': '#c2d6ea'}
    base = 180 + fbm(91, 3) * 260
    # central range across the line (z ~ 0), bending; the pass sits near x = -1500
    rz = 900 * np.sin(X / 3800 + 0.4)
    rd = np.abs(Z - rz)
    core = np.maximum(0, 1 - rd / 4400) ** 1.2
    peaks = ridged(92, 5, 5) * 0.75 + 0.25
    pass_cut = 1 - 0.62 * np.exp(-((X + 1500) / 1500) ** 2)
    rng_h = core * peaks * 3000 * pass_cut
    # side ranges along the east and west edges
    side = smooth((np.abs(X) - 7400) / 2600)
    rng_h = np.maximum(rng_h, side * (ridged(93, 6) * 0.7 + 0.3) * 2400)
    H = base + rng_h
    # lakes: a big lake in each valley, flat at its level
    m.lakes = [(-5200, -5600, 1300, 900), (5600, 5200, 1500, 1000)]
    for lx, lz, r, r2 in m.lakes:
        dl = np.hypot((X - lx) / r, (Z - lz) / r2) + (fbm(95, 8) - 0.5) * 0.3
        lvl = 175
        H = np.where(dl < 1.0, lvl - 1.0, H * smooth((dl - 1.0) / 0.6) + (lvl + 4) * (1 - smooth((dl - 1.0) / 0.6)))
    m.H = H
    for t, (bx, bz) in enumerate([(1200, -9000), (-1200, 9000)]):
        elev = float(np.median(H[(np.abs(X - bx) < 400) & (np.abs(Z - bz) < 1200)]))
        m.add_base(t, bx, bz, round(max(elev, 200)))
    for b in m.bases: m.base_targets(b)
    m.villages = [(-2600, -6800), (4200, -8200), (2600, 6600), (-4600, 8600), (-6200, -2600), (6000, 2400)]
    for vx, vz in m.villages:
        m.H = flatten(m.H, disc_w(vx, vz, 250, 400), max(sample(m.H, vx, vz), 190))
    m.tanker = dict(x=0, z=0, radius=3200, alt=3600)
    return m

# ---------------------------------------------------------------- surface materials
def corners_mean(A): return (A[:-1, :-1] + A[:-1, 1:] + A[1:, :-1] + A[1:, 1:]) / 4
def corners_max(A): return np.maximum.reduce([A[:-1, :-1], A[:-1, 1:], A[1:, :-1], A[1:, 1:]])

def slope_cells(H):
    gx = ((H[:-1, 1:] + H[1:, 1:]) - (H[:-1, :-1] + H[1:, :-1])) / (2 * SP)
    gz = ((H[1:, :-1] + H[1:, 1:]) - (H[:-1, :-1] + H[:-1, 1:])) / (2 * SP)
    return np.hypot(gx, gz)

CX, CZ = corners_mean(X), corners_mean(Z)

def cell_noise(seed, cells):
    return corners_mean(noise(seed, cells))

def in_rect(b, hx, hz): return (np.abs(CX - b['x']) < hx) & (np.abs(CZ - b['z']) < hz)

def materials(m):
    raw = m.H.copy()
    if m.water_level is not None:
        m.H = np.maximum(m.H, m.water_level)          # sea surface is flat at sea level
    h = corners_mean(m.H); hmax = corners_max(m.H); hraw = corners_mean(raw); s = slope_cells(m.H)
    n1, n2, n3 = cell_noise(m.seed + 1, 24), cell_noise(m.seed + 2, 48), cell_noise(m.seed + 3, 12)
    mat = np.full(h.shape, M['grass'], np.uint8)
    if m.id == 'twin-isles':
        mat[:] = M['grass']; mat[n1 > 0.55] = M['forest']; mat[(n2 > 0.62)] = M['meadow']
        mat[(h > 420) & (n1 > 0.4)] = M['scrub']; mat[s > 0.45] = M['rock']; mat[h > 820] = M['rock']
        mat[(s > 0.9)] = M['cliff']   # tropical: no snow, the volcano tops out in bare rock
        mat[(h < 22) & (s < 0.25)] = M['beach']
    elif m.id == 'dune-sea':
        mat[:] = M['sand']; mat[n2 > 0.6] = M['dune_crest']
        # dune crests where the surface bulges above the local average
        mat[s > 0.30] = M['red_rock']; mat[m_mesa(m) & (s < 0.2)] = M['mesa_top']
        mat[corners_mean(np.abs(X)) > 8200] = np.where(s[corners_mean(np.abs(X)) > 8200] > 0.18, M['red_rock'], M['scrub'])
        mat[corners_mean(m.wadi.astype(float)) > 0.5] = M['wadi_gravel']
        mat[corners_mean(m.salt.astype(float)) > 0.5] = M['salt_flat']
        for ox, oz in m.oases:
            dd = np.hypot(CX - ox, CZ - oz)
            mat[dd < 1000] = M['oasis']; mat[dd < 420] = M['lake']
    elif m.id == 'iron-coast':
        dd = corners_mean(m.coast_d)
        mat[:] = M['grass']; mat[n1 > 0.56] = M['forest']; mat[(n2 > 0.6) & (h < 300)] = M['field_green']
        mat[(n2 < 0.3) & (h < 300)] = M['field_gold']; mat[(h > 500) & (n1 > 0.45)] = M['pine_forest']
        mat[s > 0.42] = M['rock']; mat[s > 0.75] = M['cliff']; mat[h > 1000] = M['rock']; mat[(h > 1250) & (s < 0.9)] = M['snow']
        mat[(h < 18) & (s < 0.2)] = M['beach']
        tx, tz = m.town; mat[np.hypot(CX - tx, CZ - tz) < 520] = M['town']
    elif m.id == 'high-valley':
        mat[:] = M['meadow']; mat[n1 > 0.52] = M['grass']
        farm = (h < 420) & (n3 > 0.45)
        bi = (np.floor(CX / 400 + n2 * 2) * 7919 + np.floor(CZ / 600) * 104729).astype(np.int64)
        pick = (bi * 2654435761 % 4294967296) / 4294967296
        mat[farm & (pick < 0.35)] = M['field_gold']; mat[farm & (pick >= 0.35) & (pick < 0.7)] = M['field_green']
        mat[(h > 350) & (n2 > 0.42)] = M['forest']; mat[(h > 700) & (h < 1500) & (n2 > 0.3)] = M['pine_forest']
        mat[(h > 1300) | (s > 0.55)] = M['rock']; mat[s > 0.95] = M['cliff']
        snowline = 1650 + (n1 - 0.5) * 500
        mat[(h > snowline) & (s < 1.1)] = M['snow']
        for vx, vz in m.villages: mat[np.hypot(CX - vx, CZ - vz) < 260] = M['town']
        mat[(hmax < 175.5) & (h < 175.5)] = M['lake']
    # water from the flat sea surface
    if m.water_level is not None:
        wet = hmax <= m.water_level + 0.01
        mat[wet] = M['shallows']; mat[wet & (hraw < -45)] = M['sea']; mat[wet & (hraw < -180)] = M['deep_sea']
    # airfield aprons
    for b in m.bases: mat[in_rect(b, 260, 1150)] = M['apron']
    m.mat = mat
    # lakes must be truly flat: snap all corners of lake cells to the lowest corner
    if m.id in ('high-valley', 'dune-sea'):
        lake = mat == M['lake']
        for j, i in zip(*np.nonzero(lake)):
            lv = 175.0 if m.id == 'high-valley' else 96.0
            m.H[j:j + 2, i:i + 2] = lv

def m_mesa(m): return corners_mean(m.mesa_mask) > 0.5

# ---------------------------------------------------------------- props and lights
def is_water(m, x, z):
    i = int((x - X0) // SP); j = int((z - X0) // SP)
    if not (0 <= i < N - 1 and 0 <= j < N - 1): return True
    return MATS[m.mat[j, i]][2] == 'water'

def mat_at(m, x, z):
    i = int(min(max((x - X0) // SP, 0), N - 2)); j = int(min(max((z - X0) // SP, 0), N - 2))
    return MATS[m.mat[j, i]][0]

def clear_of_sites(m, x, z, r=320):
    for b in m.bases:
        if abs(x - b['x']) < 320 + r and abs(z - b['z']) < 1250 + r: return False
    for t in m.targets:
        if t['type'] != 'ship' and math.hypot(x - t['x'], z - t['z']) < r: return False
    return True

def props(m):
    rng = np.random.default_rng(m.seed + 77)
    want = {'twin-isles': {'forest': 'tree', 'grass': 'tree', 'meadow': 'tree', 'beach': 'palm', 'rock': 'rock'},
            'dune-sea': {'oasis': 'palm', 'red_rock': 'rock', 'scrub': 'cactus', 'sand': 'cactus', 'wadi_gravel': 'rock'},
            'iron-coast': {'forest': 'tree', 'pine_forest': 'pine', 'grass': 'tree', 'town': 'house', 'rock': 'rock', 'field_green': 'house'},
            'high-valley': {'forest': 'tree', 'pine_forest': 'pine', 'meadow': 'tree', 'town': 'house', 'rock': 'rock', 'field_gold': 'house'}}[m.id]
    dens = {'tree': 0.5, 'pine': 0.6, 'palm': 0.5, 'rock': 0.08, 'cactus': 0.02, 'house': 0.04}
    out = []
    tries = 0
    while len(out) < 420 and tries < 60000:
        tries += 1
        x, z = rng.uniform(-11500, 11500), rng.uniform(-12000, 12000)
        mt = mat_at(m, x, z)
        if mt not in want: continue
        kind = want[mt]
        if mt == 'town': kind = 'house'
        if rng.random() > (1.0 if mt in ('town', 'oasis') else dens[kind]): continue
        if not clear_of_sites(m, x, z): continue
        out.append(dict(type=kind, x=round(x), z=round(z), y=round(float(sample(m.H, x, z)), 1),
                        s=round(float(rng.uniform(0.8, 1.4)), 2), rot=round(float(rng.uniform(0, 360)))))
    # town houses: dense cluster
    towns = [m.town] if m.id == 'iron-coast' else getattr(m, 'villages', [])
    for tx, tz in towns:
        for _ in range(26 if m.id == 'iron-coast' else 10):
            x, z = tx + rng.normal(0, 200), tz + rng.normal(0, 200)
            if is_water(m, x, z): continue
            out.append(dict(type='house', x=round(x), z=round(z), y=round(float(sample(m.H, x, z)), 1),
                            s=round(float(rng.uniform(0.8, 1.5)), 2), rot=round(float(rng.uniform(0, 90)))))
    if m.id == 'iron-coast':
        for x, z in [(float(coast_x(-4800)) + 120, -4800), (float(coast_x(3800)) + 100, 3800)]:
            out.append(dict(type='lighthouse', x=round(x), z=round(z), y=round(float(sample(m.H, x, z)), 1), s=1, rot=0))
    m.props = out

def lights(m):
    L = []
    def add(kind, x, y, z, size=1.0, blink=0): L.append(dict(kind=kind, x=round(x, 1), y=round(y, 1), z=round(z, 1), size=size, blink=blink))
    for b in m.bases:
        y = b['elev'] + 1
        for zz in range(-1000, 1001, 100):
            for sx in (-48, 48): add('runway_edge', b['x'] + sx, y, b['z'] + zz)
        for sx in range(-40, 41, 10):
            add('runway_end', b['x'] + sx, y, b['z'] - 1005); add('runway_end', b['x'] + sx, y, b['z'] + 1005)
        d = 1 if b['team'] == 0 else -1
        for k in range(1, 8):  # approach lights before the threshold
            add('approach', b['x'], y + 2, b['z'] - d * (1000 + k * 60), 1.4, 0.0)
        add('beacon', b['x'] + 260, y + 30, b['z'], 2.0, 1.2)
    for c in m.carriers:
        # CVN-90 deck layout in carrier-local game axes (+X starboard, +Z bow), from jets/carrier_game.js
        y = c['deck_height'] + 0.3
        def cl(kind, lx, ly, lz, size=1.0, blink=0):
            wx, wz = carrier_to_world(c, lx, lz); add(kind, wx, ly, wz, size, blink)
        ang = math.radians(-9); ux, uz = math.sin(ang), math.cos(ang); px, pz = uz, -ux
        for k in range(14):                                   # angled landing deck: centreline and edges
            t = k * 205 / 13; ox, oz = -4 + ux * t, -157 + uz * t
            cl('deck', ox, y, oz)
            for sgn in (-1, 1): cl('deck_edge', ox + px * 11.5 * sgn, y, oz + pz * 11.5 * sgn)
        for cx in (-8, 8):                                    # bow catapult tracks
            for k in range(6): cl('deck', cx, y, 56 + k * 19, 0.8)
        for k in range(-7, 8):                                # flight deck outline
            cl('deck_edge', 40, y, k * 21, 0.8)
            cl('deck_edge', -40, y, k * 21, 0.8)
        cl('ols', -37.4, 23.0, -74.2, 1.6)                    # landing lens ("meatball")
        cl('mast', 28.5, 52, -40, 2.0, 0.9)
    for t in m.targets:
        if t['type'] == 'ship': add('mast', t['x'], 26, t['z'], 1.5, 0.7)
        elif t['type'] == 'radar': add('beacon', t['x'], sample(m.H, t['x'], t['z']) + 36, t['z'], 1.6, 1.0)
    for p in m.props:
        if p['type'] == 'house': add('town', p['x'], p['y'] + 4, p['z'], 1.0, 0)
        if p['type'] == 'lighthouse': add('lighthouse', p['x'], p['y'] + 34, p['z'], 3.0, 2.5)
    m.lights = L

def carrier_to_world(c, lx, lz):
    h = math.radians(c['heading'])
    return c['x'] + lx * math.cos(h) + lz * math.sin(h), c['z'] - lx * math.sin(h) + lz * math.cos(h)

# ---------------------------------------------------------------- spawns and checks
def spawns(m):
    S = []
    for t in (0, 1):
        d = 1 if t == 0 else -1
        b = next(b for b in m.bases if b['team'] == t)
        S.append(dict(team=t, kind='air', x=b['x'], y=max(1500, round(b['elev'] + 1400)), z=b['z'] + d * 600, heading=b['heading'], ref='base'))
        S.append(dict(team=t, kind='runway', x=b['x'], y=round(b['elev'] + 3, 1), z=b['z'] - d * 850, heading=b['heading'], ref='base'))
        for c in m.carriers:
            if c['team'] == t:
                sx, sz = carrier_to_world(c, -8, 56)   # on catapult 1
                S.append(dict(team=t, kind='carrier', x=round(sx, 1), y=c['deck_height'] + 3, z=round(sz, 1), heading=c['heading'], ref='carrier', catapult='cat1'))
                S.append(dict(team=t, kind='air', x=c['x'], y=1500, z=c['z'] + d * 600, heading=c['heading'], ref='carrier'))
    m.spawns = S

def check(m):
    errs, H = [], m.H
    peak = float(H.max())
    if peak > 3200: errs.append(f'peak {peak:.0f} m over 3200')
    for b in m.bases:
        hs = [sample(H, b['x'] + dx, b['z'] + dz) for dx in (-45, 0, 45) for dz in range(-1000, 1001, 100)]
        if max(hs) - min(hs) > 0.5: errs.append(f"runway team {b['team']} not flat ({max(hs) - min(hs):.1f} m)")
        if any(is_water(m, b['x'] + dx, b['z'] + dz) for dx in (-60, 60) for dz in range(-1000, 1001, 200)):
            errs.append(f"runway team {b['team']} touches water")
        # climb-out: nothing within 2 km ahead/behind higher than 400 m above the runway
        d = 1 if b['team'] == 0 else -1
        for dz in range(1000, 4001, 200):
            for dz2 in (dz, -dz):
                if sample(H, b['x'], b['z'] + d * dz2) > b['elev'] + 400 + (dz - 1000) * 0.3:
                    errs.append(f"runway team {b['team']} obstructed at {dz2} m"); break
    for t in m.targets:
        w = is_water(m, t['x'], t['z'])
        if t['type'] == 'ship' and not w: errs.append(f"ship at {t['x']},{t['z']} on land")
        if t['type'] != 'ship' and w: errs.append(f"{t['type']} at {t['x']},{t['z']} in water")
        if abs(t['x']) > ARENA['x'] - 300 or abs(t['z']) > ARENA['z'] - 300: errs.append(f"{t['type']} outside arena")
    for c in m.carriers:
        for a in range(0, 360, 30):
            x, z = c['x'] + 700 * math.cos(math.radians(a)), c['z'] + 700 * math.sin(math.radians(a))
            if not is_water(m, x, z) or m.mat[int((z - X0) // SP), int((x - X0) // SP)] == M['shallows']:
                errs.append(f"carrier team {c['team']} not in open water"); break
    tk = m.tanker
    for a in range(0, 360, 5):
        x, z = tk['x'] + tk['radius'] * math.cos(math.radians(a)), tk['z'] + tk['radius'] * math.sin(math.radians(a))
        hmax = max(sample(H, x + dx, z + dz) for dx in (-600, 0, 600) for dz in (-600, 0, 600))
        if hmax > tk['alt'] - 700: errs.append(f'tanker orbit clearance {tk["alt"] - hmax:.0f} m'); break
    for s in m.spawns:
        if s['kind'] == 'air':
            hmax = max(sample(H, s['x'] + dx, s['z'] + dz) for dx in range(-1600, 1601, 400) for dz in range(-1600, 1601, 400))
            if s['y'] - hmax < 700: errs.append(f"air spawn team {s['team']} clearance {s['y'] - hmax:.0f} m")
    return errs

def stats(m):
    wet = np.array([MATS[k][2] == 'water' for k in range(len(MATS))])[m.mat]
    inA = (np.abs(CX) < ARENA['x']) & (np.abs(CZ) < ARENA['z'])
    j, i = np.unravel_index(np.argmax(m.H), m.H.shape)
    return dict(peak=round(float(m.H.max())), peak_at=[round(float(xs[i])), round(float(xs[j]))],
                water_pct=round(float(wet[inA].mean() * 100)), snow_pct=round(float((m.mat[inA] == M['snow']).mean() * 100), 1),
                props=len(m.props), lights=len(m.lights))

# ---------------------------------------------------------------- output
def export(m, out):
    hq = np.clip(np.round(m.H / HSCALE), -32768, 32767).astype('<i2')
    mq = m.mat.astype(np.uint8)
    with open(os.path.join(out, m.id + '.h16'), 'wb') as f: f.write(hq.tobytes())
    with open(os.path.join(out, m.id + '.mat'), 'wb') as f: f.write(mq.tobytes())
    tod = {}
    for k, v in TOD.items():
        v = dict(v)
        if k == 'day': v.update(m.tint)
        tod[k] = v
    doc = dict(
        format='tailonlock-map', version=1, id=m.id, name=m.name, request=m.request, blurb=m.blurb,
        axes='metres; +X right, +Y up, +Z from the blue base toward the red base (same as the browser game)',
        arena=ARENA, water_level=m.water_level, water_sky_mix=0.25, sky_far_ground=m.sky_far,
        heightmap=dict(samples=N, spacing=SP, origin=[X0, X0], encoding='int16le', scale=HSCALE,
                       order='row-major, row j = z, column i = x', file=m.id + '.h16',
                       data=base64.b64encode(hq.tobytes()).decode()),
        surface=dict(cells=N - 1, encoding='uint8', order='row-major, row j = z, column i = x', file=m.id + '.mat',
                     data=base64.b64encode(mq.tobytes()).decode()),
        palette=[dict(id=k, name=n, color=c, kind=kd, jitter=jt) for k, (n, c, kd, jt) in enumerate(MATS)],
        time_of_day=tod, light_colors=LIGHT_COLORS,
        bases=m.bases, carriers=m.carriers, targets=m.targets, spawns=m.spawns, tanker=m.tanker,
        props=m.props, lights=m.lights, weather=m.weather, stats=stats(m))
    with open(os.path.join(out, m.id + '.json'), 'w') as f: json.dump(doc, f, separators=(',', ':'))
    return doc

def main(out):
    os.makedirs(out, exist_ok=True)
    docs, bad = [], False
    for build in (twin_isles, dune_sea, iron_coast, high_valley):
        m = build(); materials(m); spawns(m); props(m); lights(m)
        ridge = lambda x, z, m=m: min(1.0, max(0.05, (sample(m.H, x, z) - 600) / 1400)) if abs(x) < 12800 and abs(z) < 12800 else 0.05
        m.weather = weather.build(m.id, m.seed, lambda x, z, m=m: sample(m.H, x, z), ridge)
        errs = check(m)
        for k, w in m.weather['presets'].items(): print(f'   {k:9s} {len(w["clouds"]):3d} clouds {w["puff_count"]:5d} puffs  {w["summary"]}')
        print(f'{m.id:12s} peak {m.H.max():6.0f} m  targets {len(m.targets)}  props {len(m.props)}  lights {len(m.lights)}  ' +
              ('OK' if not errs else 'PROBLEMS: ' + '; '.join(errs)))
        bad |= bool(errs)
        docs.append(export(m, out))
    index = [dict(id=d['id'], name=d['name'], request=d['request'], blurb=d['blurb'], file=d['id'] + '.json',
                  carriers=len(d['carriers']), stats=d['stats']) for d in docs]
    with open(os.path.join(out, 'maps_index.json'), 'w') as f: json.dump(index, f, indent=1)
    with open(os.path.join(out, 'maps_game.js'), 'w') as f:
        f.write('// Tailon Lock maps, generated by src/build_maps.py. window.ACE_MAPS[id] = map document (see FORMAT.md).\n')
        f.write('window.ACE_MAPS = ' + json.dumps({d['id']: d for d in docs}, separators=(',', ':')) + ';\n')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'out'))
