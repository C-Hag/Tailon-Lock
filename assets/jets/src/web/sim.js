// Point-mass flight model: the same equations as perf.py, so the spec card and the
// cockpit agree. Wind-axis integration, ISA atmosphere, Mach-dependent drag and thrust.
const G0 = 9.80665, KT = 1.943844, FT = 3.28084;
function isa(h) {
  h = Math.max(0, h);
  let T, p;
  if (h < 11000) { T = 288.15 - 0.0065 * h; p = 101325 * Math.pow(T / 288.15, 5.25588); }
  else { T = 216.65; p = 22632.1 * Math.exp(-(h - 11000) / 6341.62); }
  const rho = p / (287.053 * T);
  return { T, p, rho, a: Math.sqrt(1.4 * 287.053 * T) };
}
const RHO11 = isa(11000).rho;
function waveF(M) { if (M < 0.85) return 0; if (M < 1.05) { const x = (M - 0.85) / 0.2; return x * x * (3 - 2 * x); } return Math.max(0.55, 1 - 0.28 * (M - 1.05)); }
function sweepF(a, M) { return a.k_swept ? Math.min(1, Math.max(0, (M - 0.7) / 0.5)) : 0; }
function cd0(a, M) { return a.cd0 * (1 + a.wave * waveF(M)); }
function kInd(a, M, sw) { const k = a.k + ((a.k_swept || a.k) - a.k) * sw; return k * (1 + (M > 0.9 ? 1.3 * (M - 0.9) : 0)); }
function clMax(a, sw) { return a.clmax + ((a.clmax_swept || a.clmax) - a.clmax) * sw; }
function thrustOf(a, h, M, ab) {
  const sig = isa(h).rho / 1.225, s11 = RHO11 / 1.225;
  const lapse = h < 11000 ? Math.pow(sig, 0.75) : Math.pow(s11, 0.75) * sig / s11;
  if (ab) return a.t_ab * 1000 * lapse * (1 + 0.25 * M + 0.04 * M * M);
  return a.t_dry * 1000 * lapse * (1 + 0.18 * M - 0.02 * M * M);
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const wrap360 = d => ((d % 360) + 360) % 360;
const wrap180 = d => ((d + 540) % 360) - 180;

class Sim {
  constructor(jet) {
    this.a = jet;
    this.twin = !/^1 ×/.test(jet.engines);
    this.reset();
  }
  reset() {
    const a = this.a;
    Object.assign(this, {
      h: 4500, V: 230, gam: 0, psi: 40, phi: 0, n: 1, alpha: 4, beta: 0,
      thr: 0.82, ab: false, rpm: [88, 88], egt: [650, 650], noz: [20, 20], ff: [0, 0],
      fuel: a.fuel * 0.6, x: 0, z: 0, t: 0, vs: 0, gMax: 1, gMin: 1, gear: false, flaps: 0,
      sweep: 20, sweepCmd: 20, inp: { pitch: 0, roll: 0 }, auto: true, ap: { i: 0, t: 0 },
      stores: a.weapons.map(w => ({ name: w[0], n: w[1], where: w[2], alt: w[2].includes('(alt.)') })), wpn: 0,
      gunRds: parseInt((a.gun.match(/(\d+) rds/) || [0, 0])[1]), master: true, warn: {}, pPrev: 0,
      contacts: [{ id: 1, x: 32000, z: 52000, h: 7000, hdg: 220, v: 240, type: 'FTR', hostile: true },
                 { id: 2, x: 42000, z: 60000, h: 7300, hdg: 225, v: 240, type: 'FTR', hostile: true },
                 { id: 3, x: -26000, z: 70000, h: 9500, hdg: 180, v: 200, type: 'AEW', hostile: true },
                 { id: 4, x: -6000, z: -14000, h: 6000, hdg: 40, v: 230, type: 'FRD', hostile: false }],
      emitters: [{ x: 40000, z: 30000, type: 'SAM', code: 'S' }, { x: -50000, z: 45000, type: 'SAM', code: '6' }],
    });
  }
  mass() { const a = this.a; return a.m_empty + this.fuel + a.m_stores + 100 * a.crew; }
  get M() { return this.V / isa(this.h).a; }
  get ias() { return this.V * Math.sqrt(isa(this.h).rho / 1.225); } // m/s, EAS approximates CAS here
  step(dt) {
    const a = this.a, at = isa(this.h), M = this.V / at.a;
    const W = this.mass() * G0, q = 0.5 * at.rho * this.V * this.V;
    // wing sweep schedule (variable-sweep jets): 20 deg below M0.7 to 68 deg at M1.2, lagged
    if (a.k_swept) { this.sweepCmd = 20 + 48 * sweepF(a, M); this.sweep += clamp(this.sweepCmd - this.sweep, -7 * dt, 7 * dt); }
    const sw = a.k_swept ? (this.sweep - 20) / 48 : 0;
    if (this.auto) this.autopilot(dt);
    // load-factor command from stick; limited by CLmax (stall) and the g-limiter
    const pin = this.inp.pitch;
    let nCmd = pin >= 0 ? 1 + pin * (a.g_max - 1) : 1 + pin * (1 - a.g_min);
    const cmax = clMax(a, sw) + (this.flaps ? 0.3 : 0);
    const nStall = cmax * q * a.S / W;
    nCmd = clamp(nCmd, Math.max(-nStall * 0.6, a.g_min), Math.min(nStall, a.g_max));
    this.n += (nCmd - this.n) * Math.min(1, dt * 6);
    const CL = this.n * W / Math.max(q * a.S, 1);
    this.alpha = clamp(CL / 0.065 + 0.5, -10, a.alpha_max);
    // engines: lever 0..1 = idle..military, above 1 = afterburner (detent at 1.0)
    this.ab = this.thr > 1.001;
    const tgtRpm = this.ab ? 100 : 65 + 35 * clamp(this.thr, 0, 1);
    const spool = this.a.era.startsWith('197') ? 0.6 : 1.1;
    for (let i = 0; i < 2; i++) {
      this.rpm[i] += (tgtRpm - this.rpm[i]) * Math.min(1, dt * spool);
      const egtT = 380 + (this.rpm[i] - 65) * 12 + (this.ab ? 60 : 0) + (i ? 6 : 0);
      this.egt[i] += (egtT - this.egt[i]) * Math.min(1, dt * 0.9);
      const nozT = this.ab ? 60 + 40 * (this.thr - 1) / 0.25 : clamp(100 - (this.rpm[i] - 65) * 2.6, 0, 100) * 0.9;
      this.noz[i] += (nozT - this.noz[i]) * Math.min(1, dt * 2);
    }
    const rpmF = clamp((this.rpm[0] - 65) / 35, 0, 1);
    let T = this.ab ? thrustOf(a, this.h, M, false) + (thrustOf(a, this.h, M, true) - thrustOf(a, this.h, M, false)) * clamp((this.thr - 1) / 0.25, 0.3, 1)
                    : thrustOf(a, this.h, M, false) * (0.06 + 0.94 * Math.pow(rpmF, 2.2));
    if (this.fuel <= 0) T = 0;
    const fuelRate = T * (this.ab ? a.tsfc * 2.6 : a.tsfc) / 3600; // kg/s
    this.fuel = Math.max(0, this.fuel - fuelRate * dt);
    this.ff = this.twin ? [fuelRate * 1800, fuelRate * 1800] : [fuelRate * 3600, 0]; // kg/h per engine
    this.thrust = T;
    const D = q * a.S * (cd0(a, M) + kInd(a, M, sw) * CL * CL) + (this.gear ? q * 1.2 : 0) + (this.flaps ? q * 0.6 : 0);
    // equations of motion (wind axes)
    const V = this.V, g = G0;
    this.V = Math.max(40, V + ((T - D) / this.mass() - g * Math.sin(this.gam)) * dt);
    this.gam += (g / V) * (this.n * Math.cos(this.phi) - Math.cos(this.gam)) * dt;
    this.gam = clamp(this.gam, -1.5, 1.5);
    this.psi = wrap360(this.psi + (180 / Math.PI) * (g / V) * this.n * Math.sin(this.phi) / Math.max(0.1, Math.cos(this.gam)) * dt);
    const p = clamp(this.inp.roll, -1, 1) * a.roll * Math.PI / 180 * clamp(q / 9000, 0.25, 1);
    this.pRate = p;
    this.phi += p * dt;
    if (this.phi > Math.PI) this.phi -= 2 * Math.PI;
    if (this.phi < -Math.PI) this.phi += 2 * Math.PI;
    const vs = V * Math.sin(this.gam);
    this.vs += (vs - this.vs) * Math.min(1, dt * 1.5); // VSI lag
    this.h += vs * dt;
    if (this.h < 30) { this.h = 30; this.gam = Math.max(this.gam, 0.05); }
    const hd = this.psi * Math.PI / 180;
    this.x += Math.sin(hd) * V * Math.cos(this.gam) * dt;
    this.z += Math.cos(hd) * V * Math.cos(this.gam) * dt;
    this.beta = clamp(-p * 0.02, -0.15, 0.15);
    this.gMax = Math.max(this.gMax, this.n); this.gMin = Math.min(this.gMin, this.n);
    this.t += dt;
    for (const c of this.contacts) { const r = c.hdg * Math.PI / 180; c.x += Math.sin(r) * c.v * dt; c.z += Math.cos(r) * c.v * dt; c.hdg = wrap360(c.hdg + Math.sin(this.t / 40 + c.id) * 0.6 * dt); }
    // cautions / warnings
    const Mn = this.M;
    this.warn = {
      stall: this.alpha > a.alpha_max * 0.85 || (this.ias < 75 && !this.gear),
      overg: this.n > a.g_max * 0.97,
      fuel: this.fuel < a.fuel * 0.2,
      bingo: this.fuel < a.fuel * 0.3,
      pullup: this.h < 600 && this.gam < -0.1,
      speed: Mn > a.m_lim * 0.98 || q > a.q_lim * 0.97,
      ab: this.ab,
      gear: this.gear, lowalt: this.h < 300,
    };
  }
  autopilot(dt) {
    // demo: a looping sequence of manoeuvres flown by a simple controller
    const S = [
      { d: 8, bank: 0, gam: 0, thr: 0.86 }, { d: 14, bank: 70, gam: 0, thr: 1.05 }, { d: 6, bank: 0, gam: 0.35, thr: 1.2 },
      { d: 8, bank: 0, gam: 0, thr: 0.82 }, { d: 3.5, roll: 1 }, { d: 6, bank: 0, gam: -0.12, thr: 0.7 },
      { d: 12, bank: -60, gam: 0, thr: 1.0 }, { d: 8, bank: 0, gam: 0.05, thr: 0.9 },
    ];
    const ap = this.ap;
    ap.t += dt;
    if (ap.t > S[ap.i].d) { ap.t = 0; ap.i = (ap.i + 1) % S.length; }
    const s = S[ap.i];
    // altitude guard
    let gT = s.gam ?? 0;
    if (this.h < 2500) gT = Math.max(gT, 0.2);
    if (this.h > 11000) gT = Math.min(gT, -0.05);
    if (s.roll) { this.inp.roll = 1; this.inp.pitch = 0.08; this.thr = 0.9; return; }
    const bank = (s.bank || 0) * Math.PI / 180;
    const eb = wrap180((bank - this.phi) * 180 / Math.PI) * Math.PI / 180;
    this.inp.roll = clamp(eb * 2.2, -1, 1);
    const nLevel = Math.cos(this.gam) / Math.max(0.25, Math.cos(this.phi));
    const nReq = nLevel + (gT - this.gam) * 6;
    const a = this.a;
    this.inp.pitch = nReq >= 1 ? clamp((nReq - 1) / (a.g_max - 1), 0, 1) : clamp((nReq - 1) / (1 - a.g_min), -0.6, 0);
    this.thr += (s.thr - this.thr) * Math.min(1, dt * 2);
  }
}
