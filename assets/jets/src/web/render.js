// Minimal .glb reader + flat-shaded canvas renderer (painter's algorithm), the same
// approach the game uses. Reads only what the jet models contain: POSITION/NORMAL,
// non-indexed triangles, baseColorFactor materials, node translations.
const GLB = {
  parse(buf) {
    const dv = new DataView(buf);
    if (dv.getUint32(0, true) !== 0x46546C67) throw new Error('not a glb');
    const jl = dv.getUint32(12, true);
    const json = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 20, jl)));
    const binOff = 20 + jl + 8;
    const acc = i => {
      const a = json.accessors[i], v = json.bufferViews[a.bufferView];
      return new Float32Array(buf, binOff + (v.byteOffset || 0) + (a.byteOffset || 0), a.count * 3);
    };
    const toSRGB = c => c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
    const mats = json.materials.map(m => {
      const f = m.pbrMetallicRoughness.baseColorFactor;
      return { name: m.name, rgb: [toSRGB(f[0]) * 255, toSRGB(f[1]) * 255, toSRGB(f[2]) * 255], a: f[3] };
    });
    const root = json.nodes[json.scenes[0].nodes[0]];
    const parts = root.children.map(ci => {
      const n = json.nodes[ci], mesh = json.meshes[n.mesh];
      return {
        name: n.name, t: n.translation || [0, 0, 0],
        prims: mesh.primitives.map(p => ({ pos: acc(p.attributes.POSITION), nor: acc(p.attributes.NORMAL), mat: mats[p.material] }))
      };
    });
    return { name: root.name, extras: root.extras, parts, mats };
  }
};

const Render = {
  // model: parsed glb. opts: {yaw, pitch, dist, sweep (deg extra, nf24), hide:{stores}, bg}
  draw(ctx, w, h, model, o) {
    const cy = Math.cos(o.yaw), sy = Math.sin(o.yaw), cp = Math.cos(o.pitch), sp = -Math.sin(o.pitch);
    const F = Math.min(w, h) * 1.25, cx0 = w / 2, cy0 = h / 2 + (o.dy || 0);
    const L = [0.35, 0.85, 0.4], Ln = Math.hypot(...L);
    const light = L.map(v => v / Ln);
    const tris = [];
    // the main model plus optional extra instances: {model, pos:[x,y,z], ry (rad about Y), sweep, hide}
    const inst = [{ model, pos: [0, 0, 0], ry: 0, sweep: o.sweep, hide: o.hide }, ...(o.extra || [])];
    for (const it of inst) {
    const irs = Math.sin(it.ry || 0), irc = Math.cos(it.ry || 0), [ix, iy, iz] = it.pos;
    for (const part of it.model.parts) {
      if (it.hide && it.hide[part.name]) continue;
      let rs = 0, rc = 1;
      if (part.name === 'wing_L' || part.name === 'wing_R') {
        const a = (it.sweep || 0) * Math.PI / 180 * (part.name === 'wing_L' ? 1 : -1);
        rs = Math.sin(a); rc = Math.cos(a);
      }
      const [tx, ty, tz] = part.t;
      for (const pr of part.prims) {
        const P = pr.pos, N = pr.nor, m = pr.mat;
        for (let i = 0; i < P.length; i += 9) {
          const pts = [];
          let nx = N[i], ny = N[i + 1], nz = N[i + 2];
          if (rs) { const x = nx * rc + nz * rs, z = -nx * rs + nz * rc; nx = x; nz = z; }
          if (irs) { const x = nx * irc + nz * irs, z = -nx * irs + nz * irc; nx = x; nz = z; }
          // world -> camera (orbit: yaw about Y, then pitch about X)
          const vx = nx * cy - nz * sy, vz0 = nx * sy + nz * cy;
          const vy = ny * cp - vz0 * sp, vz = ny * sp + vz0 * cp;
          let zs = 0, ok = true, cpos = [];
          for (let k = 0; k < 3; k++) {
            let x = P[i + k * 3], y = P[i + k * 3 + 1], z = P[i + k * 3 + 2];
            if (rs) { const xx = x * rc + z * rs, zz = -x * rs + z * rc; x = xx; z = zz; }
            x += tx; y += ty; z += tz;
            if (irs) { const xx = x * irc + z * irs, zz = -x * irs + z * irc; x = xx; z = zz; }
            x += ix - (o.tx || 0); y += iy - (o.ty || 0); z += iz - (o.tz || 0);
            const X = x * cy - z * sy, Z0 = x * sy + z * cy;
            const Y = y * cp - Z0 * sp, Z = y * sp + Z0 * cp + o.dist;
            if (Z < 0.5) { ok = false; break; }
            cpos.push([X, Y, Z]); zs += Z;
            pts.push(cx0 - X / Z * F, cy0 - Y / Z * F);
          }
          if (!ok) continue;
          // back-face cull in view space (normal facing away from the eye)
          const c0 = cpos[0];
          const facing = vx * c0[0] + vy * c0[1] + vz * c0[2];
          if (facing > 0 && m.a >= 1) continue;
          const wn = [nx, ny, nz];
          const lam = Math.max(0, wn[0] * light[0] + wn[1] * light[1] + wn[2] * light[2]);
          const s = 0.42 + 0.68 * lam + (wn[1] < 0 ? 0.08 : 0);
          tris.push({ z: zs / 3 + (o.under && o.under.includes(part.name) && !it.model.isExtra ? 1e6 : 0), pts, c: m.rgb, a: m.a, s });
        }
      }
    }
    }
    tris.sort((a, b) => b.z - a.z);
    ctx.lineJoin = 'round';
    for (const t of tris) {
      const col = `rgba(${Math.min(255, t.c[0] * t.s) | 0},${Math.min(255, t.c[1] * t.s) | 0},${Math.min(255, t.c[2] * t.s) | 0},${t.a})`;
      ctx.beginPath(); ctx.moveTo(t.pts[0], t.pts[1]); ctx.lineTo(t.pts[2], t.pts[3]); ctx.lineTo(t.pts[4], t.pts[5]); ctx.closePath();
      ctx.fillStyle = col; ctx.fill();
      if (t.a >= 1) { ctx.strokeStyle = col; ctx.lineWidth = 0.6; ctx.stroke(); }
    }
    return tris.length;
  }
};
