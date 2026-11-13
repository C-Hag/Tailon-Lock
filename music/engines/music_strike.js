// ---- strike music start ----
// "Carrier Dawn (Strike)": action-paced original arrangement of the Tailon Lock theme. A jet flyby made from filtered
// noise and a doppler-shifted turbine whine, layered detuned synth brass, DMX/Linn-style march drums, a 16th-note
// sequencer bass, the overdriven guitar lead and FM bells. Keys: E minor, F# minor, tritone jump to C minor, E major.
const StrikeMusic = (() => {
  const BPM = 124, STEP = 60 / BPM / 4, BAR = 16;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  const midi = n => { const m = /^([A-G])([#b]?)(\d)$/.exec(n); return 12 * (+m[3] + 1) + NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const pc = n => { const m = /^([A-G])([#b]?)$/.exec(n); return NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const hz = m => 440 * Math.pow(2, (m - 69) / 12);
  const QUAL = { m: [0, 3, 7], M: [0, 4, 7], sus: [0, 5, 7], add9: [0, 4, 7, 14], m7: [0, 3, 7, 10] };
  const NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  const MEL_A = 'E5:4 F#5:2 G5:2 B5:6 A5:2|G5:4 E5:4 C5:2 D5:2 E5:4|F#5:4 A5:2 D6:6 C#6:2 A5:2|B5:12 -:4|E5:4 F#5:2 G5:2 B5:4 E6:4|D6:2 C6:2 B5:4 G5:4 E5:4|D5:4 G5:4 B5:4 D6:4|A5:6 G5:2 F#5:8';
  const MEL_B = 'G5:2 G5:2 A5:2 B5:2 C6:4 B5:2 A5:2|A5:4 F#5:4 D5:4 F#5:2 A5:2|B5:6 A5:2 F#5:4 D5:4|E5:8 G5:4 B5:4|C6:4 B5:2 A5:2 E5:4 A5:4|G5:4 E5:2 G5:2 C6:8|D6:4 C6:2 B5:2 A5:4 F#5:4|A5:16';
  const FANFARE = 'E5:3 E5:3 E5:2 G5:2 B5:6|C6:3 B5:3 A5:2 G5:4 B5:4';
  const FINALE = 'B5:4 E6:4 G#6:4 B6:4|A6:8 G#6:4 F#6:4|G#6:4 F#6:4 E6:4 D#6:4|E6:16';
  const A_CH = ['E:m', 'C:M', 'D:M', 'E:m', 'E:m', 'C:M', 'G:M', 'D:M'];
  const B_CH = ['C:M', 'D:M', 'B:m', 'E:m', 'A:m', 'C:M', 'D:M', 'D:M'];
  const SECTIONS = [
    { name: 'Flyby', key: 'E minor', chords: ['E:m', 'E:m'], T: 0, style: 'flyby', jet: true },
    { name: 'Fanfare', key: 'E minor', chords: ['E:m', 'C:M'], T: 0, style: 'fanfare', brass: FANFARE, O: -12 },
    { name: 'Drive', key: 'E minor', chords: ['E:m', 'B:sus'], T: 0, style: 'drive' },
    { name: 'Theme A', key: 'E minor', chords: A_CH, T: 0, style: 'theme', gtr: MEL_A, O: -12 },
    { name: 'Theme B', key: 'F# minor', chords: B_CH, T: 2, style: 'brass', brass: MEL_B, O: -12 },
    { name: 'Flyby', key: 'C minor', chords: ['G:sus'], T: 0, style: 'pass', jet: true },
    { name: 'Tritone jump', key: 'C minor', chords: A_CH, T: 8, style: 'climax', gtr: MEL_A, brass: MEL_A, O: -12, bo: -24 },
    { name: 'Finale', key: 'E major', chords: ['E:add9', 'A:M', 'B:M', 'E:add9'], T: 0, style: 'finale', gtr: FINALE, brass: FINALE, O: -12, bo: -24 }
  ];
  const MARCH = 'k-g-s-gkk-g-s-gg', FILL = 'k-g-s-gkTTTTssss', DRIVE = 'k-gkk-g-k-gkk-ss';
  const BSEQ = [0, 0, 12, 0, 7, 0, 12, 0, 0, 0, 12, 0, 7, 12, 7, 0];
  const STABS = { fanfare: { 0: 1, 8: 0.6 }, drive: { 0: 1, 3: 0.7, 6: 0.8, 10: 0.7, 12: 0.8 }, theme: { 0: 1, 6: 0.7, 10: 0.7 }, climax: { 0: 1, 3: 0.7, 6: 0.8, 10: 0.7, 12: 0.8, 14: 0.6 } };
  function chordOf(spec, T) {
    const [r, q] = spec.split(':'), root = (pc(r) + T + 12) % 12;
    return { root, iv: QUAL[q], name: NAMES[root] + (q === 'M' ? '' : q) };
  }
  const steps = [], sections = [];
  for (const sec of SECTIONS) {
    sections.push({ step: steps.length, name: sec.name, key: sec.key });
    const s0 = steps.length, n = sec.chords.length, st = sec.style;
    sec.chords.forEach((spec, bi) => {
      const ch = chordOf(spec, sec.T), last = bi === n - 1;
      for (let i = 0; i < BAR; i++) {
        let drum = null, hat = false;
        if (st === 'fanfare') drum = { 0: 'X', 4: 'T0', 6: 'T1', 8: 'k', 12: 'T2', 14: 'T3' }[i] || null;
        else if (st === 'drive') { drum = (last ? 'k-gkk-g-sgsgssss' : DRIVE)[i]; hat = true; }
        else if (st === 'theme' || st === 'brass' || st === 'climax') { drum = (last ? FILL : MARCH)[i]; hat = i % 2 === 1 || st === 'climax'; if (i === 0 && bi % 4 === 0) drum = 'X'; }
        else if (st === 'pass') drum = i === 0 ? 'k' : i >= 12 ? 's' : null;
        else if (st === 'finale') { drum = bi === 3 ? (i === 0 ? 'X' : null) : (MARCH[i] === 'k' && i === 0 ? 'X' : MARCH[i]); hat = bi < 3 && i % 2 === 1; }
        if (drum === '-') drum = null;
        let tomIdx = 0;
        if (drum === 'T') { tomIdx = (last ? FILL : MARCH).slice(0, i).split('T').length - 1; drum = 'T' + tomIdx; }
        const stab = STABS[st] ? STABS[st][i] || 0 : 0;
        const bass = st === 'flyby' || st === 'fanfare' ? (i === 0 ? 'w' : null) : st === 'pass' ? (i === 0 ? 'w' : null) : st === 'finale' ? (i % 4 === 0 ? 'q' : null) : 's';
        steps.push({ sec: sec.name, key: sec.key, bar: bi, chord: ch, drum, hat, stab, bass, bseq: BSEQ[i], pad: i === 0 && (st === 'flyby' || st === 'brass' || st === 'finale' || st === 'pass'),
          bell: i === 0 && (st === 'flyby' || st === 'brass' || st === 'finale') ? 1 : 0, jet: !!sec.jet && i === 0 && bi === 0 ? n * BAR * STEP : 0, gtr: null, lead: null });
      }
    });
    for (const [field, mel, oct] of [['gtr', sec.gtr, sec.O || 0], ['lead', sec.brass, sec.bo !== undefined ? sec.bo : sec.O || 0]]) {
      if (!mel) continue;
      let pos = s0, prev = null;
      for (const tok of mel.replace(/\|/g, ' ').split(/\s+/)) {
        const [nm, l] = tok.split(':'), len = +l;
        if (nm !== '-') { const m = midi(nm) + sec.T + oct; steps[pos][field] = { m, len, bend: len >= 6, slide: prev !== null && Math.abs(m - prev) >= 5 ? prev : null }; prev = m; }
        pos += len;
      }
    }
  }
  const LOOP = sections[2].step;

  let ctx = null, master = null, revIn = null, gtrIn = null, noise = null, timer = null, next = 0, idx = 0, playing = false, vol = 0.6;
  const mute = { guitar: false, brass: false, bass: false, drums: false };
  const listeners = [];
  function impulse(sec, decay) {
    const len = Math.floor(ctx.sampleRate * sec), b = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, decay); }
    return b;
  }
  function build(dest) {
    master = ctx.createGain(); master.gain.value = vol;
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 3.5; comp.attack.value = 0.006; comp.release.value = 0.2;
    master.connect(comp); comp.connect(dest);
    const rev = ctx.createConvolver(); rev.buffer = impulse(2.4, 3);
    revIn = ctx.createGain(); revIn.connect(rev);
    const revOut = ctx.createGain(); revOut.gain.value = 0.45; rev.connect(revOut); revOut.connect(master);
    gtrIn = ctx.createGain(); gtrIn.gain.value = 3.2;
    const shaper = ctx.createWaveShaper(), curve = new Float32Array(2048);
    for (let i = 0; i < 2048; i++) { const x = i / 1024 - 1; curve[i] = Math.tanh(x * 6) * 0.9; }
    shaper.curve = curve; shaper.oversample = '4x';
    const hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 140;
    const mid = ctx.createBiquadFilter(); mid.type = 'peaking'; mid.frequency.value = 1500; mid.Q.value = 0.9; mid.gain.value = 5;
    const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 4200;
    const gc = ctx.createDynamicsCompressor(); gc.threshold.value = -30; gc.ratio.value = 8; gc.attack.value = 0.005; gc.release.value = 0.3;
    const gout = ctx.createGain(); gout.gain.value = 0.17;
    gtrIn.connect(shaper); shaper.connect(hp); hp.connect(mid); mid.connect(lp); lp.connect(gc); gc.connect(gout); gout.connect(master);
    const dly = ctx.createDelay(1); dly.delayTime.value = STEP * 3;
    const fb = ctx.createGain(); fb.gain.value = 0.28; const dg = ctx.createGain(); dg.gain.value = 0.3;
    gout.connect(dly); dly.connect(fb); fb.connect(dly); dly.connect(dg); dg.connect(master); dg.connect(revIn);
    const gs = ctx.createGain(); gs.gain.value = 0.3; gout.connect(gs); gs.connect(revIn);
    const len = ctx.sampleRate * 2, nb = ctx.createBuffer(1, len, ctx.sampleRate), d = nb.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    noise = nb;
  }
  const out = (node, dry, wet) => { const g = ctx.createGain(); g.gain.value = dry; node.connect(g); g.connect(master); if (wet) { const w = ctx.createGain(); w.gain.value = wet; node.connect(w); w.connect(revIn); } };
  const voiceBase = (root, lo) => lo + root - (root > 7 ? 12 : 0);
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
  function brassLead(t, n, dur) {
    const f = hz(n.m), lp = ctx.createBiquadFilter(), g = ctx.createGain(), oscs = [];
    lp.type = 'lowpass'; lp.Q.value = 1.2;
    lp.frequency.setValueAtTime(500, t); lp.frequency.exponentialRampToValueAtTime(4200, t + 0.07); lp.frequency.exponentialRampToValueAtTime(2200, t + 0.35);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.09, t + 0.03); g.gain.setValueAtTime(0.08, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.15);
    lp.connect(g); out(g, 1, 0.35);
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.2;
    vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.3); vg.gain.linearRampToValueAtTime(f * 0.007, t + Math.max(0.4, dur * 0.8)); vib.connect(vg);
    for (const [det, mult] of [[-12, 1], [-3, 1], [6, 1], [13, 1], [0, 0.5]]) {
      const o = ctx.createOscillator(); o.type = 'sawtooth'; o.detune.value = det;
      const ff = f * mult;
      if (n.slide !== null && n.slide !== undefined) { o.frequency.setValueAtTime(hz(n.slide) * mult, t); o.frequency.exponentialRampToValueAtTime(ff, t + 0.05); }
      else { o.frequency.setValueAtTime(ff * 0.985, t); o.frequency.exponentialRampToValueAtTime(ff, t + 0.07); }
      vg.connect(o.frequency); o.connect(lp); oscs.push(o);
    }
    for (const o of [...oscs, vib]) { o.start(t); o.stop(t + dur + 0.2); }
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
  function seqBass(t, ch, s) {
    const m = voiceBase(ch.root, 28) + s.bseq, dur = s.bass === 'w' ? BAR * STEP * 2 : s.bass === 'q' ? STEP * 3.8 : STEP * 0.9;
    const o = ctx.createOscillator(), o2 = ctx.createOscillator(), lp = ctx.createBiquadFilter(), g = ctx.createGain();
    o.type = 'sawtooth'; o2.type = 'square'; o.frequency.value = hz(m + 12); o2.frequency.value = hz(m); o2.detune.value = 4;
    lp.type = 'lowpass'; lp.Q.value = 5; lp.frequency.setValueAtTime(s.bass === 'w' ? 300 : 1800, t); lp.frequency.exponentialRampToValueAtTime(s.bass === 'w' ? 900 : 240, t + (s.bass === 'w' ? dur * 0.6 : 0.1));
    const pk = s.bass === 's' ? 0.09 : 0.1;
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + (s.bass === 'w' ? 0.6 : 0.004)); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(lp); o2.connect(lp); lp.connect(g); out(g, 1, 0.03);
    for (const x of [o, o2]) { x.start(t); x.stop(t + dur + 0.05); }
  }
  function guitar(t, n, dur) {
    const f = hz(n.m), g = ctx.createGain(), oscs = [];
    for (const [type, det, lv] of [['sawtooth', -5, 0.5], ['sawtooth', 6, 0.5], ['square', 0, 0.25]]) {
      const o = ctx.createOscillator(), og = ctx.createGain(); o.type = type; o.detune.value = det; og.gain.value = lv;
      if (n.slide !== null && n.slide !== undefined) { o.frequency.setValueAtTime(hz(n.slide), t); o.frequency.exponentialRampToValueAtTime(f, t + 0.05); }
      else if (n.bend) { const lo = f * Math.pow(2, -2 / 12); o.frequency.setValueAtTime(lo, t); o.frequency.setValueAtTime(lo, t + 0.04); o.frequency.exponentialRampToValueAtTime(f, t + 0.15); }
      else o.frequency.setValueAtTime(f, t);
      o.connect(og); og.connect(g); oscs.push(o);
    }
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.6;
    vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.28); vg.gain.linearRampToValueAtTime(f * 0.009, t + Math.max(0.38, dur * 0.7));
    vib.connect(vg); for (const o of oscs) vg.connect(o.frequency);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.5, t + 0.01); g.gain.setValueAtTime(0.5, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.15);
    g.connect(gtrIn);
    for (const o of [...oscs, vib]) { o.start(t); o.stop(t + dur + 0.2); }
  }
  // jet flyby: filtered noise roar + turbine whine with a doppler drop as it passes, panned left to right
  function jet(t, L) {
    const mid = t + L * 0.58, end = t + L;
    const pan = ctx.createStereoPanner ? ctx.createStereoPanner() : null, bus = ctx.createGain(); bus.gain.value = 1;
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
  function drum(t, d) {
    const nz = (len, type, freq, lv, wet = 0.1, q = 0.8) => { const n = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain(); n.buffer = noise; f.type = type; f.frequency.value = freq; f.Q.value = q; g.gain.setValueAtTime(lv, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len); n.connect(f); f.connect(g); out(g, 1, wet); n.start(t, Math.random()); n.stop(t + len + 0.05); };
    const tone = (f0, f1, len, lv, wet = 0.05, sweep = 0.05, type = 'sine') => { const o = ctx.createOscillator(), g = ctx.createGain(); o.type = type; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + sweep); g.gain.setValueAtTime(lv, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len); o.connect(g); out(g, 1, wet); o.start(t); o.stop(t + len + 0.05); };
    if (d === 'k') { tone(115, 50, 0.32, 0.6, 0.02, 0.04); nz(0.012, 'highpass', 3000, 0.25, 0); }
    else if (d === 's') { nz(0.17, 'bandpass', 2600, 0.42, 0.35, 0.7); tone(195, 175, 0.08, 0.25, 0.2, 0.03, 'triangle'); }
    else if (d === 'g') nz(0.06, 'bandpass', 2600, 0.1, 0.1, 0.7);
    else if (d === 'X') { nz(1.8, 'highpass', 5500, 0.18, 0.5, 0.5); tone(100, 45, 0.5, 0.5, 0.1, 0.05); }
    else if (d === 'h') nz(0.028, 'highpass', 9000, 0.06, 0.03);
    else if (d[0] === 'T') { const k = +d.slice(1); tone(200 - k * 28, 95 - k * 10, 0.32, 0.5, 0.25, 0.12); }
  }
  function schedule(i, t) {
    const s = steps[i];
    try {
      if (s.jet && !mute.drums) jet(t, s.jet);
      if (s.pad && !mute.brass) brassChord(t, s.chord, 0.7, BAR * STEP, true);
      if (s.stab && !mute.brass) brassChord(t, s.chord, s.stab, STEP * 2, false);
      if (s.bell && !mute.brass) bellChord(t, s.chord);
      if (s.bass && !mute.bass && (s.bass !== 'w' || s.bar === 0)) seqBass(t, s.chord, s);
      if (s.drum && !mute.drums) drum(t, s.drum);
      if (s.hat && !mute.drums) drum(t, 'h');
      if (s.gtr && !mute.guitar) guitar(t, s.gtr, s.gtr.len * STEP * 0.96);
      if (s.lead && !mute.brass) brassLead(t, s.lead, s.lead.len * STEP * 0.92);
    } catch (e) {}
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
// ---- strike music end ----
