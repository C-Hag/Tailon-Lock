// Outside view, HUD styles and the eight cockpit layouts. Canvas is 1280 x 820 logical px.
const CW = 1280, CVH = 820, EYE = [640, 235], PPD = 14; // px per degree in the view (HUD is conformal)
const HUDG = '#5dff7e';

function attitude(sim) { return { th: sim.gam * R2D + sim.alpha * Math.cos(sim.phi), ph: sim.phi * R2D }; }

// ---------- outside world ----------
const CLOUDS = Array.from({ length: 26 }, (_, i) => ({ x: ((i * 7919) % 40000) - 20000, z: ((i * 104729) % 40000) - 20000, h: 2500 + (i * 613) % 2500, s: 600 + (i * 331) % 900 }));
function drawWorld(c, sim, o = {}) {
  const { th, ph } = attitude(sim);
  const F = PPD * R2D, cx = EYE[0], cy = EYE[1];
  const ps = sim.psi * D2R, t = th * D2R, p = ph * D2R;
  const f = [Math.sin(ps) * Math.cos(t), Math.sin(t), Math.cos(ps) * Math.cos(t)];
  const r0 = [Math.cos(ps), 0, -Math.sin(ps)], u0 = [-Math.sin(ps) * Math.sin(t), Math.cos(t), -Math.cos(ps) * Math.sin(t)];
  const r = r0.map((v, i) => v * Math.cos(p) - u0[i] * Math.sin(p)), u = u0.map((v, i) => v * Math.cos(p) + r0[i] * Math.sin(p));
  const proj = (dx, dy, dz) => { const zf = dx * f[0] + dy * f[1] + dz * f[2]; if (zf < 5) return null; return [cx + (dx * r[0] + dy * r[1] + dz * r[2]) / zf * F, cy - (dx * u[0] + dy * u[1] + dz * u[2]) / zf * F, zf]; };
  c.save(); c.beginPath(); c.rect(0, 0, CW, o.bottom || 470); c.clip();
  c.save(); c.translate(cx, cy); c.rotate(-p);
  const hy = th * PPD;
  const sky = c.createLinearGradient(0, hy - 700, 0, hy);
  sky.addColorStop(0, '#2c5f9e'); sky.addColorStop(0.75, '#7fb0dc'); sky.addColorStop(1, '#cfe2f0');
  c.fillStyle = sky; c.fillRect(-1600, hy - 3000, 3200, 3000);
  const gnd = c.createLinearGradient(0, hy, 0, hy + 500);
  gnd.addColorStop(0, '#9aa58f'); gnd.addColorStop(0.15, '#6f7d55'); gnd.addColorStop(1, '#3f4a2c');
  c.fillStyle = gnd; c.fillRect(-1600, hy, 3200, 3000);
  c.restore();
  // ground grid (3 km squares) for motion cues
  const H = sim.h, gs = 3000, gx0 = Math.floor(sim.x / gs) * gs, gz0 = Math.floor(sim.z / gs) * gs;
  c.strokeStyle = 'rgba(40,52,30,0.55)'; c.lineWidth = 1;
  const seg = (ax, az, bx, bz) => {
    const N = 10; let prev = null;
    for (let i = 0; i <= N; i++) {
      const wx = ax + (bx - ax) * i / N, wz = az + (bz - az) * i / N;
      const q = proj(wx - sim.x, -H, wz - sim.z);
      if (q && prev) { c.beginPath(); c.moveTo(prev[0], prev[1]); c.lineTo(q[0], q[1]); c.stroke(); }
      prev = q;
    }
  };
  for (let k = -10; k <= 10; k++) { seg(gx0 + k * gs, gz0 - 30000, gx0 + k * gs, gz0 + 30000); seg(gx0 - 30000, gz0 + k * gs, gx0 + 30000, gz0 + k * gs); }
  // distant ridge for heading cue
  c.fillStyle = 'rgba(70,86,96,0.85)'; c.beginPath(); let started = false;
  for (let a = 0; a <= 360; a += 4) {
    const ar = a * D2R, hh = 900 + 700 * Math.sin(a * 0.11 * 3) + 500 * Math.sin(a * D2R * 7 + 1) + 300 * Math.sin(a * D2R * 17);
    const q = proj(Math.sin(ar) * 70000, hh - H, Math.cos(ar) * 70000), qb = proj(Math.sin(ar) * 70000, -H, Math.cos(ar) * 70000);
    if (!q || !qb) { if (started) { c.closePath(); c.fill(); c.beginPath(); started = false; } continue; }
    if (!started) { c.moveTo(qb[0], qb[1]); started = true; }
    c.lineTo(q[0], q[1]);
  }
  if (started) { c.closePath(); c.fill(); }
  // clouds
  for (const cl of CLOUDS) {
    const wx = ((cl.x - sim.x) % 40000 + 60000) % 40000 - 20000, wz = ((cl.z - sim.z) % 40000 + 60000) % 40000 - 20000;
    const q = proj(wx, cl.h - H, wz); if (!q) continue;
    const sz = cl.s / q[2] * F; if (sz < 2) continue;
    c.fillStyle = 'rgba(255,255,255,0.8)'; c.beginPath(); c.ellipse(q[0], q[1], sz, sz * 0.32, -p, 0, 7); c.fill();
  }
  c.restore();
}

// ---------- HUD ----------
// st: {rect:[x,y,w,h], metric, color, hdgPos:'top'|'bottom', tapes, lbl:{}, roll:'top'|'bottom'|null, ebracket, wpn, style}
function drawHUD(c, sim, st) {
  const [hx, hy, hw, hh] = st.rect, col = st.color || HUDG;
  // combiner glass
  c.save();
  c.fillStyle = 'rgba(120,255,160,0.05)'; c.fillRect(hx, hy, hw, hh);
  c.strokeStyle = 'rgba(20,25,28,0.9)'; c.lineWidth = 5; c.strokeRect(hx, hy, hw, hh);
  c.beginPath(); c.rect(hx, hy, hw, hh); c.clip();
  c.strokeStyle = col; c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 5; c.lineWidth = 1.6;
  const { th, ph } = attitude(sim), cx = EYE[0], cy = EYE[1];
  const T = (s, x, y, px, al = 'center', w = '') => I.text(c, s, x, y, px, col, al, w, MONO);
  const ias = st.metric ? sim.ias * 3.6 : sim.ias * KT, alt = st.metric ? sim.h : sim.h * FT;
  // flight path marker position (body axes): below boresight by alpha
  const fx = cx - sim.beta * R2D * PPD, fy = cy + sim.alpha * PPD;
  // pitch ladder, rotated about the FPM like modern HUDs
  if (st.ladder !== false) {
    c.save(); c.translate(fx, fy); c.rotate(-ph * D2R);
    const g = sim.gam * R2D;
    for (let pp = -90; pp <= 90; pp += st.ladderStep || 5) {
      const yy = (g - pp) * PPD; if (Math.abs(yy) > Math.min(hh * 0.6, st.ladderMax || 999)) continue;
      if (pp === 0) { c.beginPath(); c.moveTo(-hw * 0.42, yy); c.lineTo(-30, yy); c.moveTo(30, yy); c.lineTo(hw * 0.42, yy); c.stroke(); continue; }
      const w1 = 22, w2 = 70;
      if (pp < 0 && st.style !== 'soviet') c.setLineDash([7, 5]);
      c.beginPath();
      const tick = pp > 0 ? 8 : -8, bend = st.bend ? -pp * 0.5 : 0;
      c.moveTo(-w2, yy + tick); c.lineTo(-w2, yy); c.lineTo(-w1, yy + bend * 0.2); c.moveTo(w1, yy + bend * 0.2); c.lineTo(w2, yy); c.lineTo(w2, yy + tick); c.stroke();
      c.setLineDash([]);
      T(String(Math.abs(pp)), -w2 - 16, yy, 12); T(String(Math.abs(pp)), w2 + 16, yy, 12);
    }
    c.restore();
  }
  // FPM
  c.beginPath(); c.arc(fx, fy, 7, 0, 7); c.moveTo(fx - 7, fy); c.lineTo(fx - 18, fy); c.moveTo(fx + 7, fy); c.lineTo(fx + 18, fy); c.moveTo(fx, fy - 7); c.lineTo(fx, fy - 15); c.stroke();
  // boresight / gun cross
  c.beginPath(); c.moveTo(cx - 8, cy); c.lineTo(cx + 8, cy); c.moveTo(cx, cy - 8); c.lineTo(cx, cy + 8); c.stroke();
  // carrier approach E-bracket (navy): on-speed when FPM centred
  if (st.ebracket && (sim.gear || sim.ias < 90)) { const by = fy - (sim.alpha - 8.1) * 6; c.beginPath(); c.moveTo(fx + 30, by - 18); c.lineTo(fx + 24, by - 18); c.lineTo(fx + 24, by + 18); c.lineTo(fx + 30, by + 18); c.moveTo(fx + 24, by); c.lineTo(fx + 30, by); c.stroke(); }
  // heading
  const hdgY = st.hdgPos === 'bottom' ? hy + hh - 40 : hy + 14;
  I.htape(c, cx - 110, hdgY, 220, 20, sim.psi, { ppd: 4, px: 12, color: col, fam: MONO, box: st.hdgPos !== 'bottom' });
  if (st.hdgPos === 'bottom') T(String(Math.round(sim.psi) % 360).padStart(3, '0'), cx, hdgY + 36, 13, 'center', 'bold');
  // speed / altitude
  const L = st.lbl || {};
  if (st.tapes) {
    I.vtape(c, hx + 18, cy - 110, 62, 220, ias, { step: st.metric ? 20 : 10, every: st.metric ? 100 : 50, ppu: st.metric ? 1.0 : 1.8, side: 'l', color: col, px: 12, fam: MONO, boxBg: 'rgba(0,18,6,0.7)', min: 0 });
    I.vtape(c, hx + hw - 80, cy - 110, 62, 220, alt, { step: st.metric ? 50 : 100, every: st.metric ? 250 : 500, ppu: st.metric ? 0.25 : 0.08, side: 'r', color: col, px: 12, fam: MONO, boxBg: 'rgba(0,18,6,0.7)', boxFmt: v => String(Math.round(v / 10) * 10) });
  } else {
    c.strokeRect(hx + 22, cy - 14, 66, 26); T((L.v || '') + Math.round(ias), hx + 55, cy, 15, 'center', 'bold');
    c.strokeRect(hx + hw - 92, cy - 14, 72, 26); T((L.h || '') + String(Math.round(alt / 10) * 10), hx + hw - 56, cy, 15, 'center', 'bold');
  }
  const lx = hx + 20, ly = cy + 128;
  T(`M ${sim.M.toFixed(2)}`, lx, ly, 13, 'left'); T(`G ${sim.n.toFixed(1)}`, lx, ly + 17, 13, 'left'); T(`${sim.gMax.toFixed(1)}`, lx, ly + 34, 13, 'left');
  const ty = Math.max(hy + 44, cy - 130);
  T(`${L.a || 'α'} ${sim.alpha.toFixed(1)}`, lx, ty, 13, 'left');
  // weapon / mode block
  const w = sim.stores[sim.wpn];
  T(`${w ? (st.wpnName ? st.wpnName(w) : w.name.split(' ')[0]) + ' ' + w.n : 'GUN'}`, hx + hw - 20, ly, 13, 'right');
  T(sim.master ? (L.arm || 'ARM') : (L.safe || 'SAFE'), hx + hw - 20, ly + 17, 13, 'right');
  if (st.vsReadout) T(`${st.metric ? sim.vs.toFixed(0) : (sim.vs * FT * 60 / 100).toFixed(0) * 100}`, hx + hw - 20, ty, 13, 'right');
  // roll scale
  if (st.roll) {
    const ry = st.roll === 'bottom' ? cy + 95 : cy - 95, sgn = st.roll === 'bottom' ? 1 : -1;
    for (const b of [-45, -30, -20, -10, 0, 10, 20, 30, 45]) { const a = b * D2R; c.beginPath(); c.moveTo(cx + Math.sin(a) * 60, ry + sgn * Math.cos(a) * 60 - sgn * 60); c.lineTo(cx + Math.sin(a) * (b % 30 ? 66 : 72), ry + sgn * Math.cos(a) * (b % 30 ? 66 : 72) - sgn * 60); c.stroke(); }
    const a = -ph * D2R * sgn; c.beginPath(); c.moveTo(cx + Math.sin(a) * 56, ry + sgn * Math.cos(a) * 56 - sgn * 60); c.lineTo(cx + Math.sin(a + 0.06) * 48, ry + sgn * Math.cos(a + 0.06) * 48 - sgn * 60); c.lineTo(cx + Math.sin(a - 0.06) * 48, ry + sgn * Math.cos(a - 0.06) * 48 - sgn * 60); c.closePath(); c.fill();
  }
  // nearest hostile target designator box if inside the HUD
  for (const tg of sim.contacts) {
    if (!tg.hostile) continue;
    const rp = relPos(sim, tg.x, tg.z); if (Math.abs(rp.rel) > 12 || rp.rng > 80000) continue;
    const el = Math.atan2(tg.h - sim.h, rp.rng) * R2D - th;
    const a = -ph * D2R, dx = rp.rel * PPD, dy = -el * PPD;
    const sx = cx + dx * Math.cos(a) - dy * Math.sin(a), sy = cy + dx * Math.sin(a) + dy * Math.cos(a);
    c.strokeRect(sx - 9, sy - 9, 18, 18); T((rp.rng / (st.metric ? 1000 : 1852)).toFixed(1), sx, sy + 20, 11);
  }
  if (sim.warn.stall) T(L.stall || 'AOA', cx, cy + 60, 18, 'center', 'bold');
  if (sim.warn.pullup) T(L.pullup || 'PULL UP', cx, cy - 50, 22, 'center', 'bold');
  if (st.extra) st.extra(c, sim, T, col);
  c.restore();
}

// ---------- panel helpers ----------
function panelBg(c, top, col1, col2, glare = '#141516', shape) {
  const g = c.createLinearGradient(0, top, 0, CVH); g.addColorStop(0, col1); g.addColorStop(1, col2);
  c.fillStyle = g; c.beginPath();
  if (shape === 'flat') c.rect(0, top, CW, CVH - top);
  else { c.moveTo(0, top + 40); c.quadraticCurveTo(CW / 2, top - 30, CW, top + 40); c.lineTo(CW, CVH); c.lineTo(0, CVH); c.closePath(); }
  c.fill();
  // glareshield lip
  c.fillStyle = glare; c.beginPath();
  if (shape === 'flat') c.rect(0, top - 6, CW, 14);
  else { c.moveTo(0, top + 32); c.quadraticCurveTo(CW / 2, top - 40, CW, top + 32); c.lineTo(CW, top + 46); c.quadraticCurveTo(CW / 2, top - 22, 0, top + 46); c.closePath(); }
  c.fill();
}
function canopyFrame(c, o = {}) {
  c.save(); c.fillStyle = o.color || '#1d1f21';
  // side frames converging toward the top (canopy bow)
  c.beginPath(); c.moveTo(0, 0); c.lineTo(o.l || 70, 0); c.lineTo(o.l2 || 18, 460); c.lineTo(0, 460); c.closePath(); c.fill();
  c.beginPath(); c.moveTo(CW, 0); c.lineTo(CW - (o.l || 70), 0); c.lineTo(CW - (o.l2 || 18), 460); c.lineTo(CW, 460); c.closePath(); c.fill();
  if (o.bow) { c.beginPath(); c.moveTo(0, o.bow); c.quadraticCurveTo(CW / 2, o.bow - 60, CW, o.bow); c.lineTo(CW, o.bow + 16); c.quadraticCurveTo(CW / 2, o.bow - 44, 0, o.bow + 16); c.closePath(); c.fill(); }
  if (o.mirror) { c.fillStyle = '#2a2c2e'; c.fillRect(CW / 2 - 70, 4, 140, 22); c.fillStyle = '#7f9fbf'; c.fillRect(CW / 2 - 64, 7, 128, 15); }
  c.restore();
}
function label(c, s, x, y, px = 10, col = '#d9d9d2', w = 'bold') { I.text(c, s, x, y, px, col, 'center', w); }
function screw(c, x, y) { c.beginPath(); c.arc(x, y, 3, 0, 7); c.fillStyle = '#555'; c.fill(); c.strokeStyle = '#222'; c.beginPath(); c.moveTo(x - 2, y); c.lineTo(x + 2, y); c.stroke(); }
function gearLights(c, x, y, sim, lbl = 'GEAR', col = '#3c3') {
  for (const [dx, dy] of [[0, -14], [-14, 6], [14, 6]]) { c.beginPath(); c.arc(x + dx, y + dy, 6, 0, 7); c.fillStyle = sim.gear ? col : '#1c1c1c'; c.fill(); c.strokeStyle = '#000'; c.stroke(); }
  label(c, lbl, x, y + 24, 9);
}

// shared needle-gauge recipes
const G = {
  asiKt: (c, x, y, r, sim, o = {}) => I.dial(c, x, y, r, {
    min: 80, max: 850, a0: 0, a1: 340, map: v => lerpTab([[80, 0], [200, 0.33], [300, 0.53], [400, 0.67], [500, 0.78], [600, 0.87], [700, 0.94], [850, 1]], v),
    labels: [100, 200, 300, 400, 500, 600, 700, 800], fmt: v => v / 10, major: null, minor: null, labR: 0.6,
    extra: (c, x, y, r, ang) => { c.strokeStyle = '#eee'; for (let v = 100; v <= 850; v += (v < 400 ? 10 : 50)) { const a = ang(v), [x1, y1] = polar(x, y, r * 0.93, a), [x2, y2] = polar(x, y, r * (v % 100 ? 0.85 : 0.76), a); c.lineWidth = v % 100 ? 1 : 2; c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke(); } I.win(c, x - r * 0.3, y + r * 0.2, r * 0.6, r * 0.24, sim.M.toFixed(2), { px: r * 0.18 }); I.text(c, 'MACH', x, y + r * 0.56, r * 0.11, '#ddd'); I.text(c, 'KNOTS', x, y - r * 0.3, r * 0.12, '#ddd', 'center', 'bold'); },
    needles: [{ v: sim.ias * KT }], over: 0, ...o }),
  altFt: (c, x, y, r, sim, o = {}) => { const ft = sim.h * FT; I.dial(c, x, y, r, {
    min: 0, max: 1000, a0: 0, a1: 360, minor: 20, major: 100, labels: [0, 100, 200, 300, 400, 500, 600, 700, 800, 900], fmt: v => v / 100, labR: 0.6,
    extra: (c, x, y, r) => { I.win(c, x - r * 0.42, y - r * 0.38, r * 0.84, r * 0.3, `${String(Math.floor(ft / 1000)).padStart(2, ' ')} ${String(Math.floor(ft % 1000 / 100))}00`, { px: r * 0.22 }); I.win(c, x - r * 0.3, y + r * 0.22, r * 0.6, r * 0.22, '29.92', { px: r * 0.15, color: '#ddd' }); I.text(c, 'ALT', x, y + r * 0.6, r * 0.12, '#ddd', 'center', 'bold'); },
    needles: [{ v: ft % 1000, w: 0.07 }], ...o }); },
  vviFpm: (c, x, y, r, sim, o = {}) => I.dial(c, x, y, r, {
    min: -6000, max: 6000, a0: 270 - 170, a1: 270 + 170, map: v => (Math.sign(v) * lerpTab([[0, 0], [1000, 0.27], [2000, 0.47], [4000, 0.75], [6000, 1]], Math.abs(v)) + 1) / 2,
    labels: [-6000, -4000, -2000, -1000, 0, 1000, 2000, 4000, 6000], fmt: v => Math.abs(v / 1000), labR: 0.6,
    extra: (c, x, y, r) => { I.text(c, 'UP', x - r * 0.1, y - r * 0.35, r * 0.12, '#ddd'); I.text(c, 'DN', x - r * 0.1, y + r * 0.35, r * 0.12, '#ddd'); I.text(c, 'VERT SPEED', x + r * 0.15, y - r * 0.08, r * 0.09, '#ddd'); I.text(c, '1000 FT/MIN', x + r * 0.15, y + r * 0.08, r * 0.09, '#ddd'); },
    needles: [{ v: sim.vs * FT * 60 }], ...o }),
  gMeter: (c, x, y, r, sim, o = {}) => I.dial(c, x, y, r, {
    min: -4, max: 10, a0: 225 - 360, a1: 135, major: 1, minor: 0.5, labels: [-4, -2, 0, 2, 4, 6, 8, 10], labR: 0.6, title: o.title || 'G', titleY: 0.35,
    arcs: [{ from: o.gmax || 9, to: 10, color: '#d22' }],
    needles: [{ v: sim.gMax, color: '#e9b52a', len: 0.8, w: 0.04 }, { v: sim.gMin, color: '#e9b52a', len: 0.8, w: 0.04 }, { v: sim.n }], ...o }),
  aoaDeg: (c, x, y, r, sim, o = {}) => I.dial(c, x, y, r, {
    min: -5, max: 35, a0: -120, a1: 120, major: 5, minor: 1, labels: [0, 10, 20, 30], labR: 0.6, title: o.title || 'AOA', titleY: 0.38, units: o.units || 'DEG', unitsY: 0.55,
    arcs: [{ from: o.amax || 25, to: 35, color: '#d22' }, { from: 11, to: 15, color: '#2a2' }], needles: [{ v: sim.alpha }], ...o }),
  rpm: (c, x, y, r, sim, i = 0, o = {}) => I.dial(c, x, y, r, {
    min: 0, max: 110, a0: -135, a1: 135, map: v => lerpTab([[0, 0], [60, 0.3], [110, 1]], v), major: null, minor: null, labels: [0, 60, 70, 80, 90, 100], labR: 0.6, title: o.title || 'RPM', titleY: 0.35, units: '%', unitsY: 0.55,
    extra: (c, x, y, r, ang) => { c.strokeStyle = '#eee'; for (let v = 0; v <= 110; v += (v < 60 ? 10 : 2)) { const a = ang(v), [x1, y1] = polar(x, y, r * 0.93, a), [x2, y2] = polar(x, y, r * (v % 10 ? 0.85 : 0.77), a); c.lineWidth = v % 10 ? 1 : 2; c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke(); } },
    needles: [{ v: sim.rpm[i] }], ...o }),
  egt: (c, x, y, r, sim, i = 0, o = {}) => I.dial(c, x, y, r, {
    min: 200, max: 1200, a0: -135, a1: 135, map: v => lerpTab([[200, 0], [700, 0.35], [1000, 0.85], [1200, 1]], v), major: 100, minor: null, labels: [200, 400, 600, 800, 1000, 1200], fmt: v => v / 100, labR: 0.6,
    title: o.title || 'FTIT', titleY: 0.35, units: '°C ×100', unitsY: 0.55, arcs: [{ from: 1000, to: 1200, color: '#d22' }], needles: [{ v: sim.egt[i] }], ...o }),
  noz: (c, x, y, r, sim, i = 0, o = {}) => I.dial(c, x, y, r, {
    min: 0, max: 100, a0: -120, a1: 120, major: 10, minor: 5, labels: [0, 20, 40, 60, 80, 100], fmt: v => v / 10, labR: 0.6, title: o.title || 'NOZ POS', titleY: 0.35, units: '% OPEN', unitsY: 0.55, needles: [{ v: sim.noz[i] }], ...o }),
  clock: (c, x, y, r, sim, o = {}) => { const s = (sim.t + 10 * 3600 + 42 * 60) ; I.dial(c, x, y, r, { min: 0, max: 60, a0: 0, a1: 360, minor: 1, major: 5, labels: [5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60], fmt: v => v / 5, labPx: 0.18, labR: 0.62, title: o.title,
    needles: [{ v: (s / 3600) % 12 * 5, len: 0.55, w: 0.08 }, { v: (s / 60) % 60, len: 0.85, w: 0.06 }, { v: s % 60, len: 0.9, w: 0.025, color: o.sec || '#e33' }], ...o }); },
};

// ---------- cockpit layouts ----------
const COCKPITS = {};

// 1. AF-40: panoramic touch display
COCKPITS.panoramic = {
  pages: [['PFD', 'TSD'], ['TSD', 'FCR', 'SMS'], ['FCR', 'SMS', 'ENG'], ['ENG', 'FUEL', 'SMS']], sel: [0, 0, 0, 0],
  rects: [[150, 548, 245, 242], [400, 548, 245, 242], [650, 548, 245, 242], [900, 548, 230, 242]],
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 40, l2: 8 });
    drawHUD(c, sim, { rect: [430, 70, 420, 380], tapes: true, roll: 'bottom', hdgPos: 'top', bend: true, vsReadout: true });
    panelBg(c, 470, '#2b2e31', '#141617', '#0e0f10');
    // ICP + master lights
    c.fillStyle = '#1c1e20'; c.fillRect(520, 476, 240, 62);
    I.win(c, 530, 482, 220, 24, `${(sim.psi | 0).toString().padStart(3, '0')}° STPT 3  TCN 77X  ${(relPos(sim, HOME.x, HOME.z).rng / 1852).toFixed(0)}NM`, { px: 11, color: '#7cff8a' });
    I.win(c, 530, 509, 220, 24, `UHF 251.25  IFF 4  AP ${sim.auto ? 'ALT' : 'OFF'}`, { px: 11, color: '#7cff8a' });
    I.light(c, 430, 482, 76, 24, 'WARN', sim.warn.pullup || sim.warn.overg, '#ff3030');
    I.light(c, 430, 510, 76, 24, 'CAUTION', sim.warn.fuel || sim.warn.stall, '#ffb020');
    I.light(c, 774, 482, 76, 24, 'FIRE', false, '#ff3030');
    I.light(c, 774, 510, 76, 24, 'BINGO', sim.warn.bingo, '#ffb020');
    // panoramic display frame
    c.fillStyle = '#0a0b0c'; c.fillRect(140, 540, 1000, 268); c.strokeStyle = '#3b3e41'; c.lineWidth = 3; c.strokeRect(140, 540, 1000, 268);
    const ink = '#fff', o = { ink, bg: '#06090c', hostile: '#ff4040', friend: '#41c6ff', metric: false };
    this.rects.forEach((r, i) => {
      const s = { x: r[0], y: r[1] + 18, w: r[2], h: r[3] - 18 }, pg = this.pages[i][this.sel[i]];
      c.fillStyle = '#1a2a38'; c.fillRect(r[0], r[1], r[2], 16);
      this.pages[i].forEach((p, k) => I.text(c, p, r[0] + 8 + k * 46, r[1] + 8, 10, k === this.sel[i] ? '#6ff' : '#89a', 'left', 'bold', MONO));
      c.fillStyle = o.bg; c.fillRect(s.x, s.y, s.w, s.h);
      if (pg === 'PFD') Pages.pfd(c, s, sim, { sky: '#2a64a8', ground: '#7b5a35' });
      else if (pg === 'TSD') Pages.map(c, s, sim, { ...o, scale: 80, labels: true, terrain: 'rgba(80,110,70,0.25)' });
      else if (pg === 'FCR') Pages.radarB(c, s, sim, { ...o, ink: '#6ff', dim: 'rgba(100,255,255,0.3)', symbols: true, mode: 'AESA RWS', scale: 80 });
      else if (pg === 'SMS') Pages.sms(c, s, sim, { ...o, ink: '#fff', armTxt: 'MASTER ARM' });
      else Pages.eng(c, s, sim, { ...o, arc: '#3f6', tLbl: 'FTIT', nozLbl: 'NOZ / TVC' });
    });
    // standby flight display
    c.fillStyle = '#0a0b0c'; c.fillRect(22, 560, 106, 110); Pages.pfd(c, { x: 26, y: 564, w: 98, h: 102 }, sim, { compact: true });
    label(c, 'ISFD', 75, 682, 9);
    gearLights(c, 75, 730, sim, 'GEAR'); I.light(c, 1160, 560, 100, 26, 'MASTER ARM', sim.master, '#3f3'); I.light(c, 1160, 592, 100, 26, 'A/B', sim.ab, '#ffb020');
    I.light(c, 1160, 624, 100, 26, 'SPD BRK', false); I.light(c, 1160, 656, 100, 26, 'STALL', sim.warn.stall, '#ff3030');
    label(c, 'Tap a portal title to change page', 640, 814, 9, '#6c7378', '');
  },
  click(x, y) { this.rects.forEach((r, i) => { if (x > r[0] && x < r[0] + r[2] && y > r[1] && y < r[1] + r[3]) this.sel[i] = (this.sel[i] + 1) % this.pages[i].length; }); },
};

// 2. LF-27: HUD + UFC, two monochrome MFDs, analog standbys
COCKPITS.lightweight = {
  pages: [['FCR', 'HSD'], ['SMS', 'HSD', 'FCR']], sel: [0, 0], rects: [[110, 560, 260, 250], [910, 560, 260, 250]],
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 30, l2: 6 });
    drawHUD(c, sim, { rect: [478, 96, 324, 344], tapes: false, roll: null, hdgPos: 'bottom', lbl: {}, vsReadout: false, ladderMax: 120 });
    panelBg(c, 462, '#4b5054', '#2c2f31', '#1a1b1c');
    // UFC with data entry display
    c.fillStyle = '#2a2d30'; c.fillRect(500, 462, 280, 88); c.strokeStyle = '#111'; c.strokeRect(500, 462, 280, 88);
    const ded = [`UHF 292.30  STPT  4 ${sim.auto ? 'AUTO' : 'MAN '}`, `VHF  1     ${new Date((sim.t + 38520) * 1000).toISOString().slice(11, 19)}`, `M1 3 M4 6  C ${(sim.psi | 0).toString().padStart(3, '0')}  T 12X`, `FUEL ${(sim.fuel * 2.2046 | 0).toString().padStart(5, ' ')}  BINGO 2500`];
    c.fillStyle = '#000'; c.fillRect(560, 468, 160, 76);
    ded.forEach((l, i) => I.text(c, l, 564, 478 + i * 18, 10.5, '#5f5', 'left', 'bold', MONO));
    for (let i = 0; i < 9; i++) { c.fillStyle = '#3d4044'; c.fillRect(508 + (i % 3) * 16, 470 + (i / 3 | 0) * 24, 13, 18); c.fillRect(728 + (i % 3) * 16, 470 + (i / 3 | 0) * 24, 13, 18); }
    I.light(c, 420, 466, 70, 30, 'MASTER\nCAUTION', sim.warn.fuel || sim.warn.stall, '#ffb020', { px: 9 });
    I.light(c, 790, 466, 70, 30, 'TF FAIL', false, '#ffb020');
    I.light(c, 420, 500, 70, 22, 'ENG FIRE', false, '#ff3030'); I.light(c, 790, 500, 70, 22, 'ENGINE', sim.warn.overg, '#ffb020');
    // MFDs (green)
    this.rects.forEach((r, i) => {
      const pg = this.pages[i][this.sel[i]], labels = i === 0 ? { T0: 'CRM', T1: 'RWS', T2: 'NORM', B0: 'SWAP', B2: pg, B4: 'DCLT' } : { T0: 'AAM', T1: 'INV', B0: 'SWAP', B2: pg, B4: 'DCLT' };
      const s = I.mfd(c, r[0], r[1], r[2], r[3], { labels, sel: 'B2', ink: '#5f5' });
      const o = { ink: '#5f5', dim: 'rgba(80,255,80,0.3)', hostile: '#5f5', friend: '#5f5', ring: 'rgba(80,255,80,0.4)', threat: '#5f5', route: '#5f5', own: '#5f5' };
      if (pg === 'FCR') Pages.radarB(c, s, sim, { ...o, scale: 40, mode: '' });
      else if (pg === 'HSD') Pages.map(c, s, sim, { ...o, scale: 60 });
      else Pages.sms(c, s, sim, o);
      s.labels();
    });
    // standby / primary analog cluster
    G.asiKt(c, 448, 612, 50, sim);
    I.adi(c, 576, 616, 64, attitude(sim).th, attitude(sim).ph, { slipv: sim.beta * 8 });
    G.altFt(c, 704, 612, 50, sim);
    G.vviFpm(c, 830, 612, 44, sim);
    G.aoaDeg(c, 448, 748, 44, sim, { amax: 25 });
    const hp = relPos(sim, HOME.x, HOME.z);
    I.hsi(c, 576, 752, 56, sim.psi, { course: hp.brg, bug: wrap360(sim.psi + 20), dist: hp.rng / 1852, crsDev: 0.4 });
    G.gMeter(c, 704, 748, 44, sim);
    Pages.rwr(c, 830, 750, 46, sim); label(c, 'THREAT WARNING', 830, 805, 8);
    // engine column
    G.rpm(c, 1222, 522, 34, sim, 0); G.egt(c, 1222, 606, 34, sim, 0); G.noz(c, 1222, 690, 34, sim, 0);
    I.dial(c, 1222, 772, 32, { min: 0, max: 7000, a0: -135, a1: 135, major: 1000, minor: 500, labels: [0, 2, 4, 6], fmt: v => v, labR: 0.58, title: 'FUEL', titleY: 0.38, units: 'LB ×1000', unitsY: 0.6, needles: [{ v: sim.fuel * 2.2046 }] });
    // left console
    gearLights(c, 60, 600, sim, 'GEAR');
    I.light(c, 22, 650, 76, 22, 'A/B', sim.ab, '#3f3'); I.light(c, 22, 676, 76, 22, 'STALL', sim.warn.stall, '#ff3030'); I.light(c, 22, 702, 76, 22, 'FUEL LOW', sim.warn.fuel, '#ffb020');
    I.light(c, 22, 728, 76, 22, 'MASTER ARM', sim.master, '#3f3');
    label(c, 'Click an MFD to change page', 640, 814, 9, '#888', '');
  },
  click(x, y) { this.rects.forEach((r, i) => { if (x > r[0] && x < r[0] + r[2] && y > r[1] && y < r[1] + r[3]) this.sel[i] = (this.sel[i] + 1) % this.pages[i].length; }); },
};

// 3. ECF-90: three colour head-down displays
COCKPITS.euro = {
  pages: [['ATT', 'ENG'], ['PA', 'RDR'], ['WPN', 'DAS', 'FUEL']], sel: [0, 0, 0], rects: [[150, 545, 290, 265], [495, 530, 290, 280], [840, 545, 290, 265]],
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 36, l2: 8 });
    drawHUD(c, sim, { rect: [452, 70, 376, 380], tapes: false, roll: 'top', hdgPos: 'top', vsReadout: true });
    panelBg(c, 466, '#3d4246', '#1f2224', '#141516');
    const col = { ink: '#e8f2ff', bg: '#04070b', hostile: '#ff5050', friend: '#3fd1ff', threat: '#ff5050', terrain: 'rgba(110,130,90,0.3)', arc: '#38e1ff' };
    this.rects.forEach((r, i) => {
      const pg = this.pages[i][this.sel[i]];
      const s = I.mfd(c, r[0], r[1], r[2], r[3], { frame: '#2b3034', ink: '#9fd8ff', labels: { T0: this.pages[i][0], T1: this.pages[i][1], T2: this.pages[i][2] || '', B2: 'MENU' }, sel: 'T' + this.sel[i], px: 10 });
      if (pg === 'ATT') Pages.pfd(c, s, sim, {});
      else if (pg === 'ENG' || pg === 'FUEL') Pages.eng(c, s, sim, { ...col, tLbl: 'TJ', nozLbl: 'NOZ' });
      else if (pg === 'PA') Pages.map(c, s, sim, { ...col, scale: 80, labels: true });
      else if (pg === 'RDR') Pages.radarB(c, s, sim, { ...col, ink: '#7fe3ff', dim: 'rgba(120,220,255,0.3)', symbols: true, mode: 'TWS' });
      else if (pg === 'WPN') Pages.sms(c, s, sim, { ...col, ink: '#e8f2ff' });
      else { c.fillStyle = col.bg; c.fillRect(s.x, s.y, s.w, s.h); Pages.rwr(c, s.x + s.w / 2, s.y + s.h / 2, Math.min(s.w, s.h) * 0.42, sim, { ink: '#ff8080', ring: 'rgba(255,140,140,0.35)', bg: '#04070b' }); }
      s.labels();
    });
    // dedicated warning panel
    c.fillStyle = '#1a1c1e'; c.fillRect(1146, 480, 124, 250);
    const dwp = [['FUEL', sim.warn.fuel, '#ffb020'], ['L ENG', false], ['R ENG', false], ['HYD', false], ['LOW SPD', sim.warn.stall, '#ff3030'], ['G LIM', sim.warn.overg, '#ff3030'], ['REHEAT', sim.ab, '#3f3'], ['BINGO', sim.warn.bingo, '#ffb020'], ['GEAR', sim.gear, '#3f3'], ['CANOPY', false]];
    dwp.forEach((d, i) => I.light(c, 1152 + (i % 2) * 59, 486 + (i / 2 | 0) * 30, 54, 25, d[0], d[1], d[2] || '#ffb020', { px: 9 }));
    label(c, 'DWP', 1208, 645, 9); I.light(c, 1152, 660, 113, 30, 'ATTENTION', sim.warn.fuel || sim.warn.stall || sim.warn.overg, '#ff3030');
    // get-u-home standby
    c.fillStyle = '#1a1c1e'; c.fillRect(10, 480, 128, 250);
    I.adi(c, 74, 552, 44, attitude(sim).th, attitude(sim).ph, { slipv: sim.beta * 8, screws: false });
    I.win(c, 18, 616, 112, 22, `IAS ${Math.round(sim.ias * KT)}`, { px: 12, color: '#fff' });
    I.win(c, 18, 642, 112, 22, `ALT ${Math.round(sim.h * FT / 10) * 10}`, { px: 12, color: '#fff' });
    I.win(c, 18, 668, 112, 22, `HDG ${String(Math.round(sim.psi) % 360).padStart(3, '0')}`, { px: 12, color: '#fff' });
    label(c, 'GET-U-HOME', 74, 706, 9);
    label(c, 'Click a display to change page', 640, 814, 9, '#888', '');
  },
  click(x, y) { this.rects.forEach((r, i) => { if (x > r[0] && x < r[0] + r[2] && y > r[1] && y < r[1] + r[3]) this.sel[i] = (this.sel[i] + 1) % this.pages[i].length; }); },
};

// 4. JA 46: 1970s Nordic analog, metric, Swedish labels
COCKPITS.nordic = {
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 80, l2: 26, bow: 18 });
    drawHUD(c, sim, { rect: [490, 106, 300, 334], metric: true, ladder: true, ladderStep: 10, ladderMax: 120, tapes: false, roll: null, hdgPos: 'top', lbl: { a: 'α', arm: 'ARM', safe: 'SÄK', stall: 'STALL' },
      extra: (c, sim, T, col) => { // altitude bar for low flying: full scale 0-500 m
        const bx = 760, by = 160, bh = 200, f = clamp(sim.h / 5000, 0, 1);
        c.strokeRect(bx, by, 10, bh); c.fillRect(bx, by + bh * (1 - f), 10, bh * f); T('5', bx + 22, by, 11); T('0', bx + 22, by + bh, 11); T('km', bx + 22, by + bh / 2, 10); } });
    panelBg(c, 462, '#202224', '#101112', '#0b0b0c', 'flat');
    const W = [['HUVUD-\nVARNING', sim.warn.fuel || sim.warn.stall || sim.warn.overg, '#ff3030'], ['BRAND', false, '#ff3030'], ['HYDR', false], ['GEN', false], ['BRÄNSLE', sim.warn.fuel], ['EBK', sim.ab, '#3f3'], ['REV', false, '#3f3'], ['ÖVER-\nSTEGRING', sim.warn.stall, '#ff3030'], ['LANDSTÄLL', sim.gear, '#3f3'], ['MARKKOLL', sim.warn.pullup, '#ff3030']];
    W.forEach((w, i) => I.light(c, 98 + i * 109, 468, 100, 32, w[0], w[1], w[2] || '#ffb020', { px: 9 }));
    const { th, ph } = attitude(sim), sw = { face: '#0b0b0b', ink: '#f2f0e4' };
    // left block
    I.dial(c, 110, 585, 54, { ...sw, min: 0, max: 1400, a0: -150, a1: 150, map: v => lerpTab([[0, 0], [200, 0.12], [400, 0.35], [600, 0.55], [800, 0.72], [1000, 0.85], [1400, 1]], v), labels: [200, 400, 600, 800, 1000, 1400], fmt: v => v / 100, labR: 0.6,
      extra: (c, x, y, r) => { I.win(c, x - r * 0.3, y + r * 0.22, r * 0.6, r * 0.24, sim.M.toFixed(2), { px: r * 0.18 }); I.text(c, 'FART km/h', x, y - r * 0.32, r * 0.13, '#ddd', 'center', 'bold'); I.text(c, '×100', x, y - r * 0.15, r * 0.1, '#bbb'); }, needles: [{ v: sim.ias * 3.6 }] });
    I.adi(c, 250, 590, 64, th, ph, { slipv: sim.beta * 8, sky: '#5a6f86', ground: '#2a2a2a', symbol: '#ff9a2a', title: 'ATTITYD' });
    const hm = sim.h;
    I.dial(c, 390, 585, 54, { ...sw, min: 0, max: 1000, a0: 0, a1: 360, major: 100, minor: 20, labels: [0, 100, 200, 300, 400, 500, 600, 700, 800, 900], fmt: v => v / 100, labR: 0.6,
      extra: (c, x, y, r) => { I.win(c, x - r * 0.42, y - r * 0.4, r * 0.84, r * 0.3, `${(hm / 1000).toFixed(0).padStart(2, ' ')} ${String(Math.floor(hm % 1000 / 100))}00`, { px: r * 0.22 }); I.text(c, 'HÖJD m', x, y + r * 0.35, r * 0.13, '#ddd', 'center', 'bold'); }, needles: [{ v: hm % 1000 }] });
    G.aoaDeg(c, 110, 735, 48, sim, { ...sw, title: 'ANFALLS-', units: 'VINKEL °', amax: 22 });
    const hp = relPos(sim, HOME.x, HOME.z);
    I.hsi(c, 250, 740, 58, sim.psi, { course: hp.brg, dist: hp.rng / 1000, title: 'KURS', crsColor: '#ff9a2a', bug: null });
    I.dial(c, 390, 735, 48, { ...sw, min: -100, max: 100, a0: 270 - 165, a1: 270 + 165, map: v => (Math.sign(v) * lerpTab([[0, 0], [10, 0.3], [30, 0.6], [100, 1]], Math.abs(v)) + 1) / 2, labels: [-100, -30, -10, 0, 10, 30, 100], fmt: v => Math.abs(v), labR: 0.6,
      extra: (c, x, y, r) => I.text(c, 'STIG m/s', x + r * 0.2, y, r * 0.13, '#ddd', 'center', 'bold'), needles: [{ v: sim.vs }] });
    // centre: radar display (CI)
    c.fillStyle = '#151617'; c.fillRect(505, 520, 270, 290); c.strokeStyle = '#000'; c.strokeRect(505, 520, 270, 290);
    const cx = 640, cy = 790, R = 245;
    c.save(); c.beginPath(); c.rect(515, 532, 250, 268); c.clip();
    c.fillStyle = '#031208'; c.fillRect(515, 532, 250, 268);
    for (let i = 0; i < 260; i++) { const a = ((i * 37) % 120 - 60) * D2R, rr = ((i * 97) % 100) / 100 * R; if (rr > R * 0.35) continue; c.fillStyle = `rgba(80,255,120,${0.05 + ((i * 13) % 10) / 60})`; c.fillRect(cx + Math.sin(a) * rr, cy - Math.cos(a) * rr, 3, 2); }
    c.strokeStyle = 'rgba(80,255,120,0.35)'; for (const k of [0.25, 0.5, 0.75, 1]) { c.beginPath(); c.arc(cx, cy, R * k, -Math.PI / 2 - 1.05, -Math.PI / 2 + 1.05); c.stroke(); }
    for (const a of [-60, -30, 0, 30, 60]) { const [x2, y2] = polar(cx, cy, R, a); c.beginPath(); c.moveTo(cx, cy); c.lineTo(x2, y2); c.stroke(); }
    const sa = Math.sin(sim.t * 1.2) * 60, [sx, sy] = polar(cx, cy, R, sa); c.strokeStyle = 'rgba(120,255,150,0.9)'; c.lineWidth = 2; c.beginPath(); c.moveTo(cx, cy); c.lineTo(sx, sy); c.stroke();
    for (const t of sim.contacts) { const r = relPos(sim, t.x, t.z); if (Math.abs(r.rel) > 60 || r.rng > 60000) continue; const [tx, ty] = polar(cx, cy, r.rng / 60000 * R, r.rel); c.fillStyle = '#8f8'; c.fillRect(tx - 5, ty - 2, 10, 4); }
    c.restore();
    I.text(c, 'CI  60 km', 525, 540, 10, '#8f8', 'left', 'bold', MONO);
    // right block
    G.rpm(c, 890, 585, 52, sim, 0, { ...sw, title: 'VARVTAL' });
    G.egt(c, 1030, 585, 52, sim, 0, { ...sw, title: 'AVGASTEMP' });
    I.dial(c, 1170, 585, 52, { ...sw, min: 0, max: 100, a0: -135, a1: 135, major: 10, minor: 5, labels: [0, 25, 50, 75, 100], labR: 0.6, title: 'BRÄNSLE', titleY: 0.35, units: '%', unitsY: 0.55, arcs: [{ from: 0, to: 20, color: '#d22' }], needles: [{ v: sim.fuel / sim.a.fuel * 100 }] });
    G.gMeter(c, 890, 735, 48, sim, { ...sw, title: 'G', gmax: 7.5 });
    G.noz(c, 1030, 735, 48, sim, 0, { ...sw, title: 'MUNSTYCKE', units: '% ÖPPEN' });
    G.clock(c, 1170, 735, 46, sim, { ...sw, title: 'UR' });
  },
  glossary: [['FART', 'airspeed'], ['HÖJD', 'altitude'], ['STIG', 'vertical speed'], ['KURS', 'heading'], ['ANFALLSVINKEL', 'angle of attack'], ['VARVTAL', 'engine RPM'], ['AVGASTEMP', 'exhaust temperature'], ['BRÄNSLE', 'fuel'], ['MUNSTYCKE', 'nozzle position'], ['HUVUDVARNING', 'master warning'], ['BRAND', 'fire'], ['EBK', 'afterburner lit'], ['REV', 'thrust reverser'], ['ÖVERSTEGRING', 'stall (excess AoA)'], ['LANDSTÄLL', 'landing gear'], ['MARKKOLL', 'ground collision'], ['CI', 'central radar indicator'], ['UR', 'clock'], ['SÄK', 'safe']],
};

// 5. SY-26: wide-area LCD, metric, Chinese/English labels
COCKPITS.dragon = {
  pages: [['姿态 PFD', '发动机 ENG'], ['战术 TAC', '雷达 RDR'], ['发动机 ENG', '武器 WPN', '燃油 FUEL']], sel: [0, 0, 0], rects: [[130, 540, 330, 262], [470, 540, 340, 262], [820, 540, 330, 262]],
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 44, l2: 10 });
    drawHUD(c, sim, { rect: [430, 66, 420, 384], metric: true, tapes: true, roll: 'top', hdgPos: 'top', vsReadout: true, lbl: { stall: '失速 STALL', pullup: '拉起 PULL UP' } });
    panelBg(c, 468, '#2a2c2f', '#111213', '#0b0c0d');
    c.fillStyle = '#080909'; c.fillRect(120, 528, 1040, 284); c.strokeStyle = '#3b3e41'; c.lineWidth = 3; c.strokeRect(120, 528, 1040, 284);
    const o = { ink: '#e6f7ff', bg: '#050a0d', hostile: '#ff5a3c', friend: '#3cd2ff', threat: '#ff5a3c', metric: true, arc: '#3cd2ff', terrain: 'rgba(90,120,100,0.3)' };
    this.rects.forEach((r, i) => {
      const s = { x: r[0], y: r[1] + 20, w: r[2], h: r[3] - 20 }, pg = this.pages[i][this.sel[i]];
      c.fillStyle = '#10202b'; c.fillRect(r[0], r[1], r[2], 18);
      this.pages[i].forEach((p, k) => I.text(c, p, r[0] + 8 + k * 100, r[1] + 9, 11, k === this.sel[i] ? '#6ff' : '#7f8f99', 'left', 'bold'));
      if (pg.endsWith('PFD')) Pages.pfd(c, s, sim, { metric: true, vsLbl: '升降' });
      else if (pg.endsWith('TAC')) Pages.map(c, s, sim, { ...o, scale: 160, labels: true, homeLbl: '基地' });
      else if (pg.endsWith('RDR')) Pages.radarB(c, s, sim, { ...o, ink: '#6ff', dim: 'rgba(100,255,255,0.3)', symbols: true, mode: 'TWS 远程', scale: 160 });
      else if (pg.endsWith('WPN')) Pages.sms(c, s, sim, { ...o, armTxt: '武器 ARM' });
      else Pages.eng(c, s, sim, { ...o, rpmLbl: '转速', tLbl: '排温', nozLbl: '喷口', ffLbl: '油耗', fuelLbl: '燃油', abLbl: '加力', thrLbl: '推力' });
    });
    // UFCP
    c.fillStyle = '#1b1d1f'; c.fillRect(500, 474, 280, 48);
    I.win(c, 508, 479, 264, 18, `通信 COM1 243.0   敌我 IFF M3 1200`, { px: 10, color: '#7fe3ff', fam: FONT });
    I.win(c, 508, 500, 264, 18, `航点 WPT 05  ${(relPos(sim, HOME.x, HOME.z).rng / 1000).toFixed(0)} km  自驾 ${sim.auto ? 'AP' : '关'}`, { px: 10, color: '#7fe3ff', fam: FONT });
    I.light(c, 404, 474, 88, 22, '主告警 WARN', sim.warn.pullup || sim.warn.overg, '#ff3030', { px: 9 });
    I.light(c, 404, 500, 88, 22, '注意 CAUT', sim.warn.fuel || sim.warn.stall, '#ffb020', { px: 9 });
    I.light(c, 788, 474, 88, 22, '加力 A/B', sim.ab, '#3f3', { px: 9 }); I.light(c, 788, 500, 88, 22, '低油 FUEL', sim.warn.fuel, '#ffb020', { px: 9 });
    // standby on the left console
    c.fillStyle = '#0a0b0c'; c.fillRect(14, 560, 96, 100); Pages.pfd(c, { x: 17, y: 563, w: 90, h: 94 }, sim, { metric: true, compact: true });
    label(c, '备份 STBY', 62, 672, 9); gearLights(c, 62, 720, sim, '起落架 GEAR');
    Pages.rwr(c, 1220, 620, 46, sim, { ink: '#ff8a5a', ring: 'rgba(255,140,90,0.35)' }); label(c, '告警 RWR', 1220, 676, 9);
    label(c, 'Click a display area to change page', 640, 816, 9, '#777', '');
  },
  click: COCKPITS.euro.click,
  glossary: [['姿态', 'attitude (PFD)'], ['战术', 'tactical map'], ['雷达', 'radar'], ['发动机', 'engine'], ['武器', 'weapons'], ['燃油', 'fuel'], ['转速', 'RPM'], ['排温', 'exhaust temp'], ['喷口', 'nozzle'], ['油耗', 'fuel flow'], ['加力', 'afterburner'], ['推力', 'thrust'], ['升降', 'vertical speed'], ['主告警', 'master warning'], ['注意', 'caution'], ['低油', 'low fuel'], ['起落架', 'landing gear'], ['备份', 'standby'], ['失速', 'stall'], ['拉起', 'pull up'], ['航点', 'waypoint'], ['通信', 'radio'], ['敌我', 'IFF'], ['基地', 'home base']],
};

// 6. Vk-33: classic Soviet analog, turquoise panel, metric, Cyrillic
COCKPITS.soviet = {
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 86, l2: 30, bow: 24, color: '#2a3a3c' });
    drawHUD(c, sim, { rect: [496, 112, 288, 330], metric: true, ladder: false, tapes: false, roll: 'bottom', hdgPos: 'bottom', style: 'soviet', lbl: { v: '', h: '', a: 'α', arm: 'ГОТ', safe: 'ПРЕД', stall: 'КРИТ α', pullup: 'ОПАСНАЯ ВЫСОТА' }, wpnName: w => (w.name.startsWith('SRM') ? 'Р-МД' : w.name.startsWith('MRM') ? 'Р-СД' : 'Р-БД'),
      extra: (c, sim, T, col) => { c.beginPath(); c.arc(EYE[0], EYE[1] + 20, 46, 0, 7); c.stroke(); // aiming circle with horizon bar
        const { th, ph } = attitude(sim); c.save(); c.translate(EYE[0], EYE[1]); c.rotate(-ph * D2R); c.beginPath(); c.moveTo(-90, th * PPD); c.lineTo(-40, th * PPD); c.moveTo(40, th * PPD); c.lineTo(90, th * PPD); c.stroke(); c.restore(); } });
    panelBg(c, 464, '#4f9196', '#2e6266', '#1c2a2c', 'flat');
    const sw = { face: '#0d0d0d', ink: '#f4f4ee', rim1: '#2f3b3c', rim2: '#0f1515' }, { th, ph } = attitude(sim);
    // centre: KPP attitude (moving aircraft symbol) + PNP
    I.adi(c, 640, 584, 74, th, ph, { soviet: true, sky: '#4c84b9', ground: '#5b3d24', symbol: '#ffe14a', ...sw, slipv: sim.beta * 8 });
    label(c, 'КПП', 640, 497, 10, '#e9f5f5');
    const hp = relPos(sim, HOME.x, HOME.z);
    I.hsi(c, 640, 746, 60, sim.psi, { cyr: true, course: hp.brg, dist: hp.rng / 1000, ...sw, crsColor: '#ffe14a', bug: null, cardinal: '#ffd25a' });
    label(c, 'ПНП', 640, 812, 10, '#e9f5f5');
    // left: KUS dual airspeed, M, altitude, UAP, VAR, clock
    I.dial(c, 506, 552, 50, { ...sw, min: 0, max: 1600, a0: -150, a1: 150, map: v => lerpTab([[0, 0], [200, 0.1], [600, 0.45], [1000, 0.72], [1600, 1]], v), labels: [200, 400, 600, 800, 1000, 1200, 1600], fmt: v => v / 100, labR: 0.6,
      extra: (c, x, y, r) => { I.text(c, 'КМ/Ч', x, y + r * 0.35, r * 0.13, '#ddd', 'center', 'bold'); I.text(c, '×100', x, y - r * 0.3, r * 0.1, '#bbb'); },
      needles: [{ v: sim.V * 3.6, w: 0.03, color: '#ffcc33' }, { v: sim.ias * 3.6, w: 0.08 }] });
    label(c, 'КУС (ПРИБ/ИСТ)', 506, 614, 9, '#e9f5f5');
    I.dial(c, 376, 552, 46, { ...sw, min: 0.5, max: 3, a0: -135, a1: 135, major: 0.5, minor: 0.1, labels: [0.5, 1, 1.5, 2, 2.5, 3], labR: 0.6, title: 'М', titleY: 0.38, titlePx: 0.2, needles: [{ v: sim.M }] });
    label(c, 'ЧИСЛО М', 376, 614, 9, '#e9f5f5');
    const hm = sim.h;
    I.dial(c, 506, 680, 50, { ...sw, min: 0, max: 1000, a0: 0, a1: 360, major: 100, minor: 20, labels: [0, 100, 200, 300, 400, 500, 600, 700, 800, 900], fmt: v => v / 100, labR: 0.62,
      extra: (c, x, y, r) => { // inner km scale for the short needle
        for (let k = 0; k < 20; k += 2) { const [lx, ly] = polar(x, y, r * 0.36, k * 18); I.text(c, String(k), lx, ly, r * 0.1, '#bbb'); }
        I.text(c, 'ВД-20', x, y + r * 0.55, r * 0.1, '#ddd'); I.win(c, x + r * 0.15, y + r * 0.12, r * 0.42, r * 0.2, '760', { px: r * 0.14, color: '#ddd' }); },
      needles: [{ v: hm / 20000 * 1000, len: 0.45, w: 0.1, color: '#ddd' }, { v: hm % 1000, len: 0.9, w: 0.05 }] });
    label(c, 'ВЫСОТА М', 506, 742, 9, '#e9f5f5');
    I.dial(c, 376, 680, 46, { ...sw, min: -10, max: 40, a0: -150, a1: 60, major: 5, minor: 1, labels: [-10, 0, 10, 20, 30, 40], labR: 0.66, arcs: [{ from: 26, to: 40, color: '#d22' }],
      extra: (c, x, y, r) => { // g scale on the lower-right arc
        c.strokeStyle = '#ffcc33'; for (let g = -2; g <= 10; g++) { const a = 160 - (g + 2) / 12 * 85, [x1, y1] = polar(x, y, r * 0.92, a), [x2, y2] = polar(x, y, r * 0.8, a); c.lineWidth = g % 2 ? 1 : 2; c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke(); if (!(g % 2)) { const [lx, ly] = polar(x, y, r * 0.66, a); I.text(c, String(g), lx, ly, r * 0.14, '#ffcc33'); } }
        I.needle(c, x, y, r * 0.86, 160 - (clamp(sim.n, -2, 10) + 2) / 12 * 85, { color: '#ffcc33', w: r * 0.05, hub: false });
        I.text(c, 'α', x - r * 0.3, y - r * 0.1, r * 0.2, '#fff', 'center', 'bold'); I.text(c, 'n', x + r * 0.32, y + r * 0.1, r * 0.2, '#ffcc33', 'center', 'bold'); },
      needles: [{ v: sim.alpha }] });
    label(c, 'УАП (α / n)', 376, 742, 9, '#e9f5f5');
    I.dial(c, 506, 778, 30, { ...sw, min: -300, max: 300, a0: 270 - 160, a1: 270 + 160, map: v => (Math.sign(v) * lerpTab([[0, 0], [20, 0.35], [100, 0.7], [300, 1]], Math.abs(v)) + 1) / 2, labels: [-100, -20, 0, 20, 100], fmt: v => Math.abs(v), labR: 0.6, labPx: 0.18, screws: false, needles: [{ v: sim.vs }] });
    label(c, 'ВАР М/С', 446, 778, 9, '#e9f5f5');
    G.clock(c, 376, 780, 28, sim, { ...sw, screws: false });
    // right: dual tachometer, dual EGT, fuel
    I.dial(c, 774, 552, 50, { ...sw, min: 0, max: 110, a0: -135, a1: 135, map: v => lerpTab([[0, 0], [60, 0.3], [110, 1]], v), labels: [0, 60, 70, 80, 90, 100], labR: 0.6, title: 'ОБОРОТЫ %', titleY: 0.36,
      needles: [{ v: sim.rpm[0], color: '#fff' }, { v: sim.rpm[1] - 0.4, color: '#ffcc33', w: 0.04 }],
      extra: (c, x, y, r) => { I.text(c, '1', x - r * 0.25, y + r * 0.55, r * 0.15, '#fff', 'center', 'bold'); I.text(c, '2', x + r * 0.25, y + r * 0.55, r * 0.15, '#ffcc33', 'center', 'bold'); } });
    label(c, 'ИТА-2', 774, 614, 9, '#e9f5f5');
    I.dial(c, 904, 552, 46, { ...sw, min: 300, max: 1000, a0: -135, a1: 135, major: 100, minor: 20, labels: [300, 500, 700, 900], fmt: v => v / 100, labR: 0.6, title: 'Т ГАЗОВ', titleY: 0.36, units: '°C×100', unitsY: 0.56, arcs: [{ from: 900, to: 1000, color: '#d22' }],
      needles: [{ v: sim.egt[0] - 100, color: '#fff' }, { v: sim.egt[1] - 100, color: '#ffcc33', w: 0.04 }] });
    label(c, 'ТСТ-2', 904, 614, 9, '#e9f5f5');
    I.dial(c, 774, 680, 50, { ...sw, min: 0, max: 4000, a0: -135, a1: 135, major: 500, minor: 100, labels: [0, 1000, 2000, 3000, 4000], fmt: v => v / 1000, labR: 0.6, title: 'ТОПЛИВО', titleY: 0.36, units: 'КГ ×1000', unitsY: 0.56, arcs: [{ from: 0, to: 800, color: '#d22' }], needles: [{ v: sim.fuel }] });
    label(c, 'ТОПЛИВОМЕР', 774, 742, 9, '#e9f5f5');
    I.dial(c, 904, 680, 46, { ...sw, min: 0, max: 100, a0: -120, a1: 120, major: 10, minor: 5, labels: [0, 50, 100], labR: 0.6, title: 'СОПЛО', titleY: 0.36, units: '%', unitsY: 0.56, needles: [{ v: sim.noz[0] }, { v: sim.noz[1], color: '#ffcc33', w: 0.04 }] });
    label(c, 'ПОЛОЖ. СОПЛА', 904, 742, 9, '#e9f5f5');
    // SPO threat warning panel (direction lights)
    c.fillStyle = '#121a1b'; c.fillRect(40, 478, 270, 120); c.strokeStyle = '#000'; c.strokeRect(40, 478, 270, 120);
    label(c, 'СПО', 60, 490, 9, '#e9f5f5');
    c.strokeStyle = '#ccc'; c.lineWidth = 2; c.beginPath(); c.moveTo(175, 512); c.lineTo(175, 572); c.moveTo(145, 540); c.lineTo(205, 540); c.moveTo(160, 566); c.lineTo(190, 566); c.stroke();
    const dirs = [-157.5, -112.5, -67.5, -22.5, 22.5, 67.5, 112.5, 157.5];
    const thr = [...sim.emitters, ...sim.contacts.filter(t => t.hostile)].map(e => relPos(sim, e.x, e.z)).filter(r => r.rng < 90000);
    dirs.forEach((d, i) => { const [lx, ly] = polar(175, 542, 46, d), on = thr.some(r => Math.abs(wrap180(r.rel - d)) < 22.5); c.beginPath(); c.arc(lx, ly, 6, 0, 7); c.fillStyle = on ? '#ff3a2a' : '#3a1a18'; c.fill(); });
    ['П', 'Х', 'Н', 'F', 'Д'].forEach((t, i) => I.light(c, 236 + (i % 2) * 34, 492 + (i / 2 | 0) * 30, 28, 22, t, i === 0 && thr.length > 0, '#ffb020'));
    // weapon panel
    c.fillStyle = '#121a1b'; c.fillRect(40, 608, 270, 64);
    sim.stores.filter(w => !w.alt).forEach((w, i) => I.text(c, `${w.name.startsWith('SRM') ? 'РАКЕТЫ МД' : 'РАКЕТЫ СД'} ×${w.n}${i === sim.wpn ? ' ◄' : ''}`, 54, 624 + i * 18, 11, '#9f9', 'left', 'bold', MONO));
    I.text(c, `ПУШКА ${sim.gunRds}`, 300, 624, 11, '#9f9', 'right', 'bold', MONO);
    gearLights(c, 90, 720, sim, 'ШАССИ', '#3f3');
    I.light(c, 150, 690, 74, 24, 'ФОРСАЖ', sim.ab, '#3f3', { px: 9 }); I.light(c, 230, 690, 74, 24, 'ГЛАВНЫЙ\nВЫКЛ.', sim.master, '#3f3', { px: 8 });
    // warning lights block (right)
    const L = [['ПОЖАР', false, '#ff3030'], ['ОПАСНАЯ\nВЫСОТА', sim.warn.pullup, '#ff3030'], ['ОСТАТОК\n800', sim.fuel < 800, '#ffb020'], ['КРИТ. α', sim.warn.stall, '#ff3030'], ['ПЕРЕГРУЗКА', sim.warn.overg, '#ff3030'], ['ПРЕДЕЛ\nСКОРОСТИ', sim.warn.speed, '#ffb020'], ['ГИДРО', false], ['ГЕНЕР.', false], ['ВЫПУСТИ\nШАССИ', sim.h < 300 && !sim.gear, '#ff3030'], ['РЕЗЕРВ', sim.warn.bingo]];
    c.fillStyle = '#121a1b'; c.fillRect(980, 478, 280, 200);
    L.forEach((l, i) => I.light(c, 990 + (i % 2) * 134, 488 + (i / 2 | 0) * 38, 126, 32, l[0], l[1], l[2] || '#ffb020', { px: 10 }));
    for (const [x, y] of [[44, 482], [306, 482], [984, 482], [1256, 482]]) screw(c, x, y);
  },
  glossary: [['КПП', 'attitude director (aircraft symbol banks, horizon only pitches)'], ['ПНП', 'HSI'], ['КУС (ПРИБ/ИСТ)', 'airspeed, wide needle indicated, thin needle true'], ['ЧИСЛО М', 'Mach number'], ['ВЫСОТА / ВД-20', 'altimeter, long needle m, short needle km'], ['УАП (α / n)', 'angle of attack and g (white α, yellow n)'], ['ВАР', 'vertical speed m/s'], ['ОБОРОТЫ', 'engine RPM (needle 1 and 2)'], ['Т ГАЗОВ', 'exhaust gas temperature'], ['ТОПЛИВО', 'fuel kg'], ['СОПЛО', 'nozzle position'], ['СПО', 'radar warning receiver'], ['РАКЕТЫ МД / СД', 'short- / medium-range missiles'], ['ПУШКА', 'gun'], ['ШАССИ', 'landing gear'], ['ФОРСАЖ', 'afterburner'], ['ПОЖАР', 'fire'], ['ОПАСНАЯ ВЫСОТА', 'dangerous altitude'], ['ОСТАТОК 800', '800 kg fuel left'], ['КРИТ. α', 'critical angle of attack'], ['ПЕРЕГРУЗКА', 'over-g'], ['ПРЕДЕЛ СКОРОСТИ', 'speed limit'], ['ВЫПУСТИ ШАССИ', 'lower the gear'], ['ГОТ / ПРЕД', 'armed / safe']],
};

// 7. Ks-55: modern Russian glass, two large MFIs
COCKPITS.russian_glass = {
  pages: [['ПНИ', 'ДВИГ'], ['ТАКТ', 'РЛС', 'ВООР']], sel: [0, 0], rects: [[220, 500, 390, 312], [670, 500, 390, 312]],
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 48, l2: 10, bow: null });
    drawHUD(c, sim, { rect: [432, 64, 416, 386], metric: true, tapes: true, roll: 'bottom', hdgPos: 'top', vsReadout: true, lbl: { arm: 'ГОТ', safe: 'ПРЕД', stall: 'КРИТ α', pullup: 'ВЫВОД' }, wpnName: w => (w.name.startsWith('SRM') ? 'МД' : 'СД') });
    panelBg(c, 464, '#2f3337', '#15171a', '#0d0e10');
    const o = { ink: '#e8ecf0', bg: '#04080b', hostile: '#ff4a3a', friend: '#4ad2ff', threat: '#ff4a3a', metric: true, arc: '#7cff6a', terrain: 'rgba(110,120,90,0.3)' };
    this.rects.forEach((r, i) => {
      const pg = this.pages[i][this.sel[i]];
      const s = I.mfd(c, r[0], r[1], r[2], r[3], { frame: '#25282b', ink: '#9df', bez: 30, buttons: 6, labels: { T0: this.pages[i][0], T1: this.pages[i][1], T2: this.pages[i][2] || '', B5: 'МЕНЮ' }, sel: 'T' + this.sel[i], px: 11 });
      if (pg === 'ПНИ') Pages.pfd(c, s, sim, { metric: true, vsLbl: 'Vy' });
      else if (pg === 'ДВИГ') Pages.eng(c, s, sim, { ...o, rpmLbl: 'ОБ', tLbl: 'Т', nozLbl: 'СОПЛО', ffLbl: 'РАСХ', fuelLbl: 'ТОПЛ', abLbl: 'ФОРС', thrLbl: 'ТЯГА' });
      else if (pg === 'ТАКТ') Pages.map(c, s, sim, { ...o, scale: 120, labels: true, homeLbl: 'АЭР' });
      else if (pg === 'РЛС') Pages.radarB(c, s, sim, { ...o, ink: '#7f7', dim: 'rgba(120,255,120,0.3)', symbols: true, mode: 'ОБЗОР', scale: 120 });
      else Pages.sms(c, s, sim, { ...o, armTxt: 'ГЛАВ ВКЛ' });
      s.labels();
    });
    // side displays
    c.fillStyle = '#0a0b0c'; c.fillRect(30, 540, 170, 190);
    Pages.pfd(c, { x: 34, y: 544, w: 162, h: 140 }, sim, { metric: true, compact: true });
    I.win(c, 34, 688, 162, 18, `V ${Math.round(sim.ias * 3.6)} км/ч  M ${sim.M.toFixed(2)}`, { px: 10, color: '#fff' });
    I.win(c, 34, 708, 162, 18, `H ${Math.round(sim.h)} м  К ${String(Math.round(sim.psi) % 360).padStart(3, '0')}`, { px: 10, color: '#fff' });
    label(c, 'РЕЗЕРВ. ПРИБОР', 115, 740, 9);
    c.fillStyle = '#0a0b0c'; c.fillRect(1080, 540, 170, 190);
    Pages.rwr(c, 1165, 618, 62, sim, { ink: '#ff9a5a', ring: 'rgba(255,150,90,0.35)', bg: '#06090b' });
    I.win(c, 1084, 692, 162, 18, `ТОПЛ ${Math.round(sim.fuel)} кг`, { px: 11, color: sim.warn.fuel ? '#fa3' : '#fff' });
    I.win(c, 1084, 712, 162, 18, `ОБ ${sim.rpm[0].toFixed(0)}/${sim.rpm[1].toFixed(0)} %`, { px: 11, color: '#fff' });
    label(c, 'СПО / ДВИГ', 1165, 740, 9);
    const L = [['ПОЖАР', false, '#ff3030'], ['ОПАСНО', sim.warn.pullup || sim.warn.overg, '#ff3030'], ['ОТКАЗ', false], ['ТОПЛИВО', sim.warn.fuel], ['ФОРСАЖ', sim.ab, '#3f3'], ['ШАССИ', sim.gear, '#3f3']];
    L.forEach((l, i) => I.light(c, (i < 3 ? 70 : 1080) + (i % 3) * 66 - (i < 3 ? 0 : 0), 482, 60, 26, l[0], l[1], l[2] || '#ffb020', { px: 9 }));
    label(c, 'Click a display to change page', 640, 815, 9, '#777', '');
  },
  click: COCKPITS.lightweight.click,
  glossary: [['ПНИ', 'primary flight display'], ['ДВИГ', 'engines'], ['ТАКТ', 'tactical map'], ['РЛС', 'radar'], ['ВООР', 'weapons'], ['ОБ', 'RPM'], ['Т', 'exhaust temperature'], ['СОПЛО', 'nozzle'], ['РАСХ', 'fuel flow'], ['ТОПЛ', 'fuel'], ['ФОРС / ФОРСАЖ', 'afterburner'], ['ТЯГА', 'thrust'], ['МЕНЮ', 'menu'], ['ОБЗОР', 'search mode'], ['РЕЗЕРВ. ПРИБОР', 'standby instrument'], ['СПО', 'radar warning'], ['ПОЖАР', 'fire'], ['ОПАСНО', 'danger'], ['ОТКАЗ', 'failure'], ['ШАССИ', 'landing gear'], ['ГОТ / ПРЕД', 'armed / safe'], ['МД / СД', 'short / medium-range missile'], ['КРИТ α', 'critical AoA'], ['ВЫВОД', 'pull out (recover)'], ['АЭР', 'airfield']],
};

// 8. NF-24: 1970s US Navy pilot cockpit with VDI/HSD, wing-sweep indicator, AoA indexer
COCKPITS.navy = {
  draw(c, sim) {
    drawWorld(c, sim);
    canopyFrame(c, { l: 70, l2: 22, bow: 14, mirror: true });
    drawHUD(c, sim, { rect: [500, 108, 280, 334], tapes: false, roll: null, hdgPos: 'top', ebracket: true, ladderStep: 5, ladderMax: 110 });
    // AoA indexer beside the HUD: slow (green up chevron), on-speed (amber donut), fast (red down chevron)
    const units = sim.alpha * 1.43 + 3.5;
    const live = sim.gear;
    const idx = (x, y) => { if (!live) { c.save(); c.fillStyle = '#0d0e0f'; c.fillRect(x - 16, y - 26, 32, 64); c.restore(); }
      c.save(); c.lineWidth = 5; c.strokeStyle = live && units > 16.5 ? '#3f3' : '#1a2a1a'; c.beginPath(); c.moveTo(x - 14, y - 10); c.lineTo(x, y - 22); c.lineTo(x + 14, y - 10); c.stroke();
      c.strokeStyle = live && Math.abs(units - 15) <= 1.5 ? '#ffb020' : '#2a2210'; c.beginPath(); c.arc(x, y + 6, 8, 0, 7); c.stroke();
      c.strokeStyle = live && units < 13.5 ? '#ff3030' : '#2a1414'; c.beginPath(); c.moveTo(x - 14, y + 22); c.lineTo(x, y + 34); c.lineTo(x + 14, y + 22); c.stroke(); c.restore(); };
    c.fillStyle = '#16181a'; c.fillRect(452, 240, 40, 90); idx(472, 280);
    panelBg(c, 462, '#4b5156', '#2b2f33', '#17191b');
    const sw = { face: '#0c0c0c' }, { th, ph } = attitude(sim);
    // VDI (vertical display indicator): contact-analog CRT
    c.fillStyle = '#1a1c1e'; c.fillRect(540, 470, 200, 176);
    I.clip(c, { x: 552, y: 480, w: 176, h: 156 }, () => {
      const cx = 640, cy = 558; c.fillStyle = '#021006'; c.fillRect(552, 480, 176, 156);
      c.save(); c.translate(cx, cy); c.rotate(-ph * D2R); const ppd = 3.2, hy = th * ppd;
      c.fillStyle = 'rgba(40,140,60,0.25)'; c.fillRect(-200, hy, 400, 300);
      c.strokeStyle = '#6f6'; c.lineWidth = 1.5; c.beginPath(); c.moveTo(-200, hy); c.lineTo(200, hy); c.stroke();
      for (let k = 1; k < 8; k++) { const yy = hy + k * k * 3; c.beginPath(); c.moveTo(-200, yy); c.lineTo(200, yy); c.stroke(); }
      for (let p = -60; p <= 60; p += 10) { if (!p) continue; const yy = hy - p * ppd; c.beginPath(); c.moveTo(-24, yy); c.lineTo(24, yy); c.stroke(); I.text(c, String(p), 34, yy, 9, '#6f6', 'center', '', MONO); }
      c.restore();
      c.strokeStyle = '#9f9'; c.lineWidth = 2; c.beginPath(); c.moveTo(cx - 40, cy); c.lineTo(cx - 12, cy); c.lineTo(cx, cy + 8); c.lineTo(cx + 12, cy); c.lineTo(cx + 40, cy); c.stroke();
      I.text(c, `${Math.round(sim.ias * KT)}`, 560, 492, 11, '#9f9', 'left', 'bold', MONO); I.text(c, `${Math.round(sim.h * FT / 10) * 10}`, 720, 492, 11, '#9f9', 'right', 'bold', MONO);
    });
    label(c, 'VDI', 640, 652, 9);
    // HSD (horizontal situation display)
    c.fillStyle = '#1a1c1e'; c.fillRect(540, 658, 200, 160);
    Pages.map(c, { x: 552, y: 666, w: 176, h: 146 }, sim, { ink: '#9f9', bg: '#021006', hostile: '#9f9', friend: '#9f9', threat: '#9f9', ring: 'rgba(120,255,120,0.4)', route: '#9f9', own: '#9f9', scale: 100, cy: 0.62 });
    // left: ASI, AoA tape, standby ADI, altimeter, VSI, wing sweep, g, clock
    G.asiKt(c, 448, 528, 46, sim);
    // AoA units vertical tape
    c.fillStyle = '#0c0c0c'; c.fillRect(352, 482, 40, 130); c.strokeStyle = '#555'; c.strokeRect(352, 482, 40, 130);
    for (let u = 0; u <= 30; u += 5) { const yy = 606 - u * 4; c.strokeStyle = '#eee'; c.beginPath(); c.moveTo(352, yy); c.lineTo(362, yy); c.stroke(); I.text(c, String(u), 378, yy, 9, '#eee'); }
    c.fillStyle = '#ffb020'; c.fillRect(352, 606 - 15 * 4 - 3, 6, 6);
    c.fillStyle = '#fff'; c.fillRect(354, 606 - clamp(units, 0, 30) * 4 - 1, 36, 3); label(c, 'AOA UNITS', 372, 620, 8);
    I.adi(c, 448, 652, 46, th, ph, { slipv: sim.beta * 8, sky: '#7a7a7a', ground: '#262626', title: null });
    G.altFt(c, 448, 762, 38, sim);
    G.vviFpm(c, 360, 690, 36, sim);
    G.clock(c, 360, 776, 28, sim, sw);
    // wing sweep indicator: commanded and actual position
    I.dial(c, 210, 540, 58, { ...sw, min: 20, max: 68, a0: -100, a1: 100, major: 4, minor: 2, labels: [20, 28, 36, 44, 52, 60, 68], labR: 0.66, title: 'WING SWEEP', titleY: 0.32, titlePx: 0.12, units: 'AUTO', unitsY: 0.52,
      needles: [{ v: sim.sweepCmd, color: '#ffcc33', w: 0.04 }, { v: sim.sweep, w: 0.07 }] });
    G.gMeter(c, 210, 688, 46, sim, { ...sw, gmax: 7.5, title: 'ACCEL' });
    gearLights(c, 90, 760, sim, 'GEAR');
    I.light(c, 40, 640, 94, 26, 'WING SWEEP', Math.abs(sim.sweep - sim.sweepCmd) > 4, '#ffb020', { px: 9 });
    I.light(c, 40, 672, 94, 26, 'MASTER\nCAUTION', sim.warn.fuel || sim.warn.stall, '#ffb020', { px: 8 });
    I.light(c, 40, 704, 94, 26, 'HOOK', false, '#ffb020', { px: 9 });
    // right: dual engine instruments
    const rx = [830, 918];
    rx.forEach((x, i) => { G.rpm(c, x, 512, 36, sim, i, { title: i ? 'RPM R' : 'RPM L' }); G.egt(c, x, 596, 36, sim, i, { title: i ? 'TIT R' : 'TIT L' });
      I.dial(c, x, 680, 36, { min: 0, max: 20, a0: -135, a1: 135, major: 2, minor: 1, labels: [0, 4, 8, 12, 16, 20], labR: 0.6, title: 'FF', titleY: 0.35, units: 'PPH×1000', unitsY: 0.56, needles: [{ v: sim.ff[i] * 2.2046 / 1000 }] });
      G.noz(c, x, 764, 34, sim, i, { title: 'NOZ', units: '' }); });
    // fuel quantity with totalizer
    I.dial(c, 1060, 560, 64, { min: 0, max: 8, a0: -135, a1: 135, major: 1, minor: 0.5, labels: [0, 2, 4, 6, 8], labR: 0.62, title: 'FUEL QTY', titleY: -0.32, units: 'LB ×1000', unitsY: -0.15,
      extra: (c, x, y, r) => { I.win(c, x - r * 0.45, y + r * 0.18, r * 0.9, r * 0.26, String(Math.round(sim.fuel * 2.2046 / 10) * 10).padStart(5, '0'), { px: r * 0.2 }); I.text(c, 'TOTAL', x, y + r * 0.56, r * 0.1, '#ddd'); },
      needles: [{ v: sim.fuel * 2.2046 / 2000 + 0.05, color: '#fff' }, { v: sim.fuel * 2.2046 / 2000 - 0.05, color: '#ffcc33', w: 0.04 }] });
    label(c, 'L (white)   R (yellow) feed', 1060, 640, 8);
    Pages.rwr(c, 1060, 740, 54, sim); label(c, 'ALR THREAT', 1060, 806, 8);
    I.light(c, 1150, 670, 110, 26, 'L FIRE', false, '#ff3030', { px: 9 }); I.light(c, 1150, 700, 110, 26, 'R FIRE', false, '#ff3030', { px: 9 });
    I.light(c, 1150, 730, 110, 26, 'A/B', sim.ab, '#3f3', { px: 9 }); I.light(c, 1150, 760, 110, 26, 'BINGO', sim.warn.bingo, '#ffb020', { px: 9 });
    I.text(c, 'RIO cockpit (radar, long-range missiles) sits behind', 1205, 480, 9, '#aaa', 'right');
  },
  glossary: [['VDI', 'vertical display indicator (attitude CRT)'], ['HSD', 'horizontal situation display (map CRT)'], ['AOA UNITS', 'Navy AoA units; 15 = on-speed for landing'], ['AoA indexer', 'green ^ = slow, amber o = on-speed, red v = fast'], ['WING SWEEP', 'yellow = commanded by the computer, white = actual'], ['TIT', 'turbine inlet temperature'], ['FF', 'fuel flow'], ['ALR', 'radar warning receiver'], ['RIO', 'radar intercept officer, the back-seater']],
};
