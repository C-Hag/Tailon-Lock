// frame_check.js - is every jet of the film wholly in the picture? For every shot of intro_script.json it follows the
// camera keys every 0.05 s and projects the 8 corners of each jet's bounding box into the letterboxed 2.35:1 frame of a
// 16:9 screen. Every jet of the film, parked ones too, counts in every shot for the whole shot, whether or not its path
// runs there. The only ways out are written in the script, per shot, and listed in the output:
//   `absent`  [{id, reason, from?, to?}]: the jet is not in this shot, and why. It must then not show in the picture.
//   `closeup` a jet that may run past the frame edge, its centre staying in the frame.
//   `partial` jets seen through a long lens in the background: cut by the frame edge, or partly in view while exempt.
// With --strict any corner outside at any sample, a jet with no path and no entry, or an absent jet that shows, fails.
// A subject may also be one of the deck crew (head and feet).
const C = require('../src/intro_core.js');
const path = require('path');
const args = process.argv.slice(2);
const strict = args.includes('--strict');
const S = require(path.resolve(args.find(a => !a.startsWith('--')) || path.join(__dirname, '..', 'intro_script.json')));
const D2R = Math.PI / 180, ASPECT = 16 / 9, BOXR = 2.35, MARGIN = 0.92;
const carrier = S.actors.find(a => a.id === 'carrier');
const tracks = {};
for (const a of S.actors) if (a.keys && a.keys.length) tracks[a.id] = C.track(a.keys);
// the bounding box of the model
const BOX = { nf24: [-9.2, 9.8, 9.6, -1.6, 3.2], ks55: [-9.2, 9.8, 9.6, -1.6, 3.2], dr7: [-6.2, 6.6, 6.4, -1.1, 2.1], ja46: [-7.8, 8.6, 5.3, -0.95, 3.5] };   // from the meshes: tail, nose, half span, under, top
const box = (c, fr, m) => {
  const out = [], [f0, f1, sp, u0, u1] = BOX[m] || BOX.nf24;
  for (const f of [f0, f1]) for (const r of [-sp, sp]) for (const u of [u0, u1])
    out.push([0, 1, 2].map(i => c[i] + fr.f[i] * f + fr.r[i] * r + fr.u[i] * u));
  return out;
};
const points = (a, t) => {
  if (a.kind === 'static') {
    const h = a.heading * D2R, c = C.shipLocal(carrier.ship, t, a.pos);
    return box(c, C.frame((carrier.ship.heading + a.heading), 0, 0), a.model);
  }
  const k = tracks[a.id] && C.sample(tracks[a.id], t);
  if (!k) return null;
  return box([k[1], k[2], k[3]], C.frame(k[4], k[5], k[6]), a.model);
};
// a crew member by head and feet, on the moving ship
const crewPoints = (id, t) => {
  const a = S.actors.find(x => x.id === id), k = C.sample(tracks[id], t) || a.keys[a.keys.length - 1];   // he stays where the last key puts him
  return [C.shipLocal(carrier.ship, t, [k[1], k[2], k[3]]), C.shipLocal(carrier.ship, t, [k[1], k[2] + 1.8, k[3]])];
};
const isCrew = id => (S.actors.find(a => a.id === id) || {}).kind === 'crew';
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
// where a point lands, as fractions of the visible half-width and half-height (inside when both are within 1)
function project(cam, w) {
  const d = sub(w, cam.p), z = dot(d, cam.f);
  if (z < 1) return null;
  const th = Math.tan(cam.fov * D2R / 2), sx = dot(d, cam.r) / z / (th * ASPECT), sy = dot(d, cam.u) / z / (th * ASPECT / BOXR);
  return [sx, sy, Math.hypot(...d)];
}
let bad = 0;
const rows = [];
const exempt = [];
for (const sh of S.shots) {
  for (const x of sh.absent || []) exempt.push(`${sh.name}: ${x.id}${x.from != null || x.to != null ? ' (' + (x.from != null ? x.from : sh.t0) + ' - ' + (x.to != null ? x.to : sh.t1) + ' s)' : ''}: ${x.reason}`);
  const tr = C.track(sh.cam), want = (sh.subjects || []).map(x => (Array.isArray(x) ? x : [x, -1e9, 1e9]));
  const who = S.actors.filter(a => a.kind === 'jet' || (a.kind === 'static' && a.model === 'nf24'));
  const stat = {};
  let hz = 0, nh = 0; /* the horizon: in the picture when the view's pitch is inside its vertical half-angle */
  for (let t = sh.t0; t < sh.t1 - 1e-6; t += 0.05) {
    const k = C.sample(tr, t);
    if (!k) continue;
    const fr = C.frame(k[4], k[5], k[6]);
    nh++;
    hz += Math.abs(k[5]) < Math.atan(Math.tan((k[7] > 1 ? k[7] : 42) * D2R / 2) * ASPECT / BOXR) / D2R * MARGIN;
    const cam = { p: [k[1], k[2], k[3]], f: fr.f, r: fr.r, u: fr.u, fov: k[7] > 1 ? k[7] : 42 };
    const inside = w => { const q = project(cam, w); return q && Math.abs(q[0]) < MARGIN && Math.abs(q[1]) < MARGIN; };
    const see = (id, ws) => {
      const s = stat[id] || (stat[id] = { n: 0, inside: 0, out: [], gone: [], shows: [] });
      s.n++;
      if (!ws) { s.gone.push(+t.toFixed(2)); return; }
      const all = ws.every(inside);
      s.inside += all;
      if (!all) s.out.push(+t.toFixed(2));
    };
    for (const [id, from, to] of want) if (isCrew(id) && t >= from && t <= to) see(id, crewPoints(id, t));   // the crew a shot is about
    for (const a of who) {
      const off = (sh.absent || []).find(x => x.id === a.id && t >= (x.from != null ? x.from : -1e9) && t <= (x.to != null ? x.to : 1e9));
      let ws = points(a, t);
      if (off) {   // not in this shot: then it must not show (a cockpit's own jet and the inside of a room do not draw; specks do not count)
        const far = ws && Math.hypot(ws[0][0] - cam.p[0], ws[0][1] - cam.p[1], ws[0][2] - cam.p[2]) > (a.kind === 'static' ? 500 : 6000);
        if (ws && !far && !(sh.partial || []).includes(a.id) && !sh.scene && !(sh.hud && sh.hud.actor === a.id) && ws.some(inside)) { const s = stat[a.id] || (stat[a.id] = { n: 0, inside: 0, out: [], gone: [], shows: [] }); s.shows.push(+t.toFixed(2)); }
        continue;
      }
      if (ws && sh.closeup === a.id) { const q = C.sample(tracks[a.id], t); ws = [[q[1], q[2], q[3]]]; }
      else if (ws && (sh.partial || []).includes(a.id)) ws = [ws.reduce((m, w) => [m[0] + w[0] / ws.length, m[1] + w[1] / ws.length, m[2] + w[2] / ws.length], [0, 0, 0])];
      see(a.id, ws);
    }
  }
  const parts = Object.entries(stat).map(([id, s]) => {
    const fail = s.out.length + s.gone.length + s.shows.length > 0;
    if (fail) bad++;
    const span = (l, w) => l.length ? ` ${w} ${l[0]}-${l[l.length - 1]} s (${(l.length * 0.05).toFixed(2)} s)` : '';
    return `${id} ${s.n ? Math.round(100 * s.inside / s.n) : 100}%${span(s.out, 'OUT')}${span(s.gone, 'NO PATH, NOT EXEMPT')}${span(s.shows, 'EXEMPT BUT SHOWS')}`;
  });
  rows.push(`${sh.name.padEnd(8)} ${sh.t0.toFixed(2)}-${sh.t1.toFixed(2)}  horizon ${Math.round(100 * hz / nh)}%  ${parts.join('  ')}`);
}
const flagged = S.shots.filter(x => x.closeup || x.partial).map(x => x.name + ': ' + [x.closeup, ...(x.partial || [])].filter(Boolean).join(' '));
console.log('close-ups (centre only, this jet in this shot): ' + (flagged.join(', ') || 'none'));
console.log('exemptions (not in the shot, from the script):\n  ' + exempt.join('\n  '));
console.log('wholly in frame (2.35:1 box, 92 % of it), every jet not exempted\n' + rows.join('\n'));
if (strict && bad) {
  console.log(bad + ' jet(s) leave the frame');
  process.exitCode = 1;
}
