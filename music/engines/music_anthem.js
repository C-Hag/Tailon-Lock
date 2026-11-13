// ---- anthem music start ----
// "Nose 2 Nose Anthem": original Tailon Lock track on the I–V–vi–IV progression (C–G–Am–F), played by the same live
// band as Nose 2 Nose: funk groove drums with fills, big synth bass, electric guitar lead and power chords, with synth
// pianos and brass behind. Intro, verse, chorus, then a whole-step lift to D major (D–A–Bm–G) for the last chorus.
const AnthemMusic = (() => {
  const BPM = 122, STEP = 60 / BPM / 4, BAR = 16;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  const midi = n => { const m = /^([A-G])([#b]?)(\d)$/.exec(n); return 12 * (+m[3] + 1) + NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const pc = n => { const m = /^([A-G])([#b]?)$/.exec(n); return NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const hz = m => 440 * Math.pow(2, (m - 69) / 12);
  const QUAL = { m: [0, 3, 7], M: [0, 4, 7], sus: [0, 5, 7], add9: [0, 4, 7, 14] };
  const NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  const LOOP_C = ['C:M', 'G:M', 'A:m', 'F:M'], LOOP_D = ['D:M', 'A:M', 'B:m', 'G:M'];
  const CHORDS = [...LOOP_C, ...LOOP_C, ...LOOP_C, ...LOOP_C, ...LOOP_C, ...LOOP_D, ...LOOP_D, 'D:M'];
  const KEYS = CHORDS.map((c, b) => b >= 20 ? 'D major' : 'C major');
  const SECTIONS = [['Intro', 1], ['Verse', 5], ['Chorus', 13], ['Lift', 21], ['Hit', 29]];
  const VERSE = 'E5:3 D5:1 C5:2 E5:2 G5:4 E5:4|D5:3 C5:1 B4:2 D5:2 G5:8|C5:3 B4:1 A4:2 C5:2 E5:4 A5:4|G5:2 F5:2 E5:2 C5:2 A4:4 -:4|' +
    'E5:3 D5:1 C5:2 E5:2 G5:4 C6:4|B5:3 A5:1 G5:2 D5:2 B5:8|C6:3 B5:1 A5:2 E5:2 C6:4 B5:2 A5:2|A5:4 G5:4 F5:4 D5:4';
  const HOOK = 'G5:2 C6:2 C6:2 D6:2 E6:6 C6:2|D6:4 B5:2 G5:2 D6:8|C6:2 A5:2 C6:2 E6:2 A6:6 G6:2|F6:4 E6:2 D6:2 C6:4 A5:4|' +
    'G5:2 C6:2 C6:2 D6:2 E6:6 G6:2|G6:4 F6:2 E6:2 D6:8|E6:2 C6:2 E6:2 A6:2 G6:4 E6:4|F6:4 E6:4 D6:4 C6:2 D6:2';
  const GROOVES = { A: { k: [0, 6, 8], g: [] }, B: { k: [0, 3, 6, 8, 14], g: [7], o: 14 }, C: { k: [0, 6, 8, 11, 14], g: [10, 15] }, D: { k: [0, 3, 8, 10, 14], g: [7, 13], o: 14 } };
  const FILLS = { f4: { 12: 's', 13: 'T0', 14: 'T1', 15: 'T2' }, f8: { 8: 's', 10: 's', 11: 's', 12: 'T0', 13: 'T1', 14: 'T2', 15: 'T3' },
    f16: { 8: 'T0', 9: 'T0', 10: 'T1', 11: 'T1', 12: 'T2', 13: 'T2', 14: 'T3', 15: 'T3' } };
  const CHUG = { 0: 1, 6: 1, 10: 2 };
  const PLAN = [
    { g: null, hats: 8, kick: [0, 8], pcs: { 0: 6 } }, { g: 'A', snare: [12], hats: 8, pcs: { 0: 6 } },
    { g: 'A', hats: 16, bass: true, pcs: { 0: 3, 8: 3 } }, { g: 'B', fill: 'f4', hats: 16, bass: true, pcs: { 0: 2, 4: 6 } },
    ...['A', 'B', 'C', 'B', 'A', 'B', 'C'].map(g => ({ g, hats: 16, bass: true })), { g: 'D', fill: 'f8', hats: 16, bass: true },
    ...['B', 'C', 'D', 'B', 'B', 'C', 'D'].map((g, k) => ({ g, hats: 16, bass: true, crash: k % 4 === 0, pcs: CHUG })), { g: 'B', roll: true, bass: true, pcs: { 0: 1, 4: 1, 8: 1, 12: 1 } },
    ...['B', 'C', 'D', 'B', 'B', 'C', 'D'].map((g, k) => ({ g, hats: 16, bass: true, crash: k % 4 === 0, pcs: CHUG })), { g: 'B', fill: 'f16', hats: 16, bass: true, pcs: CHUG },
    { g: null, crash: true, kick: [0], pcs: { 0: 16 } }
  ];
  const BASS = [[[0, 0, 2], [3, 12, 1], [6, 0, 1], [8, 0, 2], [10, 12, 1], [11, 0, 1], [13, 7, 1], [14, 12, 2]],
    [[0, 0, 2], [3, 0, 1], [4, 12, 1], [6, 0, 1], [8, 0, 1], [9, 0, 1], [11, 12, 1], [12, 7, 2], [14, 12, 1], [15, 7, 1]]];
  const chordOf = spec => { const [r, q] = spec.split(':'), root = pc(r); return { root, iv: QUAL[q], name: NAMES[root] + (q === 'M' ? '' : q) }; };
  const voiceBase = (root, lo) => lo + root - (root > 7 ? 12 : 0);
  const chordAt = (bar, i) => { const p = CHORDS[bar].split('/'); return chordOf(p[i >= 8 && p[1] ? 1 : 0]); };
  const RANK = ['X', 's', 'k', 'T', 'o', 'h'];
  const NB = CHORDS.length, steps = [], sections = SECTIONS.map(([name, b]) => ({ step: (b - 1) * BAR, name, key: KEYS[b - 1] }));
  for (let b = 0; b < NB; b++) {
    const pl = PLAN[b], gr = pl.g ? GROOVES[pl.g] : null;
    for (let i = 0; i < BAR; i++) {
      const ch = chordAt(b, i), hits = [], add = (d, v) => hits.push([d, v]);
      const fill = pl.fill && FILLS[pl.fill][i] !== undefined ? FILLS[pl.fill][i] : null, inFill = pl.fill && i >= Math.min(...Object.keys(FILLS[pl.fill]).map(Number));
      if (pl.roll) { add('s', 0.25 + 0.75 * i / 15); if (i % 4 === 0) add('k', 1); }
      else if (fill) { add(fill, fill === 's' ? 0.9 : 1); if (i === 15 || i % 4 === 0) add('k', 0.9); }
      else if (!inFill) {
        const kicks = pl.kick || (gr ? gr.k : []);
        if (kicks.includes(i)) add('k', i % 8 === 0 ? 1 : 0.8);
        if ((gr && !pl.snare && (i === 4 || i === 12)) || (pl.snare && pl.snare.includes(i))) { add('s', 1); add('c', 0.9); }
        if (gr && !pl.snare && gr.g.includes(i)) add('s', 0.28);
        if (gr && gr.o === i && pl.hats) add('o', 0.8);
        else if (pl.hats === 16 || (pl.hats === 8 && i % 2 === 0)) add('h', i % 4 === 0 ? 1 : i % 2 === 0 ? 0.75 : 0.45);
      }
      if (i === 0 && pl.crash) add('X', 1);
      let bass = null;
      if (pl.bass) { const ev = BASS[b % 2].find(e => e[0] === i); if (ev) bass = { m: voiceBase(ch.root, 36) + ev[1], len: ev[2], acc: i % 8 === 0 }; }
      else if (b === NB - 1 && i === 0) bass = { m: voiceBase(ch.root, 36), len: 24, acc: true };
      const pcLen = pl.pcs && pl.pcs[i];
      const lab = RANK.find(r => hits.some(([d]) => d[0] === r || (r === 's' && d === 'c')));
      steps.push({ sec: '', key: KEYS[b], bar: b, chord: ch, hits, drum: lab === 'T' ? 'T0' : lab || null, bass, groove: !!gr && !pl.roll && !inFill,
        power: pcLen ? { len: pcLen } : null, pad: i === 0 && b >= 12, bell: i === 0 && (b === 12 || b === 20 || b === 28) ? 1 : 0,
        jet: 0, jetLv: 1, riser: 0, p1: [], p2: [], gtr: null });
    }
  }
  // place "note[+note]:len" tokens into a voice starting at a step
  function place(field, at, str, T = 0) {
    let pos = at, prev = null;
    for (const tok of str.replace(/\|/g, ' ').trim().split(/\s+/)) {
      const [nm, l] = tok.split(':'), len = +l;
      if (nm !== '-') for (const n of nm.split('+')) {
        const m = midi(n) + T, ev = { m, len, bend: len >= 6, slide: prev !== null && Math.abs(m - prev) >= 5 ? prev : null };
        if (field === 'gtr') steps[pos].gtr = ev; else steps[pos][field].push(ev);
        prev = m;
      }
      pos += len;
    }
  }
  const at = bar => (bar - 1) * BAR;
  function alberti(field, bar, every) {
    for (let i = 0; i < BAR; i += every) { const ch = chordAt(bar - 1, i), k = [0, 2, 1, 2][(i / every) % 4]; steps[at(bar) + i][field].push({ m: voiceBase(ch.root, 48) + ch.iv[k], len: every }); }
  }
  function broken(field, bar) {
    for (let i = 0; i < BAR; i++) { const ch = chordAt(bar - 1, i), tones = [0, ch.iv[1], ch.iv[2], 12, 12 + ch.iv[1]], k = [0, 1, 2, 3, 4, 3, 2, 1][i % 8]; steps[at(bar) + i][field].push({ m: voiceBase(ch.root, 72) + tones[k], len: 1 }); }
  }
  // Intro: the pianos ripple through the progression while the drummer counts in under power chords
  for (let b = 1; b <= 4; b++) { broken('p1', b); alberti('p2', b, 2); }
  steps[0].jet = 7.5; steps[0].jetLv = 0.1;
  // Verse: the guitar sings low, the pianos keep a quiet Alberti pulse
  place('gtr', at(5), VERSE, -12);
  for (let b = 5; b <= 12; b++) alberti('p2', b, 2);
  // Chorus: the hook on guitar, pianos racing behind, power chords chugging
  place('gtr', at(13), HOOK, -12);
  for (let b = 13; b <= 20; b++) { broken('p1', b); alberti('p2', b, 2); }
  steps[at(19)].riser = 2 * BAR * STEP; steps[at(19)].jet = 2 * BAR * STEP / 0.58;
  // Lift: up a whole step to D major, the pianos double the hook an octave above the guitar
  place('gtr', at(21), HOOK, -10); place('p1', at(21), HOOK, 2);
  for (let b = 21; b <= 28; b++) alberti('p2', b, 2);
  place('gtr', at(29), 'D5:16'); place('p1', at(29), 'D6+A5+F#5:16'); place('p2', at(29), 'D4+D3:16');
  for (const s of sections) for (let i = s.step; i < steps.length; i++) steps[i].sec = s.name;
  const LOOP = sections[1].step;


  let ctx = null, master = null, revIn = null, gtrIn = null, noise = null, timer = null, next = 0, idx = 0, playing = false, vol = 0.6, vel = 1;
  const mute = { guitar: false, synths: false, bass: false, drums: false };
  const listeners = [];
  const rnd = () => Math.random() + Math.random() - 1;
  function impulse(sec, decay) {
    const len = Math.floor(ctx.sampleRate * sec), b = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, decay); }
    return b;
  }
  function build(dest) {
    master = ctx.createGain(); master.gain.value = vol;
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -16; comp.ratio.value = 4; comp.attack.value = 0.004; comp.release.value = 0.15;
    master.connect(comp); comp.connect(dest);
    const rev = ctx.createConvolver(); rev.buffer = impulse(1.8, 3.5);
    revIn = ctx.createGain(); revIn.connect(rev);
    const revOut = ctx.createGain(); revOut.gain.value = 0.4; rev.connect(revOut); revOut.connect(master);
    // electric guitar: drive, cabinet-like filtering, compression, a short slapback and a touch of room
    gtrIn = ctx.createGain(); gtrIn.gain.value = 3.4;
    const shaper = ctx.createWaveShaper(), curve = new Float32Array(2049);
    for (let i = 0; i < 2049; i++) { const x = i / 1024 - 1; curve[i] = Math.tanh(x * 7) * 0.9; }
    shaper.curve = curve; shaper.oversample = '4x';
    const hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 110;
    const mid = ctx.createBiquadFilter(); mid.type = 'peaking'; mid.frequency.value = 1400; mid.Q.value = 0.8; mid.gain.value = 6;
    const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 4800;
    const gc = ctx.createDynamicsCompressor(); gc.threshold.value = -30; gc.ratio.value = 8; gc.attack.value = 0.004; gc.release.value = 0.25;
    const gout = ctx.createGain(); gout.gain.value = 0.34;
    gtrIn.connect(shaper); shaper.connect(hp); hp.connect(mid); mid.connect(lp); lp.connect(gc); gc.connect(gout); gout.connect(master);
    const dly = ctx.createDelay(1); dly.delayTime.value = 0.11;
    const dg = ctx.createGain(); dg.gain.value = 0.22; gout.connect(dly); dly.connect(dg); dg.connect(master);
    const gs = ctx.createGain(); gs.gain.value = 0.25; gout.connect(gs); gs.connect(revIn);
    const len = ctx.sampleRate * 2, nb = ctx.createBuffer(1, len, ctx.sampleRate), d = nb.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    noise = nb;
  }
  const out = (node, dry, wet) => { const g = ctx.createGain(); g.gain.value = dry * vel; node.connect(g); g.connect(master); if (wet) { const w = ctx.createGain(); w.gain.value = wet * vel; node.connect(w); w.connect(revIn); } };
  // layered detuned brass: chord stab or sustained chord
  function brassChord(t, ch, level, dur, sustained) {
    const base = voiceBase(ch.root, 52), lp = ctx.createBiquadFilter(), g = ctx.createGain(), oscs = [];
    lp.type = 'lowpass'; lp.Q.value = 1.6;
    lp.frequency.setValueAtTime(350, t); lp.frequency.exponentialRampToValueAtTime(3600, t + 0.06); lp.frequency.exponentialRampToValueAtTime(sustained ? 1700 : 900, t + 0.3);
    const a = sustained ? 0.25 : 0.02, pk = 0.05 * level;
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + a);
    if (sustained) { g.gain.setValueAtTime(pk * 0.8, t + dur - 0.2); g.gain.linearRampToValueAtTime(0.0001, t + dur + 0.3); }
    else g.gain.exponentialRampToValueAtTime(0.0001, t + Math.min(dur, 0.32));
    lp.connect(g); out(g, 1, 0.4);
    for (const iv of [...ch.iv.slice(0, 3), 12]) for (const det of [-13, -4, 5, 14]) {
      const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(base + iv); o.detune.value = det; o.connect(lp); oscs.push(o);
    }
    const sub = ctx.createOscillator(); sub.type = 'sawtooth'; sub.frequency.value = hz(base - 12); sub.connect(lp); oscs.push(sub);
    const end = t + (sustained ? dur + 0.4 : 0.4);
    for (const o of oscs) { o.start(t); o.stop(end); }
  }
  function bellChord(t, ch) {
    const base = voiceBase(ch.root, 72);
    ch.iv.slice(0, 3).forEach((iv, k) => {
      const tt = t + k * 0.04, f = hz(base + iv), car = ctx.createOscillator(), mod = ctx.createOscillator(), mg = ctx.createGain(), g = ctx.createGain();
      car.frequency.value = f; mod.frequency.value = f * 3.5;
      mg.gain.setValueAtTime(f * 2.4, tt); mg.gain.exponentialRampToValueAtTime(f * 0.15, tt + 1.2);
      mod.connect(mg); mg.connect(car.frequency); car.connect(g);
      g.gain.setValueAtTime(0.0001, tt); g.gain.linearRampToValueAtTime(0.035, tt + 0.004); g.gain.exponentialRampToValueAtTime(0.0001, tt + 2.5);
      out(g, 0.7, 0.8); car.start(tt); mod.start(tt); car.stop(tt + 2.6); mod.stop(tt + 2.6);
    });
  }
  const BASS_CURVE = new Float32Array(513); for (let i = 0; i < 513; i++) BASS_CURVE[i] = Math.tanh((i / 256 - 1) * 2.2);
  // big funk bass: saw and square through a snappy resonant filter, a sine sub underneath, lightly saturated
  function bigBass(t, n, acc) {
    const dur = n.len * STEP * 0.9, f = hz(n.m), lp = ctx.createBiquadFilter(), sat = ctx.createWaveShaper(), g = ctx.createGain(), oscs = [];
    sat.curve = BASS_CURVE;
    lp.type = 'lowpass'; lp.Q.value = 9; lp.frequency.setValueAtTime(acc ? 3200 : 2200, t); lp.frequency.exponentialRampToValueAtTime(260, t + 0.13);
    for (const [type, mult, det] of [['sawtooth', 1, -6], ['square', 1, 6], ['sawtooth', 2, 0]]) { const o = ctx.createOscillator(); o.type = type; o.frequency.value = f * mult; o.detune.value = det; o.connect(lp); oscs.push(o); }
    lp.connect(sat); sat.connect(g);
    const sub = ctx.createOscillator(), sg = ctx.createGain(); sub.type = 'sine'; sub.frequency.value = f; sg.gain.value = 2.2; sub.connect(sg); sg.connect(g); oscs.push(sub);
    const pk = acc ? 0.15 : 0.12;
    g.gain.value = 0; g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + 0.004); g.gain.setTargetAtTime(pk * 0.6, t + 0.02, 0.08); g.gain.setTargetAtTime(0.0001, t + dur, 0.025);
    out(g, 1, 0.02);
    for (const o of oscs) { o.start(t); o.stop(t + dur + 0.15); }
  }
  // electric guitar note: two detuned saws and a square into the amp chain, with bends, slides and vibrato
  function guitar(t, n, dur) {
    const f = hz(n.m), g = ctx.createGain(), oscs = [];
    for (const [type, det, lv] of [['sawtooth', -6, 0.5], ['sawtooth', 7, 0.5], ['square', 0, 0.25]]) {
      const o = ctx.createOscillator(), og = ctx.createGain(); o.type = type; o.detune.value = det + rnd() * 4; og.gain.value = lv;
      if (n.slide !== null && n.slide !== undefined) { o.frequency.setValueAtTime(hz(n.slide), t); o.frequency.exponentialRampToValueAtTime(f, t + 0.05); }
      else if (n.bend) { const lo = f * Math.pow(2, -2 / 12); o.frequency.setValueAtTime(lo, t); o.frequency.setValueAtTime(lo, t + 0.05); o.frequency.exponentialRampToValueAtTime(f, t + 0.18); }
      else o.frequency.setValueAtTime(f, t);
      o.connect(og); og.connect(g); oscs.push(o);
    }
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.4 + rnd() * 0.4;
    vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.3); vg.gain.linearRampToValueAtTime(f * 0.011, t + Math.max(0.4, dur * 0.7));
    vib.connect(vg); for (const o of oscs) vg.connect(o.frequency);
    const pk = 0.5 * vel;
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + 0.008); g.gain.setValueAtTime(pk, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.12);
    g.connect(gtrIn);
    for (const o of [...oscs, vib]) { o.start(t); o.stop(t + dur + 0.2); }
  }
  // power chord: root, fifth and octave chugged through the same amp
  function powerChord(t, ch, dur) {
    const base = voiceBase(ch.root, 40), g = ctx.createGain(), oscs = [];
    for (const iv of [0, 7, 12]) for (const det of [-8, 8]) { const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(base + iv); o.detune.value = det + rnd() * 5; o.connect(g); oscs.push(o); }
    const pk = 0.3 * vel;
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + 0.006); g.gain.setTargetAtTime(pk * 0.7, t + 0.05, 0.2); g.gain.setTargetAtTime(0.0001, t + dur, 0.06);
    g.connect(gtrIn);
    for (const o of oscs) { o.start(t); o.stop(t + dur + 0.4); }
  }
  // synth piano: two slightly detuned FM voices with a hammer-like decaying brightness, panned per piano
  function piano(t, n, dur, pan) {
    const f = hz(n.m), g = ctx.createGain(), p = ctx.createStereoPanner ? ctx.createStereoPanner() : null, oscs = [];
    const ring = Math.min(2.2, 0.35 + 140 / f), lv = 0.075 * Math.min(1.3, Math.max(0.55, 330 / f));
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(lv, t + 0.004); g.gain.setTargetAtTime(0.0001, t + 0.004, ring / 4);
    g.gain.setTargetAtTime(0.0001, t + dur, 0.05);
    if (p) { p.pan.value = pan; g.connect(p); out(p, 1, 0.3); } else out(g, 1, 0.3);
    for (const det of [-4, 4]) {
      const car = ctx.createOscillator(), mod = ctx.createOscillator(), mg = ctx.createGain();
      car.frequency.value = f; car.detune.value = det; mod.frequency.value = f;
      mg.gain.setValueAtTime(f * 1.8, t); mg.gain.exponentialRampToValueAtTime(f * 0.25, t + 0.35);
      mod.connect(mg); mg.connect(car.frequency); car.connect(g); oscs.push(car, mod);
    }
    const end = t + dur + 0.4;
    for (const o of oscs) { o.start(t); o.stop(end); }
  }
  // riser: swept noise that climbs into the downbeat
  function riser(t, L) {
    const n = ctx.createBufferSource(), bp = ctx.createBiquadFilter(), g = ctx.createGain();
    n.buffer = noise; n.loop = true; bp.type = 'bandpass'; bp.Q.value = 2.5;
    bp.frequency.setValueAtTime(300, t); bp.frequency.exponentialRampToValueAtTime(7000, t + L);
    g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.2, t + L); g.gain.linearRampToValueAtTime(0.0001, t + L + 0.05);
    n.connect(bp); bp.connect(g); out(g, 1, 0.4); n.start(t); n.stop(t + L + 0.1);
  }
  // jet flyby: filtered noise roar + turbine whine with a doppler drop as it passes, panned left to right
  function jet(t, L, lv) {
    const mid = t + L * 0.58, end = t + L;
    const pan = ctx.createStereoPanner ? ctx.createStereoPanner() : null, bus = ctx.createGain(); bus.gain.value = lv;
    if (pan) { pan.pan.setValueAtTime(-0.9, t); pan.pan.linearRampToValueAtTime(0.9, end); bus.connect(pan); out(pan, 1, 0.35); } else out(bus, 1, 0.35);
    const roar = ctx.createBufferSource(), bp = ctx.createBiquadFilter(), rg = ctx.createGain();
    roar.buffer = noise; roar.loop = true; bp.type = 'bandpass'; bp.Q.value = 0.9;
    bp.frequency.setValueAtTime(700, t); bp.frequency.exponentialRampToValueAtTime(2600, mid); bp.frequency.exponentialRampToValueAtTime(380, end);
    rg.gain.setValueAtTime(0.0001, t); rg.gain.exponentialRampToValueAtTime(0.5, mid); rg.gain.exponentialRampToValueAtTime(0.0001, end);
    roar.connect(bp); bp.connect(rg); rg.connect(bus);
    const rum = ctx.createBufferSource(), lp = ctx.createBiquadFilter(), ug = ctx.createGain();
    rum.buffer = noise; rum.loop = true; lp.type = 'lowpass'; lp.frequency.value = 180;
    ug.gain.setValueAtTime(0.0001, t); ug.gain.exponentialRampToValueAtTime(0.9, mid + 0.15); ug.gain.exponentialRampToValueAtTime(0.0001, end);
    rum.connect(lp); lp.connect(ug); ug.connect(bus);
    const wg = ctx.createGain(), wbp = ctx.createBiquadFilter(); wbp.type = 'bandpass'; wbp.frequency.value = 2600; wbp.Q.value = 2;
    wg.gain.setValueAtTime(0.0001, t); wg.gain.exponentialRampToValueAtTime(0.06, mid - 0.1); wg.gain.exponentialRampToValueAtTime(0.0001, end);
    wbp.connect(wg); wg.connect(bus);
    const whines = [];
    for (const [f0, det] of [[2300, 0], [3450, 8]]) {
      const o = ctx.createOscillator(); o.type = 'sawtooth'; o.detune.value = det;
      o.frequency.setValueAtTime(f0 * 1.12, t); o.frequency.setValueAtTime(f0 * 1.1, mid - 0.15); o.frequency.exponentialRampToValueAtTime(f0 * 0.82, mid + 0.25); o.frequency.exponentialRampToValueAtTime(f0 * 0.78, end);
      o.connect(wbp); whines.push(o);
    }
    for (const s of [roar, rum, ...whines]) { s.start(t); s.stop(end + 0.05); }
  }
  // drum kit: deep kick, fat snare with a layered clap, hats, open hat, toms and crash, all scaled by velocity
  function drum(t, d, v) {
    const nz = (len, type, freq, lv, wet = 0.1, q = 0.8, at = t) => { const n = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain(); n.buffer = noise; f.type = type; f.frequency.value = freq; f.Q.value = q; g.gain.setValueAtTime(lv * v, at); g.gain.exponentialRampToValueAtTime(0.0001, at + len); n.connect(f); f.connect(g); out(g, 1, wet); n.start(at, Math.random()); n.stop(at + len + 0.05); };
    const tone = (f0, f1, len, lv, wet = 0.05, sweep = 0.05, type = 'sine') => { const o = ctx.createOscillator(), g = ctx.createGain(); o.type = type; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + sweep); g.gain.setValueAtTime(lv * v, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len); o.connect(g); out(g, 1, wet); o.start(t); o.stop(t + len + 0.05); };
    if (d === 'k') { tone(160, 46, 0.5, 0.95, 0.03, 0.06); tone(60, 42, 0.35, 0.4, 0, 0.1); nz(0.012, 'highpass', 2500, 0.35, 0); }
    else if (d === 's') { tone(210, 175, 0.12, 0.45, 0.25, 0.03, 'triangle'); nz(0.24, 'bandpass', 1900, 0.55, 0.45, 0.6); nz(0.14, 'highpass', 5000, 0.28, 0.3, 0.5); }
    else if (d === 'c') { for (const k of [0, 0.011, 0.023]) nz(k ? 0.012 : 0.16, 'bandpass', 1250, 0.4, 0.45, 1.2, t + k); }
    else if (d === 'X') { nz(2.2, 'highpass', 5000, 0.2, 0.5, 0.5); tone(100, 45, 0.5, 0.6, 0.1, 0.05); }
    else if (d === 'h') nz(0.035, 'highpass', 8000, 0.09, 0.03);
    else if (d === 'o') nz(0.28, 'highpass', 7500, 0.08, 0.1);
    else if (d[0] === 'T') { const k = +d.slice(1); tone(230 - k * 35, 90 - k * 10, 0.4, 0.75, 0.3, 0.14); nz(0.03, 'bandpass', 1200, 0.2, 0.1); }
  }
  // live feel: every voice is nudged in time and level, the snare sits slightly behind the beat,
  // the bass slightly ahead, and the drummer drops in the odd extra ghost note
  function schedule(i, t) {
    const s = steps[i], j = ms => ms / 1000 * rnd();
    try {
      vel = 1;
      if (s.jet && !mute.drums) jet(t, s.jet, s.jetLv);
      if (s.riser && !mute.drums) riser(t, s.riser);
      vel = 0.75;
      if (s.pad && !mute.synths) brassChord(t + 0.01, s.chord, 0.6, BAR * STEP, true);
      if (s.bell && !mute.synths) bellChord(t, s.chord);
      if (!mute.drums) {
        for (const [d, v] of s.hits) { vel = 1; drum(Math.max(0, t + j(d === 's' || d === 'c' ? 4 : 6) + (d === 's' || d === 'c' ? 0.006 : 0)), d, v * (0.88 + 0.24 * Math.random())); }
        if (s.groove && i % 2 === 1 && !s.hits.some(([d]) => d === 's') && Math.random() < 0.1) { vel = 1; drum(t + 0.008, 's', 0.18 + 0.1 * Math.random()); }
      }
      if (s.bass && !mute.bass) { vel = 0.9 + 0.2 * Math.random(); bigBass(Math.max(0, t - 0.003 + j(4)), s.bass, s.bass.acc); }
      if (s.power && !mute.guitar) { vel = 0.85 + 0.2 * Math.random(); powerChord(t + j(8), s.chord, s.power.len * STEP * 0.9); }
      if (s.gtr && !mute.guitar) { vel = 0.85 + 0.2 * Math.random(); guitar(t + j(9), s.gtr, s.gtr.len * STEP * 0.96); }
      if (!mute.synths) {
        for (const n of s.p1) { vel = 0.6 + 0.12 * Math.random(); piano(t + j(7), n, n.len * STEP * 0.95, -0.6); }
        for (const n of s.p2) { vel = 0.6 + 0.12 * Math.random(); piano(t + j(7), n, n.len * STEP * 0.95, 0.6); }
      }
    } catch (e) {}
    vel = 1;
    if (listeners.length) setTimeout(() => { for (const fn of listeners) fn(i, s); }, Math.max(0, (t - ctx.currentTime) * 1000));
  }
  function tick() {
    if (!playing || !ctx) return;
    while (next < ctx.currentTime + 0.2) {
      schedule(idx, next);
      next += STEP; idx++;
      if (idx >= steps.length) idx = LOOP;
    }
  }
  return {
    steps, sections, STEP, mute,
    get playing() { return playing; },
    onStep(fn) { listeners.push(fn); },
    start(audioCtx, dest) {
      if (playing) return;
      if (ctx !== audioCtx) { ctx = audioCtx; build(dest); }
      master.gain.cancelScheduledValues(ctx.currentTime); master.gain.setValueAtTime(vol, ctx.currentTime);
      playing = true; idx = 0; next = ctx.currentTime + 0.1;
      timer = setInterval(tick, 25); tick();
    },
    stop() { playing = false; clearInterval(timer); timer = null; if (master && ctx) master.gain.setTargetAtTime(0, ctx.currentTime, 0.08); },
    volume(v) { vol = v; if (master && ctx && playing) master.gain.setTargetAtTime(v, ctx.currentTime, 0.3); },
    renderAll(audioCtx, dest) {
      ctx = audioCtx; build(dest);
      const saved = listeners.splice(0);
      for (let i = 0; i < steps.length; i++) schedule(i, 0.05 + i * STEP);
      listeners.push(...saved);
      return 0.05 + steps.length * STEP + 3;
    }
  };
})();
// ---- anthem music end ----
