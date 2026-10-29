// Instrument library: analog dials, attitude indicators, HSI, tapes, MFD pages, HUDs.
// All angles for dials are degrees clockwise from 12 o'clock.
const R2D = 180 / Math.PI, D2R = Math.PI / 180;
const FONT = '"B612", "Arial Narrow", Arial, sans-serif', MONO = '"B612 Mono", Consolas, monospace';
const lerpTab = (tab, v) => { // tab: [[value, out], ...] ascending
  if (v <= tab[0][0]) return tab[0][1];
  for (let i = 1; i < tab.length; i++) if (v <= tab[i][0]) { const [a, A] = tab[i - 1], [b, B] = tab[i]; return A + (B - A) * (v - a) / (b - a); }
  return tab[tab.length - 1][1];
};
const polar = (x, y, r, deg) => [x + Math.sin(deg * D2R) * r, y - Math.cos(deg * D2R) * r];

const I = {
  f(px, w = '', fam = FONT) { return `${w} ${px}px ${fam}`; },
  text(c, s, x, y, px, col, align = 'center', w = '', fam = FONT, base = 'middle') {
    c.font = I.f(px, w, fam); c.fillStyle = col; c.textAlign = align; c.textBaseline = base; c.fillText(s, x, y);
  },
  // round instrument case
  bezel(c, x, y, r, o = {}) {
    const g = c.createRadialGradient(x - r * 0.3, y - r * 0.4, r * 0.2, x, y, r * 1.18);
    g.addColorStop(0, o.rim1 || '#4a4d50'); g.addColorStop(1, o.rim2 || '#141516');
    c.beginPath(); c.arc(x, y, r * 1.14, 0, 7); c.fillStyle = g; c.fill();
    if (o.square) { c.fillStyle = o.rim2 || '#141516'; c.fillRect(x - r * 1.2, y - r * 1.2, r * 2.4, r * 2.4); c.beginPath(); c.arc(x, y, r * 1.14, 0, 7); c.fillStyle = g; c.fill(); }
    c.beginPath(); c.arc(x, y, r, 0, 7); c.fillStyle = o.face || '#0c0d0e'; c.fill();
    if (o.screws !== false) for (const a of [45, 135, 225, 315]) {
      const [sx, sy] = polar(x, y, r * 1.28, a); c.beginPath(); c.arc(sx, sy, Math.max(1.5, r * 0.05), 0, 7); c.fillStyle = '#2b2d2f'; c.fill();
      c.strokeStyle = '#111'; c.lineWidth = 1; c.beginPath(); c.moveTo(sx - r * 0.035, sy); c.lineTo(sx + r * 0.035, sy); c.stroke();
    }
  },
  glass(c, x, y, r) {
    const g = c.createLinearGradient(x - r, y - r, x + r * 0.2, y + r * 0.2);
    g.addColorStop(0, 'rgba(255,255,255,0.10)'); g.addColorStop(0.45, 'rgba(255,255,255,0.02)'); g.addColorStop(1, 'rgba(255,255,255,0)');
    c.beginPath(); c.arc(x, y, r, 0, 7); c.fillStyle = g; c.fill();
  },
  needle(c, x, y, len, deg, o = {}) {
    c.save(); c.translate(x, y); c.rotate(deg * D2R);
    const w = o.w || len * 0.06, tail = o.tail ?? len * 0.2;
    c.beginPath(); c.moveTo(-w / 2, tail); c.lineTo(-w / 2, -len * 0.8); c.lineTo(0, -len); c.lineTo(w / 2, -len * 0.8); c.lineTo(w / 2, tail); c.closePath();
    c.fillStyle = o.color || '#f2f2ea'; c.shadowColor = 'rgba(0,0,0,0.6)'; c.shadowBlur = 3; c.shadowOffsetY = 2; c.fill();
    c.restore();
    if (o.hub !== false) { c.beginPath(); c.arc(x, y, Math.max(2.5, len * 0.07), 0, 7); c.fillStyle = '#222'; c.fill(); c.strokeStyle = '#555'; c.lineWidth = 1; c.stroke(); }
  },
  // generic dial. s: {min,max,a0,a1,map(v)->0..1,major,minor,labels:[v],fmt,ink,face,title,units,arcs:[{from,to,color}],needles:[{v,color,len,w}]}
  dial(c, x, y, r, s) {
    I.bezel(c, x, y, r, s);
    const ink = s.ink || '#ecebe4';
    const ang = v => s.a0 + (s.a1 - s.a0) * (s.map ? s.map(v) : (v - s.min) / (s.max - s.min));
    for (const ar of s.arcs || []) {
      c.beginPath(); c.strokeStyle = ar.color; c.lineWidth = r * 0.07;
      c.arc(x, y, r * 0.86, (ang(ar.from) - 90) * D2R, (ang(ar.to) - 90) * D2R); c.stroke();
    }
    c.strokeStyle = ink; c.lineCap = 'butt';
    if (s.minor) for (let v = s.min; v <= s.max + 1e-9; v += s.minor) {
      const a = ang(v), [x1, y1] = polar(x, y, r * 0.92, a), [x2, y2] = polar(x, y, r * 0.82, a);
      c.lineWidth = Math.max(1, r * 0.018); c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke();
    }
    if (s.major) for (let v = s.min; v <= s.max + 1e-9; v += s.major) {
      const a = ang(v), [x1, y1] = polar(x, y, r * 0.93, a), [x2, y2] = polar(x, y, r * 0.74, a);
      c.lineWidth = Math.max(1.5, r * 0.035); c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke();
    }
    for (const v of s.labels || []) {
      const [lx, ly] = polar(x, y, r * (s.labR || 0.58), ang(v));
      I.text(c, s.fmt ? s.fmt(v) : String(v), lx, ly, r * (s.labPx || 0.2), ink, 'center', 'bold');
    }
    if (s.title) I.text(c, s.title, x, y + r * (s.titleY ?? -0.3), r * (s.titlePx || 0.14), s.titleColor || ink, 'center', 'bold');
    if (s.units) I.text(c, s.units, x, y + r * (s.unitsY ?? 0.32), r * 0.12, ink);
    if (s.extra) s.extra(c, x, y, r, ang);
    for (const n of s.needles || []) I.needle(c, x, y, r * (n.len || 0.88), ang(clamp(n.v, s.min - (s.over || 0), s.max + (s.over || 0))), { color: n.color, w: n.w ? r * n.w : undefined, hub: n.hub, tail: n.tail });
    I.glass(c, x, y, r);
  },
  // digital window (drum counters or LCD)
  win(c, x, y, w, h, txt, o = {}) {
    c.fillStyle = o.bg || '#050505'; c.fillRect(x, y, w, h);
    c.strokeStyle = o.frame || '#3a3a3a'; c.lineWidth = 1; c.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
    I.text(c, txt, x + (o.align === 'right' ? w - 3 : w / 2), y + h / 2 + 1, o.px || h * 0.72, o.color || '#f0efe6', o.align || 'center', o.weight || 'bold', o.fam || MONO);
  },
  // attitude indicator. o: {soviet, sky, ground, ink, slip}
  adi(c, x, y, r, pitch, roll, o = {}) {
    I.bezel(c, x, y, r, o);
    const ppd = r / 45; // px per degree of pitch
    c.save(); c.beginPath(); c.arc(x, y, r * 0.98, 0, 7); c.clip();
    c.translate(x, y);
    const ballRoll = o.soviet ? 0 : -roll;
    c.rotate(ballRoll * D2R);
    const hy = pitch * ppd;
    c.fillStyle = o.sky || '#3d78b4'; c.fillRect(-r * 2, -r * 3 + hy, r * 4, r * 3);
    c.fillStyle = o.ground || '#6b4a2a'; c.fillRect(-r * 2, hy, r * 4, r * 3);
    c.strokeStyle = '#fff'; c.lineWidth = 2; c.beginPath(); c.moveTo(-r * 2, hy); c.lineTo(r * 2, hy); c.stroke();
    c.lineWidth = 1.2;
    for (let p = -90; p <= 90; p += 5) {
      if (!p) continue;
      const yy = hy - p * ppd; if (Math.abs(yy) > r * 1.2) continue;
      const w = p % 10 ? r * 0.12 : r * 0.28;
      c.strokeStyle = p > 0 ? '#fff' : '#f4f4f4'; c.beginPath(); c.moveTo(-w, yy); c.lineTo(w, yy); c.stroke();
      if (!(p % 10)) { I.text(c, String(Math.abs(p)), -w - r * 0.1, yy, r * 0.11, '#fff'); I.text(c, String(Math.abs(p)), w + r * 0.1, yy, r * 0.11, '#fff'); }
    }
    c.restore();
    // bank scale
    c.save(); c.translate(x, y);
    const bankRot = o.soviet ? 0 : 0;
    c.strokeStyle = '#fff'; c.lineWidth = 2;
    const bs = o.soviet ? 180 : 0; // soviet: bank scale at the bottom
    for (const b of [-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60]) {
      const a = b + bs + bankRot, [x1, y1] = polar(0, 0, r * 0.97, a), [x2, y2] = polar(0, 0, r * (Math.abs(b) % 30 === 0 ? 0.84 : 0.9), a);
      c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke();
    }
    if (!o.soviet) { // sky pointer moves with the ball
      c.rotate(-roll * D2R); c.fillStyle = '#fff'; c.beginPath(); c.moveTo(0, -r * 0.84); c.lineTo(-r * 0.06, -r * 0.72); c.lineTo(r * 0.06, -r * 0.72); c.closePath(); c.fill();
    }
    c.restore();
    // aircraft symbol
    c.save(); c.translate(x, y);
    if (o.soviet) c.rotate(roll * D2R);
    c.strokeStyle = o.symbol || '#ffcf3a'; c.fillStyle = o.symbol || '#ffcf3a'; c.lineWidth = r * 0.045; c.lineCap = 'round';
    if (o.soviet) { // silhouette: wings, fin, centre dot
      c.beginPath(); c.moveTo(-r * 0.62, 0); c.lineTo(-r * 0.12, 0); c.moveTo(r * 0.12, 0); c.lineTo(r * 0.62, 0); c.moveTo(0, -r * 0.08); c.lineTo(0, -r * 0.22); c.stroke();
      c.beginPath(); c.arc(0, 0, r * 0.06, 0, 7); c.fill();
      // bank pointer on the symbol, reads against the fixed scale at the bottom
      c.beginPath(); c.moveTo(0, r * 0.72); c.lineTo(-r * 0.05, r * 0.82); c.lineTo(r * 0.05, r * 0.82); c.closePath(); c.fill();
    } else {
      c.beginPath(); c.moveTo(-r * 0.55, 0); c.lineTo(-r * 0.18, 0); c.lineTo(-r * 0.1, r * 0.08); c.moveTo(r * 0.55, 0); c.lineTo(r * 0.18, 0); c.lineTo(r * 0.1, r * 0.08); c.stroke();
      c.beginPath(); c.arc(0, 0, r * 0.035, 0, 7); c.fill();
    }
    c.restore();
    // slip ball
    if (o.slip !== false) {
      const sy = y + r * 0.83, sb = clamp(o.slipv || 0, -1, 1);
      c.fillStyle = 'rgba(0,0,0,0.6)'; c.fillRect(x - r * 0.3, sy - r * 0.07, r * 0.6, r * 0.14);
      c.strokeStyle = '#ddd'; c.lineWidth = 1; c.beginPath(); c.moveTo(x - r * 0.08, sy - r * 0.08); c.lineTo(x - r * 0.08, sy + r * 0.08); c.moveTo(x + r * 0.08, sy - r * 0.08); c.lineTo(x + r * 0.08, sy + r * 0.08); c.stroke();
      c.beginPath(); c.arc(x + sb * r * 0.22, sy, r * 0.06, 0, 7); c.fillStyle = '#111'; c.fill(); c.strokeStyle = '#999'; c.stroke();
    }
    if (o.title) I.text(c, o.title, x, y - r * 1.07, r * 0.1, o.titleColor || '#ddd', 'center', 'bold');
    I.glass(c, x, y, r);
  },
  // horizontal situation indicator. o: {course, bug, dist, cyr, ink, title, crsDev}
  hsi(c, x, y, r, hdg, o = {}) {
    I.bezel(c, x, y, r, o);
    const ink = o.ink || '#eee';
    c.save(); c.translate(x, y); c.rotate(-hdg * D2R);
    const card = o.cyr ? { 0: 'С', 9: 'В', 18: 'Ю', 27: 'З' } : { 0: 'N', 9: 'E', 18: 'S', 27: 'W' };
    for (let d = 0; d < 360; d += 5) {
      const [x1, y1] = polar(0, 0, r * 0.95, d), [x2, y2] = polar(0, 0, r * (d % 10 ? 0.89 : 0.84), d);
      c.strokeStyle = ink; c.lineWidth = d % 30 ? 1 : 2; c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke();
      if (d % 30 === 0) {
        const [lx, ly] = polar(0, 0, r * 0.7, d);
        c.save(); c.translate(lx, ly); c.rotate(d * D2R);
        I.text(c, card[d / 10] || String(d / 10), 0, 0, r * (card[d / 10] ? 0.17 : 0.14), card[d / 10] ? (o.cardinal || ink) : ink, 'center', 'bold');
        c.restore();
      }
    }
    // heading bug
    if (o.bug != null) { c.save(); c.rotate(o.bug * D2R); c.fillStyle = o.bugColor || '#ff9d2a'; c.fillRect(-r * 0.07, -r * 0.99, r * 0.14, r * 0.08); c.restore(); }
    // course arrow + deviation bar
    if (o.course != null) {
      c.save(); c.rotate(o.course * D2R); const col = o.crsColor || '#f5e04a';
      c.strokeStyle = col; c.fillStyle = col; c.lineWidth = r * 0.04;
      c.beginPath(); c.moveTo(0, -r * 0.62); c.lineTo(0, -r * 0.3); c.moveTo(0, r * 0.3); c.lineTo(0, r * 0.62); c.stroke();
      c.beginPath(); c.moveTo(0, -r * 0.66); c.lineTo(-r * 0.07, -r * 0.52); c.lineTo(r * 0.07, -r * 0.52); c.closePath(); c.fill();
      for (const k of [-2, -1, 1, 2]) { c.beginPath(); c.arc(k * r * 0.13, 0, r * 0.025, 0, 7); c.strokeStyle = ink; c.lineWidth = 1.2; c.stroke(); }
      const dev = clamp(o.crsDev || 0, -2.2, 2.2) * r * 0.13;
      c.strokeStyle = col; c.lineWidth = r * 0.04; c.beginPath(); c.moveTo(dev, -r * 0.27); c.lineTo(dev, r * 0.27); c.stroke();
      c.restore();
    }
    c.restore();
    // lubber line + fixed aircraft
    c.fillStyle = o.lubber || '#fff'; c.beginPath(); c.moveTo(x, y - r * 0.98); c.lineTo(x - r * 0.05, y - r * 1.1); c.lineTo(x + r * 0.05, y - r * 1.1); c.closePath(); c.fill();
    c.strokeStyle = o.lubber || '#fff'; c.lineWidth = r * 0.035;
    c.beginPath(); c.moveTo(x - r * 0.18, y); c.lineTo(x + r * 0.18, y); c.moveTo(x, y - r * 0.12); c.lineTo(x, y + r * 0.2); c.moveTo(x - r * 0.08, y + r * 0.17); c.lineTo(x + r * 0.08, y + r * 0.17); c.stroke();
    if (o.dist != null) { I.win(c, x - r * 0.98, y - r * 1.02, r * 0.5, r * 0.22, o.dist.toFixed(0).padStart(3, '0'), { px: r * 0.16, color: ink }); I.text(c, o.cyr ? 'Д км' : 'NM', x - r * 0.73, y - r * 0.68, r * 0.1, ink); }
    if (o.course != null) { I.win(c, x + r * 0.48, y - r * 1.02, r * 0.5, r * 0.22, String(Math.round(o.course)).padStart(3, '0'), { px: r * 0.16, color: ink }); I.text(c, o.cyr ? 'ЗПУ' : 'CRS', x + r * 0.73, y - r * 0.68, r * 0.1, ink); }
    if (o.title) I.text(c, o.title, x, y + r * 1.08, r * 0.1, o.titleColor || '#ddd', 'center', 'bold');
    I.glass(c, x, y, r);
  },
  // vertical tape (glass displays & HUDs). o: {step, every, fmt, color, box, side:'l'|'r', ppu}
  vtape(c, x, y, w, h, v, o) {
    c.save(); c.beginPath(); c.rect(x, y, w, h); c.clip();
    if (o.bg) { c.fillStyle = o.bg; c.fillRect(x, y, w, h); }
    const mid = y + h / 2, col = o.color || '#fff';
    c.strokeStyle = col; c.lineWidth = 1.5;
    const first = Math.floor((v - h / 2 / o.ppu) / o.step) * o.step;
    for (let t = first; t <= v + h / 2 / o.ppu; t += o.step) {
      const yy = mid - (t - v) * o.ppu, maj = Math.abs(t / o.every - Math.round(t / o.every)) < 1e-6;
      const tx = o.side === 'l' ? x + w : x;
      c.beginPath(); c.moveTo(tx, yy); c.lineTo(tx + (o.side === 'l' ? -1 : 1) * (maj ? w * 0.25 : w * 0.13), yy); c.stroke();
      if (maj && (o.min == null || t >= o.min)) I.text(c, o.fmt ? o.fmt(t) : String(t), o.side === 'l' ? x + w * 0.66 : x + w * 0.34, yy, o.px || 13, col, o.side === 'l' ? 'right' : 'left', '', o.fam || FONT);
    }
    c.restore();
    if (o.box !== false) {
      const bw = w * 0.92, bh = (o.px || 13) * 1.8, bx = o.side === 'l' ? x : x + w - bw;
      c.fillStyle = o.boxBg || '#000'; c.strokeStyle = col; c.lineWidth = 1.5;
      c.fillRect(bx, mid - bh / 2, bw, bh); c.strokeRect(bx, mid - bh / 2, bw, bh);
      I.text(c, o.boxFmt ? o.boxFmt(v) : String(Math.round(v)), bx + bw / 2, mid + 1, (o.px || 13) * 1.2, col, 'center', 'bold', o.fam || MONO);
    }
  },
  // heading tape
  htape(c, x, y, w, h, hdg, o = {}) {
    c.save(); c.beginPath(); c.rect(x, y, w, h); c.clip();
    if (o.bg) { c.fillStyle = o.bg; c.fillRect(x, y, w, h); }
    const col = o.color || '#fff', ppd = o.ppd || 4, cx = x + w / 2;
    c.strokeStyle = col; c.lineWidth = 1.5;
    for (let d = Math.floor((hdg - w / 2 / ppd) / 5) * 5; d <= hdg + w / 2 / ppd; d += 5) {
      const xx = cx + (d - hdg) * ppd, maj = d % 10 === 0;
      c.beginPath(); c.moveTo(xx, y + h); c.lineTo(xx, y + h - (maj ? h * 0.35 : h * 0.2)); c.stroke();
      if (maj) I.text(c, String(wrap360(d) / 10 | 0).padStart(2, '0'), xx, y + h * 0.35, o.px || 12, col, 'center', '', o.fam || FONT);
    }
    c.restore();
    c.fillStyle = col; c.beginPath(); c.moveTo(cx, y + h); c.lineTo(cx - 5, y + h + 7); c.lineTo(cx + 5, y + h + 7); c.closePath(); c.fill();
    if (o.box) { c.fillStyle = '#000'; c.fillRect(cx - 22, y - 18, 44, 17); c.strokeStyle = col; c.strokeRect(cx - 22, y - 18, 44, 17); I.text(c, String(Math.round(hdg) % 360).padStart(3, '0'), cx, y - 9, 13, col, 'center', 'bold', MONO); }
  },
  // annunciator light
  light(c, x, y, w, h, label, on, color = '#ffb020', o = {}) {
    c.fillStyle = on ? color : (o.off || '#1b1c1d'); c.fillRect(x, y, w, h);
    c.strokeStyle = '#000'; c.lineWidth = 1.5; c.strokeRect(x, y, w, h);
    if (on) { c.save(); c.shadowColor = color; c.shadowBlur = 10; c.fillStyle = color; c.globalAlpha = 0.25; c.fillRect(x - 2, y - 2, w + 4, h + 4); c.restore(); }
    const lines = String(label).split('\n');
    lines.forEach((ln, i) => I.text(c, ln, x + w / 2, y + h / 2 + (i - (lines.length - 1) / 2) * (o.px || 10) * 1.1, o.px || 10, on ? '#111' : (o.dim || '#4a4a48'), 'center', 'bold'));
  },
  // MFD bezel with option-select buttons; returns inner screen rect
  mfd(c, x, y, w, h, o = {}) {
    const b = o.bez ?? Math.round(Math.min(w, h) * 0.11);
    c.fillStyle = o.frame || '#232527'; c.fillRect(x, y, w, h);
    c.strokeStyle = '#0c0c0c'; c.lineWidth = 2; c.strokeRect(x, y, w, h);
    const sx = x + b, sy = y + b, sw = w - 2 * b, sh = h - 2 * b;
    c.fillStyle = o.screen || '#020604'; c.fillRect(sx, sy, sw, sh);
    const nb = o.buttons ?? 5, lab = o.labels || {};
    const btn = [];
    if (nb) for (let i = 0; i < nb; i++) {
      const f = (i + 0.5) / nb;
      const pos = [[sx + sw * f, y + b * 0.5, 'T'], [sx + sw * f, y + h - b * 0.5, 'B'], [x + b * 0.5, sy + sh * f, 'L'], [x + w - b * 0.5, sy + sh * f, 'R']];
      for (const [bx, by, side] of pos) {
        c.fillStyle = '#3a3c3f'; c.fillRect(bx - b * 0.3, by - b * 0.26, b * 0.6, b * 0.52);
        c.strokeStyle = '#111'; c.lineWidth = 1; c.strokeRect(bx - b * 0.3, by - b * 0.26, b * 0.6, b * 0.52);
        const t = lab[side + i];
        if (t) btn.push(() => {
          const tx = side === 'L' ? sx + 4 : side === 'R' ? sx + sw - 4 : bx, ty = side === 'T' ? sy + 9 : side === 'B' ? sy + sh - 9 : by;
          const al = side === 'L' ? 'left' : side === 'R' ? 'right' : 'center';
          const sel = o.sel === side + i;
          c.font = I.f(o.px || 10, 'bold', MONO); const tw = c.measureText(t).width;
          if (sel) { c.fillStyle = o.ink || '#4f4'; c.fillRect(al === 'left' ? tx - 1 : al === 'right' ? tx - tw - 1 : tx - tw / 2 - 1, ty - 6, tw + 2, 12); }
          I.text(c, t, tx, ty, o.px || 10, sel ? '#000' : (o.ink || '#4f4'), al, 'bold', MONO);
        });
      }
    }
    // soft-key legends are drawn after the page content via s.labels()
    return { x: sx, y: sy, w: sw, h: sh, labels: () => btn.forEach(f => f()) };
  },
  // clipped screen helper
  clip(c, s, fn) { c.save(); c.beginPath(); c.rect(s.x, s.y, s.w, s.h); c.clip(); fn(); c.restore(); },
};

// ---------- tactical pictures shared by MFD pages ----------
function relPos(sim, px, pz) {
  const dx = px - sim.x, dz = pz - sim.z, rng = Math.hypot(dx, dz);
  const brg = wrap360(Math.atan2(dx, dz) * R2D);
  return { rng, brg, rel: wrap180(brg - sim.psi) };
}
const HOME = { x: -18000, z: -42000 };

const Pages = {
  // B-scope fire-control radar: azimuth +-60 deg, range scale in nm or km
  radarB(c, s, sim, o = {}) {
    const col = o.ink || '#3f3', dim = o.dim || 'rgba(60,255,60,0.35)', scale = o.scale || 40, km = o.metric;
    I.clip(c, s, () => {
      c.strokeStyle = dim; c.lineWidth = 1;
      for (let i = 1; i < 4; i++) { const yy = s.y + s.h * i / 4; c.beginPath(); c.moveTo(s.x + s.w * 0.08, yy); c.lineTo(s.x + s.w * 0.92, yy); c.stroke(); }
      for (const az of [-30, 0, 30]) { const xx = s.x + s.w / 2 + az / 60 * s.w * 0.42; c.setLineDash([3, 5]); c.beginPath(); c.moveTo(xx, s.y + 14); c.lineTo(xx, s.y + s.h - 14); c.stroke(); c.setLineDash([]); }
      // antenna sweep
      const sweep = Math.sin(sim.t * 1.6) * 60, ax = s.x + s.w / 2 + sweep / 60 * s.w * 0.42;
      c.strokeStyle = col; c.lineWidth = 2; c.beginPath(); c.moveTo(ax, s.y + s.h - 4); c.lineTo(ax, s.y + s.h - 12); c.stroke();
      const unit = km ? 1000 : 1852;
      for (const t of sim.contacts) {
        const r = relPos(sim, t.x, t.z), rr = r.rng / unit;
        if (Math.abs(r.rel) > 60 || rr > scale) continue;
        const xx = s.x + s.w / 2 + r.rel / 60 * s.w * 0.42, yy = s.y + s.h - 14 - rr / scale * (s.h - 28);
        c.fillStyle = t.hostile ? (o.hostile || col) : (o.friend || col);
        if (o.symbols) {
          c.strokeStyle = t.hostile ? (o.hostile || '#f44') : (o.friend || '#4cf'); c.lineWidth = 2;
          c.beginPath(); if (t.hostile) { c.moveTo(xx, yy - 6); c.lineTo(xx + 6, yy); c.lineTo(xx, yy + 6); c.lineTo(xx - 6, yy); c.closePath(); } else c.arc(xx, yy, 6, Math.PI, 0); c.stroke();
        } else c.fillRect(xx - 4, yy - 3, 8, 6);
        const tr = wrap180(t.hdg - sim.psi) * D2R; c.strokeStyle = c.fillStyle; c.lineWidth = 1.5;
        c.beginPath(); c.moveTo(xx, yy); c.lineTo(xx + Math.sin(tr) * 12, yy - Math.cos(tr) * 12); c.stroke();
        I.text(c, String(Math.round(t.h * (km ? 0.001 : FT / 1000))), xx + 9, yy + 9, 10, c.fillStyle, 'left', '', MONO);
      }
      I.text(c, String(scale), s.x + 4, s.y + 22, 11, col, 'left', 'bold', MONO);
      I.text(c, o.mode || 'RWS', s.x + s.w / 2, s.y + 22, 11, col, 'center', 'bold', MONO);
      I.text(c, (km ? 'КМ ' : '') + 'A6 4B', s.x + s.w - 4, s.y + 22, 10, col, 'right', '', MONO);
    });
  },
  // plan-view tactical / horizontal situation display (track-up)
  map(c, s, sim, o = {}) {
    const col = o.ink || '#fff', unit = o.metric ? 1000 : 1852, scale = o.scale || 60;
    I.clip(c, s, () => {
      if (o.bg) { c.fillStyle = o.bg; c.fillRect(s.x, s.y, s.w, s.h); }
      const cx = s.x + s.w / 2, cy = s.y + s.h * (o.cy || 0.7), ppr = (s.h * (o.cy || 0.7) - 10) / scale; // px per unit
      const P = (wx, wz) => { const r = relPos(sim, wx, wz); return [cx + Math.sin(r.rel * D2R) * r.rng / unit * ppr, cy - Math.cos(r.rel * D2R) * r.rng / unit * ppr]; };
      // terrain blobs for the moving map
      if (o.terrain) { c.fillStyle = o.terrain; for (let k = 0; k < 9; k++) { const [tx, ty] = P(((k * 37) % 9 - 4) * 30000, ((k * 53) % 9 - 4) * 30000); c.beginPath(); c.ellipse(tx, ty, 50 * ppr / 15, 30 * ppr / 15, k, 0, 7); c.fill(); } }
      c.strokeStyle = o.ring || 'rgba(255,255,255,0.35)'; c.lineWidth = 1;
      for (const k of [0.5, 1]) { c.beginPath(); c.arc(cx, cy, scale * k * ppr, 0, 7); c.stroke(); }
      I.text(c, String(scale / 2), cx + scale * 0.5 * ppr * 0.71 + 3, cy - scale * 0.5 * ppr * 0.71, 10, col, 'left', '', MONO);
      // threat rings
      for (const e of sim.emitters) { const [ex, ey] = P(e.x, e.z); c.strokeStyle = o.threat || '#f44'; c.lineWidth = 1.5; c.beginPath(); c.arc(ex, ey, 25000 / unit * ppr, 0, 7); c.stroke(); I.text(c, e.code, ex, ey, 12, o.threat || '#f44', 'center', 'bold', MONO); }
      // route + home
      const [hx, hy] = P(HOME.x, HOME.z); c.strokeStyle = o.route || '#fff'; c.setLineDash([6, 4]); c.beginPath(); c.moveTo(cx, cy); c.lineTo(hx, hy); c.stroke(); c.setLineDash([]);
      c.beginPath(); c.arc(hx, hy, 6, 0, 7); c.stroke(); I.text(c, o.homeLbl || 'B1', hx + 9, hy, 10, col, 'left', '', MONO);
      for (const t of sim.contacts) {
        const [tx, ty] = P(t.x, t.z), tc = t.hostile ? (o.hostile || '#f44') : (o.friend || '#4cf');
        c.strokeStyle = tc; c.fillStyle = tc; c.lineWidth = 2; c.beginPath();
        if (t.hostile) { c.moveTo(tx, ty - 7); c.lineTo(tx + 7, ty); c.lineTo(tx, ty + 7); c.lineTo(tx - 7, ty); c.closePath(); } else c.arc(tx, ty, 7, Math.PI, 0);
        c.stroke();
        const tr = wrap180(t.hdg - sim.psi) * D2R; c.beginPath(); c.moveTo(tx, ty); c.lineTo(tx + Math.sin(tr) * 16, ty - Math.cos(tr) * 16); c.stroke();
        if (o.labels) I.text(c, t.type, tx + 10, ty - 9, 10, tc, 'left', '', MONO);
      }
      // ownship
      c.fillStyle = o.own || '#fff'; c.beginPath(); c.moveTo(cx, cy - 9); c.lineTo(cx - 6, cy + 7); c.lineTo(cx, cy + 3); c.lineTo(cx + 6, cy + 7); c.closePath(); c.fill();
      I.text(c, String(Math.round(sim.psi)).padStart(3, '0') + '°', cx, s.y + 12, 12, col, 'center', 'bold', MONO);
    });
  },
  // threat warning scope
  rwr(c, x, y, r, sim, o = {}) {
    const col = o.ink || '#3f3';
    c.beginPath(); c.arc(x, y, r, 0, 7); c.fillStyle = o.bg || '#030503'; c.fill();
    c.strokeStyle = o.ring || 'rgba(80,255,80,0.4)'; c.lineWidth = 1;
    for (const k of [0.33, 0.66, 1]) { c.beginPath(); c.arc(x, y, r * k * 0.95, 0, 7); c.stroke(); }
    for (let a = 0; a < 360; a += 30) { const [x1, y1] = polar(x, y, r * 0.95, a), [x2, y2] = polar(x, y, r * 0.85, a); c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke(); }
    const list = [...sim.emitters.map(e => ({ ...e, lethal: 0.6 })), ...sim.contacts.filter(t => t.hostile).map(t => ({ x: t.x, z: t.z, code: t.type === 'AEW' ? 'E' : '29', lethal: 0.85 }))];
    for (const e of list) {
      const rp = relPos(sim, e.x, e.z), rr = r * clamp(e.lethal - rp.rng / 300000, 0.25, 0.9);
      const [ex, ey] = polar(x, y, rr, rp.rel);
      I.text(c, e.code, ex, ey, r * 0.2, col, 'center', 'bold', MONO);
      if (rp.rng < 45000) { c.strokeStyle = col; c.beginPath(); c.arc(ex, ey, r * 0.14, 0, 7); c.stroke(); }
    }
    c.strokeStyle = col; c.beginPath(); c.moveTo(x - 5, y); c.lineTo(x + 5, y); c.moveTo(x, y - 5); c.lineTo(x, y + 5); c.stroke();
  },
  // stores management
  sms(c, s, sim, o = {}) {
    const col = o.ink || '#3f3';
    I.clip(c, s, () => {
      if (o.bg) { c.fillStyle = o.bg; c.fillRect(s.x, s.y, s.w, s.h); }
      const cx = s.x + s.w / 2, cy = s.y + s.h * 0.48, k = Math.min(s.w, s.h) / 200;
      c.strokeStyle = col; c.lineWidth = 1.5;
      // plan-view silhouette
      c.beginPath(); c.moveTo(cx, cy - 70 * k); c.lineTo(cx + 12 * k, cy - 20 * k); c.lineTo(cx + 80 * k, cy + 25 * k); c.lineTo(cx + 80 * k, cy + 35 * k); c.lineTo(cx + 15 * k, cy + 30 * k);
      c.lineTo(cx + 35 * k, cy + 60 * k); c.lineTo(cx, cy + 55 * k); c.lineTo(cx - 35 * k, cy + 60 * k); c.lineTo(cx - 15 * k, cy + 30 * k); c.lineTo(cx - 80 * k, cy + 35 * k); c.lineTo(cx - 80 * k, cy + 25 * k); c.lineTo(cx - 12 * k, cy - 20 * k); c.closePath(); c.stroke();
      let row = 0;
      sim.stores.forEach((w, i) => {
        if (w.alt) return;
        const short = w.name.split(' ')[0];
        const yy = s.y + s.h - 30 - (sim.stores.filter(q => !q.alt).length - 1 - row) * 15; row++;
        const sel = i === sim.wpn;
        I.text(c, `${sel ? '▶ ' : '  '}${w.n} ${short}  ${w.where.split(' ').slice(0, 2).join(' ').toUpperCase()}`, s.x + 8, yy, 11, col, 'left', sel ? 'bold' : '', MONO);
      });
      if (sim.gunRds) I.text(c, `GUN ${sim.gunRds}`, s.x + s.w - 8, s.y + 34, 12, col, 'right', 'bold', MONO);
      I.text(c, sim.master ? (o.armTxt || 'ARM') : 'SAFE', s.x + 8, s.y + 34, 12, sim.master ? (o.warn || '#ff4') : col, 'left', 'bold', MONO);
      // station boxes along the wing
      const stations = sim.stores.filter(q => !q.alt);
      let n = 0;
      for (const w of stations) for (let j = 0; j < w.n; j++) {
        const side = n % 2 ? 1 : -1, idx = Math.floor(n / 2), half = Math.ceil(stations.reduce((a, q) => a + q.n, 0) / 2);
        const xx = cx + side * (18 + idx * 58 / Math.max(1, half)) * k, yy = cy + (5 + idx * 22 / Math.max(1, half)) * k;
        c.strokeStyle = col; c.strokeRect(xx - 5 * k, yy - 3 * k, 10 * k, 22 * k);
        I.text(c, w.name[0], xx, yy + 8 * k, 8 * k, col, 'center', 'bold', MONO); n++;
      }
    });
  },
  // engine / fuel page (glass)
  eng(c, s, sim, o = {}) {
    const col = o.ink || '#fff', a = sim.a, n = sim.twin ? 2 : 1, lb = o.metric ? 1 : 2.20462;
    I.clip(c, s, () => {
      if (o.bg) { c.fillStyle = o.bg; c.fillRect(s.x, s.y, s.w, s.h); }
      const gw = s.w / (n * 2 + 0.5), gr = Math.min(gw * 0.42, s.h * 0.17);
      for (let e = 0; e < n; e++) {
        const items = [[o.rpmLbl || 'RPM', sim.rpm[e], 0, 110, '%', 100, 0], [o.tLbl || 'TIT', sim.egt[e], 200, 1100, '°C', 980, 0]];
        items.forEach(([lbl, v, mn, mx, u, red], k) => {
          const gx = s.x + gw * (0.75 + e * 2 + k), gy = s.y + s.h * 0.28;
          c.strokeStyle = 'rgba(255,255,255,0.3)'; c.lineWidth = 4; c.beginPath(); c.arc(gx, gy, gr, Math.PI * 0.75, Math.PI * 2.25); c.stroke();
          const f = clamp((v - mn) / (mx - mn), 0, 1), warn = v > red;
          c.strokeStyle = warn ? '#f44' : (o.arc || '#4f4'); c.beginPath(); c.arc(gx, gy, gr, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * f)); c.stroke();
          I.text(c, (v).toFixed(0), gx, gy + 2, gr * 0.45, warn ? '#f44' : col, 'center', 'bold', MONO);
          I.text(c, lbl + ' ' + u, gx, gy + gr + 10, 10, col, 'center', '', MONO);
        });
        const nx = s.x + gw * (0.75 + e * 2) - gw * 0.4, ny = s.y + s.h * 0.55;
        I.text(c, `${o.nozLbl || 'NOZ'} ${sim.noz[e].toFixed(0)}%`, nx, ny, 11, col, 'left', '', MONO);
        I.text(c, `${o.ffLbl || 'FF'} ${(sim.ff[e] * lb).toFixed(0)}${o.metric ? ' kg/h' : ' pph'}`, nx, ny + 16, 11, sim.ab ? (o.abc || '#ff4') : col, 'left', '', MONO);
        if (sim.ab) I.text(c, o.abLbl || 'AB', nx + gw * 1.7, ny, 12, o.abc || '#ff4', 'right', 'bold', MONO);
      }
      const fy = s.y + s.h * 0.8;
      I.text(c, `${o.fuelLbl || 'FUEL'} ${(sim.fuel * lb).toFixed(0)} ${o.metric ? 'kg' : 'lb'}`, s.x + 10, fy, 13, sim.warn.fuel ? '#fa3' : col, 'left', 'bold', MONO);
      const fw = s.w - 20; c.strokeStyle = col; c.strokeRect(s.x + 10, fy + 10, fw, 10);
      c.fillStyle = sim.warn.fuel ? '#fa3' : (o.arc || '#4f4'); c.fillRect(s.x + 11, fy + 11, (fw - 2) * sim.fuel / a.fuel, 8);
      I.text(c, `${o.thrLbl || 'THR'} ${(sim.thrust / 1000).toFixed(0)} kN`, s.x + s.w - 10, fy, 11, col, 'right', '', MONO);
    });
  },
  // glass primary flight display with tapes. o: {metric, ink, sky, ground}
  pfd(c, s, sim, o = {}) {
    const th = (sim.gam * R2D + sim.alpha * Math.cos(sim.phi)), ph = sim.phi * R2D;
    const ias = o.metric ? sim.ias * 3.6 : sim.ias * KT, alt = o.metric ? sim.h : sim.h * FT;
    I.clip(c, s, () => {
      const cx = s.x + s.w / 2, cy = s.y + s.h / 2 + 6, ppd = s.h / 60;
      c.save(); c.translate(cx, cy); c.rotate(-ph * D2R);
      c.fillStyle = o.sky || '#2f6db3'; c.fillRect(-s.w * 2, -s.h * 3 + th * ppd, s.w * 4, s.h * 3);
      c.fillStyle = o.ground || '#7a5230'; c.fillRect(-s.w * 2, th * ppd, s.w * 4, s.h * 3);
      c.strokeStyle = '#fff'; c.lineWidth = 2; c.beginPath(); c.moveTo(-s.w * 2, th * ppd); c.lineTo(s.w * 2, th * ppd); c.stroke();
      c.lineWidth = 1.2;
      for (let p = -80; p <= 80; p += 5) {
        if (!p) continue; const yy = (th - p) * ppd; if (Math.abs(yy) > s.h * 0.4) continue;
        const w = p % 10 ? s.w * 0.05 : s.w * 0.11;
        c.beginPath(); c.moveTo(-w, yy); c.lineTo(w, yy); c.stroke();
        if (!(p % 10)) I.text(c, String(p), w + 12, yy, 10, '#fff', 'center', '', MONO);
      }
      c.restore();
      c.strokeStyle = o.sym || '#ffd23c'; c.lineWidth = 3;
      c.beginPath(); c.moveTo(cx - s.w * 0.2, cy); c.lineTo(cx - s.w * 0.07, cy); c.lineTo(cx - s.w * 0.05, cy + 6); c.moveTo(cx + s.w * 0.2, cy); c.lineTo(cx + s.w * 0.07, cy); c.lineTo(cx + s.w * 0.05, cy + 6); c.stroke();
      c.fillStyle = o.sym || '#ffd23c'; c.fillRect(cx - 2, cy - 2, 4, 4);
      c.save(); c.translate(cx, cy); c.strokeStyle = '#fff'; c.lineWidth = 1.5;
      for (const b of [-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60]) { const [x1, y1] = polar(0, 0, s.h * 0.36, b), [x2, y2] = polar(0, 0, s.h * (b % 30 ? 0.38 : 0.4), b); c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke(); }
      c.rotate(-ph * D2R); c.fillStyle = '#fff'; c.beginPath(); c.moveTo(0, -s.h * 0.355); c.lineTo(-5, -s.h * 0.32); c.lineTo(5, -s.h * 0.32); c.closePath(); c.fill();
      c.restore();
      const tw = Math.max(o.compact ? 30 : 46, s.w * 0.17);
      if (o.compact) {
        I.htape(c, s.x + 2, s.y + 2, s.w - 4, 14, sim.psi, { ppd: 2, px: 8, bg: 'rgba(0,0,0,0.5)', fam: MONO });
        I.text(c, String(Math.round(ias)), s.x + 3, cy, 10, '#fff', 'left', 'bold', MONO);
        I.text(c, String(Math.round(alt)), s.x + s.w - 3, cy, 10, '#fff', 'right', 'bold', MONO);
        return;
      }
      I.vtape(c, s.x + 2, s.y + 26, tw, s.h - 52, ias, { step: o.metric ? 20 : 10, every: o.metric ? 100 : 50, ppu: o.metric ? 1.0 : 1.8, side: 'l', bg: 'rgba(0,0,0,0.45)', px: 11, fam: MONO, min: 0 });
      I.vtape(c, s.x + s.w - tw - 2, s.y + 26, tw, s.h - 52, alt, { step: o.metric ? 50 : 100, every: o.metric ? 250 : 500, ppu: o.metric ? 0.25 : 0.08, side: 'r', bg: 'rgba(0,0,0,0.45)', px: 11, fam: MONO });
      I.htape(c, s.x + tw + 10, s.y + 4, s.w - 2 * tw - 20, 18, sim.psi, { ppd: 3, px: 10, bg: 'rgba(0,0,0,0.45)', fam: MONO });
      I.text(c, `M ${sim.M.toFixed(2)}`, s.x + 6, s.y + s.h - 14, 12, '#fff', 'left', 'bold', MONO);
      I.text(c, `${sim.n.toFixed(1)}G`, s.x + tw + 10, s.y + s.h - 14, 12, '#fff', 'left', 'bold', MONO);
      I.text(c, `${o.vsLbl || 'VS'} ${o.metric ? sim.vs.toFixed(0) : (sim.vs * FT * 60).toFixed(0)}`, s.x + s.w - 6, s.y + s.h - 14, 11, '#fff', 'right', '', MONO);
      I.text(c, `α ${sim.alpha.toFixed(1)}`, s.x + s.w - tw - 10, s.y + s.h - 14, 12, sim.warn.stall ? '#f44' : '#fff', 'right', 'bold', MONO);
    });
  },
};
