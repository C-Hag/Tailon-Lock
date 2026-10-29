#!/usr/bin/env python3
"""Add selectable jets to the browser game (web/index.html). Re-appliable: run it on whatever the source file holds.

usage: game_patch.py <game.html> <jets_game.js> [out.html]
Edits are anchored on exact strings; the script stops if an anchor is missing or already patched.
"""
import json, sys

ORDER = ['ja46', 'lf27', 'ks55', 'sy26']
ROLE = {
    'ja46': 'Swedish-style canard fighter. The slowest of the four: a tough, old-school interceptor.',
    'lf27': 'Light single-engine fighter. Nimble, cheap and quick to climb.',
    'ks55': 'Big twin-engine Flanker-style jet. Best turner, long legs.',
    'sy26': 'Stealthy canard heavyweight. Fast, climbs well, longest range.',
}
MARK = '// ---------- selectable jets ----------'


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


def jets_js(src):
    s = open(src).read()
    types = {t['id']: t for t in json.loads(s[s.index('['):s.rindex(']') + 1])}
    out = []
    for jid in ORDER:
        t = types[jid]; m = t['mesh']
        lv, lf = decimate(m['v'], m['f'], 1.1)
        g = t['game']
        out.append({'id': jid, 'des': t['designation'], 'name': t['name'], 'role': ROLE[jid],
                    'g': {k: g[k] for k in ('speed', 'turn', 'climb', 'range')},
                    'tail': round(-min(p[2] for p in m['v']), 1),
                    'm': compact(m['v'], m['f'], m['rad']), 'lod': compact(lv, lf, m['rad'])})
        print(f"{jid}: {len(m['f'])} faces, far LOD {len(lf)} faces", file=sys.stderr)
    return json.dumps(out, separators=(',', ':'))


JS_CORE = MARK + r'''
// Original jets from the Tailon Lock hangar (jets/jets_game.js). The near mesh is the full model, the far one a
// clustered low-poly copy. Stats (3-10 hangar scores) nudge the flight model by at most 8% either way.
const JETS = __JETS__;
const JET_IDS = JETS.map(j => j.id), JT = {};
if (!Array.isArray(settings.jets) || settings.jets.length < 2) settings.jets = ['ja46', 'lf27'];
settings.jets = settings.jets.slice(0, 2).map((j, i) => JET_IDS.includes(j) ? j : JET_IDS[i]);
(() => {
  const fac = s => 0.92 + (s - 3) / 7 * 0.16;
  // blend the paint toward the team colour so blue and red stay readable; glass and nozzles keep their colour
  const tint = (hex, team) => {
    const c = hex2rgb(hex), t = hex2rgb(TEAM[team].body), L = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
    if (L < 45) return hex;
    const k = L / 150, o = c.map((v, i) => clamp(Math.round(v * 0.4 + t[i] * k * 0.6), 0, 255));
    return '#' + o.map(v => v.toString(16).padStart(2, '0')).join('');
  };
  const build = (m, team) => mesh(m.v, m.f.map(([i, c]) => [i, tint(c, team)]), m.rad);
  for (const j of JETS) {
    JT[j.id] = { turn: fac(j.g.turn), roll: fac((j.g.turn + j.g.speed) / 2), speed: fac(j.g.speed), climb: fac(j.g.climb), burn: 1 / fac(j.g.range), tail: j.tail,
      mesh: [0, 1].map(t => Object.assign(build(j.m, t), { lod: build(j.lod, t) })) };
  }
})();
function setJet(p, id) { p.jet = JT[id] ? id : JET_IDS[0]; p.jt = JT[p.jet]; }
const jetMeshFor = q => q.jt ? q.jt.mesh[q.team] : MESH.jet[q.team];

'''

JS_MENU = r'''// jet picker on the main menu (one row per local player)
function jetInfo(id) {
  const j = JETS.find(o => o.id === id), bar = v => '■'.repeat(Math.round(v / 2)) + '□'.repeat(5 - Math.round(v / 2));
  return `${j.role}<br><span class="jbars">SPEED ${bar(j.g.speed)} TURN ${bar(j.g.turn)} CLIMB ${bar(j.g.climb)} RANGE ${bar(j.g.range)}</span>`;
}
function syncJets() {
  const two = settings.mode === '2p' || (settings.mode === 'net' && settings.seats === 2);
  show('jp1', two);
  $('jl0').textContent = two ? 'Player 1 jet' : 'Your jet';
  for (let s = 0; s < 2; s++) {
    for (const b of document.querySelectorAll(`[data-jet][data-seat="${s}"]`)) b.setAttribute('aria-pressed', b.dataset.jet === settings.jets[s]);
    $('ji' + s).innerHTML = jetInfo(settings.jets[s]);
  }
}
(() => {
  for (let s = 0; s < 2; s++) $('jseg' + s).innerHTML = JETS.map(j => `<button type="button" data-jet="${j.id}" data-seat="${s}">${j.des} ${j.name}</button>`).join('');
  for (const b of document.querySelectorAll('[data-jet]')) b.addEventListener('click', () => { settings.jets[+b.dataset.seat] = b.dataset.jet; saveSettings(); syncJets(); });
  for (const b of document.querySelectorAll('[data-mode],[data-seats]')) b.addEventListener('click', syncJets);
  syncJets();
})();

'''

HTML_PICK = '''    <div class="jetpick">
      <div class="jp" id="jp0"><span class="lab" id="jl0">Your jet</span><span class="seg jseg" role="group" aria-label="Player 1 jet" id="jseg0"></span><p class="jinfo" id="ji0"></p></div>
      <div class="jp" id="jp1" hidden><span class="lab">Player 2 jet</span><span class="seg jseg" role="group" aria-label="Player 2 jet" id="jseg1"></span><p class="jinfo" id="ji1"></p></div>
    </div>
'''

CSS = '''.jetpick{display:grid;gap:12px;margin:0 0 18px}
.jp{display:grid;gap:6px;justify-items:start}
.jseg{flex-wrap:wrap}
.jinfo{font-size:13px;color:var(--ink-dim);margin:0;line-height:1.45;max-width:60ch}
.jbars{font-family:var(--font-display);font-size:11px;letter-spacing:.04em;color:var(--ink)}
'''

EDITS = [
    # (anchor, replacement) applied once each
    ('const TDEF = {', None),  # JS_CORE goes before this line
    ('    <button class="go" id="start" type="button">Start mission</button>', None),  # picker before Start
    ('.diffwrap{display:inline-flex;gap:10px;align-items:center}\n', None),  # CSS after this rule
    ('const coarse = matchMedia(', None),  # menu JS before this line
    ('p.fuel = Math.max(0, p.fuel - (0.18 + 0.55 * p.throttle) * dt);',
     'p.fuel = Math.max(0, p.fuel - (0.18 + 0.55 * p.throttle) * p.jt.burn * dt);'),
    ('rotPair(p.f, p.u, c.pitch * 1.3 * auth * dt);', 'rotPair(p.f, p.u, c.pitch * 1.3 * p.jt.turn * auth * dt);'),
    ('rotPair(p.u, p.r, c.roll * 2.5 * dt);', 'rotPair(p.u, p.r, c.roll * 2.5 * p.jt.roll * dt);'),
    ('const target = 120 + 270 * thrEff;', 'const target = 120 + 270 * thrEff * p.jt.speed;'),
    ('- p.f[1] * 70 * dt -', '- p.f[1] * 70 / p.jt.climb * dt -'),
    ("Object.assign(p, { name: r.name, hidx: r.hidx, remote: !!r.remote, idx: i });",
     "Object.assign(p, { name: r.name, hidx: r.hidx, remote: !!r.remote, idx: i });\n"
     "    setJet(p, r.hidx >= 0 && !r.remote ? settings.jets[r.hidx] : JET_IDS[Math.random() * JET_IDS.length | 0]);"),
    ('addMesh(V, MESH.jet[q.team], q.pos, q.r, q.u, q.f, 1, TEAM[q.team].ui);',
     'addMesh(V, jetMeshFor(q), q.pos, q.r, q.u, q.f, 1, TEAM[q.team].ui);'),
    ("addDot(V, sub(q.pos, mul(q.f, 6.5)), 1.4, '#ffb347', 1, 1);",
     "addDot(V, sub(q.pos, mul(q.f, q.jt ? q.jt.tail : 6.5)), 1.4, '#ffb347', 1, 1);"),
    ('  const wv = M.v.map(v =>',
     '  if (M.lod && rad / Math.max(cc[2], 1) * V.F < 45) M = M.lod;\n  const wv = M.v.map(v =>'),
    ("ms: p.missiles, fl: p.flares,", "ms: p.missiles, fl: p.flares, jt: p.jet,"),
    ("rp.missiles = +ps.ms || 0; rp.flares = +ps.fl || 0;",
     "rp.missiles = +ps.ms || 0; rp.flares = +ps.fl || 0; if (JT[ps.jt] && rp.jet !== ps.jt) setJet(rp, ps.jt);"),
]


def main():
    src, jets, out = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else sys.argv[1]
    h = open(src).read()
    if MARK in h:
        sys.exit('already patched')
    core = JS_CORE.replace('__JETS__', jets_js(jets))
    for anchor, rep in EDITS:
        n = h.count(anchor)
        if n != 1:
            sys.exit(f'anchor found {n} times: {anchor[:70]}')
        if rep is None:
            rep = {'const TDEF = {': core + anchor,
                   'const coarse = matchMedia(': JS_MENU + anchor}.get(anchor)
            if rep is None:
                rep = HTML_PICK + anchor if anchor.lstrip().startswith('<button') else anchor + CSS
        h = h.replace(anchor, rep)
    open(out, 'w').write(h)
    print('patched', out, len(h), 'bytes', file=sys.stderr)


main()
