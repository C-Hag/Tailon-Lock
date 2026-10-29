// ---- cinematic music start ----
// "Carrier Dawn (Cinematic)": original arrangement of the Ace4Combat theme. FM bell chords, 808-style drums,
// an overdriven sustained guitar lead, power-fifth bass and a big reverb. Keys: E minor, up to F# minor,
// a tritone jump to C minor, and another tritone jump to an E major finale. Melody and progressions are original.
const CineMusic = (() => {
  const BPM = 90, STEP = 60 / BPM / 4, BAR = 16;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  const midi = n => { const m = /^([A-G])([#b]?)(\d)$/.exec(n); return 12 * (+m[3] + 1) + NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const pc = n => { const m = /^([A-G])([#b]?)$/.exec(n); return NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const hz = m => 440 * Math.pow(2, (m - 69) / 12);
  const QUAL = { m: [0, 3, 7], M: [0, 4, 7], m7: [0, 3, 7, 10], M7: [0, 4, 7, 11], m9: [0, 3, 7, 10, 14], sus: [0, 5, 7], '7': [0, 4, 7, 10], add9: [0, 4, 7, 14] };
  const NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  const MEL_A = 'E5:4 F#5:2 G5:2 B5:6 A5:2|G5:4 E5:4 C5:2 D5:2 E5:4|F#5:4 A5:2 D6:6 C#6:2 A5:2|B5:12 -:4|E5:4 F#5:2 G5:2 B5:4 E6:4|D6:2 C6:2 B5:4 G5:4 E5:4|D5:4 G5:4 B5:4 D6:4|A5:6 G5:2 F#5:8';
  const MEL_B = 'G5:2 G5:2 A5:2 B5:2 C6:4 B5:2 A5:2|A5:4 F#5:4 D5:4 F#5:2 A5:2|B5:6 A5:2 F#5:4 D5:4|E5:8 G5:4 B5:4|C6:4 B5:2 A5:2 E5:4 A5:4|G5:4 E5:2 G5:2 C6:8|D6:4 C6:2 B5:2 A5:4 F#5:4|A5:16';
  const FINALE = 'B5:4 E6:4 G#6:4 B6:4|A6:8 G#6:4 F#6:4|G#6:4 F#6:4 E6:4 D#6:4|E6:16';
  const A_CH = ['E:m', 'C:M', 'D:M', 'E:m', 'E:m', 'C:M', 'G:M', 'D:M'];
  const B_CH = ['C:M', 'D:M', 'B:m', 'E:m', 'A:m', 'C:M', 'D:M', 'D:M'];
  const SECTIONS = [
    { name: 'Intro', key: 'E minor', chords: ['E:m9', 'C:M7', 'A:m7', 'B:sus'], T: 0, style: 'intro' },
    { name: 'Build', key: 'E minor', chords: ['E:m', 'B:sus'], T: 0, mel: '-:16|B5:16', O: -12, style: 'build' },
    { name: 'Theme A', key: 'E minor', chords: A_CH, T: 0, mel: MEL_A, O: -12, style: 'theme' },
    { name: 'Lift', key: 'F# minor', chords: B_CH, T: 2, mel: MEL_B, O: -12, style: 'theme' },
    { name: 'Tritone jump', key: 'C minor', chords: A_CH, T: 8, mel: MEL_A, O: -12, style: 'climax' },
    { name: 'Finale', key: 'E major', chords: ['E:add9', 'A:M', 'B:M', 'E:add9'], T: 0, mel: FINALE, O: -12, style: 'finale' }
  ];
  function chordOf(spec, T) {
    const [r, q] = spec.split(':'), root = (pc(r) + T + 12) % 12;
    return { root, iv: QUAL[q], name: NAMES[root] + (q === 'M' ? '' : q === 'M7' ? 'maj7' : q) };
  }
  const DRUMS = {
    intro: () => null,
    build: (i, bi) => bi === 0 ? ({ 0: 'k', 3: 'k', 8: 'k', 11: 'k' }[i] || null) : (i < 8 ? ({ 0: 'k', 3: 'k' }[i] || null) : 'r' + (i - 8)),
    theme: (i, bi, last) => last && i >= 8 ? (i % 2 === 0 ? 'T' + (i - 8) / 2 : null) : ({ 0: 'k', 6: 'k', 10: 'k', 4: 's', 12: 's', 14: 'o' }[i] || (i % 2 === 0 ? 'h' : null)),
    climax: (i, bi, last) => (i === 0 && bi % 4 === 0 ? 'X' : null) || (last && i >= 8 ? (i % 2 === 0 ? 'T' + (i - 8) / 2 : 's') : ({ 0: 'k', 3: 'k', 6: 'k', 10: 'k', 4: 's', 12: 's', 14: 'o' }[i] || (i % 2 === 0 ? 'h' : null))),
    finale: (i, bi) => bi === 3 ? (i === 0 ? 'X' : null) : ({ 0: 'X', 4: 's', 8: 'k', 12: 's' }[i] || (i % 2 === 0 ? 'h' : null))
  };
  const steps = [], sections = [];
  for (const sec of SECTIONS) {
    sections.push({ step: steps.length, name: sec.name, key: sec.key });
    const s0 = steps.length, n = sec.chords.length;
    sec.chords.forEach((spec, bi) => {
      const ch = chordOf(spec, sec.T), last = bi === n - 1;
      for (let i = 0; i < BAR; i++) {
        const st = sec.style;
        const bell = st === 'intro' ? (i === 0 ? 1 : i === 8 ? 0.5 : 0) : st === 'build' ? (i === 0 ? 0.8 : 0) : (i === 0 ? 0.9 : i === 6 ? 0.6 : i === 10 && st === 'climax' ? 0.5 : 0);
        const bass = st === 'intro' ? null : st === 'build' ? (i % 2 === 0 ? 'p' : null) : st === 'finale' ? (i === 0 ? 'w' : null) : (i % 2 === 0 ? (i === 14 ? 'o' : 'p') : null);
        steps.push({ sec: sec.name, key: sec.key, bar: bi, chord: ch, bell, pad: i === 0 && st !== 'build', bass, drum: DRUMS[st](i, bi, last), gtr: null, swell: st === 'intro' && last && i === 0 });
      }
    });
    if (sec.mel) {
      let pos = s0, prev = null;
      for (const tok of sec.mel.replace(/\|/g, ' ').split(/\s+/)) {
        const [nm, l] = tok.split(':'), len = +l;
        if (nm !== '-') {
          const m = midi(nm) + sec.T + (sec.O || 0);
          steps[pos].gtr = { m, len, bend: len >= 6, slide: prev !== null && Math.abs(m - prev) >= 5 ? prev : null };
          prev = m;
        }
        pos += len;
      }
    }
  }
  const LOOP = sections[1].step;

  let ctx = null, master = null, rev = null, revIn = null, dly = null, gtrIn = null, noise = null, timer = null, next = 0, idx = 0, playing = false, vol = 0.6;
  const mute = { guitar: false, bells: false, bass: false, drums: false };
  const listeners = [];
  function impulse(sec, decay) {
    const len = Math.floor(ctx.sampleRate * sec), b = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, decay); }
    return b;
  }
  function build(dest) {
    master = ctx.createGain(); master.gain.value = vol;
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -16; comp.ratio.value = 3; comp.attack.value = 0.01; comp.release.value = 0.25;
    master.connect(comp); comp.connect(dest);
    rev = ctx.createConvolver(); rev.buffer = impulse(3.2, 2.6);
    revIn = ctx.createGain(); revIn.gain.value = 1; revIn.connect(rev);
    const revOut = ctx.createGain(); revOut.gain.value = 0.55; rev.connect(revOut); revOut.connect(master);
    // guitar chain: drive -> amp tone -> compressor (sustain) -> dry + delay + reverb
    gtrIn = ctx.createGain(); gtrIn.gain.value = 3.2;
    const shaper = ctx.createWaveShaper(), curve = new Float32Array(2048);
    for (let i = 0; i < 2048; i++) { const x = i / 1024 - 1; curve[i] = Math.tanh(x * 6) * 0.9; }
    shaper.curve = curve; shaper.oversample = '4x';
    const hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 140;
    const mid = ctx.createBiquadFilter(); mid.type = 'peaking'; mid.frequency.value = 1500; mid.Q.value = 0.9; mid.gain.value = 5;
    const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 4200; lp.Q.value = 0.7;
    const gc = ctx.createDynamicsCompressor(); gc.threshold.value = -30; gc.ratio.value = 8; gc.attack.value = 0.005; gc.release.value = 0.3;
    const gout = ctx.createGain(); gout.gain.value = 0.2;
    gtrIn.connect(shaper); shaper.connect(hp); hp.connect(mid); mid.connect(lp); lp.connect(gc); gc.connect(gout);
    gout.connect(master);
    dly = ctx.createDelay(1); dly.delayTime.value = STEP * 3;
    const fb = ctx.createGain(); fb.gain.value = 0.32; const dg = ctx.createGain(); dg.gain.value = 0.35;
    gout.connect(dly); dly.connect(fb); fb.connect(dly); dly.connect(dg); dg.connect(master); dg.connect(revIn);
    const gs = ctx.createGain(); gs.gain.value = 0.35; gout.connect(gs); gs.connect(revIn);
    const len = ctx.sampleRate, nb = ctx.createBuffer(1, len, len), d = nb.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    noise = nb;
  }
  const out = (node, dry, wet) => { const g = ctx.createGain(); g.gain.value = dry; node.connect(g); g.connect(master); if (wet) { const w = ctx.createGain(); w.gain.value = wet; node.connect(w); w.connect(revIn); } };
  function bell(t, m, vel, dur) {
    const f = hz(m), car = ctx.createOscillator(), mod = ctx.createOscillator(), mg = ctx.createGain(), g = ctx.createGain();
    car.frequency.value = f; mod.frequency.value = f * 3.5;
    mg.gain.setValueAtTime(f * 2.6 * vel, t); mg.gain.exponentialRampToValueAtTime(f * 0.15, t + 1.4);
    const car2 = ctx.createOscillator(), mod2 = ctx.createOscillator(), mg2 = ctx.createGain();
    car2.frequency.value = f * 1.003; mod2.frequency.value = f; mg2.gain.setValueAtTime(f * 1.2, t); mg2.gain.exponentialRampToValueAtTime(f * 0.1, t + 0.8);
    mod.connect(mg); mg.connect(car.frequency); mod2.connect(mg2); mg2.connect(car2.frequency);
    car.connect(g); car2.connect(g);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.05 * vel, t + 0.004); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    out(g, 0.8, 0.7);
    for (const o of [car, mod, car2, mod2]) { o.start(t); o.stop(t + dur + 0.05); }
  }
  function bellChord(t, ch, vel, dur) {
    const base = 60 + ((ch.root - 0 + 12) % 12) - (ch.root > 7 ? 12 : 0);
    ch.iv.forEach((iv, k) => bell(t + k * 0.035, base + iv, vel * (k ? 0.8 : 1), dur));
    bell(t, base + 12 + ch.iv[1], vel * 0.5, dur);
  }
  function pad(t, ch, dur) {
    const base = 48 + ch.root - (ch.root > 7 ? 12 : 0), g = ctx.createGain(), lp = ctx.createBiquadFilter();
    lp.type = 'lowpass'; lp.frequency.setValueAtTime(700, t); lp.frequency.linearRampToValueAtTime(1500, t + dur * 0.6);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.022, t + 0.7); g.gain.setValueAtTime(0.022, t + dur - 0.3); g.gain.linearRampToValueAtTime(0.0001, t + dur + 0.4);
    lp.connect(g); out(g, 1, 0.8);
    const oscs = [];
    for (const iv of ch.iv.slice(0, 3)) for (const det of [-7, 7]) { const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(base + 12 + iv); o.detune.value = det; o.connect(lp); oscs.push(o); }
    for (const o of oscs) { o.start(t); o.stop(t + dur + 0.5); }
  }
  function bass(t, ch, kind) {
    const root = 28 + ch.root + (ch.root > 7 ? -12 : 0) + (kind === 'o' ? 12 : 0), dur = kind === 'w' ? BAR * STEP : STEP * 1.8;
    const g = ctx.createGain(), lp = ctx.createBiquadFilter();
    lp.type = 'lowpass'; lp.Q.value = 4; lp.frequency.setValueAtTime(1400, t); lp.frequency.exponentialRampToValueAtTime(260, t + 0.18);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.11, t + 0.006); g.gain.exponentialRampToValueAtTime(kind === 'w' ? 0.04 : 0.0001, t + (kind === 'w' ? 0.6 : dur));
    if (kind === 'w') g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    lp.connect(g); out(g, 1, 0.05);
    for (const iv of [0, 7]) { const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(root + 12 + iv); o.connect(lp); o.start(t); o.stop(t + dur + 0.05); }
    const sub = ctx.createOscillator(), sg = ctx.createGain(); sub.frequency.value = hz(root); sg.gain.setValueAtTime(0.09, t); sg.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    sub.connect(sg); sg.connect(master); sub.start(t); sub.stop(t + dur + 0.05);
  }
  function guitar(t, n, dur) {
    const f = hz(n.m), g = ctx.createGain(), oscs = [];
    for (const [type, det, lv] of [['sawtooth', -5, 0.5], ['sawtooth', 6, 0.5], ['square', 0, 0.25]]) {
      const o = ctx.createOscillator(), og = ctx.createGain(); o.type = type; o.detune.value = det; og.gain.value = lv;
      if (n.slide !== null && n.slide !== undefined) { o.frequency.setValueAtTime(hz(n.slide), t); o.frequency.exponentialRampToValueAtTime(f, t + 0.06); }
      else if (n.bend) { o.frequency.setValueAtTime(f * Math.pow(2, -2 / 12), t); o.frequency.setValueAtTime(f * Math.pow(2, -2 / 12), t + 0.05); o.frequency.exponentialRampToValueAtTime(f, t + 0.2); }
      else o.frequency.setValueAtTime(f, t);
      o.connect(og); og.connect(g); oscs.push(o);
    }
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.3;
    vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.35); vg.gain.linearRampToValueAtTime(f * 0.009, t + Math.max(0.45, dur * 0.7));
    vib.connect(vg); for (const o of oscs) vg.connect(o.frequency);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.5, t + 0.012); g.gain.setValueAtTime(0.5, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.18);
    g.connect(gtrIn);
    for (const o of [...oscs, vib]) { o.start(t); o.stop(t + dur + 0.25); }
  }
  function drum(t, d) {
    const nz = (len, type, freq, lv, wet = 0.2, q = 0.8) => { const n = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain(); n.buffer = noise; f.type = type; f.frequency.value = freq; f.Q.value = q; g.gain.setValueAtTime(lv, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len); n.connect(f); f.connect(g); out(g, 1, wet); n.start(t, Math.random() * 0.5); n.stop(t + len + 0.05); };
    const tone = (f0, f1, len, lv, wet = 0.05) => { const o = ctx.createOscillator(), g = ctx.createGain(); o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + Math.min(len, 0.12)); g.gain.setValueAtTime(lv, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len); o.connect(g); out(g, 1, wet); o.start(t); o.stop(t + len + 0.05); };
    if (d === 'k') { tone(130, 46, 0.65, 0.55); nz(0.02, 'lowpass', 2500, 0.15, 0); }
    else if (d === 's') { nz(0.22, 'bandpass', 1800, 0.35, 0.9, 0.6); tone(220, 170, 0.09, 0.2, 0.4); nz(0.05, 'highpass', 4000, 0.15, 0.6); }
    else if (d === 'h') nz(0.04, 'highpass', 8500, 0.07, 0.05);
    else if (d === 'o') nz(0.3, 'highpass', 7500, 0.07, 0.2);
    else if (d === 'X') { nz(2.6, 'highpass', 5000, 0.16, 0.6, 0.5); tone(90, 40, 1.2, 0.5, 0.3); }
    else if (d[0] === 'T') { const k = +d.slice(1); tone(210 - k * 32, 90 - k * 10, 0.4, 0.45, 0.3); }
    else if (d[0] === 'r') { const k = +d.slice(1); nz(0.1, 'bandpass', 1800, 0.06 + k * 0.035, 0.8, 0.6); }
  }
  function swell(t) {
    const n = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain(), len = BAR * STEP;
    n.buffer = noise; n.loop = true; f.type = 'highpass'; f.frequency.setValueAtTime(9000, t); f.frequency.exponentialRampToValueAtTime(1500, t + len);
    g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.09, t + len); g.gain.linearRampToValueAtTime(0.0001, t + len + 0.05);
    n.connect(f); f.connect(g); out(g, 1, 0.5); n.start(t); n.stop(t + len + 0.1);
  }
  function schedule(i, t) {
    const s = steps[i];
    try {
      if (s.bell && !mute.bells) bellChord(t, s.chord, s.bell, s.sec === 'Intro' ? 5 : 3);
      if (s.pad && !mute.bells) pad(t, s.chord, BAR * STEP);
      if (s.swell && !mute.drums) swell(t);
      if (s.bass && !mute.bass) bass(t, s.chord, s.bass);
      if (s.drum && !mute.drums) drum(t, s.drum);
      if (s.gtr && !mute.guitar) guitar(t, s.gtr, s.gtr.len * STEP * 0.97);
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
      return 0.05 + steps.length * STEP + 4;
    }
  };
})();
// ---- cinematic music end ----
