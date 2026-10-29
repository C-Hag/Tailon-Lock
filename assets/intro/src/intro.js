// ---- intro start ----
// Tailon Lock opening cinematic. It plays INTRO_SCRIPT, the engine-neutral shot script (intro/intro_script.json, built
// by intro/src/build_script.js): world-space keys for every jet, missile and camera, flares in closed form, plus
// events, text cards and screen effects. The scene uses the project's own assets: the NF-24 Cormorant and CVN-90
// Stormhaven from the jets work, the Ks-55 Grifon already in the game, and the Twin Isles sea map with its dawn light
// and cumulus weather (INTRO_ASSETS). Music: Steel Horizon (heavy C64-style mix); the picture runs on the music's clock.
// It is shot like an aviation film: long lenses on the deck at sunrise, crew and the shooter's launch signal, cameras
// bolted to the jets (cockpit with its head-up display, wing, rail and tail), heat haze, wing vapour and vapour cones,
// cut on the beat, with a warm grade and film grain.
// States: 'boot' (press any key, which also unlocks audio), 'intro' (playing, any key skips), then the menu opens
// over the last shot, which keeps drifting until a game starts.
const GAME_LOGO = 'TAILON LOCK', GAME_SUBTITLE = 'THE GREAT NOSE2NOSE ENGAGEMENT';   // the game's name (src/brand.h)
const INTRO_SCRIPT = __INTRO_SCRIPT__;
const INTRO_ASSETS = __INTRO_ASSETS__;
const Intro = (() => {
  const S = INTRO_SCRIPT, A = INTRO_ASSETS, IC = IntroCore;
  const FIXED = 1 / 60, D2R_ = Math.PI / 180;
  // ---------- light ----------
  const tod = A.tod, SUN = norm(S.env.sun || tod.sun);   // the script may lower the sun for its sunrise shot
  const c01 = h => hex2rgb(h).map(v => v / 255);
  const SKY_T = hex2rgb(tod.sky_top), SKY_H = hex2rgb(tod.sky_horizon), FOGC = hex2rgb(tod.fog);
  const AMB = c01(tod.ambient).map(v => v * tod.ambient_intensity), SUNL = c01(tod.sun_color).map(v => v * tod.sun_intensity);
  const GLOW = [255, 196, 140];
  const WT = c01(tod.water_tint), SEA0 = hex2rgb(A.far_ground);
  const WATER = SEA0.map((c, i) => c * (0.55 + 0.45 * WT[i]) * (1 - A.water_sky_mix) + (SKY_H[i] + SKY_T[i]) / 2 * A.water_sky_mix);
  const FOG_D = 16000;
  const ease = t => t * t * (3 - 2 * t);
  const mix3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const css = (c, a) => a == null || a >= 1 ? `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})` : `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${a.toFixed(3)})`;
  function fogCol(dir) { const k = Math.max(0, dot(dir, SUN)); return mix3(FOGC, GLOW, Math.pow(k, 6) * 0.75); }
  function fogged(c, w, cam) {
    const d = sub(w, cam.pos), dl = len(d) || 1, t = clamp(1 - Math.exp(-dl / FOG_D), 0, 0.92);
    return mix3(c, fogCol(mul(d, 1 / dl)), t);
  }
  function litFace(col, n) {
    const k = dot(n, SUN), s = 0.82 * Math.max(0, k) + 0.18 * Math.abs(k);
    return [col[0] * (AMB[0] + SUNL[0] * s * 1.15), col[1] * (AMB[1] + SUNL[1] * s * 1.15), col[2] * (AMB[2] + SUNL[2] * s * 1.15)];
  }
  // ---------- meshes ----------
  function prep(m) {
    // model-space face normals once; per frame they are only rotated
    const M = { v: m.v, f: m.f.map(([i, c]) => ({ i, c: typeof c === 'string' ? hex2rgb(c) : c })), rad: m.rad };
    for (const fc of M.f) { const a = M.v[fc.i[0]], b = M.v[fc.i[1]], c = M.v[fc.i[2]]; fc.n = norm(cross(sub(b, a), sub(c, a))); }
    return M;
  }
  const MODELS = {};
  MODELS.nf24 = Object.assign(prep(A.nf24.m), { lod: prep(A.nf24.lod), minY: A.nf24.minY, tail: A.nf24.tail });
  MODELS.cvn90 = prep(A.cvn90.m);
  {
    const ks = (typeof JT !== 'undefined' && JT.ks55) || null;
    const src = ks ? ks.mesh[1] : MESH.jet[1];
    MODELS.ks55 = Object.assign(prep({ v: src.v, f: src.f.map(f => [f.i, f.c]), rad: src.rad }), src.lod ? { lod: prep({ v: src.lod.v, f: src.lod.f.map(f => [f.i, f.c]), rad: src.lod.rad }) } : {}, { tail: ks ? ks.tail : 6.5 });
    // frigate escort: a simple low-poly hull, superstructure and mast (about 130 m)
    const V = [], F = [];
    const hull = '#69707a', deckc = '#59606a', sup = '#7c838c';
    const H = [[-7, 0, -62], [7, 0, -62], [8, 0, 30], [0, 0, 66], [-8, 0, 30], [-7, 7, -62], [7, 7, -62], [8.5, 8, 30], [0, 10, 66], [-8.5, 8, 30]];
    V.push(...H);
    F.push([[5, 6, 7, 8, 9], deckc], [[0, 1, 6, 5], hull], [[1, 2, 7, 6], hull], [[2, 3, 8, 7], hull], [[3, 4, 9, 8], hull], [[4, 0, 5, 9], hull]);
    box(V, F, 0, 7, -8, 5, 9, 16, sup, '#8a9199'); box(V, F, 0, 16, -2, 3, 6, 5, sup, '#8a9199'); box(V, F, 0, 22, -2, 0.5, 14, 0.5, '#444'); box(V, F, 0, 7, -40, 4, 4, 6, sup, '#8a9199');
    MODELS.frigate = prep({ v: V, f: F, rad: 70 });
  }
  // ---------- terrain (Twin Isles at 400 m) ----------
  const TRI = [];
  {
    const T = A.terrain, n = T.n, sp = T.spacing, o = T.origin, step = 1, m = Math.floor((n - 1) / step);
    const hgt = (i, j) => T.h[Math.min(n - 1, j * step) * n + Math.min(n - 1, i * step)];
    const P = (i, j) => [o[0] + i * step * sp, hgt(i, j), o[1] + j * step * sp];
    const matAt = (i, j) => T.mat[Math.min(T.n - 2, j * step) * (T.n - 1) + Math.min(T.n - 2, i * step)];
    const hash = (i, j, t) => { let x = (Math.imul(i, 73856093) ^ Math.imul(j, 19349663) ^ Math.imul(t, 83492791)) >>> 0; x = Math.imul(x ^ (x >>> 13), 1274126177) >>> 0; x ^= x >>> 16; return (x & 255) / 255; };
    for (let j = 0; j < m; j++) for (let i = 0; i < m; i++) {
      const a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
      if (Math.max(a[1], b[1], c[1], d[1]) < 25) continue;
      const pal = T.palette[matAt(i, j)] || T.palette[0];
      for (const [t, q] of [[0, [a, b, c]], [1, [a, c, d]]]) {
        let nn = norm(cross(sub(q[1], q[0]), sub(q[2], q[0]))); if (nn[1] < 0) nn = mul(nn, -1);
        const base = hex2rgb(pal.water ? '#c8b98a' : pal.color).map(v => v * (1 + pal.jitter * (2 * hash(i, j, t) - 1)));
        TRI.push({ p: q.map(v => [v[0], Math.max(v[1], 0), v[2]]), c: litFace(base, nn), mid: [(q[0][0] + q[1][0] + q[2][0]) / 3, 0, (q[0][2] + q[1][2] + q[2][2]) / 3] });
      }
    }
  }
  // ---------- clouds ----------
  const PUFFS = [];
  // env.clear_clouds (version 3): [x, z, r] circles of open sky; map clouds centred inside them are left out
  const cleared = cl => (S.env.clear_clouds || []).some(c => Math.hypot(cl.x - c[0], cl.z - c[1]) < c[2]);
  for (const cl of A.clouds.filter(cl => !cleared(cl)).concat(S.env.extra_clouds || [])) {
    const depth = Math.max(1, cl.top - cl.base);
    for (const p of cl.puffs) {
      const [dx, dy, dz, r] = p;
      const nn = norm([dx, dy - 0.35 * depth, dz]), h = clamp(dy / depth, 0, 1);
      const k = Math.max(0, dot(nn, SUN));
      const lit = clamp(0.25 + 0.55 * k + 0.35 * h, 0, 1);
      const col = mix3([128, 126, 158], [255, 214, 178], lit);
      PUFFS.push({ w: [cl.x + dx, cl.base + dy, cl.z + dz], r, c: col, sq: p[4] || 1 });
    }
  }
  // ---------- tracks ----------
  const ACT = {};
  for (const a of S.actors) { ACT[a.id] = a; if (a.keys && a.keys.length) a.tr = IC.track(a.keys); }
  const SHOTS = S.shots.map(s => Object.assign({}, s, { tr: IC.track(s.cam) }));
  const tmp = [];
  function actorFrame(a, t) {
    if (a.kind === 'ship') { const p = IC.shipPos(a.ship, t); const fr = IC.frame(a.ship.heading, 0, 0); return { pos: p, f: fr.f, r: fr.r, u: fr.u }; }
    if (a.kind === 'static') {
      const sh = ACT[a.attach].ship, p = IC.shipLocal(sh, t, a.pos), fr = IC.frame(sh.heading + a.heading, 0, 0);
      return { pos: p, f: fr.f, r: fr.r, u: fr.u };
    }
    if (!a.tr) return null;
    if (a.spawn != null && t < a.spawn) return null;
    if (a.end != null && t > a.end) return null;
    const s = IC.sample(a.tr, t, tmp); if (!s) return null;
    const fr = IC.frame(s[4], s[5], s[6]);
    return { pos: [s[1], s[2], s[3]], f: fr.f, r: fr.r, u: fr.u };
  }
  const inWin = (list, t) => { if (!list) return 0; for (const w of list) if (t >= w[0] && t < w[1]) return w[2] == null ? 1 : w[2]; return 0; };
  // ---------- run state ----------
  let st = null;     // { t, acc, parts, evI, rng, ... }
  let mode = 'off';  // off | boot | play | hold
  let clockFn = null, musicOn = false, lastNow = 0;
  function rng(seed) { let a = seed >>> 0; return () => { a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
  function reset() {
    st = { t: 0, sim: 0, parts: [], ev: 0, R: rng(4711), trails: {}, tracers: [], shownMenu: false, firedSounds: new Set(), lastPrev: {} };
  }
  // ---------- particles ----------
  // kinds: 0 smoke, 1 fire, 2 flash, 3 debris, 4 steam, 5 spray, 6 flare smoke, 7 steam wisp
  const PK = [
    { c0: [150, 146, 150], c1: [92, 88, 96], a: 0.55 },
    { c0: [255, 236, 150], c1: [210, 70, 24], a: 1 },
    { c0: [255, 250, 225], c1: [255, 190, 90], a: 1 },
    { c0: [40, 34, 30], c1: [30, 28, 26], a: 1 },
    { c0: [245, 240, 236], c1: [210, 206, 214], a: 0.5 },
    { c0: [240, 244, 250], c1: [200, 214, 230], a: 0.45 },
    { c0: [236, 232, 236], c1: [170, 166, 176], a: 0.5 },
    { c0: [244, 238, 234], c1: [214, 208, 214], a: 0.2 },
  ];
  function part(kind, pos, vel, life, size, grow, drag = 0.6) { if (st.parts.length < 3200) st.parts.push({ k: kind, p: pos.slice(), v: vel.slice(), life, max: life, s: size, g: grow, d: drag }); }
  function explode(pos, size) {
    const R = st.R;
    part(2, pos, [0, 0, 0], 0.18, 7 * size, 40 * size);
    for (let i = 0; i < 14 * size; i++) { const d = norm([R() - 0.5, R() - 0.4, R() - 0.5]); part(1, pos, mul(d, 20 + 40 * R()), 0.45 + 0.6 * R(), 3 + 3 * size * R(), 9, 1.6); }
    for (let i = 0; i < 16 * size; i++) { const d = norm([R() - 0.5, R() - 0.3, R() - 0.5]); part(0, add(pos, mul(d, 4 * R())), mul(d, 12 + 20 * R()), 2.5 + 2 * R(), 7 + 5 * size, 9, 1.2); }
    if (size > 1) for (let i = 0; i < 26; i++) { const d = norm([R() - 0.5, R() - 0.2, R() - 0.5]); part(3, pos, mul(d, 60 + 90 * R()), 1.5 + 1.5 * R(), 1.4, 0, 0.3); }
  }
  // ---------- sounds ----------
  function sfx(e) {
    if (!AU.ctx || !settings.sound) return;
    const n = e.name;
    if (n === 'klaxon') {
      const reps = (e.repeat || 1) + 1;
      for (let k = 0; k < reps; k++) setTimeout(() => klaxon(), k * (e.every || 0.9) * 1000);
    } else if (n === 'catapult') { sNoise(1.6, 0.32, 3200, 160); sTone(70, 38, 0.5, 0.18, 'sine'); }
    else if (n === 'missile') { whoosh(1.1, 0.26, 600, 3400); }
    else if (n === 'flares') { for (let k = 0; k < 6; k++) setTimeout(() => { sNoise(0.12, 0.12, 2400, 900); sTone(1300, 500, 0.08, 0.03); }, k * 80); }
    else if (n === 'explode') { sNoise(1.6, 0.42, 1100, 50); sTone(90, 30, 0.7, 0.3, 'sine'); }
    else if (n === 'cloud') { whoosh(1.8, 0.18, 300, 1400); }
    else if (n === 'flyby') { whoosh(1.3, 0.34, 180, 2600); sNoise(1.1, 0.26, 420, 60); }
  }
  function klaxon() {
    try {
      const c = AU.ctx, t = c.currentTime, o = c.createOscillator(), o2 = c.createOscillator(), f = c.createBiquadFilter(), g = c.createGain();
      o.type = 'sawtooth'; o2.type = 'square'; f.type = 'lowpass'; f.frequency.value = 1800;
      o.frequency.setValueAtTime(330, t); o.frequency.exponentialRampToValueAtTime(880, t + 0.62);
      o2.frequency.setValueAtTime(332, t); o2.frequency.exponentialRampToValueAtTime(884, t + 0.62);
      g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(0.13, t + 0.04); g.gain.setValueAtTime(0.13, t + 0.58); g.gain.exponentialRampToValueAtTime(0.0008, t + 0.72);
      o.connect(f); o2.connect(f); f.connect(g); g.connect(AU.master); o.start(t); o2.start(t); o.stop(t + 0.75); o2.stop(t + 0.75);
    } catch (e) {}
  }
  function whoosh(dur, vol, f0, f1) {
    try {
      const c = AU.ctx, t = c.currentTime, s = c.createBufferSource(), f = c.createBiquadFilter(), g = c.createGain();
      s.buffer = AU.noise; s.loop = true; f.type = 'bandpass'; f.Q.value = 0.8;
      f.frequency.setValueAtTime(f0, t); f.frequency.exponentialRampToValueAtTime(f1, t + dur * 0.4); f.frequency.exponentialRampToValueAtTime(f0, t + dur);
      g.gain.setValueAtTime(0.0008, t); g.gain.exponentialRampToValueAtTime(vol, t + dur * 0.25); g.gain.exponentialRampToValueAtTime(0.0008, t + dur);
      s.connect(f); f.connect(g); g.connect(AU.master); s.start(t); s.stop(t + dur + 0.05);
    } catch (e) {}
  }
  // ---------- simulation (fixed step, seeded, so a replay looks the same) ----------
  function step(t, dt) {
    const R = st.R;
    // events
    const evs = S.events;
    while (st.ev < evs.length && (evs[st.ev].t ?? evs[st.ev].t0) <= t) {
      const e = evs[st.ev++];
      if (e.type === 'explode') { explode(e.pos, e.size || 1); if (!st.quiet) sfx({ name: 'explode' }); }
      else if (e.type === 'sound' && !st.quiet) sfx(e);
      else if (e.type === 'splash') { for (let i = 0; i < 30; i++) part(5, e.pos, [(R() - 0.5) * 30, 30 + 40 * R(), (R() - 0.5) * 30], 2 + R(), 8, 10, 0.5); }
    }
    // guns
    for (const e of evs) if (e.type === 'guns' && t >= e.t0 && t < e.t1) {
      const a = ACT[e.actor], fr = actorFrame(a, t); if (!fr) continue;
      if ((Math.floor(t * 30) !== Math.floor((t - dt) * 30))) st.tracers.push({ p: add(fr.pos, mul(fr.f, 9)), v: add(mul(fr.f, 950), [(R() - 0.5) * 12, (R() - 0.5) * 12, (R() - 0.5) * 12]), life: 0.7 });
    }
    for (const b of st.tracers) { b.p = add(b.p, mul(b.v, dt)); b.v[1] -= 9.81 * dt; b.life -= dt; }
    st.tracers = st.tracers.filter(b => b.life > 0);
    // per-actor emitters
    for (const a of S.actors) {
      if (a.kind === 'missile') {
        const fr = actorFrame(a, t); if (!fr) continue;
        const tail = sub(fr.pos, mul(fr.f, 2.5));
        part(0, tail, [(R() - 0.5) * 3, (R() - 0.5) * 3, (R() - 0.5) * 3], 2.2 + R(), 2.2, 5.5, 1.5);
        continue;
      }
      if (a.kind !== 'jet') continue;
      const fr = actorFrame(a, t); if (!fr) { st.trails[a.id] = null; continue; }
      const M = MODELS[a.model];
      // burning wreck
      if (a.dead != null && t >= a.dead) {
        part(1, fr.pos, [(R() - 0.5) * 8, (R() - 0.5) * 8, (R() - 0.5) * 8], 0.4 + 0.3 * R(), 4 + 3 * R(), 6, 1.5);
        part(0, fr.pos, [(R() - 0.5) * 4, 2 + R() * 3, (R() - 0.5) * 4], 3 + 2 * R(), 5, 8, 1.2);
        continue;
      }
      // catapult steam: from the shuttle track while the jet runs down the deck
      const sp = st.lastPrev[a.id] ? dist(st.lastPrev[a.id], fr.pos) / dt : 0;
      st.lastPrev[a.id] = fr.pos;
      if (fr.pos[1] < 30 && fr.pos[1] > 19 && sp > 20 && sp < 130 && R() < 0.35) part(4, sub(fr.pos, [0, 1.2, 0]), [(R() - 0.5) * 4, 2 + 3 * R(), (R() - 0.5) * 4], 0.9 + 0.6 * R(), 1.5, 3.5, 1.6);
      // sea spray under a low jet in afterburner
      const ab = inWin(a.ab, t);
      if (ab && fr.pos[1] < 45 && fr.pos[1] > 5 && R() < 0.9) {
        const tail = sub(fr.pos, mul(fr.f, (M.tail || 8) + 6));
        part(5, [tail[0] + (R() - 0.5) * 5, 0.5, tail[2] + (R() - 0.5) * 5], [(R() - 0.5) * 6, 3 + 5 * R(), (R() - 0.5) * 6], 0.8 + 0.6 * R(), 2, 4.5, 0.9);
      }
      // wingtip vapour trails
      if (inWin(a.trail, t)) {
        const tr = st.trails[a.id] || (st.trails[a.id] = []);
        const span = M === MODELS.nf24 ? 9.4 : 7;
        // a cut in the flight path (a new segment) would join two far-apart points with a long straight bar
        if (tr.length && dist(tr[tr.length - 1].l, add(fr.pos, mul(fr.r, -span))) > 40) tr.length = 0;
        tr.push({ t, l: add(fr.pos, mul(fr.r, -span)), r: add(fr.pos, mul(fr.r, span)) });
        while (tr.length && t - tr[0].t > 1.6) tr.shift();
      } else st.trails[a.id] = null;
    }
    // emitters: steam drifting off the catapult tracks (particles stay put in the air, so the deck slides out from under them)
    for (const e of S.emitters || []) {
      if (t < e.t0 || t > e.t1 || R() > e.rate * dt) continue;
      const sh = ACT[e.attach].ship, l = mix3(e.a, e.b, R());
      part(7, IC.shipLocal(sh, t, l), [(R() - 0.5) * 1.2, 0.6 + 1.0 * R(), (R() - 0.5) * 1.2], 1.0 + 0.8 * R(), 0.5, 1.3, 0.8);
    }
    // flares: smoke trail
    for (const fl of S.flares) { const p = IC.flarePos(fl, t); if (p && R() < 0.7) part(6, p, [(R() - 0.5) * 2, 1, (R() - 0.5) * 2], 1.6, 1.6, 3.5, 1.2); }
    // particles
    for (const q of st.parts) {
      const k = Math.exp(-q.d * dt);
      q.v = mul(q.v, k);
      if (q.k === 3) q.v[1] -= 9.81 * dt; else if (q.k === 5) q.v[1] -= 6 * dt; else if (q.k === 0 || q.k === 6) q.v[1] += 0.8 * dt;
      q.p = add(q.p, mul(q.v, dt)); q.s += q.g * dt; q.life -= dt;
    }
    st.parts = st.parts.filter(q => q.life > 0 && q.p[1] > -2);
  }
  function advance(to) {
    while (st.sim + FIXED <= to) { st.sim += FIXED; step(st.sim, FIXED); }
    st.t = to;
  }
  // ---------- camera ----------
  function shotAt(t) { for (const s of SHOTS) if (t >= s.t0 && t < s.t1) return s; return SHOTS[SHOTS.length - 1]; }
  function camAt(t, sh = shotAt(t), yawOff = 0) {
    const k = IC.sample(sh.tr, clamp(t, sh.t0, sh.cam[sh.cam.length - 1][0]), []) || sh.cam[sh.cam.length - 1];
    let pos = [k[1], k[2], k[3]], yaw = k[4], pitch = k[5], roll = k[6];
    // a camera bolted to a jet: rebuild it from the jet's own frame, so it cannot drift against the airframe
    const at = sh.attach && ACT[sh.attach.actor] ? actorFrame(ACT[sh.attach.actor], t) : null;
    if (at) {
      const o = sh.attach.offset, d = sh.attach.dir;
      pos = add(add(add(at.pos, mul(at.r, o[0])), mul(at.u, o[1])), mul(at.f, o[2]));
      const f = norm(add(add(mul(at.r, d[0]), mul(at.u, d[1])), mul(at.f, d[2])));
      const u = norm(sub(at.u, mul(f, dot(at.u, f))));
      [yaw, pitch, roll] = IC.angles(f, u);
    }
    // camera jitter: a hand, a gyro mount or an airframe, as a fraction of the field of view
    if (sh.vibe) {
      const a = sh.vibe.a * k[7], w = sh.vibe.f * Math.PI * 2;
      yaw += a * (0.6 * Math.sin(t * w) + 0.4 * Math.sin(t * w * 1.73 + 1.3));
      pitch += a * (0.6 * Math.sin(t * w * 1.21 + 2.1) + 0.4 * Math.sin(t * w * 2.37 + 0.4));
      roll += a * 0.5 * Math.sin(t * w * 0.83 + 4);
    }
    let shake = 0; for (const f of IC.active(S.fx, t)) if (f.type === 'shake') shake = Math.max(shake, f.amount * (1 - (t - f.t0) / (f.t1 - f.t0)));
    if (shake) { yaw += Math.sin(t * 71) * 0.9 * shake; pitch += Math.sin(t * 53 + 1) * 0.8 * shake; roll += Math.sin(t * 37 + 2) * 1.2 * shake; }
    yaw += yawOff;   // whip pan
    const fr = IC.frame(yaw, pitch, roll);
    return { pos, f: fr.f, r: fr.r, u: fr.u, fov: k[7], shot: sh };
  }
  // ---------- drawing ----------
  function project(V, w) { const c = toCam(V.cam, w); if (c[2] < NEAR) return null; return [V.cx + c[0] / c[2] * V.F, V.cy - c[1] / c[2] * V.F, c[2]]; }
  function addModel(V, M, pos, r, u, f, s = 1, opt = {}) {
    const cc = toCam(V.cam, pos), rad = M.rad * s;
    if (cc[2] < -rad) return;
    const dl = Math.hypot(cc[0], cc[1], cc[2]);
    if (dl > 40000) return;
    if (cc[2] > rad) {
      if (Math.abs(cc[0]) > V.vw / 2 / V.F * cc[2] + rad * 2) return;
      if (Math.abs(cc[1]) > V.sh / 2 / V.F * cc[2] + rad * 2) return;
      if (rad / cc[2] * V.F < 1.2) { V.list.push({ k: 1, z: cc[2], x: V.cx + cc[0] / cc[2] * V.F, y: V.cy - cc[1] / cc[2] * V.F, s: 1.5, c: css(fogged(opt.dot || [60, 60, 70], pos, V.cam)), a: 1 }); return; }
    }
    if (M.lod && rad / Math.max(cc[2], 1) * V.F < 60) M = M.lod;
    const wv = M.v.map(v => [pos[0] + (r[0] * v[0] + u[0] * v[1] + f[0] * v[2]) * s, pos[1] + (r[1] * v[0] + u[1] * v[1] + f[1] * v[2]) * s, pos[2] + (r[2] * v[0] + u[2] * v[1] + f[2] * v[2]) * s]);
    const cv = wv.map(w => toCam(V.cam, w));
    const fogT = clamp(1 - Math.exp(-dl / FOG_D), 0, 0.92), fc = fogCol(mul(sub(pos, V.cam.pos), 1 / (dl || 1)));
    const dim = opt.dim || 1;
    for (const face of M.f) {
      const idx = face.i;
      let pts = idx.map(i => cv[i]), behind = 0;
      for (const q of pts) if (q[2] < NEAR) behind++;
      if (behind === pts.length) continue;
      if (behind) pts = clipNear(pts);
      if (pts.length < 3) continue;
      const nm = face.n, n = [r[0] * nm[0] + u[0] * nm[1] + f[0] * nm[2], r[1] * nm[0] + u[1] * nm[1] + f[1] * nm[2], r[2] * nm[0] + u[2] * nm[1] + f[2] * nm[2]];
      let c = litFace(face.c, n);
      if (dim !== 1) c = mul(c, dim);
      c = mix3(c, fc, fogT);
      const P = []; let z = 0;
      for (const q of pts) { P.push(V.cx + q[0] / q[2] * V.F, V.cy - q[1] / q[2] * V.F); z += q[2]; }
      V.list.push({ k: 0, z: z / pts.length + (opt.zb || 0), p: P, c: css(c) });
    }
  }
  function addBall(V, w, r, c, a, add2 = false, zb = 0) {
    const cc = toCam(V.cam, w); if (cc[2] < NEAR) return;
    const pr = r / cc[2] * V.F; if (pr < 0.4) return;
    const x = V.cx + cc[0] / cc[2] * V.F, y = V.cy - cc[1] / cc[2] * V.F;
    if (x < V.vx - pr || x > V.vx + V.vw + pr || y < V.vy - pr || y > V.vy + V.sh + pr) return;
    V.list.push({ k: 3, z: cc[2] + zb, x, y, r: Math.min(pr, V.sh * 3), c, a, add: add2 });
  }
  function addSeg(V, a, b, col, wWorld, alpha = 1, minW = 1, add2 = false) {
    let ca = toCam(V.cam, a), cb = toCam(V.cam, b);
    if (ca[2] < NEAR && cb[2] < NEAR) return;
    if (ca[2] < NEAR) ca = cutNear(cb, ca); else if (cb[2] < NEAR) cb = cutNear(ca, cb);
    const z = (ca[2] + cb[2]) / 2, w = Math.max(minW, wWorld / z * V.F);
    V.list.push({ k: 2, z, x1: V.cx + ca[0] / ca[2] * V.F, y1: V.cy - ca[1] / ca[2] * V.F, x2: V.cx + cb[0] / cb[2] * V.F, y2: V.cy - cb[1] / cb[2] * V.F, c: col, w: Math.min(w, 60), a: alpha, add: add2 });
  }
  function drawItems(list) {
    list.sort((a, b) => b.z - a.z);
    ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    for (const it of list) {
      if (it.k === 0) {
        const p = it.p;
        ctx.fillStyle = it.c; ctx.strokeStyle = it.c; ctx.lineWidth = 0.6;
        ctx.beginPath(); ctx.moveTo(p[0], p[1]);
        for (let i = 2; i < p.length; i += 2) ctx.lineTo(p[i], p[i + 1]);
        ctx.closePath(); ctx.fill(); ctx.stroke();
      } else if (it.k === 1) {
        ctx.globalAlpha = it.a; ctx.fillStyle = it.c;
        const s = it.s; ctx.fillRect(Math.round(it.x - s / 2), Math.round(it.y - s / 2), Math.max(1, Math.round(s)), Math.max(1, Math.round(s)));
        ctx.globalAlpha = 1;
      } else if (it.k === 2) {
        if (it.add) ctx.globalCompositeOperation = 'lighter';
        ctx.globalAlpha = it.a; ctx.strokeStyle = it.c; ctx.lineWidth = it.w;
        ctx.beginPath(); ctx.moveTo(it.x1, it.y1); ctx.lineTo(it.x2, it.y2); ctx.stroke();
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
      } else if (it.k === 3) {
        if (it.add) ctx.globalCompositeOperation = 'lighter';
        ctx.globalAlpha = it.a; ctx.fillStyle = it.c;
        ctx.beginPath(); ctx.arc(it.x, it.y, Math.max(0.6, it.r), 0, Math.PI * 2); ctx.fill();
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
      } else if (it.k === 4) {
        // cloud puff: a soft rim, then a sphere shaded from its sunlit side to the shadowed base
        const r = it.r;
        const g = ctx.createRadialGradient(it.x + it.ox, it.y + it.oy, r * 0.05, it.x, it.y, r);
        g.addColorStop(0, it.c2); g.addColorStop(0.5, it.c); g.addColorStop(0.86, it.c3); g.addColorStop(1, it.c4);
        ctx.globalAlpha = it.a; ctx.fillStyle = g; ctx.beginPath(); ctx.arc(it.x, it.y, r, 0, Math.PI * 2); ctx.fill();
        ctx.globalAlpha = 1;
      }
    }
  }
  function horizon(V) {
    const { cam, cx, cy, F } = V;
    const gx = cam.r[1], gy = -cam.u[1], g0 = cam.f[1] * F, gl2 = gx * gx + gy * gy;
    let hx = cx, hy = cy, nx = 0, ny = -1;
    if (gl2 > 1e-6) { const gl = Math.sqrt(gl2); nx = gx / gl; ny = gy / gl; hx = cx - nx * g0 / gl; hy = cy - ny * g0 / gl; }
    return { hx, hy, nx, ny, gx, gy, g0 };
  }
  function drawSkySea(V, t) {
    const { vx, vy, vw, sh } = V, Hz = horizon(V);
    const big = Math.hypot(vw, sh);
    // sky: horizon glow to the deep dawn blue overhead
    const sg = ctx.createLinearGradient(Hz.hx, Hz.hy, Hz.hx + Hz.nx * big * 0.9, Hz.hy + Hz.ny * big * 0.9);
    sg.addColorStop(0, css(mix3(SKY_H, [255, 214, 170], 0.25))); sg.addColorStop(0.08, css(SKY_H)); sg.addColorStop(0.45, css(mix3(SKY_H, SKY_T, 0.75))); sg.addColorStop(1, css(mix3(SKY_T, [20, 26, 70], 0.5)));
    ctx.fillStyle = sg; ctx.fillRect(vx, vy, vw, sh);
    // sun glow and disc
    const sp = project(V, add(V.cam.pos, mul(SUN, 50000)));
    if (sp) {
      const R0 = sh * 0.9, g = ctx.createRadialGradient(sp[0], sp[1], 0, sp[0], sp[1], R0);
      g.addColorStop(0, 'rgba(255,236,200,0.95)'); g.addColorStop(0.06, 'rgba(255,200,140,0.55)'); g.addColorStop(0.3, 'rgba(255,160,110,0.18)'); g.addColorStop(1, 'rgba(255,140,100,0)');
      ctx.fillStyle = g; ctx.fillRect(vx, vy, vw, sh);
      // the disc at its real size (0.53 degrees, a little more for the eye): huge through a long lens, a dot on a wide one
      const dr = Math.max(V.F * 0.0075, sh * 0.012), low = clamp(1 - SUN[1] * 10, 0, 1);
      const dg = ctx.createRadialGradient(sp[0], sp[1], 0, sp[0], sp[1], dr);
      dg.addColorStop(0, '#fff8e8'); dg.addColorStop(0.7, low > 0.3 ? '#ffd9a0' : '#fff2d8'); dg.addColorStop(1, low > 0.3 ? '#ffb070' : '#ffe6c0');
      ctx.fillStyle = dg; ctx.beginPath(); ctx.arc(sp[0], sp[1], dr, 0, Math.PI * 2); ctx.fill();
    }
    // sea polygon below the horizon
    const val = c => Hz.gx * (c[0] - V.cx) + Hz.gy * (c[1] - V.cy) + Hz.g0;
    const corners = [[vx, vy], [vx + vw, vy], [vx + vw, vy + sh], [vx, vy + sh]], poly = [];
    for (let i = 0; i < 4; i++) {
      const a = corners[i], b = corners[(i + 1) % 4], va = val(a), vb = val(b);
      if (va < 0) poly.push(a);
      if ((va < 0) !== (vb < 0)) { const tt = va / (va - vb); poly.push([a[0] + (b[0] - a[0]) * tt, a[1] + (b[1] - a[1]) * tt]); }
    }
    if (poly.length >= 3) {
      const down = V.cam.pos[1];
      const gg = ctx.createLinearGradient(Hz.hx, Hz.hy, Hz.hx - Hz.nx * big * 0.8, Hz.hy - Hz.ny * big * 0.8);
      const hzc = mix3(FOGC, SKY_H, 0.35);
      gg.addColorStop(0, css(mix3(hzc, WATER, 0.25))); gg.addColorStop(0.04, css(mix3(hzc, WATER, 0.6))); gg.addColorStop(0.25, css(mix3(WATER, SKY_T, 0.15))); gg.addColorStop(1, css(mix3(WATER, [10, 24, 48], 0.35)));
      ctx.fillStyle = gg; ctx.beginPath(); ctx.moveTo(poly[0][0], poly[0][1]);
      for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i][0], poly[i][1]);
      ctx.closePath(); ctx.fill();
      // sun glitter: a column of flickering dashes from the reflection point toward the viewer
      const mp = project(V, add(V.cam.pos, mul([SUN[0], -SUN[1], SUN[2]], 50000)));
      if (mp) {
        ctx.save(); ctx.beginPath(); ctx.moveTo(poly[0][0], poly[0][1]); for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i][0], poly[i][1]); ctx.closePath(); ctx.clip();
        ctx.globalCompositeOperation = 'lighter';
        const R = rng(Math.floor(t * 12)), along = [-Hz.nx, -Hz.ny], side = [-Hz.ny, Hz.nx];
        const hd = Math.abs(Hz.gx * (mp[0] - V.cx) + Hz.gy * (mp[1] - V.cy) + Hz.g0) / Math.sqrt(Hz.gx * Hz.gx + Hz.gy * Hz.gy || 1);
        const gs = ctx.createRadialGradient(mp[0], mp[1], 0, mp[0], mp[1], sh * 0.5);
        gs.addColorStop(0, 'rgba(255,200,140,0.5)'); gs.addColorStop(1, 'rgba(255,160,110,0)');
        ctx.fillStyle = gs; ctx.fillRect(vx, vy, vw, sh);
        for (let i = 0; i < 160; i++) {
          const d = Math.pow(R(), 1.7) * big * 0.9, wdt = (3 + d * 0.12) * (0.4 + R()), off = (R() - 0.5) * (8 + d * 0.35 + hd * 0.3);
          const x = mp[0] + along[0] * d + side[0] * off, y = mp[1] + along[1] * d + side[1] * off;
          ctx.globalAlpha = 0.25 + 0.5 * R() * (1 - d / big);
          ctx.fillStyle = R() < 0.5 ? '#ffe6b8' : '#ffc890';
          ctx.fillRect(x - side[0] * wdt / 2, y, wdt, Math.max(1, 1 + d * 0.006));
        }
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over'; ctx.restore();
      }
      drawWater(V, t);
    }
  }
  // whitecaps on a hashed grid around the camera; two scales so both a low pass and a top-down shot show speed
  const WIND = norm([Math.sin(250 * D2R_), 0, Math.cos(250 * D2R_)]);
  function hash2(i, j, s) { let x = (Math.imul(i, 73856093) ^ Math.imul(j, 19349663) ^ Math.imul(s, 83492791)) >>> 0; x = Math.imul(x ^ (x >>> 13), 1274126177) >>> 0; x ^= x >>> 16; return x >>> 0; }
  function drawWater(V, t) {
    const cam = V.cam, hgt = Math.max(4, cam.pos[1]);
    const scales = [clamp(hgt * 0.07, 1.5, 30), clamp(hgt * 0.3, 4, 80), clamp(hgt * 1.2, 14, 300)];
    ctx.lineCap = 'round';
    scales.forEach((s, si) => {
      const R = si === 0 ? 30 : 34, ci = Math.floor(cam.pos[0] / s), cj = Math.floor(cam.pos[2] / s);
      const paths = [new Path2D(), new Path2D(), new Path2D()];
      for (let j = -R; j <= R; j++) for (let i = -R; i <= R; i++) {
        const h = hash2(ci + i, cj + j, si + 7);
        if ((h & 255) > 52) continue;
        const x = (ci + i + ((h >>> 8) & 255) / 255) * s, z = (cj + j + ((h >>> 16) & 255) / 255) * s;
        const ph = ((h >>> 24) & 255) / 255, life = (t * 0.35 + ph) % 1;
        if (life > 0.65) continue;
        const L = s * (0.25 + 0.35 * Math.sin(life / 0.65 * Math.PI));
        const a = [x - WIND[0] * L * 0.5, 0, z - WIND[2] * L * 0.5], b = [x + WIND[0] * L * 0.5, 0, z + WIND[2] * L * 0.5];
        let ca = toCam(cam, a), cb = toCam(cam, b);
        if (ca[2] < NEAR || cb[2] < NEAR) continue;
        const sx1 = V.cx + ca[0] / ca[2] * V.F, sy1 = V.cy - ca[1] / ca[2] * V.F, sx2 = V.cx + cb[0] / cb[2] * V.F, sy2 = V.cy - cb[1] / cb[2] * V.F;
        if ((sx1 < V.vx && sx2 < V.vx) || (sx1 > V.vx + V.vw && sx2 > V.vx + V.vw) || (sy1 < V.vy && sy2 < V.vy) || (sy1 > V.vy + V.sh && sy2 > V.vy + V.sh)) continue;
        const dd = (ca[2] + cb[2]) / 2 / (s * R);
        paths[dd < 0.3 ? 0 : dd < 0.6 ? 1 : 2].moveTo(sx1, sy1); paths[dd < 0.3 ? 0 : dd < 0.6 ? 1 : 2].lineTo(sx2, sy2);
      }
      const al = si === 2 ? [0.3, 0.18, 0.08] : si === 1 ? [0.45, 0.28, 0.12] : [0.55, 0.34, 0.15];
      for (let k = 0; k < 3; k++) { ctx.strokeStyle = `rgba(236,232,240,${al[k]})`; ctx.lineWidth = k === 0 && si === 0 ? 1.5 : 1; ctx.stroke(paths[k]); }
    });
  }
  function flat(V, pts, col, alpha) {
    let c = pts.map(w => toCam(V.cam, w));
    if (c.every(q => q[2] < NEAR)) return;
    c = clipNear(c); if (c.length < 3) return;
    ctx.globalAlpha = alpha; ctx.fillStyle = col; ctx.beginPath();
    c.forEach((q, i) => { const x = V.cx + q[0] / q[2] * V.F, y = V.cy - q[1] / q[2] * V.F; i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
    ctx.closePath(); ctx.fill(); ctx.globalAlpha = 1;
  }
  function drawWakes(V, t) {
    for (const a of S.actors) {
      if (a.kind !== 'ship' || !a.wake) continue;
      const sh = a.ship, L = a.model === 'cvn90' ? 160 : 62, W0 = a.model === 'cvn90' ? 22 : 7, k = a.wake;
      const P = (lx, lz) => { const p = IC.shipLocal(sh, t, [lx, 0.2, lz]); return p; };
      if (dist(P(0, 0), V.cam.pos) > 30000) continue;
      const segs = 7, len0 = 1100 * k;
      for (let i = 0; i < segs; i++) {
        const z0 = -L - len0 * i / segs, z1 = -L - len0 * (i + 1) / segs, w0 = W0 + (i / segs) * 120 * k, w1 = W0 + ((i + 1) / segs) * 120 * k;
        flat(V, [P(-w0, z0), P(w0, z0), P(w1, z1), P(-w1, z1)], '#e8eef4', 0.34 * (1 - i / segs));
        flat(V, [P(-w0 * 0.35, z0), P(w0 * 0.35, z0), P(w1 * 0.25, z1), P(-w1 * 0.25, z1)], '#ffffff', 0.4 * (1 - i / segs));
      }
      // bow wave
      flat(V, [P(-W0 * 1.1, L * 0.7), P(0, L + 4), P(W0 * 1.1, L * 0.7), P(W0 * 1.6, L * 0.4), P(-W0 * 1.6, L * 0.4)], '#f2f6fa', 0.35);
    }
  }
  function drawTerrain(V) {
    for (const tr of TRI) {
      const dx = tr.mid[0] - V.cam.pos[0], dz = tr.mid[2] - V.cam.pos[2];
      if (dx * V.cam.f[0] + dz * V.cam.f[2] < -900 * Math.hypot(V.cam.f[0], V.cam.f[2]) || dx * dx + dz * dz > 32000 * 32000) continue;
      const c = tr.p.map(w => toCam(V.cam, w));
      if (c[0][2] < NEAR && c[1][2] < NEAR && c[2][2] < NEAR) continue;
      let pts = c; if (c.some(q => q[2] < NEAR)) pts = clipNear(c); if (pts.length < 3) continue;
      const P = []; let z = 0, minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
      for (const q of pts) { const x = V.cx + q[0] / q[2] * V.F, y = V.cy - q[1] / q[2] * V.F; P.push(x, y); z += q[2]; if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
      if (maxx < V.vx || minx > V.vx + V.vw || maxy < V.vy || miny > V.vy + V.sh) continue;
      V.list.push({ k: 0, z: z / pts.length, p: P, c: css(fogged(tr.c, tr.mid, V.cam)) });
    }
  }
  function drawClouds(V) {
    let inside = 0;
    for (const q of PUFFS) {
      const d = sub(q.w, V.cam.pos), dl = len(d);
      if (dl < q.r) inside = Math.max(inside, 1 - dl / q.r);
      if (dl > 34000) continue;
      const cc = toCam(V.cam, q.w);
      if (cc[2] < -q.r) continue;
      if (cc[2] < NEAR + q.r * 0.3) continue;
      const pr = q.r / cc[2] * V.F;
      if (pr < 1) continue;
      const x = V.cx + cc[0] / cc[2] * V.F, y = V.cy - cc[1] / cc[2] * V.F;
      if (x < V.vx - pr * 1.2 || x > V.vx + V.vw + pr * 1.2 || y < V.vy - pr * 1.2 || y > V.vy + V.sh + pr * 1.2) continue;
      const fogT = clamp(1 - Math.exp(-dl / (FOG_D * 1.6)), 0, 0.85);
      const col = mix3(q.c, fogCol(mul(d, 1 / dl)), fogT);
      // a puff about to swallow the camera thins out instead of showing its edge across the frame
      const near = clamp((dl - q.r) / (q.r * 1.5), 0, 1);
      if (near <= 0.02) continue;
      // a lit cap toward the sun
      const sc = toCam(V.cam, add(q.w, mul(norm(add(SUN, [0, 0.6, 0])), q.r * 0.6)));
      const ox = sc[2] > NEAR ? (V.cx + sc[0] / sc[2] * V.F) - x : 0, oy = sc[2] > NEAR ? (V.cy - sc[1] / sc[2] * V.F) - y : -pr * 0.3;
      const fc = fogCol(mul(d, 1 / dl));
      const hi = mix3(mix3(q.c, [255, 240, 222], 0.6), fc, fogT * 0.6), lo = mix3(mix3(q.c, [110, 108, 146], 0.35), fc, fogT);
      V.list.push({ k: 4, z: cc[2], x, y, r: Math.min(pr, V.sh * 4), c: css(col), c2: css(hi), c3: css(lo), c4: css(lo, 0), ox: clamp(ox, -pr * 0.45, pr * 0.45), oy: clamp(oy, -pr * 0.45, pr * 0.45), a: near });
    }
    return inside;
  }
  // L(fr, x, y, z): a point in an actor's own frame (x right, y up, z forward) in world space
  const L = (fr, x, y, z) => [fr.pos[0] + fr.r[0] * x + fr.u[0] * y + fr.f[0] * z, fr.pos[1] + fr.r[1] * x + fr.u[1] * y + fr.f[1] * z, fr.pos[2] + fr.r[2] * x + fr.u[2] * y + fr.f[2] * z];
  // wing vapour: humid air condensing over the wings and in the leading-edge vortices under a hard pull
  function drawVapour(V, a, M, fr, t, lv) {
    const sc = M === MODELS.nf24 ? 1 : 0.75, fl = 0.75 + 0.25 * Math.sin(t * 43 + a.id.length * 2) * Math.sin(t * 27);
    for (const sd of [-1, 1]) {
      for (const dz of [1.4, 0, -1.4]) addSeg(V, L(fr, sd * 1.8 * sc, 0.75, (0.6 + dz) * sc), L(fr, sd * 8.4 * sc, 0.45, (-2.4 + dz * 0.6) * sc), '#f6f4f8', 1.5 * sc, 0.2 * lv * fl, 1);
      for (const x of [3, 5.5, 8]) addSeg(V, L(fr, sd * x * sc, 0.55, -3 * sc), L(fr, sd * x * sc, 0.7, (-8 - x * 0.3) * sc), '#f2f0f4', 0.7 * sc, 0.16 * lv * fl, 1);
      addSeg(V, L(fr, sd * 1.3 * sc, 0.95, 4.6 * sc), L(fr, sd * 2.6 * sc, 1.15, -6 * sc), '#ffffff', 0.45 * sc, 0.38 * lv * fl, 1);
    }
  }
  // vapour cone: the shock collar of a fast pass through humid air, a bright front edge and a bell of mist behind it
  function drawCone(V, M, fr, t, lv) {
    const sc = M === MODELS.nf24 ? 1 : 0.8, fl = 0.8 + 0.2 * Math.sin(t * 57);
    for (let i = 0; i < 8; i++) { const z = (1.6 - i) * sc, r = (1.9 + i * 0.36) * sc; addBall(V, L(fr, 0, 0.5, z), r, '#f4f4f8', 0.12 * lv * fl * (1 - i / 10)); }
    let prev = null;
    for (let i = 0; i <= 18; i++) {
      const a = i / 18 * Math.PI * 2, p = L(fr, Math.cos(a) * 2.1 * sc, 0.5 + Math.sin(a) * 2.1 * sc, 1.7 * sc);
      if (prev) addSeg(V, prev, p, '#ffffff', 0.25 * sc, 0.4 * lv * fl, 1);
      prev = p;
    }
  }
  // deck crew: a figure from a pose (S.poses), drawn as thick strokes so it reads as a silhouette against the sun
  function poseAt(a, t) {
    const P = a.poses; let i = 0;
    while (i + 1 < P.length && P[i + 1][0] <= t) i++;
    const cur = S.poses[P[i][1]], prev = i ? S.poses[P[i - 1][1]] : cur, k = clamp((t - P[i][0]) / 0.25, 0, 1);
    return cur.map((v, j) => prev[j] + (v - prev[j]) * k);
  }
  function drawCrew(V, a, t) {
    const sh = ACT[a.attach].ship, s = IC.sample(a.tr, clamp(t, a.keys[0][0], a.keys[a.keys.length - 1][0]), tmp); if (!s) return;
    const base = IC.shipLocal(sh, t, [s[1], s[2], s[3]]);
    const cc = toCam(V.cam, base); if (cc[2] < 0.3 || cc[2] > 1600) return;
    if (2 / cc[2] * V.F < 2) return;
    const fr0 = IC.frame(sh.heading + s[4], 0, 0), fr = { pos: base, r: fr0.r, u: [0, 1, 0], f: [fr0.f[0], 0, fr0.f[2]] };
    const [cr, tp, le, la, lb, re, ra, rb, wk, circ] = poseAt(a, t);
    const toward = norm(sub(V.cam.pos, base));
    const shade = h => css(litFace(hex2rgb(h), toward).map(v => v * 0.9));
    const jer = shade(a.jersey), hel = shade(a.helmet), trou = shade('#4a4a50');
    const D = Math.PI / 180, hipY = 0.95 - 0.42 * cr, hip = [0, hipY, -0.1 * cr];
    const tpr = tp * D, neck = [0, hipY + 0.55 * Math.cos(tpr), hip[2] + 0.55 * Math.sin(tpr)], head = [0, hipY + 0.78 * Math.cos(tpr), hip[2] + 0.78 * Math.sin(tpr)];
    const W3 = p => L(fr, p[0], p[1], p[2]);
    const ph = t * 6.5 + a.id.length;
    for (const sd of [-1, 1]) {
      const sw = Math.sin(ph + (sd > 0 ? 0 : Math.PI)) * wk;
      const foot = [sd * 0.13, 0.03 + Math.max(0, sw) * 0.07, sw * 0.28 + (cr > 0.5 ? (sd > 0 ? 0.25 : -0.35) : 0)];
      const knee = [sd * 0.12, hipY * 0.5 + 0.05 * cr, (hip[2] + foot[2]) / 2 + 0.06 + 0.38 * cr];
      addSeg(V, W3([sd * 0.1, hipY, hip[2]]), W3(knee), trou, 0.17, 1, 1); addSeg(V, W3(knee), W3(foot), trou, 0.15, 1, 1);
      const e = (sd < 0 ? le : re) * D, az = (sd < 0 ? la : ra) * D, b = (sd < 0 ? lb : rb) * D;
      const shp = [sd * 0.19, neck[1] - 0.05, neck[2]];
      const d = [sd * Math.sin(e) * Math.sin(az), -Math.cos(e), Math.sin(e) * Math.cos(az)];
      let pp = sub([0, 1, 0], mul(d, d[1])); pp = len(pp) < 0.2 ? [0, 0, 1] : norm(pp);
      const elbow = add(shp, mul(d, 0.3)), fd = add(mul(d, Math.cos(b)), mul(pp, Math.sin(b)));
      let hand = add(elbow, mul(fd, 0.28));
      if (sd > 0 && circ) hand = add(hand, [Math.cos(t * 13) * 0.09 * circ, Math.sin(t * 13) * 0.09 * circ, 0]);
      addSeg(V, W3(shp), W3(elbow), jer, 0.13, 1, 1); addSeg(V, W3(elbow), W3(hand), jer, 0.11, 1, 1);
    }
    addSeg(V, W3([0, hipY + 0.05, hip[2]]), W3(neck), jer, 0.42, 1, 1);
    addBall(V, W3(head), 0.14, hel, 1, false, -0.05);
  }
  function drawActors(V, t) {
    for (const a of S.actors) {
      if (a.kind === 'missile') {
        const fr = actorFrame(a, t); if (!fr) continue;
        addSeg(V, add(fr.pos, mul(fr.f, 1.8)), sub(fr.pos, mul(fr.f, 1.8)), '#e8e8ec', 0.35, 1);
        addBall(V, sub(fr.pos, mul(fr.f, 2.2)), 0.7, '#ffe6a0', 1, true);
        addBall(V, sub(fr.pos, mul(fr.f, 2.4)), 1.0, 'rgba(255,170,80,0.6)', 0.5, true);
        continue;
      }
      if (a.kind === 'crew') { drawCrew(V, a, t); continue; }
      if (V.skip === a.id) continue;   // the jet we are sitting in
      const fr = actorFrame(a, t); if (!fr) continue;
      const M = MODELS[a.model]; if (!M) continue;
      const dead = a.dead != null && t >= a.dead;
      addModel(V, M, fr.pos, fr.r, fr.u, fr.f, 1, { dim: dead ? 0.35 : 1, dot: a.team ? [90, 40, 40] : [40, 50, 80] });
      if (a.kind !== 'jet' || dead) continue;
      const ab = inWin(a.ab, t);
      if (ab) {
        const tail = M.tail || 8, fl = 0.85 + 0.3 * Math.sin(t * 61 + a.id.length) * Math.sin(t * 37);
        for (const sx of [-0.85, 0.85]) {
          const n0 = add(add(fr.pos, mul(fr.f, -tail + 0.2)), mul(fr.r, sx)), n1 = add(n0, mul(fr.f, -(3 + 4 * ab) * fl));
          addSeg(V, n0, n1, '#ff9a3a', 0.85, 0.8 * ab, 1, true);
          addSeg(V, n0, add(n0, mul(fr.f, -(2 + 2.2 * ab) * fl)), '#fff2c8', 0.45, 0.95, 1, true);
          addBall(V, n0, 0.9 * ab, 'rgba(255,190,110,0.55)', 0.45, true);
        }
      }
      const vap = inWin(a.vapour, t), cone = inWin(a.cone, t);
      if (V.own === a.id) continue;   // the camera is bolted to this jet: its vapour would smear across the lens
      if (vap) drawVapour(V, a, M, fr, t, vap);
      if (cone) drawCone(V, M, fr, t, cone);
    }
    // vapour trails
    for (const id in st.trails) {
      const tr = st.trails[id]; if (!tr || tr.length < 2 || id === V.own) continue;
      for (let i = 1; i < tr.length; i++) {
        const al = 0.55 * (1 - (st.t - tr[i].t) / 1.6);
        if (al <= 0) continue;
        addSeg(V, tr[i - 1].l, tr[i].l, '#f4f2f6', 0.5, al, 1); addSeg(V, tr[i - 1].r, tr[i].r, '#f4f2f6', 0.5, al, 1);
      }
    }
    // flares
    for (const fl of S.flares) {
      const p = IC.flarePos(fl, t); if (!p) continue;
      const k = 1 - (t - fl.t) / fl.life;
      addBall(V, p, 1.2 + 0.6 * Math.sin(t * 50 + fl.t), '#ffffff', 1, true);
      addBall(V, p, 2.5 * k + 1.2, 'rgba(255,230,170,0.5)', 0.7 * k, true);
    }
    for (const b of st.tracers) if (dist(b.p, V.cam.pos) > 30) addSeg(V, b.p, sub(b.p, mul(norm(b.v), 14)), '#ffe680', 0.25, 0.9, 1, true);
    // particles
    for (const q of st.parts) {
      const P = PK[q.k], u = 1 - q.life / q.max, c = mix3(P.c0, P.c1, u);
      let a = P.a * (q.k === 1 || q.k === 2 ? 1 - u : (1 - u) * Math.min(1, u * 6 + 0.3));
      if (q.k === 0 || q.k === 6) { const lit = litFace([1, 1, 1], SUN); for (let i = 0; i < 3; i++) c[i] = Math.min(255, c[i] * (0.75 + 0.35 * lit[i])); }
      addBall(V, q.p, q.s, css(c), a, q.k === 1 || q.k === 2);
    }
  }
  // ---------- 2D overlay: cards, effects ----------
  function typed(c, t) { if (!c.type) return c.text; const n = Math.floor((t - c.t0) * c.type); return c.text.slice(0, Math.max(0, n)); }
  function drawOverlay(V, t, inside) {
    const { vx, vy, vw, sh } = V;
    const fxs = IC.active(S.fx, t);
    // cloud white-out
    if (inside > 0) { ctx.fillStyle = `rgba(246,236,228,${clamp(inside * 1.6, 0, 0.82)})`; ctx.fillRect(0, 0, W, H); }
    // lens flare when the sun is on screen
    const sp = project(V, add(V.cam.pos, mul(SUN, 50000)));
    if (sp && sp[0] > vx && sp[0] < vx + vw && sp[1] > vy && sp[1] < vy + sh && inside < 0.3) {
      ctx.globalCompositeOperation = 'lighter';
      const dx = V.cx - sp[0], dy = V.cy - sp[1];
      [[0.6, 0.03, 'rgba(255,170,120,0.08)'], [1.25, 0.06, 'rgba(255,140,90,0.05)'], [1.6, 0.018, 'rgba(160,200,255,0.08)']].forEach(([k, r, c]) => {
        ctx.fillStyle = c; ctx.beginPath(); ctx.arc(sp[0] + dx * k, sp[1] + dy * k, sh * r, 0, Math.PI * 2); ctx.fill();
      });
      ctx.globalCompositeOperation = 'source-over';
    }
    let lb = 0;
    for (const f of S.fx) if (f.type === 'letterbox' && t >= f.t0) lb = t < f.t1 ? (f.from != null ? lerp(f.from, f.to, ease((t - f.t0) / (f.t1 - f.t0))) : f.amount) : (f.to != null ? f.to : lb);
    const barH = Math.round(Math.max(0, (H - W / 2.35) / 2, H * 0.09) * lb);
    V.barH = barH;
    for (const f of fxs) {
      const u = (t - f.t0) / (f.t1 - f.t0);
      if (f.type === 'alarm') {
        const k = 0.5 + 0.5 * Math.sin((t - f.t0) / f.period * Math.PI * 2 - Math.PI / 2);
        const g = ctx.createRadialGradient(W / 2, H / 2, Math.min(W, H) * 0.25, W / 2, H / 2, Math.max(W, H) * 0.75);
        g.addColorStop(0, 'rgba(255,30,20,0)'); g.addColorStop(1, `rgba(255,30,20,${(0.45 * k).toFixed(3)})`);
        ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
      } else if (f.type === 'radar') drawRadar(V, t, f, u);
    }
    for (const f of S.fx) if (f.type === 'smoketitle' && t >= f.form[0]) drawSmokeTitle(V, f, t);
    for (const f of fxs) if (f.type === 'grade') drawGrade(f, t);
    for (const f of fxs) if (f.type === 'flash') { const a = lerp(f.from, f.to, (t - f.t0) / (f.t1 - f.t0)); if (a > 0) { ctx.fillStyle = f.color; ctx.globalAlpha = clamp(a, 0, 1); ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1; } }
    // letterbox
    if (barH > 0) { ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, barH); ctx.fillRect(0, H - barH, W, barH); }
    // cards
    const u0 = Math.min(W, H * 1.6);
    for (const c of IC.active(S.cards, t)) {
      if (c.blink && Math.floor((t - c.t0) / c.blink) % 2 === 1) continue;
      if (st.menuOpen && (c.style === 'title' || c.style === 'press')) continue;
      const fade = c.t1 < 1e8 ? clamp((c.t1 - t) / 0.5, 0, 1) : 1;
      ctx.globalAlpha = fade;
      const s = typed(c, t);
      if (c.style === 'location') {
        const sz = Math.round(u0 * 0.042), y = H - barH - sh * 0.2;
        txt(s, W / 2, y, sz, '#fbe9cc', 'center');
        const full = (ctx.font = `${sz}px ${FONT}`, ctx.measureText(c.text).width), k = clamp((t - c.t0) / 1.2, 0, 1);
        ctx.fillStyle = 'rgba(251,233,204,0.8)'; ctx.fillRect(W / 2 - full / 2 * k, y + sz * 0.8, full * k, Math.max(1, sz * 0.08));
      } else if (c.style === 'sub') txt(s, W / 2, H - barH - sh * 0.2 + u0 * 0.07, Math.round(u0 * 0.02), '#e2c49e', 'center');
      else if (c.style === 'alert') {
        const sz = Math.round(u0 * 0.06), y = barH + sh * 0.26;
        ctx.font = `${sz}px ${FONT}`; const w = ctx.measureText(s).width;
        ctx.strokeStyle = '#ff4a3a'; ctx.lineWidth = Math.max(2, sz * 0.08); ctx.strokeRect(W / 2 - w / 2 - sz * 0.5, y - sz * 0.85, w + sz, sz * 1.7);
        txt(s, W / 2, y, sz, '#ff4a3a', 'center');
      } else if (c.style === 'alert-sub') txt(s, W / 2, barH + sh * 0.26 + u0 * 0.07, Math.round(u0 * 0.022), '#ffd25a', 'center');
      else if (c.style === 'callsign') txt(s, Math.max(12, W * 0.04), barH ? H - barH / 2 : H - u0 * 0.05, Math.round(Math.min(u0 * 0.02, (barH || 40) * 0.42)), HUDC, 'left');
      else if (c.style === 'radio') txt('"' + s + '"', W / 2, barH ? H - barH / 2 : H - u0 * 0.06, Math.round(Math.min(u0 * 0.024, (barH || 40) * 0.48)), '#ffffff', 'center');
      else if (c.style === 'title') drawTitle(t - c.t0);
      else if (c.style === 'subtitle') { ctx.globalAlpha = clamp((t - c.t0) / 1.2, 0, 1); txt(s, W / 2, H * 0.42 + u0 * 0.115, Math.max(8, Math.round(u0 * 0.024)), '#fbe9cc', 'center'); }
      else if (c.style === 'press') txt(s, W / 2, H * 0.5 + u0 * 0.11, Math.round(u0 * 0.022), '#fbe9cc', 'center');
      ctx.globalAlpha = 1;
    }
    for (const f of fxs) {
      const u = (t - f.t0) / (f.t1 - f.t0);
      if (f.type === 'fade') { const a = lerp(f.from, f.to, u); if (a > 0) { ctx.fillStyle = f.color; ctx.globalAlpha = clamp(a, 0, 1); ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1; } }
    }
    if (mode === 'play' && t < 5) { ctx.globalAlpha = clamp((5 - t) / 1.5, 0, 0.7); txt('ANY KEY SKIPS', W - Math.max(10, W * 0.03), barH ? barH / 2 : 14, Math.max(8, Math.round(u0 * 0.014)), '#cdc6ff', 'right'); ctx.globalAlpha = 1; }
  }
  function drawTitle(dt) {
    const u0 = Math.min(W, H * 1.6), k = clamp(dt / 0.22, 0, 1), sc = lerp(1.9, 1, 1 - Math.pow(1 - k, 3));
    const sz = Math.round(u0 * 0.12 * sc), x = W / 2, y = H * 0.44, off = Math.max(2, Math.round(sz * 0.07));
    ctx.font = `700 ${sz}px ${FONT}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.globalAlpha = k;
    ctx.fillStyle = 'rgba(0,0,0,0.45)'; ctx.fillText(GAME_LOGO, x + off * 2, y + off * 2);
    ctx.fillStyle = '#6C5EB5'; ctx.fillText(GAME_LOGO, x + off, y + off);
    const g = ctx.createLinearGradient(0, y - sz / 2, 0, y + sz / 2);
    g.addColorStop(0, '#ffffff'); g.addColorStop(0.55, '#fff1d8'); g.addColorStop(1, '#ffb877');
    ctx.fillStyle = g; ctx.fillText(GAME_LOGO, x, y);
    // glint sweeping across once
    const gx = (dt - 0.3) / 0.9;
    if (gx > 0 && gx < 1) {
      ctx.save(); ctx.globalCompositeOperation = 'source-atop';
      ctx.restore();
      const w = ctx.measureText(GAME_LOGO).width, px = x - w / 2 + w * gx;
      ctx.globalCompositeOperation = 'lighter'; ctx.fillStyle = 'rgba(255,255,255,0.14)';
      ctx.beginPath(); ctx.moveTo(px - sz * 0.2, y - sz * 0.55); ctx.lineTo(px + sz * 0.05, y - sz * 0.55); ctx.lineTo(px - sz * 0.15, y + sz * 0.55); ctx.lineTo(px - sz * 0.4, y + sz * 0.55); ctx.fill();
      ctx.globalCompositeOperation = 'source-over';
    }
    txt(GAME_SUBTITLE, x, y + sz * 0.62, Math.max(8, Math.round(u0 * 0.02)), '#cdc6ff', 'center');
    ctx.globalAlpha = 1;
  }
  // ---------- smoke title: the last kill's fire and smoke drift together into a cloud that spells the text ----------
  // Puffs are sampled from the text rasterized at screen centre. Each starts in the fireball (the kill position
  // projected through the camera), waits a random delay, then eases along a curl to its place in the letters while it
  // turns from fire to smoke to sunlit cloud (warm on top, lavender in shadow). Once formed it keeps billowing.
  let ST = null;
  const SPR = {};
  function smokeSprite(c) {
    const key = c.join(',');
    if (SPR[key]) return SPR[key];
    const cv = document.createElement('canvas'); cv.width = cv.height = 48;
    const g = cv.getContext('2d'), gr = g.createRadialGradient(24, 24, 0, 24, 24, 24);
    gr.addColorStop(0, css(c, 1)); gr.addColorStop(0.45, css(c, 0.75)); gr.addColorStop(1, css(c, 0));
    g.fillStyle = gr; g.fillRect(0, 0, 48, 48);
    return (SPR[key] = cv);
  }
  function smokeLayout(f) {
    const ready = !document.fonts || document.fonts.check(`700 20px ${FONT}`);
    const key = W + 'x' + H + (ready ? 'f' : '') + f.text;
    if (ST && ST.key === key) return ST;
    const u0 = Math.min(W, H * 1.6), m = document.createElement('canvas').getContext('2d');
    m.font = `700 100px ${FONT}`;
    const sz = Math.max(24, Math.round(Math.min(u0 * 0.14, 100 * W * 0.84 / m.measureText(f.text).width)));
    const cv = document.createElement('canvas'); cv.width = W; cv.height = Math.ceil(sz * 1.6);
    const g = cv.getContext('2d'); g.font = `700 ${sz}px ${FONT}`; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillStyle = '#fff';
    g.fillText(f.text, W / 2, cv.height / 2);
    const im = g.getImageData(0, 0, cv.width, cv.height).data, step = Math.max(3, Math.round(sz / 15)), R = rng(f.seed || 1);
    const pts = []; let y0 = 1e9, y1 = -1e9;
    for (let y = 0; y < cv.height; y += step) for (let x = 0; x < cv.width; x += step) if (im[(y * cv.width + x) * 4 + 3] > 128) { y0 = Math.min(y0, y); y1 = Math.max(y1, y); pts.push([x, y]); }
    const cy = H * 0.42 - cv.height / 2;
    ST = { key, step, puffs: pts.map(([x, y]) => ({
      x: x + (R() - 0.5) * step * 0.8, y: cy + y + (R() - 0.5) * step * 0.8, lit: 1 - (y - y0) / Math.max(1, y1 - y0),
      d: R(), sx: (R() - 0.5), sy: (R() - 0.5), curl: (R() < 0.5 ? -1 : 1) * (0.3 + 0.7 * R()), ph: R() * 6.283, s: 0.85 + 0.4 * R()
    })) };
    return ST;
  }
  const FIRE0 = [255, 236, 170], FIRE1 = [255, 128, 50], SMOKE = [118, 104, 112], CLOUD_LIT = [255, 242, 228], CLOUD_SH = [196, 180, 210], CLOUD_DEEP = [150, 138, 176];
  function drawSmokeTitle(V, f, t) {
    const L = smokeLayout(f), span = f.form[1] - f.form[0], st0 = Math.min(W, H) * 0.06;
    const ep = project(V, f.pos), ex = ep ? clamp(ep[0], -W * 0.2, W * 1.2) : W / 2, ey = ep ? clamp(ep[1], -H * 0.2, H * 1.2) : H * 0.55;
    const q = v => Math.round(v / 12) * 12;   // colours are quantized so a handful of sprites cover every puff
    for (const p of L.puffs) {
      const e0 = clamp((t - f.form[0] - p.d * span * 0.4) / (span * 0.6), 0, 1), e = e0 < 0.5 ? 4 * e0 * e0 * e0 : 1 - Math.pow(-2 * e0 + 2, 3) / 2;
      if (t < f.form[0] + p.d * span * 0.4 * 0.2) continue;
      // start in the fireball, curl out to the letters
      const sx = ex + p.sx * st0 * (1 + 2 * e), sy = ey + p.sy * st0 * (1 + 2 * e);
      let x = lerp(sx, p.x, e), y = lerp(sy, p.y, e);
      const dx = p.x - sx, dy = p.y - sy, c = Math.sin(Math.PI * e) * p.curl * 0.25;
      x += -dy * c; y += dx * c - Math.sin(Math.PI * e) * st0 * 0.8;
      // billow once formed
      const age = Math.max(0, t - f.form[1]), bl = clamp(e0 * 1.5 - 0.5, 0, 1);
      x += Math.sin(t * 0.7 + p.ph) * L.step * 0.35 * bl + age * L.step * 0.04;
      y += Math.cos(t * 0.55 + p.ph * 1.3) * L.step * 0.25 * bl - age * L.step * 0.02;
      // colour: fire -> smoke -> sunlit cloud
      let col;
      const cloud = mix3(CLOUD_SH, CLOUD_LIT, Math.pow(p.lit, 0.6));
      if (e < 0.18) col = mix3(FIRE0, FIRE1, e / 0.18);
      else if (e < 0.5) col = mix3(FIRE1, SMOKE, clamp((e - 0.18) / 0.2, 0, 1));
      else col = mix3(SMOKE, cloud, clamp((e - 0.5) / 0.4, 0, 1));
      const r = L.step * p.s * lerp(0.5, 1.45, e) * (1 + 0.08 * Math.sin(t * 0.9 + p.ph) * bl);
      const a = clamp((t - f.form[0] - p.d * span * 0.08) / 0.25, 0, 1) * lerp(1, 0.8, e);
      const fire = e < 0.25;
      if (fire) ctx.globalCompositeOperation = 'lighter';
      ctx.globalAlpha = a * (fire ? 0.6 : 1);
      if (!fire && e > 0.6) { ctx.drawImage(smokeSprite(CLOUD_DEEP.map(q)), x - r * 1.1 + r * 0.2, y - r * 1.1 + r * 0.35, r * 2.2, r * 2.2); }
      ctx.drawImage(smokeSprite(col.map(q)), x - r, y - r, r * 2, r * 2);
      ctx.globalCompositeOperation = 'source-over';
    }
    ctx.globalAlpha = 1;
  }
  function drawRadar(V, t, f, u) {
    const R = Math.round(Math.min(W, H) * 0.14), cx = W - R - Math.max(14, W * 0.04), cy = H - (V.barH || 0) - R - Math.max(16, R * 0.3);
    ctx.save();
    ctx.globalAlpha = clamp(u * 6, 0, 1) * clamp((1 - u) * 8, 0, 1);
    ctx.fillStyle = 'rgba(4,22,10,0.85)'; ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = 'rgba(120,240,140,0.5)'; ctx.lineWidth = 1;
    for (const k of [0.33, 0.66, 1]) { ctx.beginPath(); ctx.arc(cx, cy, R * k, 0, Math.PI * 2); ctx.stroke(); }
    ctx.beginPath(); ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy); ctx.moveTo(cx, cy - R); ctx.lineTo(cx, cy + R); ctx.stroke();
    const sw = (t * 2.4) % (Math.PI * 2);
    const g = ctx.createConicGradient ? ctx.createConicGradient(sw - Math.PI / 2 - 0.9, cx, cy) : null;
    if (g) { g.addColorStop(0, 'rgba(120,255,150,0)'); g.addColorStop(0.14, 'rgba(120,255,150,0.45)'); g.addColorStop(0.1401, 'rgba(120,255,150,0)'); ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.fill(); }
    ctx.strokeStyle = 'rgba(160,255,170,0.9)'; ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.sin(sw) * R, cy - Math.cos(sw) * R); ctx.stroke();
    for (let i = 0; i < f.contacts; i++) {
      const b = (f.bearing + (i - 1.5) * 6) * D2R_, rr = R * (0.85 - 0.25 * u - (i % 2) * 0.05);
      const x = cx + Math.sin(b) * rr, y = cy - Math.cos(b) * rr;
      ctx.fillStyle = Math.floor(t * 4) % 2 ? '#ff5a48' : '#ffb0a0'; ctx.fillRect(x - 2, y - 2, 4, 4);
    }
    ctx.fillStyle = '#9cf0a8'; ctx.fillRect(cx - 2, cy - 2, 4, 4);
    ctx.restore();
    txt('AIR SEARCH', cx, cy + R + Math.max(8, R * 0.14), Math.max(7, Math.round(R * 0.11)), '#9cf0a8', 'center');
  }
  // ---------- heat haze ----------
  // Shimmer: rows of the finished picture are shifted sideways by a few pixels, rippling over time. Used for the
  // air over the sea and the deck in long-lens shots (fx 'haze', a screen band) and behind every lit afterburner.
  function shimmer(x0, y0, w, h, amp, t, taper) {
    x0 = Math.max(0, Math.floor(x0)); y0 = Math.max(0, Math.floor(y0));
    w = Math.min(W - x0, Math.ceil(w)); h = Math.min(H - y0, Math.ceil(h));
    if (w < 4 || h < 4) return;
    const cvs = ctx.canvas;
    for (let y = y0; y < y0 + h; y += 2) {
      const k = taper ? clamp((y - y0) / h, 0, 1) : Math.sin(Math.PI * (y - y0) / h);
      const dx = Math.round((0.6 * Math.sin(y * 0.37 + t * 29) + 0.4 * Math.sin(y * 0.11 - t * 17 + 2)) * amp * k);
      if (dx) ctx.drawImage(cvs, x0, y, w, 2, x0 + dx, y, w, 2);
    }
  }
  function drawHaze(V, t) {
    const res = Math.max(1, H / 360);
    for (const f of IC.active(S.fx, t)) if (f.type === 'haze') shimmer(0, V.vy + V.sh * f.y0, W, V.sh * (f.y1 - f.y0), f.amount * res, t, true);
    let n = 0;
    for (const a of S.actors) {
      if (a.kind !== 'jet' || V.skip === a.id || n >= 4) continue;
      const ab = inWin(a.ab, t); if (!ab || (a.dead != null && t >= a.dead)) continue;
      const fr = actorFrame(a, t); if (!fr) continue;
      const M = MODELS[a.model], tail = M.tail || 8;
      let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, seen = 0;
      for (const [z, rr] of [[-tail - 2.5, 1.5], [-tail - 14, 4], [-tail - 32, 7]]) for (const [sx, sy] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
        const q = project(V, L(fr, sx * rr, sy * rr, z)); if (!q) continue;
        seen++; x0 = Math.min(x0, q[0]); x1 = Math.max(x1, q[0]); y0 = Math.min(y0, q[1]); y1 = Math.max(y1, q[1]);
      }
      if (seen < 4 || x1 - x0 < 10 || y1 - y0 < 8 || x1 < 0 || x0 > W || y1 < 0 || y0 > H) continue;
      if ((x1 - x0) * (y1 - y0) > W * H * 0.5) continue;
      shimmer(x0, y0, x1 - x0, y1 - y0, clamp((y1 - y0) / 30, 1, 2.5) * res * ab, t + n, false); n++;
    }
  }
  // ---------- cockpit: canopy frame and head-up display ----------
  const HUDG = '#9dff9a';
  function drawCockpit(V, t, hud) {
    const a = ACT[hud.actor], fr = actorFrame(a, t); if (!fr) return;
    const s0 = IC.sample(a.tr, t - 0.05, []), s1 = IC.sample(a.tr, t + 0.05, []);
    const vel = s0 && s1 ? [(s1[1] - s0[1]) / 0.1, (s1[2] - s0[2]) / 0.1, (s1[3] - s0[3]) / 0.1] : mul(fr.f, 200);
    const sp = len(vel), [yaw, pitch] = IC.angles(fr.f, fr.u);
    const u0 = Math.min(W, H * 1.6), lw = Math.max(1, Math.round(H / 400));
    // the head-up display: a glass panel above the glare shield
    const hx = W * 0.5, hw = u0 * 0.3, hy0 = H * 0.24, hy1 = H * 0.7;
    ctx.save();
    ctx.fillStyle = 'rgba(120,255,160,0.035)'; ctx.fillRect(hx - hw / 2, hy0, hw, hy1 - hy0);
    ctx.beginPath(); ctx.rect(hx - hw / 2, hy0, hw, hy1 - hy0); ctx.clip();
    ctx.strokeStyle = HUDG; ctx.fillStyle = HUDG; ctx.lineWidth = lw; ctx.globalAlpha = 0.9;
    const P = d => project(V, add(V.cam.pos, mul(d, 1000)));
    const ys = yaw * D2R_, hor = [Math.cos(ys), 0, -Math.sin(ys)];
    for (let e = -30; e <= 30; e += 5) {
      if (Math.abs(e - pitch) > 16) continue;
      const er = e * D2R_, c = [Math.sin(ys) * Math.cos(er), Math.sin(er), Math.cos(ys) * Math.cos(er)];
      const wide = e === 0 ? 0.12 : 0.05, gap = e === 0 ? 0.02 : 0.022;
      const pa = P(add(c, mul(hor, -wide))), pb = P(add(c, mul(hor, -gap))), pc = P(add(c, mul(hor, gap))), pd = P(add(c, mul(hor, wide)));
      if (!pa || !pd) continue;
      if (Math.min(pa[1], pd[1]) < hy0 + 20 * lw || Math.max(Math.abs(pa[0] - hx), Math.abs(pd[0] - hx)) > hw * 0.22) continue;
      ctx.setLineDash(e < 0 ? [4 * lw, 3 * lw] : []);
      ctx.beginPath(); ctx.moveTo(pa[0], pa[1]); ctx.lineTo(pb[0], pb[1]); ctx.moveTo(pc[0], pc[1]); ctx.lineTo(pd[0], pd[1]); ctx.stroke();
      if (e) txt(String(Math.abs(e)), pd[0] + (pd[0] - pa[0]) * 0.12, pd[1] + (pd[1] - pa[1]) * 0.12, Math.max(7, Math.round(u0 * 0.011)), HUDG, 'center', false);
    }
    ctx.setLineDash([]);
    // flight path marker
    const fp = P(norm(vel));
    if (fp) { const r = 4 * lw; ctx.beginPath(); ctx.arc(fp[0], fp[1], r, 0, Math.PI * 2); ctx.moveTo(fp[0] - r, fp[1]); ctx.lineTo(fp[0] - r * 2.6, fp[1]); ctx.moveTo(fp[0] + r, fp[1]); ctx.lineTo(fp[0] + r * 2.6, fp[1]); ctx.moveTo(fp[0], fp[1] - r); ctx.lineTo(fp[0], fp[1] - r * 2); ctx.stroke(); }
    // boresight
    const bs = P(fr.f);
    if (bs) { ctx.beginPath(); ctx.moveTo(bs[0] - 8 * lw, bs[1]); ctx.lineTo(bs[0] - 4 * lw, bs[1] + 3 * lw); ctx.lineTo(bs[0], bs[1]); ctx.lineTo(bs[0] + 4 * lw, bs[1] + 3 * lw); ctx.lineTo(bs[0] + 8 * lw, bs[1]); ctx.stroke(); }
    // heading tape, speed, altitude, g
    const fs = Math.max(7, Math.round(u0 * 0.013)), hd = ((yaw % 360) + 360) % 360;
    for (let k = -3; k <= 3; k++) {
      const h = Math.round(hd / 10) * 10 + k * 10, x = hx + (h - hd) * hw * 0.012;
      ctx.beginPath(); ctx.moveTo(x, hy0 + 4 * lw); ctx.lineTo(x, hy0 + 8 * lw); ctx.stroke();
      txt(String(((h % 360) + 360) % 360 / 10 | 0).padStart(2, '0'), x, hy0 + 14 * lw, fs, HUDG, 'center', false);
    }
    ctx.beginPath(); ctx.moveTo(hx, hy0 + 1); ctx.lineTo(hx - 3 * lw, hy0 + 4 * lw); ctx.lineTo(hx + 3 * lw, hy0 + 4 * lw); ctx.closePath(); ctx.fill();
    const by = (hy0 + hy1) / 2;
    ctx.strokeRect(hx - hw * 0.46, by - fs, hw * 0.16, fs * 2); txt(String(Math.round(sp * 1.944)), hx - hw * 0.38, by, fs, HUDG, 'center', false);
    ctx.strokeRect(hx + hw * 0.28, by - fs, hw * 0.18, fs * 2); txt(String(Math.round(fr.pos[1] * 3.281 / 10) * 10), hx + hw * 0.37, by, fs, HUDG, 'center', false);
    txt('M ' + (sp / 340).toFixed(2), hx - hw * 0.38, by + fs * 2.4, fs, HUDG, 'center', false);
    // the target: a box, LOCK for the missile, the gun pipper for guns
    const tg = hud.target && ACT[hud.target], tf = tg ? actorFrame(tg, t) : null;
    if (tf && !(tg.dead != null && t >= tg.dead)) {
      const q = project(V, tf.pos);
      if (q) {
        const b = 7 * lw; ctx.strokeRect(q[0] - b, q[1] - b, b * 2, b * 2);
        if (hud.guns) {
          const pp = P(add(fr.f, mul(fr.u, -0.035)));
          if (pp) { ctx.beginPath(); ctx.arc(pp[0], pp[1], 14 * lw, 0, Math.PI * 2); ctx.stroke(); ctx.fillRect(pp[0] - lw, pp[1] - lw, 2 * lw, 2 * lw); }
          txt('GUN', hx - hw * 0.38, hy1 - fs * 1.5, fs, HUDG, 'center', false);
        } else {
          ctx.beginPath(); ctx.moveTo(q[0], q[1] - b * 1.6); ctx.lineTo(q[0] + b * 1.6, q[1]); ctx.lineTo(q[0], q[1] + b * 1.6); ctx.lineTo(q[0] - b * 1.6, q[1]); ctx.closePath(); ctx.stroke();
          if (Math.floor(t * 6) % 2) txt('LOCK', q[0], q[1] - b * 2.6, fs, HUDG, 'center', false);
          txt('SRM', hx - hw * 0.38, hy1 - fs * 1.5, fs, HUDG, 'center', false);
        }
        txt(String(Math.round(dist(tf.pos, fr.pos) / 1852 * 10) / 10) + ' NM', hx + hw * 0.37, hy1 - fs * 1.5, fs, HUDG, 'center', false);
      }
    }
    ctx.restore();
    // the frame: glare shield, the canopy bow and its mirror
    ctx.fillStyle = '#121318';
    ctx.beginPath(); ctx.moveTo(0, H); ctx.lineTo(0, H * 0.8); ctx.quadraticCurveTo(W * 0.5, H * 0.66, W, H * 0.8); ctx.lineTo(W, H); ctx.closePath(); ctx.fill();
    ctx.strokeStyle = 'rgba(120,118,140,0.5)'; ctx.lineWidth = lw;
    ctx.beginPath(); ctx.moveTo(0, H * 0.8); ctx.quadraticCurveTo(W * 0.5, H * 0.66, W, H * 0.8); ctx.stroke();
    ctx.strokeStyle = '#17181e'; ctx.lineWidth = Math.max(4, H * 0.045);
    ctx.beginPath(); ctx.moveTo(-W * 0.02, H * 0.84); ctx.quadraticCurveTo(W * 0.04, H * 0.02, W * 0.5, -H * 0.04); ctx.quadraticCurveTo(W * 0.96, H * 0.02, W * 1.02, H * 0.84); ctx.stroke();
    ctx.fillStyle = '#26272e'; ctx.fillRect(W * 0.5 - u0 * 0.05, 0, u0 * 0.1, H * 0.035);
  }
  // ---------- film look: a warm grade, vignette and grain ----------
  let GRAIN = null;
  function drawGrade(f, t) {
    if (f.warm) { ctx.globalCompositeOperation = 'soft-light'; ctx.fillStyle = `rgba(255,150,70,${(f.warm * 2.5).toFixed(3)})`; ctx.fillRect(0, 0, W, H); ctx.globalCompositeOperation = 'source-over'; }
    if (f.vignette) {
      const g = ctx.createRadialGradient(W / 2, H / 2, Math.min(W, H) * 0.35, W / 2, H / 2, Math.hypot(W, H) * 0.55);
      g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, `rgba(0,0,0,${f.vignette})`);
      ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
    }
    if (f.grain) {
      if (!GRAIN) {
        GRAIN = document.createElement('canvas'); GRAIN.width = GRAIN.height = 128;
        const gx = GRAIN.getContext('2d'), im = gx.createImageData(128, 128), R = rng(99);
        for (let i = 0; i < im.data.length; i += 4) { const v = R() * 255 | 0; im.data[i] = im.data[i + 1] = im.data[i + 2] = v; im.data[i + 3] = 255; }
        gx.putImageData(im, 0, 0);
      }
      const R = rng(Math.floor(t * 24) + 1), ox = -(R() * 128 | 0), oy = -(R() * 128 | 0);
      ctx.globalAlpha = f.grain; ctx.globalCompositeOperation = 'overlay';
      for (let y = oy; y < H; y += 128) for (let x = ox; x < W; x += 128) ctx.drawImage(GRAIN, x, y);
      ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
    }
  }
  // ---------- frame ----------
  // the picture through one shot's camera: world, haze and cockpit, but no cards or screen effects
  function renderScene(t, sh, yawOff = 0) {
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);
    const cam = camAt(t, sh, yawOff);
    const vfov = (cam.fov || 42) * D2R_;
    const V = { cam, vx: 0, vy: 0, vw: W, sh: H, cx: W / 2, cy: H / 2, F: (H / 2) / Math.tan(vfov / 2), list: [], skip: cam.shot.hud ? cam.shot.hud.actor : null, own: cam.shot.attach ? cam.shot.attach.actor : null };
    if (W / H > 1.9) V.F = (W / 2) / Math.tan(vfov * 0.9);
    drawSkySea(V, t);
    drawWakes(V, t);
    drawTerrain(V);
    V.inside = drawClouds(V);
    drawActors(V, t);
    drawItems(V.list);
    drawHaze(V, t);
    if (cam.shot.hud) drawCockpit(V, t, cam.shot.hud);
    return V;
  }
  let BUF = null;
  const WHIP = 80;   // degrees the camera swings away from each shot in a whip pan
  function renderAt(t) {
    let i = SHOTS.findIndex(s => t >= s.t0 && t < s.t1); if (i < 0) i = SHOTS.length - 1;
    const sh = SHOTS[i], pv = SHOTS[i - 1], nx = SHOTS[i + 1];
    let V;
    // whip pan, centred on the cut: shot A swings away (ease in), shot B swings in (ease out), smeared sideways
    let w = null;
    if (nx && nx.in && nx.in.type === 'whip' && t >= nx.t0 - nx.in.dur / 2) w = { a: sh, b: nx, f: nx.in };
    else if (pv && sh.in && sh.in.type === 'whip' && t < sh.t0 + sh.in.dur / 2) w = { a: pv, b: sh, f: sh.in };
    if (w) {
      const u = clamp((t - (w.b.t0 - w.f.dur / 2)) / w.f.dur, 0, 1), dir = w.f.dir || 1;
      V = u < 0.5 ? renderScene(t, w.a, dir * WHIP * Math.pow(u * 2, 2)) : renderScene(t, w.b, -dir * WHIP * Math.pow((1 - u) * 2, 2));
      const k = Math.sin(Math.PI * u), dx = dir * W * 0.035 * k;
      if (k > 0.05) { ctx.globalAlpha = 0.3; for (let j = 1; j <= 5; j++) ctx.drawImage(ctx.canvas, -dx * j, 0); ctx.globalAlpha = 1; }
    } else if (pv && sh.in && sh.in.type === 'dissolve' && t < sh.t0 + sh.in.dur) {
      // dissolve: the outgoing shot keeps running on its over-run keys, the new one fades in over it
      const u = ease(clamp((t - sh.t0) / sh.in.dur, 0, 1));
      renderScene(t, pv);
      if (!BUF || BUF.width !== ctx.canvas.width || BUF.height !== ctx.canvas.height) { BUF = document.createElement('canvas'); BUF.width = ctx.canvas.width; BUF.height = ctx.canvas.height; }
      const bx = BUF.getContext('2d'); bx.globalAlpha = 1; bx.drawImage(ctx.canvas, 0, 0);
      V = renderScene(t, sh);
      ctx.globalAlpha = 1 - u; ctx.drawImage(BUF, 0, 0); ctx.globalAlpha = 1;
    } else V = renderScene(t, sh);
    drawOverlay(V, t, V.inside);
  }
  // ---------- flow ----------
  function clock() {
    if (musicOn && AU.ctx && AU.ctx.state !== 'running' && performance.now() - st.t0p > 1500) {
      // audio is blocked: carry on silently on the page clock
      musicOn = false; try { HorizonMusic.stop(); } catch (e) {}
    }
    if (musicOn && AU.ctx) return AU.ctx.currentTime - st.t0a;
    return (performance.now() - st.t0p) / 1000;
  }
  function startMusic() {
    musicOn = false;
    if (!(AU.ctx && settings.sound && settings.music && typeof HorizonMusic !== 'undefined')) return;
    try {
      if (curTrack && curTrack.m.playing) curTrack.m.stop();
      curTrack = null;
      if (HorizonMusic.playing) HorizonMusic.stop();
      HorizonMusic.volume(0.6);
      HorizonMusic.start(AU.ctx, AU.master);
      st.t0a = AU.ctx.currentTime + 0.1; musicOn = true;
    } catch (e) { musicOn = false; }
  }
  let prevPad = true, armT = 0;
  function play() {
    auInit(); reset();
    st.t0p = performance.now() + 100;
    startMusic();
    mode = 'play'; state = 'intro'; st.menuOpen = false;
    show('boot', false); show('menu', false); show('over', false); show('pause', false);
    armT = performance.now() + 500; prevPad = true;
  }
  function toMenuFromIntro(skipped) {
    if (mode !== 'play') return;
    mode = 'hold'; state = 'menu'; st.menuOpen = true;
    if (skipped && st.t < S.menu_at) { st.quiet = true; st.holdFrom = S.menu_at; st.t0p = performance.now() - S.menu_at * 1000; st.sim = Math.max(st.sim, S.menu_at - 2); st.parts = []; st.ev = S.events.findIndex(e => (e.t ?? e.t0) > S.menu_at); if (st.ev < 0) st.ev = S.events.length; }
    st.holdStart = performance.now(); st.holdT = Math.max(st.t, S.menu_at);
    swallowUntil = performance.now() + 400;
    if (musicOn && HorizonMusic.playing) { curTrack = TRACKS.horizon || null; musicVol = -1; introKeep = true; }
    show('menu', true); syncMenu();
    const b = $('start'); if (b && !b.hidden) b.focus();
  }
  function update() {
    if (mode === 'boot') { pollBootPad(); return; }
    if (mode === 'play') {
      const t = clock();
      if (t > 0) advance(t); else st.t = 0;
      // any gamepad or HOTAS button skips
      const pressed = anyPadButton();
      if (pressed && !prevPad && performance.now() > armT) { toMenuFromIntro(true); return; }
      prevPad = pressed;
      if (st.t >= S.menu_at) toMenuFromIntro(false);
    } else if (mode === 'hold') {
      // the last shot keeps drifting behind the menu, at a quarter speed, and stops at the end of its keys
      const t = Math.min(st.holdT + (performance.now() - st.holdStart) / 4000, SHOTS[SHOTS.length - 1].t1 - 0.05);
      if (t > st.sim) advance(t); else st.t = t;
    }
  }
  function anyPadButton() { for (const gp of pads()) for (const b of gp.buttons) if (b && b.pressed) return true; return false; }
  function pollBootPad() { const p = anyPadButton(); if (p && !prevPad) play(); prevPad = p; }
  function render() { renderAt(st.t); }
  function boot() {
    reset();
    if (settings.intro === false) return false;
    mode = 'boot'; state = 'boot'; prevPad = true;
    show('menu', false); show('boot', true);
    const b = $('bootGo'); if (b) b.focus();
    return true;
  }
  function release() { mode = 'off'; st && (st.parts = []); }
  // input: any key or click skips; on the boot screen it starts the intro (the gesture also unlocks audio)
  addEventListener('keydown', e => {
    if (mode === 'boot') { if (e.target && e.target.id === 'bootSkip') return; if (e.code === 'Tab') return; e.preventDefault(); if (!e.repeat) play(); }
    else if (mode === 'play') { e.preventDefault(); if (!e.repeat && performance.now() > armT) toMenuFromIntro(true); }
  }, true);
  addEventListener('pointerdown', e => {
    if (mode === 'boot') { if (e.target && e.target.id === 'bootSkip') return; play(); }
    else if (mode === 'play' && performance.now() > armT) { e.preventDefault(); toMenuFromIntro(true); }
  }, true);
  let swallowUntil = 0;
  addEventListener('click', e => { if (performance.now() < swallowUntil) { e.stopPropagation(); e.preventDefault(); } }, true);
  return {
    boot, play, update, render, release,
    get active() { return mode === 'play' || mode === 'hold' || mode === 'boot' || mode === 'frozen'; },
    get mode() { return mode; },
    get t() { return st ? st.t : 0; },
    skipToMenu() { if (mode === 'boot') { mode = 'play'; state = 'intro'; reset(); st.t0p = performance.now(); show('boot', false); } toMenuFromIntro(true); },
    seek(t) { if (!st) reset(); st.quiet = true; mode = 'play'; state = 'intro'; st.t0p = performance.now() - t * 1000; if (musicOn) { musicOn = false; try { HorizonMusic.stop(); } catch (e) {} } advance(t); st.quiet = false; },
    pause(t) { mode = 'frozen'; if (!st) reset(); st.quiet = true; if (t < st.sim) { reset(); st.quiet = true; } advance(t); },
    script: S
  };
})();
// ---- intro end ----
