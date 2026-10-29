// Hangar page: jet picker, 3D model viewer, spec sheet, live cockpit, comparison table.
const MODELS = {};
const CVM = GLB.parse(Uint8Array.from(atob(CARRIER_B64), ch => ch.charCodeAt(0)).buffer);
for (const j of JETS) {
  const bin = Uint8Array.from(atob(GLB_B64[j.id]), ch => ch.charCodeAt(0));
  MODELS[j.id] = GLB.parse(bin.buffer);
}
const $ = s => document.querySelector(s);
const fmt = (v, d = 0) => Number(v).toLocaleString('en-US', { maximumFractionDigits: d, minimumFractionDigits: d });
const STAT_NAMES = { speed: 'Speed', turn: 'Turn', climb: 'Climb', range: 'Range', field: 'Short field', firepower: 'Firepower', stealth: 'Stealth' };
let cur = JETS[0];

function fitCanvas(cv, w, h) {
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
  const c = cv.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); return c;
}

// ---------- picker ----------
function buildPicker() {
  const el = $('#picker');
  for (const j of JETS) {
    const b = document.createElement('button');
    b.className = 'pick'; b.id = 'pick-' + j.id; b.setAttribute('aria-label', `${j.designation} ${j.name}`);
    b.innerHTML = `<canvas width="200" height="90"></canvas><span class="d">${j.designation}</span><span class="n">${j.name}</span><span class="i">${j.inspired}</span>`;
    b.onclick = () => select(j.id, true);
    el.appendChild(b);
    const cv = b.querySelector('canvas'), c = fitCanvas(cv, 200, 90);
    cv.style.width = '100%';
    Render.draw(c, 200, 90, MODELS[j.id], { yaw: 2.35, pitch: 0.42, dist: 13 + j.length * 0.15, sweep: 0 });
  }
}

// ---------- 3D viewer ----------
const view = { yaw: 2.4, pitch: 0.35, dist: 30, spin: true, sweep: 0, stores: true, drag: null };
function drawViewer() {
  const cv = $('#viewer'), w = cv.clientWidth, h = cv.clientHeight;
  if (cv._w !== w || cv._h !== h) { cv._c = fitCanvas(cv, w, h); cv._w = w; cv._h = h; }
  const c = cv._c;
  c.clearRect(0, 0, w, h);
  // ground shadow ellipse
  const g = c.createRadialGradient(w / 2, h * 0.72, 10, w / 2, h * 0.72, w * 0.4);
  g.addColorStop(0, 'rgba(0,0,0,0.35)'); g.addColorStop(1, 'rgba(0,0,0,0)'); c.fillStyle = g; c.fillRect(0, 0, w, h);
  const n = Render.draw(c, w, h, MODELS[cur.id], { yaw: view.yaw, pitch: view.pitch, dist: view.dist, sweep: view.sweep, hide: { stores: !view.stores }, dy: -h * 0.03 });
  $('#tri').textContent = `${cur.tris} triangles · ${fmt(cur.length, 1)} m long · ${fmt(cur.id === 'nf24' ? cur.span : cur.span, 1)} m span`;
}
function initViewer() {
  const cv = $('#viewer');
  cv.addEventListener('pointerdown', e => { view.drag = [e.clientX, e.clientY, view.yaw, view.pitch]; view.spin = false; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', e => { if (!view.drag) return; view.yaw = view.drag[2] - (e.clientX - view.drag[0]) * 0.01; view.pitch = clamp(view.drag[3] + (e.clientY - view.drag[1]) * 0.008, -1.4, 1.5); });
  cv.addEventListener('pointerup', () => view.drag = null);
  cv.addEventListener('wheel', e => { e.preventDefault(); view.dist = clamp(view.dist * (1 + Math.sign(e.deltaY) * 0.08), 14, 70); }, { passive: false });
  $('#spin').onclick = () => { view.spin = !view.spin; };
  $('#stores').onchange = e => { view.stores = e.target.checked; };
  $('#sweep').oninput = e => { view.sweep = +e.target.value - 20; $('#sweepv').textContent = e.target.value + '°'; };
  for (const [id, y, p] of [['v-front', Math.PI, 0.05], ['v-side', Math.PI / 2, 0.02], ['v-top', Math.PI, 1.55], ['v-34', 2.4, 0.35]]) $('#' + id).onclick = () => { view.yaw = y; view.pitch = p; view.spin = false; };
}

// ---------- spec sheet ----------
function specSheet(j) {
  const p = j.perf, imp = j.units === 'imperial';
  const kmh = v => imp ? `${fmt(v / 1.852)} kt` : `${fmt(v)} km/h`, m = v => imp ? `${fmt(v * 3.28084 / 100) * 100} ft` : `${fmt(v)} m`;
  const rows = [
    ['Top speed at 11 km', `Mach ${p.m_max_alt.toFixed(2)} (${fmt(p.v_max_alt_kmh)} km/h)`],
    ['Top speed at sea level', `${fmt(p.v_max_sl_kmh)} km/h`],
    ['Supercruise (no afterburner)', p.supercruise ? `Mach ${p.supercruise.toFixed(2)}` : 'No'],
    ['Climb rate, sea level', `${fmt(p.climb_ms)} m/s (${fmt(p.climb_ms * 196.85 / 1000, 0)}k ft/min)`],
    ['Service ceiling', `${fmt(p.ceiling_m)} m`],
    ['Instantaneous turn, 4.5 km', `${p.turn_instant.toFixed(1)} °/s at ${fmt(p.corner_kmh)} km/h`],
    ['Sustained turn, 4.5 km', `${p.turn_sustained.toFixed(1)} °/s`],
    ['Roll rate / g limits', `${j.roll} °/s · +${j.g_max} / ${j.g_min} g`],
    ['Max angle of attack', `${j.alpha_max}°${j.alpha_max >= 50 ? ' (thrust vectoring)' : ''}`],
    ['Ferry range (internal fuel)', `${fmt(p.range_km)} km · combat radius ≈ ${fmt(p.radius_km)} km`],
    ['Take-off roll (full afterburner)', `${fmt(p.takeoff_m)} m`],
    ['Landing roll', `${fmt(p.landing_m)} m${j.landing === 'reverser' ? ' (thrust reverser)' : j.landing === 'chute' ? ' (drag chute)' : j.landing === 'hook' ? ' on land; arrested on a carrier ≈ 100 m' : ''}`],
    ['Stall speed (clean, combat mass)', `${fmt(p.stall_kmh)} km/h`],
  ];
  const dims = [
    ['Length × span × height', `${j.length.toFixed(1)} × ${j.span.toFixed(1)}${j.span_swept ? ` (${j.span_swept.toFixed(1)} swept)` : ''} × ${j.height.toFixed(1)} m`],
    ['Wing area · aspect ratio', `${j.S} m² · ${j.AR.toFixed(2)}`],
    ['Empty · internal fuel · combat mass', `${fmt(j.m_empty)} · ${fmt(j.fuel)} · ${fmt(j.m_combat)} kg`],
    ['Thrust-to-weight · wing loading', `${p.tw.toFixed(2)} · ${fmt(p.wing_loading)} kg/m²`],
    ['Engines', j.engines], ['Radar cross-section', j.rcs], ['Radar', j.radar], ['Crew', j.crew],
  ];
  const tr = r => `<tr><th scope="row">${r[0]}</th><td>${r[1]}</td></tr>`;
  const bars = Object.entries(j.game).map(([k, v]) => `<div class="bar"><span>${STAT_NAMES[k]}</span><i style="--v:${v / 10}"></i><b>${v.toFixed(1)}</b></div>`).join('');
  return `
    <div class="eyebrow">${j.bloc} · ${j.era} · inspired by the ${j.inspired}</div>
    <h2><span class="des">${j.designation}</span> ${j.name}</h2>
    <p class="role">${j.role}</p>
    <p class="blurb">${j.blurb}</p>
    <div class="bars" aria-label="Game stats, 3 to 10 relative to the other seven">${bars}</div>
    <p class="note">Game stats are relative to the other seven jets (3 = weakest, 10 = best).</p>
    <details open><summary>Why it flies</summary><ul class="aero">${j.aero.map(a => `<li>${a}</li>`).join('')}</ul></details>
    <details open><summary>Performance <small>computed by the flight model at combat mass (${fmt(j.m_combat)} kg)</small></summary><div class="tw"><table>${rows.map(tr).join('')}</table></div></details>
    <details><summary>Dimensions and engines</summary><div class="tw"><table>${dims.map(tr).join('')}</table></div></details>
    <details open><summary>Weapons</summary><ul class="wpn">${j.weapons.map(w => `<li><b>${w[1]} ×</b> ${w[0]} <span>${w[2]}</span></li>`).join('')}<li><b>Gun</b> ${j.gun}</li></ul></details>`;
}

// ---------- cockpit ----------
let sim = null, ck = null, keys = {}, stick = null;
function setCockpit(j) {
  sim = new Sim(j); ck = COCKPITS[j.cockpit];
  $('#ck-title').textContent = `${j.designation} ${j.name} cockpit`;
  $('#ck-desc').textContent = j.cockpit_desc;
  const gl = ck.glossary;
  $('#gloss').hidden = !gl;
  if (gl) $('#gloss-list').innerHTML = gl.map(g => `<div><dt>${g[0]}</dt><dd>${g[1]}</dd></div>`).join('');
  $('#demo').setAttribute('aria-pressed', 'true');
  $('#thr').value = Math.round(sim.thr * 100);
}
function initCockpit() {
  const cv = $('#cockpit');
  const toLogical = e => { const r = cv.getBoundingClientRect(); return [(e.clientX - r.left) / r.width * CW, (e.clientY - r.top) / r.height * CVH]; };
  cv.addEventListener('pointerdown', e => {
    cv.focus(); const [x, y] = toLogical(e);
    if (y < 455) { stick = { x0: e.clientX, y0: e.clientY, w: cv.getBoundingClientRect().width }; takeControl(); cv.setPointerCapture(e.pointerId); }
    else if (ck.click) ck.click(x, y);
  });
  cv.addEventListener('pointermove', e => { if (!stick) return; const k = stick.w / 5; sim.inp.roll = clamp((e.clientX - stick.x0) / k, -1, 1); sim.inp.pitch = clamp((e.clientY - stick.y0) / k, -1, 1); }); // drag down = pull back
  const end = () => { if (stick) { stick = null; sim.inp.roll = 0; sim.inp.pitch = 0; } };
  cv.addEventListener('pointerup', end); cv.addEventListener('pointercancel', end);
  cv.addEventListener('keydown', e => { const k = e.key.toLowerCase(); if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' '].includes(k)) e.preventDefault(); keys[k] = true; onKey(k); });
  cv.addEventListener('keyup', e => { keys[e.key.toLowerCase()] = false; });
  cv.addEventListener('blur', () => { keys = {}; });
  $('#demo').onclick = () => { sim.auto = !sim.auto; if (!sim.auto) { sim.inp.pitch = 0; sim.inp.roll = 0; } $('#demo').setAttribute('aria-pressed', String(sim.auto)); };
  $('#reset').onclick = () => { sim.reset(); $('#thr').value = Math.round(sim.thr * 100); $('#demo').setAttribute('aria-pressed', 'true'); };
  $('#thr').oninput = e => { takeControl(); sim.thr = +e.target.value / 100; };
  $('#gearb').onclick = () => { sim.gear = !sim.gear; };
  $('#wpnb').onclick = () => cycleWeapon();
}
function takeControl() { if (sim.auto) { sim.auto = false; $('#demo').setAttribute('aria-pressed', 'false'); } }
function cycleWeapon() { const n = sim.stores.length; for (let k = 1; k <= n; k++) { const i = (sim.wpn + k) % n; if (!sim.stores[i].alt) { sim.wpn = i; break; } } }
function onKey(k) {
  if (k === 'g') sim.gear = !sim.gear;
  if (k === 'f') sim.flaps = sim.flaps ? 0 : 1;
  if (k === 'm') sim.master = !sim.master;
  if (k === 'q') cycleWeapon();
  if (k === 'p') { sim.auto = !sim.auto; $('#demo').setAttribute('aria-pressed', String(sim.auto)); }
  if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright', 's', 'x', 'a', 'd', '+', '-', '=', 'shift', 'control'].includes(k)) takeControl();
}
function readKeys(dt) {
  if (sim.auto || stick) return;
  const pitch = (keys['arrowdown'] || keys['s'] ? 1 : 0) - (keys['arrowup'] || keys['x'] ? 1 : 0);
  const roll = (keys['arrowright'] || keys['d'] ? 1 : 0) - (keys['arrowleft'] || keys['a'] ? 1 : 0);
  sim.inp.pitch += (pitch - sim.inp.pitch) * Math.min(1, dt * 5);
  sim.inp.roll += (roll - sim.inp.roll) * Math.min(1, dt * 8);
  if (keys['+'] || keys['='] || keys['shift']) sim.thr = Math.min(1.25, sim.thr + dt * 0.4);
  if (keys['-'] || keys['control']) sim.thr = Math.max(0, sim.thr - dt * 0.4);
  $('#thr').value = Math.round(sim.thr * 100);
}

// ---------- comparison table ----------
function compareTable() {
  const cols = [['Jet', j => `<button class="link" data-id="${j.id}">${j.designation} ${j.name}</button>`], ['Inspired by', j => j.inspired], ['Mach max', j => j.perf.m_max_alt.toFixed(2)], ['Supercruise', j => j.perf.supercruise ? 'M ' + j.perf.supercruise.toFixed(2) : '–'],
    ['T/W', j => j.perf.tw.toFixed(2)], ['Wing load kg/m²', j => fmt(j.perf.wing_loading)], ['Inst. turn °/s', j => j.perf.turn_instant.toFixed(1)], ['Sust. turn °/s', j => j.perf.turn_sustained.toFixed(1)],
    ['Climb m/s', j => fmt(j.perf.climb_ms)], ['Ceiling m', j => fmt(j.perf.ceiling_m)], ['Range km', j => fmt(j.perf.range_km)], ['Take-off m', j => fmt(j.perf.takeoff_m)], ['Missiles', j => j.weapons.filter(w => !w[2].includes('alt')).reduce((a, w) => a + w[1], 0)], ['Tris', j => j.tris]];
  $('#compare').innerHTML = `<thead><tr>${cols.map(c => `<th scope="col">${c[0]}</th>`).join('')}</tr></thead><tbody>${JETS.map(j => `<tr${j.id === cur.id ? ' class="cur"' : ''}>${cols.map(c => `<td>${c[1](j)}</td>`).join('')}</tr>`).join('')}</tbody>`;
  $('#compare').querySelectorAll('button.link').forEach(b => b.onclick = () => { select(b.dataset.id, true); $('#top').scrollIntoView({ behavior: 'smooth' }); });
}

function select(id, user) {
  cur = JETS.find(j => j.id === id) || JETS[0];
  document.querySelectorAll('.pick').forEach(b => b.setAttribute('aria-current', String(b.id === 'pick-' + cur.id)));
  $('#spec').innerHTML = specSheet(cur);
  view.dist = 17 + cur.length * 0.5; view.sweep = 0;
  $('#sweepbox').hidden = cur.id !== 'nf24'; $('#sweep').value = 20; $('#sweepv').textContent = '20°';
  $('#stores').checked = view.stores = true; $('#storesbox').hidden = !MODELS[cur.id].parts.some(p => p.name === 'stores');
  setCockpit(cur);
  compareTable();
  if (user) { try { history.replaceState(null, '', '#' + cur.id); } catch (e) {} }
}


// ---------- carrier ----------
const cvv = { yaw: 2.4, pitch: 0.42, dist: 430, spin: false, wing: true, drag: null, visible: true };
function carrierExtras() {
  if (!cvv.wing) return [];
  const nf = MODELS.nf24, D = CARRIER.deck, y = D.deck_height_m + 1.9, ex = [];
  for (const c of D.catapults.slice(0, 2)) ex.push({ model: nf, pos: [c.start[0], y, c.start[2] + 6], ry: 0 });
  for (const p of D.spawn_parking) ex.push({ model: nf, pos: [p[0], y, p[2]], ry: p[0] < 0 ? 2.3 : -2.3, sweep: 48 });
  // one jet on the glide slope, 600 m behind the target wire
  const td = D.landing.hook_touchdown, h = D.angled_deck.heading_offset_deg * Math.PI / 180, s = 600;
  ex.push({ model: nf, pos: [td[0] - Math.sin(h) * s, D.deck_height_m + s * Math.tan(D.landing.glide_slope_deg * Math.PI / 180) + 2, td[2] - Math.cos(h) * s], ry: h, sweep: 0 });
  return ex;
}
function drawCarrier() {
  const cv = $('#cvview'), w = cv.clientWidth, h = cv.clientHeight;
  if (!w) return;
  if (cv._w !== w || cv._h !== h) { cv._c = fitCanvas(cv, w, h); cv._w = w; cv._h = h; }
  const c = cv._c;
  const g = c.createLinearGradient(0, 0, 0, h); g.addColorStop(0, '#1d3446'); g.addColorStop(1, '#0f1c26');
  c.fillStyle = g; c.fillRect(0, 0, w, h);
  if (cvv.pitch < 1.2) { // sea to the horizon
    const hy = h / 2 - Math.tan(cvv.pitch) * Math.min(w, h) * 1.25;
    const sg = c.createLinearGradient(0, hy, 0, h); sg.addColorStop(0, '#3d6b84'); sg.addColorStop(1, '#173447');
    c.fillStyle = sg; c.fillRect(0, Math.max(0, hy), w, h);
  }
  Render.draw(c, w, h, CVM, { yaw: cvv.yaw, pitch: cvv.pitch, dist: cvv.dist, extra: carrierExtras(), under: ['hull', 'flight_deck'], tz: cvv.tz ?? -30, ty: cvv.ty ?? 0, tx: cvv.tx ?? 0 });
}
function initCarrier() {
  const cv = $('#cvview');
  cv.addEventListener('pointerdown', e => { cvv.drag = [e.clientX, e.clientY, cvv.yaw, cvv.pitch]; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', e => { if (!cvv.drag) return; cvv.yaw = cvv.drag[2] - (e.clientX - cvv.drag[0]) * 0.01; cvv.pitch = clamp(cvv.drag[3] + (e.clientY - cvv.drag[1]) * 0.008, -0.3, 1.5699); });
  cv.addEventListener('pointerup', () => cvv.drag = null);
  cv.addEventListener('wheel', e => { e.preventDefault(); cvv.dist = clamp(cvv.dist * (1 + Math.sign(e.deltaY) * 0.08), 120, 1400); }, { passive: false });
  const set = (y, p, d, tz, ty) => { Object.assign(cvv, { yaw: y, pitch: p, dist: d, tz, ty, tx: 0 }); };
  $('#cv-34').onclick = () => set(2.4, 0.42, 430, -30, 0);
  $('#cv-top').onclick = () => set(0, 1.5699, 470, 0, 0);
  $('#cv-side').onclick = () => set(-Math.PI / 2, 0.03, 420, 0, 0);
  $('#cv-app').onclick = () => { const D = CARRIER.deck, h = D.angled_deck.heading_offset_deg * Math.PI / 180, td = D.landing.hook_touchdown, s = 470; // chase view behind the jet on the glide slope
    set(h, 0.12, 160, td[2] - Math.cos(h) * s, D.deck_height_m + s * Math.tan(D.landing.glide_slope_deg * Math.PI / 180) + 2); cvv.tx = td[0] - Math.sin(h) * s; };
  $('#cv-wing').onchange = e => { cvv.wing = e.target.checked; };
  if ('IntersectionObserver' in window) new IntersectionObserver(es => { cvv.visible = es[0].isIntersecting; }).observe(cv);
  const S = CARRIER.spec, D = CARRIER.deck, tr = r => `<tr><th scope="row">${r[0]}</th><td>${r[1]}</td></tr>`;
  $('#cvtri').textContent = `${CARRIER.tris} triangles · ${S.length} m long · deck ${S.deck_width} m wide`;
  const rows = [['Length · flight deck width', `${S.length} m · ${S.deck_width} m`], ['Beam at waterline · draught', `${S.beam_wl} m · ${S.draft} m`],
    ['Displacement (full load)', `${fmt(S.displacement)} t`], ['Propulsion', S.power], ['Top speed', `${S.speed_kn}+ kn (${fmt(S.speed_kn * 1.852)} km/h)`],
    ['Air wing · crew', `about ${S.air_wing} aircraft · ${fmt(S.crew)}`],
    ['Catapults', `3 electromagnetic (2 bow, 1 waist), ${D.catapults[0].stroke_m} m stroke, ${S.cat_energy_mj} MJ, up to ${S.cat_max_end_kt} kt end speed`],
    ['Arresting gear', `3 wires, ${S.arrest_energy_mj} MJ, up to ${S.arrest_max_kt} kt engagement, ${S.wire_runout_m} m run-out`],
    ['Landing aid', `optical lens on the port side, ${D.landing.glide_slope_deg}° glide slope, aim at wire ${D.landing.target_wire}`]];
  const ct = CARRIER.compat.map(k => `<tr${k.id === 'nf24' ? ' class="cur"' : ''}><td>${k.name}</td><td>${fmt(k.need_kt)} / ${fmt(k.cat_kt)} kt</td><td>${fmt(k.vapp_kt)} kt</td><td>${k.arrest_mj.toFixed(1)} MJ</td><td>${k.verdict}</td></tr>`).join('');
  $('#cvspec').innerHTML = `
    <div class="eyebrow">Ship · CATOBAR · ${S.length} m</div>
    <h2 id="cv-title"><span class="des">${S.designation}</span> ${S.name}</h2>
    <p class="role">${S.role}</p><p class="blurb">${S.blurb}</p>
    <details open><summary>Deck layout</summary><ul class="aero">${S.design.map(d => `<li>${d}</li>`).join('')}</ul></details>
    <details open><summary>Specifications</summary><div class="tw"><table>${rows.map(tr).join('')}</table></div></details>
    <details open><summary>Which jets can use it <small>physics check, 30 kn wind over the deck</small></summary>
      <div class="tw"><table class="cmp"><thead><tr><th>Jet</th><th>Launch speed needed / catapult gives</th><th>Approach speed</th><th>Wire energy</th><th>Verdict</th></tr></thead><tbody>${ct}</tbody></table></div>
      <p class="note">Launch at full take-off mass; landing with 25% fuel and half the weapons. The wires take up to ${S.arrest_energy_mj} MJ and ${S.arrest_max_kt} kt.</p></details>`;
}

let last = performance.now();
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000); last = now;
  if (view.spin && !view.drag) view.yaw += dt * 0.35;
  drawViewer();
  if (cvv.visible) drawCarrier();
  if (sim) {
    readKeys(dt);
    for (let i = 0; i < 4; i++) sim.step(dt / 4);
    const cv = $('#cockpit');
    if (!cv._c) { cv._c = fitCanvas(cv, CW, CVH); }
    const c = cv._c; c.clearRect(0, 0, CW, CVH);
    ck.draw(c, sim);
    if (sim.auto) I.text(c, 'DEMO AUTOPILOT · drag the view or use the keys to fly', 640, 460, 11, 'rgba(255,255,255,0.7)', 'center', 'bold');
  }
  requestAnimationFrame(frame);
}

function boot() {
  buildPicker(); initViewer(); initCockpit(); initCarrier();
  const h = (location.hash || '').slice(1);
  select(JETS.some(j => j.id === h) ? h : JETS[0].id, false);
  requestAnimationFrame(frame);
}
(document.fonts && document.fonts.ready ? document.fonts.ready : Promise.resolve()).then(boot);
