// parity.js - the C game's figure engine (src/figure.c, through tests/test_figure --dump) and this one make the same
// triangles: for each crew member and pose, the same count, roles, parts and shading, every vertex within 0.05 mm
// and every colour within 0.02.
// usage: node parity.js <test_figure binary>     Original code for this project.
const { execFileSync } = require('child_process');
const F = require('./figure.js');
const bin = process.argv[2];
const ROLES = ['trousers', 'belt', 'shirt', 'collar', 'sleeve', 'skin', 'skinShade', 'hair', 'brow', 'lid', 'lip', 'eyeWhite', 'iris', 'mouth', 'cap', 'gold', 'kit',
  'boot', 'pocket', 'tape', 'tee', 'paper', 'anchor', 'oak'];
const PARTS = { trunk: 1, head: 2, face: 3, handL: 4, handR: 5, eye: 7 };
let bad = 0;
for (const [who, name] of [[0, 'operator'], [1, 'supervisor'], [2, 'commander']])
  for (const [raise, open] of [[0, null], [1, 0.7]]) {
    const c = F.CREW[name], face = open == null ? undefined : { open, wide: 0.3, smile: 0.2, browUp: 0.5, blink: 0.3 };
    const js = F.build({ height: 1.75, kit: c.kit, skin: c.skin, hair: c.hair, iris: c.iris, faceShape: c.faceShape, face, pose: c.pose(1.75, raise) }).tris;
    const lines = execFileSync(bin, ['--dump', String(who), String(raise)].concat(open == null ? [] : [String(open)])).toString().trim().split('\n');
    const n = +lines[0];
    let worst = 0, mism = 0, cworst = 0;
    if (n !== js.length) { console.log(`${name} raise ${raise}: ${n} triangles in C, ${js.length} here`); bad++; continue; }
    for (let i = 0; i < n; i++) {
      const f = lines[i + 1].split(' ').map(Number), t = js[i];
      const part = PARTS[t.part] || 0, cpart = f[1] === 6 ? 0 : f[1];
      const shade = t.ns ? 2 : t.col ? 1 : 0;
      if (ROLES[f[0]] !== t.role || cpart !== part || !!f[2] !== !!t.two || f[12] !== shade) mism++;
      const cols = t.ns ? t.cs.flat() : t.col ? t.col : null;
      if (cols) for (let k = 0; k < cols.length; k++) cworst = Math.max(cworst, Math.abs(f[13 + k] - cols[k]));
      for (let k = 0; k < 9; k++) worst = Math.max(worst, Math.abs(f[3 + k] - t.v[Math.floor(k / 3)][k % 3]));
    }
    const ok = !mism && worst < 5e-5 && cworst < 0.02;
    if (!ok) bad++;
    console.log(`${name.padEnd(10)} ${open == null ? 'at rest  ' : 'speaking '}: ${n} triangles, ${mism} differ in role or part, largest vertex gap ${(worst * 1000).toFixed(4)} mm, colour ${cworst.toFixed(4)} ${ok ? 'ok' : 'DIFFERENT'}`);
  }
// the speech timelines: the same segments, shapes and word starts
const VN = ['rest', 'mbp', 'fv', 'th', 'aa', 'ee', 'ih', 'oo', 'oh', 'w', 'sh', 'tt', 'kk', 'ss'];
const sp = execFileSync(bin, ['--speech']).toString().trim().split('\n');
let at = 0;
for (const [text, t0, t1] of [['BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.', 5.6, 7.0], ['ALERT, ALERT! WE HAVE INBOUND.', 7.05, 8.05], ['SEND THE BIRDS.', 8.1, 9.2]]) {
  const [ns, nw] = sp[at++].split(' ').map(Number), tr = F.speechTrack(text, t0, t1);
  let diff = ns !== tr.seg.length || nw !== tr.words.length ? 1 : 0, worst = 0;
  for (let i = 0; i < ns; i++) { const [a, b, v] = sp[at++].split(' ').map(Number), s2 = tr.seg[i]; if (!s2 || VN[v] !== s2.v) diff++; else worst = Math.max(worst, Math.abs(a - s2.t0), Math.abs(b - s2.t1)); }
  for (let i = 0; i < nw; i++) { const [a, w] = sp[at++].split(' '); if (w !== tr.words[i]) diff++; worst = Math.max(worst, Math.abs(+a - tr.starts[i])); }
  const ok = !diff && worst < 1e-4;
  if (!ok) bad++;
  console.log(`speech "${text}": ${ns} segments, ${nw} words, ${diff} differ, largest time gap ${worst.toExponential(1)} s ${ok ? 'ok' : 'DIFFERENT'}`);
}
process.exitCode = bad ? 1 : 0;
