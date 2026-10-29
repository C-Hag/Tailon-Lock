#!/usr/bin/env node
// Pull every note out of the Web Audio music engines, so the C game can play
// them on its own SID-style synth instead of streaming the mp3 renders.
//
//   node tools/extract_scores.js music/engines assets/music
//
// Each engine runs headless against a stub AudioContext through its own
// renderAll(). Its instrument functions (the ones schedule() calls) are
// wrapped so each top-level call is logged with its time, arguments and the
// engine's current velocity. The output per theme is assets/music/<key>.json:
// { key, length, loop, events: [[t, function, vel, ...args]] for drums and
// effects, notes: [[t, instrument, midi, dur, vel]] for everything pitched }.
'use strict';
const fs = require('fs'), path = require('path');

const ENGINES = {
  horizon: { file: 'music_horizon.js', name: 'HorizonMusic' },
  pop: { file: 'music_pop.js', name: 'PopMusic' },
  h2h: { file: 'music_h2h.js', name: 'H2HMusic' },
  anthem: { file: 'music_anthem.js', name: 'AnthemMusic' },
  strike: { file: 'music_strike.js', name: 'StrikeMusic' },
  cine: { file: 'music_cine.js', name: 'CineMusic' },
  overtone: { file: 'music_overtone.js', name: 'OvertoneMusic' },
  chip: { html: 'web/index.html', start: '// ---- music start ----', end: '// ---- music end ----', name: 'Music' },
};
// helpers that are not instruments
const SKIP = new Set(['place', 'alberti', 'broken', 'thirdBelow', 'impulse', 'build', 'schedule', 'tick', 'chordOf', 'send', 'env', 'out']);

// ---- a stub Web Audio graph: every node accepts every call ----
// Oscillators are the exception: each one is logged as a note (start, stop,
// first frequency) tagged with the instrument call that made it, which is how
// arpeggios, sequences and chord voicings come out exactly as the engine
// built them.
let OSC = null, CUR = null;
function param(v = 0) {
  return { value: v, setValueAtTime() { return this; }, linearRampToValueAtTime() { return this; }, exponentialRampToValueAtTime() { return this; },
    setTargetAtTime() { return this; }, cancelScheduledValues() { return this; }, setValueCurveAtTime() { return this; }, cancelAndHoldAtTime() { return this; } };
}
const PARAMS = new Set(['gain', 'frequency', 'detune', 'Q', 'pan', 'delayTime', 'playbackRate', 'threshold', 'knee', 'ratio',
  'attack', 'release', 'offset', 'reduction']);
function node() {
  const n = new Proxy({}, {
    get(o, k) {
      if (k in o) return o[k];
      if (k === 'connect') return (d) => d;
      if (k === 'disconnect' || k === 'start' || k === 'stop' || k === 'setPeriodicWave') return () => {};
      if (PARAMS.has(k)) return (o[k] = param(1));
      return undefined;
    },
    set(o, k, v) { o[k] = v; return true; },
  });
  return n;
}
class Ctx {
  constructor() { this.currentTime = 0; this.sampleRate = 48000; this.destination = node(); this.state = 'running'; }
  createBuffer(ch, len) { const d = Array.from({ length: ch }, () => new Float32Array(len)); return { length: len, numberOfChannels: ch, sampleRate: 48000, getChannelData: i => d[i] }; }
  createPeriodicWave() { return {}; }
}
Ctx.prototype.createOscillator = function () {
  const o = node(), rec = { inst: CUR ? CUR.name : null, vel: CUR ? CUR.vel : 1, f: null, pts: [], t0: null, t1: null, type: 'sine' };
  const freq = param(440);
  /* pitch points: [time, value, reached by a ramp?] */
  freq.setValueAtTime = function (v, t) { rec.pts.push([t, v, false]); return this; };
  freq.exponentialRampToValueAtTime = function (v, t) { rec.pts.push([t, v, true]); return this; };
  freq.linearRampToValueAtTime = freq.exponentialRampToValueAtTime;
  Object.defineProperty(freq, 'value', { get() { return rec.f === null ? 440 : rec.f; }, set(v) { rec.f = v; } });
  o.frequency = freq;
  o.start = t => { rec.t0 = t || 0; if (OSC) OSC.push(rec); };
  o.stop = t => { rec.t1 = t; };
  Object.defineProperty(o, 'type', { get() { return rec.type; }, set(v) { rec.type = v; } });
  return o;
};
for (const k of ['Gain', 'BiquadFilter', 'BufferSource', 'Convolver', 'DynamicsCompressor', 'WaveShaper', 'Delay',
  'StereoPanner', 'ChannelMerger', 'ChannelSplitter', 'ConstantSource', 'Analyser', 'IIRFilter'])
  Ctx.prototype['create' + k] = node;

// ---- seeded Math.random so a re-run gives the same score ----
function seed(s) { return () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// Wraps `function NAME(args) { body }` so the outermost instrument call logs itself.
function instrument(src) {
  let out = '', i = 0;
  const re = /\n  function ([A-Za-z0-9_]+)\(([^)]*)\) \{/g;
  let m;
  while ((m = re.exec(src))) {
    const name = m[1];
    if (SKIP.has(name)) continue;
    const open = m.index + m[0].length;
    // find the matching brace (the engines keep braces out of strings in these bodies)
    let depth = 1, j = open;
    while (depth && j < src.length) { const c = src[j++]; if (c === '{') depth++; else if (c === '}') depth--; }
    out += src.slice(i, open);
    out += ` if (__depth++ === 0) __rec(${JSON.stringify(name)}, arguments, typeof vel === 'undefined' ? 1 : vel); try {` + src.slice(open, j - 1) + '} finally { if (--__depth === 0) __end(); } }';
    i = j;
    re.lastIndex = j;
  }
  return out + src.slice(i);
}

function round(v) { return typeof v === 'number' ? Math.round(v * 10000) / 10000 : v; }
function clean(a) {
  if (Array.isArray(a)) return a.map(clean);
  if (a && typeof a === 'object') { const o = {}; for (const k in a) if (typeof a[k] !== 'function') o[k] = clean(a[k]); return o; }
  return round(a);
}

function extract(key, def, root) {
  let src;
  if (def.file) src = fs.readFileSync(path.join(root, 'music/engines', def.file), 'utf8');
  else { const h = fs.readFileSync(path.join(root, def.html), 'utf8'); src = h.slice(h.indexOf(def.start), h.indexOf(def.end)); }
  const events = [];
  const rec = (name, args, vel) => {
    const a = Array.from(args).map(clean);
    events.push([a[0], name, vel, ...a.slice(1)]);
    CUR = { name, vel };
  };
  OSC = [];
  // remember when each step starts (schedule(i, t)) and which step the loop goes back to
  const meta = { starts: [], loop: 0 };
  let body = instrument(src)
    .replace(/\n  function schedule\(i, t\) \{/, '\n  function schedule(i, t) { __meta.starts[i] = t;')
    .replace(/(const LOOP = [^;\n]+;)/, '$1 __meta.loop = LOOP;');
  const code = `let __depth = 0;\n${body}\nreturn ${def.name};`;
  const saved = Math.random;
  Math.random = seed(1234);
  try {
    const eng = new Function('__rec', '__end', '__meta', 'window', 'setTimeout', 'setInterval', 'clearInterval', code)(rec, () => { CUR = null; }, meta, {}, () => 0, () => 0, () => {});
    if (eng.heavy === false) eng.heavy = true;
    const ctx = new Ctx();
    const length = eng.renderAll(ctx, node());
    // loop point: the start of the step tick() wraps around to
    const loop = meta.starts[meta.loop] || 0.05;
    events.sort((a, b) => a[0] - b[0]);
    /* the pitched parts come out as notes below; keep only the drums and effects as calls */
    const KEEP = new Set(['drum', 'hit', 'jet', 'riser', 'seaWave', 'sonar', 'swell']);
    const calls = {};
    for (const e of events) calls[e[1]] = (calls[e[1]] || 0) + 1;
    const kept = events.filter(e => KEEP.has(e[1])).map(e => e.slice(0, e[1] === 'drum' || e[1] === 'hit' ? 5 : 6));
    // oscillator notes: audible pitches only (LFOs are below 20 Hz), one per
    // instrument, start and semitone (unison layers and detuned copies merge)
    const notes = [], seen = new Set();
    const semis = f => 69 + 12 * Math.log2(f / 440);
    for (const o of OSC) {
      if (!o.inst || o.t0 === null) continue;
      const end = o.t1 === null ? o.t0 + 0.3 : o.t1;
      /* piecewise pitch: a segment that ramps into the next point is a slide and merges into it */
      let pts = o.pts.slice().sort((x, y) => x[0] - y[0]);
      if (!pts.length) pts = [[o.t0, o.f === null ? 440 : o.f, false]];
      const segs = [];
      for (let i = 0; i < pts.length; i++) {
        const t = Math.max(o.t0, pts[i][0]), nx = pts[i + 1];
        if (nx && nx[2]) continue;
        const t1 = nx ? Math.min(end, nx[0]) : end;
        if (t1 <= t) continue;
        /* a pitch reached by a ramp starts where the slide began */
        const from = pts[i][2] && i > 0 ? Math.max(o.t0, pts[i - 1][0]) : t;
        segs.push([from, t1, pts[i][1]]);
      }
      for (const [t, t1, f] of segs) {
        if (f < 20) continue;
        const mf = semis(f), m = Math.round(mf);
        if (Math.abs(mf - m) > 0.3) continue; /* FM modulators sit off the scale */
        const k = o.inst + '|' + Math.round(t * 200) + '|' + m;
        if (seen.has(k)) continue;
        seen.add(k);
        notes.push([round(t), o.inst, m, round(t1 - t), round(o.vel), o.type[0]]);
      }
    }
    notes.sort((a, b) => a[0] - b[0]);
    /* drum oscillators are part of the drum calls */
    const pitched = notes.filter(n => !KEEP.has(n[1])).map(n => n.slice(0, 5));
    return { key, length: round(length), loop: round(loop), events: kept, notes: pitched, calls };
  } finally { Math.random = saved; }
}

const [, , rootArg, outArg] = process.argv;
const root = rootArg ? path.resolve(rootArg, '..', '..') : process.cwd();
const outDir = outArg || 'assets/music';
fs.mkdirSync(outDir, { recursive: true });
for (const [key, def] of Object.entries(ENGINES)) {
  try {
    const s = extract(key, def, root);
    const counts = s.calls;
    delete s.calls;
    fs.writeFileSync(path.join(outDir, key + '.json'), JSON.stringify(s));
    const nc = {};
    for (const n of s.notes) nc[n[1]] = (nc[n[1]] || 0) + 1;
    console.log(key, s.length.toFixed(1) + ' s', 'loop ' + s.loop.toFixed(1), JSON.stringify(counts), 'notes', JSON.stringify(nc));
  } catch (e) { console.log(key, 'FAILED', e.message); }
}
