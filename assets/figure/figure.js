// figure.js - the figure engine: people as low-poly 3D meshes made in code. A skeleton is sized by Leonardo's canon
// (the Vitruvian Man: the VITRUVIAN table, fractions of the height), posed by joint angles, and covered by lofted
// rings (cross-sections along each bone) that move with their bones. Uniforms and kit (coveralls, khakis, ball caps,
// headsets) are colours and small parts made the same way. The C game has the same engine in src/figure.c; both draw
// the same triangles. Original code for this project; no data from elsewhere besides the canon's ratios.
// Units: metres, y up, z forward (the way the figure faces), x to the figure's left.
const Figure = (() => {
  // ---- the canon, as fractions of the height H ----
  const VITRUVIAN = {
    span: 1,             // arm span = height
    head: 1 / 8,         // chin to crown
    face: 1 / 10,        // chin to hairline, in three equal parts: chin-nose, nose-brows, brows-hairline
    chestTop: 1 / 6,     // crown to the top of the chest
    chestUnder: 1 / 4,   // crown to the line under the chest
    shoulders: 1 / 4,    // the widest shoulders
    elbowTip: 1 / 4,     // elbow to fingertip
    elbowPit: 1 / 8,     // elbow to armpit
    hand: 1 / 10,
    foot: 1 / 7,
    knee: 1 / 4,         // sole to knee, and knee to crotch
    crotch: 1 / 2,
    navel: 0.6           // the centre of the circle the spread limbs touch
  };
  const PALM = 1 / 24;   // Vitruvius's units: a foot is 4 palms, a cubit 6

  // ---- the skeleton from the canon ----
  function skeleton(H) {
    const V = VITRUVIAN, chin = H * (1 - V.head), deltoid = 0.026 * H;
    const shX = V.shoulders * H / 2 - deltoid;                 // shoulder joint: the widest shoulders less the deltoid
    const pitX = shX + deltoid;                                 // the armpit, under the deltoid's edge
    const elbowX = pitX + V.elbowPit * H, tipX = V.span * H / 2; // elbow to armpit and the half span fix the arm
    const wristX = tipX - V.hand * H;
    return {
      H, chin, deltoid,
      crown: H, hairline: chin + V.face * H, brow: chin + V.face * H * 2 / 3, nose: chin + V.face * H / 3,
      chestTop: H * (1 - V.chestTop), chestUnder: H * (1 - V.chestUnder), navel: V.navel * H, crotch: V.crotch * H,
      knee: V.knee * H, ankle: 0.04 * H, footLen: V.foot * H,
      shX, shY: H * (1 - V.chestTop) - 0.012 * H, hipX: 0.055 * H, hipY: V.crotch * H + 0.025 * H,
      upper: elbowX - shX, fore: wristX - elbowX, hand: V.hand * H, palm: 0.56 * V.hand * H
    };
  }

  // ---- small 3x3 matrix and vector helpers ----
  const mmul = (a, b) => { const r = []; for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]; return r; };
  const mv = (m, v) => [m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]];
  const rx = a => { const c = Math.cos(a), s = Math.sin(a); return [1, 0, 0, 0, c, -s, 0, s, c]; };
  const ry = a => { const c = Math.cos(a), s = Math.sin(a); return [c, 0, s, 0, 1, 0, -s, 0, c]; };
  const rz = a => { const c = Math.cos(a), s = Math.sin(a); return [c, -s, 0, s, c, 0, 0, 0, 1]; };
  const I = [1, 0, 0, 0, 1, 0, 0, 0, 1];
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const len = a => Math.hypot(a[0], a[1], a[2]), norm = a => mul(a, 1 / (len(a) || 1));
  const D = Math.PI / 180;
  // a joint's rotation from its pose angles in degrees: bend about x (forward and back), twist about the bone, spread
  const rot = p => p ? mmul(mmul(rz((p[2] || 0) * D), ry((p[1] || 0) * D)), rx((p[0] || 0) * D)) : I;

  // ---- the bones: rest position (T-pose) and the joint each hangs from ----
  // each bone: parent, joint position at rest, the direction it points at rest, its length
  function bones(K) {
    const up = [0, 1, 0], down = [0, -1, 0];
    const B = {
      hips: { parent: null, at: [0, K.hipY, 0], dir: up, len: K.chestUnder - K.hipY },
      chest: { parent: 'hips', at: [0, K.chestUnder, 0], dir: up, len: K.chestTop - K.chestUnder },
      neck: { parent: 'chest', at: [0, K.chestTop, 0], dir: up, len: K.chin + 0.01 * K.H - K.chestTop },
      head: { parent: 'neck', at: [0, K.chin + 0.01 * K.H, 0], dir: up, len: K.crown - K.chin - 0.01 * K.H }
    };
    for (const [s, x] of [['L', 1], ['R', -1]]) {
      B['shoulder' + s] = { parent: 'chest', at: [0, K.shY, 0], dir: [x, 0, 0], len: K.shX };
      B['upper' + s] = { parent: 'shoulder' + s, at: [x * K.shX, K.shY, 0], dir: [x, 0, 0], len: K.upper };
      B['fore' + s] = { parent: 'upper' + s, at: [x * (K.shX + K.upper), K.shY, 0], dir: [x, 0, 0], len: K.fore };
      B['hand' + s] = { parent: 'fore' + s, at: [x * (K.shX + K.upper + K.fore), K.shY, 0], dir: [x, 0, 0], len: K.hand };
      B['thigh' + s] = { parent: 'hips', at: [x * K.hipX, K.hipY, 0], dir: down, len: K.hipY - K.knee };
      B['shin' + s] = { parent: 'thigh' + s, at: [x * K.hipX, K.knee, 0], dir: down, len: K.knee - K.ankle };
      B['foot' + s] = { parent: 'shin' + s, at: [x * K.hipX, K.ankle, 0], dir: [0, 0, 1], len: K.footLen - 0.03 * K.H };
    }
    return B;
  }

  // forward kinematics: each bone's world rotation and joint position for a pose (angles in degrees per bone)
  function solve(K, pose) {
    const B = bones(K), W = {};
    const order = ['hips', 'chest', 'neck', 'head', 'shoulderL', 'shoulderR', 'upperL', 'upperR', 'foreL', 'foreR', 'handL', 'handR',
      'thighL', 'thighR', 'shinL', 'shinR', 'footL', 'footR'];
    const root = pose.root || [0, 0, 0], rootRot = ry((pose.yaw || 0) * D);
    for (const n of order) {
      const b = B[n], p = b.parent && W[b.parent];
      const local = rot(pose[n]);
      const R = p ? mmul(p.R, local) : mmul(rootRot, local);
      let at;
      if (!p) at = add(mv(rootRot, b.at), root);
      else at = add(p.at, mv(p.R, sub(b.at, B[b.parent].at)));
      W[n] = { R, at, end: add(at, mv(R, mul(b.dir, b.len))), dir: b.dir, len: b.len };
    }
    return W;
  }

  // ---- the mesh: rings along the bones ----
  // A part is a list of rings in its bone's frame: [t (0-1 along the bone), half-width, half-depth, forward offset,
  // colour role]. The bone's frame: u along the bone, x across, z forward (for limbs pointing sideways, z stays
  // forward; for the trunk, x is the figure's left).
  const SEG = 10;
  function frame(w) {
    // the bone's axes in the world: along, across and forward, from its rest direction turned by its rotation
    const along = mv(w.R, w.dir);
    const fwd0 = Math.abs(w.dir[2]) > 0.9 ? [0, -1, 0] : [0, 0, 1];
    const fwd = mv(w.R, fwd0), across = norm(cross(along, fwd));
    return { along, across, fwd };
  }
  const sides = (K, w, rings, role, out, shape) => {
    const F = frame(w), base = [];
    for (const r of rings) {
      const c = add(w.at, mul(F.along, r[0] * w.len)), pts = [];
      for (let i = 0; i < SEG; i++) {
        const a = i / SEG * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        // a rounded box: a superellipse, rounder for limbs, squarer for the trunk
        const e = shape || 0.8, cx = Math.sign(ca) * Math.pow(Math.abs(ca), e), cz = Math.sign(sa) * Math.pow(Math.abs(sa), e);
        pts.push(add(c, add(mul(F.across, cx * r[1] * K.H), mul(F.fwd, cz * r[2] * K.H + (r[3] || 0) * K.H))));
      }
      base.push({ pts, role: r[4] || role });
    }
    for (let k = 0; k + 1 < base.length; k++)
      for (let i = 0; i < SEG; i++) {
        const j = (i + 1) % SEG, a = base[k].pts, b = base[k + 1].pts;
        out.push({ v: [a[i], b[j], a[j]], role: base[k].role, part: role }, { v: [a[i], b[i], b[j]], role: base[k].role, part: role });
      }
    // close both ends
    for (const k of [0, base.length - 1]) {
      const P = base[k].pts, c = mul(P.reduce((s, p) => add(s, p), [0, 0, 0]), 1 / SEG);
      for (let i = 0; i < SEG; i++) out.push({ v: k ? [P[(i + 1) % SEG], P[i], c] : [P[i], P[(i + 1) % SEG], c], role: base[k].role, part: role });
    }
  };

  // ring tables in fractions of H (a fit adult build)
  const ARM = {
    upper: [[0, 0.026, 0.027], [0.3, 0.025, 0.026], [0.65, 0.022, 0.022], [1, 0.019, 0.019]],
    fore: [[0, 0.019, 0.019], [0.3, 0.02, 0.018], [0.75, 0.016, 0.013], [1, 0.0135, 0.01]],
    palm: [[0, 0.016, 0.009], [0.45, 0.026, 0.009], [1, 0.025, 0.008]]
  };
  const LEG = {
    thigh: [[0, 0.046, 0.05], [0.4, 0.043, 0.045], [0.85, 0.03, 0.032], [1, 0.027, 0.03]],
    shin: [[0, 0.027, 0.03], [0.3, 0.028, 0.034, -0.004], [0.75, 0.02, 0.021], [1, 0.016, 0.018]],
    foot: [[0, 0.018, 0.02, 0, 'boot'], [0.5, 0.022, 0.014, 0, 'boot'], [1, 0.02, 0.01, 0, 'boot']]
  };

  function build(fig) {
    const K = skeleton(fig.height || 1.75), W = solve(K, fig.pose || {}), tris = [];
    const H = K.H, C = fig.kit || {};
    // the trunk: rings from the crotch to the base of the neck, wider at the chest, the waist taken in at the navel
    const hips = W.hips, chest = W.chest;
    const trunkRing = (y, w, d, z, role) => {
      // between the hips and the chest the trunk follows both bones: below the chest's joint the hips, above it the chest
      const b = y <= K.chestUnder ? hips : chest, b0 = y <= K.chestUnder ? K.hipY : K.chestUnder;
      return [(y - b0) / b.len, w, d, z, role, b];
    };
    const TR = [[K.crotch - 0.02 * H, 0.07, 0.055, 0, 'trousers'], [K.crotch + 0.03 * H, 0.088, 0.062, -0.004, 'trousers'],
      [K.crotch + 0.07 * H, 0.084, 0.058, 0, 'belt'], [K.navel - 0.015 * H, 0.078, 0.057, 0.004, 'shirt'], [K.navel + 0.05 * H, 0.083, 0.06, 0.004, 'shirt'],
      [K.chestUnder, 0.092, 0.066, 0.01, 'shirt'], [K.chestUnder + 0.04 * H, 0.097, 0.066, 0.01, 'shirt'], [K.shY - 0.004 * H, 0.096, 0.056, 0, 'shirt'],
      [K.chestTop + 0.004 * H, 0.05, 0.042, 0, 'collar']].map(r => trunkRing(...r));
    // the trunk's rings sit on two bones; place each by its own bone
    const tb = [];
    for (const r of TR) {
      const w = r[5], F = frame(w), c = add(w.at, mul(F.along, r[0] * w.len)), pts = [];
      for (let i = 0; i < SEG; i++) {
        const a = i / SEG * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        const cx = Math.sign(ca) * Math.pow(Math.abs(ca), 0.65), cz = Math.sign(sa) * Math.pow(Math.abs(sa), 0.8);
        pts.push(add(c, add(mul(F.across, cx * r[1] * H), mul(F.fwd, cz * r[2] * H + r[3] * H))));
      }
      tb.push({ pts, role: r[4] });
    }
    for (let k = 0; k + 1 < tb.length; k++)
      for (let i = 0; i < SEG; i++) {
        const j = (i + 1) % SEG, a = tb[k].pts, b = tb[k + 1].pts;
        tris.push({ v: [a[i], b[j], a[j]], role: tb[k].role, part: 'trunk' }, { v: [a[i], b[i], b[j]], role: tb[k].role, part: 'trunk' });
      }
    // the neck
    sides(K, W.neck, [[0, 0.034, 0.036, 0], [0.6, 0.031, 0.033, 0.003], [1.1, 0.03, 0.032, 0.006]], 'skin', tris);
    // the head: rings from the chin to the crown, by the face's thirds (see head())
    head(K, W.head, fig, tris);
    // arms and legs
    for (const s of ['L', 'R']) {
      sides(K, W['upper' + s], [[-0.12, 0.024, 0.026, 0, 'sleeve'], ...ARM.upper.map(r => [...r, 0, 'sleeve'])], 'sleeve', tris);
      sides(K, W['fore' + s], ARM.fore.map((r, i) => [...r, 0, C.rolled && i >= 1 ? 'skin' : 'sleeve']), 'sleeve', tris);
      // the hand: the palm, the fingers curled by the pose (the hand bone's own bend), the thumb
      const hw = W['hand' + s];
      const h0 = tris.length;
      sides(K, { ...hw, len: K.palm }, ARM.palm.map(r => [...r, 0, 'skin']), 'skin', tris);
      const curl = ((fig.pose || {})['curl' + s] || 0) * D, F = frame(hw);
      const knuckle = add(hw.at, mul(F.along, K.palm));
      // fingers bend toward the palm's side (down for a T-pose hand) about the hand's across axis
      const bend = (v, a) => add(mul(v, Math.cos(a)), mul(cross(F.across, v), Math.sin(a) * (s === 'L' ? -1 : 1)));
      const f1 = norm(bend(F.along, curl)), f2 = norm(bend(F.along, curl * 2.1));
      const p1 = add(knuckle, mul(f1, (K.hand - K.palm) * 0.55)), p2 = add(p1, mul(f2, (K.hand - K.palm) * 0.45));
      tube(K, knuckle, p1, F.across, 0.023, 0.0072, 0.021, 0.0066, 'skin', tris);
      tube(K, p1, p2, F.across, 0.021, 0.0066, 0.015, 0.005, 'skin', tris);
      const t0 = add(hw.at, add(mul(F.along, K.palm * 0.25), mul(F.fwd, 0.012 * H)));
      const t1 = add(t0, add(mul(norm(add(F.along, mul(F.fwd, 0.9))), 0.045 * H), mul(f1, 0.01 * H)));
      tube(K, t0, t1, F.across, 0.009, 0.008, 0.007, 0.006, 'skin', tris);
      for (let i = h0; i < tris.length; i++) tris[i].part = 'hand' + s;
      sides(K, W['thigh' + s], LEG.thigh.map(r => [...r, 0, 'trousers']), 'trousers', tris);
      sides(K, W['shin' + s], LEG.shin.map(r => [...r, 0, 'trousers']), 'trousers', tris);
      // the boot: from the heel to the toe
      const fw = W['foot' + s], ff = frame(fw), heel = add(fw.at, add(mul(ff.along, -0.03 * H), mul(mv(fw.R, [0, -1, 0]), 0.02 * H)));
      tube(K, heel, add(fw.at, add(mul(ff.along, fw.len), mul(mv(fw.R, [0, -1, 0]), 0.025 * H))), mv(fw.R, [1, 0, 0]), 0.022, 0.022, 0.02, 0.012, 'boot', tris);
    }
    kit(K, W, fig, tris);
    return { tris, joints: W, K };
  }

  // a tube between two points, its cross-section a rounded box (half-width along `across`)
  function tube(K, a, b, across, w0, d0, w1, d1, role, out) {
    const along = norm(sub(b, a)), x = norm(sub(across, mul(along, dot(across, along)))), z = cross(x, along);
    const ring = (c, w, d) => Array.from({ length: SEG }, (_, i) => {
      const t = i / SEG * Math.PI * 2, ca = Math.cos(t), sa = Math.sin(t);
      return add(c, add(mul(x, Math.sign(ca) * Math.pow(Math.abs(ca), 0.8) * w * K.H), mul(z, Math.sign(sa) * Math.pow(Math.abs(sa), 0.8) * d * K.H)));
    });
    const A = ring(a, w0, d0), B = ring(b, w1, d1);
    for (let i = 0; i < SEG; i++) {
      const j = (i + 1) % SEG;
      out.push({ v: [A[i], B[j], A[j]], role }, { v: [A[i], B[i], B[j]], role });
      out.push({ v: [A[i], A[j], a], role }, { v: [B[j], B[i], b], role });
    }
  }

  // ---- the head, by the face's thirds ----
  // rings from the chin (u = 0) to the crown (u = 1) in head heights: half-width, forward and back extent. The chin
  // to the nose's base, the nose's base to the brows and the brows to the hairline are each a third of the face
  // (chin to hairline = 4/5 of the head); the ear is as long as one third, between the brows and the nose's base.
  const HEAD = [
    // u, half-width, forward (the face's profile), back
    [0.0, 0.14, 0.30, -0.05], [0.04, 0.2, 0.40, -0.1], [0.1, 0.28, 0.41, -0.2], [0.16, 0.33, 0.43, -0.3], [0.2, 0.35, 0.44, -0.34],
    [0.24, 0.37, 0.45, -0.38], [0.267, 0.39, 0.44, -0.4], [0.33, 0.42, 0.43, -0.43], [0.4, 0.44, 0.44, -0.46], [0.47, 0.46, 0.43, -0.48],
    [0.533, 0.46, 0.465, -0.49], [0.6, 0.46, 0.46, -0.5], [0.7, 0.46, 0.445, -0.5], [0.8, 0.45, 0.41, -0.49], [0.88, 0.41, 0.34, -0.44],
    [0.94, 0.32, 0.24, -0.34], [0.98, 0.18, 0.12, -0.18], [1.0, 0.04, 0.02, -0.03]];
  const HN = 24;   // segments round the head
  // a point on the head's surface: u up the head, a round it (0 = the left side, pi/2 = the face)
  const headAt = (u) => {
    for (let k = 0; k + 1 < HEAD.length; k++) {
      const [u0, w0, f0, b0] = HEAD[k], [u1, w1, f1, b1] = HEAD[k + 1];
      if (u <= u1) { const t = (u - u0) / (u1 - u0); return [w0 + (w1 - w0) * t, f0 + (f1 - f0) * t, b0 + (b1 - b0) * t]; }
    }
    return HEAD[HEAD.length - 1].slice(1);
  };
  const surf = (u, a) => {
    const [hw, zf, zb] = headAt(u), ca = Math.cos(a), sa = Math.sin(a);
    return [Math.sign(ca) * Math.pow(Math.abs(ca), 0.85) * hw, sa > 0 ? Math.pow(sa, 0.9) * zf : sa * -zb];
  };
  // the angle round the head at which the surface is x across (front half)
  const angleAt = (u, x) => { const [hw] = headAt(u), c = Math.max(-1, Math.min(1, x / hw)); return Math.acos(Math.sign(c) * Math.pow(Math.abs(c), 1 / 0.85)); };
  function head(K, w, fig, out) {
    const F = frame(w), hH = K.H / 8, bottom = add(w.at, mul(F.along, -0.01 * K.H)), C = fig.kit || {};
    const open = (fig.face && fig.face.open) || 0, blink = (fig.face && fig.face.blink) || 0;
    const jaw = u => (u < 0.205 ? open * 0.07 * Math.min(1, (0.205 - u) / 0.06) : 0);   // the jaw drops for speech
    const P = (u, x, z) => add(bottom, add(mul(F.along, (u - jaw(u)) * hH), add(mul(F.across, x * hH), mul(F.fwd, z * hH))));
    const S = (u, a, lift = 0) => { const [x, z] = surf(u, a), n = Math.hypot(x, z) || 1; return P(u, x + x / n * lift, z + z / n * lift); };
    const rings = HEAD.map(([u]) => Array.from({ length: HN }, (_, i) => {
      const a = i / HN * Math.PI * 2, [x, z] = surf(u, a);
      return { p: P(u, x, z), u, a: ((a % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI), x, z };
    }));
    // hair: above the hairline at the front, down to the nape behind, over the ears' tops at the sides
    const hairAt = q => q.u >= 0.8 || (q.z < -0.05 && q.u > 0.4) || (q.u > 0.6 && Math.abs(q.x) > 0.3 && q.z < 0.25);
    for (let k = 0; k + 1 < rings.length; k++)
      for (let i = 0; i < HN; i++) {
        const j = (i + 1) % HN, a = rings[k], b = rings[k + 1];
        const role = hairAt(a[i]) && hairAt(b[j]) && hairAt(a[j]) ? 'hair' : 'skin';
        out.push({ v: [a[i].p, b[j].p, a[j].p], role, part: 'head' }, { v: [a[i].p, b[i].p, b[j].p], role, part: 'head' });
      }
    const c0 = P(0, 0, 0.12);
    for (let i = 0; i < HN; i++) out.push({ v: [rings[0][i].p, rings[0][(i + 1) % HN].p, c0], role: 'skin', part: 'head' });
    const quad = (a, b, c, d, role) => out.push({ v: [a, b, c], role, part: 'face', two: true }, { v: [a, c, d], role, part: 'face', two: true });
    // the nose: the bridge between the eyes, the tip forward of the face, the wings and the base
    const bridge = P(0.52, 0, 0.47), bl = P(0.43, 0.05, 0.452), br = P(0.43, -0.05, 0.452), tip = P(0.31, 0, 0.555);
    const wl = P(0.28, 0.075, 0.445), wr = P(0.28, -0.075, 0.445), base = P(0.265, 0, 0.47);
    for (const [v, role] of [[[bridge, bl, tip], 'skin'], [[bridge, tip, br], 'skin'], [[bl, wl, tip], 'skin'], [[br, tip, wr], 'skin'],
      [[wl, base, tip], 'skinShade'], [[tip, base, wr], 'skinShade']]) out.push({ v, role, part: 'face', two: true });
    for (const sx of [1, -1]) {
      // the eye: the white, the iris; the upper lid comes down to blink
      const ac = angleAt(0.5, 0.2 * sx), eu0 = 0.475, eu1 = 0.525;
      const E = (u, da, lift) => S(u, ac + da * sx, lift);
      const top = eu1 - (eu1 - eu0) * blink;
      out.push({ v: [E(0.5, -0.13, 0.008), E(eu0, 0, 0.008), E(0.5, 0.13, 0.008)], role: 'eyeWhite', two: true });
      if (blink < 0.6) out.push({ v: [E(0.5, -0.13, 0.008), E(0.5, 0.13, 0.008), E(top, 0, 0.008)], role: 'eyeWhite', two: true });
      if (blink < 0.6) quad(E(0.482, -0.04, 0.011), E(0.482, 0.04, 0.011), E(Math.min(top, 0.518), 0.04, 0.011), E(Math.min(top, 0.518), -0.04, 0.011), 'iris');
      quad(E(top - 0.004, -0.14, 0.012), E(top - 0.004, 0.14, 0.012), E(top + 0.01, 0.12, 0.012), E(top + 0.01, -0.12, 0.012), 'lid');
      // the brow along the brow ridge
      quad(E(0.56, -0.2, 0.012), E(0.565, 0.18, 0.012), E(0.595, 0.16, 0.012), E(0.59, -0.2, 0.012), 'brow');
      // the ear, as long as one third of the face, between the brows and the nose's base, flat against the head
      const ec = P(0.4, 0.455 * sx, -0.06);
      const e0 = add(ec, mul(F.along, -hH * 0.133)), e1 = add(ec, mul(F.along, hH * 0.02)), e2 = add(ec, mul(F.along, hH * 0.133));
      tube(K, e0, e1, F.fwd, 0.006, 0.003, 0.012, 0.0035, 'skin', out);   // the lobe widening to the bowl
      tube(K, e1, e2, F.fwd, 0.012, 0.0035, 0.007, 0.003, 'skin', out);   // the helix rounding over the top
    }
    // the lips round the front of the face, the mouth's line between them
    const lip = (u0, u1, role, lift) => { for (let k = -3; k < 3; k++) { const a0 = Math.PI / 2 + k * 0.09, a1 = a0 + 0.09; quad(S(u0, a0, lift), S(u0, a1, lift), S(u1, a1, lift), S(u1, a0, lift), role); } };
    lip(0.212, 0.238, 'lip', 0.006);
    lip(0.17, 0.196, 'lip', 0.008);
    lip(0.198 - open * 0.05, 0.212, 'mouth', 0.009);
    // the ball cap: a crown over the skull and the bill over the eyes, with the ship's gold on the bill for an officer
    if (C.cap) {
      // the crown from just above the brows (u 0.7) over the top, a little off the head
      const cr = HEAD.filter(([u]) => u >= 0.7).map(([u, hw, zf, zb]) => [u, hw * 1.06 + 0.02, zf * 1.06 + 0.02, zb * 1.06 - 0.02]);
      const caps = cr.map(([u, hw, zf, zb]) => Array.from({ length: HN }, (_, i) => {
        const a = i / HN * Math.PI * 2, ca = Math.cos(a), sa = Math.sin(a);
        return add(bottom, add(mul(F.along, (u + 0.03) * hH), add(mul(F.across, Math.sign(ca) * Math.pow(Math.abs(ca), 0.85) * hw * hH), mul(F.fwd, (sa > 0 ? sa * zf : -sa * zb) * hH))));
      }));
      for (let k = 0; k + 1 < caps.length; k++)
        for (let i = 0; i < HN; i++) { const j = (i + 1) % HN; out.push({ v: [caps[k][i], caps[k + 1][j], caps[k][j]], role: 'cap' }, { v: [caps[k][i], caps[k + 1][i], caps[k + 1][j]], role: 'cap' }); }
      const bill = [P(0.725, -0.42, 0.44), P(0.725, 0.42, 0.44), P(0.69, 0.34, 0.86), P(0.69, -0.34, 0.86)];
      out.push({ v: [bill[0], bill[1], bill[2]], role: 'cap', two: true }, { v: [bill[0], bill[2], bill[3]], role: 'cap', two: true });
      if (C.gold) { /* the gold oak leaves along the bill's front edge */
        const g0 = [P(0.712, -0.38, 0.6), P(0.712, 0.38, 0.6), P(0.692, 0.33, 0.84), P(0.692, -0.33, 0.84)];
        out.push({ v: [g0[0], g0[1], g0[2]], role: 'gold', two: true }, { v: [g0[0], g0[2], g0[3]], role: 'gold', two: true });
      }
      const ship = [P(0.79, -0.22, 0.46), P(0.79, 0.22, 0.46), P(0.88, 0.19, 0.4), P(0.88, -0.19, 0.4)];
      out.push({ v: [ship[0], ship[1], ship[2]], role: 'gold', two: true }, { v: [ship[0], ship[2], ship[3]], role: 'gold', two: true });
    }
    // the headset: a band over the crown, cups over the ears, a boom to the corner of the mouth
    if (C.headset) {
      const band = [];
      for (let i = 0; i <= 8; i++) { const a = Math.PI * i / 8; band.push(P(0.4 + Math.sin(a) * 0.62, Math.cos(a) * 0.5, -0.03)); }
      for (let i = 0; i + 1 < band.length; i++) tube(K, band[i], band[i + 1], F.fwd, 0.004, 0.003, 0.004, 0.003, 'kit', out);
      for (const sx of [1, -1]) { const c = P(0.4, 0.5 * sx, -0.04); tube(K, sub(c, mul(F.across, 0.04 * sx * hH)), add(c, mul(F.across, 0.04 * sx * hH)), F.fwd, 0.011, 0.012, 0.011, 0.012, 'kit', out); }
      tube(K, P(0.38, 0.5, 0.0), P(0.22, 0.25, 0.45), F.across, 0.0015, 0.0015, 0.0015, 0.0015, 'kit', out);
      tube(K, P(0.22, 0.25, 0.45), P(0.2, 0.1, 0.5), F.across, 0.004, 0.004, 0.004, 0.004, 'kit', out);
    }
  }

  // ---- uniform details ----
  function kit(K, W, fig, out) {
    const C = fig.kit || {}, ch = W.chest, F = frame(ch), H = K.H;
    const on = (y, x, z) => add(ch.at, add(mul(F.along, y * H), add(mul(F.across, x * H), mul(F.fwd, z * H))));
    const quad = (a, b, c, d, role) => out.push({ v: [a, b, c], role, two: true }, { v: [a, c, d], role, two: true });
    // the open collar's V and the T-shirt in it
    quad(on(0.085, -0.03, 0.05), on(0.085, 0.03, 0.05), on(0.06, 0.006, 0.071), on(0.06, -0.006, 0.071), C.tee || 'tee');
    // breast pockets with flaps, the name tape
    for (const sx of [1, -1]) {
      quad(on(0.05, 0.02 * sx, 0.075), on(0.05, 0.065 * sx, 0.072), on(0.0, 0.065 * sx, 0.074), on(0.0, 0.02 * sx, 0.077), 'pocket');
    }
    if (C.nametape) quad(on(0.06, 0.02, 0.0745), on(0.06, 0.07, 0.072), on(0.052, 0.07, 0.0725), on(0.052, 0.02, 0.075), 'tape');
    // the collar devices: an anchor for a chief, the oak leaf for a commander
    if (C.device) for (const sx of [1, -1]) quad(on(0.083, 0.035 * sx, 0.052), on(0.083, 0.05 * sx, 0.048), on(0.07, 0.05 * sx, 0.056), on(0.07, 0.035 * sx, 0.06), C.device);
    // the clipboard in the left hand
    if (C.clipboard) {
      const h = W.handL, hf = frame(h), c = add(h.at, mul(hf.along, K.palm * 0.6));
      const ax = mul(hf.along, 0.06 * H), ay = mul(cross(hf.along, hf.across), 0.085 * H), n = mul(hf.across, -0.012 * H);
      quad(add(add(c, n), sub(ay, ax)), add(add(c, n), add(ay, ax)), add(add(c, n), sub(mul(ay, -1), mul(ax, -1))), add(add(c, n), sub(mul(ay, -1), ax)), 'paper');
    }
  }

  // ---- the look: colours for each role from the kit ----
  const PALETTES = {
    // shipboard flame-resistant coveralls: dark navy, the ball cap, black boots
    coveralls: { shirt: [44, 58, 86], sleeve: [44, 58, 86], trousers: [40, 53, 80], belt: [36, 46, 70], collar: [44, 58, 86], pocket: [38, 50, 76], tape: [24, 30, 44], tee: [44, 58, 86] },
    // service khaki: shirt and trousers, a dark belt, the white T-shirt at the open collar
    khaki: { shirt: [176, 150, 104], sleeve: [176, 150, 104], trousers: [166, 140, 96], belt: [40, 32, 24], collar: [176, 150, 104], pocket: [160, 136, 94], tape: [140, 118, 82], tee: [232, 230, 222] },
    shared: { boot: [22, 22, 24], cap: [22, 30, 50], gold: [214, 172, 72], kit: [20, 22, 24], eyeWhite: [214, 208, 200], iris: [52, 40, 30], mouth: [70, 30, 30], paper: [228, 226, 214], anchor: [214, 172, 72], oak: [200, 204, 210] }
  };
  function colour(fig, role) {
    const C = fig.kit || {}, U = PALETTES[C.uniform || 'coveralls'];
    if (role === 'skin') return fig.skin || [196, 152, 122];
    if (role === 'skinShade') return (fig.skin || [196, 152, 122]).map(v => v * 0.72);
    if (role === 'hair') return fig.hair || [52, 40, 32];
    if (role === 'brow') return (fig.hair || [52, 40, 32]).map(v => v * 0.8);
    if (role === 'lid') return (fig.skin || [196, 152, 122]).map(v => v * 0.7);
    if (role === 'lip') return (fig.skin || [196, 152, 122]).map((v, i) => v * [0.86, 0.68, 0.66][i]);
    return U[role] || PALETTES.shared[role] || [255, 0, 255];
  }

  // ---- drawing: project, sort far to near, shade each facet (key light, fill, rim) ----
  // cam: { pos, f (forward), u (up), r (right), F (focal length in pixels), cx, cy }; light: direction toward the key
  // light and its colour. fill(points, colour) draws one triangle.
  function draw(figs, cam, light, fill) {
    const all = [];
    for (const fig of figs) {
      const mesh = fig.mesh || build(fig);
      for (const t of mesh.tris) {
        let n = norm(cross(sub(t.v[1], t.v[0]), sub(t.v[2], t.v[0])));
        const c = mul(add(add(t.v[0], t.v[1]), t.v[2]), 1 / 3), d = sub(c, cam.pos), z = dot(d, cam.f);
        if (z < 0.05) continue;
        if (dot(n, d) > 0) {
          if (!t.two) continue; // facing away
          n = mul(n, -1);       // a flat part (eyes, lips, insignia) shows either side
        }
        const pts = t.v.map(v => { const q = sub(v, cam.pos), zz = Math.max(0.05, dot(q, cam.f)); return [cam.cx + dot(q, cam.r) / zz * cam.F, cam.cy - dot(q, cam.u) / zz * cam.F]; });
        const base = colour(fig, t.role), k = Math.max(0, dot(n, light.dir)), view = norm(mul(d, -1)), rim = Math.pow(1 - Math.max(0, dot(n, view)), 3);
        const lit = base.map((b, i) => Math.min(255, b * (0.24 + 0.85 * k * light.key[i] / 255) + 255 * rim * 0.2 * light.rim[i] / 255));
        all.push({ z, pts, col: lit });
      }
    }
    all.sort((a, b) => b.z - a.z);
    for (const t of all) fill(t.pts, t.col);
  }

  // ---- measuring a figure against the canon (the checker's test, and the viewer's table) ----
  // the same ratios measured on the built mesh, not on the skeleton's numbers: the T-pose for the span, the height,
  // the head, the hand and the forearm; the arms down for the shoulders; the spread pose for the navel's circle
  function measureMesh(H) {
    const ext = (tris, f) => { let lo = Infinity, hi = -Infinity; for (const t of tris) for (const v of t.v) { const x = f(v); lo = Math.min(lo, x); hi = Math.max(hi, x); } return [lo, hi]; };
    const T = build({ height: H, pose: POSES.tpose }), K = T.K, W = T.joints;
    const [x0, x1] = ext(T.tris, v => v[0]), [y0, y1] = ext(T.tris, v => v[1]);
    const headT = T.tris.filter(t => t.part === 'head'), [hy0] = ext(headT, v => v[1]);
    const handT = T.tris.filter(t => t.part === 'handL'), [hx0, hx1] = ext(handT, v => v[0]);
    const S = build({ height: H, pose: POSES.stand });
    const band = S.tris.filter(t => t.part !== 'head' && t.v.every(v => Math.abs(v[1] - K.shY) < 0.03 * H)), [sx0, sx1] = ext(band, v => v[0]);

    const boot = T.tris.filter(t => t.role === 'boot' && t.v.every(v => v[0] > 0)), [bz0, bz1] = ext(boot, v => v[2]);
    const Vt = build({ height: H, pose: vitruvianPose(H) }), navel = [0, Vt.joints.hips.at[1] + (K.navel - K.hipY), 0];
    const far = (tris) => Math.max(...tris.flatMap(t => t.v.map(v => len(sub(v, navel)))));
    const rTip = far(Vt.tris.filter(t => t.part === 'handL')), rFoot = far(Vt.tris.filter(t => t.role === 'boot'));
    const h = y1 - y0;
    return {
      height: h, span: (x1 - x0) / h, head: (y1 - hy0) / h, hand: (hx1 - W.handL.at[0]) / h, elbowTip: (hx1 - W.foreL.at[0]) / h,
      // Leonardo's armpit: the span, the shoulders and elbow to fingertip put the elbow H/4 from the middle, so his
      // armpit lies on the shoulder's outer line (no body has its armpit there); measured from that line
      elbowPit: (W.foreL.at[0] - sx1) / h, shoulders: (sx1 - sx0) / h, foot: (bz1 - bz0) / h, knee: (W.shinL.at[1] - y0) / h,
      kneeCrotch: (K.crotch - W.shinL.at[1]) / h, chestTop: (y1 - K.chestTop) / h, face: (K.hairline - K.chin) / h,
      thirds: [(K.nose - K.chin) / h, (K.brow - K.nose) / h, (K.hairline - K.brow) / h], circle: rFoot / rTip
    };
  }

  // Leonardo's second figure, worked out from the canon: the legs spread until the head drops by H/14, the arms raised
  // until the fingertips reach the standing figure's crown
  function vitruvianPose(H) {
    const K = skeleton(H), drop = H / 14, leg = K.hipY;
    const spread = Math.acos(1 - drop / leg) / D, arm = K.upper + K.fore + K.hand;
    const raise = Math.asin(Math.min(1, (H - (K.shY - drop)) / arm)) / D;
    return { root: [0, -drop, 0], upperL: [0, 0, raise], upperR: [0, 0, -raise], thighL: [0, 0, spread], thighR: [0, 0, -spread] };
  }

  // poses, in degrees per bone: [bend about x, twist, spread about z]
  const POSES = {
    tpose: {},
    stand: { upperL: [0, 0, -84], upperR: [0, 0, 84], foreL: [-12, 0, 0], foreR: [-12, 0, 0] }
  };
  // sitting on a chair seat 0.46 m high: the thighs forward, the shins down
  const SEAT = (H, extra) => Object.assign({ root: [0, 0.46 + 0.045 * H / 1.75 - skeleton(H).hipY, 0], thighL: [-90, 0, 0], thighR: [-90, 0, 0], shinL: [88, 0, 0], shinR: [88, 0, 0], footL: [0, 0, 0], footR: [0, 0, 0] }, extra);

  // the control room's crew: the kit, the look and the pose of each
  const CREW = {
    // the air detector in the ship's flame-resistant coveralls and a headset, seated, leaning in to his scope,
    // both hands forward on the keyboard
    operator: { kit: { uniform: 'coveralls', headset: true, nametape: true }, skin: [205, 160, 128], hair: [34, 26, 20],
      pose: H => SEAT(H, { hips: [14, 0, 0], chest: [8, 0, 0], neck: [-12, 0, 0], head: [-8, 0, 0],
        upperL: [0, -62, -58], upperR: [0, 62, 58], foreL: [0, -48, 6], foreR: [0, 48, -6], handL: [0, -10, -8], handR: [0, 10, 8], curlL: 30, curlR: 34 }) },
    // the chief in service khakis with his anchors, standing, the clipboard held at his waist
    supervisor: { kit: { uniform: 'khaki', cap: true, device: 'anchor', clipboard: true }, skin: [190, 140, 110], hair: [46, 40, 36],
      pose: () => ({ upperL: [0, -8, -82], foreL: [0, -86, 0], handL: [0, 0, 16], curlL: 60, upperR: [0, 8, 82], foreR: [0, 12, 0], handR: [0, 0, 0], curlR: 24, head: [0, -6, 0] }) },
    // the commander in khakis with his silver oak leaves and the ship's cap with gold on the bill, in his chair;
    // raise from 0 to 1 lifts his right hand as he gives the order
    commander: { kit: { uniform: 'khaki', cap: true, gold: true, device: 'oak' }, skin: [196, 150, 120], hair: [140, 138, 134],
      pose: (H, raise = 0) => SEAT(H, { hips: [-6, 0, 0], chest: [-2, 0, 0], neck: [4, 0, 0],
        upperL: [0, 12, -84], foreL: [0, -88, 0], handL: [0, -6, -10], curlL: 45,
        upperR: [0, -12 + 62 * raise, 84 - 40 * raise], foreR: [0, 88 - 20 * raise, 55 * raise], handR: [0, 0, -15 * raise], curlR: 45 - 38 * raise }) }
  };
  return { VITRUVIAN, PALM, skeleton, solve, build, draw, measureMesh, vitruvianPose, POSES, SEAT, CREW, colour };
})();
if (typeof module !== 'undefined') module.exports = Figure;
