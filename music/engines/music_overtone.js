// ---- overtone music start ----
// "Carrier Dawn (Overtone)": the SID theme reimagined in adaptive just intonation. Every note is retuned to a pure
// ratio of the chord it sits on, so chords ring without beating, and arpeggios climb the natural overtone series of the
// chord root, including the soft "natural seventh" (7:4) and alphorn eleventh (11:8) that equal temperament never plays.
// Voices: a ring-modulated SID pulse lead, overtone arpeggios, a filtered SID bass and noise drums. Theme C is a new
// melody played entirely on the natural harmonics of each chord's root over C, Ab, E and B.
const OvertoneMusic = (() => {
  const BPM = 120, STEP = 60 / BPM / 4, BAR = 16;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  const midi = n => { const m = /^([A-G])([#b]?)(\d)$/.exec(n); return 12 * (+m[3] + 1) + NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const pcOf = n => { const m = /^([A-G])([#b]?)$/.exec(n); return (NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0) + 12) % 12; };
  const et = m => 440 * Math.pow(2, (m - 69) / 12);
  const NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  // pure ratios for each interval above the chord root (septimal tritone and seventh)
  const RATIO = [1, 16 / 15, 9 / 8, 6 / 5, 5 / 4, 4 / 3, 7 / 5, 3 / 2, 8 / 5, 5 / 3, 7 / 4, 15 / 8];
  const chordOf = name => { const m = /^([A-G][#b]?)(m?)$/.exec(name); const root = pcOf(m[1]), minor = m[2] === 'm'; return { name, root, minor }; };
  // retune a note to the chord: find the chord root at or below it and multiply by the pure ratio
  const ji = (m, ch, mel) => { const iv = ((m - ch.root) % 12 + 12) % 12; return et(m - iv) * (mel && iv === 10 ? (ch.minor ? 9 / 5 : 16 / 9) : mel && iv === 6 ? 45 / 32 : RATIO[iv]); };
  // the drone fundamental of a chord, 40-80 Hz; minor chords sit on the harmonics 10:12:15 of a root a major third below
  const fund = ch => { let f = et(24 + ch.root) * (ch.minor ? 4 / 5 : 1); while (f < 40) f *= 2; while (f >= 80) f /= 2; return f; };
  const ARP_MAJ = [4, 5, 6, 7, 8, 9, 10, 12, 14, 12, 10, 9, 8, 7, 6, 5], ARP_MIN = [10, 12, 15, 18, 20, 24, 27, 30, 24, 20, 18, 15, 12, 10, 9, 10];
  const MEL_A = 'E5:4 F#5:2 G5:2 B5:6 A5:2|G5:4 E5:4 C5:2 D5:2 E5:4|F#5:4 A5:2 D6:6 C#6:2 A5:2|B5:12 -:4|E5:4 F#5:2 G5:2 B5:4 E6:4|D6:2 C6:2 B5:4 G5:4 E5:4|D5:4 G5:4 B5:4 D6:4|A5:6 G5:2 F#5:8';
  const MEL_B = 'G5:2 G5:2 A5:2 B5:2 C6:4 B5:2 A5:2|A5:4 F#5:4 D5:4 F#5:2 A5:2|B5:6 A5:2 F#5:4 D5:4|E5:8 G5:4 B5:4|C6:4 B5:2 A5:2 E5:4 A5:4|G5:4 E5:2 G5:2 C6:8|D6:4 C6:2 B5:2 A5:4 F#5:4|A5:16';
  // Theme C is written in harmonic numbers of each chord's root
  const MEL_C = 'h8:4 h9:2 h10:2 h12:6 h11:2|h10:8 h9:4 h8:4|h12:4 h10:2 h12:2 h16:6 h14:2|h15:8 h12:8|h10:4 h12:2 h15:2 h16:6 h18:2|h20:8 h18:4 h15:4|h12:4 h14:4 h16:4 h18:4|h15:12 -:4';
  const A_CH = ['Em', 'C', 'D', 'Em', 'Em', 'C', 'G', 'D'], B_CH = ['C', 'D', 'Bm', 'Em', 'Am', 'C', 'D', 'D'];
  const SECTIONS = [
    { name: 'Intro', chords: ['Em', 'Em', 'C', 'D'] },
    { name: 'Theme A', chords: A_CH, mel: MEL_A },
    { name: 'Theme B', chords: B_CH, mel: MEL_B },
    { name: 'Theme C', chords: ['C', 'C', 'Ab', 'Ab', 'E', 'E', 'B', 'B'], mel: MEL_C, overtones: true },
    { name: 'Theme A organum', chords: A_CH, mel: MEL_A, organum: true }
  ];
  const BASS = [0, 0, 12, 0, 0, 12, 0, 12, 0, 0, 12, 0, 7, 0, 12, 7];
  const steps = [], sections = [];
  for (const sec of SECTIONS) {
    sections.push({ step: steps.length, name: sec.name, key: sec.overtones ? 'overtones' : 'E minor' });
    const s0 = steps.length;
    sec.chords.forEach((c, bi) => {
      const ch = chordOf(c), last = bi === sec.chords.length - 1, intro = sec.name === 'Intro';
      for (let i = 0; i < BAR; i++) {
        let drum = null;
        if (intro && bi < 2) drum = i % 4 === 2 ? 'h' : null;
        else if (last && i >= 8 && !sec.overtones) drum = i === 8 || i === 10 ? 's' : 'T' + (i - 8);
        else if (sec.overtones) drum = { 0: 'k', 8: 'k', 12: 's' }[i] || (i % 4 === 2 ? 'h' : null);
        else drum = { 0: 'k', 4: 's', 8: 'k', 11: 'k', 12: 's' }[i] || (i % 2 === 0 ? 'h' : null);
        steps.push({ sec: sec.name, key: sec.overtones ? 'overtones' : 'just E minor', bar: bi, chord: ch, bass: (intro && bi < 2) || (sec.overtones && i % 4) ? null : 36 + ch.root - (ch.root > 6 ? 12 : 0) + BASS[i],
          arp: i === 0 && !(intro && bi < 1), drum, lead: null, harm: null });
      }
    });
    if (sec.mel) {
      let pos = s0, prev = null;
      for (const tok of sec.mel.replace(/\|/g, ' ').split(/\s+/)) {
        const [n, l] = tok.split(':'), len = +l;
        if (n !== '-') {
          const s = steps[pos];
          // harmonic tokens play the h-th overtone of the chord root, in the melody's octave
          const h = n[0] === 'h' ? +n.slice(1) : 0, f = h ? fund(s.chord) * h : ji(midi(n), s.chord, true);
          const m = h ? Math.round(69 + 12 * Math.log2(f / 440)) : midi(n);
          s.lead = { m, f, len, from: prev, name: n, h };
          if (sec.organum) s.harm = { f: f * 2 / 3, len };
          prev = f;
        }
        pos += len;
      }
    }
  }
  const LOOP = sections[1].step;

  let ctx = null, out = null, wet = null, noise = null, timer = null, next = 0, idx = 0, playing = false, vol = 0.5;
  const mute = { lead: false, arps: false, bass: false, drums: false };
  const listeners = [];
  function build(dest) {
    out = ctx.createGain(); out.gain.value = vol; out.connect(dest);
    // a soft stereo echo and short hall, so the pure intervals can ring
    wet = ctx.createGain(); wet.gain.value = 1;
    const dl = ctx.createDelay(1), dr = ctx.createDelay(1), fb = ctx.createGain(), pl = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain(), pr = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain();
    dl.delayTime.value = STEP * 3; dr.delayTime.value = STEP * 6; fb.gain.value = 0.3;
    if (pl.pan) { pl.pan.value = -0.7; pr.pan.value = 0.7; }
    const dg = ctx.createGain(); dg.gain.value = 0.28;
    wet.connect(dl); dl.connect(pl); dl.connect(dr); dr.connect(pr); dr.connect(fb); fb.connect(dl); pl.connect(dg); pr.connect(dg); dg.connect(out);
    const rev = ctx.createConvolver(), len = Math.floor(ctx.sampleRate * 1.6), b = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = b.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3); }
    rev.buffer = b; const rg = ctx.createGain(); rg.gain.value = 0.35; wet.connect(rev); rev.connect(rg); rg.connect(out);
    const nl = ctx.sampleRate, nb = ctx.createBuffer(1, nl, nl), nd = nb.getChannelData(0);
    for (let i = 0; i < nl; i++) nd[i] = Math.random() * 2 - 1;
    noise = nb;
  }
  function send(node, dry, w) { const g = ctx.createGain(); g.gain.value = dry; node.connect(g); g.connect(out); if (w) { const x = ctx.createGain(); x.gain.value = w; node.connect(x); x.connect(wet); } }
  function env(g, t, peak, dur, rel = 0.06) {
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(peak, t + 0.005);
    g.gain.exponentialRampToValueAtTime(peak * 0.7, t + Math.min(0.12, dur * 0.5));
    g.gain.setValueAtTime(peak * 0.7, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + rel);
  }
  // SID pulse: saw minus a delayed copy of itself, with slow pulse-width modulation
  function pulse(t, f, dur, dest, pw = 0.3, pwm = 0.8) {
    const o = ctx.createOscillator(), d = ctx.createDelay(0.05), inv = ctx.createGain(), g = ctx.createGain();
    o.type = 'sawtooth'; o.frequency.setValueAtTime(f, t);
    d.delayTime.setValueAtTime(pw / f, t);
    const lfo = ctx.createOscillator(), lg = ctx.createGain(); lfo.frequency.value = pwm; lg.gain.value = 0.15 / f; lfo.connect(lg); lg.connect(d.delayTime);
    inv.gain.value = -1; o.connect(g); o.connect(d); d.connect(inv); inv.connect(g); g.connect(dest);
    for (const x of [o, lfo]) { x.start(t); x.stop(t + dur + 0.15); }
    return o;
  }
  // lead: a pulse voice blended with a ring-modulated triangle pair a pure fifth apart, for a glassy SID bell tone
  function lead(t, n, dur, level, harm) {
    const f = n.f, g = ctx.createGain(); env(g, t, level, dur, 0.1); send(g, 1, 0.5);
    const p = pulse(t, f, dur, g, 0.3, 0.7);
    const car = ctx.createOscillator(), mod = ctx.createOscillator(), ring = ctx.createGain(), rl = ctx.createGain();
    car.type = 'triangle'; mod.type = 'triangle'; car.frequency.setValueAtTime(f, t); mod.frequency.setValueAtTime(f * 3 / 2, t);
    ring.gain.value = 0; mod.connect(ring.gain); car.connect(ring); rl.gain.value = 1.3; ring.connect(rl); rl.connect(g);
    if (n.from && Math.abs(Math.log2(n.from / f)) > 0.3) for (const o of [p, car]) { o.frequency.setValueAtTime(n.from, t); o.frequency.exponentialRampToValueAtTime(f, t + 0.05); }
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.4; vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.18); vg.gain.linearRampToValueAtTime(f * 0.012, t + 0.45);
    vib.connect(vg); vg.connect(p.frequency); vg.connect(car.frequency);
    for (const x of [car, mod, vib]) { x.start(t); x.stop(t + dur + 0.15); }
    if (harm) { const hg = ctx.createGain(); env(hg, t, level * 0.55, dur, 0.1); send(hg, 0.8, 0.6); pulse(t, harm.f, dur, hg, 0.45, 1.3); }
  }
  // overtone arpeggio: a narrow pulse racing up and down the chord root's harmonic series
  function arp(t, ch, dur) {
    const f0 = fund(ch) * 2, list = ch.minor ? ARP_MIN : ARP_MAJ, g = ctx.createGain(), lp = ctx.createBiquadFilter();
    lp.type = 'lowpass'; lp.Q.value = 6; lp.frequency.setValueAtTime(900, t); lp.frequency.exponentialRampToValueAtTime(4200, t + dur * 0.5); lp.frequency.exponentialRampToValueAtTime(1200, t + dur);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.028, t + 0.02); g.gain.setValueAtTime(0.028, t + dur * 0.8); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    const o = pulse(t, f0 * list[0], dur, lp, 0.12, 0.3);
    for (let k = 0, tt = t; tt < t + dur; k++, tt += STEP / 3) { let h = list[k % list.length]; while (f0 * h > 1400) h /= 2; o.frequency.setValueAtTime(f0 * h, tt); }
    lp.connect(g); send(g, 0.9, 0.7);
  }
  function bass(t, m, ch) {
    const f = et(m - ((m - ch.root) % 12 + 12) % 12) * RATIO[((m - ch.root) % 12 + 12) % 12];
    const o = ctx.createOscillator(), sub = ctx.createOscillator(), fl = ctx.createBiquadFilter(), g = ctx.createGain();
    o.type = 'sawtooth'; o.frequency.value = f; sub.type = 'square'; sub.frequency.value = f; sub.detune.value = 7;
    fl.type = 'lowpass'; fl.Q.value = 8; fl.frequency.setValueAtTime(1800, t); fl.frequency.exponentialRampToValueAtTime(280, t + 0.12);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.12, t + 0.004); g.gain.exponentialRampToValueAtTime(0.0001, t + STEP * 1.6);
    o.connect(fl); sub.connect(fl); fl.connect(g); send(g, 1, 0.05);
    for (const x of [o, sub]) { x.start(t); x.stop(t + STEP * 1.7); }
  }
  function hit(t, type, ch) {
    const n = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain();
    n.buffer = noise; n.connect(f); f.connect(g); send(g, 1, 0.15);
    const tone = (f0, f1, len, lv) => { const o = ctx.createOscillator(), og = ctx.createGain(); o.type = 'triangle'; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + len); og.gain.setValueAtTime(lv, t); og.gain.exponentialRampToValueAtTime(0.0001, t + len); o.connect(og); send(og, 1, 0.1); o.start(t); o.stop(t + len + 0.02); };
    let len = 0.04, lv = 0.05;
    if (type === 'k') { f.type = 'lowpass'; f.frequency.value = 900; len = 0.03; lv = 0.12; tone(160, 42, 0.14, 0.42); }
    else if (type === 's') { f.type = 'highpass'; f.frequency.value = 1300; len = 0.15; lv = 0.18; tone(250, 150, 0.06, 0.16); }
    else if (type === 'h') { f.type = 'highpass'; f.frequency.value = 7000; len = 0.035; lv = 0.045; }
    else { const k = +type.slice(1), f0 = fund(ch) * [4, 3, 5 / 2, 2, 3 / 2, 5 / 4, 1, 1][k]; f.type = 'bandpass'; f.frequency.value = 600; len = 0.05; lv = 0.05; tone(f0 * 2, f0, 0.18, 0.3); }
    g.gain.setValueAtTime(lv, t); g.gain.exponentialRampToValueAtTime(0.0001, t + len);
    n.start(t, Math.random() * 0.5); n.stop(t + len + 0.02);
  }
  function schedule(i, t) {
    const s = steps[i];
    try {
      if (s.lead && !mute.lead) lead(t, s.lead, s.lead.len * STEP * 0.92, 0.07, s.harm);
      if (s.arp && !mute.arps) arp(t, s.chord, BAR * STEP);
      if (s.bass !== null && !mute.bass) bass(t, s.bass, s.chord);
      if (s.drum && !mute.drums) hit(t, s.drum, s.chord);
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
      out.gain.cancelScheduledValues(ctx.currentTime); out.gain.setValueAtTime(vol, ctx.currentTime);
      playing = true; idx = 0; next = ctx.currentTime + 0.1;
      timer = setInterval(tick, 25); tick();
    },
    stop() { playing = false; clearInterval(timer); timer = null; if (out && ctx) { out.gain.setTargetAtTime(0, ctx.currentTime, 0.05); } },
    volume(v) { vol = v; if (out && ctx && playing) out.gain.setTargetAtTime(v, ctx.currentTime, 0.2); },
    renderAll(audioCtx, dest, from = 0, to = steps.length) {
      ctx = audioCtx; build(dest);
      const saved = listeners.splice(0);
      for (let i = from; i < to; i++) schedule(i, 0.05 + (i - from) * STEP);
      listeners.push(...saved);
      return 0.05 + (to - from) * STEP + 2;
    }
  };
})();
// ---- overtone music end ----
