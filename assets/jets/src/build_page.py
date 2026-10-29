import json, base64, sys, os
import build_data  # rebuilds out/jets_data.json
from jets import BUILDERS
from geom import mirx
jets = json.load(open('out/jets_data.json'))
ids = [j['id'] for j in jets]
b64 = {k: base64.b64encode(open(f'out/{k}.glb','rb').read()).decode() for k in ids}
keep = [{k: v for k, v in j.items()} for j in jets]
js = lambda n: open('web/' + n).read()
import math
from carrier import build as cv_build, SPEC as CV_SPEC
import subprocess; subprocess.run(['python3', 'build_carrier.py'], check=True)
cv_parts, cv_mats, cv_deck = cv_build()
compat = []
for j in jets:
    m_to = j['m_empty'] + j['fuel'] + j['m_stores'] + 100 * j['crew']
    need = j['perf']['vlof_kmh'] / 3.6 - 15.4
    have = min(math.sqrt(2 * CV_SPEC['cat_energy_mj'] * 1e6 / m_to), CV_SPEC['cat_max_end_kt'] / 1.944)
    m_l = j['m_empty'] + 0.25 * j['fuel'] + 100 * j['crew'] + 0.5 * j['m_stores']
    vapp = 1.15 * math.sqrt(2 * m_l * 9.80665 / (1.225 * j['S'] * j['cl_to']))
    E = 0.5 * m_l * (vapp - 15.4) ** 2 / 1e6
    ok = need <= have and vapp * 1.944 <= 145 and E <= CV_SPEC['arrest_energy_mj']
    verdict = 'Carrier jet: hook, catapult tow bar, strengthened gear' if j.get('landing') == 'hook' else ('Possible as a naval version (needs a hook and stronger gear)' if ok else 'No: approach too fast for the wires')
    compat.append({'id': j['id'], 'name': f"{j['designation']} {j['name']}", 'need_kt': round(need * 1.944), 'cat_kt': round(have * 1.944), 'vapp_kt': round(vapp * 1.944), 'arrest_mj': round(E, 1), 'ok': ok, 'verdict': verdict})
CARRIER = {'spec': CV_SPEC, 'deck': cv_deck, 'compat': compat, 'tris': sum(p['mesh'].count() for p in cv_parts)}
json.dump(CARRIER, open('out/carrier_data.json', 'w'), indent=1)
html = open('web/page.html').read() + "\n<script>\nconst JETS = " + json.dumps(keep, separators=(',', ':')) + ";\nconst GLB_B64 = " + json.dumps(b64) + ";\nconst CARRIER = " + json.dumps(CARRIER, separators=(',', ':')) + ";\nconst CARRIER_B64 = " + json.dumps(base64.b64encode(open('out/cvn90.glb', 'rb').read()).decode()) + ";\n" + \
    "\n".join(js(n) for n in ['render.js', 'sim.js', 'instruments.js', 'cockpits.js', 'app.js']) + "\n</script>\n"
open('out/tailonlock-hangar.html', 'w').write(html)
print('page bytes', len(html))
# game-format export: indexed vertices, X mirrored (game uses +X = right wing), faces [[i,j,k], '#rrggbb']
game = []
for j in jets:
    parts, mats = BUILDERS[j['id']]()
    V, idx, F = [], {}, []
    def vid(p):
        q = (round(-p[0], 3), round(p[1], 3), round(p[2], 3))
        if q not in idx: idx[q] = len(V); V.append(list(q))
        return idx[q]
    for pr in parts:
        for m, ts in pr['mesh'].tris.items():
            col = mats[m]['color']
            for (a, b, c) in ts:
                F.append([[vid(a), vid(c), vid(b)], col])  # mirrored, so reverse winding
    game.append({'id': j['id'], 'designation': j['designation'], 'name': j['name'], 'game': j['game'],
                 'flight': {k: j[k] for k in ['m_empty', 'fuel', 'm_stores', 'm_combat', 't_ab', 't_dry', 'S', 'k', 'cd0', 'wave', 'clmax', 'g_max', 'g_min', 'm_lim', 'q_lim', 'roll', 'alpha_max', 'tsfc'] + (['k_swept', 'clmax_swept'] if 'k_swept' in j else [])},
                 'perf': j['perf'], 'weapons': j['weapons'], 'gun': j['gun'], 'mesh': {'v': V, 'f': F, 'rad': round(max(max(abs(x) for x in v) for v in V), 2)}})
open('out/jets_game.js', 'w').write("// Tailon Lock jets: game-format meshes (+X right wing, +Y up, +Z nose, metres) and flight data.\n// Generated from the same source as the .glb files. Usage: mesh(J.mesh.v, J.mesh.f, J.mesh.rad) style.\nconst JET_TYPES = " + json.dumps(game, separators=(',', ':')) + ";\nif (typeof module !== 'undefined') module.exports = JET_TYPES;\n")
print('game js bytes', os.path.getsize('out/jets_game.js'))

# carrier in the game's format: X mirrored (game +X = starboard/right), deck points mirrored too
V, idx, F = [], {}, []
def vid(p):
    q = (round(-p[0], 2), round(p[1], 2), round(p[2], 2))
    if q not in idx: idx[q] = len(V); V.append(list(q))
    return idx[q]
for pr in cv_parts:
    t = pr.get('translation', (0, 0, 0))
    for m, ts in pr['mesh'].tris.items():
        for (a, b2, c) in ts: F.append([[vid(a), vid(c), vid(b2)], cv_mats[m]['color']])
def flip(o):
    if isinstance(o, dict): return {k: (-v if k == 'heading_offset_deg' else flip(v)) for k, v in o.items()}
    if isinstance(o, list) and len(o) == 3 and all(isinstance(x, (int, float)) for x in o): return [-o[0], o[1], o[2]]
    if isinstance(o, list): return [flip(x) for x in o]
    return o
deck_g = flip(cv_deck); deck_g['angled_deck']['note'] = 'negative = toward port (game +X is starboard); same sign rule as catapults'
open('out/carrier_game.js', 'w').write("// CVN-90 Stormhaven in the game's mesh format (+X = starboard, +Y up, +Z bow, metres, waterline y=0) plus deck data.\nconst CARRIER_TYPE = " + json.dumps({'id': 'cvn90', 'spec': CV_SPEC, 'deck': deck_g, 'compat': compat, 'mesh': {'v': V, 'f': F, 'rad': 165}}, separators=(',', ':')) + ";\nif (typeof module !== 'undefined') module.exports = CARRIER_TYPE;\n")
print('carrier game js', os.path.getsize('out/carrier_game.js'))
