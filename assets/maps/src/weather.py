"""Weather presets and cloud fields for the Tailon Lock maps.

Each map gets four presets (clear, scattered, overcast, storm) tuned to its climate. A preset is
atmosphere settings plus cloud layers, using real cloud genera with plausible bases and tops:
  Cu  cumulus (fair-weather to congestus)      puff clouds
  Cb  cumulonimbus (tower + anvil)             puff clouds
  Fs  stratus fractus (ragged scud under rain) puff clouds
  Sc  stratocumulus, St stratus, As altostratus, Ns nimbostratus, Ci cirrus, Cs cirrostratus
                                              sheet layers with a tileable coverage mask
Puff clouds are lists of spheres that engines draw as camera-facing billboards; every puff has a
quality tier (0 = always, 1 = medium and up, 2 = high only). See FORMAT.md for the shading rule.
"""
import base64, math
import numpy as np

MASK = 64              # sheet mask resolution (tileable)
MASK_SPAN = 32000.0    # metres covered by one mask tile, centred on the origin, repeats beyond

def pnoise(seed, cells, n=MASK):
    """Tileable value noise, n x n, 0..1."""
    rng = np.random.default_rng(seed)
    L = rng.random((cells, cells))
    u = np.arange(n) / n * cells
    U, V = np.meshgrid(u, u)
    i0 = np.floor(U).astype(int); j0 = np.floor(V).astype(int)
    fu = U - i0; fv = V - j0; fu = fu * fu * (3 - 2 * fu); fv = fv * fv * (3 - 2 * fv)
    i1 = (i0 + 1) % cells; j1 = (j0 + 1) % cells
    a, b, c, d = L[j0, i0], L[j0, i1], L[j1, i0], L[j1, i1]
    return (a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv

def sheet_mask(seed, coverage, kind):
    if kind == 'streak':       # cirrus: noise smeared along x into long streaks
        n = sum(pnoise(seed + o, c) * a for o, (c, a) in enumerate([(8, 1), (16, .6), (32, .4)]))
        n = sum(np.roll(n, k, axis=1) for k in range(-8, 9)) / 17
        n = n * 0.6 + 0.4 * pnoise(seed + 9, 4)
    else:
        n = sum(pnoise(seed + o, c) * a for o, (c, a) in enumerate([(4, 1), (8, .5), (16, .3), (32, .15)])) / 1.95
        if kind == 'lumpy':   # stratocumulus cells
            n = n * 0.7 + 0.3 * pnoise(seed + 21, 32)
    if coverage >= 0.999:
        thr = n.min() - 0.01
    else:
        thr = np.quantile(n, 1 - coverage)
    soft = 0.06
    d = np.clip((n - thr) / soft, 0, 1)
    if coverage >= 0.999: d = np.clip(0.75 + 0.25 * (n - n.min()) / (n.max() - n.min() + 1e-9), 0, 1)
    return base64.b64encode(np.round(d * 255).astype(np.uint8).tobytes()).decode()

def cumulus(rng, x, z, base, depth, R, kind='Cu'):
    """One cumulus: flat base ring, rising domes, cauliflower top. Offsets relative to (x, base, z)."""
    P = []
    r0 = R * 0.42
    for k in range(7):                                     # flat base
        a = k / 7 * 2 * math.pi + rng.uniform(0, .5)
        rr = R * rng.uniform(0.35, 0.7)
        P.append([rr * math.cos(a), r0 * 0.55, rr * math.sin(a), r0 * rng.uniform(0.85, 1.1), 0 if k % 2 == 0 else 1])
    levels = max(1, int(depth / (r0 * 0.9)))
    for L in range(1, levels + 1):                         # rising domes, narrowing
        y = r0 * 0.55 + L * depth / (levels + 1)
        spread = R * 0.55 * (1 - 0.55 * L / levels)
        cnt = 4 if L < levels else 3
        for k in range(cnt):
            a = rng.uniform(0, 2 * math.pi); rr = spread * rng.uniform(0.2, 1)
            P.append([rr * math.cos(a), y, rr * math.sin(a), r0 * rng.uniform(0.7, 1.0) * (1 - 0.25 * L / levels), 0 if k == 0 else 1])
    for k in range(6):                                     # detail puffs round the top and sides
        a = rng.uniform(0, 2 * math.pi); y = rng.uniform(0.3, 1.0) * depth
        rr = R * 0.6 * (1 - 0.4 * y / depth)
        P.append([rr * math.cos(a), y, rr * math.sin(a), r0 * rng.uniform(0.35, 0.55), 2])
    return dict(genus=kind, x=round(x), z=round(z), base=round(base), top=round(base + depth + r0),
                puffs=[[round(p[0]), round(p[1]), round(p[2]), round(p[3]), p[4]] for p in P])

def cumulonimbus(rng, x, z, base, top, wind_from):
    """Tower from base to the tropopause, anvil spreading downwind, ragged base."""
    R = rng.uniform(2200, 3200)
    P = []
    depth = top - base
    lv = 14
    for L in range(lv):
        y = depth * L / lv * 0.82
        spread = R * (0.8 - 0.25 * L / lv)
        for k in range(5):
            a = rng.uniform(0, 2 * math.pi); rr = spread * rng.uniform(0.1, 0.9)
            P.append([rr * math.cos(a), y + 300, rr * math.sin(a), R * rng.uniform(0.45, 0.6), 0 if k < 2 else 1])
    # anvil: flat wide puffs at the top, pushed downwind
    wd = math.radians(wind_from + 180)
    ax, az = math.sin(wd), math.cos(wd)
    for k in range(26):
        t = rng.uniform(-0.3, 1.0); s = rng.uniform(-1, 1)
        dx = ax * t * R * 2.6 - az * s * R * 1.5; dz = az * t * R * 2.6 + ax * s * R * 1.5
        P.append([dx, depth * rng.uniform(0.86, 0.97), dz, R * rng.uniform(0.5, 0.75), 0 if k % 3 == 0 else 1, 0.35])
    # ragged base and rain shaft area
    for k in range(10):
        a = rng.uniform(0, 2 * math.pi); rr = R * rng.uniform(0.2, 0.9)
        P.append([rr * math.cos(a), rng.uniform(-150, 100), rr * math.sin(a), R * rng.uniform(0.25, 0.4), 2])
    out = []
    for p in P:
        q = [round(p[0]), round(p[1]), round(p[2]), round(p[3]), p[4]]
        if len(p) > 5: q.append(p[5])  # vertical squash for anvil puffs
        out.append(q)
    return dict(genus='Cb', x=round(x), z=round(z), base=round(base), top=round(top), puffs=out,
                rain=dict(radius=round(R * 0.8), rate='heavy'), lightning=True)

def place_clouds(rng, H_at, n, base, depth, R, pref=None, kind='Cu', spread=14000):
    """Scatter n cumulus over the area, keeping them apart; pref(x, z) -> 0..1 acceptance weight."""
    C, tries = [], 0
    while len(C) < n and tries < n * 200:
        tries += 1
        x, z = rng.uniform(-spread, spread), rng.uniform(-spread, spread)
        if pref and rng.random() > pref(x, z): continue
        r = R * rng.uniform(0.6, 1.3)
        if any(math.hypot(x - c['x'], z - c['z']) < (r + 600) for c in C): continue
        d = depth * rng.uniform(0.5, 1.15)
        b = base + rng.uniform(-80, 120)
        C.append(cumulus(rng, x, z, b, d, r, kind))
    return C

def count_for(coverage, R, spread=14000):
    area = (2 * spread) ** 2
    return int(coverage * area / (math.pi * R * R) * 0.9)

# Climates: (preset -> atmosphere + layers). Heights in metres above sea level.
CLIMATE = {
    'twin-isles': dict(label='Tropical maritime', presets={
        'clear':     dict(vis=60, wind=(70, 6), turb=0.05, cu=(0.06, 600, 500, 700)),
        'scattered': dict(vis=40, wind=(70, 8), turb=0.15, cu=(0.30, 650, 1300, 1000), sheets=[('Ci', 9500, 9900, 0.25)]),
        'overcast':  dict(vis=18, wind=(90, 12), turb=0.25, sheets=[('Sc', 900, 1500, 0.85), ('As', 4200, 5000, 0.6)], sun=0.35),
        'storm':     dict(vis=6, wind=(110, 18), turb=0.7, cb=(3, 500, 12500), fs=(0.25, 320), sheets=[('Ns', 700, 3200, 0.55)],
                          precip='showers', lightning=6, sun=0.18)}),
    'dune-sea': dict(label='Hot desert', presets={
        'clear':     dict(vis=40, wind=(20, 5), turb=0.2, haze=0.5),
        'scattered': dict(vis=45, wind=(20, 7), turb=0.35, cu=(0.12, 3200, 900, 1100), sheets=[('Ci', 10500, 10900, 0.35)]),
        'overcast':  dict(vis=30, wind=(200, 9), turb=0.3, sheets=[('As', 4800, 5600, 0.8), ('Cs', 9800, 10200, 0.6)], sun=0.45),
        'storm':     dict(vis=2.5, wind=(200, 20), turb=0.75, cb=(2, 2800, 13000), sheets=[('As', 5200, 6000, 0.5)],
                          precip='virga', dust=True, lightning=3, sun=0.3)}),
    'iron-coast': dict(label='Temperate maritime', presets={
        'clear':     dict(vis=50, wind=(250, 6), turb=0.05, cu=(0.05, 900, 400, 800)),
        'scattered': dict(vis=35, wind=(250, 9), turb=0.2, cu=(0.28, 1000, 1200, 1000), sheets=[('Ci', 8800, 9200, 0.3)]),
        'overcast':  dict(vis=10, wind=(230, 11), turb=0.2, sheets=[('St', 320, 800, 1.0), ('As', 3500, 4300, 0.7)],
                          precip='drizzle', sun=0.25),
        'storm':     dict(vis=5, wind=(220, 22), turb=0.75, cb=(3, 700, 10500), fs=(0.3, 380), sheets=[('Ns', 600, 4800, 0.8)],
                          precip='rain', lightning=4, sun=0.15)}),
    'high-valley': dict(label='Alpine continental', presets={
        'clear':     dict(vis=80, wind=(300, 5), turb=0.1, valley_fog=True),
        'scattered': dict(vis=50, wind=(300, 9), turb=0.35, cu=(0.25, 2300, 1300, 1000), oro=True, sheets=[('Ci', 9200, 9600, 0.3)]),
        'overcast':  dict(vis=15, wind=(280, 10), turb=0.3, sheets=[('St', 1150, 1700, 1.0), ('As', 4500, 5200, 0.5)], sun=0.3),
        'storm':     dict(vis=6, wind=(260, 16), turb=0.8, cb=(3, 1900, 11500), oro=True, cu=(0.12, 2100, 2000, 1300),
                          precip='showers', lightning=8, sun=0.2)}),
}

GENUS = {'Cu': 'Cumulus', 'Cb': 'Cumulonimbus', 'Fs': 'Stratus fractus', 'Sc': 'Stratocumulus', 'St': 'Stratus',
         'As': 'Altostratus', 'Ns': 'Nimbostratus', 'Ci': 'Cirrus', 'Cs': 'Cirrostratus', 'Fg': 'Valley fog'}
SHEET_KIND = {'Sc': 'lumpy', 'St': 'flat', 'As': 'flat', 'Ns': 'flat', 'Ci': 'streak', 'Cs': 'flat', 'Fg': 'flat'}
SHEET_COLOR = {'Sc': '#e6e8ee', 'St': '#cfd3da', 'As': '#d6dbe4', 'Ns': '#8c929c', 'Ci': '#ffffff', 'Cs': '#f2f5fa', 'Fg': '#e4e8ee'}

def okta(c): return max(1, int(round(c * 8))) if c > 0 else 0

def build(map_id, seed, H_at, ridge_pref=None):
    cl = CLIMATE[map_id]
    out = {}
    for k, (name, p) in enumerate(cl['presets'].items()):
        rng = np.random.default_rng(seed * 10 + k)
        clouds, sheets, layers = [], [], []
        wind_from, wind_speed = p['wind']
        if 'cu' in p:
            cov, base, depth, R = p['cu']
            pref = ridge_pref if p.get('oro') else None
            n = count_for(cov, R)
            cs = place_clouds(rng, H_at, n, base, depth, R, pref)
            clouds += cs
            layers.append(dict(genus='Cu', base=base, top=base + depth, oktas=okta(cov)))
        if 'cb' in p:
            cnt, base, top = p['cb']
            for i in range(cnt):
                for _ in range(200):
                    x, z = rng.uniform(-9000, 9000), rng.uniform(-9000, 9000)
                    if all(math.hypot(x - c['x'], z - c['z']) > 7000 for c in clouds if c['genus'] == 'Cb'): break
                clouds.append(cumulonimbus(rng, x, z, base, top, wind_from))
            layers.append(dict(genus='Cb', base=base, top=top, oktas=2))
        if 'fs' in p:
            cov, base = p['fs']
            for c in place_clouds(rng, H_at, count_for(cov, 600), base, 120, 600, None, 'Fs'):
                for q in c['puffs']: q[1] = round(q[1] * 0.35)
                c['top'] = base + 200; clouds.append(c)
            layers.append(dict(genus='Fs', base=base, top=base + 200, oktas=okta(cov)))
        for genus, base, top, cov in p.get('sheets', []):
            sheets.append(dict(genus=genus, base=base, top=top, coverage=cov, color=SHEET_COLOR[genus],
                               mask=dict(size=MASK, span=MASK_SPAN, encoding='uint8', data=sheet_mask(seed * 37 + base, cov, SHEET_KIND[genus]))))
            layers.append(dict(genus=genus, base=base, top=top, oktas=okta(cov)))
        if p.get('valley_fog'):
            sheets.append(dict(genus='Fg', base=150, top=260, coverage=0.5, color=SHEET_COLOR['Fg'], tod_only=['dawn'],
                               mask=dict(size=MASK, span=MASK_SPAN, encoding='uint8', data=sheet_mask(seed * 41, 0.5, 'flat'))))
            layers.append(dict(genus='Fg', base=150, top=260, oktas=4, note='dawn only'))
        layers.sort(key=lambda l: l['base'])
        puffs = sum(len(c['puffs']) for c in clouds)
        out[name] = dict(
            label=name.capitalize(), visibility_km=p['vis'], wind=dict(from_deg=wind_from, speed_mps=wind_speed),
            turbulence=p['turb'], precipitation=p.get('precip', 'none'), lightning_per_min=p.get('lightning', 0),
            dust=bool(p.get('dust')), haze=p.get('haze', 0.2), sun_factor=p.get('sun', 1.0),
            ambient_factor=round(1 + (1 - p.get('sun', 1.0)) * 0.6, 2),
            summary=', '.join(f"{GENUS[l['genus']]} {l['oktas']}/8 at {l['base']:,} m" for l in layers) or 'No cloud',
            layers=layers, clouds=clouds, sheets=sheets, puff_count=puffs)
    return dict(climate=cl['label'], default='scattered', presets=out)
