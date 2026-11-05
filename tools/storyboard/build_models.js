#!/usr/bin/env node
// build_models.js - collects the intro's models for the storyboard editor (tools/storyboard/index.html) into one
// models.json: the same meshes the intro draws, taken from the game's own sources.
//   nf24, cvn90, terrain, dawn light   assets/intro/intro_assets.json
//   ks55, ja46, lf27, sy26             the JETS table in web/index.html (ks55 in the bandit's red tint, as the intro)
//   dr7                                the game's jetMesh() at 0.85 scale, as the intro's MODELS.dr7
//   frigate                            the intro's own low-poly escort (assets/intro/src/intro.js)
//   missile                            a small body, fins and nozzle (the intro draws a streak; this is for placing)
//   crew, operator, supervisor, ...    assets/figure/figure.js figures, the room from Figure.room()
// Usage: node tools/storyboard/build_models.js [out.json]   (default tools/storyboard/models.json)
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.resolve(__dirname, '../..');
const out = process.argv[2] || path.join(__dirname, 'models.json');
const A = JSON.parse(fs.readFileSync(path.join(ROOT, 'assets/intro/intro_assets.json'), 'utf8'));
const S = JSON.parse(fs.readFileSync(path.join(ROOT, 'assets/intro/intro_script.json'), 'utf8'));
const html = fs.readFileSync(path.join(ROOT, 'web/index.html'), 'utf8');
const Figure = require(path.join(ROOT, 'assets/figure/figure.js'));

const hex = c => typeof c === 'string' ? c : '#' + c.map(v => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('');
const hex2rgb = h => { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
const r2 = v => Math.round(v * 1000) / 1000;
// a model: v [[x, y, z]] (x right, y up, z forward, metres), f [[[i, j, k, ...], '#rrggbb']], rad
// (vertices rounded to a millimetre and shared where they meet, which keeps the people small)
function model(label, m, extra = {}) {
  const V = [], at = new Map(), idx = m.v.map(p => {
    const q = p.map(r2), k = q.join(',');
    if (!at.has(k)) { at.set(k, V.length); V.push(q); }
    return at.get(k);
  });
  return Object.assign({ label, v: V, f: m.f.map(([i, c]) => [i.map(j => idx[j]), hex(c)]), rad: r2(m.rad) }, extra);
}

// the game's mesh helpers (web/index.html), the same arithmetic
function box(V, F, x, y, z, sx, sy, sz, col, top) {
  const b = V.length;
  for (const [dx, dy, dz] of [[-1, 0, -1], [1, 0, -1], [1, 0, 1], [-1, 0, 1], [-1, 1, -1], [1, 1, -1], [1, 1, 1], [-1, 1, 1]]) V.push([x + dx * sx, y + dy * sy, z + dz * sz]);
  F.push([[b + 4, b + 5, b + 6, b + 7], top || col], [[b, b + 1, b + 5, b + 4], col], [[b + 1, b + 2, b + 6, b + 5], col], [[b + 2, b + 3, b + 7, b + 6], col], [[b + 3, b, b + 4, b + 7], col]);
}
function jetMesh(body, acc) {
  const V = [[0, 0, 7], [0, 1, 0], [-0.9, 0, 0], [0.9, 0, 0], [0, -0.8, 0], [0, 0.7, -6], [-0.7, 0, -6], [0.7, 0, -6], [0, -0.5, -6],
    [-0.9, 0, 2], [-6, 0, -4], [-0.9, 0, -4.5], [0.9, 0, 2], [6, 0, -4], [0.9, 0, -4.5],
    [0, 0.7, -2.5], [0, 3.4, -6.4], [0, 0.7, -6],
    [-0.7, 0.1, -4.6], [-2.8, 0.1, -6.6], [-0.7, 0.1, -6.4], [0.7, 0.1, -4.6], [2.8, 0.1, -6.6], [0.7, 0.1, -6.4]];
  const glass = '#1d2a44';
  return { v: V, f: [[[0, 1, 3], glass], [[0, 3, 4], body], [[0, 4, 2], body], [[0, 2, 1], glass],
    [[1, 5, 7, 3], body], [[3, 7, 8, 4], body], [[4, 8, 6, 2], body], [[2, 6, 5, 1], body], [[5, 6, 8, 7], '#2a2a2a'],
    [[9, 10, 11], acc], [[12, 14, 13], acc], [[15, 16, 17], acc], [[18, 19, 20], body], [[21, 23, 22], body]], rad: 8 };
}
// the game's team tint (web/index.html JT): paint blended toward the team colour, glass and nozzles kept
const TEAM = [{ body: '#8fb3ff' }, { body: '#ff8a6a' }];
const tint = (h, team) => {
  const c = hex2rgb(h), t = hex2rgb(TEAM[team].body), L = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
  if (L < 45) return h;
  const k = L / 150;
  return hex(c.map((v, i) => v * 0.4 + t[i] * k * 0.6));
};

const M = {};
M.nf24 = model('NF-24 Cormorant (Viper)', A.nf24.m, { kind: 'jet', tail: A.nf24.tail });
M.cvn90 = model('CVN-90 Stormhaven', A.cvn90.m, { kind: 'ship' });

const jets = JSON.parse(html.match(/^const JETS = (\[.*\]);$/m)[1]);
for (const j of jets) {
  const team = j.id === 'ks55' ? 1 : 0;
  M[j.id] = model(`${j.des} ${j.name}`, { v: j.m.v, f: j.m.f.map(([i, c]) => [i, tint(c, team)]), rad: j.m.rad }, { kind: 'jet', tail: j.tail });
}
{
  const d = jetMesh('#7b818a', '#4b5058'), k = 0.85;
  M.dr7 = model('DR-7 Vesper drone', { v: d.v.map(v => v.map(x => x * k)), f: d.f, rad: d.rad * k }, { kind: 'jet', tail: 5.2 });
}
{
  const V = [], F = [];
  const hull = '#69707a', deckc = '#59606a', sup = '#7c838c';
  V.push([-7, 0, -62], [7, 0, -62], [8, 0, 30], [0, 0, 66], [-8, 0, 30], [-7, 7, -62], [7, 7, -62], [8.5, 8, 30], [0, 10, 66], [-8.5, 8, 30]);
  F.push([[5, 6, 7, 8, 9], deckc], [[0, 1, 6, 5], hull], [[1, 2, 7, 6], hull], [[2, 3, 8, 7], hull], [[3, 4, 9, 8], hull], [[4, 0, 5, 9], hull]);
  box(V, F, 0, 7, -8, 5, 9, 16, sup, '#8a9199'); box(V, F, 0, 16, -2, 3, 6, 5, sup, '#8a9199'); box(V, F, 0, 22, -2, 0.5, 14, 0.5, '#444'); box(V, F, 0, 7, -40, 4, 4, 6, sup, '#8a9199');
  M.frigate = model('Frigate escort', { v: V, f: F, rad: 70 }, { kind: 'ship' });
}
{
  // a missile, 3.6 m: an octagonal body, a nose cone, four fins and a glowing nozzle
  const V = [], F = [], n = 8, R = 0.09, body = '#e8e8ec';
  for (let i = 0; i < n; i++) { const a = i / n * Math.PI * 2; V.push([Math.cos(a) * R, Math.sin(a) * R, -1.8], [Math.cos(a) * R, Math.sin(a) * R, 1.4]); }
  V.push([0, 0, 1.8]);
  for (let i = 0; i < n; i++) { const j = (i + 1) % n; F.push([[2 * i, 2 * i + 1, 2 * j + 1, 2 * j], body], [[2 * i + 1, 2 * n, 2 * j + 1], '#c8c8cc']); }
  F.push([Array.from({ length: n }, (_, i) => 2 * (n - 1 - i)), '#ffb050']);
  for (const [x, y] of [[1, 0], [0, 1], [-1, 0], [0, -1]]) { const b = V.length; V.push([x * R, y * R, -1.7], [x * 0.32, y * 0.32, -1.75], [x * R, y * R, -1.2]); F.push([[b, b + 1, b + 2], '#9a9aa0']); }
  M.missile = model('Missile', { v: V, f: F, rad: 2 }, { kind: 'missile' });
}

// people: the figure engine's triangles, one colour each from its role. Figure x is the figure's left; the scene's
// x is right, so x is mirrored (and the winding with it).
function figureModel(label, fig, extra) {
  const b = Figure.build(fig), V = [], F = [];
  for (const t of b.tris) {
    const col = t.col || Figure.colour(fig, t.role) || [128, 128, 128];
    const i = V.length;
    for (const p of t.v) V.push([-p[0], p[1], p[2]]);
    F.push([[i, i + 2, i + 1], hex(col)]);
  }
  return model(label, { v: V, f: F, rad: fig.height || 1.8 }, Object.assign({ kind: 'crew' }, extra));
}
// deck crew: coveralls with the jersey colour (the script's `jersey`) on these roles
const JERSEY = ['shirt', 'sleeve', 'collar', 'pocket', 'tee'];
const deck = figureModel('Deck crew', { height: 1.78, kit: { uniform: 'coveralls' }, pose: Figure.POSES.stand }, { jersey: [] });
{
  // mark the faces whose colour is the jersey's: the coverall top
  const tops = new Set(JERSEY.map(r => hex(Figure.colour({ kit: { uniform: 'coveralls' } }, r))));
  deck.jersey = deck.f.map((f, i) => tops.has(f[1]) ? i : -1).filter(i => i >= 0);
}
M.crew = deck;
// the control room's three, in their own kit and pose (seated at the console, or standing)
for (const [id, fig] of Object.entries(Figure.CREW)) {
  const H = fig.height || 1.75, pose = typeof fig.pose === 'function' ? fig.pose(H) : fig.pose || Figure.POSES.stand;
  M['cic_' + id] = figureModel('Control room: ' + id, Object.assign({}, fig, { height: H, pose }), { room: true });
}

{
  const R = Figure.room(), V = [], F = [];
  for (const t of R.tris) {
    const i = V.length;
    for (const p of t.v) V.push([-p[0], p[1], p[2]]);
    F.push([[i, i + 2, i + 1], hex(t.col)]);
  }
  M.room = model('Control room (CIC)', { v: V, f: F, rad: 8 }, { kind: 'room', screens: R.screens.map(s => ({ o: [-s.o[0], s.o[1], s.o[2]], ex: [-s.ex[0], s.ex[1], s.ex[2]], ey: [-s.ey[0], s.ey[1], s.ey[2]] })) });
}

const env = { tod: A.tod, terrain: A.terrain, clouds: (A.clouds || []).concat(S.env.extra_clouds || []), clear_clouds: S.env.clear_clouds || [], sun: S.env.sun || A.tod.sun, cats: A.cvn90.cats };
fs.writeFileSync(out, JSON.stringify({ format: 'tailonlock-storyboard-models', version: 1, models: M, env }));
const n = Object.values(M).reduce((s, m) => s + m.f.length, 0);
console.log(`${out}: ${Object.keys(M).length} models, ${n} faces, ${(fs.statSync(out).size / 1e6).toFixed(2)} MB`);
