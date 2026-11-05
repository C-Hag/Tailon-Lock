import json, sys
from carrier import build, SPEC
from geom import write_glb
parts, mats, extras = build()
n = sum(p['mesh'].count() for p in parts)
size = write_glb('out/cvn90.glb', 'cvn90', parts, mats, extras)
print('cvn90', n, 'tris', size, 'bytes')
json.dump({'spec': SPEC, 'deck': extras}, open('out/carrier_data.json', 'w'), indent=1)
