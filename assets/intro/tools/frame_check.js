// frame_check.js - is what a shot is about in the picture? For every shot of intro_script.json it follows the camera
// keys and projects each jet (and the carrier) into the letterboxed 2.35:1 frame of a 16:9 screen, every 0.05 s. It
// reports how much of the shot each one spends in frame, and with --strict it fails when a shot's subjects (its
// `subjects`, actor ids the builder lists) leave the frame for longer than a blink.
// usage: node frame_check.js [intro_script.json] [--strict]     Original code for this project.
const C = require('../src/intro_core.js');
const path = require('path');
const args = process.argv.slice(2);
const strict = args.includes('--strict');
const S = require(path.resolve(args.find(a => !a.startsWith('--')) || path.join(__dirname, '..', 'intro_script.json')));
const D2R = Math.PI / 180, ASPECT = 16 / 9, BOX = 2.35, MARGIN = 0.92, GRACE = 0.25;
const tracks = {};
for (const a of S.actors) if (a.keys && a.keys.length) tracks[a.id] = C.track(a.keys);
// the points that must show: a jet's centre; a ship's bow and stern, so the whole ship is in the picture
const points = (a, t) => {
  if (a.kind === 'ship') return [C.shipLocal(a.ship, t, [0, 20, 160]), C.shipLocal(a.ship, t, [0, 20, -160])];
  const k = tracks[a.id] && C.sample(tracks[a.id], t);
  if (!k || (a.spawn != null && t < a.spawn) || (a.end != null && t > a.end)) return null;
  return [[k[1], k[2], k[3]]];
};
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
// where a point lands, as fractions of the visible half-width and half-height (inside when both are within 1)
function project(cam, w) {
  const d = sub(w, cam.p), z = dot(d, cam.f);
  if (z < 1) return null;
  const th = Math.tan(cam.fov * D2R / 2), sx = dot(d, cam.r) / z / (th * ASPECT), sy = dot(d, cam.u) / z / (th * ASPECT / BOX);
  return [sx, sy, Math.hypot(...d)];
}
let bad = 0;
const rows = [];
for (const sh of S.shots) {
  if (sh.scene) continue;
  const tr = C.track(sh.cam), want = (sh.subjects || []).map(x => (Array.isArray(x) ? x : [x, -1e9, 1e9]));
  const who = S.actors.filter(a => a.kind === 'jet' || a.id === 'carrier');
  const stat = {};
  let hz = 0, nh = 0; /* the horizon: in the picture when the view's pitch is inside its vertical half-angle */
  for (let t = sh.t0; t < sh.t1 - 1e-6; t += 0.05) {
    const k = C.sample(tr, t);
    if (!k) continue;
    const fr = C.frame(k[4], k[5], k[6]);
    nh++;
    hz += Math.abs(k[5]) < Math.atan(Math.tan((k[7] > 1 ? k[7] : 42) * D2R / 2) * ASPECT / BOX) / D2R * MARGIN;
    const cam = { p: [k[1], k[2], k[3]], f: fr.f, r: fr.r, u: fr.u, fov: k[7] > 1 ? k[7] : 42 };
    for (const a of who) {
      if (sh.hud && sh.hud.actor === a.id) continue;
      const need = want.some(([id, from, to]) => id === a.id && t >= from && t <= to);
      const ws = points(a, t);
      if (!ws) continue;
      const s = stat[a.id] || (stat[a.id] = { n: 0, inside: 0, out: 0, worst: 0, first: null, need: false });
      s.need = s.need || need;
      s.n++;
      const inside = ws.every(w => {
        const q = project(cam, w);
        return q && Math.abs(q[0]) < MARGIN && Math.abs(q[1]) < MARGIN && q[2] < 6000;
      });
      if (inside || !need) { s.inside += inside; s.out = 0; } else {
        s.out += 0.05;
        s.worst = Math.max(s.worst, s.out);
        if (s.first == null && s.out > GRACE) s.first = +t.toFixed(2);
      }
    }
  }
  const parts = Object.entries(stat).map(([id, s]) => {
    const need = s.need, fail = need && s.worst > GRACE;
    if (fail) bad++;
    return `${need ? '*' : ' '}${id} ${Math.round(100 * s.inside / s.n)}%${fail ? ' OUT from ' + s.first + ' s for ' + s.worst.toFixed(2) + ' s' : ''}`;
  });
  rows.push(`${sh.name.padEnd(8)} ${sh.t0.toFixed(2)}-${sh.t1.toFixed(2)}  horizon ${Math.round(100 * hz / nh)}%  ${parts.join('  ')}`);
}
console.log('in frame (2.35:1 box, 92 % of it); * = a subject of the shot\n' + rows.join('\n'));
if (strict && bad) {
  console.log(bad + ' subject(s) leave the frame');
  process.exitCode = 1;
}
