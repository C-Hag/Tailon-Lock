#!/usr/bin/env node
// launch_drone.js - a draft of the launch idea (2026-10-31), as storyboard shots:
//   "drone"  from Viper 1 leaving the bow: a camera drone 45 degrees above the take-off point, in front of the pair,
//            wide angle, looking back at their noses as they roll and climb; it flies ahead of them as they close.
//   "boom"   higher up as they reach the speed of sound: a camera hanging above the boom point, ahead and to the
//            side, panning down onto the pair as they pass under it with the ba-boom (19.5 s).
// Usage: node tools/storyboard/drafts/launch_drone.js > draft.json
'use strict';
const path = require('path');
const IC = require(path.resolve(__dirname, '../../../assets/intro/src/intro_core.js'));
const S = require(path.resolve(__dirname, '../../../assets/intro/intro_script.json'));
const A = Object.fromEntries(S.actors.map(a => [a.id, a]));
const tr = { blue1: IC.track(A.blue1.keys), blue2: IC.track(A.blue2.keys) };
const at = (id, t) => { const s = IC.sample(tr[id], t, []); return [s[1], s[2], s[3]]; };
const mid = t => { const a = at('blue1', t), b = at('blue2', t); return a.map((v, i) => (v + b[i]) / 2); };
const sub = (a, b) => a.map((v, i) => v - b[i]), add = (a, b) => a.map((v, i) => v + b[i]), mul = (a, k) => a.map(v => v * k);
const norm = a => mul(a, 1 / Math.hypot(...a));
const r2 = v => Math.round(v * 100) / 100;
const smooth = u => u * u * (3 - 2 * u), lerp = (a, b, u) => a + (b - a) * u;
const fovOf = mm => 2 * Math.atan(12 / mm) * 180 / Math.PI;
// tilt: the camera's pitch as a share of the pitch down to the subject (1 = centred on it; less lifts the frame so
// the horizon shows and the subject sits low in it)
function key(t, p, look, fov, tilt = 1) {
  const f = norm(sub(look, p)), up = [0, 1, 0], u = norm(sub(up, mul(f, f[1])));
  let [y, pi, r] = IC.angles(f, u); pi *= tilt;
  return [r2(t), r2(p[0]), r2(p[1]), r2(p[2]), r2(y), r2(pi), r2(r), r2(fov)];
}
const unwrapKeys = K => { for (let i = 1; i < K.length; i++) for (const c of [4, 6]) { while (K[i][c] - K[i - 1][c] > 180) K[i][c] -= 360; while (K[i][c] - K[i - 1][c] < -180) K[i][c] += 360; } return K; };

// Viper 1 leaves the bow at about 12.4 s (its path starts to rise), Viper 2 about 0.6 s later
const tOff = 12.4, P0 = at('blue1', tOff);
const drone = { t0: 12.6, t1: 17.4 }, boom = { t0: 17.4, t1: 20.6 };
const dk = [];
for (let t = drone.t0; t <= drone.t1 + 1.2 + 1e-6; t += 0.2) {
  // the drone leads the pair, 45 degrees up from them: 70 m ahead and 70 m up at the cut, closing to 32 m by the
  // end, so they grow toward the lens; it starts over the take-off point and its own lag smooths the pair's surge
  const u = smooth(Math.min(1, (t - drone.t0) / (drone.t1 - drone.t0))), d = lerp(70, 32, u);
  const m = mid(t), p = [m[0] + 6 * (1 - u), m[1] + d, m[2] + d];
  const look = add(m, [0, 2, -4]);
  dk.push(key(t, p, look, fovOf(16), 0.75));
}
const bk = [], zb = at('blue1', 19.5)[2];
for (let t = boom.t0; t <= boom.t1 + 1.2 + 1e-6; t += 0.2) {
  // a camera plane off their right wing and a little above, flying the same way at 125 m/s: the pair (about
  // 200 m/s) comes up from behind, passes it at the boom and pulls away
  const p = [4800 + 48, 72, zb + 125 * (t - 19.5)];
  bk.push(key(t, p, mid(t), fovOf(24), 0.8));
}
const draft = {
  format: 'tailonlock-storyboard', version: 1, title: "Claude's draft: launch drone and the boom", objects: [], hidden: [], introShotsInFilm: true,
  shots: [
    { id: 'claude_drone', name: 'drone (Claude draft)', t0: drone.t0, t1: drone.t1, scene: 'world', in: { type: 'cut', dur: 0.5 }, keys: unwrapKeys(dk),
      desc: "As Viper 1 leaves the bow, cut to a camera drone 45 degrees above the take-off point, ahead of the pair, 16 mm. Looking back at the noses as Viper 2 follows, both roll and climb toward the lens; the drone flies ahead and lets them close." },
    { id: 'claude_boom', name: 'boom (Claude draft)', t0: boom.t0, t1: boom.t1, scene: 'world', in: { type: 'cut', dur: 0.5 }, keys: unwrapKeys(bk),
      desc: "As they reach the speed of sound: a camera plane off their right wing, 20 m above them and slower; the pair closes from behind, passes it with the ba-boom at 19.5 s and pulls away. 24 mm." }
  ]
};
process.stdout.write(JSON.stringify(draft));
