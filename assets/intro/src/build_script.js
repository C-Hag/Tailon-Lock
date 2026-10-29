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
// Version 7 is cut to the main theme, Full Burn "Arp sound DMX10" (re-scored from DMX3: the same bars and timing) (assets/music/fullburn-intro.wav: half a second of
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

const S = { format: 'ace4combat-intro', version: 7, title: GAME.title, duration: 0, menu_at: 0,
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
      u = len(L) > 0.5 ? norm(L) : prevU;
      if (opt.gain) { const lv = sub(add(A[i], [0, G, 0]), mul(f, dot(add(A[i], [0, G, 0]), f))); const lat = sub(lv, mul([0, 1, 0], 0)); u = norm(lerp3(norm(sub([0, 1, 0], mul(f, f[1]))), u, opt.gain)); }
    } else u = norm(sub([0, 1, 0], mul(f, f[1])));
    prevU = u;
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
const parked = [[22, -95, -70], [22, -118, -70], [-38, -12, 60], [20, -140, -110]];
parked.forEach(([lx, lz, h], i) => actor('parked' + (i + 1), { kind: 'static', model: 'nf24', team: 0, attach: 'carrier', pos: [lx, JY, lz], heading: h }));
const blue1 = actor('blue1', { kind: 'jet', model: 'nf24', team: 0, callsign: 'VIPER 1' });
const blue2 = actor('blue2', { kind: 'jet', model: 'nf24', team: 0, callsign: 'VIPER 2' });
const red1 = actor('red1', { kind: 'jet', model: 'ks55', team: 1, callsign: 'BANDIT 1' });
// the wingman is not a pilot: a DR-7 Vesper, the unmanned drone of Beat the Drone! (MESH_DRONE in the game)
const red2 = actor('red2', { kind: 'jet', model: 'dr7', team: 1, callsign: 'VESPER' });

// ---------- 1. catapult launch and the low run over the sea (10.0 - 18.9) ----------
// A real catapult launch: the jet leaves the bow wings level near its minimum flying speed, about 20 m over the sea,
// so nothing rolls there. It rotates into an 8 degree climb for a second and a half, wings level; once it is clearly
// climbing (2.5 s off the deck) it makes a gentle clearing turn away from the other catapult (side: +1 right, -1 left;
// about 25 degrees of bank),
// then a gentler reversal (15 degrees) back onto the run heading and a long, shallow let-down to the low run, timed
// so the lift never drops low enough to tip the jet over (the attitude comes from the motion: see fly()).
function launch(a, lx, tStart, stroke, tEnd, side) {
  const pts = [];
  for (let t = 0; t < tStart - 1e-6; t += 1) pts.push([t, ...deck(t, lx, 56)]);
  pts.push([tStart, ...deck(tStart, lx, 56)]);
  const accel = 2 * 95 / (stroke * stroke);
  for (let k = 1; k <= 10; k++) { const t = tStart + stroke * k / 10, s = 0.5 * accel * (t - tStart) ** 2; pts.push([+t.toFixed(3), ...deck(t, lx, 56 + s)]); }
  const tb = tStart + stroke, P0 = deck(tb, lx, 151), v0 = CAR.speed + accel * stroke;
  const bump = (x, a0, a1) => x <= a0 || x >= a1 ? 0 : Math.sin(Math.PI * (x - a0) / (a1 - a0)) ** 2;   // smooth 0-1-0
  const speed = tau => v0 + 13 * tau;
  // flight path angle (degrees): rotate, climb, level off; then the let-down, a sine whose area undoes the climb
  // 8 degrees off the bow, eased back to 4 through Viper 1's victory roll (it keeps climbing), then levelling off
  const climb = tau => tau < 0.2 ? 0 : tau < 1.0 ? 8 * ease((tau - 0.2) / 0.8) : tau < 2.4 ? 8 : tau < 3.0 ? 8 - 4 * ease((tau - 2.4) / 0.6) :
    tau < 4.3 ? 4 : tau < 5.0 ? 4 * (1 - ease((tau - 4.3) / 0.7)) : 0;
  const DOWN0 = 5.0, DOWN1 = tSkimRef - tb + 0.2;
  // yaw rate (degrees a second) for a given bank at the current speed
  const rate = (bank, tau) => G * Math.tan(bank * D2R) / speed(tau) / D2R;
  const CLR = [4.5, 5.9], REV = [6.3, 7.9];      // seconds after the deck: wings level for 2.5, the roll, then the turns
  let p = P0.slice(), yaw = 0, gam = 0, h = 0;
  const dt = 0.05, T = tEnd - tb;
  // the let-down's depth: integrate the climb first
  let rise = 0; for (let tau = 0; tau < DOWN0; tau += dt) rise += speed(tau) * Math.sin(climb(tau) * D2R) * dt;
  let area = 0; for (let tau = DOWN0; tau < DOWN1; tau += dt) area += speed(tau) * Math.sin(D2R) * bump(tau, DOWN0, DOWN1) * dt;
  const down = (rise - 2) / area;                 // degrees at the deepest point: ends 2 m above where it started
  let clr = 0, rev = 0; for (let tau = 0; tau < T; tau += dt) { clr += rate(22, tau) * bump(tau, ...CLR) * dt; rev += bump(tau, ...REV) * dt; }
  const revRate = clr / rev;                      // the reversal takes back exactly what the clearing turn took
  for (let tau = 0, k = 0; tau <= T + 1e-6; tau += dt, k++) {
    if (k % 5 === 0) pts.push([+(tb + tau).toFixed(3), p[0], p[1], p[2]]);
    gam = climb(tau) - down * bump(tau, DOWN0, DOWN1);
    yaw += side * (rate(22, tau) * bump(tau, ...CLR) - revRate * bump(tau, ...REV)) * dt;
    p = add(p, mul(dirOf(yaw, gam), speed(tau) * dt));
  }
  return pts.filter(q => q[0] <= tEnd + 1e-6);
}
const tSkimRef = bar(4.1);       // the pair pass the skim camera low over the water (the skim shot's tSkim)
const t1End = bar(4.5);         // 20.61: end of the low run, start of the climb
const runB1 = launch(blue1, 8, 10.0, 2.05, t1End, 1);
const runB2 = launch(blue2, -8, 10.6, 2.05, t1End, -1);
{ // close the gap to 30 m behind the leader by the end of the low run
  const tb2 = 10.6 + 2.05, gap = runB1[runB1.length - 1][3] - runB2[runB2.length - 1][3] - 30;
  for (const p of runB2) if (p[0] > tb2) p[3] += gap * ease(clamp((p[0] - tb2 - 0.4) / 3.6, 0, 1));
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
  let p = [last[1], last[2], last[3]], sp = Math.min(v, 250), pitch = 0;
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
fly(blue1, pullUp(runB1), { yaw: 0, coordinated: true, rollFree: [VROLL[0], VROLL[0] + VROLL[1]],
  rolls: [...vroll, [tMerge + 0.4, -360], [tMerge + 0.9, -290], [tMerge + 2.0, -285]] });
fly(blue2, pullUp(runB2), { yaw: 0, coordinated: true, rolls: [[tMerge + 0.4, 0], [tMerge + 0.9, -65], [tMerge + 2.0, -70]] });
const mB1 = state(blue1, tMerge), mB2 = state(blue2, tMerge);
// bandits come down the line from the north, just above and to the left
const vR = 215;
function bandit(a, ref, off, roll) {
  const pass = add(ref.pos, off), d = dirOf(180, -4);
  const pts = [];
  for (let t = tMerge - 2.4; t <= tMerge + 1.7 + 1e-6; t += 0.25) pts.push([+t.toFixed(3), ...add(pass, mul(d, vR * (t - tMerge)))]);
  fly(a, pts, { yaw: 180, rolls: [[tMerge - 2.4, 0], [tMerge + 0.2, 0], [tMerge + 0.7, roll]] });
}
bandit(red1, mB1, [-30, 9, 0], -80);
bandit(red2, mB2, [-46, 18, 0], 75);

// Version 6 flies the dogfight as two continuous fights, so each can be one long take: Viper 1 against the KS-55
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
{
  const s = state(blue1, tS7), f = norm([s.vel[0], 0, s.vel[2]]), yaw0 = Math.atan2(f[0], f[2]) / D2R;
  const R0 = add(add(s.pos, mul(f, 560)), [0, 70, 0]);
  fly(red1, onward({ pos: R0, vel: mul(f, 215) }, tS7, tS11, { yaw: t => t < tS7 + 0.9 ? yaw0 : t < tS7 + 3.6 ? yaw0 - 115 : yaw0 - 60,
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
  S.env.extra_clouds = [Object.assign({}, CLOUD, { puffs })];
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
  // Viper 1 comes around from the splash and joins on the right, high
  const s1 = state(blue1, tS11), v1 = s1.vel;
  const join = [];
  for (let t = tS11; t <= tKill2 + 0.75 + 1e-6; t += 0.25) {
    const own = add(s1.pos, mul(v1, t - tS11));
    const r = at(red2, t - 3.3), rs = state(red2, t - 3.3), side = norm(cross([0, 1, 0], rs.f));
    const slot = add([r[1], r[2], r[3]], add(mul(side, -170), [0, 45, 0]));
    const k = ease(clamp((t - tS11) / 3.4, 0, 1));
    join.push([+t.toFixed(3), ...lerp3(own, slot, k)]);
  }
  fly(blue1, join, { smooth: 0.45 });
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
// after the kill both Vipers pull up and split, left and right, toward the sun
fly(blue2, onward(state(blue2, killT2), killT2, tEnd, { yaw: t => t < killT2 + 2 ? 40 : 30, rate: 30, pitch: 28, prate: 14, speed: 270 }));
fly(blue1, onward(state(blue1, killT2), killT2, tEnd, { yaw: 110, rate: 24, pitch: 22, prate: 12, speed: 270 }));
for (const a of [blue1, blue2]) (a.trail = a.trail || []).push([r2(killT2 + 0.2), r2(killT2 + 4)]);
// afterburner windows [t0, t1, level]
blue1.ab = [[9.4, 9.9, 0.5], [9.9, tS7 + 1.2, 1], [tS7 + 3.5, killT, 0.8], [tS11 - 0.3, tEnd, 1]];
blue2.ab = [[9.6, 10.4, 0.5], [10.4, tEnd, 1]];
red1.ab = [[tMerge - 2.4, killT, 0.9]];
red2.ab = [[tMerge - 2.4, killT2, 0.8]];
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
crew('shooter1', 'yellow', [[0, 19, 72, -90]], [[0, 'stand'], [8.9, 'tension'], [9.8, 'launch'], [10.7, 'point'], [11.6, 'stand']]);
crew('shooter2', 'yellow', [[0, -19, 72, 90]], [[0, 'stand'], [9.5, 'tension'], [10.4, 'launch'], [11.3, 'point'], [12.2, 'stand']]);
crew('green1', 'green', [[0, 12, 52, -90], [8.4, 12, 52, -90], [9.2, 17, 44, 160], [12, 17, 44, 160]], [[0, 'kneel'], [8.4, 'walk'], [9.2, 'thumbs'], [10.2, 'stand']]);
crew('green2', 'green', [[0, -12, 52, 90], [8.7, -12, 52, 90], [9.5, -17, 44, 200], [12, -17, 44, 200]], [[0, 'kneel'], [8.7, 'walk'], [9.5, 'thumbs'], [10.6, 'stand']]);
crew('blue1c', 'blue', [[0, -24, 92, 180], [8, -24, 80, 180]], [[0, 'walk']]);
crew('blue2c', 'blue', [[0, -29, 84, 180], [8, -29, 71, 180]], [[0, 'walk']]);
crew('blue3c', 'blue', [[0, 26, 102, -170], [12, 24, 84, -170]], [[0, 'walk']]);
crew('white1', 'white', [[0, 27, -12, -60]], [[0, 'stand'], [9.4, 'salute'], [10.4, 'stand']]);
crew('red1c', 'red', [[0, -31, 34, 70]], [[0, 'stand']]);
crew('purple1', 'purple', [[0, 24, 30, -120]], [[0, 'stand']]);
crew('green3', 'green', [[0, -2, 40, 0]], [[0, 'kneel'], [9.0, 'stand'], [10.5, 'point']]);
// steam drifting off the catapult tracks before the launch (carrier-local segments)
S.emitters = [
  { type: 'steam', attach: 'carrier', a: [8, DECK + 0.1, 58], b: [8, DECK + 0.1, 150], t0: 0, t1: 13, rate: 10 },
  { type: 'steam', attach: 'carrier', a: [-8, DECK + 0.1, 58], b: [-8, DECK + 0.1, 150], t0: 0, t1: 13.5, rate: 10 },
  { type: 'steam', attach: 'carrier', a: [-25, DECK + 0.1, -49], b: [-37, DECK + 0.1, 30], t0: 0, t1: 13, rate: 4 }
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
const tSkim = bar(4.1);    // the pair pass the camera low over the water, just before they climb
blue1.cone = [[r2(tSkim - 0.6), r2(tSkim + 0.4), 1], [tMerge - 0.35, tMerge + 0.25, 0.7]];
blue2.cone = [[r2(tSkim - 0.5), r2(tSkim + 0.5), 0.8], [tMerge - 0.3, tMerge + 0.2, 0.6]];

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
  const s = { name, t0: r2(t0), t1: r2(t1), desc, cam: keys };
  for (const k of ['lens', 'rig', 'attach', 'hud', 'vibe', 'in']) if (opt[k] != null) s[k] = opt[k];
  S.shots.push(s);
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

// Version 5 holds each take longer and lets the camera follow the jets: 11 shots instead of 14 over the same music,
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
// 1 Sunrise, 300 mm: across a kilometre of sea the island slides across the rising sun; heat haze
shot('sunrise', 0, 5.5, t => {
  const isl = deck(2.6, 28, -40, 0), pos = add(sub(isl, mul(SUNH, 900)), [0, 6, 0]);
  const yaw = Math.atan2(SUNH[0], SUNH[2]) / D2R;
  return { pos, yaw: yaw + 0.75 - 0.25 * t, pitch: 2.3 + 0.015 * t, fov: lerp(lensFov(280), lensFov(340), easeOut(t / 5.5)) };
}, 'The sun comes up behind the island of CVN-90 Stormhaven, seen from a kilometre away through a 300 mm lens.', { lens: 300, rig: 'long lens from a boat', vibe: { a: 0.006, f: 0.5 } });
// 2 On deck, one take from 18 mm to the launch: a slow dolly at knee height ahead of the alert jets, a crane move to the
// shooter as the klaxon goes, then the operator swings onto Viper 1 as it charges down the catapult and past
shot('deck', 5.5, HIT, t => {
  const u = clamp((t - 5.5) / 2.5, 0, 1), k = ease(clamp((t - 7.4) / 1.6, 0, 1)), w = clamp((t - 8) / 2, 0, 1);
  const pos0 = deck(t, lerp(2.2, 0.4, u), lerp(100, 95, u), DECK + 0.7), look0 = deck(t, 4, 60, DECK + 2.2);
  const pos1 = deck(t, 25, lerp(101.5, 100, w), DECK + 1.1), look1 = deck(t, lerp(15, 14, w), 67, DECK + 1.6);
  const j = ease(clamp((t - 10.05) / 0.9, 0, 1)), jet = smooth(tt => add(P(blue1, tt), [0, 1.2, 0]), t, 0.12, 5.5);
  const fov = lerp(lerp(lensFov(18), lensFov(70), k), lensFov(26), ease(clamp((t - 10.0) / 1.1, 0, 1)));
  return { pos: lerp3(pos0, pos1, k), look: lerp3(lerp3(look0, look1, k), jet, j), fov };
}, 'One take on the flight deck: knee height ahead of the jets in the steam, a crane move to the shooter as the klaxon goes, his run-up and launch signals, then a swing onto Viper 1 as the catapult fires and it roars past.', { lens: 18, rig: 'dolly on deck, crane to the shooter, panning with the launch', vibe: { a: 0.004, f: 1.2 }, in: { type: 'dissolve', dur: 0.6 } });
// Version 6 cuts like a film: 8 shots, every take 5 to 8 seconds, each on one continuous piece of action.
const T_CHASE = bar(3.5), T_SKIM = bar(6.5), T_LOCK = bar(21);
// 3 Chase, 35 to 24 mm: on the band hit, close behind Viper 1 off the bow; the camera jet swings out and forward round
// the pair until it looks back at both NF-24s low over the sea, the carrier behind them
shot('chase', HIT, T_CHASE, t => {
  const u = ease(clamp((t - HIT - 0.4) / 5.2, 0, 1));
  const s = state(blue1, t), fr = flatFrame(blue1, t, 0.4, HIT);
  const th = lerp(180, 70, u) * D2R, R = lerp(30, 55, u);
  const pos = add(s.pos, add(add(mul(fr.side, R * Math.sin(th)), mul(fr.f, R * Math.cos(th))), [0, lerp(3.5, 9, u), 0]));
  const pair = smooth(tt => lerp3(P(blue1, tt), P(blue2, tt), 0.3), t, 0.25, HIT);   // nearer Viper 1: it is close and big
  // mid-orbit, aim partly at Viper 1 itself, so the close jet stays well inside the frame as the camera swings round
  const onV1 = 0.7 * Math.sin(Math.PI * u);
  const look = lerp3(lerp3(add(s.pos, add(mul(fr.f, 120), [0, 1, 0])), pair, u), s.pos, onV1);
  return { pos, look, fov: lerp(lensFov(35), lensFov(28), u) };
}, 'On the band hit: chasing Viper 1 off the bow, then the camera jet swings out and forward round the pair until it looks back at both NF-24s skimming the sea, the carrier behind them.', { lens: 35, rig: 'camera jet, orbiting the pair', vibe: { a: 0.005, f: 5 } });
// 4 Cockpit, 5.6 s: Viper 1 low over the sea, the pull-up, the KS-55 and the drone growing from dots, flashing past at
// the merge, and the break
const COCKPIT = [0, 1.75, 2.6], FWD = [0, -0.06, 1];
// 4 On the water, 24 mm: a metre over the waves, the pair come in low, pass overhead with vapour cones and run on
// the camera skims the sea 40 m/s slower than the pair, so they close from behind, pass overhead and pull away
const SKIM = (() => { const s = state(blue1, tSkim); return { p: add(s.pos, [-40, -s.pos[1] + 1.6, 0]), v: Math.max(60, s.vel[2] - 40) }; })();
shot('skim', T_CHASE, T_SKIM, t => ({ pos: add(SKIM.p, [0, 0, SKIM.v * (t - tSkim)]), look: add(smooth(tt => lerp3(P(blue1, tt), P(blue2, tt), 0.3), t, 0.2, T_CHASE), [0, 3, 0]), fov: lensFov(24) }),
  'Skimming the waves: the pair close from behind, pass overhead in humid air with vapour cones on their backs, and climb away toward the bandits.', { lens: 24, rig: 'camera helicopter on the deck', vibe: { a: 0.006, f: 3 } });
rigShot('cockpit', T_SKIM, tS7, blue1, COCKPIT, FWD, 'In the cockpit of Viper 1, the head-up display and the NF-24 panel: low over the sea, the pull-up, the KS-55 and the drone grow from dots, flash past at the merge, and Viper 1 rolls into the break.', { lens: 21, rig: 'cockpit camera', hud: { actor: 'blue1', target: 'red1', panel: true }, vibe: { a: 0.003, f: 14 } });
// 5 Viper 1 against the KS-55, one 6.8 s take: on the right wing as the first missile drops off the rail and lights;
// the camera jet falls back and widens out to hold both jets through the turns, the second missile and splash one
shot('fight1', tS7, tS9, t => {
  const s = state(blue1, t), fr = flatFrame(blue1, t, 0.5, tS7);
  const k = ease(clamp((t - tS7 - 0.6) / 2.4, 0, 1));
  const bandit = smooth(tt => P(red1, tt), t, 0.3, tS7);
  const sep = dist(s.pos, bandit), D = lerp(35, clamp(sep * 0.3, 60, 150), k);
  const pos = add(s.pos, add(add(mul(fr.side, lerp(15, D * 0.6, k)), mul(fr.f, lerp(-30, -D, k))), [0, lerp(4, D * 0.3, k), 0]));
  const look = lerp3(add(s.pos, mul(fr.f, 300)), lerp3(s.pos, bandit, 0.7), ease(clamp((t - tS7 - 0.4) / 1.6, 0, 1)));
  return { pos, look, fov: lerp(lensFov(20), lensFov(28), k) };
}, 'On Viper 1\'s wing: fox two, the KS-55 fans out flares and the missile takes them. The camera falls back and holds both jets as Viper 1 stays in the bandit\'s turn, cuts across its reversal and fires again. Splash one.', { lens: 20, rig: 'camera jet on the wing, falling back into a wide chase', vibe: { a: 0.004, f: 5 } });
// 6 Viper 2 against the drone, one 6.8 s take: a camera jet off to the side keeps both in frame as the drone fires,
// Viper 2 breaks into it, and the two fall into the scissors until Viper 2 gets the guns
shot('fight2', tS9, tS11, t => {
  const mid = smooth(tt => lerp3(P(blue2, tt), P(red2, tt), 0.5), t, 0.4, tS9);
  const sep = smooth(tt => [dist(P(blue2, tt), P(red2, tt)), 0, 0], t, 0.6, tS9)[0];
  const fr = flatFrame(blue2, t, 1.0, tS9), D = clamp(sep * 1.1, 150, 520);
  return { pos: add(add(add(mid, mul(fr.side, -D)), mul(fr.f, -0.35 * D)), [0, 0.25 * D, 0]), look: mid, fov: lensFov(28) };
}, 'From a camera jet alongside: the drone fires at Viper 2, flares and a hard break, the missile misses; the drone overshoots and the two reverse across each other in a flat scissors until Viper 2 is on the guns.', { lens: 28, rig: 'camera jet abeam, pacing the fight', vibe: { a: 0.004, f: 2 } });

// the last missile's launch decides where the finale cuts
const L2 = S.actors.filter(a => a.kind === 'missile').pop().spawn;
const tFinale = +(L2 + 0.45).toFixed(3);
// 10 Lock, one take in Viper 2's cockpit: the drone runs for the sunrise, jinking; Viper 1 joins on the right; the lock
// diamond closes, tone, fox two
// 7 Pursuit, 50 mm: over Viper 2's shoulder as the drone runs for the sunrise, jinking; Viper 1 joins on the right
shot('pursuit', tS11, T_LOCK, t => {
  const s = state(blue2, t), fr = flatFrame(blue2, t, 0.6, tS11);
  const drone = smooth(tt => P(red2, tt), t, 0.4, tS11);
  const back = norm(smooth(tt => state(blue2, tt).vel, t, 0.6, tS11));   // along its climb, not just level
  const own = smooth(tt => P(blue2, tt), t, 0.3, tS11);
  return { pos: add(add(add(own, mul(back, -55)), mul(fr.side, 7)), [0, 5, 0]), look: lerp3(own, drone, 0.35), fov: lensFov(35) };
}, 'Over Viper 2\'s shoulder: the drone runs for the sunrise, weaving; Viper 1 joins high on the right.', { lens: 35, rig: 'camera jet in trail', vibe: { a: 0.004, f: 4 } });
rigShot('lock', T_LOCK, tFinale, blue2, COCKPIT, FWD, 'In Viper 2\'s cockpit: the drone runs for the sunrise, jinking, Viper 1 joins on the right, the lock closes and Viper 2 fires.', { lens: 21, rig: 'cockpit camera', hud: { actor: 'blue2', target: 'red2', panel: true, lock: [r2(L2 - 2.2), r2(L2 - 0.5)] }, vibe: { a: 0.003, f: 14 } });
// 11 Finale, 40 mm: behind Viper 2 into the sun; the hit on bar 13, the Vipers split, and the fire and smoke
// drift together into the title, a cloud in the dawn sky. The camera glides to a stop and holds under the menu.
const FIN = (() => { const s = state(blue2, killT2); return { p: add(add(s.pos, mul(s.f, -55)), [0, 10, 0]), v: s.vel }; })();
shot('finale', tFinale, tEnd, t => {
  let pos;
  if (t <= killT2) { const s = state(blue2, t); pos = add(add(s.pos, mul(s.f, -55)), [0, 10, 0]); }
  else { const tau = t - killT2; pos = add(FIN.p, mul(FIN.v, (1 - Math.exp(-1.4 * tau)) / 1.4)); }
  const d = dist(pos, killPos2), aim = sub(killPos2, [0, 0.07 * d, 0]);
  let look = lerp3(P(red2, Math.min(t, killT2 - 0.01)) || aim, aim, ease(clamp((t - (killT2 - 1.0)) / 1.0, 0, 1)));
  // as the title thins away the camera tilts down after the falling wreck to see it splash into the sea
  const sp = S.events.find(e => e.type === 'splash' && e.actor === 'red2');
  const k = ease(clamp((t - (sp.t - 3.8)) / 3.2, 0, 1));
  if (k > 0) look = lerp3(look, add(sp.pos, [0, 25, 0]), 0.85 * k);
  return { pos, look, fov: lerp(lensFov(40), lensFov(32), k) };
}, 'Behind Viper 2, into the sun: the missile hits on bar 13 (34.24 s), the Vipers split, and the fire and smoke drift together into ' + GAME.logo + ', a cloud in the dawn sky; it vaporizes as the camera tilts down to the wreck splashing into the sea.', { lens: 40, rig: 'camera jet in trail, then hovering' });

// ---------- cards, effects, sounds ----------
// the title forms, holds a moment, then vaporizes into thin air (3.5 s), gone as the wreck splashes into the sea
const splash2 = S.events.find(e => e.type === 'splash' && e.actor === 'red2');
const tForm0 = r2(killT2 + 0.35), tForm1 = r2(killT2 + 3.3), tGone1 = r2(splash2.t), tGone0 = r2(tGone1 - 3.5);
S.cards = [
  { t0: 1.4, t1: 7.6, style: 'location', text: 'SOMEWHERE IN THE PACIFIC', type: 16 },
  { t0: 3.6, t1: 7.6, style: 'sub', text: '05:47 LOCAL  -  CVN-90 STORMHAVEN', type: 28 },
  // the alert as the control room hears it, in the lower bar, not across the picture: the radar operator, then the commander
  { t0: 8.3, t1: 9.7, style: 'radio', text: 'RADAR: BOGEYS INBOUND, BEARING 3-5-0' },
  { t0: 9.9, t1: 11.4, style: 'radio', text: 'COMMANDER: SEND THE BIRDS!' },
  { t0: +(half(1) - 0.2).toFixed(2), t1: half(1) + 1.6, style: 'callsign', text: 'VIPER 1  -  NF-24 CORMORANT' },
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
  { t0: 0, t1: 1e9, type: 'grade', warm: 0.1, grain: 0.07, vignette: 0.32 },
  { t0: 0, t1: r2(tGone1 - 1.0), type: 'letterbox', amount: 1 },
  { t0: r2(tGone1 - 1.0), t1: r2(tGone1 + 0.4), type: 'letterbox', from: 1, to: 0 },
  { t0: 0, t1: 5.5, type: 'haze', y0: 0.5, y1: 1, amount: 1.4 },
  { t0: 10.0, t1: 12.05, type: 'haze', y0: 0.66, y1: 1, amount: 0.7 },
  { t0: 8.3, t1: 10.0, type: 'alarm', period: 0.9 },
  { t0: 8.2, t1: 9.4, type: 'radar', contacts: 2, bearing: 350 },
  { t0: 9.55, t1: 10.0, type: 'shake', amount: 0.35 },
  { t0: 11.55, t1: 12.05, type: 'shake', amount: 1.2 },
  { t0: 12.05, t1: 12.5, type: 'shake', amount: 0.6 },
  { t0: r2(tSkim - 0.2), t1: r2(tSkim + 0.2), type: 'shake', amount: 1 },
  { t0: tMerge - 0.05, t1: tMerge + 0.45, type: 'shake', amount: 0.8 },
  { t0: tS7 + 0.2, t1: tS7 + 0.5, type: 'shake', amount: 0.5 },
  { t0: r2(killT2), t1: r2(killT2 + 0.5), type: 'flash', color: '#fff2dc', from: 0.45, to: 0 },
  { t0: r2(killT2), t1: r2(killT2 + 0.6), type: 'shake', amount: 0.7 },
  // the fire and smoke of the last kill become the title: a cloud in the sky
  { t0: tForm0, t1: 1e9, type: 'smoketitle', text: GAME.logo, pos: killPos2, form: [tForm0, tForm1], dissolve: [tGone0, tGone1], seed: 2026 }
];
S.events.push({ t: 8.3, type: 'sound', name: 'klaxon', repeat: 2, every: 0.9 });
S.events.push({ t: 10.0, type: 'sound', name: 'catapult' }, { t: 10.6, type: 'sound', name: 'catapult' });
S.events.push({ t: 11.6, type: 'sound', name: 'flyby' }, { t: r2(tSkim - 0.3), type: 'sound', name: 'flyby' }, { t: tMerge - 0.15, type: 'sound', name: 'flyby' });
S.events.push({ t: r2(tForm0 + 0.3), type: 'sound', name: 'cloud' });
S.events.sort((a, b) => (a.t ?? a.t0) - (b.t ?? b.t0));
S.duration = r2(tGone1 + 0.5);
S.menu_at = r2(tGone1 + 0.5);
S.music = { module: 'recording', track: 'fullburn-intro', name: 'Full Burn, Arp sound DMX10 (film cut)', start_offset: 0,
  cues: { band_hit: HIT, bar: +BAR.toFixed(5), riff: bar(2), verse: bar(10), pre: bar(18), chorus: bar(26), merge: tMerge, dogfight: tS7, last_kill: r2(killT2), title_formed: tForm1, title_gone: tGone1 },
  note: 'Times are seconds from the first music sample. After the band hit, bar n starts at band_hit + n * bar.' };
S.env = Object.assign({ map: 'twin-isles', time_of_day: 'dawn', weather: 'scattered', assets: 'intro_assets.json' }, S.env);
for (const a of S.actors) delete a._track;
fs.writeFileSync(process.argv[2] || 'intro_script.json', JSON.stringify(S));
console.error('script', (JSON.stringify(S).length / 1024).toFixed(0), 'KB; kills at', killT.toFixed(2), killT2.toFixed(2), '; shots', S.shots.map(s => s.name + ' ' + s.t0 + '-' + s.t1).join(', '));
const missiles = S.actors.filter(a => a.kind === 'missile').map(a => a.id + ' ' + a.spawn.toFixed(2) + '-' + a.end.toFixed(2));
console.error(missiles.join('; '));
