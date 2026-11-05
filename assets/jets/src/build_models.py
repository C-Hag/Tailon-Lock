import sys, json, os
from jets import BUILDERS
from geom import write_glb
out = sys.argv[1] if len(sys.argv) > 1 else 'out'
os.makedirs(out, exist_ok=True)
for k, fn in BUILDERS.items():
    parts, mats = fn()
    n = sum(p['mesh'].count() for p in parts)
    allp = [v for p in parts for ts in p['mesh'].tris.values() for t in ts for v in t]
    xs=[p[0] for p in allp]; ys=[p[1] for p in allp]; zs=[p[2] for p in allp]
    size = write_glb(f'{out}/{k}.glb', k, parts, mats, {'id': k})
    print(k, n, 'tris', size, 'bytes', 'span %.2f len %.2f h %.2f' % (max(xs)-min(xs), max(zs)-min(zs), max(ys)-min(ys)))
