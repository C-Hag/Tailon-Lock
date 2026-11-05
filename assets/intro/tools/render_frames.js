// Renders still frames of the intro from a built game file, for checking shots without playing the music.
// usage: node render_frames.js <game.html> <out dir> [t1,t2,...]   (needs Playwright with Chromium)
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const [html, out, list] = process.argv.slice(2);
const DEFAULT = '2.5,5,5.9,6.7,7.6,9,9.9,11,11.85,12.6,13.6,13.76,13.9,15,16.5,17.2,18.5,18.88,20.3,21,21.3,21.42,21.8,23,23.9,24.05,24.8,26.5,27.05,27.8,29.5,30.8,32.2,32.7,33.6,34.24,34.5,35.3,36.2,37,37.6,39,41,50';
(async () => {
  fs.mkdirSync(out, { recursive: true });
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
  const errs = []; p.on('pageerror', e => errs.push(e.message)); p.on('console', m => { if (m.type() === 'error') errs.push(m.text()); });
  await p.route('**/fonts.g*/**', r => r.abort());
  await p.goto('file://' + path.resolve(html)); await p.waitForTimeout(800);
  const times = (list || DEFAULT).split(',').map(Number);
  const log = [];
  for (const t of times) {
    const r = await p.evaluate(t => { document.getElementById('boot').hidden = true; const I = window.__intro; I.pause(t); const t0 = performance.now(); I.render(); const ms = performance.now() - t0; const sh = I.script.shots.find(s => t >= s.t0 && t < s.t1); return { ms, shot: sh ? sh.name : '?' }; }, t);
    await p.waitForTimeout(50);
    const f = `t${t.toFixed(2).padStart(5, '0')}_${r.shot}.jpg`;
    await p.screenshot({ path: path.join(out, f), type: 'jpeg', quality: 82 });
    log.push(`${t}\t${r.shot}\t${r.ms.toFixed(1)} ms`);
  }
  const avg = await p.evaluate(() => { const I = window.__intro, o = []; for (const t of [5, 5.9, 9.5, 13.76, 16.5, 18.5, 23, 27.8, 35.3, 37.6]) { I.pause(t); const t0 = performance.now(); for (let i = 0; i < 5; i++) I.render(); o.push(t + ':' + ((performance.now() - t0) / 5).toFixed(1)); } return o.join(' '); });
  log.push('render ms ' + avg, 'errors: ' + (errs.join(' | ') || 'none'));
  fs.writeFileSync(path.join(out, 'log.txt'), log.join('\n') + '\n');
  console.log(log.join('\n'));
  await b.close();
  if (errs.length) process.exitCode = 1;
})();
