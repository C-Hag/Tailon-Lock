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
      // each point's normal, for skin drawn smooth: the rounded box's outward direction
      const ns = Array.from({ length: SEG }, (_, i) => {
        const a = i / SEG * Math.PI * 2, e = shape || 0.8, cx = Math.sign(Math.cos(a)) * Math.pow(Math.abs(Math.cos(a)), e), cz = Math.sign(Math.sin(a)) * Math.pow(Math.abs(Math.sin(a)), e);
        return norm(add(mul(F.across, cx / r[1]), mul(F.fwd, cz / r[2])));
      });
      base.push({ pts, ns, role: r[4] || role });
    }
    for (let k = 0; k + 1 < base.length; k++)
      for (let i = 0; i < SEG; i++) {
        const j = (i + 1) % SEG, a = base[k].pts, b = base[k + 1].pts, na = base[k].ns, nb = base[k + 1].ns;
        if (out.skin && base[k].role === 'skin' && base[k + 1].role === 'skin') {   // skin: smooth, as the face is
          const cs = [out.skin, out.skin, out.skin];
          out.push({ v: [a[i], b[j], a[j]], role: 'skin', part: role, ns: [na[i], nb[j], na[j]], cs }, { v: [a[i], b[i], b[j]], role: 'skin', part: role, ns: [na[i], nb[i], nb[j]], cs });
        } else out.push({ v: [a[i], b[j], a[j]], role: base[k].role, part: role }, { v: [a[i], b[i], b[j]], role: base[k].role, part: role });
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
    tris.skin = fig.skin || [196, 152, 122];   // skin is drawn smooth, in this colour (sides, tube)
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
    const nr = (w, d) => Array.from({ length: SEG }, (_, i) => {
      const t = i / SEG * Math.PI * 2, ca = Math.cos(t), sa = Math.sin(t);
      return norm(add(mul(x, Math.sign(ca) * Math.pow(Math.abs(ca), 0.8) / w), mul(z, Math.sign(sa) * Math.pow(Math.abs(sa), 0.8) / d)));
    });
    const NA = nr(w0, d0), NB = nr(w1, d1), cs = out.skin && role === 'skin' ? [out.skin, out.skin, out.skin] : null;
    for (let i = 0; i < SEG; i++) {
      const j = (i + 1) % SEG;
      if (cs) out.push({ v: [A[i], B[j], A[j]], role, ns: [NA[i], NB[j], NA[j]], cs }, { v: [A[i], B[i], B[j]], role, ns: [NA[i], NB[i], NB[j]], cs });   // skin: smooth
      else out.push({ v: [A[i], B[j], A[j]], role }, { v: [A[i], B[i], B[j]], role });
      out.push({ v: [A[i], A[j], a], role }, { v: [B[j], B[i], b], role });
    }
  }

  // ---- the head, by the face's thirds ----
  // rings from the chin (u = 0) to the crown (u = 1) in head heights: half-width, forward and back extent. The chin
  // to the nose's base, the nose's base to the brows and the brows to the hairline are each a third of the face
  // (chin to hairline = 4/5 of the head); the ear is as long as one third, between the brows and the nose's base.
  const HEAD = [
    // u, half-width, forward (the face's profile), back: a head about three quarters as broad as it is tall
    [0.0, 0.112, 0.30, -0.05], [0.04, 0.16, 0.40, -0.1], [0.1, 0.224, 0.41, -0.2], [0.16, 0.264, 0.43, -0.3], [0.2, 0.28, 0.44, -0.34],
    [0.24, 0.296, 0.45, -0.38], [0.267, 0.312, 0.44, -0.4], [0.33, 0.336, 0.43, -0.43], [0.4, 0.352, 0.44, -0.46], [0.47, 0.368, 0.43, -0.48],
    [0.533, 0.368, 0.465, -0.49], [0.6, 0.368, 0.46, -0.5], [0.7, 0.368, 0.445, -0.5], [0.8, 0.36, 0.41, -0.49], [0.88, 0.328, 0.34, -0.44],
    [0.94, 0.256, 0.24, -0.34], [0.98, 0.144, 0.12, -0.18], [1.0, 0.032, 0.02, -0.03]];
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
  // ---- the face engine ----
  // The head is a fine mesh round the head bone, shaded smooth, its shape set by landmarks from the classical rules:
  // the eyes at half the head's height, one eye's width apart (the head five eyes wide), the nose as wide as the eyes
  // are apart at their inner corners, the mouth's corners under the pupils, the ears between the brow line and the
  // nose's base, the face in thirds. Per person, parameters (0-1) shape the brow ridge, cheekbones, jaw, chin, nose,
  // lips, eye sockets and age; expressions (jaw open, mouth shapes, brows, blink, smile, gaze) move it.
  const FACE_DEFAULT = { brow: 0.5, cheek: 0.5, jaw: 0.5, chin: 0.5, bridge: 0.5, tip: 0.5, lips: 0.5, socket: 0.5, age: 0.3, stubble: 0.2 };
  // rings up the head and segments round it: for the film, and finer for a close-up (fig.detail)
  const GRID = [[36, 40], [60, 72]];
  const EYE_U = 0.5;
  // the landmarks in head units (head height = 1); x across, u up from the chin, z forward
  function landmarks() {
    const hwEye = headAt(EYE_U)[0], EW = 2 * hwEye / 5;   // five eyes across the head at the eyes
    return {
      EW, eyeX: EW, inner: EW / 2, outer: 1.5 * EW, eyeU: EYE_U, brow: 2 / 3 * 0.8, noseBase: 1 / 3 * 0.8, hairline: 0.8,
      mouthU: 0.8 / 3 * 2 / 3, mouthX: EW, noseHalf: EW / 2, earTop: 2 / 3 * 0.8, earBottom: 1 / 3 * 0.8
    };
  }
  const gauss = (d2, r) => Math.exp(-d2 / (r * r));
  // the face's own value noise: a tiny hash of a lattice, smoothly blended, for skin tone and stubble
  const hash = (i, j) => { let h = (i * 374761393 + j * 668265263) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
  const noise = (x, y) => {
    const i = Math.floor(x), j = Math.floor(y), fx = x - i, fy = y - j, sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const a = hash(i, j), b = hash(i + 1, j), c = hash(i, j + 1), d = hash(i + 1, j + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
  };
  // the mouth's shape from the expression: open (the jaw), wide, round (pursed), smile
  function headShape(u, a, fp, ex, L) {
    let [x, z] = surf(u, a);
    const front = Math.sin(a) > 0, ax = Math.abs(x);
    let dz = 0, dx = 0, du = 0;
    if (front) {
      // the brow ridge over the eyes; the sockets under it; the cheekbones below and outside the eyes
      dz += (0.015 + 0.04 * fp.brow) * gauss((u - 0.56) ** 2 * 6 + (ax - L.eyeX) ** 2 * 0.5, 0.06);   // over each eye
      dz -= (0.02 + 0.05 * fp.socket) * gauss((u - L.eyeU) ** 2 + (ax - L.eyeX) ** 2, 0.075);
      dz += (0.01 + 0.035 * fp.cheek) * gauss((u - 0.4) ** 2 + (ax - 0.3) ** 2, 0.09);
      // the chin forward, the lips full, the mouth's line between them
      dz += (0.01 + 0.04 * fp.chin) * gauss((u - 0.05) ** 2 * 3 + x * x, 0.1);
      const mw = L.mouthX * (1 + 0.25 * ex.wide + 0.15 * ex.smile - 0.35 * ex.round);
      const inMouth = gauss(Math.max(0, ax - mw * 0.8) ** 2, 0.05);
      dz += (0.008 + 0.022 * fp.lips) * gauss((u - L.mouthU - 0.028) ** 2, 0.022) * inMouth * (1 + 0.6 * ex.round);
      dz += (0.008 + 0.026 * fp.lips) * gauss((u - L.mouthU + 0.03) ** 2, 0.024) * inMouth * (1 + 0.6 * ex.round);
      dz -= 0.012 * gauss((u - L.mouthU) ** 2, 0.01) * inMouth;
      // age: the folds from the nose's wings to the mouth's corners
      dz -= 0.012 * fp.age * gauss((ax - (L.noseHalf + (L.mouthX - L.noseHalf) * (L.noseBase - u) / (L.noseBase - L.mouthU))) ** 2 + 0, 0.03) * (u < L.noseBase && u > L.mouthU - 0.02 ? 1 : 0);
      // the smile lifts the mouth's corners and the cheeks
      du += ex.smile * 0.02 * gauss((u - L.mouthU) ** 2 + (ax - L.mouthX) ** 2, 0.06);
      dz += ex.smile * 0.012 * gauss((u - 0.36) ** 2 + (ax - 0.26) ** 2, 0.07);
      // the finer modelling: the temples' hollow, the hollow under the cheekbones, the groove from the nose to the
      // upper lip, the fold under the lower lip
      dz -= 0.012 * gauss((u - 0.62) ** 2 + (ax - 0.34) ** 2, 0.06);
      dz -= (0.004 + 0.012 * fp.cheek) * gauss((u - 0.28) ** 2 + (ax - 0.29) ** 2, 0.05);
      dz -= 0.005 * gauss((u - (L.noseBase + L.mouthU) / 2) ** 2 * 3, 0.03) * gauss(x * x, 0.025);
      dz -= 0.008 * gauss((u - L.mouthU + 0.075) ** 2, 0.02) * inMouth;
    }
    // the jaw: wider or narrower below the mouth
    if (u < 0.3) x *= 1 + 0.22 * (fp.jaw - 0.5) * (1 - u / 0.3);
    const n = Math.hypot(x, z) || 1;
    return { x: x + x / n * dz + dx, z: z + z / n * dz, u: u + du };
  }
  function head(K, w, fig, out) {
    const F = frame(w), hH = K.H / 8, bottom = add(w.at, mul(F.along, -0.01 * K.H)), C = fig.kit || {};
    const fp = Object.assign({}, FACE_DEFAULT, fig.faceShape || {}), L = landmarks();
    const fe = fig.face || {}, ex = { open: fe.open || 0, wide: fe.wide || 0, round: fe.round || 0, smile: fe.smile || 0, browUp: fe.browUp || 0, frown: fe.frown || 0, blink: fe.blink || 0 };
    const skin = fig.skin || [196, 152, 122], hair = fig.hair || [52, 40, 32];
    // the jaw opens about a hinge in front of the ears: everything below the mouth's line turns down
    const jawA = ex.open * 0.26;
    const hinge = [0, 0.34, -0.12];   // u, z of the hinge
    // the lips part along the mouth's line: inside the mouth's width the lower lip goes with the jaw and the upper
    // stays; outside it the skin stretches smoothly from the cheeks down to the jaw
    const MW = L.mouthX * (1 + 0.25 * ex.wide + 0.15 * ex.smile - 0.35 * ex.round);
    const jawWeight = (u, x) => {
      const soft = u >= L.mouthU + 0.02 ? 0 : u <= L.mouthU - 0.06 ? 1 : (L.mouthU + 0.02 - u) / 0.08;
      const sharp = u < L.mouthU ? 1 : 0, inside = Math.max(0, Math.min(1, (MW * 1.05 - Math.abs(x)) / (MW * 0.25)));
      return sharp * inside + soft * (1 - inside);
    };
    const place = (u, x, z) => {
      const k = jawWeight(u, x) * jawA;
      if (k) { const du = u - hinge[1], dz = z - hinge[2], c = Math.cos(k), s = Math.sin(k); u = hinge[1] + du * c - dz * s; z = hinge[2] + du * s + dz * c; }
      return add(bottom, add(mul(F.along, u * hH), add(mul(F.across, x * hH), mul(F.fwd, z * hH))));
    };
    const P = (u, x, z) => place(u, x, z);
    // the skin's colour at a point: tone variation, warmer cheeks, nose and lips; stubble on the jaw; the hair
    const tone = (u, x, z) => {
      let c = skin.map(v => v * (0.96 + 0.08 * noise(x * 9 + 3, u * 9)));
      const warm = 0.08 * gauss((u - 0.38) ** 2 + (Math.abs(x) - 0.26) ** 2, 0.08) + 0.06 * gauss((u - 0.32) ** 2 + x * x, 0.05);
      c = [c[0] * (1 + warm), c[1] * (1 - warm * 0.3), c[2] * (1 - warm * 0.4)];
      // the lips: redder and a little darker, the lower one fuller in colour
      const lip = gauss((u - L.mouthU) ** 2 * 1.2, 0.045) * gauss(Math.max(0, Math.abs(x) - L.mouthX * 0.85) ** 2, 0.03) * (z > 0.2 ? 1 : 0);
      if (lip > 0.05) c = c.map((v, i) => v * (1 - lip * 0.35) + [170, 82, 80][i] * lip * 0.35);
      const st = fp.stubble * (u < L.mouthU + 0.04 ? 1 : u < L.noseBase ? Math.max(0, Math.abs(x) - L.noseHalf) * 3 : 0) * (z > -0.2 ? 1 : 0);
      if (st > 0) { const k = Math.min(1, st) * (0.25 + 0.2 * noise(x * 40, u * 40)); c = c.map((v, i) => v * (1 - k) + hair[i] * 0.6 * k); }
      // age lines: across the forehead and from the eyes' outer corners
      if (fp.age > 0.2 && z > 0.2) {
        const fl = u > 0.62 && u < 0.76 ? Math.max(0, Math.sin((u - 0.62) * 90)) ** 8 : 0;
        const cf = gauss((u - L.eyeU) ** 2 + (Math.abs(x) - L.outer - 0.05) ** 2, 0.05) * Math.max(0, Math.sin(u * 160)) ** 6;
        const k = (fl + cf) * 0.12 * fp.age;
        c = c.map(v => v * (1 - k));
      }
      // the shade the face casts on itself: the eye sockets, under the nose, under the lower lip and under the chin
      const ao = (z > 0.2 ? 0.2 * gauss((u - L.eyeU) ** 2 + (Math.abs(x) - L.eyeX) ** 2, 0.07) + 0.16 * gauss((u - L.noseBase + 0.015) ** 2 * 4 + x * x, 0.07)
        + 0.1 * gauss((u - L.mouthU + 0.05) ** 2 * 4 + x * x, 0.06) : 0) + 0.2 * gauss(u * u, 0.035);
      return c.map(v => v * (1 - ao));
    };
    // how much hair covers a point (0-1), soft at the hairline, the temples and the nape
    const smooth01 = (e0, e1, v) => { const t = Math.max(0, Math.min(1, (v - e0) / (e1 - e0))); return t * t * (3 - 2 * t); };
    const hairAt = (u, x, z) => Math.max(smooth01(L.hairline - 0.02, L.hairline + 0.03, u + 0.015 * Math.cos(x * 6)) * smooth01(-0.1, 0.0, z + 0.6 - u),
      smooth01(0.0, -0.1, z) * smooth01(0.38, 0.46, u), smooth01(0.33, 0.4, Math.abs(x)) * smooth01(0.56, 0.64, u) * smooth01(0.3, 0.18, z));
    // the grid: u from the chin to the crown, finer round the face
    const [FU, FA] = GRID[fig.detail ? 1 : 0];
    const us = Array.from({ length: FU + 1 }, (_, k) => { const t = k / FU; return t * t * (3 - 2 * t) * 0.5 + t * 0.5; })
      .filter(u => Math.abs(u - L.mouthU) > 0.008).concat([L.mouthU - 0.002, L.mouthU + 0.002]).sort((a, b) => a - b);
    const lipRow = us.indexOf(L.mouthU - 0.002);   // the row between the lips: left open across the mouth
    const G = us.map(u => Array.from({ length: FA }, (_, i) => {
      const a = i / FA * Math.PI * 2, sh = headShape(u, a, fp, ex, L), p = P(sh.u, sh.x, sh.z);
      const h = hairAt(u, sh.x, sh.z), sk = tone(u, sh.x, sh.z), hc = hair.map(v => v * (0.9 + 0.2 * noise(sh.x * 30 + 7, u * 30)));
      return { p, col: sk.map((v, j) => v * (1 - h) + hc[j] * h), hr: h > 0.5, u, x: sh.x, z: sh.z };
    }));
    // smooth normals: each vertex's from its neighbours round and up the grid
    const N = G.map((row, k) => row.map((q, i) => {
      const a = G[Math.min(G.length - 1, k + 1)][i].p, b = G[Math.max(0, k - 1)][i].p, c = row[(i + 1) % FA].p, d = row[(i + FA - 1) % FA].p;
      return norm(cross(sub(c, d), sub(a, b)));
    }));
    const tri = (A, B, Cc, nA, nB, nC, part) => out.push({ v: [A.p, B.p, Cc.p], role: A.hr && B.hr && Cc.hr ? 'hair' : 'skin', part, ns: [nA, nB, nC], cs: [A.col, B.col, Cc.col] });
    for (let k = 0; k + 1 < us.length; k++)
      for (let i = 0; i < FA; i++) {
        const j = (i + 1) % FA;
        if (k === lipRow && G[k][i].z > 0.2 && Math.abs(G[k][i].x) < MW * 0.98 && Math.abs(G[k][j].x) < MW * 0.98) continue;
        tri(G[k][i], G[k + 1][j], G[k][j], N[k][i], N[k + 1][j], N[k][j], 'head');
        tri(G[k][i], G[k + 1][i], G[k + 1][j], N[k][i], N[k + 1][i], N[k + 1][j], 'head');
      }
    const c0 = P(0, 0, 0.12);
    for (let i = 0; i < FA; i++) out.push({ v: [G[0][i].p, G[0][(i + 1) % FA].p, c0], role: 'skin', part: 'head', col: tone(0, 0, 0.1) });
    // the mouth's inside, dark, and the teeth, seen when the jaw opens
    {
      const mz = 0.38, mw = MW * 0.95, P0 = (u, x, z) => add(bottom, add(mul(F.along, u * hH), add(mul(F.across, x * hH), mul(F.fwd, z * hH))));
      const q = (u0, u1, x0, x1, z, col) => { const A = P0(u0, x0, z), B = P0(u0, x1, z), Cc = P0(u1, x1, z), Dd = P0(u1, x0, z); out.push({ v: [A, B, Cc], role: 'mouth', two: true, part: 'face', col }, { v: [A, Cc, Dd], role: 'mouth', two: true, part: 'face', col }); };
      q(L.mouthU - 0.02 - ex.open * 0.09, L.mouthU + 0.01, -mw, mw, mz, [52, 20, 22]);
      if (ex.open > 0.05) q(L.mouthU - 0.006 - 0.012 * ex.open, L.mouthU + 0.004, -mw * 0.7, mw * 0.7, mz + 0.03, [214, 208, 196]);   // the upper teeth
    }
    // the nose: rings from between the eyes to the tip, then in to the base between the wings
    const noseRing = (u, w, d) => {
      const [, zf] = surf(u, Math.PI / 2), rows = [];
      for (let i = 0; i <= 8; i++) { const a = Math.PI * i / 8, x = Math.cos(a) * w, zz = zf + Math.sin(a) * d - 0.004; rows.push({ p: P(u, x, zz), col: tone(u, x, zz), hr: false }); }
      return rows;
    };
    const bridge = 0.6 + 0.8 * fp.bridge, tip = 0.7 + 0.6 * fp.tip;
    const NR = [[0.53, 0.04, 0.01 * bridge], [0.47, 0.05, 0.03 * bridge], [0.41, 0.06, 0.05 * bridge], [0.35, 0.075, 0.075 * tip], [0.31, 0.09, 0.09 * tip], [0.285, L.noseHalf, 0.07 * tip], [0.268, L.noseHalf * 0.85, 0.035]].map(r => noseRing(...r));
    const NN = NR.map((row, k) => row.map((q, i) => {
      const up = NR[Math.max(0, k - 1)][i].p, dn = NR[Math.min(NR.length - 1, k + 1)][i].p, l = row[Math.max(0, i - 1)].p, r = row[Math.min(8, i + 1)].p;
      return norm(cross(sub(r, l), sub(dn, up)));
    }));
    for (let k = 0; k + 1 < NR.length; k++)
      for (let i = 0; i < 8; i++) {
        const A = NR[k][i], B = NR[k][i + 1], Cc = NR[k + 1][i + 1], Dd = NR[k + 1][i];
        out.push({ v: [A.p, Dd.p, Cc.p], role: 'skin', part: 'face', two: true, ns: [NN[k][i], NN[k + 1][i], NN[k + 1][i + 1]], cs: [A.col, Dd.col, Cc.col] },
          { v: [A.p, Cc.p, B.p], role: 'skin', part: 'face', two: true, ns: [NN[k][i], NN[k + 1][i + 1], NN[k][i + 1]], cs: [A.col, Cc.col, B.col] });
      }
    // the nostrils: two dark slots under the tip
    for (const sx of [1, -1]) {
      const [, zf] = surf(0.275, Math.PI / 2), a0 = P(0.272, sx * 0.02, zf + 0.06), a1 = P(0.272, sx * 0.065, zf + 0.04), a2 = P(0.278, sx * 0.045, zf + 0.075);
      out.push({ v: [a0, a1, a2], role: 'mouth', part: 'face', two: true, col: [70, 40, 38] });
    }
    // the eyes: eyeballs in their sockets, the iris and pupil turned to the gaze, lids that close to blink
    const gaze = fe.gaze ? norm(sub(fe.gaze, add(bottom, mul(F.along, L.eyeU * hH)))) : F.fwd;
    for (const sx of [1, -1]) {
      const rE = L.EW * 0.5, [, zf] = surf(L.eyeU, angleAt(L.eyeU, L.eyeX * sx)), ec = P(L.eyeU, L.eyeX * sx, zf - rE * 0.9 - 0.03 * fp.socket);
      const RE = rE * hH, ax = F.across, up = F.along, fw = F.fwd;
      // the eyeball, smooth: each point's normal straight out from its centre
      const sphere = (c, r, role, col) => {
        const rows = 8, segs = 14, pts = [];
        for (let k = 0; k <= rows; k++) { const b = -Math.PI / 2 + Math.PI * k / rows; pts.push(Array.from({ length: segs }, (_, i) => { const a = i / segs * Math.PI * 2; return add(c, add(mul(ax, Math.cos(b) * Math.cos(a) * r), add(mul(up, Math.sin(b) * r), mul(fw, Math.cos(b) * Math.sin(a) * r)))); })); }
        const nn = p => norm(sub(p, c)), cs = [col, col, col];
        for (let k = 0; k < rows; k++) for (let i = 0; i < segs; i++) {
          const j = (i + 1) % segs, A = pts[k][i], B = pts[k + 1][j], Cc = pts[k][j], Dd = pts[k + 1][i];
          out.push({ v: [A, B, Cc], role, part: 'eye', ns: [nn(A), nn(B), nn(Cc)], cs }, { v: [A, Dd, B], role, part: 'eye', ns: [nn(A), nn(Dd), nn(B)], cs });
        }
      };
      sphere(ec, RE, 'eyeWhite', [222, 216, 208]);
      // the iris and pupil: discs on the eyeball's front, toward the gaze
      const g = norm(add(mul(gaze, 1), mul(fw, 0.4))), gx = norm(cross(up, g)), gy = cross(g, gx);
      const disc = (r, lift, col, role, ox = 0, oy = 0) => {
        const c = add(add(ec, mul(g, RE * lift)), add(mul(gx, ox * RE), mul(gy, oy * RE))), ring = Array.from({ length: 10 }, (_, i) => { const a = i / 10 * Math.PI * 2; return add(c, add(mul(gx, Math.cos(a) * r), mul(gy, Math.sin(a) * r))); });
        for (let i = 0; i < 10; i++) out.push({ v: [c, ring[i], ring[(i + 1) % 10]], role, part: 'eye', two: true, col });
      };
      disc(RE * 0.46, 1.0, fig.iris || [70, 58, 44], 'iris');
      disc(RE * 0.19, 1.012, [10, 10, 12], 'iris');
      // the wet eye: the screen's light caught in it, and a smaller second light
      disc(RE * 0.07, 1.022, [240, 240, 236], 'iris', -0.17, 0.2);
      disc(RE * 0.035, 1.024, [220, 224, 226], 'iris', 0.16, -0.12);
      // the lids: the upper comes down over the eyeball to blink, the lower stays
      const lid = (from, to, col, rk = 1.07) => {
        const segs = 10, rows = 3, r = RE * rk, pts = [];
        for (let k = 0; k <= rows; k++) { const b = from + (to - from) * k / rows; pts.push(Array.from({ length: segs + 1 }, (_, i) => { const a = Math.PI * 0.08 + Math.PI * 0.84 * i / segs; return add(ec, add(mul(ax, Math.cos(b) * Math.cos(a) * r), add(mul(up, Math.sin(b) * r), mul(fw, Math.cos(b) * Math.sin(a) * r)))); })); }
        const nn = p => norm(sub(p, ec)), cs = [col, col, col];   // smooth, round the eyeball
        for (let k = 0; k < rows; k++) for (let i = 0; i < segs; i++) {
          const A = pts[k][i], B = pts[k + 1][i + 1], Cc = pts[k][i + 1], Dd = pts[k + 1][i];
          out.push({ v: [A, B, Cc], role: 'lid', part: 'eye', two: true, ns: [nn(A), nn(B), nn(Cc)], cs }, { v: [A, Dd, B], role: 'lid', part: 'eye', two: true, ns: [nn(A), nn(Dd), nn(B)], cs });
        }
      };
      const open = -0.36 + (0.62 + 0.1 * ex.browUp - 0.08 * ex.frown) * (1 - ex.blink);   // the upper lid's edge, from the eye's middle
      lid(Math.PI / 2, open, tone(L.eyeU + 0.04, L.eyeX, zf).map(v => v * 0.92));
      lid(open + 0.06, open - 0.03, hair.map(v => v * 0.45), 1.085);   // the lashes along the upper lid's edge
      lid(-0.38, -Math.PI / 2, tone(L.eyeU - 0.04, L.eyeX, zf).map(v => v * 0.95));
      // the brow: a band of hair over the ridge, the inner end lifting or drawing down
      const bu = (k) => 0.57 + 0.035 * ex.browUp * (1 - k * 0.3) - 0.025 * ex.frown * (1 - k);
      const bpts = [0, 0.25, 0.5, 0.75, 1].map(k => { const x = sx * (L.inner * 0.9 + (L.outer + 0.06 - L.inner * 0.9) * k), [, zb] = surf(bu(k), angleAt(bu(k), x)); return [P(bu(k) + 0.018 * Math.sin(k * Math.PI), x, zb + 0.022), P(bu(k) - 0.012, x, zb + 0.022)]; });
      for (let k = 0; k + 1 < bpts.length; k++) out.push({ v: [bpts[k][0], bpts[k + 1][0], bpts[k + 1][1]], role: 'brow', part: 'face', two: true }, { v: [bpts[k][0], bpts[k + 1][1], bpts[k][1]], role: 'brow', part: 'face', two: true });
      // the ear, between the brow line and the nose's base, flat against the head
      const ec2 = P((L.earTop + L.earBottom) / 2, 0.375 * sx, -0.06), half = (L.earTop - L.earBottom) / 2 * hH;
      const e0 = add(ec2, mul(F.along, -half)), e1 = add(ec2, mul(F.along, half * 0.15)), e2 = add(ec2, mul(F.along, half));
      tube(K, e0, e1, F.fwd, 0.006, 0.003, 0.012, 0.0035, 'skin', out);
      tube(K, e1, e2, F.fwd, 0.012, 0.0035, 0.007, 0.003, 'skin', out);
    }
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
      const bill = [P(0.725, -0.35, 0.44), P(0.725, 0.35, 0.44), P(0.69, 0.28, 0.86), P(0.69, -0.28, 0.86)];
      out.push({ v: [bill[0], bill[1], bill[2]], role: 'cap', two: true }, { v: [bill[0], bill[2], bill[3]], role: 'cap', two: true });
      if (C.gold) { /* the gold oak leaves along the bill's front edge */
        const g0 = [P(0.712, -0.31, 0.6), P(0.712, 0.31, 0.6), P(0.692, 0.27, 0.84), P(0.692, -0.27, 0.84)];
        out.push({ v: [g0[0], g0[1], g0[2]], role: 'gold', two: true }, { v: [g0[0], g0[2], g0[3]], role: 'gold', two: true });
      }
      const ship = [P(0.79, -0.18, 0.46), P(0.79, 0.18, 0.46), P(0.88, 0.15, 0.4), P(0.88, -0.15, 0.4)];
      out.push({ v: [ship[0], ship[1], ship[2]], role: 'gold', two: true }, { v: [ship[0], ship[2], ship[3]], role: 'gold', two: true });
    }
    // the headset: a band over the crown, cups over the ears, a boom to the corner of the mouth
    if (C.headset) {
      const band = [];
      for (let i = 0; i <= 8; i++) { const a = Math.PI * i / 8; band.push(P(0.4 + Math.sin(a) * 0.62, Math.cos(a) * 0.42, -0.03)); }
      for (let i = 0; i + 1 < band.length; i++) tube(K, band[i], band[i + 1], F.fwd, 0.004, 0.003, 0.004, 0.003, 'kit', out);
      for (const sx of [1, -1]) { const c = P(0.4, 0.42 * sx, -0.04); tube(K, sub(c, mul(F.across, 0.04 * sx * hH)), add(c, mul(F.across, 0.04 * sx * hH)), F.fwd, 0.011, 0.012, 0.011, 0.012, 'kit', out); }
      tube(K, P(0.38, 0.42, 0.0), P(0.22, 0.2, 0.45), F.across, 0.0015, 0.0015, 0.0015, 0.0015, 'kit', out);
      tube(K, P(0.22, 0.2, 0.45), P(0.2, 0.08, 0.5), F.across, 0.004, 0.004, 0.004, 0.004, 'kit', out);
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
    // the enlisted working uniform of the early 1990s: a light-blue chambray shirt, dark-blue denim dungarees, a black
    // belt, the white T-shirt at the open collar, the name stencilled over the pocket
    dungarees: { shirt: [118, 146, 184], sleeve: [118, 146, 184], trousers: [38, 48, 76], belt: [30, 28, 26], collar: [118, 146, 184], pocket: [108, 136, 174], tape: [30, 36, 52], tee: [228, 226, 218] },
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

  // the gradient that reproduces three corner colours over a triangle: along the direction the brightness changes
  // fastest, from the colour at one end of the triangle's extent to the other (each colour channel is affine over the
  // triangle, so it is exact for the brightness and close for the hue)
  function gradient(P, cs) {
    const lum = c => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
    const x1 = P[1][0] - P[0][0], y1 = P[1][1] - P[0][1], x2 = P[2][0] - P[0][0], y2 = P[2][1] - P[0][1], det = x1 * y2 - x2 * y1;
    if (Math.abs(det) < 1e-9) return null;
    const field = f => { const a = f(cs[1]) - f(cs[0]), b = f(cs[2]) - f(cs[0]); return [(a * y2 - b * y1) / det, (b * x1 - a * x2) / det]; };
    const g = field(lum), gl = Math.hypot(g[0], g[1]);
    if (gl < 1e-6) return null;
    const ux = g[0] / gl, uy = g[1] / gl, proj = P.map(p => (p[0] - P[0][0]) * ux + (p[1] - P[0][1]) * uy), lo = Math.min(...proj), hi = Math.max(...proj);
    const at = s => [0, 1, 2].map(i => { const gi = field(c => c[i]); return cs[0][i] + (gi[0] * ux + gi[1] * uy) * s; }).map(v => Math.max(0, Math.min(255, v)));
    return { x0: P[0][0] + ux * lo, y0: P[0][1] + uy * lo, x1: P[0][0] + ux * hi, y1: P[0][1] + uy * hi, c0: at(lo), c1: at(hi) };
  }

  // ---- drawing: project, sort far to near, shade each facet (key light, fill, rim) ----
  // cam: { pos, f (forward), u (up), r (right), F (focal length in pixels), cx, cy }; light: direction toward the key
  // light and its colour. fill(points, colour) draws one triangle.
  const SKIN_ROLES = new Set(['skin', 'skinShade', 'lip', 'lid']), SSS = [0.22, 0.06, 0.03];
  // a triangle lit for the camera: culled when it faces away (unless two-sided), else its corners on the screen, its
  // depth, and the colour at each corner (as fig_draw and fig_raster light them in the C game)
  function litTri(t, fig, cam, light) {
    let n = norm(cross(sub(t.v[1], t.v[0]), sub(t.v[2], t.v[0])));
    const c = mul(add(add(t.v[0], t.v[1]), t.v[2]), 1 / 3), d = sub(c, cam.pos), z = dot(d, cam.f);
    if (z < 0.05) return null;
    if (dot(n, d) > 0) {
      if (!t.two) return null; // facing away
      n = mul(n, -1);          // a flat part (eyes, lips, insignia) shows either side
    }
    const zs = t.v.map(v => dot(sub(v, cam.pos), cam.f));
    const pts = t.v.map((v, q) => { const p = sub(v, cam.pos), zz = Math.max(0.05, zs[q]); return [cam.cx + dot(p, cam.r) / zz * cam.F, cam.cy - dot(p, cam.u) / zz * cam.F]; });
    const view = norm(mul(d, -1));
    // skin: its own light, so a coloured screen tints a face but does not paint it; wrapped and soft, warm where
    // the light turns to shadow (light scattered under the skin), a faint sheen, a softer rim
    const skinShade = (nn, base) => {
      const kl = dot(nn, light.dir), w = Math.max(0, (kl + 0.35) / 1.35), sss = w * (1 - w) * 4;
      const kL = (light.key[0] + light.key[1] + light.key[2]) / 3, key = light.key.map(v => kL + (v - kL) * 0.4);
      const r = sub(mul(nn, 2 * kl), light.dir), sp = kl > 0 ? Math.pow(Math.max(0, dot(r, view)), 16) * 0.1 : 0, rim = Math.pow(1 - Math.max(0, dot(nn, view)), 3);
      return base.map((b, i) => Math.min(255, b * (0.28 + 0.8 * w * key[i] / 255) + b * SSS[i] * sss * key[i] / 255 + 255 * sp * key[i] / 255 + 255 * rim * 0.14 * light.rim[i] / 255));
    };
    const shade = (nn, base) => {
      const k = Math.max(0, dot(nn, light.dir)), rim = Math.pow(1 - Math.max(0, dot(nn, view)), 3);
      return base.map((b, i) => Math.min(255, b * (0.24 + 0.85 * k * light.key[i] / 255) + 255 * rim * 0.2 * light.rim[i] / 255));
    };
    let cs;
    if (t.lit) cs = [t.col, t.col, t.col];   // the room, already lit (lightRoom)
    else if (t.ns) { const sh = SKIN_ROLES.has(t.role) ? skinShade : shade; cs = t.ns.map((nn, q) => sh(dot(nn, view) < 0 ? mul(nn, -1) : nn, t.cs[q])); }
    else { const c1 = (SKIN_ROLES.has(t.role) ? skinShade : shade)(n, t.col || colour(fig, t.role)); cs = [c1, c1, c1]; }
    return { z, zs, pts, cs, smooth: !!t.ns && !t.lit, tag: t.tag || 0 };
  }
  function draw(figs, cam, light, fill) {
    const all = [];
    for (const fig of figs) {
      const mesh = fig.mesh || build(fig);
      for (const t of mesh.tris) {
        const L = litTri(t, fig, cam, light);
        if (!L) continue;
        // a smooth part (the face): each corner lit by its own normal; the colour across the facet is then an affine
        // field, drawn as a linear gradient along its steepest direction (the same as shading the corners and
        // blending between them)
        if (L.smooth) all.push({ z: L.z, pts: L.pts, col: L.cs.reduce((a, c) => a.map((v, i) => v + c[i] / 3), [0, 0, 0]), grad: gradient(L.pts, L.cs) });
        else all.push({ z: L.z, pts: L.pts, col: L.cs[0], tag: L.tag });
      }
    }
    all.sort((a, b) => b.z - a.z);
    for (const t of all) fill(t.pts, t.col, t.grad, t.tag || 0);
  }

  // ---- the close-up path: people drawn with a depth buffer ----
  // The painter's far-to-near sort is enough for the room, but a face up close has small parts in layers (the
  // eyeball, the iris, the lids, the lashes, the brows over the skin), so people are drawn here a pixel at a time:
  // each triangle's corners lit, the colour blended smoothly across it, the nearest surface kept at every sample, two
  // by two samples to a pixel so the edges are smooth. The camera's picture is scaled by sc and moved by (ox, oy)
  // into a W x H picture; returns the box drawn and its pixels (RGBA, the alpha how much of each pixel is covered).
  function raster(figs, cam, light, sc, ox, oy, W, H) {
    const RSS = 2, tris = [];
    let bx0 = Infinity, by0 = Infinity, bx1 = -Infinity, by1 = -Infinity;
    for (const fig of figs) {
      const mesh = fig.mesh || build(fig);
      for (const t of mesh.tris) {
        for (const v of t.v) {
          const p = sub(v, cam.pos), zz = dot(p, cam.f);
          if (zz < 0.05) continue;
          const x = (cam.cx + dot(p, cam.r) / zz * cam.F) * sc + ox, y = (cam.cy - dot(p, cam.u) / zz * cam.F) * sc + oy;
          bx0 = Math.min(bx0, x); by0 = Math.min(by0, y); bx1 = Math.max(bx1, x); by1 = Math.max(by1, y);
        }
        const L = litTri(t, fig, cam, light);
        if (L && L.zs.every(z => z >= 0.05)) tris.push(L);
      }
    }
    const x0 = Math.max(0, Math.floor(bx0) - 1), y0 = Math.max(0, Math.floor(by0) - 1), x1 = Math.min(W, Math.ceil(bx1) + 1), y1 = Math.min(H, Math.ceil(by1) + 1);
    if (x1 <= x0 || y1 <= y0) return null;
    const bw = (x1 - x0) * RSS, bh = (y1 - y0) * RSS, Z = new Float32Array(bw * bh), Cb = new Float32Array(bw * bh * 3);
    for (const L of tris) {
      const X = L.pts.map(p => (p[0] * sc + ox - x0) * RSS), Y = L.pts.map(p => (p[1] * sc + oy - y0) * RSS), IZ = L.zs.map(z => 1 / z), cs = L.cs;
      const area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
      if (Math.abs(area) < 1e-9) continue;
      const sx0 = Math.max(0, Math.floor(Math.min(...X))), sx1 = Math.min(bw, Math.ceil(Math.max(...X))), sy0 = Math.max(0, Math.floor(Math.min(...Y))), sy1 = Math.min(bh, Math.ceil(Math.max(...Y)));
      for (let y = sy0; y < sy1; y++)
        for (let x = sx0; x < sx1; x++) {
          const px = x + 0.5, py = y + 0.5;
          const w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) / area, w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) / area, w2 = 1 - w0 - w1;
          if (w0 < 0 || w1 < 0 || w2 < 0) continue;
          const iz = w0 * IZ[0] + w1 * IZ[1] + w2 * IZ[2], at = y * bw + x;
          if (iz <= Z[at]) continue;
          Z[at] = iz;
          for (let j = 0; j < 3; j++) Cb[at * 3 + j] = w0 * cs[0][j] + w1 * cs[1][j] + w2 * cs[2][j];
        }
    }
    const w = x1 - x0, h = y1 - y0, rgba = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        let r = 0, g = 0, b = 0, cov = 0;
        for (let sy = 0; sy < RSS; sy++) for (let sx = 0; sx < RSS; sx++) {
          const at = (y * RSS + sy) * bw + x * RSS + sx;
          if (Z[at] > 0) { cov++; r += Cb[at * 3]; g += Cb[at * 3 + 1]; b += Cb[at * 3 + 2]; }
        }
        if (!cov) continue;
        const o = (y * w + x) * 4;
        rgba[o] = r / cov; rgba[o + 1] = g / cov; rgba[o + 2] = b / cov; rgba[o + 3] = 255 * cov / (RSS * RSS);
      }
    return { x: x0, y: y0, w, h, rgba };
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
    // the air detector in chambray and dungarees and a sound-powered phone headset, seated, leaning in to his scope,
    // both hands forward on the keyboard
    // twenty-four: a smooth face, a short nose, full lips, shallow sockets, a clean shave
    operator: { kit: { uniform: 'dungarees', headset: true, nametape: true }, skin: [205, 160, 128], hair: [34, 26, 20], iris: [86, 62, 40],
      faceShape: { brow: 0.3, cheek: 0.45, jaw: 0.45, chin: 0.45, bridge: 0.35, tip: 0.55, lips: 0.65, socket: 0.3, age: 0.05, stubble: 0.08 },
      pose: H => SEAT(H, { hips: [14, 0, 0], chest: [8, 0, 0], neck: [-12, 0, 0], head: [-8, 0, 0],
        upperL: [0, -62, -58], upperR: [0, 62, 58], foreL: [0, -48, 6], foreR: [0, 48, -6], handL: [0, -10, -8], handR: [0, 10, 8], curlL: 30, curlR: 34 }) },
    // the chief in service khakis with his anchors, standing, the clipboard held at his waist
    // forty-five: a heavy brow and a square jaw, a broken-in nose, a day's stubble
    supervisor: { kit: { uniform: 'khaki', cap: true, device: 'anchor', clipboard: true }, skin: [190, 140, 110], hair: [46, 40, 36], iris: [60, 70, 80],
      faceShape: { brow: 0.75, cheek: 0.55, jaw: 0.8, chin: 0.6, bridge: 0.7, tip: 0.65, lips: 0.4, socket: 0.6, age: 0.5, stubble: 0.55 },
      pose: () => ({ upperL: [0, -8, -82], foreL: [0, -86, 0], handL: [0, 0, 16], curlL: 60, upperR: [0, 8, 82], foreR: [0, 12, 0], handR: [0, 0, 0], curlR: 24, head: [0, -6, 0] }) },
    // the commander in khakis with his silver oak leaves and the ship's cap with gold on the bill, in his chair;
    // raise from 0 to 1 lifts his right hand as he gives the order
    // fifty-five: high cheekbones, a long straight nose, thin lips, deep-set eyes, lines at the eyes and brow
    commander: { kit: { uniform: 'khaki', cap: true, gold: true, device: 'oak' }, skin: [196, 150, 120], hair: [140, 138, 134], iris: [76, 92, 104],
      faceShape: { brow: 0.6, cheek: 0.75, jaw: 0.55, chin: 0.65, bridge: 0.8, tip: 0.45, lips: 0.3, socket: 0.8, age: 0.85, stubble: 0.25 },
      pose: (H, raise = 0) => SEAT(H, { hips: [-6, 0, 0], chest: [-2, 0, 0], neck: [4, 0, 0],
        upperL: [0, 12, -84], foreL: [0, -88, 0], handL: [0, -6, -10], curlL: 45,
        upperR: [0, -12 + 62 * raise, 84 - 40 * raise], foreR: [0, 88 - 20 * raise, 55 * raise], handR: [0, 0, -15 * raise], curlR: 45 - 38 * raise }) }
  };
  // ---- speech: the dialogue's words turned into mouth shapes over the card's time ----
  // Our own small letter-to-sound rules and a word list for the film's lines give phonemes; each phoneme has a mouth
  // shape (a viseme: how open, wide and round the mouth is); the phonemes are paced over the card's window (vowels
  // longer than consonants, pauses at the punctuation, the brisk pace of a voice on a ship's net), and the shapes are
  // blended where they meet, as a real mouth runs one sound into the next.
  const VISEMES = {
    rest: [0, 0, 0], mbp: [0, 0, 0], fv: [0.08, 0.2, 0], th: [0.15, 0.25, 0], aa: [0.75, 0.35, 0], ee: [0.3, 0.9, 0],
    ih: [0.4, 0.55, 0], oo: [0.35, 0, 0.9], oh: [0.6, 0.1, 0.6], w: [0.15, 0, 0.8], sh: [0.25, 0.1, 0.5], tt: [0.25, 0.4, 0],
    kk: [0.35, 0.3, 0], ss: [0.12, 0.6, 0]
  };
  // phonemes (our own short names) and their mouth shapes
  const PH = {
    AA: 'aa', AE: 'aa', AH: 'aa', AO: 'oh', AW: 'aa', AY: 'aa', EH: 'ee', ER: 'ih', EY: 'ee', IH: 'ih', IY: 'ee', OW: 'oh', OY: 'oh',
    UH: 'oo', UW: 'oo', B: 'mbp', P: 'mbp', M: 'mbp', F: 'fv', V: 'fv', TH: 'th', DH: 'th', T: 'tt', D: 'tt', N: 'tt', L: 'tt',
    S: 'ss', Z: 'ss', SH: 'sh', TSH: 'sh', JH: 'sh', K: 'kk', G: 'kk', NG: 'kk', HH: 'kk', R: 'w', W: 'w', Y: 'ee'
  };
  const VOWEL = new Set(['AA', 'AE', 'AH', 'AO', 'AW', 'AY', 'EH', 'ER', 'EY', 'IH', 'IY', 'OW', 'OY', 'UH', 'UW']);
  // the words of the film's lines, and the numbers as a ship's voice says them
  const WORDS = {
    BOGEYS: 'B OW G IY Z', TWO: 'T UW', CONTACTS: 'K AA N T AE K T S', BEARING: 'B EH R IH NG', THREE: 'TH R IY', FIVE: 'F AY V',
    ZERO: 'Z IY R OW', CLOSING: 'K L OW Z IH NG', FAST: 'F AE S T', ALERT: 'AH L ER T', WE: 'W IY', HAVE: 'HH AE V',
    INBOUND: 'IH N B AW N D', SEND: 'S EH N D', THE: 'DH AH', BIRDS: 'B ER D Z'
  };
  const DIGITS = ['ZERO', 'ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX', 'SEVEN', 'EIGHT', 'NINE'];
  // letter-to-sound rules for words not in the list: the common English spellings, longest first
  const RULES = [['TION', 'SH AH N'], ['SH', 'SH'], ['TH', 'TH'], ['PH', 'F'], ['NG', 'NG'], ['CK', 'K'], ['EE', 'IY'], ['EA', 'IY'], ['OO', 'UW'],
    ['OU', 'AW'], ['OW', 'OW'], ['AI', 'EY'], ['AY', 'EY'], ['OI', 'OY'], ['A', 'AE'], ['E', 'EH'], ['I', 'IH'], ['O', 'AA'], ['U', 'AH'], ['Y', 'IY'],
    ['B', 'B'], ['C', 'K'], ['D', 'D'], ['F', 'F'], ['G', 'G'], ['H', 'HH'], ['J', 'JH'], ['K', 'K'], ['L', 'L'], ['M', 'M'], ['N', 'N'], ['P', 'P'],
    ['Q', 'K'], ['R', 'R'], ['S', 'S'], ['T', 'T'], ['V', 'V'], ['W', 'W'], ['X', 'K S'], ['Z', 'Z']];
  function toPhonemes(word) {
    if (WORDS[word]) return WORDS[word].split(' ');
    const out = [];
    for (let i = 0; i < word.length;) {
      const r = RULES.find(([g]) => word.startsWith(g, i));
      if (!r) { i++; continue; }
      out.push(...r[1].split(' '));
      i += r[0].length;
    }
    return out;
  }
  // the words of a line, numbers spelt digit by digit, each with its phonemes and the pause after it
  function words(text) {
    const out = [];
    const re = /([A-Z']+|\d)([^A-Z'\d]*)/g;
    let m;
    while ((m = re.exec(text.toUpperCase()))) {
      const w = /\d/.test(m[1]) ? DIGITS[+m[1]] : m[1].replace(/'/g, '');
      const p = m[2], pause = /[.!?]/.test(p) ? 0.18 : /,/.test(p) ? 0.1 : 0;
      out.push({ word: m[1], ph: toPhonemes(w), pause });
    }
    return out;
  }
  // a line's timeline over [t0, t1]: every phoneme's start and end and shape, and where each word starts. The voice
  // fills the window but for a breath at the start and its last pause, the pace set by the window
  function speechTrack(text, t0, t1) {
    const W = words(text), lead = 0.06;
    let weight = 0, pauses = 0;
    for (const w of W) { for (const p of w.ph) weight += VOWEL.has(p) ? 1 : 0.55; pauses += w.pause; }
    const tail = W.length ? W[W.length - 1].pause : 0;
    const k = Math.max(0.01, (t1 - t0 - lead - pauses) / (weight || 1));
    const seg = [{ t0, t1: t0 + lead, v: 'rest' }], starts = [];
    let t = t0 + lead;
    W.forEach((w, i) => {
      starts.push(t);
      for (const p of w.ph) { const d = (VOWEL.has(p) ? 1 : 0.55) * k; seg.push({ t0: t, t1: t + d, v: PH[p] || 'rest', ph: p, word: i }); t += d; }
      if (w.pause) { seg.push({ t0: t, t1: t + w.pause, v: 'rest', word: i }); t += w.pause; }
    });
    seg[seg.length - 1].t1 = t1;   // the last stretch ends with the card
    void tail;
    return { seg, words: W.map(w => w.word), starts };
  }
  // the mouth at time t: the shapes of the phonemes near t, each weighted by a short raised-cosine round its middle,
  // so neighbouring sounds overlap (open, wide, round); outside the line the mouth rests
  function mouthAt(track, t) {
    let open = 0, wide = 0, round = 0, wsum = 0;
    for (const s of track.seg) {
      const mid = (s.t0 + s.t1) / 2, half = Math.max(0.05, (s.t1 - s.t0) * 0.9), d = Math.abs(t - mid);
      if (d >= half) continue;
      const w = 0.5 + 0.5 * Math.cos(Math.PI * d / half), v = VISEMES[s.v];
      open += v[0] * w; wide += v[1] * w; round += v[2] * w; wsum += w;
    }
    if (!wsum) return { open: 0, wide: 0, round: 0 };
    return { open: open / wsum, wide: wide / wsum, round: round / wsum };
  }
  // how many words of the caption are showing at t: a word appears as its first sound starts
  const wordsShown = (track, t) => track.starts.filter(s => s <= t).length;
  // the caption's text up to its nth spoken word, with the punctuation that follows that word
  function spokenUpTo(text, n) {
    if (n <= 0) return '';
    const re = /([A-Za-z']+|\d)([^A-Za-z'\d ]*)/g;
    let m, k = 0;
    while ((m = re.exec(text))) if (++k === n) return text.slice(0, m.index + m[0].length);
    return text;
  }

  // ---- the control room: steel, pipes and consoles, made in code like the people ----
  // A combat centre the way the big gun ships had them after their 1980s refits, here on our fictional carrier:
  // welded and riveted bulkhead plating with its frames, the deckhead crowded with pipes, a fire main, cable trays and
  // ducting, a row of grey consoles with round radar repeaters and green alphanumeric CRTs, two strike-weapon consoles,
  // edge-lit plexiglass status boards with a plotter writing on their far side, sound-powered phone boxes and their
  // headsets, a clock, the general-quarters light, rubber mats on the deck, and the operator's console close by. Every
  // big surface is cut into tiles, so the room's lamps (lightRoom) light it in pools and the painter's sort can place
  // people among it.
  // Each triangle: v, col (its paint), two (seen from either side), part ('far' or 'near'), emit (0 lit by the lamps;
  // 1 shines by itself; 2 and 3 the red and blue lamps, their brightness set when lit), tag (1 + the screen drawn on it,
  // or 0). A screen: kind (SCREEN), o its top-left corner, ex across and ey down, w x h its content's own units, and
  // bay (the console it sits in).
  const SCREEN = { scope: 1, ppi: 2, alpha: 3, strike: 4, harpoon: 5, boardL: 6, boardR: 7, clock: 8, gq: 9, label: 10, steam: 11, keys: 12, guards: 13, keyboard: 14 };
  const PAINT = {
    plate: [150, 154, 150], rivet: [176, 180, 174], weld: [70, 72, 70], frame: [132, 136, 132], deck: [70, 72, 74], deckhead: [120, 124, 122],
    mat: [26, 26, 28], rib: [40, 40, 42], pipe: [184, 184, 178], pipeGrey: [110, 116, 120], fire: [150, 34, 28], cable: [32, 32, 34],
    tray: [96, 100, 100], duct: [140, 144, 142], console: [120, 126, 130], consoleDark: [74, 80, 84], bezel: [26, 28, 30], glass: [6, 12, 9],
    key: [150, 158, 158], label: [30, 32, 34], phone: [104, 108, 104], black: [22, 22, 24], alu: [170, 176, 182], edge: [255, 214, 140],
    lensBlue: [130, 160, 230], lensRed: [235, 50, 36], amber: [255, 170, 60], green: [90, 255, 140], redLit: [255, 60, 50], mug: [200, 200, 192],
    coffee: [40, 24, 14], vinyl: [60, 70, 96], steel: [92, 96, 100]
  };
  function roomBuilder() {
    const tris = [], screens = [], S = { part: 'far', two: false, emit: 0, tag: 0 };
    const tri = (a, b, c, col) => tris.push({ v: [a, b, c], col, two: S.two, part: S.part, emit: S.emit, tag: S.tag });
    // a quad o, o+a, o+a+b, o+b, facing along a x b
    const quad = (o, a, b, col) => { const p1 = add(o, a), p2 = add(p1, b), p3 = add(o, b); tri(o, p1, p2, col); tri(o, p2, p3, col); };
    const tile = (o, a, b, na, nb, col) => {
      for (let i = 0; i < na; i++) for (let j = 0; j < nb; j++) quad(add(o, add(mul(a, i / na), mul(b, j / nb))), mul(a, 1 / na), mul(b, 1 / nb), col);
    };
    // a box from corner o along X, Y and Z (X x Y = Z); faces: 1 front (-Z), 2 back, 4 left (-X), 8 right, 16 top, 32 bottom
    const box = (o, X, Y, Z, col, faces = 63) => {
      if (faces & 1) quad(o, Y, X, col);
      if (faces & 2) quad(add(o, Z), X, Y, col);
      if (faces & 4) quad(o, Z, Y, col);
      if (faces & 8) quad(add(o, X), Y, Z, col);
      if (faces & 16) quad(add(o, Y), Z, X, col);
      if (faces & 32) quad(o, X, Z, col);
    };
    const abox = (x0, y0, z0, x1, y1, z1, col, faces = 63) => box([x0, y0, z0], [x1 - x0, 0, 0], [0, y1 - y0, 0], [0, 0, z1 - z0], col, faces);
    // a cylinder from a to b, n sides, capped at both ends if caps
    const cyl = (a, b, r, n, col, caps) => {
      const u = norm(sub(b, a)), h = Math.abs(u[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0], p1 = norm(cross(h, u)), p2 = cross(u, p1), L = sub(b, a);
      const at = (c, i) => add(c, add(mul(p1, r * Math.cos(2 * Math.PI * i / n)), mul(p2, r * Math.sin(2 * Math.PI * i / n))));
      for (let i = 0; i < n; i++) { const A = at(a, i); quad(A, sub(at(a, i + 1), A), L, col); }
      if (caps) for (let i = 0; i < n; i++) { tri(b, at(b, i), at(b, i + 1), col); tri(a, at(a, i + 1), at(a, i), col); }
    };
    // a panel o + ex u + ey v (u, v 0-1) with a hole (u0, v0)-(u1, v1) for what sits in it, so nothing lies on
    // top of anything else and the painter's sort has nothing to get wrong
    const frame = (o, ex, ey, u0, v0, u1, v1, col) => {
      const at = (uu, vv) => add(o, add(mul(ex, uu), mul(ey, vv)));
      const q = (ua, va, ub, vb) => quad(at(ua, va), mul(ex, ub - ua), mul(ey, vb - va), col);
      q(0, 0, 1, v0); q(0, v1, 1, 1); q(0, v0, u0, v1); q(u1, v0, 1, v1);
    };
    const screen = (kind, o, ex, ey, w, h, bay, col) => {
      screens.push({ kind, o, ex, ey, w, h, bay });
      const e = S.emit, g = S.tag;
      S.emit = 1; S.tag = screens.length;
      if (col) quad(o, ex, ey, col);
      S.emit = e; S.tag = g;
    };
    return { tris, screens, S, tri, quad, tile, box, abox, cyl, frame, screen };
  }

  function room() {
    const B = roomBuilder(), { S, quad, tile, abox, cyl, frame, screen } = B, P = PAINT;
    const ZW = 4.4, X0 = -4.2, X1 = 5.4, YC = 2.55;
    const shift = (c, k) => c.map(v => v + k);
    // the after bulkhead: three strakes of plates, rivet rows along their butts and seams, the welds, the frames
    for (let r = 0; r < 3; r++)
      for (let k = 0; k < 8; k++) {
        const x = X0 + k * 1.2, y = r * 0.85;
        tile([x, y + 0.85, ZW], [1.2, 0, 0], [0, -0.85, 0], 2, 2, shift(P.plate, ((k * 7 + r * 3) % 5 - 2) * 3));
        for (let i = 0; i < 6; i++) quad([x + 0.03, y + 0.1 + i * 0.13 + 0.012, ZW - 0.004], [0.024, 0, 0], [0, -0.024, 0], P.rivet);
        for (let i = 0; i < 8; i++) quad([x + 0.1 + i * 0.14, y + 0.85 - 0.028, ZW - 0.004], [0.024, 0, 0], [0, -0.024, 0], P.rivet);
      }
    for (const y of [0.85, 1.7]) for (let k = 0; k < 8; k++) quad([X0 + k * 1.2, y + 0.007, ZW - 0.002], [1.2, 0, 0], [0, -0.014, 0], P.weld);
    for (let k = 0; k <= 4; k++) for (let j = 0; j < 3; j++) abox(X0 + 0.6 + k * 2.4 - 0.04, j * 0.85, ZW - 0.14, X0 + 0.6 + k * 2.4 + 0.04, (j + 1) * 0.85, ZW, P.frame, 1 | 4 | 8);
    // the deck, and the ribbed rubber mats in front of the consoles
    tile([X0, 0, 0.2], [0, 0, ZW - 0.2], [X1 - X0, 0, 0], 7, 12, P.deck);
    for (const [m0, m1] of [[-3.9, -0.3], [0.5, 4.6]]) {
      tile([m0, 0.006, 3.0], [0, 0, 0.64], [m1 - m0, 0, 0], 1, 4, P.mat);
      for (let i = 0; i < 7; i++) quad([m0 + 0.04, 0.01, 3.04 + i * 0.09], [0, 0, 0.022], [m1 - m0 - 0.08, 0, 0], P.rib);
    }
    // the deckhead: its beams, the pipe runs with their flanges and hangers, the fire main and its valve, a cable tray,
    // the trunk of ducting, and the pipes coming down the bulkhead
    tile([X0, YC, 2.2], [X1 - X0, 0, 0], [0, 0, ZW - 2.2], 12, 3, P.deckhead);
    for (const z of [2.75, 3.6]) for (let k = 0; k < 8; k++) abox(X0 + k * 1.2, YC - 0.15, z - 0.03, X0 + (k + 1) * 1.2, YC, z + 0.03, P.frame, 1 | 2 | 32);
    const PIPES = [[2.4, 2.95, 0.06, P.pipe], [2.29, 3.2, 0.035, P.pipeGrey], [2.42, 3.4, 0.05, P.fire], [2.36, 3.85, 0.075, P.pipe], [2.28, 4.08, 0.04, P.pipeGrey]];
    for (const [y, z, r, col] of PIPES)
      for (let k = 0; k < 6; k++) {
        const x = X0 + k * 1.6;
        cyl([x, y, z], [x + 1.6, y, z], r, 8, col, false);
        cyl([x + 1.585, y, z], [x + 1.615, y, z], r * 1.4, 8, col, true);
        abox(x + 0.8 - 0.012, y + r, z - 0.012, x + 0.8 + 0.012, YC, z + 0.012, P.steel, 1 | 4 | 8);
      }
    cyl([1.2, 2.42, 3.33], [1.2, 2.42, 3.29], 0.1, 10, P.fire, true);
    cyl([1.2, 2.42, 3.4], [1.2, 2.42, 3.33], 0.012, 5, P.steel, false);
    for (let k = 0; k < 6; k++) {
      const x = X0 + k * 1.6;
      abox(x, 2.15, 3.55, x + 1.6, 2.17, 3.86, P.tray, 1 | 16 | 32);
      abox(x, 2.17, 3.55, x + 1.6, 2.22, 3.56, P.tray, 1 | 2 | 16);
      for (let i = 0; i < 4; i++) cyl([x, 2.19 + (i & 1) * 0.012, 3.6 + i * 0.065], [x + 1.6, 2.19 + (i & 1) * 0.012, 3.6 + i * 0.065], 0.016, 5, i === 2 ? P.pipeGrey : P.cable, false);
      abox(x, 2.08, 4.12, x + 1.6, 2.4, 4.38, P.duct, 1 | 16 | 32);
      abox(x + 1.56, 2.06, 4.1, x + 1.6, 2.42, 4.38, P.frame, 1 | 4 | 8 | 32);
    }
    for (const x of [-3.05, 0.75, 4.0]) for (let j = 0; j < 3; j++) cyl([x, j * 0.7, ZW - 0.06], [x, (j + 1) * 0.7, ZW - 0.06], 0.035, 8, P.pipeGrey, false);
    // the lamps: dimmed blue fixtures overhead, red battle lanterns on the bulkhead
    for (const x of [-2.4, 0.4, 3.0]) {
      abox(x - 0.35, YC - 0.07, 3.0, x + 0.35, YC, 3.16, P.frame, 1 | 2 | 4 | 8);
      S.emit = 3; quad([x - 0.33, YC - 0.071, 3.02], [0.66, 0, 0], [0, 0, 0.12], P.lensBlue); S.emit = 0;
    }
    for (const x of [-1.6, 1.95, 4.6]) {
      abox(x - 0.07, 2.02, ZW - 0.1, x + 0.07, 2.18, ZW, P.black, 1 | 4 | 8 | 16);
      S.emit = 2; quad([x - 0.05, 2.16, ZW - 0.102], [0.1, 0, 0], [0, -0.12, 0], P.lensRed); S.emit = 0;
    }
    // the console row: eleven bays, each a cabinet, a writing shelf, a sloped key panel, the display unit with its
    // CRT under a hood and its name plate; two of them are the strike-weapon consoles
    const KINDS = ['ppi', 'alpha', 'alpha', 'ppi', 'alpha', 'ppi', 'strike', 'harpoon', 'alpha', 'ppi', 'alpha'];
    KINDS.forEach((kind, i) => {
      const x = -3.9 + i * 0.8, cx = x + 0.39, weapon = kind === 'strike' || kind === 'harpoon';
      abox(x, 0, 3.82, x + 0.78, 0.76, 4.3, P.console, 4 | 8);
      tile([x, 0.76, 3.82], [0.78, 0, 0], [0, -0.76, 0], 1, 2, P.console);
      abox(x, 0.76, 3.6, x + 0.78, 0.8, 4.3, P.consoleDark, 1 | 4 | 8 | 16);
      screen(weapon ? SCREEN.guards : SCREEN.keys, [x + 0.04, 0.92, 3.88], [0.7, 0, 0], [0, -0.12, -0.24], 700, 270, i, P.consoleDark);
      abox(x, 0.8, 3.9, x + 0.78, 1.46, 4.3, P.console, 4 | 8 | 16);
      frame([x, 1.46, 3.9], [0.78, 0, 0], [0, -0.66, 0], 0.11 / 0.78, 0.01 / 0.66, 0.67 / 0.78, 0.51 / 0.66, P.console);
      frame([x + 0.11, 1.37, 3.9], [0.56, 0, 0], [0, -0.42, 0], 0.08 / 0.56, 0.04 / 0.42, 0.48 / 0.56, 0.36 / 0.42, P.bezel);
      abox(cx - 0.27, 1.33, 3.8, cx + 0.27, 1.355, 3.9, P.consoleDark, 1 | 16 | 32);
      screen(SCREEN[kind], [cx - 0.2, 1.33, 3.9], [0.4, 0, 0], [0, -0.32, 0], 400, 320, i, P.glass);
      screen(SCREEN.label, [x + 0.11, 1.45, 3.9], [0.56, 0, 0], [0, -0.08, 0], 560, 80, i, P.label);
    });
    // on the bulkhead above them: the sound-powered phone boxes with a handset on its hook and a headset hung beside,
    // the clock and the general-quarters light
    for (const i of [1, 4, 7, 10]) {
      const cx = -3.9 + i * 0.8 + 0.39;
      abox(cx - 0.08, 1.6, ZW - 0.07, cx + 0.08, 1.8, ZW, P.phone, 1 | 4 | 8 | 16);
      abox(cx + 0.09, 1.6, ZW - 0.06, cx + 0.13, 1.8, ZW - 0.02, P.black, 1 | 4 | 8 | 16);
      for (const s of [-1, 1]) cyl([cx - 0.24 + s * 0.065, 1.58, ZW - 0.05], [cx - 0.24 + s * 0.065, 1.58, ZW - 0.02], 0.035, 8, P.black, true);
      abox(cx - 0.31, 1.64, ZW - 0.04, cx - 0.17, 1.66, ZW - 0.02, P.black, 1 | 16);
    }
    cyl([0.3, 1.95, ZW - 0.05], [0.3, 1.95, ZW], 0.14, 14, P.black, false);
    S.two = false;
    screen(SCREEN.clock, [0.18, 2.07, ZW - 0.052], [0.24, 0, 0], [0, -0.24, 0], 240, 240, -1, null);
    abox(2.0, 2.14, 3.92, 2.7, 2.3, 4.0, P.black, 1 | 4 | 8 | 32);
    abox(2.33, 2.3, 3.95, 2.37, 2.4, 3.97, P.steel, 1 | 4 | 8);
    screen(SCREEN.gq, [2.02, 2.28, 3.918], [0.66, 0, 0], [0, -0.12, 0], 660, 120, -1, [30, 8, 6]);
    // the plexiglass status boards on their posts, lit from their top edge
    for (const [x0, kind] of [[0.15, SCREEN.boardL], [2.25, SCREEN.boardR]]) {
      for (const x of [x0 - 0.03, x0 + 1.95 + 0.03]) for (let j = 0; j < 3; j++) abox(x - 0.02, j * 0.85, 3.13, x + 0.02, (j + 1) * 0.85, 3.17, P.alu, 1 | 4 | 8);
      abox(x0, 1.95, 3.13, x0 + 1.95, 1.99, 3.17, P.alu, 1 | 32);
      abox(x0, 0.97, 3.13, x0 + 1.95, 1.0, 3.17, P.alu, 1 | 16);
      S.emit = 1; quad([x0, 1.95, 3.128], [1.95, 0, 0], [0, -0.008, 0], P.edge); S.emit = 0;
      S.two = true;
      screen(kind, [x0, 1.95, 3.15], [1.95, 0, 0], [0, -0.95, 0], 1950, 950, -1, [20, 26, 30]);
      S.two = false;
    }
    // ---- near: the radar operator's console, turned toward his chair ----
    S.part = 'near';
    const th = -25 * D, X = [Math.cos(th), 0, -Math.sin(th)], Y = [0, 1, 0], Z = [Math.sin(th), 0, Math.cos(th)];
    const sw = 0.57, sh = 0.376, c = [-0.557, 1.25, 1.25], L = 0.42 * sw;
    const at = (u, v, w) => add(add(add(c, mul(X, u - L - 0.13)), mul(Y, v - 1.25)), mul(Z, w));
    const lbox = (u0, v0, w0, u1, v1, w1, col, faces) => B.box(at(u0, v0, w0), mul(X, u1 - u0), mul(Y, v1 - v0), mul(Z, w1 - w0), col, faces);
    const W = sw + 0.26;
    lbox(0, 0, 0, W, 0.9, 0.55, P.console, 1 | 4 | 8);
    lbox(0, 0.9, -0.32, W, 0.94, 0.55, P.consoleDark, 1 | 4 | 8 | 16);
    lbox(0.1, 0.94, -0.28, 0.58, 0.968, -0.05, P.black, 1 | 4 | 8);
    screen(SCREEN.keyboard, at(0.1, 0.968, -0.05), mul(X, 0.48), mul(Z, -0.23), 480, 230, -1, P.black);
    lbox(0.61, 0.94, -0.25, 0.71, 0.965, -0.1, P.black, 1 | 4 | 8 | 16);
    cyl(at(0.66, 0.965, -0.175), at(0.66, 0.985, -0.175), 0.032, 10, [120, 132, 138], true);
    lbox(0, 0.94, 0.04, W, 1.0, 0.55, P.consoleDark, 1 | 4 | 8);
    lbox(0, 1.0, 0, W, 1.62, 0.55, P.console, 4 | 8 | 16);
    const bt = 1.25 + sh / 2 + 0.035, bh = sh + 0.07, bw = sw + 0.06;
    frame(at(0, 1.62, 0), mul(X, W), mul(Y, -0.62), 0.1 / W, (1.62 - bt) / 0.62, (0.1 + bw) / W, (1.62 - bt + bh) / 0.62, P.console);
    frame(at(0.1, bt, 0), mul(X, bw), mul(Y, -bh), 0.03 / bw, 0.035 / bh, (0.03 + sw) / bw, (0.035 + sh) / bh, P.bezel);
    lbox(0.08, bt + 0.01, -0.12, W - 0.08, bt + 0.035, 0, P.consoleDark, 1 | 16 | 32);
    screen(SCREEN.scope, at(0.13, 1.25 + sh / 2, 0), mul(X, sw), mul(Y, -sh), 500, 330, -1, P.glass);
    // the sound-powered phone's handset hung on the console's side, its cord's jack box, and a mug of coffee
    lbox(-0.05, 0.98, 0.08, -0.005, 1.2, 0.14, P.black, 1 | 4 | 16 | 32);
    cyl(at(-0.03, 1.2, 0.11), at(-0.03, 1.23, 0.11), 0.03, 8, P.black, true);
    cyl(at(-0.03, 0.95, 0.11), at(-0.03, 0.98, 0.11), 0.028, 8, P.black, true);
    const m = at(0.78, 0.94, -0.17);
    cyl(m, add(m, [0, 0.095, 0]), 0.04, 10, P.mug, false);
    for (let i = 0; i < 10; i++) {
      const a0 = 2 * Math.PI * i / 10, a1 = 2 * Math.PI * (i + 1) / 10, top = add(m, [0, 0.088, 0]);
      B.tri(top, add(top, [0.036 * Math.cos(a0), 0, -0.036 * Math.sin(a0)]), add(top, [0.036 * Math.cos(a1), 0, -0.036 * Math.sin(a1)]), P.coffee);
    }
    screen(SCREEN.steam, add(m, [0, 0.1, 0]), [0.04, 0, 0], [0, -0.04, 0], 40, 40, -1, null);
    return { tris: B.tris, screens: B.screens };
  }

  // a bolted swivel chair: its seat's centre at seat, turned by yaw (degrees, as a figure's); 'swivel' a low back on a
  // pedestal bolted to the deck, 'high' the watch officer's tall chair with arms and a foot ring
  function chair(kind, seat, yaw) {
    const B = roomBuilder(), P = PAINT, R = ry(yaw * D), high = kind === 'high';
    B.S.part = 'near';
    const at = p => add(mv(R, p), seat), Xv = mv(R, [1, 0, 0]), Yv = [0, 1, 0], Zv = mv(R, [0, 0, 1]);
    const lbox = (x0, y0, z0, x1, y1, z1, col) => B.box(at([x0, y0, z0]), mul(Xv, x1 - x0), mul(Yv, y1 - y0), mul(Zv, z1 - z0), col, 63);
    const foot = [seat[0], 0, seat[2]];
    B.cyl(foot, [seat[0], 0.02, seat[2]], 0.17, 12, P.steel, true);
    for (let i = 0; i < 4; i++) { const a = (45 + 90 * i) * D, b = add(foot, [0.13 * Math.cos(a), 0, 0.13 * Math.sin(a)]); B.cyl(b, add(b, [0, 0.032, 0]), 0.014, 6, P.rivet, true); }
    B.cyl([seat[0], 0.02, seat[2]], [seat[0], seat[1] - 0.07, seat[2]], 0.035, 8, P.steel, false);
    lbox(-0.24, -0.07, -0.22, 0.24, 0, 0.24, P.vinyl);
    lbox(-0.03, -0.05, -0.28, 0.03, high ? 0.3 : 0.2, -0.24, P.steel);
    lbox(high ? -0.22 : -0.17, high ? 0.12 : 0.12, -0.31, high ? 0.22 : 0.17, high ? 0.62 : 0.4, -0.25, P.vinyl);
    if (high) {
      for (const s of [-1, 1]) { lbox(s * 0.25 - 0.025, 0.17, -0.2, s * 0.25 + 0.025, 0.2, 0.16, P.vinyl); lbox(s * 0.25 - 0.012, 0, -0.08, s * 0.25 + 0.012, 0.17, -0.05, P.steel); }
      const ry0 = 0.38 - seat[1];
      for (let i = 0; i < 8; i++) {
        const a0 = 2 * Math.PI * i / 8, a1 = 2 * Math.PI * (i + 1) / 8;
        B.cyl(at([0.2 * Math.cos(a0), ry0, 0.2 * Math.sin(a0)]), at([0.2 * Math.cos(a1), ry0, 0.2 * Math.sin(a1)]), 0.01, 5, P.steel, false);
      }
    }
    return B.tris;
  }

  // the room lit: each triangle's paint times the ambient light plus every lamp's (a half-wrapped diffuse light that
  // falls off with distance); the lamps' own lenses at the brightness given. L: { amb: [r, g, b], red, blue,
  // lamps: [{ p, c: [r, g, b], r }] }
  function lightRoom(tris, L) {
    return tris.map(t => {
      let col;
      if (t.emit === 1) col = t.col;
      else if (t.emit) col = mul(t.col, t.emit === 2 ? L.red : L.blue);
      else {
        const n = norm(cross(sub(t.v[1], t.v[0]), sub(t.v[2], t.v[0])));
        const c = mul(add(add(t.v[0], t.v[1]), t.v[2]), 1 / 3), k = L.amb.slice();
        for (const lp of L.lamps) {
          const d = sub(lp.p, c), dl = len(d), w = 1 / (1 + (dl / lp.r) * (dl / lp.r));
          let nd = dot(n, d) / (dl || 1);
          if (t.two) nd = Math.abs(nd);
          const f = (0.3 + 0.7 * Math.max(0, nd)) * w;
          for (let i = 0; i < 3; i++) k[i] += lp.c[i] * f;
        }
        col = t.col.map((v, i) => v * k[i]);
      }
      return { v: t.v, col: col.map(v => Math.min(255, v)), two: t.two, part: t.part, tag: t.tag, lit: true };
    });
  }

  // the room's lamps: the dimmed blue overheads, the red battle lanterns (brighter and pulsing at general quarters,
  // gq 0-1), the glow of every screen and the status boards' edge light
  const GLOW = { 1: [[0.08, 0.4, 0.22], 0.8], 2: [[0.05, 0.35, 0.16], 0.7], 3: [[0.06, 0.32, 0.18], 0.7], 4: [[0.4, 0.25, 0.05], 0.7], 5: [[0.4, 0.25, 0.05], 0.7],
    6: [[0.35, 0.28, 0.12], 1.2], 7: [[0.35, 0.28, 0.12], 1.2] };
  function roomLamps(screens, alert, gq) {
    const red = alert ? 0.55 + 0.45 * gq : 0.22, blue = alert ? 0.7 : 1;
    const L = { amb: alert ? [0.07 + 0.06 * gq, 0.045, 0.07] : [0.045, 0.065, 0.11], red, blue, lamps: [] };
    for (const x of [-2.4, 0.4, 3.0]) L.lamps.push({ p: [x, 2.4, 3.08], c: [0.2 * blue, 0.3 * blue, 0.6 * blue], r: 1.4 });
    for (const x of [-1.6, 1.95, 4.6]) L.lamps.push({ p: [x, 2.1, 4.2], c: [0.8 * red, 0.1 * red, 0.07 * red], r: 1.0 });
    for (const s of screens) {
      const g = GLOW[s.kind];
      if (!g) continue;
      const n = norm(cross(s.ex, s.ey));
      L.lamps.push({ p: add(add(s.o, mul(add(s.ex, s.ey), 0.5)), mul(n, 0.25)), c: g[0], r: g[1] });
    }
    return L;
  }

  return { VITRUVIAN, PALM, skeleton, solve, build, draw, measureMesh, vitruvianPose, POSES, SEAT, CREW, colour, speechTrack, mouthAt, wordsShown, spokenUpTo, words, toPhonemes, FACE_DEFAULT, landmarks, SCREEN, room, chair, lightRoom, roomLamps, raster };
})();
if (typeof module !== 'undefined') module.exports = Figure;
