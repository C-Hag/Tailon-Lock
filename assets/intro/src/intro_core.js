// ---- intro core start ----
// Engine-neutral helpers for the intro shot script (intro_script.json): key sampling, frames from angles and flare
// paths. The script builder (build_script.js) uses the same code, so what it plans is what the game plays.
const IntroCore = (() => {
  const D2R = Math.PI / 180;
  // keys: [t, x, y, z, yaw, pitch, roll, ...extra]. A null entry splits the list into segments; between segments the
  // owner is hidden. Position: cubic Hermite with Catmull-Rom tangents inside the segment (one-sided at its ends, and
  // zero where a key repeats the one before it, so a hold eases out). Angles and extras: linear. Angles are unwrapped
  // in the data, so no wrap-around handling is needed. Engines should call track() once per list and keep the result.
  function segments(keys) {
    const out = []; let cur = [];
    for (const k of keys || []) { if (k) cur.push(k); else if (cur.length) { out.push(cur); cur = []; } }
    if (cur.length) out.push(cur);
    return out;
  }
  function track(keys) { return keys && keys.segs ? keys : { segs: segments(keys) }; }
  function sample(tr, t, out) {
    tr = track(tr);
    let K = null;
    for (const s of tr.segs) if (t >= s[0][0] && t <= s[s.length - 1][0]) K = s; // at a joint, the later segment
    if (!K) return null;
    let lo = 0, hi = K.length - 1;
    while (hi - lo > 1) { const m = (lo + hi) >> 1; if (K[m][0] <= t) lo = m; else hi = m; }
    const res = out || [];
    const a = K[lo], b = K[hi] || a;
    res.length = a.length; res[0] = t;
    if (b === a || b[0] === a[0]) { for (let c = 1; c < a.length; c++) res[c] = a[c]; return res; }
    const dt = b[0] - a[0], u = (t - a[0]) / dt;
    const p = K[lo - 1], n = K[hi + 1];
    const h00 = 2 * u * u * u - 3 * u * u + 1, h10 = u * u * u - 2 * u * u + u, h01 = -2 * u * u * u + 3 * u * u, h11 = u * u * u - u * u;
    for (let c = 1; c <= 3; c++) res[c] = h00 * a[c] + h10 * dt * tan(p, a, b, c) + h01 * b[c] + h11 * dt * tan(a, b, n, c);
    for (let c = 4; c < a.length; c++) res[c] = a[c] + (b[c] - a[c]) * u;
    return res;
  }
  // Catmull-Rom tangent at key m (prev p, next n); zero when m repeats p, so holds ease out
  function tan(p, m, n, c) {
    if (p && p[1] === m[1] && p[2] === m[2] && p[3] === m[3]) return 0;
    if (n && n[1] === m[1] && n[2] === m[2] && n[3] === m[3]) return 0;
    if (!p) return n ? (n[c] - m[c]) / (n[0] - m[0]) : 0;
    if (!n) return (m[c] - p[c]) / (m[0] - p[0]);
    return (n[c] - p[c]) / (n[0] - p[0]);
  }
  // yaw: degrees clockwise from +Z (0 = north/red, 90 = +X/east). pitch: nose up positive. roll: right wing down positive.
  function frame(yaw, pitch, roll) {
    const y = yaw * D2R, p = pitch * D2R, r = roll * D2R;
    const f = [Math.sin(y) * Math.cos(p), Math.sin(p), Math.cos(y) * Math.cos(p)];
    const r0 = [Math.cos(y), 0, -Math.sin(y)];
    const u0 = [f[1] * r0[2] - f[2] * r0[1], f[2] * r0[0] - f[0] * r0[2], f[0] * r0[1] - f[1] * r0[0]];
    const c = Math.cos(r), s = Math.sin(r);
    return { f, r: [r0[0] * c - u0[0] * s, r0[1] * c - u0[1] * s, r0[2] * c - u0[2] * s], u: [u0[0] * c + r0[0] * s, u0[1] * c + r0[1] * s, u0[2] * c + r0[2] * s] };
  }
  function angles(f, u) {
    const yaw = Math.atan2(f[0], f[2]) / D2R, pitch = Math.asin(Math.max(-1, Math.min(1, f[1]))) / D2R;
    const y = yaw * D2R, r0 = [Math.cos(y), 0, -Math.sin(y)];
    const u0 = [f[1] * r0[2] - f[2] * r0[1], f[2] * r0[0] - f[0] * r0[2], f[0] * r0[1] - f[1] * r0[0]];
    const roll = Math.atan2(u[0] * r0[0] + u[1] * r0[1] + u[2] * r0[2], u[0] * u0[0] + u[1] * u0[1] + u[2] * u0[2]) / D2R;
    return [yaw, pitch, roll];
  }
  // flares: drag k and a light gravity, closed form so any engine gets the same path
  const FLARE_K = 1.6, FLARE_G = 5;
  function flarePos(fl, t) {
    const tau = t - fl.t;
    if (tau < 0 || tau > fl.life) return null;
    const e = (1 - Math.exp(-FLARE_K * tau)) / FLARE_K;
    return [fl.p[0] + fl.v[0] * e, fl.p[1] + fl.v[1] * e - 0.5 * FLARE_G * tau * tau, fl.p[2] + fl.v[2] * e];
  }
  // a ship moves in a straight line: carrier-local (lx, ly, lz) with +X starboard, +Z bow -> world
  function shipPos(s, t) { const h = s.heading * D2R; return [s.x + Math.sin(h) * s.speed * t, 0, s.z + Math.cos(h) * s.speed * t]; }
  function shipLocal(s, t, l) {
    const h = s.heading * D2R, c = Math.cos(h), n = Math.sin(h), o = shipPos(s, t);
    return [o[0] + l[0] * c + l[2] * n, (l[1] || 0), o[2] - l[0] * n + l[2] * c];
  }
  // the cue in effect at time t (cards, fx, shots all use [t0, t1))
  function active(list, t) { return list.filter(e => t >= e.t0 && t < e.t1); }
  return { segments, track, sample, frame, angles, flarePos, shipPos, shipLocal, active, FLARE_K, FLARE_G, D2R };
})();
if (typeof module !== 'undefined') module.exports = IntroCore;
// ---- intro core end ----
