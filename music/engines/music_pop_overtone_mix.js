// ---- pop music start ----
// "Nose 2 Nose (Pop)": original Tailon Lock pop-rock song, about 2:20. A snare-drum march in E minor grows under a
// jet engine that roars in over 11 seconds and passes as the full band enters, then a big 80s movie-style bell intro, a driving guitar riff, verses over a pulsing
// eighth-note bass, an I–V–vi–IV chorus, a synth solo, and a final chorus lifted a whole step that fades out over the
// last 20 seconds, with an overtone bridge before the final choruses. Every pitched voice is retuned to pure ratios of
// the chord it plays over, overtone arpeggios climb each root's harmonic series, and a ring-modulated SID glass lead
// doubles the hook. Played by the live band engine: human timing, groove variations and fills.
const PopMusic = (() => {
  const BPM = 128, STEP = 60 / BPM / 4, BAR = 16;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  const midi = n => { const m = /^([A-G])([#b]?)(\d)$/.exec(n); return 12 * (+m[3] + 1) + NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const pc = n => { const m = /^([A-G])([#b]?)$/.exec(n); return NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); };
  const et = m => 440 * Math.pow(2, (m - 69) / 12);
  // adaptive just intonation: every pitched voice is retuned to a pure ratio of the chord it is played over
  const RATIO = [1, 16 / 15, 9 / 8, 6 / 5, 5 / 4, 4 / 3, 45 / 32, 3 / 2, 8 / 5, 5 / 3, 16 / 9, 15 / 8];
  let tune = null;
  const hz = m => { if (!tune) return et(m); const iv = ((m - tune.root) % 12 + 12) % 12; return et(m - iv) * (iv === 10 && tune.iv[1] === 3 ? 9 / 5 : RATIO[iv]); };
  // the chord root's natural harmonic series, 40-80 Hz; minor chords sit on harmonics 10:12:15 of a root a third below
  const fund = ch => { let f = et(24 + ch.root) * (ch.iv[1] === 3 ? 4 / 5 : 1); while (f < 40) f *= 2; while (f >= 80) f /= 2; return f; };
  const ARP_MAJ = [4, 5, 6, 7, 8, 9, 10, 12, 14, 12, 10, 9, 8, 7, 6, 5], ARP_MIN = [10, 12, 15, 18, 20, 24, 27, 30, 24, 20, 18, 15, 12, 10, 9, 10];
  const QUAL = { m: [0, 3, 7], M: [0, 4, 7], sus: [0, 5, 7], add9: [0, 4, 7, 14] };
  const NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  const INTRO = 'B4:14 A4:1 G4:1|G4:4 A4:4 E5:8|D5:14 B4:1 A4:1|A4:16';
  const RIFF = ['E3:1 E3:1 G3:1 E3:1 A3:2 E3:1 B3:2 E3:1 D4:2 B3:1 A3:1 G3:2', 'E3:1 E3:1 G3:1 E3:1 A3:2 E3:1 B3:2 E3:1 D4:2 E4:1 D4:1 B3:2'];
  const VERSE = 'E4:2 E4:2 G4:2 A4:2 B4:6 A4:2|G4:2 E4:2 C4:2 E4:2 G4:4 E4:4|D4:2 G4:2 B4:2 D5:2 B4:4 A4:2 G4:2|A4:12 -:4|' +
    'E4:2 E4:2 G4:2 A4:2 B4:4 E5:4|C5:2 B4:2 A4:2 G4:2 E4:4 G4:4|B4:2 A4:2 G4:2 D4:2 G4:4 B4:4|A4:8 F#4:8';
  const PRE = 'C5:8 D5:8|E5:8 F#5:8|G5:12 F#5:2 E5:2|F#5:16';
  const HOOK = 'B4:2 D5:2 D5:2 E5:2 G5:6 D5:2|F#5:4 E5:2 D5:2 A5:8|G5:2 E5:2 G5:2 B5:2 E5:6 D5:2|E5:4 D5:2 C5:2 G4:8|' +
    'B4:2 D5:2 D5:2 E5:2 G5:6 B5:2|A5:4 G5:2 F#5:2 D5:8|E5:2 G5:2 B5:2 E6:2 D6:4 B5:4|C6:4 B5:4 A5:4 F#5:4';
  // overtone bridge: written in harmonic numbers of each chord's root
  const BRIDGE = 'h8:4 h9:2 h10:2 h12:6 h11:2|h10:8 h9:4 h8:4|h12:4 h10:2 h12:2 h16:6 h14:2|h15:8 h12:8|h10:4 h12:2 h15:2 h16:6 h18:2|h20:8 h18:4 h15:4|h12:4 h14:4 h16:4 h18:4|h15:12 -:4';
  const MARCH = 'E4:3 E4:1 E4:3 G4:1 B4:4 B4:3 A4:1|G4:3 A4:1 B4:3 G4:1 E4:8|D4:3 D4:1 D4:3 F#4:1 A4:4 A4:3 G4:1|F#4:3 G4:1 A4:3 F#4:1 E4:8';
  const SOLO = 'B5:2 E6:2 G6:2 B6:2 A6:1 G6:1 F#6:1 E6:1 D6:2 E6:2|C6:1 D6:1 E6:1 G6:1 E6:1 D6:1 C6:1 B5:1 A5:2 G5:2 E5:4|' +
    'D6:2 B5:1 D6:1 G6:2 D6:1 G6:1 B6:4 A6:2 G6:2|F#6:6 E6:1 F#6:1 A6:8|G6:1 F#6:1 E6:1 D6:1 E6:1 D6:1 B5:1 A5:1 B5:2 E6:2 G6:4|' +
    'E6:1 G6:1 C7:2 B6:1 A6:1 G6:1 E6:1 G6:4 E6:4|D6:1 G6:1 B6:1 D7:1 B6:1 G6:1 D6:1 B5:1 D6:1 G6:1 B6:1 D7:1 B6:2 A6:2|A6:4 F#6:4 D6:4 A5:4';
  const MINOR = ['E:m', 'C:M', 'G:M', 'D:M'], POP = ['G:M', 'D:M', 'E:m', 'C:M'], POP_A = ['A:M', 'E:M', 'F#:m', 'D:M'];
  const GROOVES = { R: { k: [0, 6, 8, 10], g: [] }, A: { k: [0, 6, 8], g: [] }, B: { k: [0, 3, 6, 8, 14], g: [7], o: 14 }, C: { k: [0, 6, 8, 11, 14], g: [10, 15] }, D: { k: [0, 3, 8, 10, 14], g: [7, 13], o: 14 } };
  const FILLS = { f4: { 12: 's', 13: 'T0', 14: 'T1', 15: 'T2' }, f8: { 8: 's', 10: 's', 11: 's', 12: 'T0', 13: 'T1', 14: 'T2', 15: 'T3' },
    f16: { 8: 'T0', 9: 'T0', 10: 'T1', 11: 'T1', 12: 'T2', 13: 'T2', 14: 'T3', 15: 'T3' } };
  // march snare: accented rudiment with ghost notes in between
  const MARCH_SN = [1, 0.3, 0.3, 0.8, 1, 0.3, 0.8, 0.3, 1, 0.3, 0.3, 0.8, 1, 0.3, 0.8, 0.8];
  const CHUG = { 0: 1, 6: 1, 10: 2 };
  // bass shapes: [step, interval, length]
  const BASS = { drive: [0, 2, 4, 6, 8, 10, 12, 14].map(s => [s, s === 14 ? 12 : 0, 2]),
    funkA: [[0, 0, 2], [3, 12, 1], [6, 0, 1], [8, 0, 2], [10, 12, 1], [11, 0, 1], [13, 7, 1], [14, 12, 2]],
    funkB: [[0, 0, 2], [3, 0, 1], [4, 12, 1], [6, 0, 1], [8, 0, 1], [9, 0, 1], [11, 12, 1], [12, 7, 2], [14, 12, 1], [15, 7, 1]],
    march: [[0, 0, 4], [4, 12, 4], [8, 0, 4], [12, 7, 4]], whole: [[0, 0, 16]] };
  // the arrangement, one entry per bar
  const bars = [];
  const add = (sec, key, chords, opts, perBar) => chords.forEach((c, k) => bars.push(Object.assign({ sec, key, ch: c }, opts, perBar ? perBar(k, chords.length) : {})));
  // the march grows under the approaching jet: snare alone, then bass and guitar, and the full band as the jet passes
  add('Jet march', 'E minor', ['E:m', 'E:m', 'E:m', 'E:m', 'D:M', 'B:m/E:m', 'E:m', 'E:m', 'D:M', 'B:m/E:m'], {},
    k => k < 6 ? { march: true, mvel: [0.3, 0.45, 0.6, 0.7, 0.8, 0.95][k], bass: k >= 2 ? 'march' : null, kick: k >= 1 }
      : { g: 'D', hats: 16, bass: 'march', crash: k === 6, bell: k === 6 ? 1 : 0, pad: true, pcs: CHUG, fill: k === 9 ? 'f8' : null });
  add('Intro', 'E minor', MINOR, { half: true, bass: 'whole', pad: true }, k => ({ crash: k === 0, fill: k === 3 ? 'f4' : null, bell: 1 }));
  add('Riff', 'E minor', ['E:m', 'E:m', 'E:m', 'E:m'], { g: 'R', hats: 8, bass: 'drive' }, k => ({ crash: k === 0, fill: k === 3 ? 'f4' : null, pcs: k === 3 ? { 12: 4 } : null }));
  add('Verse', 'E minor', [...MINOR, ...MINOR], { hats: 8, bass: 'drive' }, k => ({ g: 'ABAC'[k % 4], fill: k === 7 ? 'f8' : null }));
  add('Pre-chorus', 'E minor', ['C:M', 'D:M', 'E:m', 'D:M'], { hats: 16, bass: 'drive', pad: true, pcs: CHUG }, k => k === 3 ? { roll: true } : { g: 'D', kick: [0, 4, 8, 12] });
  const chorus = (name, key, chords, extra) => add(name, key, [...chords, ...chords], Object.assign({ hats: 16, bass: 'funk', pad: true, pcs: CHUG }, extra), (k, n) => ({ g: 'BCDB'[k % 4], crash: k % 4 === 0, bell: k === 0 ? 0.7 : 0, fill: k === n - 1 ? 'f16' : null }));
  chorus('Chorus', 'G major', POP);
  add('Riff', 'E minor', ['E:m', 'E:m'], { g: 'R', hats: 8, bass: 'drive' }, k => ({ crash: k === 0, fill: k === 1 ? 'f8' : null }));
  add('Synth solo', 'E minor', [...MINOR, ...MINOR], { hats: 16, bass: 'funk', pad: true, pcs: CHUG }, k => ({ g: 'BCDB'[k % 4], crash: k % 4 === 0, fill: k === 7 ? 'f8' : null }));
  add('Overtone bridge', 'overtones', ['C:M', 'C:M', 'Ab:M', 'Ab:M', 'E:M', 'E:M', 'B:M', 'B:M'], { half: true, bass: 'whole', pad: true }, k => ({ crash: k === 0, bell: k % 2 === 0 ? 0.6 : 0, roll: k === 7 }));
  chorus('Chorus', 'G major', POP, {});
  bars[bars.length - 1].fill = null; bars[bars.length - 1].roll = true;
  chorus('Lift chorus', 'A major', POP_A);
  chorus('Outro chorus', 'A major', POP_A);
  const chordOf = spec => { const [r, q] = spec.split(':'), root = pc(r); return { root, iv: QUAL[q], name: NAMES[root] + (q === 'M' ? '' : q) }; };
  const voiceBase = (root, lo) => lo + root - (root > 7 ? 12 : 0);
  const chordAt = (bar, i) => { const p = bars[bar].ch.split('/'); return chordOf(p[i >= 8 && p[1] ? 1 : 0]); };
  const RANK = ['X', 's', 'k', 'T', 'o', 'h'];
  const OT_SECS = ['Verse', 'Chorus', 'Synth solo', 'Overtone bridge', 'Lift chorus', 'Outro chorus'];
  const NB = bars.length, steps = [], sections = [];
  const FADE_BARS = 20 / (BAR * STEP), FADE_FROM = NB - FADE_BARS;
  for (let b = 0; b < NB; b++) {
    const pl = bars[b], gr = pl.g ? GROOVES[pl.g] : null;
    if (!b || bars[b - 1].sec !== pl.sec) sections.push({ step: steps.length, name: pl.sec, key: pl.key });
    for (let i = 0; i < BAR; i++) {
      const ch = chordAt(b, i), hits = [], hit = (d, v) => hits.push([d, v]);
      const fill = pl.fill && FILLS[pl.fill][i] !== undefined ? FILLS[pl.fill][i] : null, inFill = pl.fill && i >= Math.min(...Object.keys(FILLS[pl.fill]).map(Number));
      if (pl.roll) { hit('s', 0.25 + 0.75 * i / 15); if (i % 4 === 0) hit('k', 1); }
      else if (fill) { hit(fill, fill === 's' ? 0.9 : 1); if (i === 15 || i % 4 === 0) hit('k', 0.9); }
      else if (pl.march) { hit('s', MARCH_SN[i] * pl.mvel * (0.85 + 0.15 * (i % 4 === 0))); if (pl.kick && (i === 0 || i === 8)) hit('k', pl.mvel); if (pl.kick && (i === 6 || i === 14)) hit('T3', 0.6 * pl.mvel); }
      else if (pl.half) { if (i === 0 || i === 10) hit('k', 1); if (i === 8) { hit('s', 1); hit('c', 1); } if (i % 4 === 2) hit('h', 0.5); }
      else if (!inFill) {
        const kicks = pl.kick || (gr ? gr.k : []);
        if (kicks.includes(i)) hit('k', i % 8 === 0 ? 1 : 0.8);
        if (gr && (i === 4 || i === 12)) { hit('s', 1); hit('c', 0.9); }
        if (gr && gr.g.includes(i)) hit('s', 0.28);
        if (gr && gr.o === i && pl.hats) hit('o', 0.8);
        else if (pl.hats === 16 || (pl.hats === 8 && i % 2 === 0)) hit('h', i % 4 === 0 ? 1 : i % 2 === 0 ? 0.75 : 0.45);
      }
      if (i === 0 && pl.crash) hit('X', 1);
      let bass = null;
      if (pl.bass) {
        const shape = pl.bass === 'funk' ? BASS[b % 2 ? 'funkB' : 'funkA'] : BASS[pl.bass], ev = shape.find(e => e[0] === i);
        if (ev) bass = { m: voiceBase(ch.root, 36) + ev[1], len: ev[2], acc: i % 8 === 0 };
      }
      const pcLen = pl.pcs && pl.pcs[i];
      const lab = RANK.find(r => hits.some(([d]) => d[0] === r || (r === 's' && d === 'c')));
      const pos = b + i / BAR, fade = pos < FADE_FROM ? 1 : Math.max(0, Math.pow(1 - (pos - FADE_FROM) / FADE_BARS, 1.6));
      steps.push({ sec: pl.sec, key: pl.key, bar: b, chord: ch, hits, drum: lab === 'T' ? 'T0' : lab || null, bass, groove: !!gr && !pl.roll && !inFill,
        power: pcLen ? { len: pcLen } : null, pad: i === 0 && !!pl.pad, bell: i === 0 ? pl.bell || 0 : 0, fade,
        jet: 0, jetLv: 1, jetPk: 0.58, riser: 0, riserLv: 1, lv: pl.mvel || 1, p1: [], p2: [], gtr: null, syn: null, glass: null,
        ot: i === 0 && (OT_SECS.includes(pl.sec) || (pl.sec === 'Jet march' && b >= 6)) });
    }
  }
  const secAt = (name, n = 1) => { let c = 0; for (const s of sections) if (s.name === name && ++c === n) return s.step; return -1; };
  // place "note[+note]:len" tokens into a voice starting at a step
  function place(field, at, str, T = 0) {
    let pos = at, prev = null;
    for (const tok of str.replace(/\|/g, ' ').trim().split(/\s+/)) {
      const [nm, l] = tok.split(':'), len = +l;
      if (nm !== '-') for (const n of nm.split('+')) {
        // "hN" is the N-th natural harmonic of the chord root
        const f = n[0] === 'h' ? fund(steps[pos].chord) * +n.slice(1) : 0, m = f ? Math.round(69 + 12 * Math.log2(f / 440)) : midi(n) + T;
        const ev = { m, f, h: f ? +n.slice(1) : 0, len, bend: len >= 6, slide: prev !== null && Math.abs(m - prev) >= 5 ? prev : null };
        if (field === 'gtr' || field === 'syn' || field === 'glass') steps[pos][field] = ev; else steps[pos][field].push(ev);
        prev = m;
      }
      pos += len;
    }
  }
  const barOf = step => step / BAR + 1;
  function alberti(field, bar, every) {
    for (let i = 0; i < BAR; i += every) { const ch = chordAt(bar - 1, i), k = [0, 2, 1, 2][(i / every) % 4]; steps[(bar - 1) * BAR + i][field].push({ m: voiceBase(ch.root, 48) + ch.iv[k], len: every }); }
  }
  function broken(field, bar) {
    for (let i = 0; i < BAR; i++) { const ch = chordAt(bar - 1, i), tones = [0, ch.iv[1], ch.iv[2], 12, 12 + ch.iv[1]], k = [0, 1, 2, 3, 4, 3, 2, 1][i % 8]; steps[(bar - 1) * BAR + i][field].push({ m: voiceBase(ch.root, 72) + tones[k], len: 1 }); }
  }
  const range = (step, n) => Array.from({ length: n }, (_, k) => barOf(step) + k);
  // Jet march: one long engine roar that peaks as the full band comes in, a riser underneath
  const march = secAt('Jet march'), drop = march + 6 * BAR, jetL = 13.5;
  steps[0].jet = jetL; steps[0].jetPk = (drop * STEP) / jetL; steps[0].riser = drop * STEP;
  // Intro: big bell chords, a singing guitar, pianos rippling far behind
  const intro = secAt('Intro'); place('gtr', intro, INTRO);
  for (const b of range(intro, 4)) broken('p1', b);
  // Riff: the guitar riff over a pulsing eighth-note bass
  for (const n of [1, 2]) { const r = secAt('Riff', n), len = n === 1 ? 4 : 2; for (let k = 0; k < len; k++) place('gtr', r + k * BAR, RIFF[k % 2]); }
  // Verse and pre-chorus
  const verse = secAt('Verse'); place('gtr', verse, VERSE);
  for (const b of range(verse, 8)) alberti('p2', b, 2);
  const pre = secAt('Pre-chorus'); place('gtr', pre, PRE);
  for (const b of range(pre, 4)) broken('p1', b);
  // Choruses: the hook on guitar, pianos doubling an octave up in the background
  for (const [name, n, T] of [['Chorus', 1, 0], ['Chorus', 2, 0], ['Lift chorus', 1, 2], ['Outro chorus', 1, 2]]) {
    const c = secAt(name, n); place('gtr', c, HOOK, T); place('glass', c, HOOK, T + 12);
    for (const b of range(c, 8)) alberti('p2', b, 2);
  }
  // March: the guitar plays the minor marching tune, then the synth takes it an octave up as the band kicks in
  place('gtr', march + 2 * BAR, MARCH); place('syn', drop, MARCH, 12); place('gtr', drop, MARCH);
  for (const b of range(drop, 4)) alberti('p2', b, 2);
  // Synth solo over the power chords
  const solo = secAt('Synth solo'); place('syn', solo, SOLO, -12);
  for (const b of range(solo, 8)) broken('p1', b);
  // a second jet passes into the lift chorus
  const lift = secAt('Lift chorus'), jl = 2 * BAR * STEP;
  steps[lift - 2 * BAR].jet = jl / 0.58; steps[lift - 2 * BAR].riser = jl;
  // smooth joins: the riff break builds into the solo, every other section change gets a cymbal swell
  const brk = secAt('Riff', 2); steps[brk].riser = 2 * BAR * STEP; steps[brk].riserLv = 0.8;
  for (const sec of sections.slice(1)) {
    const from = sec.step - BAR;
    if (!steps.slice(Math.max(0, from - BAR), sec.step).some(x => x.riser)) { steps[from].riser = BAR * STEP; steps[from].riserLv = 0.55; }
  }
  // overtone bridge: the glass lead plays the harmonic melody over half-time drums and pads
  place('glass', secAt('Overtone bridge'), BRIDGE);
  for (const b of range(secAt('Overtone bridge'), 8)) alberti('p2', b, 2);
  const LOOP = secAt('Riff');



  let ctx = null, master = null, revIn = null, gtrIn = null, fadeG = null, noise = null, timer = null, next = 0, idx = 0, playing = false, vol = 0.6, vel = 1;
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
    fadeG = ctx.createGain(); master.connect(fadeG);
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -16; comp.ratio.value = 4; comp.attack.value = 0.004; comp.release.value = 0.15;
    fadeG.connect(comp); comp.connect(dest);
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
  // synth solo lead: five detuned saws with a filter sweep, glide between leaps and delayed vibrato
  function synthLead(t, n, dur) {
    const f = hz(n.m), lp = ctx.createBiquadFilter(), g = ctx.createGain(), oscs = [];
    lp.type = 'lowpass'; lp.Q.value = 1.2;
    lp.frequency.setValueAtTime(900, t); lp.frequency.exponentialRampToValueAtTime(6000, t + 0.05); lp.frequency.exponentialRampToValueAtTime(3200, t + 0.3);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.1 * vel, t + 0.01); g.gain.setValueAtTime(0.09 * vel, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.15);
    lp.connect(g); const g2 = ctx.createGain(); g2.gain.value = 1 / vel; g.connect(g2); out(g2, 1, 0.35);
    const d = ctx.createDelay(1); d.delayTime.value = STEP * 3; const dg = ctx.createGain(); dg.gain.value = 0.25; g.connect(d); d.connect(dg); out(dg, 1, 0.3);
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
  // SID pulse: saw minus a delayed copy of itself, with slow pulse-width modulation
  function pulse(t, f, dur, dest, pw = 0.3, pwm = 0.8) {
    const o = ctx.createOscillator(), d = ctx.createDelay(0.05), inv = ctx.createGain(), g = ctx.createGain();
    o.type = 'sawtooth'; o.frequency.setValueAtTime(f, t); d.delayTime.setValueAtTime(pw / f, t);
    const lfo = ctx.createOscillator(), lg = ctx.createGain(); lfo.frequency.value = pwm; lg.gain.value = 0.15 / f; lfo.connect(lg); lg.connect(d.delayTime);
    inv.gain.value = -1; o.connect(g); o.connect(d); d.connect(inv); inv.connect(g); g.connect(dest);
    for (const x of [o, lfo]) { x.start(t); x.stop(t + dur + 0.15); }
    return o;
  }
  // overtone arpeggio: a narrow pulse racing up and down the chord root's harmonic series, natural sevenths included
  function otArp(t, ch, dur) {
    const f0 = fund(ch) * 2, list = ch.iv[1] === 3 ? ARP_MIN : ARP_MAJ, g = ctx.createGain(), lp = ctx.createBiquadFilter();
    lp.type = 'lowpass'; lp.Q.value = 6; lp.frequency.setValueAtTime(900, t); lp.frequency.exponentialRampToValueAtTime(4200, t + dur * 0.5); lp.frequency.exponentialRampToValueAtTime(1200, t + dur);
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(0.03, t + 0.02); g.gain.setValueAtTime(0.03, t + dur * 0.8); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    const o = pulse(t, f0 * list[0], dur, lp, 0.12, 0.3);
    for (let k = 0, tt = t; tt < t + dur; k++, tt += STEP / 3) { let h = list[k % list.length]; while (f0 * h > 1400) h /= 2; o.frequency.setValueAtTime(f0 * h, tt); }
    const p = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
    if (p) { p.pan.setValueAtTime(-0.5, t); p.pan.linearRampToValueAtTime(0.5, t + dur); g.connect(p); }
    lp.connect(g); out(p || g, 1, 0.5);
  }
  // glass lead: a SID pulse blended with two triangles a pure fifth apart, ring-modulated into a bell-like tone
  function glassLead(t, n, dur) {
    const f = n.f || hz(n.m), g = ctx.createGain(), pk = 0.08;
    g.gain.setValueAtTime(0.0001, t); g.gain.linearRampToValueAtTime(pk, t + 0.006); g.gain.exponentialRampToValueAtTime(pk * 0.7, t + Math.min(0.12, dur * 0.5));
    g.gain.setValueAtTime(pk * 0.7, t + dur); g.gain.exponentialRampToValueAtTime(0.0001, t + dur + 0.12);
    out(g, 1, 0.5);
    const p = pulse(t, f, dur, g, 0.3, 0.7);
    const car = ctx.createOscillator(), mod = ctx.createOscillator(), ring = ctx.createGain(), rl = ctx.createGain();
    car.type = 'triangle'; mod.type = 'triangle'; car.frequency.setValueAtTime(f, t); mod.frequency.setValueAtTime(f * 3 / 2, t);
    ring.gain.value = 0; mod.connect(ring.gain); car.connect(ring); rl.gain.value = 1.3; ring.connect(rl); rl.connect(g);
    const vib = ctx.createOscillator(), vg = ctx.createGain(); vib.frequency.value = 5.4; vg.gain.setValueAtTime(0, t); vg.gain.setValueAtTime(0, t + 0.18); vg.gain.linearRampToValueAtTime(f * 0.012, t + 0.45);
    vib.connect(vg); vg.connect(p.frequency); vg.connect(car.frequency);
    for (const x of [car, mod, vib]) { x.start(t); x.stop(t + dur + 0.15); }
  }
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
  function riser(t, L, lv = 1) {
    const n = ctx.createBufferSource(), bp = ctx.createBiquadFilter(), g = ctx.createGain();
    n.buffer = noise; n.loop = true; bp.type = 'bandpass'; bp.Q.value = 2.5;
    bp.frequency.setValueAtTime(300, t); bp.frequency.exponentialRampToValueAtTime(7000, t + L);
    g.gain.setValueAtTime(0.008 * lv, t); g.gain.exponentialRampToValueAtTime(0.2 * lv, t + L); g.gain.linearRampToValueAtTime(0.0001, t + L + 0.05);
    n.connect(bp); bp.connect(g); out(g, 1, 0.4); n.start(t); n.stop(t + L + 0.1);
  }
  // jet flyby: filtered noise roar + turbine whine with a doppler drop as it passes, panned left to right
  function jet(t, L, lv, pk = 0.58) {
    const mid = t + L * pk, end = t + L;
    const pan = ctx.createStereoPanner ? ctx.createStereoPanner() : null, bus = ctx.createGain(); bus.gain.value = lv;
    if (pan) { pan.pan.setValueAtTime(-0.9, t); pan.pan.linearRampToValueAtTime(0.9, end); bus.connect(pan); out(pan, 1, 0.35); } else out(bus, 1, 0.35);
    const roar = ctx.createBufferSource(), bp = ctx.createBiquadFilter(), rg = ctx.createGain();
    roar.buffer = noise; roar.loop = true; bp.type = 'bandpass'; bp.Q.value = 0.9;
    bp.frequency.setValueAtTime(700, t); bp.frequency.exponentialRampToValueAtTime(2600, mid); bp.frequency.exponentialRampToValueAtTime(380, end);
    rg.gain.setValueAtTime(0.25, t); rg.gain.exponentialRampToValueAtTime(0.5, mid); rg.gain.exponentialRampToValueAtTime(0.0001, end);
    roar.connect(bp); bp.connect(rg); rg.connect(bus);
    const rum = ctx.createBufferSource(), lp = ctx.createBiquadFilter(), ug = ctx.createGain();
    rum.buffer = noise; rum.loop = true; lp.type = 'lowpass'; lp.frequency.value = 180;
    ug.gain.setValueAtTime(0.4, t); ug.gain.exponentialRampToValueAtTime(0.9, mid + 0.15); ug.gain.exponentialRampToValueAtTime(0.0001, end);
    rum.connect(lp); lp.connect(ug); ug.connect(bus);
    const wg = ctx.createGain(), wbp = ctx.createBiquadFilter(); wbp.type = 'bandpass'; wbp.frequency.value = 2600; wbp.Q.value = 2;
    wg.gain.setValueAtTime(0.02, t); wg.gain.exponentialRampToValueAtTime(0.06, mid - 0.1); wg.gain.exponentialRampToValueAtTime(0.0001, end);
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
      fadeG.gain.setTargetAtTime(s.fade, t, 0.05);
      if (s.jet && !mute.drums) jet(t, s.jet, s.jetLv, s.jetPk);
      if (s.riser && !mute.drums) riser(t, s.riser, s.riserLv);
      tune = s.chord;
      vel = 0.75;
      if (s.pad && !mute.synths) brassChord(t + 0.01, s.chord, 0.6, BAR * STEP, true);
      if (s.bell && !mute.synths) { vel = s.bell; bellChord(t, s.chord); }
      if (!mute.drums) {
        for (const [d, v] of s.hits) { vel = 1; drum(Math.max(0, t + j(d === 's' || d === 'c' ? 4 : 6) + (d === 's' || d === 'c' ? 0.006 : 0)), d, v * (0.88 + 0.24 * Math.random())); }
        if (s.groove && i % 2 === 1 && !s.hits.some(([d]) => d === 's') && Math.random() < 0.1) { vel = 1; drum(t + 0.008, 's', 0.18 + 0.1 * Math.random()); }
      }
      if (s.bass && !mute.bass) { vel = (0.9 + 0.2 * Math.random()) * s.lv; bigBass(Math.max(0, t - 0.003 + j(4)), s.bass, s.bass.acc); }
      if (s.power && !mute.guitar) { vel = 0.85 + 0.2 * Math.random(); powerChord(t + j(8), s.chord, s.power.len * STEP * 0.9); }
      if (s.gtr && !mute.guitar) { vel = (0.85 + 0.2 * Math.random()) * Math.sqrt(s.lv); guitar(t + j(9), s.gtr, s.gtr.len * STEP * 0.96); }
      if (s.syn && !mute.synths) { vel = 0.9 + 0.15 * Math.random(); synthLead(t + j(6), s.syn, s.syn.len * STEP * 0.92); }
      if (s.glass && !mute.synths) { vel = (s.glass.h ? 1 : 0.55) * (0.9 + 0.15 * Math.random()); glassLead(t + j(5), s.glass, s.glass.len * STEP * 0.92); }
      if (s.ot && !mute.synths) { vel = 0.8; otArp(t, s.chord, BAR * STEP); }
      if (!mute.synths) {
        for (const n of s.p1) { vel = 0.6 + 0.12 * Math.random(); piano(t + j(7), n, n.len * STEP * 0.95, -0.6); }
        for (const n of s.p2) { vel = 0.6 + 0.12 * Math.random(); piano(t + j(7), n, n.len * STEP * 0.95, 0.6); }
      }
    } catch (e) {}
    vel = 1; tune = null;
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
      fadeG.gain.cancelScheduledValues(ctx.currentTime); fadeG.gain.setValueAtTime(1, ctx.currentTime);
      playing = true; idx = 0; next = ctx.currentTime + 0.1;
      timer = setInterval(tick, 25); tick();
    },
    stop() { playing = false; clearInterval(timer); timer = null; if (master && ctx) master.gain.setTargetAtTime(0, ctx.currentTime, 0.08); },
    volume(v) { vol = v; if (master && ctx && playing) master.gain.setTargetAtTime(v, ctx.currentTime, 0.3); },
    renderAll(audioCtx, dest, from = 0, to = steps.length) {
      ctx = audioCtx; build(dest); fadeG.gain.value = steps[from].fade;
      const saved = listeners.splice(0);
      for (let i = from; i < to; i++) schedule(i, 0.05 + (i - from) * STEP);
      listeners.push(...saved);
      return 0.05 + (to - from) * STEP + 3;
    }
  };
})();
// ---- pop music end ----
