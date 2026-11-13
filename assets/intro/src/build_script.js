#!/usr/bin/env node
// Builds intro_script.json, the engine-neutral shot script of the Tailon Lock opening cinematic.
// usage: node build_script.js <out.json>
// Authoring happens here (waypoints, missiles, camera moves); the output bakes everything to world-space keys so an
// engine only interpolates. See ../INTRO.md for the format. Times are seconds from the first music sample.
'use strict';
const fs = require('fs');
const C = require('./intro_core.js');
// the game's name (src/brand.h has the same for the C side)
const GAME = { title: 'Tailon Lock', logo: 'TAILON LOCK', subtitle: 'THE GREAT NOSE2NOSE ENGAGEMENT' };

// ---------- maths ----------
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const lerp = (a, b, t) => a + (b - a) * t;
const lerp3 = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
const dist = (a, b) => len(sub(a, b));
const ease = t => t * t * (3 - 2 * t);
const easeOut = t => 1 - (1 - t) * (1 - t);
const D2R = Math.PI / 180;
const dirOf = (yaw, pitch = 0) => C.frame(yaw, pitch, 0).f;
const r1 = v => Math.round(v * 10) / 10, r2 = v => Math.round(v * 100) / 100;
function rng(seed) { let a = seed >>> 0; return () => { a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }

// ---------- music ----------
// The film is cut to the main theme, Full Burn "Arp sound DMX10" (re-scored from DMX3: the same bars and timing) (assets/music/fullburn-intro.wav: half a second of
// silence, then bars 1 - 40 of the song with a fade). Its tempo table is 6 frames a sixteenth on the PAL clock
// (50.125 Hz), so a bar is 96 / 50.125 = 1.9152 s. The jet builds over the six-bar intro and the full beat comes in on
// bar 7, at 0.5 + 6 bars = 11.99 s, the band hit. bar(n) is n bars after it: the riff starts on bar(2), the verse on
// bar(10), the pre-chorus on bar(18) and the chorus on bar(26), where the smoke has just formed the title.
const HIT = 11.99, BAR = 96 / 50.125;
const bar = n => +(HIT + n * BAR).toFixed(3);

// ---------- world ----------
const CAR = { x: 4800, z: -2300, heading: 0, speed: 14 };
const DECK = 20.5, NF_MINY = -1.545, KS_MINY = -1.08;
const JY = DECK - NF_MINY;
const deck = (t, lx, lz, ly = JY) => C.shipLocal(CAR, t, [lx, ly, lz]);
const G = 9.81;

const S = { format: 'tailonlock-intro', version: 8, title: GAME.title, duration: 0, menu_at: 0,
  music: {}, env: {}, actors: [], flares: [], events: [], shots: [], cards: [], fx: [] };
const ACT = {};
function actor(id, o) { const a = Object.assign({ id, keys: [] }, o); ACT[id] = a; S.actors.push(a); return a; }

// ---------- flight paths ----------
// fly(actor, pts, opt): pts are [t, x, y, z] waypoints (world). The path is a Catmull-Rom curve through them,
// sampled at DT. Attitude comes from the motion like a real jet: nose along the velocity, lift (the top of the jet)
// along the acceleration minus gravity, so turns bank and pull-ups pitch by themselves. opt.rolls adds roll on top
// ([t, deg] pairs, linear); opt.yaw fixes the heading while the jet is (nearly) stopped; opt.rollRate (degrees a
// second, 200 unless given) caps how fast the jet may roll, so a weave in the path can't snap it over; opt.coordinated keeps the lift at
// least 0.6 g upwards, so a push-over (the nose easing down after a climb) lowers the lift instead of rolling the jet
// inverted, and the bank comes from the turn alone; opt.rollFree [t0, t1] leaves a scripted roll out of the cap.
// opt.aoa (the time the jet leaves the deck) raises the nose above the flight path by the angle of attack a wing
// needs for its lift: about 5 degrees at the 107 m/s of a catapult shot, less as it speeds up (with the square of
// the speed), more in a pull (with the g), so the jet rotates nose-up off the bow as a real one does.
const DT = 0.05, KEY_DT = 0.1;
function fly(a, pts, opt = {}) {
  opt = Object.assign({ rollRate: 200 }, opt);     // no jet rolls faster than this (a real fighter tops out near 200-300)
  const K = C.track(pts.map(p => [p[0], p[1], p[2], p[3]]));
  const t0 = pts[0][0], t1 = pts[pts.length - 1][0];
  const n = Math.round((t1 - t0) / DT), P = [];
  for (let i = 0; i <= n; i++) { const t = t0 + i * DT, s = C.sample(K, Math.min(t, t1)); P.push([s[1], s[2], s[3]]); }
  const Vv = P.map((p, i) => mul(sub(P[Math.min(n, i + 1)], P[Math.max(0, i - 1)]), 1 / (DT * (Math.min(n, i + 1) - Math.max(0, i - 1)))));
  const Ar = Vv.map((v, i) => mul(sub(Vv[Math.min(n, i + 1)], Vv[Math.max(0, i - 1)]), 1 / (DT * (Math.min(n, i + 1) - Math.max(0, i - 1)))));
  const sig = (opt.smooth || 0.25) / DT, W = Math.ceil(sig * 2.5);
  const A = Ar.map((_, i) => { let s = [0, 0, 0], w = 0; for (let k = -W; k <= W; k++) { const j = clamp(i + k, 0, n), g = Math.exp(-k * k / (2 * sig * sig)); s = add(s, mul(Ar[j], g)); w += g; } return mul(s, 1 / w); });
  const rollK = opt.rolls ? C.track(opt.rolls.map(([t, d]) => [t, 0, 0, 0, d])) : null;
  let prevU = [0, 1, 0], prev = null;
  const out = [];
  for (let i = 0; i <= n; i++) {
    const t = t0 + i * DT, v = Vv[i], sp = len(v);
    let f = sp > 8 ? norm(v) : dirOf(opt.yaw != null ? opt.yaw : CAR.heading, 0);
    let u;
    if (sp > 8 && !opt.flat) {
      let L = add(A[i], [0, G, 0]);
      if (opt.coordinated) L[1] = Math.max(L[1], 0.6 * G);   // easing the nose down never rolls the jet over
      L = sub(L, mul(f, dot(L, f)));
      if (opt.coordinated) {   // and again across the path: in a steep climb, speeding up, the floor above is not enough
        const up = norm(sub([0, 1, 0], mul(f, f[1]))), k = dot(L, up);
        if (k < 0.6 * G * Math.sqrt(1 - f[1] * f[1])) L = add(L, mul(up, 0.6 * G * Math.sqrt(1 - f[1] * f[1]) - k));
      }
      u = len(L) > 0.5 ? norm(L) : prevU;
      if (opt.gain) { const lv = sub(add(A[i], [0, G, 0]), mul(f, dot(add(A[i], [0, G, 0]), f))); const lat = sub(lv, mul([0, 1, 0], 0)); u = norm(lerp3(norm(sub([0, 1, 0], mul(f, f[1]))), u, opt.gain)); }
    } else u = norm(sub([0, 1, 0], mul(f, f[1])));
    prevU = u;
    if (opt.aoa != null && sp > 8 && t > opt.aoa) {
      const g = len(add(A[i], [0, G, 0])) / G;
      const al = Math.min(9, 5 * (107 / sp) ** 2 * Math.max(0.3, g)) * ease(clamp((t - opt.aoa) / 0.8, 0, 1)) * D2R;
      [f, u] = [add(mul(f, Math.cos(al)), mul(u, Math.sin(al))), sub(mul(u, Math.cos(al)), mul(f, Math.sin(al)))];
    }
    let [yaw, pitch, roll] = C.angles(f, u);
    if (rollK) { const s = C.sample(rollK, clamp(t, opt.rolls[0][0], opt.rolls[opt.rolls.length - 1][0])); roll += s ? s[4] : 0; }
    if (prev) { while (yaw - prev[0] > 180) yaw -= 360; while (yaw - prev[0] < -180) yaw += 360; while (roll - prev[2] > 180) roll -= 360; while (roll - prev[2] < -180) roll += 360; }
    prev = [yaw, pitch, roll];
    out.push([t, P[i][0], P[i][1], P[i][2], yaw, pitch, roll]);
  }
  if (opt.rollRate && a.keys.length) {             // carry on from the roll the jet had where this segment takes over
    const was = C.sample(C.track(a.keys.filter(k => k)), t0);
    if (was) { let r = was[6]; while (r - out[0][6] > 180) r -= 360; while (r - out[0][6] < -180) r += 360; out[0][6] = r; }
  }
  if (opt.rollRate) for (let i = 1; i <= n; i++) {
    if (opt.rollFree && out[i][0] >= opt.rollFree[0] - 0.1 && out[i][0] <= opt.rollFree[1] + 0.1) continue;   // a scripted roll
    const lim = opt.rollRate * DT, d = out[i][6] - out[i - 1][6];
    if (Math.abs(d) > lim) out[i][6] = out[i - 1][6] + Math.sign(d) * lim;
  }
  const step = Math.round(KEY_DT / DT), keys = [];
  for (let i = 0; i <= n; i += step) keys.push(out[i]);
  if ((n % step) !== 0) keys.push(out[n]);
  // a new segment replaces whatever an earlier one planned from its start time on
  const cutAt = keys[0][0], old = a.keys.length ? C.sample(C.track(a.keys), cutAt) : null;
  while (a.keys.length && (!a.keys[a.keys.length - 1] || a.keys[a.keys.length - 1][0] >= cutAt - 1e-6)) a.keys.pop();
  if (old && a.keys.length) a.keys.push(old.map((v, i) => i ? r1(v) : r2(v)));
  if (a.keys.length) a.keys.push(null);
  for (const k of keys) a.keys.push([r2(k[0]), r1(k[1]), r1(k[2]), r1(k[3]), r1(k[4]), r1(k[5]), r1(k[6])]);
  a._track = null;
  return a;
}
const track = a => a._track || (a._track = C.track(a.keys));
const at = (a, t) => C.sample(track(a), t);
function state(a, t) {
  const s = at(a, t); if (!s) return null;
  const fr = C.frame(s[4], s[5], s[6]);
  let s0 = at(a, t - 0.05) || s, s1 = at(a, t + 0.05) || s;
  // where a new path starts (the jet jumps), take the velocity from the side that stays on one path
  const sp = (p, q) => Math.hypot(q[1] - p[1], q[2] - p[2], q[3] - p[3]) / ((q[0] - p[0]) || 1);
  if (sp(s0, s) > 900) s0 = s; else if (sp(s, s1) > 900) s1 = s;
  const dtv = (s1[0] - s0[0]) || 1;
  return { pos: [s[1], s[2], s[3]], vel: [(s1[1] - s0[1]) / dtv, (s1[2] - s0[2]) / dtv, (s1[3] - s0[3]) / dtv], f: fr.f, r: fr.r, u: fr.u, yaw: s[4], pitch: s[5], roll: s[6] };
}
// straight-ish flight helper: from p0 along heading/pitch at speed, with accel; returns waypoints every dt
function line(t0, t1, p0, yaw, pitch, v0, acc = 0, dt = 0.5, bend = null) {
  const pts = []; let p = p0.slice(), v = v0, y = yaw, pi = pitch;
  for (let t = t0; t <= t1 + 1e-6; t += dt) {
    pts.push([+t.toFixed(3), p[0], p[1], p[2]]);
    const d = dirOf(y, pi);
    p = add(p, mul(d, v * dt + 0.5 * acc * dt * dt)); v += acc * dt;
    if (bend) { const b = bend(t); y += (b.yaw || 0) * dt; pi += (b.pitch || 0) * dt; }
  }
  return pts;
}
// arc helper: a level-ish turn. Positions around centre (cx, cz) with radius R, heading changes from yaw0 by turn deg.
function turn(t0, t1, p0, yaw0, turnDeg, R, y1 = null, dt = 0.25) {
  const pts = [], s = Math.sign(turnDeg), right = dirOf(yaw0 + 90 * s, 0);
  const c = add(p0, mul(right, R));
  for (let t = t0; t <= t1 + 1e-6; t += dt) {
    const u = (t - t0) / (t1 - t0), yaw = yaw0 + turnDeg * u;
    const back = dirOf(yaw - 90 * s, 0), p = add(c, mul(back, R));
    p[1] = lerp(p0[1], y1 == null ? p0[1] : y1, ease(u));
    pts.push([+t.toFixed(3), p[0], p[1], p[2]]);
  }
  return pts;
}

// ---------- weapons ----------
let flareSeq = 0, mslSeq = 0;
function flares(a, t, n = 6, every = 0.09, side = 1) {
  const R = rng(1000 + flareSeq * 7), list = [];
  for (let i = 0; i < n; i++) {
    const tt = +(t + i * every).toFixed(3), st = state(a, tt); if (!st) continue;
    const tail = add(st.pos, mul(st.f, -8));
    const ej = add(add(mul(st.r, (i % 2 ? 1 : -1) * (18 + 10 * R())), mul(st.u, -14 - 8 * R())), mul(st.f, -10));
    const v = add(mul(st.vel, 0.35), ej);
    const fl = { id: 'fl' + (flareSeq++), t: tt, p: tail.map(r1), v: v.map(r1), life: 2.6 };
    S.flares.push(fl); list.push(fl);
  }
  S.events.push({ t, type: 'sound', name: 'flares' });
  return list;
}
// missile(shooter, target, t, opt): proportional navigation with motor boost and a g limit. opt.decoy: [t, flares]
// switches the seeker to the hottest (newest) flare in view at that time; the warhead fuzes inside `fuze` metres.
function missile(shooter, target, t0, opt = {}) {
  const id = 'msl' + (++mslSeq), m = actor(id, { kind: 'missile', model: 'missile', team: shooter.team, trail: 'smoke' });
  const st = state(shooter, t0);
  let p = add(add(st.pos, mul(st.u, -1.6)), mul(st.r, opt.rail || 3.4)), v = add(st.vel, mul(st.f, 12));
  let goal = () => { const s = at(target, tt); return s ? [s[1], s[2], s[3]] : null; }, tt = t0;
  const out = [], dt = 1 / 120, fuze = opt.fuze || 9;
  let prevLos = null, end = null, hitT = null, kind = 'miss';
  for (let k = 0; k < 120 * 6; k++) {
    tt = t0 + k * dt;
    if (opt.decoy && tt >= opt.decoy[0] && !goal.decoy) {
      const fl = opt.decoy[1];
      const pick = fl.filter(f => f.t <= tt).sort((a, b) => b.t - a.t)[0] || fl[0];
      goal = () => C.flarePos(pick, tt); goal.decoy = true;
    }
    const g = goal();
    if (!g) { end = p; break; }
    const los = sub(g, p), dist = len(los);
    if (dist < fuze) { end = p; hitT = tt; kind = goal.decoy ? 'decoy' : 'hit'; break; }
    const sp = len(v), age = tt - t0;
    const thrust = age < 0.15 ? 0 : age < 1.8 ? 620 : 0;
    const losN = mul(los, 1 / dist);
    let acc = mul(norm(v), thrust - 0.0006 * sp * sp);
    if (age > 0.15) {
      if (prevLos) {
        const rate = mul(sub(losN, prevLos), 1 / dt), cv = Math.max(0, -dot(sub(goal.vel ? goal.vel : [0, 0, 0], v), losN));
        let lat = mul(rate, 4 * Math.max(cv, sp));
        lat = sub(lat, mul(norm(v), dot(lat, norm(v))));
        const lim = 320; if (len(lat) > lim) lat = mul(norm(lat), lim);
        acc = add(acc, lat);
        acc = add(acc, mul(sub(losN, norm(v)), 160 * Math.max(0, 1 - age)));
      }
      prevLos = losN;
    } else acc = add(acc, [0, -G, 0]);
    v = add(v, mul(acc, dt)); p = add(p, mul(v, dt));
    if (k % 6 === 0) out.push([tt, p[0], p[1], p[2]]);
    if (age > (opt.life || 4)) { end = p; hitT = tt; kind = 'selfdestruct'; break; }
  }
  out.push([hitT || tt, end[0], end[1], end[2]]);
  fly(m, out, { smooth: 0.06, flat: true });
  m.spawn = t0; m.end = hitT || tt;
  S.events.push({ t: t0, type: 'sound', name: 'missile', actor: shooter.id });
  S.events.push({ t: +m.end.toFixed(3), type: 'explode', pos: end.map(r1), size: kind === 'hit' ? 2 : 1, actor: id });
  if (kind === 'hit') kill(target, m.end, opt.kill || {});
  return { m, kind, t: m.end, pos: end };
}
// a hit jet: the flight path stops and a burning, tumbling wreck falls to the sea
function kill(a, t, o) {
  const st = state(a, t), segEnd = a.keys.findIndex(k => k && k[0] > t);
  // truncate the live path at t and continue as a ballistic wreck
  let i = a.keys.length; for (let j = a.keys.length - 1; j >= 0; j--) if (a.keys[j] && a.keys[j][0] > t) i = j;
  a.keys = a.keys.slice(0, i); a._track = null;
  const k0 = [r2(t), r1(st.pos[0]), r1(st.pos[1]), r1(st.pos[2]), r1(st.yaw), r1(st.pitch), r1(st.roll)];
  a.keys.push(k0);
  // o.fall: seconds to the sea; the wreck then dives, pushed down as hard as it takes (found by bisection)
  const drag = o.fall ? 0.12 : 0.35, fallFor = push => { let q = st.pos, w = add(mul(st.vel, 0.75), [0, -push, 0]), n = 0;
    while (q[1] > 0 && n < 400) { w = add(mul(w, Math.exp(-drag * 0.1)), [0, -G * 0.1, 0]); q = add(q, mul(w, 0.1)); n++; } return n * 0.1; };
  let push = 0;
  if (o.fall) { let lo = 0, hi = 400; for (let i = 0; i < 30; i++) { push = (lo + hi) / 2; if (fallFor(push) > o.fall) lo = push; else hi = push; } }
  let p = st.pos, v = add(mul(st.vel, 0.75), [0, -push, 0]), yaw = st.yaw, pitch = st.pitch, roll = st.roll, tt = t;
  const dt = 0.1;
  while (p[1] > 0 && tt < t + 14) {
    v = add(mul(v, Math.exp(-drag * dt)), [0, -G * dt, 0]); p = add(p, mul(v, dt)); tt += dt;
    yaw += 70 * dt; roll += 260 * dt; pitch = lerp(pitch, -50, 0.6 * dt);
    a.keys.push([r2(tt), r1(p[0]), r1(Math.max(p[1], 0)), r1(p[2]), r1(yaw), r1(pitch), r1(roll)]);
  }
  a._track = null;
  a.dead = r2(t);
  S.events.push({ t: r2(t), type: 'kill', actor: a.id });
  S.events.push({ t: r2(tt), type: 'splash', pos: [r1(p[0]), 0, r1(p[2])], actor: a.id });
}
function guns(a, t0, t1) { S.events.push({ t0, t1, type: 'guns', actor: a.id }); }

// ---------- actors ----------
actor('carrier', { kind: 'ship', model: 'cvn90', team: 0, ship: CAR, wake: 1 });
actor('frigate1', { kind: 'ship', model: 'frigate', team: 0, ship: { x: 3900, z: -3000, heading: 0, speed: 14 }, wake: 0.6 });
actor('frigate2', { kind: 'ship', model: 'frigate', team: 0, ship: { x: 5700, z: -3000, heading: 0, speed: 14 }, wake: 0.6 });
// parked jets on deck (carrier-local position, heading relative to the ship)
const parked = [[22, -95, -70], [22, -118, -70], [24, -72, -70], [20, -140, -110]];
parked.forEach(([lx, lz, h], i) => actor('parked' + (i + 1), { kind: 'static', model: 'nf24', team: 0, attach: 'carrier', pos: [lx, JY, lz], heading: h }));
const blue1 = actor('blue1', { kind: 'jet', model: 'nf24', team: 0, callsign: 'VIPER 1' });
const blue2 = actor('blue2', { kind: 'jet', model: 'ja46', team: 0, callsign: 'VIPER 2' });   // JA 46 Gladan
const red1 = actor('red1', { kind: 'jet', model: 'ks55', team: 1, callsign: 'BANDIT 1' });
// the wingman is not a pilot: a DR-7 Vesper, the unmanned drone of Beat the Drone! (MESH_DRONE in the game)
const red2 = actor('red2', { kind: 'jet', model: 'dr7', team: 1, callsign: 'VESPER' });

// ---------- 1. catapult launch and the low run over the sea (10.0 - 18.9) ----------
// A real catapult launch: the jet leaves the bow wings level near its minimum flying speed, about 20 m over the sea,
// so nothing rolls there. It rotates into a 6 degree climb for a second and a half, wings level; once it is clearly
// climbing (2.5 s off the deck) it makes a gentle clearing turn away from the other catapult (side: +1 right, -1 left;
// about 25 degrees of bank),
// then a gentler reversal (15 degrees) back onto the run heading. It keeps climbing all the way (a jet off the
// catapult never dives back at the sea): 6 degrees off the bow, then a shallow half-degree climb for the low run.
function launch(a, lx, tStart, stroke, tEnd, side, z0 = 56, ly = JY) {
  const pts = [];
  for (let t = 0; t < tStart - 1e-6; t += 1) pts.push([t, ...deck(t, lx, z0, ly)]);
  pts.push([tStart, ...deck(tStart, lx, z0, ly)]);
  const accel = 2 * (151 - z0) / (stroke * stroke);
  for (let k = 1; k <= 10; k++) { const t = tStart + stroke * k / 10, s = 0.5 * accel * (t - tStart) ** 2; pts.push([+t.toFixed(3), ...deck(t, lx, z0 + s, ly)]); }
  const tb = tStart + stroke, P0 = deck(tb, lx, 151, ly), v0 = CAR.speed + accel * stroke;
  const bump = (x, a0, a1) => x <= a0 || x >= a1 ? 0 : Math.sin(Math.PI * (x - a0) / (a1 - a0)) ** 2;   // smooth 0-1-0
  const speed = tau => v0 + 16 * tau;
  // flight path angle (degrees): rotate, climb, then pull up hard (2026-10-31, the storyboard: the pair
  // roll and accelerate upward toward the drone camera, and are high in the air for the sonic boom)
  // 6 degrees off the bow, up to 20 degrees through Viper 1's victory roll, held, then eased toward 9 through the boom
  const climb = tau => tau < 0.2 ? 0 : tau < 1.0 ? 6 * ease((tau - 0.2) / 0.8) : tau < 2.0 ? 6 : tau < 3.5 ? 6 + 14 * ease((tau - 2.0) / 1.5)
    : tau < 5.6 ? 20 : tau < 9.0 ? 20 - 11 * ease((tau - 5.6) / 3.4) : 9;   // eased over gently: a push-over harder than gravity rolls the jet over
  // yaw rate (degrees a second) for a given bank at the current speed
  const rate = (bank, tau) => G * Math.tan(bank * D2R) / speed(tau) / D2R;
  const CLR = [4.5, 5.9], REV = [6.3, 7.9];      // seconds after the deck: wings level for 2.5, the roll, then the turns
  let p = P0.slice(), yaw = 0, gam = 0, h = 0;
  const dt = 0.05, T = tEnd - tb;
  let clr = 0, rev = 0; for (let tau = 0; tau < T; tau += dt) { clr += rate(22, tau) * bump(tau, ...CLR) * dt; rev += bump(tau, ...REV) * dt; }
  const revRate = clr / rev;                      // the reversal takes back exactly what the clearing turn took
  for (let tau = 0, k = 0; tau <= T + 1e-6; tau += dt, k++) {
    if (k % 5 === 0 && k) pts.push([+(tb + tau).toFixed(3), p[0], p[1], p[2]]);   // (the stroke's last point is tb's)
    gam = climb(tau);
    yaw += side * (rate(22, tau) * bump(tau, ...CLR) - revRate * bump(tau, ...REV)) * dt;
    p = add(p, mul(dirOf(yaw, gam), speed(tau) * dt));
  }
  return pts.filter(q => q[0] <= tEnd + 1e-6);
}
const t1End = bar(4.5);         // 20.61: end of the low run, start of the climb
const runB1 = launch(blue1, 8, 10.0, 2.05, t1End, 1);
const runB2 = launch(blue2, -8, 10.95, 1.7, t1End, -1, 80, DECK + 0.92);   // spotted well ahead of Viper 1, so the two never overlap on the deck, and off the bow as before
{ // close up alongside the leader (line abreast) by the end of the low run, so both go through the barrier together
  // (measured at the same moment on both paths: their last points are at different times)
  // (2026-10-31: in height as well, now that they climb hard, and sooner, so the drone has both together for the roll)
  const at1 = (t, c) => { const j = runB1.findIndex(q => q[0] >= t); if (j < 1) return runB1[runB1.length - 1][c]; const a = runB1[j - 1], b = runB1[j]; return a[c] + (b[c] - a[c]) * (t - a[0]) / (b[0] - a[0]); };
  const tb2 = 10.6 + 2.05;
  for (const p of runB2) if (p[0] > tb2) { const k = ease(clamp((p[0] - tb2 - 0.3) / 2.6, 0, 1)); p[2] = lerp(p[2], at1(p[0], 2), k); p[3] = lerp(p[3], at1(p[0], 3), k); }
}
// pull-up into the merge, head on with the bandits on the fanfare crash (bar 4 = 18.88)
const tMerge = bar(9);
// the climb to the merge: up at 14 degrees off the sea, levelling out at CRUISE metres, clear of the islands' hills
// (terrainMax looks under the path in the map's height grid), then the merge head on
const TER = JSON.parse(fs.readFileSync(require('path').join(__dirname, '..', 'intro_assets.json'))).terrain;
function groundAt(x, z) {
  const gx = (x - TER.origin[0]) / TER.spacing, gz = (z - TER.origin[1]) / TER.spacing, n = TER.n;
  const i = clamp(Math.floor(gx), 0, n - 2), j = clamp(Math.floor(gz), 0, n - 2), u = clamp(gx - i, 0, 1), w = clamp(gz - j, 0, 1);
  const h = (a, b) => TER.h[b * n + a];
  return lerp(lerp(h(i, j), h(i + 1, j), u), lerp(h(i, j + 1), h(i + 1, j + 1), u), w);
}
const CRUISE = 480;
function pullUp(run) {
  const last = run[run.length - 1], prev = run[run.length - 2];
  const v = (last[3] - prev[3]) / (last[0] - prev[0]);
  const pts = run.slice();
  let p = [last[1], last[2], last[3]], sp = Math.min(v, 250), pitch = Math.atan2(last[2] - prev[2], Math.hypot(last[1] - prev[1], last[3] - prev[3])) / D2R;
  for (let t = last[0] + 0.25; t <= tMerge + 2.6 + 1e-6; t += 0.25) {
    const goal = p[1] < CRUISE - 120 ? 14 : p[1] < CRUISE ? 14 * (CRUISE - p[1]) / 120 : 0;
    pitch += clamp(goal - pitch, -6 * 0.25, 10 * 0.25);
    sp += 2 * 0.25;
    p = add(p, mul(dirOf(0, pitch), sp * 0.25));
    pts.push([+t.toFixed(3), p[0], p[1], p[2]]);
  }
  return pts;
}
// Viper 1's victory roll: 2.5 s wings level off the bow, then one 360-degree aileron roll to the left while it climbs,
// the rate easing in and out (0.35 s each) around 240 degrees a second, 1.85 s in all. Not what a real pilot does at
// this height and speed, but it looks great. The merge break that follows is counted on from 360.
const VROLL = [+(12.05 + 2.5).toFixed(2), 1.85, 0.35];
const vroll = (() => {
  const [t0, T, ramp] = VROLL, peak = 360 / (T - ramp), keys = [];
  let deg = 0;
  for (let t = 0; t <= T + 1e-9; t += 0.05) {
    keys.push([+(t0 + t).toFixed(3), +(-deg).toFixed(2)]);
    const r = t < ramp ? peak * ease(t / ramp) : t > T - ramp ? peak * ease((T - t) / ramp) : peak;
    deg = Math.min(360, deg + r * 0.05);
  }
  keys[keys.length - 1][1] = -360;
  return keys;
})();
fly(blue1, pullUp(runB1), { yaw: 0, coordinated: true, aoa: 10.0 + 2.05, rollFree: [VROLL[0], VROLL[0] + VROLL[1]],
  rolls: [...vroll, [tMerge + 0.4, -360], [tMerge + 0.9, -290], [tMerge + 2.0, -285]] });
fly(blue2, pullUp(runB2), { yaw: 0, coordinated: true, aoa: 10.6 + 2.05, rolls: [[tMerge + 0.4, 0], [tMerge + 0.9, -65], [tMerge + 2.0, -70]] });
const mB1 = state(blue1, tMerge), mB2 = state(blue2, tMerge);
// bandits come down the line from the north, just above and to the left
// (the approach from the cockpit, and the cut to the outside as they pass, on the merge cue)
const vR = 215, T_TALLY = +(bar(10) - 5).toFixed(3), T_CP = 22.8, T_MERGE = +(tMerge - 0.15).toFixed(3);   // the bandits are there from the cut into the cockpit
function bandit(a, ref, off, roll) {
  const pass = add(ref.pos, off), d = dirOf(180, -4);
  const pts = [];
  for (let t = T_CP; t <= tMerge + 2.2 + 1e-6; t += 0.25) pts.push([+t.toFixed(3), ...add(pass, mul(d, vR * (t - tMerge)))]);
  fly(a, pts, { yaw: 180, rolls: [[T_CP, 0], [tMerge + 0.2, 0], [tMerge + 0.7, roll]] });
}
bandit(red1, mB1, [-7, 7, 0], -80);   // between the Vipers: tight enough for a very long lens
bandit(red2, mB2, [7, -6, 0], 75);

// The film flies the dogfight as two continuous fights, so each can be one long take: Viper 1 against the KS-55
// straight out of the merge (bars 7 - 11), then Viper 2 against the drone (bars 11 - 15), and the drone's run for the
// sunrise. Nobody jumps to a new place between phases any more.
// the riff (bars 2 - 10) carries the low run and the merge, the verse the two fights (bars 10 - 14 and 14 - 18), the
// pre-chorus the drone's run and the last kill, so the title forms on the chorus
const tS7 = bar(10), tS9 = bar(14), tS11 = bar(18);
const tKill2 = bar(24.25), tEnd = 90;
// fly on from a state: heading eases toward a goal yaw (with an optional jink, degrees a second), pitch toward a goal
// pitch. yaw and pitch may be functions of (t, position).
function onward(s, t0, t1, o) {
  const pts = []; let p = s.pos.slice(), y = Math.atan2(s.vel[0], s.vel[2]) / D2R, pi = Math.asin(clamp(s.vel[1] / (len(s.vel) || 1), -1, 1)) / D2R, sp = len(s.vel);
  for (let t = t0; t <= t1 + 1e-6; t += 0.25) {
    pts.push([+t.toFixed(3), p[0], p[1], p[2]]);
    const g = typeof o.yaw === 'function' ? o.yaw(t, p) : o.yaw;
    let dy = g == null ? 0 : g - y; while (dy > 180) dy -= 360; while (dy < -180) dy += 360;
    y += clamp(dy * 1.2, -o.rate, o.rate) * 0.25 + (o.jink ? o.jink(t) * 0.25 : 0);
    const gp = typeof o.pitch === 'function' ? o.pitch(t, p) : o.pitch;
    pi += clamp((gp - pi) * 1.5, -o.prate, o.prate) * 0.25;
    sp += ((o.speed || sp) - sp) * 0.5 * 0.25;
    p = add(p, mul(dirOf(y, pi), sp * 0.25));
  }
  return pts;
}
// steer toward another jet's position a little ahead in time (pure pursuit with lead); holds when it is gone
const pursue = (b, lead) => {
  let last = null;
  const at3 = t => { const r = at(b, t); return r ? [r[1], r[2], r[3]] : null; };
  return {
    yaw: (t, p) => { const r = at3(t + lead); if (r) last = r; return last ? Math.atan2(last[0] - p[0], last[2] - p[2]) / D2R : null; },
    pitch: (t, p) => { const r = at3(t + lead) || last; return r ? clamp(Math.atan2(r[1] - p[1], Math.hypot(r[0] - p[0], r[2] - p[2])) / D2R, -15, 20) : 0; }
  };
};

// ---------- 2. Viper 1 and the KS-55 (bar 7 - 11: 23.94 - 30.76) ----------
// Out of the merge the KS-55 has turned back in ahead of Viper 1. Fox two: the bandit fans flares and breaks left, the
// missile takes the flares. Viper 1 stays in lag behind the turning bandit, it reverses, Viper 1 cuts across, closes,
// and the second missile goes through the late flares. Splash one.
let killT;
const FIGHT1_YAW = 65;
{
  // the whole fight is turned toward the sun at the cut, and the bandit's turn kept small: the flares go into the sun, and the kill is still against it
  const s0 = state(blue1, tS7), sp = Math.hypot(s0.vel[0], s0.vel[2]), s = Object.assign({}, s0, { vel: [sp * Math.sin(FIGHT1_YAW * D2R), s0.vel[1], sp * Math.cos(FIGHT1_YAW * D2R)] });
  const f = norm([s.vel[0], 0, s.vel[2]]), yaw0 = FIGHT1_YAW;
  const R0 = add(add(s.pos, mul(f, 560)), [0, 70, 0]);
  fly(red1, onward({ pos: R0, vel: mul(f, 215) }, tS7, tS11, { yaw: t => t < tS7 + 0.9 ? yaw0 : t < tS7 + 3.6 ? yaw0 + 70 : yaw0 + 40,
    rate: 26, pitch: 0, prate: 6, speed: 225 }));
  const pu = pursue(red1, 0.8);
  fly(blue1, onward(s, tS7, tS11 + 1, { yaw: pu.yaw, rate: 22, pitch: pu.pitch, prate: 14, speed: 245 }));
  const fl = flares(red1, +(tS7 + 0.85).toFixed(3), 8, 0.08);
  missile(blue1, red1, +(tS7 + 0.2).toFixed(3), { decoy: [tS7 + 1.0, fl], fuze: 14 });
  flares(red1, +(tS7 + 5.0).toFixed(3), 5, 0.1);
  const hit = missile(blue1, red1, +(tS7 + 4.3).toFixed(3), { fuze: 10 });
  if (hit.kind !== 'hit') throw new Error('splash one missed: ' + hit.kind);
  killT = hit.t;
}
// ---------- 3. Viper 2 and the drone (bar 11 - 15: 30.76 - 37.59) ----------
// The drone sits behind Viper 2 and fires; flares and a hard break into it, the missile misses. The drone has more
// speed and overshoots: it slides past outside Viper 2's turn and ends up ahead of it, the two reversing across each
// other in a scissors with Viper 2 now behind, and Viper 2 gets the guns. The drone flies Viper 2's own track shifted
// in time (behind it, then ahead), plus a sideways slide during the overshoot.
{
  const P0 = [4500, 640, 200], yaw = 300, PER = 2.6, pre = 2.6;
  const weave = (t, t0, amp, ph) => t < t0 ? 0 : amp * (2 * Math.PI / PER) * Math.cos(2 * Math.PI * (t - t0) / PER + ph);
  fly(blue2, onward({ pos: add(P0, mul(dirOf(yaw, 0), -205 * pre)), vel: mul(dirOf(yaw, 0), 205) }, tS9 - pre, tS11 + 1.5, { yaw: null, rate: 0, pitch: 0, prate: 5, speed: 200,
    jink: t => t < tS9 + 0.9 ? 0 : t < tS9 + 3.1 ? 44 : weave(t, tS9 + 3.1, 24, Math.PI / 2) }), { rollRate: 120 });
  const lagD = t => lerp(1.9, -1.1, ease(clamp((t - tS9 - 1.0) / 4.0, 0, 1)));
  const slide = t => 75 * Math.sin(Math.PI * clamp((t - tS9 - 1.2) / 3.8, 0, 1));
  const pts = [];
  for (let t = tS9; t <= tS11 + 0.25 + 1e-6; t += 0.25) {
    const b = state(blue2, t - lagD(t)), side = norm(cross([0, 1, 0], b.f));
    pts.push([+t.toFixed(3), ...add(add(b.pos, mul(side, -slide(t))), [0, 30 + 25 * Math.sin(Math.PI * clamp((t - tS9 - 1.2) / 3.8, 0, 1)), 0])]);
  }
  fly(red2, pts, { smooth: 0.3, rollRate: 120 });
  const fl = flares(blue2, +(tS9 + 0.7).toFixed(3), 8, 0.08);
  missile(red2, blue2, +(tS9 + 0.15).toFixed(3), { decoy: [tS9 + 0.95, fl], fuze: 14, rail: -3.2 });
  guns(blue2, +(tS9 + 5.2).toFixed(3), +(tS9 + 6.0).toFixed(3));
  for (const a of [red2, blue2]) (a.trail = a.trail || []).push([+(tS9 + 2.5).toFixed(3), +(tS11 + 0.3).toFixed(3)]);
}
// Viper 1 is back from its kill at the start of the second fight: top cover over the scissors, in the same picture
{
  const m = t => lerp3(state(blue2, t).pos, state(red2, t).pos, 0.5), pts = [];
  for (let t = tS9; t <= tS11 - 0.1; t += 0.25) pts.push([+t.toFixed(3), ...add(m(t), [0, 62, 0])]);
  pts.push([+(tS11 + 0.05).toFixed(3), ...add(m(tS11 + 0.05), [0, 62, 0])]);
  fly(blue1, pts, { smooth: 0.8, rollRate: 90 });
}
// ---------- scenery ----------
// A towering cumulus stands off to the north-west as scenery (an extra cloud next to the map's own weather).
const CLOUD = { x: 4200, z: 2500, base: 1020, top: 1500 };
{
  const R = rng(77), puffs = [];
  const H = CLOUD.top - CLOUD.base;
  for (let i = 0; i < 40; i++) {
    const h = Math.pow(R(), 0.8), rr = (1 - h * 0.5) * 430;
    const a = R() * Math.PI * 2, d = Math.sqrt(R()) * rr;
    const rad = 115 + 110 * R() * (1 - h * 0.4);
    puffs.push([Math.round(Math.cos(a) * d), Math.round(h * (H - rad * 0.7) + rad * 0.3), Math.round(Math.sin(a) * d), Math.round(rad)]);
  }
  const tower = (x, z, base, top, seed) => {   // towers either side of the sun, for the last chase
    const Q = rng(seed), p = [], Ht = top - base;
    for (let i = 0; i < 36; i++) { const h = Math.pow(Q(), 0.8), rr = (1 - h * 0.45) * 520, a = Q() * Math.PI * 2, d = Math.sqrt(Q()) * rr, rad = 150 + 130 * Q() * (1 - h * 0.4);
      p.push([Math.round(Math.cos(a) * d), Math.round(h * (Ht - rad * 0.7) + rad * 0.3), Math.round(Math.sin(a) * d), Math.round(rad)]); }
    return { x, z, base, top, puffs: p };
  };
  S.env.extra_clouds = [Object.assign({}, CLOUD, { puffs }), tower(8200, 4600, 760, 2100, 91), tower(8600, 800, 700, 1900, 92)];
  // the last chase flies east into the sunrise at about 1 km: clear the towering cumulus that stands on its path
}
{
  // the drone: out of the scissors it extends, climbs and jinks toward the low sun
  // (a slow weave, 5 degrees a second at most: a hard jink would roll it past 70 degrees each way twice a second)
  fly(red2, onward(state(red2, tS11), tS11, tKill2 + 3, { yaw: 76, rate: 16, pitch: 9, prate: 5, speed: 240, jink: t => 5 * Math.sin((t - tS11) * 1.2) }), { rollRate: 90 });
  // Viper 2 eases from its own track into lag pursuit on the drone's path, then closes to a firing position
  const s2 = state(blue2, tS11), v2 = s2.vel;
  const lag = t => lerp(1.1, 2.4, ease(clamp((t - tS11) / 3, 0, 1))) - lerp(0, 0.6, ease(clamp((t - (tKill2 - 4)) / 3, 0, 1)));
  const chase = [];
  for (let t = tS11; t <= tKill2 + 0.75 + 1e-6; t += 0.25) {
    const s = at(red2, t - lag(t)), own = add(s2.pos, mul(v2, t - tS11));
    chase.push([+t.toFixed(3), ...lerp3(own, [s[1] + 6, s[2] - 18, s[3]], ease(clamp((t - tS11) / 2.6, 0, 1)))]);
  }
  fly(blue2, chase, { smooth: 0.7, rollRate: 90 });
  // Viper 1 flies on Viper 2's right through the pursuit, then drops back on its beam, out of the cockpit's view
  const T_L = bar(21), slot = (t, sd, fw, up) => { const st = state(blue2, t), side = norm(cross([0, 1, 0], [st.vel[0], 0, st.vel[2]])), f = norm(st.vel); return add(add(add(st.pos, mul(side, sd)), mul(f, fw)), [0, up, 0]); };
  const join = [], join2 = [];
  for (let t = tS11; t <= T_L + 0.5; t += 0.25) join.push([+t.toFixed(3), ...slot(t, 38, 12, 14)]);
  // from the cockpit of Viper 2 it is in the windscreen: ahead of it, a little high and right, on the way to the drone
  for (let t = T_L; t <= tKill2 + 0.75 + 1e-6; t += 0.25) join2.push([+t.toFixed(3), ...slot(t, 26, 130, 10)]);
  fly(blue1, join, { smooth: 0.45 });
  fly(blue1, join2, { smooth: 0.8, rollRate: 90 });
}
// the last missile: Viper 2 fires so that it hits on bar 13. Each try is rolled back.
let killT2, killPos2;
{
  const snap = () => ({ a: S.actors.length, e: S.events.length, f: S.flares.length, m: mslSeq, keys: red2.keys.slice(), dead: red2.dead });
  const back = s => { for (const a of S.actors.splice(s.a)) delete ACT[a.id]; S.events.length = s.e; S.flares.length = s.f; mslSeq = s.m; red2.keys = s.keys; red2.dead = s.dead; red2._track = null; };
  let L = tKill2 - 1.4;
  for (let i = 0; i < 6; i++) { const s = snap(), h = missile(blue2, red2, +L.toFixed(3), { fuze: 10 }); back(s); if (h.kind !== 'hit') { L -= 0.3; continue; } L += tKill2 - h.t; }
  flares(red2, +(tKill2 - 0.75).toFixed(3), 5, 0.1);
  // the wreck dives into the sea 7.6 s after the hit, as the smoke title thins away to nothing
  const hit = missile(blue2, red2, +L.toFixed(3), { fuze: 10, kill: { fall: 7.6 } });
  killT2 = hit.t; killPos2 = hit.pos.map(r1);
  S.events.push({ t: r2(killT2 + 0.05), type: 'explode', pos: killPos2, size: 3, actor: red2.id });
}
// nothing flies into a hill, and the drone's run for the sunrise has open sky: up to four circles along it
for (const a of [blue1, blue2]) for (let t = 10; t < tKill2; t += 0.25) {
  const q = at(a, t); if (q && q[2] < groundAt(q[1], q[3]) + 40 && groundAt(q[1], q[3]) > 1) throw new Error(a.id + ' hits the ground at ' + t.toFixed(2));
}
S.env.clear_clouds = [0, 1, 2, 3].map(k => { const q = at(red2, lerp(tS11, tKill2 + 1, k / 3)); return [Math.round(q[1]), Math.round(q[3]), 700]; });
// and the line to the sun from where the KS-55 dies: a low sun goes behind any cumulus on the horizon
S.env.clear_clouds.push([7300, 2900, 1100], [9100, 3200, 1200], [11100, 3550, 1300], [13100, 3900, 1400]);
// the last missile decides where the finale cuts
const L2 = S.actors.filter(a => a.kind === 'missile').pop().spawn;
const tFinale = +(L2 + 0.8).toFixed(3);
// after the kill (dive slightly and break away, not follow the destroyed plane all the way
// down): Viper 2 eases its nose down a few degrees for two seconds, then breaks off in a hard climbing turn to the
// right, away from the falling wreck; Viper 1 stays on its wing
const BRK = { side: 1, dive: -8, climb: 14, rate: 15 };
{
  const K = state(blue2, killT2), sp0 = len(K.vel), yaw0 = Math.atan2(K.vel[0], K.vel[2]) / D2R, gam0 = Math.asin(clamp(K.vel[1] / sp0, -1, 1)) / D2R;
  const gamAt = tau => tau < 1.0 ? lerp(gam0, BRK.dive, ease(tau / 1.0)) : tau < 1.8 ? BRK.dive : lerp(BRK.dive, BRK.climb, ease(clamp((tau - 1.8) / 2.0, 0, 1)));
  const turn = tau => ease(clamp((tau - 1.6) / 0.7, 0, 1)) * (1 - ease(clamp((tau - 8.5) / 1.2, 0, 1)));   // about 110 degrees, then away
  let p = K.pos.slice(), yaw = yaw0; const pts = [], dt = 0.05;
  for (let tau = 0, k = 0; killT2 + tau <= tEnd + 0.3 + 1e-6; tau += dt, k++) {
    if (k % 5 === 0) pts.push([+(killT2 + tau).toFixed(3), ...p]);
    yaw += BRK.side * BRK.rate * turn(tau) * dt;
    p = add(p, mul(dirOf(yaw, gamAt(tau)), (sp0 + 6 * tau) * dt));
  }
  fly(blue2, pts, { smooth: 0.5, coordinated: true, rollRate: 120 });
  const st = state(blue2, killT2 - 0.3), side = norm(cross([0, 1, 0], [st.vel[0], 0, st.vel[2]]));
  const wing = t => { const q = state(blue2, t), sd = norm(cross([0, 1, 0], [q.vel[0], 0, q.vel[2]])), f = norm([q.vel[0], 0, q.vel[2]]);
    return add(add(add(q.pos, mul(sd, 34 * BRK.side)), mul(f, -22)), [0, 12, 0]); };
  const pre = t => add(add(state(blue2, Math.min(t, killT2 - 0.01)).pos, mul(side, 20)), [0, 7, 0]), w1 = [];
  for (let t = tFinale; t <= tEnd + 0.3 + 1e-6; t += 0.25) w1.push([+t.toFixed(3), ...lerp3(pre(t), wing(Math.max(t, killT2)), ease(clamp((t - killT2) / 1.6, 0, 1)))]);
  fly(blue1, w1, { smooth: 0.5, coordinated: true, rollRate: 120 });
}
// Viper 2 flies on Viper 1's wing through the first fight (the fight of its own opens at the cut after it)
{
  const tmp = { keys: [] }, pts = [];
  for (let t = tS7; t <= tS9 + 0.2; t += 0.25) { const st = state(blue1, Math.min(t, tS9 - 0.02)), sd = norm(cross([0, 1, 0], [st.vel[0], 0, st.vel[2]])), f = norm([st.vel[0], 0, st.vel[2]]); pts.push([+t.toFixed(3), ...add(add(add(st.pos, mul(sd, 34)), mul(f, 46)), [0, 14, 0])]); }
  fly(tmp, pts, { smooth: 0.8, rollRate: 90 });
  const a0 = blue2.keys.filter(k => k && k[0] < r2(tS7)), a1 = blue2.keys.filter(k => k && k[0] >= r2(tS9));
  // (the keys of the later path keep their segments: only the first fight's stretch is replaced)
  const rest = blue2.keys.slice(blue2.keys.findIndex(k => k && k[0] >= r2(tS9)));
  const edge = (tr, t) => C.sample(tr, t).map((v, q) => +v.toFixed(q ? 2 : 3));
  const end0 = edge(track(blue2), r2(tS7) - 0.002), end1 = edge(C.track(tmp.keys), r2(tS9) - 0.002);
  blue2.keys = [...a0, end0, null, ...tmp.keys.filter(k => k && k[0] < r2(tS9)), end1, null, ...rest]; blue2._track = null;
}
for (const a of [blue1, blue2]) (a.trail = a.trail || []).push([r2(killT2 + 0.2), r2(killT2 + 4)]);
// afterburner windows [t0, t1, level]
blue1.ab = [[9.4, 9.9, 0.5], [9.9, tS7 + 1.2, 1], [tS7 + 3.5, killT, 0.8], [tS11 - 0.3, tEnd, 1]];
blue2.ab = [[9.6, 10.75, 0.5], [10.75, tEnd, 1]];
red1.ab = [[T_CP, killT, 0.9]];
red2.ab = [[T_CP, killT2, 0.8]];
for (const a of [blue1, blue2, red1, red2]) a.visible = [[0, 999]];

// ---------- deck crew ----------
// Flight-deck crew as simple figures, in carrier-local positions. Their jersey colours follow real carrier practice
// (yellow: catapult officer, the "shooter"; green: catapult and arresting gear; blue: plane handlers; red: ordnance;
// purple: fuel; white: safety). Poses are named; S.poses holds each as numbers so any engine can draw them:
// [crouch 0..1, torso pitch deg, L arm elevation, L arm azimuth, L elbow, R arm elevation, R arm azimuth, R elbow,
//  walk 0..1, R hand circle 0..1]. Arm elevation 0 = hanging down, 90 = level, 180 = straight up; azimuth 0 = forward,
// 90 = out to that side; elbow = bend in degrees. Poses blend over 0.25 s.
S.poses = {
  stand:   [0, 0, 8, 20, 10, 8, 20, 10, 0, 0],
  walk:    [0, 4, 12, 10, 25, 12, 10, 25, 1, 0],
  kneel:   [0.75, 15, 30, 10, 40, 30, 10, 40, 0, 0],
  thumbs:  [0, 0, 8, 20, 10, 75, 10, 110, 0, 0],
  tension: [0, -4, 20, 30, 20, 175, 10, 15, 0, 1],      // run up the engines: right hand up, two fingers circling
  launch:  [0.65, 38, 25, 40, 20, 70, 0, 0, 0, 0],      // the shooter crouches, touches the deck and points to the bow
  point:   [0, 0, 10, 20, 10, 95, 0, 0, 0, 0],
  salute:  [0, 0, 8, 20, 10, 95, 55, 150, 0, 0]
};
const CREW = { yellow: '#f0cf2e', green: '#3c9e52', blue: '#3f6fd6', red: '#cf3a2c', purple: '#7b52c6', white: '#e9e7e2', brown: '#7a5a3a' };
// crew(id, jersey, keys [[t, lx, lz, heading]], poses [[t, name]]): heading is relative to the ship, degrees clockwise
function crew(id, jersey, keys, poses) {
  actor(id, { kind: 'crew', attach: 'carrier', jersey: CREW[jersey], helmet: CREW[jersey], keys: keys.map(k => [k[0], k[1], DECK, k[2], k[3]]), poses });
}
crew('shooter1', 'yellow', [[0, 24, 52, -90]], [[0, 'stand'], [8.9, 'tension'], [9.8, 'launch'], [10.7, 'point'], [11.6, 'stand']]);
crew('shooter2', 'yellow', [[0, -21, 94, 90]], [[0, 'stand'], [9.5, 'tension'], [10.75, 'launch'], [11.6, 'point'], [12.4, 'stand']]);
crew('green1', 'green', [[0, 12, 52, -90], [8.4, 12, 52, -90], [9.2, 17, 44, 160], [12, 17, 44, 160]], [[0, 'kneel'], [8.4, 'walk'], [9.2, 'thumbs'], [10.2, 'stand']]);
crew('green2', 'green', [[0, -12, 52, 90], [8.7, -12, 52, 90], [9.5, -17, 44, 200], [12, -17, 44, 200]], [[0, 'kneel'], [8.7, 'walk'], [9.5, 'thumbs'], [10.6, 'stand']]);
crew('blue1c', 'blue', [[0, 27, 96, 180], [8, 27, 86, 180]], [[0, 'walk']]);
crew('blue2c', 'blue', [[0, 30, 84, 180], [8, 30, 72, 180]], [[0, 'walk']]);
crew('blue3c', 'blue', [[0, 26, 102, -170], [12, 24, 84, -170]], [[0, 'walk']]);
crew('white1', 'white', [[0, 27, -12, -60]], [[0, 'stand'], [9.4, 'salute'], [10.4, 'stand']]);
crew('red1c', 'red', [[0, -31, 34, 70]], [[0, 'stand']]);
crew('purple1', 'purple', [[0, 24, 30, -120]], [[0, 'stand']]);
crew('green3', 'green', [[0, -2, 40, 0]], [[0, 'kneel'], [9.0, 'stand'], [10.5, 'point']]);
// steam drifting off the catapult tracks before the launch (carrier-local segments)
S.emitters = [
  { type: 'steam', attach: 'carrier', a: [8, DECK + 0.1, 58], b: [8, DECK + 0.1, 150], t0: 8.6, t1: 11, rate: 6 },
  { type: 'steam', attach: 'carrier', a: [-8, DECK + 0.1, 58], b: [-8, DECK + 0.1, 150], t0: 8.6, t1: 11, rate: 6 },
  { type: 'steam', attach: 'carrier', a: [-25, DECK + 0.1, -49], b: [-37, DECK + 0.1, 30], t0: 0, t1: 13, rate: 4 },
  { type: 'steam', attach: 'carrier', a: [-8, DECK + 0.1, 66], b: [-8, DECK + 0.1, 96], t0: 8, t1: 11.5, rate: 12 },
  { type: 'steam', attach: 'carrier', a: [8, DECK + 0.1, 50], b: [8, DECK + 0.1, 90], t0: 8, t1: 11, rate: 22 }
];

// ---------- vapour ----------
// Wing vapour from the load factor: humid sea air condenses over the wing above about 4 g. Windows are
// [t0, t1, level]. Vapour cones (the shock collar of a fast, low pass in humid air) are scripted by hand.
function vapourFrom(a) {
  const tr = track(a), out = []; let cur = null;
  const t0 = 9, t1 = 75, h = 0.05;
  for (let t = t0; t <= t1; t += h) {
    if (a.dead != null && t >= a.dead) break;
    const s0 = C.sample(tr, t - h), s1 = C.sample(tr, t), s2 = C.sample(tr, t + h);
    let lv = 0;
    if (s0 && s1 && s2 && s0[0] < s1[0] && s1[0] < s2[0]) {
      const acc = [(s2[1] - 2 * s1[1] + s0[1]) / (h * h), (s2[2] - 2 * s1[2] + s0[2]) / (h * h) + G, (s2[3] - 2 * s1[3] + s0[3]) / (h * h)];
      const sp = Math.hypot(s2[1] - s0[1], s2[2] - s0[2], s2[3] - s0[3]) / (2 * h);
      const n = len(acc) / G;
      if (sp > 120) lv = Math.round(clamp((n - 3.5) / 3, 0, 1) * 4) / 4;
    }
    if (lv > 0 && cur && cur[2] === lv) cur[1] = r2(t + h);
    else if (lv > 0) { cur = [r2(t), r2(t + h), lv]; out.push(cur); }
    else cur = null;
  }
  // drop flickers shorter than a fifth of a second
  return out.filter(w => w[1] - w[0] >= 0.2);
}
for (const a of [blue1, blue2, red1, red2]) a.vapour = vapourFrom(a);
const tSkim = bar(4.1);    // the pair pass the camera plane with vapour cones, high over the sea, on the boom
blue1.cone = [[r2(tSkim - 0.6), r2(tSkim + 0.4), 1], [tMerge - 0.35, tMerge + 0.25, 0.7]];
blue2.cone = [[r2(tSkim - 0.6), r2(tSkim + 0.4), 1],   // side by side: both through the barrier at once
  [tMerge - 0.3, tMerge + 0.2, 0.6]];

// ---------- cameras ----------
// shot(name, t0, t1, fn, desc, opt): fn(t) -> {pos, look} or {pos, yaw, pitch} or {pos, f, u}, plus roll and fov
// (vertical, degrees). opt: lens (mm on a full-frame sensor, 24 mm tall, which sets the fov when fn gives none),
// rig (what the camera is: a word for the shot list), attach {actor, offset [right, up, fwd], dir [right, up, fwd],
// roll 0..1} (the camera is bolted to that actor: an engine that supports it computes the exact camera from the
// actor every frame, and the keys are the same camera baked for engines that do not), hud {actor, target?, guns?}
// (the view is from that cockpit: do not draw that jet; draw its canopy frame and head-up display), vibe {a, f}
// (camera jitter: amplitude as a fraction of the fov, frequency in Hz).
const lensFov = mm => +(2 * Math.atan(12 / mm) / D2R).toFixed(2);
function shot(name, t0, t1, fn, desc, opt = {}) {
  const keys = []; let prev = null;
  // keys run 1.2 s past the cut, so the next shot can dissolve or whip out of this one
  const n = Math.round((t1 - t0 + (t1 < tEnd - 1 ? 1.2 : 0)) / KEY_DT);
  const fov0 = opt.lens ? lensFov(opt.lens) : 42;
  for (let i = 0; i <= n; i++) {
    const t = t0 + i * KEY_DT;
    let c = null;
    // the first key a hair after the cut, so a shot that starts where a jet's new path starts sees the new path
    try { c = fn(i ? t : t + 0.01); } catch (e) { if (t <= t1 + 1e-6) throw e; }
    // the over-run stops where an actor runs out of keys
    if (!c || !c.pos || c.pos.some(v => !isFinite(v))) { if (t <= t1 + 1e-6) throw new Error(name + ': no camera at ' + t); break; }
    let yaw = c.yaw, pitch = c.pitch, roll = c.roll || 0;
    if (c.look) { const d = norm(sub(c.look, c.pos)); yaw = Math.atan2(d[0], d[2]) / D2R; pitch = Math.asin(clamp(d[1], -1, 1)) / D2R; }
    if (c.f) { const f = norm(c.f), u = norm(sub(c.u, mul(f, dot(c.u, f)))); [yaw, pitch, roll] = C.angles(f, u); }
    if (prev) { while (yaw - prev[0] > 180) yaw -= 360; while (yaw - prev[0] < -180) yaw += 360; while (roll - prev[1] > 180) roll -= 360; while (roll - prev[1] < -180) roll += 360; }
    prev = [yaw, roll];
    keys.push([r2(t), r2(c.pos[0]), r2(c.pos[1]), r2(c.pos[2]), r2(yaw), r2(pitch), r2(roll), r2(c.fov || fov0)]);
  }
  FS_CLOSEUP = opt.closeup || null;
  // every jet of the film is in every shot unless the shot says why not (tools/frame_check.js holds it to that)
  const absent = [...(opt.absent || []).map(([id, reason, from, to]) => Object.assign({ id, reason }, from != null ? { from } : {}, to != null ? { to } : {})),
    ...(opt.parkedIn ? [] : ['parked1', 'parked2', 'parked3', 'parked4'].map(id => ({ id, reason: 'parked aft on the deck, not in the action' })))];
  if (opt.subjects) frameSubjects(keys, opt.subjects, t0, t1);
  const s = { name, t0: r2(t0), t1: r2(t1), desc, cam: keys, absent };
  if (opt.subjects) s.subjects = opt.subjects.map(x => (Array.isArray(x) ? [x[0], r2(x[1]), r2(Math.min(x[2], t1))] : x));
  for (const k of ['scene', 'lens', 'rig', 'attach', 'hud', 'vibe', 'in', 'shade', 'closeup', 'partial']) if (opt[k] != null) s[k] = opt[k];
  S.shots.push(s);
}

// A camera operator's framing: the shot's subjects (actor ids, or [id, from, to] for part of the shot) stay inside the
// letterboxed 2.35:1 frame of a 16:9 screen, within FRAME_IN of its half-width and half-height. Where the camera the
// shot asked for would lose one, the aim moves just enough to keep it in, and the lens widens when they do not fit;
// both corrections are smoothed over about a second, so the camera eases instead of twitching. A ship counts by its
// bow and stern, so the whole ship stays in the picture. tools/frame_check.js checks the result.
const FRAME_IN = 0.8, ASPECT = 16 / 9, BOX = 2.35;
const JBOX = { nf24: [-9.2, 9.8, 9.6, -1.6, 3.2], ks55: [-9.2, 9.8, 9.6, -1.6, 3.2], dr7: [-6.2, 6.6, 6.4, -1.1, 2.1], ja46: [-7.8, 8.6, 5.3, -0.95, 3.5] };   // as in tools/frame_check.js
let FS_CLOSEUP = null;   // this jet may run past the edge: only its centre counts
function subjectPoints(id, t) {
  if (id === 'carrier') return [C.shipLocal(CAR, t, [0, 20, 163]), C.shipLocal(CAR, t, [0, 20, -159])];
  const a = S.actors.find(x => x.id === id);
  if (a.kind === 'crew') { const k = at(a, t) || a.keys[a.keys.length - 1]; return [C.shipLocal(CAR, t, [k[1], k[2] + 0.9, k[3]])]; }
  if ((a.spawn != null && t < a.spawn) || (a.end != null && t > a.end)) return [];
  const s = at(a, t);
  if (!s) return [];
  if (id === FS_CLOSEUP) return [[s[1], s[2], s[3]]];
  // the jet's whole bounding box, as tools/frame_check.js has it
  const fr = C.frame(s[4], s[5], s[6]), out = [], [f0, f1, sp, u0, u1] = JBOX[a.model] || JBOX.nf24;
  for (const f of [f0, f1]) for (const r of [-sp, sp]) for (const u of [u0, u1])
    out.push([0, 1, 2].map(i => s[i + 1] + fr.f[i] * f + fr.r[i] * r + fr.u[i] * u));
  return out;
}
function frameSubjects(keys, subjects, t0, t1) {
  const fix = keys.map(k => {
    const t = k[0], fr = C.frame(k[4], k[5], k[6]), th = Math.tan(k[7] * D2R / 2);
    const pts = [];
    for (const x of subjects) {
      const [id, a, b] = Array.isArray(x) ? x : [x, -1e9, 1e9];
      if (t < a || t > b || t > t1 + 0.11) continue;   // (the key just past the cut holds the last frame of the take)
      for (const w of subjectPoints(id, Math.min(t, t1 - 0.01))) {
        const d = sub(w, [k[1], k[2], k[3]]), z = dot(d, fr.f);
        if (z > 0.5) pts.push([dot(d, fr.r) / z, dot(d, fr.u) / z]);
      }
    }
    if (!pts.length) return [0, 0, th];
    const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
    const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys);
    // the lens: wide enough for the subjects' spread, never narrower than asked for, at most 80 degrees
    const thx = (x1 - x0) / 2 / (FRAME_IN * ASPECT), thy = (y1 - y0) / 2 / (FRAME_IN * ASPECT / BOX);
    const tn = Math.min(Math.tan(40 * D2R), Math.max(th, thx * 1.05, thy * 1.05));
    const W = FRAME_IN * tn * ASPECT, H = FRAME_IN * tn * ASPECT / BOX;
    const sx = x1 > W ? x1 - W : x0 < -W ? x0 + W : 0, sy = y1 > H ? y1 - H : y0 < -H ? y0 + H : 0;
    return [sx, sy, tn];
  });
  // ease the corrections: a gaussian over about +-0.6 s; the lens takes the widest nearby value first, so it opens
  // before a subject needs the room rather than after
  const n = fix.length, sm = (i, f) => {
    let s = 0, w = 0;
    for (let j = Math.max(0, i - 8); j <= Math.min(n - 1, i + 8); j++) { const g = Math.exp(-((j - i) ** 2) / 18); s += f(j) * g; w += g; }
    return s / w;
  };
  const wide = fix.map((_, i) => Math.max(...fix.slice(Math.max(0, i - 4), i + 5).map(f => f[2])));
  keys.forEach((k, i) => {
    const sx = sm(i, j => fix[j][0]), sy = sm(i, j => fix[j][1]), tn = Math.max(sm(i, j => wide[j]), wide[i]);
    const fr = C.frame(k[4], k[5], k[6]);
    const f = norm(add(add(fr.f, mul(fr.r, sx)), mul(fr.u, sy)));
    let yaw = Math.atan2(f[0], f[2]) / D2R;
    while (yaw - k[4] > 180) yaw -= 360;
    while (yaw - k[4] < -180) yaw += 360;
    k[4] = r2(yaw);
    k[5] = r2(Math.asin(clamp(f[1], -1, 1)) / D2R);
    k[7] = r2(2 * Math.atan(tn) / D2R);
  });
  // the easing lags where a shot starts: a last touch where it would leave anything too near the edge
  for (const k of keys) {
    const fr = C.frame(k[4], k[5], k[6]), th = Math.tan(k[7] * D2R / 2), W = 0.88 * th * ASPECT, H = W / BOX;
    let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (const x of subjects) {
      const [id, a, b] = Array.isArray(x) ? x : [x, -1e9, 1e9];
      if (k[0] < a || k[0] > b || k[0] > t1 + 0.11) continue;
      for (const w of subjectPoints(id, Math.min(k[0], t1 - 0.01))) {
        const d = sub(w, [k[1], k[2], k[3]]), z = dot(d, fr.f);
        if (z > 0.5) { x0 = Math.min(x0, dot(d, fr.r) / z); x1 = Math.max(x1, dot(d, fr.r) / z); y0 = Math.min(y0, dot(d, fr.u) / z); y1 = Math.max(y1, dot(d, fr.u) / z); }
      }
    }
    if (x0 > x1) continue;
    const sx = x1 > W ? x1 - W : x0 < -W ? x0 + W : 0, sy = y1 > H ? y1 - H : y0 < -H ? y0 + H : 0;
    if (!sx && !sy) continue;
    const f = norm(add(add(fr.f, mul(fr.r, sx)), mul(fr.u, sy)));
    let yaw = Math.atan2(f[0], f[2]) / D2R;
    while (yaw - k[4] > 180) yaw -= 360;
    while (yaw - k[4] < -180) yaw += 360;
    k[4] = r2(yaw); k[5] = r2(Math.asin(clamp(f[1], -1, 1)) / D2R);
  }
}
// a camera bolted to an actor
function rigShot(name, t0, t1, a, offset, dir, desc, opt = {}) {
  const fn = t => {
    const s = state(a, t), o = offset, d = dir;
    const pos = add(add(add(s.pos, mul(s.r, o[0])), mul(s.u, o[1])), mul(s.f, o[2]));
    const f = norm(add(add(mul(s.r, d[0]), mul(s.u, d[1])), mul(s.f, d[2])));
    return { pos, f, u: s.u };
  };
  shot(name, t0, t1, fn, desc, Object.assign({ attach: { actor: a.id, offset, dir, roll: 1 } }, opt));
}
const P = (a, t) => state(a, t).pos;
const carPos = t => C.shipPos(CAR, t);
const half = n => +(HIT + n * BAR).toFixed(3);   // bar positions, also at halves (1.5 = bar one and a half)
// the sun is lowered for the intro so it can sit right behind the carrier's island in a long-lens shot
const SUNV = (() => { const e = 3.3 * D2R, az = 80 * D2R; return [Math.cos(e) * Math.sin(az), Math.sin(e), Math.cos(e) * Math.cos(az)].map(v => +v.toFixed(4)); })();
S.env.sun = SUNV;
const SUNH = norm([SUNV[0], 0, SUNV[2]]);

// The film holds each take longer and lets the camera follow the jets: 11 shots instead of 14 over the same music,
// most of them as long as the manoeuvre they show, with quick hard cuts between them (the brief: "slower between the
// takes, but the cut could go faster"). The framing is wider, and the two cockpit shots carry the game's own HUD and
// instrument panel (`hud.panel`).
// smooth(): a camera operator's eye. An exponential average of fn over the last few tau seconds, looked ahead by tau so
// it does not trail behind: the frame follows a jet smoothly instead of being nailed to it. Times before tmin clamp;
// times where fn has nothing (past the end of a path) are left out.
function smooth(fn, t, tau, tmin) {
  let s = [0, 0, 0], w = 0;
  for (let k = 0; k <= 30; k++) {
    const d = k * tau / 10, g = Math.exp(-d / tau);
    let v = null; try { v = fn(Math.max(tmin, t + tau - d)); } catch (e) { v = null; }
    if (v) { s = add(s, mul(v, g)); w += g; }
  }
  if (!w) throw new Error('smooth: nothing to follow at ' + t);
  return mul(s, 1 / w);
}
// a level frame along a jet's smoothed track: a chase camera keeps its horizon while the jet rolls
function flatFrame(a, t, tau, tmin) {
  const v = smooth(tt => state(a, tt).vel, t, tau, tmin), f = norm([v[0], 0, v[2]]);
  return { f, side: norm(cross([0, 1, 0], f)) };
}
// ---- The film: the camera, 11 shots in three acts. Low, long lenses and the low sun behind the jets. Every shot lists
// every jet that shows in it: frameSubjects keeps each one's whole bounding box in the letterbox, and frame_check holds it to that ----
const DAWN_ABS = [['red1', 'still inbound: its path starts at the cut into the cockpit shot'], ['red2', 'still inbound: its path starts at the cut into the cockpit shot']];
const SHADE = 0.68;   // backlit: the jets a little darker (and the crew black), the grade unchanged
const azDir = az => [Math.sin(az * D2R), 0, Math.cos(az * D2R)];
const bearing = (a, b) => Math.atan2(b[0] - a[0], b[2] - a[2]) / D2R;
const lens = mm => lensFov(mm);
// Act 1. The close-in on the carrier at dawn, in from high off her stern a kilometre out, closing and coming down along
// her port side, then past the bow and turning to look back along the whole ship. Water, carrier and horizon stay in the
// frame all the way: the camera looks no lower than the horizon allows, and the framing keeps bow and stern in.
// The deck take, on the jets taking off, not behind the carrier: high off her bow, looking back
// down the deck at the two alert jets on the bow catapults, the escorts either side of her; the camera sinks and
// closes slowly, the lens creeping in on the jets.
// (the first new cut's dawn) 135 mm, a metre over the deck from off her port side, on the line to the sun,
// so both jets stand against it. A slow push and a slow arc; the sun stays in the top of the frame.
shot('sunrise', 0, 5.2, t => {
  const u = ease(clamp(t / 5.2, 0, 1)), az = lerp(70, 86, u), D = lerp(96, 78, u), d = azDir(az);
  // (above the deck, looking down onto it: the jets and the deck in the frame, not the hull's side)
  const H = DECK + 10, c = deck(t, 0, 56, DECK + 1), pos = [c[0] - d[0] * D, H, c[2] - d[2] * D];
  return { pos, yaw: az, pitch: Math.atan2(DECK + 2.5 - H, D) / D2R, fov: lens(135) };
}, 'Dawn on the deck through a long lens: both alert jets against the low sun in heat haze, a slow push and arc.', { absent: DAWN_ABS, lens: 135, rig: 'long lens at deck height off the port side', shade: SHADE, vibe: { a: 0.002, f: 0.4 }, subjects: ['blue1', 'blue2'] });
const CIC_POSE = { pos: deck(5.5, 2.2, 100, DECK + 0.7), look: deck(5.5, 4, 60, DECK + 2.2), fov: lensFov(18) };
const INBOUND = [['red1', 'still inbound: its path starts at the cut into the cockpit shot'], ['red2', 'still inbound: its path starts at the cut into the cockpit shot']];
const DOWN = ['red1', 'shot down at ' + killT.toFixed(2) + ' s in fight1, and its wreck goes with that take'];
shot('cic', 5.2, 9.0, () => CIC_POSE, 'Inside the carrier\'s control room: the radar operator calls the contacts, the supervisor sounds the alert, the commander sends the alert fighters.', { absent: ['blue1', 'blue2', 'red1', 'red2'].map(id => [id, 'the shot is inside the control room']), scene: 'cic', in: { type: 'dissolve', dur: 0.5 } });
// Catapult, the take-off, one take, a close-up of Viper 2. At its tail as the burners light, Viper 1 behind it against the
// deck; it swings forward with Viper 1 as that goes, past the shooter, and after Viper 2 down the deck, inboard of the
// edge so the hull never shows; off the bow it swings out and draws level, the sun over it, in slow motion.
const zLocal = (z, t) => z - C.shipPos(CAR, t)[2];
const EDGE = C.track([[0, -24, 0, 0], [66, -24, 0, 0], [118, -17, 0, 0], [135, -15, 0, 0], [150, -14, 0, 0], [165, -12, 0, 0], [400, -12, 0, 0]]);   // just inboard of the port edge, by ship z
const T_CAT = 13.7;
shot('catapult', 9.0, T_CAT, t => {
  const v2 = smooth(tt => P(blue2, tt), t, 0.1, 9), v1 = smooth(tt => P(blue1, tt), t, 0.1, 9);
  const back = lerp(25, 6, ease(clamp((t - 13.0) / 0.7, 0, 1))), z2 = zLocal(v2[2], t) - back, hold = lerp(71, 69, ease(clamp((t - 9) / 1.4, 0, 1)));
  const z = 0.5 * (hold + z2 + Math.sqrt((hold - z2) ** 2 + 4));   // a soft max: it waits, then runs, and never backs up
  const lx = lerp(lerp(-24, C.sample(EDGE, clamp(z, 0, 400))[1], ease(clamp((t - 10.2) / 0.8, 0, 1))), -38, ease(clamp((t - 13.0) / 0.7, 0, 1)));
  // (up a little as it runs: its eye above the deck furniture, which then stays against the dark deck)
  const y = lerp(DECK + lerp(1.2, 3, ease(clamp((t - 10.9) / 0.8, 0, 1))), v2[1] - 3, ease(clamp((t - 12.9) / 0.8, 0, 1)));
  const pos = C.shipLocal(CAR, t, [lx, y, z]), j = ease(clamp((t - 13.0) / 0.7, 0, 1)), aim = lerp3(v2, v1, lerp(0.5, 0.2, j));
  const w = ease(clamp((t - 10.0) / 1.1, 0, 1));
  return { pos, yaw: lerp(97, bearing(pos, aim), w), pitch: Math.max(1.5, Math.asin(clamp((aim[1] + 1.5 - pos[1]) / dist(pos, aim), -1, 1)) / D2R), fov: lens(24) };
}, 'One take on the catapults: at Viper 2\'s nozzle as the burners light, Viper 1 going, the shooter black against the sky, then after Viper 2 down the deck, off the bow in slow motion and level with it, the sun over it.', { lens: 24, absent: INBOUND, closeup: 'blue2', rig: 'close on Viper 2 at deck level, then along the edge, then alongside', shade: SHADE, vibe: { a: 0.003, f: 1.3 }, in: { type: 'dissolve', dur: 0.3 },
  subjects: ['blue1', 'blue2'] });
// Deck, one take before the catapult take (as in the first film): low on the deck behind the crew, the shooter in the
// middle of the frame through a long lens as he gives his run-up and launch signals in the steam, then the operator
// swings onto Viper 1 as the catapult fires and it roars past, widening out. The catapult take follows for Viper 2.
const toLocal = (t, w) => { const h = CAR.heading * D2R, c = Math.cos(h), n = Math.sin(h), o = C.shipPos(CAR, t), d = [w[0] - o[0], 0, w[2] - o[2]]; return [d[0] * c - d[2] * n, w[1], d[0] * n + d[2] * c]; };
const SH1 = [24, 52], T_DECK1 = 9.95;   // (to the launch signal: as his hand comes down, the catapult take)
shot('deck', 9.0, T_DECK1, t => {
  // up the deck from the shooter, low: he stands in the middle of the frame with the jet in the steam beside him, and the camera swings onto the jet as the catapult fires
  const B = toLocal(t, smooth(tt => P(blue1, Math.min(tt, 10.0)), Math.min(t, 10.0), 0.1, 8.5)), S = SH1;
  const dx = S[0] - B[0], dz = S[1] - B[2], dl = Math.hypot(dx, dz) || 1, ux = dx / dl, uz = dz / dl;
  const w = ease(clamp((t - 9.0) / 2, 0, 1));
  void ux; void uz;   // (up the deck from him, inboard of the edge: from the side the camera stood off the ship)
  const pos = deck(t, S[0] - 5, S[1] + lerp(27, 25, w), DECK + 1.5);
  const look1 = deck(t, lerp(S[0], B[0], 0.2), lerp(S[1], B[2], 0.2), DECK + 1.6);
  const j = ease(clamp((t - 10.05) / 0.9, 0, 1)), jet = smooth(tt => add(P(blue1, tt), [0, 1.2, 0]), t, 0.12, 5.5);
  const fov = lerp(lensFov(70), lensFov(26), ease(clamp((t - 10.0) / 1.1, 0, 1)));
  return { pos, look: lerp3(look1, jet, j), fov };
}, 'One take on the flight deck: up the deck from the shooter, through a long lens in the steam as he gives his run-up and launch signals; as his hand comes down, the catapult take.', { absent: [...INBOUND, ['blue2', 'on the other catapult, behind the camera\'s shoulder: it shows in the catapult take that follows'], ['blue1', 'in the steam beyond the shooter, the long lens on him until the swing', -1e9, 10.7], ['blue1', 'roars past the lens off the catapult', 10.75], ...['parked1', 'parked2', 'parked3', 'parked4'].map(id => [id, 'aft, left behind as the camera swings with Viper 1', 10.6])], subjects: [['blue1', 10.7, 99]], parkedIn: true, partial: ['blue1', 'blue2', 'parked1', 'parked2', 'parked3', 'parked4'], lens: 70, rig: 'dolly on deck, panning with the launch', vibe: { a: 0.004, f: 1.2 }, in: { type: 'dissolve', dur: 0.3 } });
// (the first camera drone over the climb, then riding low beside the pair against
// the sun through the boom, as in the first new cut)
// Drone, 16 mm: it starts 45 degrees up and ahead of the pair as they leave the bow, looking back at them with the
// carrier behind, then flies on ahead of the leader along its climb, 15 degrees above its path, so we look at the
// noses as they roll and accelerate up toward the lens. It lets them close from 42 m to 22 m.
const T_RIDE = 18.0;
const leadMid = t => lerp3(P(blue1, t), P(blue2, t), 0.3);
shot('drone', T_CAT, T_RIDE, t => {
  const u = ease(clamp((t - T_CAT) / (T_RIDE - T_CAT), 0, 1)), w = ease(clamp((t - T_CAT) / 1.8, 0, 1));
  const m = smooth(leadMid, t, 0.3, T_CAT), v = norm(smooth(tt => state(blue1, tt).vel, t, 0.5, T_CAT));
  const D = lerp(42, 22, u), side = norm(cross([0, 1, 0], [v[0], 0, v[2]]));
  const start = add(m, [0, 0.71 * D, 0.71 * D]);
  const lead = add(add(m, mul(v, D * Math.cos(15 * D2R))), add(mul([0, 1, 0], D * Math.sin(15 * D2R)), mul(side, 6)));
  return { pos: lerp3(start, lead, w), look: add(m, mul(v, 4)), fov: lensFov(20) };
}, 'A camera drone above and ahead of the pair as they leave the bow, wide, leading the climb: looking back at the noses with the carrier behind them, the victory roll, and both accelerating up toward the lens.', { absent: INBOUND, subjects: ['blue1', 'blue2'], lens: 16, rig: 'camera drone leading the pair', vibe: { a: 0.004, f: 1.5 } });
// Ride, 20 mm: a camera jet low beside Viper 2 on the sun side's far side, looking across the pair into the sun and
// the water; they go through the barrier beside it, vapour cones on the ba-boom.
shot('ride', T_RIDE, T_CP, t => {
  const own = smooth(tt => P(blue2, tt), t, 0.25, T_RIDE), fr = flatFrame(blue2, t, 0.5, T_RIDE), u = ease(clamp((t - T_RIDE) / 3, 0, 1));
  const pos = add(add(add(own, mul(fr.side, lerp(-34, -28, u))), mul(fr.f, lerp(12, -6, u))), [0, lerp(-11, -8, u), 0]);
  const m = lerp3(P(blue1, t), P(blue2, t), 0.5), d = norm(sub(m, pos));
  return { pos, yaw: Math.atan2(d[0], d[2]) / D2R, pitch: Math.asin(d[1]) / D2R + 2, fov: lens(20) };
}, 'Riding low beside the pair, looking across them into the sun and the water: Viper 1 and the Gladan side by side, vapour cones and the ba-boom.', { absent: INBOUND, lens: 20, rig: 'camera jet close on the wing', shade: SHADE, vibe: { a: 0.003, f: 4 }, in: { type: 'dissolve', dur: 0.4 }, subjects: ['blue1', 'blue2'] });
const T_LOCK = bar(21);
// Act 2. Viper 1's cockpit: the bandits are on the scope and in the HUD's brackets long before they can be seen; by the
// end of the take they are in the windscreen.
rigShot('cockpit', T_CP, T_MERGE, blue1, [0, 1.75, 2.6], [0, -0.06, 1], 'In Viper 1\'s cockpit: two contacts on the scope and in the head-up display\'s brackets, then the KS-55 and the drone in the windscreen, growing from dots until they fill it as they pass.', { absent: [['blue1', 'the camera is in its cockpit: the game does not draw that jet'], ['blue2', 'on Viper 1\'s beam, behind the cockpit\'s field of view']], lens: 21, rig: 'cockpit camera', hud: { actor: 'blue1', target: 'red1', panel: true }, vibe: { a: 0.003, f: 14 } });
// Merge, from outside: a very long lens high on the line, on the bandits' side: the Vipers come at it, the bandits go away from
// it, all four in the frame from the cut to the end. They cross with vapour cones on the music's crash and break.
const MP = [4500, 548, 2450];   // (300 m west, off the line of the airfield's runway, which showed under the merge through the long lens)
shot('merge', T_MERGE, tS7, t => {
  const c = [blue1, blue2, red1, red2].map(a => P(a, Math.min(t, 31.1))).reduce((m, p) => add(m, mul(p, 0.25)), [0, 0, 0]);
  return { pos: MP, look: c, fov: lens(500) };
}, 'From outside, a very long lens high on the line of the merge: the Vipers coming on, the KS-55 and the drone going away, stacked by the lens; they cross with vapour cones and all four break.', { lens: 500, rig: 'very long lens, panning', vibe: { a: 0.001, f: 1 }, subjects: ['blue1', 'blue2', 'red1', 'red2'] });
// Viper 1 against the KS-55, one long take down the line of the fight through a long lens, low behind Viper 1: the missile
// goes out ahead of it, the flares fan out in slow motion, the fight turns, the second missile, and the KS-55 goes up.
shot('fight1', tS7, tS9, t => {
  const Q = q => Math.min(q, 38.78), tt0 = Math.min(t, 38.75), own = smooth(q => P(blue1, Q(q)), tt0, 0.3, tS7), u = ease(clamp((t - 33) / 3.5, 0, 1));
  const off = ease(clamp((t - 36.9) / 1.6, 0, 1)), d = azDir(lerp(70, 82, u)), side = [d[2], 0, -d[0]];
  // looking into the sun all the way, dropping back as the fight opens out across the frame; after the kill it slides
  // out and up, clear of the smoke the Vipers fly through
  const pos = add(add(add(own, mul(d, -lerp(150, 250, u))), mul(side, -lerp(40, 120, off))), [0, lerp(12, 30, off), 0]);
  return { pos, yaw: lerp(70, 82, u) + 4, pitch: -1, fov: lens(90) };
}, 'Into the sun through a long lens, level behind Viper 1 with Viper 2 on its wing: fox two, the flares fan out against the sun in slow motion and take the missile. The fight turns into the sun, the second missile, and the KS-55 goes up against it in slow motion, shards and smoke.', { absent: [['red2', 'away from this fight: no path until it opens fight2 at 38.8 s']], lens: 90, rig: 'long lens level in trail, to one side', shade: SHADE, vibe: { a: 0.002, f: 2 }, subjects: ['blue1', 'blue2', 'red1'] });
// Viper 2 and the drone: a slow half circle round the fight, closing as the two cross, the sun behind them at the crossing.
shot('fight2', tS9, tS11, t => {
  const mid = smooth(tt => lerp3(P(blue2, tt), P(red2, tt), 0.5), t, 0.6, tS9);
  const sep = smooth(tt => [dist(P(blue2, tt), P(red2, tt)), 0, 0], t, 0.9, tS9)[0];
  const u = clamp((t - tS9) / (tS11 - tS9), 0, 1), th = lerp(358, 178, 0.5 * u + 0.5 * ease(u)), D = clamp(sep * 0.8, 170, 380);
  const d = azDir(th);
  return { pos: add(mid, [d[0] * D, -0.22 * D, d[2] * D]), look: mid, roll: 7 * Math.sin(Math.PI * u), fov: lens(38) };
}, 'A slow half circle round the fight, low: the drone fires at Viper 2, flares and a hard break; the two cross against the sun in a flat scissors, wingtip trails, until Viper 2 is on the guns.', { absent: [DOWN], lens: 38, rig: 'camera jet orbiting the fight', shade: 0.85, vibe: { a: 0.002, f: 1.5 }, subjects: ['blue1', 'blue2', 'red2'] });
// Act 3. Pursuit, from ahead through a long lens, the sun behind the camera: the drone coming on, the two Vipers behind
// it, stacked by the lens, closing on the lens as it backs away more slowly than they run.
shot('pursuit', tS11, T_LOCK, t => {
  // (the first take, over Viper 2's shoulder, 35 mm)
  const fr = flatFrame(blue2, t, 0.6, tS11), drone = smooth(tt => P(red2, tt), t, 0.4, tS11);
  const back = norm(smooth(tt => state(blue2, tt).vel, t, 0.6, tS11)), own = smooth(tt => P(blue2, tt), t, 0.3, tS11);
  return { pos: add(add(add(own, mul(back, -55)), mul(fr.side, 38)), [0, 6, 0]), look: lerp3(own, drone, 0.5), fov: lensFov(35) };
}, 'Over the Gladan\'s shoulder: the drone runs for the sunrise, weaving; Viper 1 with them.', { absent: [DOWN], closeup: 'blue1', subjects: ['blue2', 'red2'], lens: 35, rig: 'camera jet in trail', vibe: { a: 0.004, f: 4 } });
rigShot('lock', T_LOCK, tFinale, blue2, [0, 1.75, 2.6], [0, -0.06, 1], 'In Viper 2\'s cockpit, the sun through the canopy: Viper 1 ahead in the windscreen, the drone beyond it running for the sunrise, jinking; the lock closes and Viper 2 fires.', { absent: [DOWN, ['blue2', 'the camera is in its cockpit: the game does not draw that jet']], lens: 21, rig: 'cockpit camera', hud: { actor: 'blue2', target: 'red2', panel: true, lock: [r2(L2 - 2.2), r2(L2 - 0.5)] }, vibe: { a: 0.003, f: 14 } });
// 11 Finale, 40 mm: behind Viper 2 into the sun; the hit on bar 13, the Vipers split, and the fire and smoke
// drift together into the title, a cloud in the dawn sky. The camera glides to a stop and holds under the menu.
const splash2End = () => S.events.find(e => e.type === 'splash' && e.actor === 'red2').pos;
const splash2Pre = () => S.events.find(e => e.type === 'splash' && e.actor === 'red2').t;
// (in close behind the Gladan, as in the finale, off the line to the sun)
const FIN_BACK = 150, FIN_UP = -30, FIN_AIM = 0.02;   // a little below them, the shot into the bright sky
const finAt = t => add(add(smooth(tt => P(blue2, tt), t, 0.25, tFinale - 1), mul(azDir(55), -FIN_BACK)), [0, FIN_UP, 0]);
const FIN = { p: finAt(killT2 - 0.05), v: mul(state(blue2, killT2 - 0.05).vel, 0.4) };
const FIN_ABS = [DOWN, ['red2', 'the wreck has gone into the sea', r2(splash2Pre())], ...['blue1', 'blue2'].map(id => [id, 'broken away out of the picture, under the menu', r2(killT2 + 25.6)])];
shot('finale', tFinale, tEnd, t => {
  let pos;
  if (t <= killT2) pos = finAt(Math.min(t, killT2 - 0.05));
  else { const tau = t - killT2; pos = add(FIN.p, mul(FIN.v, (1 - Math.exp(-1.4 * tau)) / 1.4)); }
  const d = dist(pos, killPos2), aim = add(killPos2, [0, FIN_AIM * d, 0]);
  const look = lerp3(P(red2, Math.min(t, killT2 - 0.01)) || aim, aim, ease(clamp((t - (killT2 - 1.0)) / 1.0, 0, 1)));
  return { pos, look, fov: lerp(lensFov(60), lensFov(40), ease(clamp((t - killT2 - 0.2) / 2.4, 0, 1))) };
}, 'Behind the Gladan into the sun: Viper 2 (the Gladan) hits the drone on bar 13, dives a little and breaks away to the right with Viper 1 on its wing, and the fire and smoke drift together into ' + GAME.logo + ', a cloud in the dawn sky, which the camera holds as it thins away.', { absent: FIN_ABS, subjects: ['blue1', 'blue2', 'red2'], lens: 40, rig: 'camera jet in trail, then hovering on the title' });

// Slow motion: the film clock (the music) to story time. The rate eases down and up, and the time lost is made up by
// easing a little faster, before the slow part or after it, never by a jump: the two clocks are the same again before
// every sync point (checked below), and everywhere else.
function buildWarp() {
  const bump = (m, a, b, r) => ease(clamp((m - a) / r, 0, 1)) * ease(clamp((b - m) / r, 0, 1));
  const WIN = [
    { slow: [12.15, 13.25, 0.45], fast: [13.3, 15.75] },   // off the bow, made up through the overpass
    { slow: [31.95, 33.05, 0.38], fast: [33.1, 35.3] },    // the flares
    { slow: [killT + 0.03, killT + 0.81, 0.45], fast: [killT + 0.81, 38.75] },   // splash one
    { fast: [55.2, 57.3], slow: [57.42, 58.42, 0.55] },    // into the last kill: the flares and the missile
    { slow: [58.46, 59.7, 0.42], fast: [59.7, 61.6] }      // the last kill
  ];
  const h = 0.005, integ = (a, b, f) => { let v = 0; for (let m = a; m < b; m += h) v += f(m) * h; return v; };
  for (const w of WIN) {
    w.f = m => bump(m, w.slow[0], w.slow[1], 0.3);
    w.g = m => bump(m, w.fast[0], w.fast[1], 0.5);
    w.k = (1 - w.slow[2]) * integ(w.slow[0], w.slow[1], w.f) / integ(w.fast[0], w.fast[1], w.g);
  }
  const keys = []; let s = 0, n = 0;
  for (let m = 12.1; m < 61.7; m += h, n++) {
    let d = 0;
    for (const w of WIN) { if (m >= w.slow[0] && m <= w.slow[1]) d -= (1 - w.slow[2]) * w.f(m); if (m >= w.fast[0] && m <= w.fast[1]) d += w.k * w.g(m); }
    if (n % 16 === 0 && (Math.abs(s) > 1e-3 || d !== 0 || !keys.length || keys[keys.length - 1][0] !== keys[keys.length - 1][1])) keys.push([r3(m), r3(m + s)]);
    s += d * h;
    if (!WIN.some(w => m >= Math.min(w.slow[0], w.fast[0]) && m <= Math.max(w.slow[1], w.fast[1]))) s = 0;
  }
  keys.push([61.7, 61.7]);
  const clock = t => { if (t < keys[0][0]) return t; for (let i = 1; i < keys.length; i++) if (t < keys[i][0]) return keys[i - 1][1] + (keys[i][1] - keys[i - 1][1]) * (t - keys[i - 1][0]) / (keys[i][0] - keys[i - 1][0]); return t; };
  for (const c of [HIT, bar(2), 19.5, tMerge, tS7, 31.342, 35.442, killT, tS9, tS11, killT2, tForm1, bar(26), tGone1])
    if (Math.abs(clock(c) - c) > 0.012) throw new Error('slow motion is off the music at ' + c + ': ' + clock(c));
  let top = 0; for (let i = 1; i < keys.length; i++) { const r = (keys[i][1] - keys[i - 1][1]) / (keys[i][0] - keys[i - 1][0]); top = Math.max(top, r); if (r < 0.2) throw new Error('warp stalls at ' + keys[i][0]); }
  const inv = v => { for (let i = 1; i < keys.length; i++) if (v < keys[i][1]) return keys[i - 1][0] + (keys[i][0] - keys[i - 1][0]) * (v - keys[i - 1][1]) / (keys[i][1] - keys[i - 1][1]); return v; };
  console.error('peaks ' + WIN.map(w => (1 + w.k).toFixed(2)).join(' ') + '; fastest ' + top.toFixed(2) + 'x; takes on the film clock: ' + S.shots.map(x => x.name + ' ' + (Math.min(inv(x.t1), 66.43) - inv(x.t0)).toFixed(2)).join(', '));
  return keys;
}
const r3 = v => Math.round(v * 1000) / 1000;

// ---------- cards, effects, sounds ----------
// the title forms, holds a moment, then vaporizes into thin air (3.5 s), gone as the wreck splashes into the sea
const splash2 = S.events.find(e => e.type === 'splash' && e.actor === 'red2');
const tForm0 = r2(killT2 + 0.35), tForm1 = r2(killT2 + 3.3), tGone1 = r2(splash2.t), tGone0 = r2(tGone1 - 1.25);
S.cards = [
  { t0: 1.4, t1: 5.1, style: 'location', text: 'SOMEWHERE IN THE PACIFIC', type: 16 },
  { t0: 3.6, t1: 5.1, style: 'sub', text: '05:47 LOCAL  -  CVN-90 STORMHAVEN', type: 28 },
  // the alert as it is spoken in the control room, in the lower bar: the radar operator, the supervisor, the commander
  { t0: 5.6, t1: 7.0, style: 'dialog', speaker: 'RADAR', text: 'BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.', type: 40 },
  { t0: 7.05, t1: 8.05, style: 'dialog', speaker: 'SUPERVISOR', text: 'ALERT, ALERT! WE HAVE INBOUND.', type: 40 },
  { t0: 8.1, t1: 9.2, style: 'dialog', speaker: 'COMMANDER', text: 'SEND THE BIRDS.', type: 22 },
  { t0: +(half(1) - 0.2).toFixed(2), t1: half(1) + 1.6, style: 'callsign', text: 'VIPER 1  -  NF-24 CORMORANT' },
  { t0: 15.6, t1: 17.2, style: 'callsign', text: 'VIPER 2  -  JA 46 GLADAN' },
  { t0: T_CP + 0.5, t1: T_CP + 2.4, style: 'radio', text: 'VIPER 1, TWO CONTACTS ON THE SCOPE!' },
  { t0: T_TALLY - 0.9, t1: T_TALLY + 0.5, style: 'radio', text: 'VIPER 1, TALLY TWO!' },
  { t0: tMerge, t1: tMerge + 1.4, style: 'callsign', text: 'MERGED  -  KS-55 GRIFON  +  DR-7 VESPER DRONE' },
  { t0: tS7 + 0.2, t1: tS7 + 1.3, style: 'radio', text: 'VIPER 1, FOX TWO!' },
  { t0: tS7 + 4.3, t1: tS7 + 5.3, style: 'radio', text: 'VIPER 1, FOX TWO!' },
  { t0: +(killT + 0.15).toFixed(2), t1: +(killT + 1.6).toFixed(2), style: 'radio', text: 'SPLASH ONE!' },
  { t0: tS9 + 0.3, t1: tS9 + 1.6, style: 'radio', text: 'VIPER 2, DEFENDING, DRONE ON MY SIX!' },
  { t0: tS9 + 5.2, t1: tS9 + 6.2, style: 'radio', text: 'GUNS, GUNS!' },
  { t0: tS11 + 1.0, t1: tS11 + 2.6, style: 'radio', text: 'VIPER 2, TALLY THE DRONE, PRESSING!' },
  { t0: +(L2 + 0.05).toFixed(2), t1: +(L2 + 1.2).toFixed(2), style: 'radio', text: 'VIPER 2, FOX TWO!' },
  { t0: r2(killT2 + 0.5), t1: r2(killT2 + 2.2), style: 'radio', text: 'SPLASH TWO.' },
  { t0: r2(tForm1 - 0.3), t1: r2(tGone0 + 1.5), style: 'subtitle', text: GAME.subtitle },
  { t0: r2(tGone1 - 0.5), t1: 1e9, style: 'press', text: 'PRESS ANY KEY', blink: 0.6 }
];
S.fx = [
  { t0: 0, t1: 2.6, type: 'fade', from: 1, to: 0, color: '#000' },
  { t0: 0, t1: 1e9, type: 'grade', warm: 0.1, grain: 0.02, vignette: 0.32 },
  { t0: 0, t1: r2(tGone1 - 1.0), type: 'letterbox', amount: 1 },
  // (no bars coming back after the title, and no reframing as they open: the film ends full frame)
  { t0: 0, t1: 5.5, type: 'haze', y0: 0.5, y1: 1, amount: 1.4 },
  { t0: 9.0, t1: 13.0, type: 'haze', y0: 0.66, y1: 1, amount: 0.7 },
  { t0: 13.7, t1: 15.0, type: 'haze', y0: 0.5, y1: 1, amount: 1.2 },
  { t0: 0, t1: 66.43, type: 'glow', amount: 0.75 },
  { t0: 9.0, t1: 10.0, type: 'alarm', period: 0.9 },
  { t0: 9.6, t1: 10.0, type: 'shake', amount: 0.35 },
  { t0: 10.1, t1: 11.2, type: 'shake', amount: 0.5 },
  { t0: 15.35, t1: 15.75, type: 'shake', amount: 0.5 },
  { t0: r2(killT), t1: r2(killT + 0.5), type: 'flash', color: '#fff2dc', from: 0.22, to: 0 },
  { t0: r2(tSkim - 0.2), t1: r2(tSkim + 0.2), type: 'shake', amount: 1 },
  { t0: tMerge - 0.05, t1: tMerge + 0.45, type: 'shake', amount: 0.8 },
  { t0: tS7 + 0.2, t1: tS7 + 0.5, type: 'shake', amount: 0.5 },
  { t0: r2(killT2), t1: r2(killT2 + 0.5), type: 'flash', color: '#fff2dc', from: 0.45, to: 0 },
  { t0: r2(killT2), t1: r2(killT2 + 0.6), type: 'shake', amount: 0.7 },
  // the fire and smoke of the last kill become the title: a cloud in the sky
  { t0: tForm0, t1: 1e9, type: 'smoketitle', text: GAME.logo, pos: killPos2, form: [tForm0, tForm1], dissolve: [tGone0, tGone1], seed: 2026 }
];
S.events.push({ t: 8.3, type: 'sound', name: 'klaxon', repeat: 2, every: 0.9 });
S.events.push({ t: 10.0, type: 'sound', name: 'catapult' }, { t: 10.95, type: 'sound', name: 'catapult' });
S.events.push({ t: 11.6, type: 'sound', name: 'flyby' }, { t: 15.1, type: 'sound', name: 'flyby' }, { t: r2(tSkim - 0.3), type: 'sound', name: 'flyby' }, { t: tMerge - 0.15, type: 'sound', name: 'flyby' });
S.events.push({ t: r2(tForm0 + 0.3), type: 'sound', name: 'cloud' });
// sonic booms as the jets go through the barrier low over the sea: the skim pass, then the merge, a little further off
// the two jets abreast each make their own boom (each one a "ba-boom", the bow and tail shock of its N-wave); side
// by side, the wingman's lands a hair after the leader's, so the pair is heard as one heavy ba-boom
S.events.push({ t: 19.5, type: 'sound', name: 'boom', vol: 1 }, { t: 19.54, type: 'sound', name: 'boom', vol: 0.85 },
  { t: 29.0, type: 'sound', name: 'boom', vol: 0.7 });
S.events.sort((a, b) => (a.t ?? a.t0) - (b.t ?? b.t0));
S.duration = r2(tGone1 + 0.5);
S.menu_at = r2(tGone1 + 0.5);
S.music = { module: 'recording', track: 'fullburn-intro', name: 'Full Burn, Arp sound DMX10 (film cut)', start_offset: 0,
  cues: { band_hit: HIT, bar: +BAR.toFixed(5), riff: bar(2), verse: bar(10), pre: bar(18), chorus: bar(26), merge: tMerge, dogfight: tS7, last_kill: r2(killT2), title_formed: tForm1, title_gone: tGone1 },
  note: 'Times are seconds from the first music sample. After the band hit, bar n starts at band_hit + n * bar.' };
// Jets out of the story are not there, so nothing passes through the back of another take; a Viper is the finale's camera.
const hide = (a, cut, t1 = 1e9) => { const t0 = r2(cut), last = C.sample(track(a), t0 - 0.002), i = a.keys.findIndex(k => k && k[0] > t0 - 0.002); if (i < 0) return; const j = a.keys.findIndex((k, q) => q >= i && k && k[0] >= r2(t1)); a.keys = [...a.keys.slice(0, i), last.map((v, q) => +v.toFixed(q ? 2 : 3)), ...(j < 0 ? [] : [null, ...a.keys.slice(j)])]; a._track = null; };   // (a key right up to the cut: the cockpit before the finale rides on it)
// The drone has no path through the first fight; the KS-55's wreck goes with its take; the bandits are there to the end of the merge.
hide(red2, tS7, tS9); hide(red1, tS9);
S.events = S.events.filter(e => !(e.type === 'splash' && e.actor === 'red1'));
S.warp = buildWarp();
S.env = Object.assign({ map: 'twin-isles', time_of_day: 'dawn', weather: 'scattered', assets: 'intro_assets.json' }, S.env);

// ---------- the longer launch ----------
// The film's cut of the score plays the opening's build 8 bars longer (tools/fullburn_score.py INTRO_CUT): the
// arpeggios of bars 3-4 twice more, and bars 5-6 three times before the band hit. The picture gets two new takes
// from it, and everything else keeps its place against the music:
//   `fleet` (0 - OPEN) opens the film: the carrier group at dawn through a long lens from far off, the ship black
//   against the sunrise with the sun just over her, before the close-in;
//   `deck` (DECK_T - DECK_T + DECK_D) after the control room: one crane move on Viper 1 on its catapult, from the
//   launch bar at deck level up past the cockpit and out to the shooter's run-up signal, the jet blast rolling
//   steam under it, before the catapult take.
// Each is made by moving the story after its start that much later (insertTime): times, keys, windows and cues. The
// carrier group starts that much further back, so the ship is where it was at every moment after the insert, and
// the jets parked on her deck before it go back with her: the world after each insert is the one before, later.
const OPEN = r3(4 * BAR), DECK_T = 5.2, DECK_D = r3(4 * BAR);   // the deck take before the control room   // 7.661 s each: four bars
const SONG_SHIFT = +(8 * BAR).toFixed(4);   // the score: 8 bars (15.3218 s); the picture is a millisecond off it, well inside a frame
function insertTime(P, D, keepAt) {
  const sh = t => (t >= P - 1e-9 && t < 1e8 ? r3(t + D) : t);   // 'forever' ends stay forever
  const sh1 = (t0, t1) => (t1 <= P + 1e-9 || t1 >= 1e8 ? t1 : t0 < P - 1e-9 && t1 - P < 1.0 ? t1 : r3(t1 + D));   // an end just past the insert stays
  const win = w => { const a = w[0]; w[0] = sh(w[0]); w[1] = sh1(a, w[1]); };
  const keep = o => keepAt && keepAt(o);
  for (const s of [CAR, ...S.actors.filter(a => a.kind === 'ship' && a.ship !== CAR).map(a => a.ship)]) {
    const h = s.heading * D2R; s.x = r2(s.x - Math.sin(h) * s.speed * D); s.z = r2(s.z - Math.cos(h) * s.speed * D);
  }
  const back = [-Math.sin(CAR.heading * D2R) * CAR.speed * D, 0, -Math.cos(CAR.heading * D2R) * CAR.speed * D];
  const moveKeys = (keys, world) => {
    let last = null;
    const out = [];
    for (const k of keys) {
      if (!k) { out.push(null); last = null; continue; }
      if (k[0] < P - 1e-9) { if (world) { k[1] = r2(k[1] + back[0]); k[3] = r2(k[3] + back[2]); } last = k; out.push(k); continue; }
      const k2 = k.slice(); k2[0] = sh(k[0]);
      if (last && last[0] < P - 1e-9) {   // across the insert: the jet rides the deck the whole time, so fill in
        const n = Math.ceil((k2[0] - last[0]) / 0.5);
        for (let i = 1; i < n; i++) { const u = i / n; out.push(last.map((v, c) => (c ? r2(v + (k2[c] - v) * u) : r3(last[0] + (k2[0] - last[0]) * u)))); }
      }
      out.push(k2); last = k2;
    }
    return out;
  };
  for (const a of S.actors) {
    const world = a.kind === 'jet' || a.kind === 'missile';
    if (a.keys) a.keys = moveKeys(a.keys, world);
    for (const f of ['ab', 'cone', 'vapour', 'visible']) if (a[f]) a[f].forEach(win);
    if (Array.isArray(a.trail)) a.trail.forEach(win);
    if (a.poses) a.poses = a.poses.map(([t, n]) => [sh(t), n]);
    for (const f of ['spawn', 'end', 'dead']) if (a[f] != null) a[f] = sh(a[f]);
    a._track = null;
  }
  for (const f of S.flares) f.t = sh(f.t);
  for (const e of S.events) { if (e.t != null) e.t = sh(e.t); if (e.t0 != null) { const a = e.t0; e.t0 = sh(a); e.t1 = sh1(a, e.t1); } }
  for (const s of S.shots) {
    const a = s.t0; s.t0 = sh(a); s.t1 = sh1(a, s.t1);
    s.cam = moveKeys(s.cam, true);
    for (const x of s.absent || []) { if (x.from != null) x.from = sh(x.from); if (x.to != null) x.to = sh(x.to); }
    if (s.subjects) s.subjects = s.subjects.map(x => (Array.isArray(x) ? [x[0], sh(x[1]), sh(x[2])] : x));
    if (s.hud && s.hud.lock) s.hud.lock = s.hud.lock.map(sh);
  }
  for (const list of [S.cards, S.fx, S.emitters]) for (const c of list) {
    if (keep(c)) continue;
    // steam that starts just before the insert belongs to the shot after it: move it whole
    if (list === S.emitters && c.t0 < P - 1e-9 && c.t0 > P - 1.5 && c.t1 > P) { c.t0 = r3(c.t0 + D); c.t1 = r3(c.t1 + D); continue; }
    const a = c.t0; c.t0 = sh(a); c.t1 = sh1(a, c.t1);
    for (const f of ['form', 'dissolve']) if (c[f]) c[f] = c[f].map(sh);
  }
  S.warp = S.warp.map(([m, s]) => [sh(m), sh(s)]);
  for (const k in S.music.cues) if (k !== 'bar') S.music.cues[k] = sh(S.music.cues[k]);
  S.duration = sh(S.duration); S.menu_at = sh(S.menu_at);
}
// the deck first (it is later in the film), then the opening: the alarm and its shake stay at the start of the deck
// take; the film's fade-in, the grade, the glow, the letterbox and the location cards stay at the very start
insertTime(DECK_T, DECK_D);
insertTime(0, OPEN, o => o.t0 === 0 && (o.type === 'fade' || o.type === 'grade' || o.type === 'glow' || o.type === 'letterbox') || o.style === 'location' || o.style === 'sub');
// before the shifted start the jets sit on the deck as they did at its first key, and ride along with her
for (const a of [blue1, blue2]) {
  const k0 = a.keys[0], v = [Math.sin(CAR.heading * D2R) * CAR.speed, 0, Math.cos(CAR.heading * D2R) * CAR.speed], pre = [];
  for (let t = 0; t < k0[0] - 1e-6; t += 0.5) pre.push([r3(t), r2(k0[1] - v[0] * (k0[0] - t)), k0[2], r2(k0[3] - v[2] * (k0[0] - t)), ...k0.slice(4)]);
  a.keys = [...pre, ...a.keys]; a._track = null;
}
const DT1 = DECK_T + OPEN;   // where the deck take starts now
// the location cards over the fleet, the haze of the sunrise there too
for (const c of S.cards) if (c.style === 'location' || c.style === 'sub') { c.t0 = r2(c.t0 + 0.6); c.t1 = r2(OPEN - 0.25); }
S.fx.push({ t0: 0, t1: OPEN + 0.5, type: 'haze', y0: 0.45, y1: 1, amount: 1.6 });
// The opening is the first flyby at dawn, slower: in from high off the carrier's
// stern a kilometre out, closing and coming down along her port side toward the bow, the whole ship, the sea and the
// horizon in the frame; it dissolves into the long lens on the deck.
{
  const K = OPEN / 5.2;   // the first track, stretched over the longer opening
  const FLY0 = C.track([[0, -260, 230, -1050], [1.8 * K, -320, 170, -560], [3.6 * K, -300, 105, -60], [5.2 * K, -250, 70, 340], [6.4 * K, -200, 60, 520]]);
  shot('fleet', 0, OPEN + 1.5, t => {   // (1.5 s longer: the dawn deck shot after it is 1.5 s shorter)
    const u = clamp(t / OPEN, 0, 1), e = ease(u), k = C.sample(FLY0, t);
    const pos = C.shipLocal(CAR, t, [k[1], k[2], k[3]]), look = C.shipLocal(CAR, t, [0, 22, lerp(40, -30, e)]);
    const fov = lerp(62, 52, e), vhalf = Math.atan(Math.tan(fov * D2R / 2) * ASPECT / BOX) / D2R;
    const d = norm(sub(look, pos)), pitch = Math.max(Math.asin(d[1]) / D2R, -0.62 * vhalf);
    return { pos, yaw: Math.atan2(d[0], d[2]) / D2R, pitch, roll: -4 * Math.sin(Math.PI * u), fov };
  }, 'A wide flyby of CVN-90 Stormhaven at dawn: in from high off the stern, closing down her port side toward the bow, the whole ship, the sea and the horizon in the frame.',
  { absent: DAWN_ABS, parkedIn: true, lens: 20, rig: 'camera helicopter', vibe: { a: 0.004, f: 0.5 }, subjects: ['carrier'] });
}
// the close-in now dissolves in from the fleet
{ const sr = S.shots.find(s => s.name === 'sunrise'); sr.t0 = r2(sr.t0 + 1.5); sr.in = { type: 'dissolve', dur: 1.0 }; }
S.shots.find(s => s.name === 'cic').in = { type: 'dissolve', dur: 0.5 };
// Deck: Viper 1 on the starboard bow catapult, the shooter to its right, the sun beyond them (east, to starboard).
// From the launch bar at deck level, up the nose and past the cockpit, then out to the port side for the jet in
// profile with the blast steam rolling under it, ending low on the shooter's run-up signal against the sun.
{
  const L = (t, l) => C.shipLocal(CAR, t, l);
  const DK = DECK;
  const PK = C.track([[0, -6, DK + 0.6, 72], [2.3, -9, DK + 2.5, 66], [4.0, -14, DK + 5.0, 60], [5.9, -28, DK + 3.0, 54], [DECK_D + 1.2, -36, DK + 1.6, 50.5]]);
  const LK = C.track([[0, 8, DK + 1.2, 62], [2.3, 8, JY + 1.0, 59], [4.0, 8, JY + 1.4, 57], [5.9, 12, JY + 0.4, 55], [DECK_D + 1.2, 19, DK + 1.6, 53]]);
  const MM = C.track([[0, 0, 0, 0, 50], [2.3, 0, 0, 0, 50], [4.0, 0, 0, 0, 60], [5.9, 0, 0, 0, 40], [DECK_D + 1.2, 0, 0, 0, 35]]);
  // the radar scope, drawn in code (cic.c): the sweep finds the two contacts closing, before the control room calls them
  shot('radar', DT1, DT1 + DECK_D, () => CIC_POSE, 'The carrier\'s radar scope, filling the picture: the sweep finds two contacts closing from the north, and a slow push goes in as the operator hooks one.',
    { absent: ['blue1', 'blue2', 'red1', 'red2'].map(id => [id, 'the shot is the radar scope']), scene: 'radar', in: { type: 'dissolve', dur: 0.3 } });
}
// (keep it panoramic when the planes take off, roll and rise; shorten the radar for it.) The radar
// gives up half its time and the control room moves up with it, so one wide take holds the whole launch: the carrier
// from off her port bow with both jets on the catapults at the alarm, off the bow on the band hit, the roll and the climb.
{
  const TO0 = S.shots.find(s => s.name === 'catapult').t0 - 3.8, TO1 = S.shots.find(s => s.name === 'drone').t1;
  const cicShot = S.shots.find(s => s.name === 'cic'), radarShot = S.shots.find(s => s.name === 'radar');
  const moveShot = (sh, t0, t1) => { sh.t0 = r2(t0); sh.t1 = r2(t1); };
  moveShot(cicShot, cicShot.t0 - 3.8, cicShot.t1 - 3.8);
  moveShot(radarShot, radarShot.t0, cicShot.t0);
  for (const c of S.cards) if (c.style === 'dialog') { c.t0 = r2(c.t0 - 3.8); c.t1 = r2(c.t1 - 3.8); }
  // the catapult take's close-up shake and deck heat haze belong to that camera
  const catT0 = TO0 + 3.8;
  // (the red alarm flashing on the picture's edges is annoying; the klaxon stays.) The catapult
  // take comes back for the shooter's signal and the launch, then the wide view again off the bow, and the drone's view
  // from in front for the climb and the roll.
  S.fx = S.fx.filter(f => f.type !== 'alarm');
  const catShot = S.shots.find(s => s.name === 'catapult'), droneShot = S.shots.find(s => s.name === 'drone');
  // (the officer who sends the jets off must stay in sight: the catapult take runs whole, to the climb)
  const CAT_END = droneShot.t0;
  catShot.t1 = r2(CAT_END);
  const pair = t => lerp3(P(blue1, t), P(blue2, t), 0.5);
  const ship = t => C.shipLocal(CAR, t, [0, 22, 30]);
  // it holds off the bow as they run, and lets them come up to it and pass as they roll and climb
  const TOK = C.track([[TO0, -235, 58, 200], [TO0 + 5.5, -235, 58, 215], [TO0 + 8, -230, 62, 270], [TO0 + 10, -215, 90, 480], [TO1, -190, 190, 880]]);
  shot('takeoff', TO0, TO1, t => {
    const u = clamp((t - TO0) / (TO1 - TO0), 0, 1), e = ease(u);
    const k = C.sample(TOK, t), pos = C.shipLocal(CAR, t, [k[1], k[2], k[3]]);
    const j = ease(clamp((t - (TO0 + 5.6)) / 5, 0, 1));   // from the ship to the pair as they leave her
    return { pos, look: lerp3(ship(t), smooth(pair, t, 0.4, TO0), lerp(0.25, 0.9, j)), fov: lens(24) };
  }, 'One wide take of the launch from a helicopter off the carrier\'s port bow: the whole ship with both jets on the catapults at the alarm, off the bow on the band hit, the victory roll and the climb into the dawn.',
  { absent: [...INBOUND, ...[29.2, 29.0, 29.3, 29.0].map((at, n) => ['parked' + (n + 1), 'left behind on the ship as the pair climb', at])], parkedIn: true, lens: 24, rig: 'camera helicopter off the port bow', partial: ["parked1", "parked2", "parked3", "parked4"], vibe: { a: 0.003, f: 0.5 }, in: { type: 'dissolve', dur: 0.5 },
    subjects: ['blue1', 'blue2', ['carrier', -1e9, TO0 + 7.5]] });
  {
    // the one wide take, cut in two round the catapult's
    const tk = S.shots[S.shots.length - 1], tk2 = JSON.parse(JSON.stringify(tk));
    tk.t1 = r2(catT0);
    const dk = S.shots.find(x => x.name === 'deck');
    // (after the shooter's hands come down, the whole catapult take; as Viper 2 draws level off the bow, the
    // wide view of the liftoff from the side; then the drone over the climb)
    const dr = S.shots.find(x => x.name === 'drone'), ct = S.shots.find(x => x.name === 'catapult');
    const T_PASS = 28.6, T_DRONE2 = 30.7;
    ct.t0 = r2(dk.t0 + (T_DECK1 - 9.0)); ct.t1 = T_PASS; delete ct.in;
    tk2.t0 = T_PASS; tk2.t1 = T_DRONE2; tk2.name = 'takeoff2'; tk2.in = undefined; S.shots.push(tk2);
    dr.t0 = T_DRONE2;
  }
}
S.shots.sort((a, b) => a.t0 - b.t0);
S.music.song_shift = SONG_SHIFT;
S.music.cues.deck = DT1;
S.version = 11;

for (const a of S.actors) delete a._track;
fs.writeFileSync(process.argv[2] || 'intro_script.json', JSON.stringify(S));
console.error('script', (JSON.stringify(S).length / 1024).toFixed(0), 'KB; kills at', killT.toFixed(2), killT2.toFixed(2), '; shots', S.shots.map(s => s.name + ' ' + s.t0 + '-' + s.t1).join(', '));
const missiles = S.actors.filter(a => a.kind === 'missile').map(a => a.id + ' ' + a.spawn.toFixed(2) + '-' + a.end.toFixed(2));
console.error(missiles.join('; '));
