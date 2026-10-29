// parity.js - the C game's figure engine (src/figure.c, through tests/test_figure --dump) and this one make the same
// triangles: for each crew member and pose, the same count, roles and parts, and every vertex within 0.05 mm.
// usage: node parity.js <test_figure binary>     Original code for this project.
const { execFileSync } = require('child_process');
const F = require('./figure.js');
const bin = process.argv[2];
const ROLES = ['trousers', 'belt', 'shirt', 'collar', 'sleeve', 'skin', 'skinShade', 'hair', 'brow', 'lid', 'lip', 'eyeWhite', 'iris', 'mouth', 'cap', 'gold', 'kit',
  'boot', 'pocket', 'tape', 'tee', 'paper', 'anchor', 'oak'];
const PARTS = { trunk: 1, head: 2, face: 3, handL: 4, handR: 5 };
let bad = 0;
for (const [who, name] of [[0, 'operator'], [1, 'supervisor'], [2, 'commander']])
  for (const raise of [0, 1]) {
    const c = F.CREW[name], js = F.build({ height: 1.75, kit: c.kit, skin: c.skin, hair: c.hair, pose: c.pose(1.75, raise) }).tris;
    const lines = execFileSync(bin, ['--dump', String(who), String(raise)]).toString().trim().split('\n');
    const n = +lines[0];
    let worst = 0, mism = 0;
    if (n !== js.length) { console.log(`${name} raise ${raise}: ${n} triangles in C, ${js.length} here`); bad++; continue; }
    for (let i = 0; i < n; i++) {
      const f = lines[i + 1].split(' ').map(Number), t = js[i];
      const part = PARTS[t.part] || 0, cpart = f[1] === 6 ? 0 : f[1];
      if (ROLES[f[0]] !== t.role || cpart !== part || !!f[2] !== !!t.two) mism++;
      for (let k = 0; k < 9; k++) worst = Math.max(worst, Math.abs(f[3 + k] - t.v[Math.floor(k / 3)][k % 3]));
    }
    const ok = !mism && worst < 5e-5;
    if (!ok) bad++;
    console.log(`${name.padEnd(10)} raise ${raise}: ${n} triangles, ${mism} differ in role or part, largest vertex gap ${(worst * 1000).toFixed(4)} mm ${ok ? 'ok' : 'DIFFERENT'}`);
  }
process.exitCode = bad ? 1 : 0;
