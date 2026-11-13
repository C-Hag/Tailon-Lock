#!/usr/bin/env python3
"""Collect what the intro needs from the jets and maps work into one compact JSON (intro_assets.json).

usage: build_assets.py <tailonlock project dir> <out.json>
Takes the NF-24 mesh (+ a clustered far LOD), the Twin Isles terrain at 400 m, its dawn light and the cumulus puffs
of its 'scattered' weather. Same axes as the game: metres, +X right, +Y up, +Z toward the red base.
"""
import base64, json, struct, subprocess, sys, os

root, out = sys.argv[1], sys.argv[2]

# same far-LOD rule as jets/src/game_patch.py
def decimate(v, f, cell):
    """Vertex clustering: snap vertices to a grid, average each cell, drop collapsed and duplicate faces."""
    key = [tuple(int(c // cell) for c in p) for p in v]
    ids, acc = {}, []
    for p, k in zip(v, key):
        if k not in ids:
            ids[k] = len(acc); acc.append([0.0, 0.0, 0.0, 0])
        a = acc[ids[k]]; a[0] += p[0]; a[1] += p[1]; a[2] += p[2]; a[3] += 1
    nv = [[round(a[0] / a[3], 2), round(a[1] / a[3], 2), round(a[2] / a[3], 2)] for a in acc]
    seen, nf = set(), []
    for tri, col in f:
        t = [ids[key[i]] for i in tri]
        if len(set(t)) < 3:
            continue
        s = tuple(sorted(t))
        if s in seen:
            continue
        seen.add(s); nf.append([t, col])
    return nv, nf


def compact(v, f, rad):
    return {'v': [[round(c, 2) for c in p] for p in v], 'f': f, 'rad': round(rad, 1)}


jets = json.loads(subprocess.check_output(['node', '-e',
    'process.stdout.write(JSON.stringify(require(process.argv[1])))', os.path.join(root, 'jets', 'jets_game.js')]))
nf = next(j for j in jets if j['id'] == 'nf24')
m = nf['mesh']
lv, lf = decimate(m['v'], m['f'], 1.1)
nf24 = {'id': 'nf24', 'des': nf['designation'], 'name': nf['name'], 'm': compact(m['v'], m['f'], m['rad']),
        'lod': compact(lv, lf, m['rad']), 'minY': round(min(p[1] for p in m['v']), 2),
        'tail': round(-min(p[2] for p in m['v']), 1)}

car = json.loads(subprocess.check_output(['node', '-e',
    'process.stdout.write(JSON.stringify(require(process.argv[1])))', os.path.join(root, 'jets', 'carrier_game.js')]))
cvn90 = {'id': 'cvn90', 'des': car['spec']['designation'], 'name': car['spec']['name'],
         'm': compact(car['mesh']['v'], car['mesh']['f'], car['mesh']['rad']),
         'cats': [{'name': c['name'], 'start': c['start'], 'end': c['end']} for c in car['deck']['catapults']]}

mp = json.load(open(os.path.join(root, 'maps', 'twin-isles.json')))
h = mp['heightmap']; n = h['samples']
H = struct.unpack('<%dh' % (n * n), base64.b64decode(h['data']))
mat = base64.b64decode(mp['surface']['data']); mc = mp['surface']['cells']
S = 2  # every 2nd sample: 65 x 65 at 400 m
hs = [[round(H[j * n + i] * h['scale']) for i in range(0, n, S)] for j in range(0, n, S)]
ms = [[mat[min(j, mc - 1) * mc + min(i, mc - 1)] for i in range(0, mc, S)] for j in range(0, mc, S)]
terrain = {'n': len(hs), 'spacing': h['spacing'] * S, 'origin': h['origin'],
           'h': [v for row in hs for v in row], 'mat': [v for row in ms for v in row],
           'palette': [{'color': p['color'], 'water': p['kind'] == 'water', 'jitter': p['jitter']} for p in mp['palette']]}

w = mp['weather']['presets']['scattered']
clouds = []
for c in w['clouds']:
    puffs = [p[:4] + ([p[5]] if len(p) > 5 else []) for p in c['puffs'] if p[4] <= 1]
    clouds.append({'x': c['x'], 'z': c['z'], 'base': c['base'], 'top': c['top'], 'puffs': puffs})

assets = {'source': {'jet': 'jets/jets_game.js nf24', 'carrier': 'jets/carrier_game.js', 'map': 'maps/twin-isles.json', 'tod': 'dawn', 'weather': 'scattered'},
          'nf24': nf24, 'cvn90': cvn90, 'terrain': terrain, 'tod': mp['time_of_day']['dawn'],
          'water_sky_mix': mp['water_sky_mix'], 'far_ground': mp['sky_far_ground'],
          'carriers': mp['carriers'][:1], 'ships': [t for t in mp['targets'] if t['type'] == 'ship' and t['team'] == 0],
          'clouds': clouds}
s = json.dumps(assets, separators=(',', ':'))
open(out, 'w').write(s)
print(f"nf24 {len(m['f'])} faces, LOD {len(lf)}; clouds {len(clouds)} / {sum(len(c['puffs']) for c in clouds)} puffs; {len(s)//1024} KB", file=sys.stderr)
