// Plays the real input flow of a built game file: boot screen, key, intro, skip, menu with music, replay, game.
// usage: node flow_test.js <ace4combat.html> [out dir]   (needs Playwright with Chromium)
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const [html, out = '.'] = process.argv.slice(2);
(async () => {
  fs.mkdirSync(out, { recursive: true });
  const b = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] }); const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
  const errs = []; p.on('pageerror', e => errs.push(e.message));
  await p.route('**/fonts.g*/**', r => r.abort());
  await p.goto('file://' + path.resolve(html)); await p.waitForTimeout(600);
  const st = () => p.evaluate(() => [window.__ace.state, window.__intro.mode, window.__intro.t.toFixed(2), document.getElementById('menu').hidden, document.getElementById('boot').hidden, window.__ace.track, window.__ace.music].join(' '));
  const log = [];
  log.push('load ' + await st());
  await p.keyboard.press('Space'); await p.waitForTimeout(3000); log.push('after key 3s ' + await st());
  await p.keyboard.press('KeyX'); await p.waitForTimeout(500); log.push('after skip ' + await st());
  await p.screenshot({ path: path.join(out, 'flow_menu.jpg'), type: 'jpeg', quality: 80 });
  await p.waitForTimeout(1500); log.push('hold 2s ' + await st());
  await p.click('#introPlay'); await p.waitForTimeout(1500); log.push('watch again ' + await st());
  await p.evaluate(() => window.__intro.seek(40.5)); await p.waitForTimeout(400); log.push('near end ' + await st());
  await p.waitForTimeout(800); log.push('end ' + await st());
  await p.click('#start'); await p.waitForTimeout(800); log.push('game ' + await st());
  await p.screenshot({ path: path.join(out, 'flow_game.jpg'), type: 'jpeg', quality: 80 });
  log.push('errors: ' + (errs.join(' | ') || 'none'));
  fs.writeFileSync(path.join(out, 'flow.txt'), log.join('\n') + '\n');
  console.log(log.join('\n'));
  await b.close();
  if (errs.length) process.exitCode = 1;
})();
