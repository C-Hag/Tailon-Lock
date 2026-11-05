// sidfx.js - the game's sound effects played live: a SID-style three-voice chip and a frame driver that plays our
// effect songs (assets/sfx/*.fxs, the format in docs/SFX_FORMAT.md). The voices: triangle, sawtooth, pulse and
// noise oscillators on a 24-bit phase, ring modulation, the chip's attack-decay-sustain-release envelope, a
// resonant state-variable filter. The driver steps every instrument's wave, pulse and filter programs once a PAL
// frame (50 a second) and applies vibrato and slides. Written for this project from the chip's published behaviour
// (its register map, its oscillator and envelope timing); no emulator code. The C game has the same engine in
// src/sidfx.c; both make the same samples.
const SidFx = (() => {
  const CLOCK = 985248, FRAME = 1 / 50;
  // fitted to the effects as the 6581 chip played them: the filter's cutoff curve (Hz from the 11-bit register,
  // steep at the top like the chip's), the DC each voice's output carries (so a note starting or ending thumps, as
  // on the chip), and the output's gentle top roll-off
  const FC0 = 30, FC1 = 5000, FCP = 3, DC = 1.5, OUT_LP = 14000;
  // the envelope's times for each rate setting, in seconds: attack, and decay or release (three times as long)
  const ATTACK = [0.002, 0.008, 0.016, 0.024, 0.038, 0.056, 0.068, 0.08, 0.1, 0.25, 0.5, 0.8, 1, 3, 5, 8];
  const DECAY = ATTACK.map(v => v * 3);

  // ---- the song: parsed from its text ----
  function parse(text) {
    const S = { play: { once: 1 }, tempo: 1, volume: 15, waves: {}, pulses: {}, filters: {}, insts: {}, cmds: {}, tracks: [[], [], []], loops: [null, null, null] };
    let block = null, track = -1;
    const hex = s => s.endsWith('.') ? parseInt(s, 10) : parseInt(s, 16);
    const sx = s => { const v = hex(s.replace('-', '')); return s.startsWith('-') ? -v : v; };
    for (const raw of text.split('\n')) {
      const line = raw.replace(/\s#.*$/, '').replace(/^#.*$/, '').trimEnd();
      if (!line.trim()) continue;
      const w = line.trim().split(/\s+/);
      if (!/^\s/.test(line)) {
        block = null; track = -1;
        if (w[0] === 'play') S.play = w[1] === 'loop' ? { loop: parseInt(w[2], 10) } : { once: parseFloat(w[2]) };
        else if (w[0] === 'tempo') S.tempo = parseInt(w[1], 10);
        else if (w[0] === 'volume') S.volume = hex(w[1]);
        else if (w[0] === 'wave') block = S.waves[w[1]] = [];
        else if (w[0] === 'pulse') block = S.pulses[w[1]] = [];
        else if (w[0] === 'filter') block = S.filters[w[1]] = [];
        else if (w[0] === 'instrument') {
          const I = { ad: 0, sr: 0, wave: null, pulse: null, filter: null, filteron: false };
          for (const kv of w.slice(2)) {
            const [k, v] = kv.split('=');
            if (k === 'ad' || k === 'sr') I[k] = hex(v);
            else if (k === 'flags') I.filteron = v.split(',').includes('filteron');
            else I[k] = v;
          }
          S.insts[w[1]] = I;
        } else if (w[0] === 'command') {
          S.cmds[w[1]] = w[2] === 'vib' ? { vib: [hex(w[3]), hex(w[4])] } : w[2] === 'slide' ? { slide: (hex(w[3]) << 16) >> 16 } : {};
        } else if (w[0] === 'track') track = parseInt(w[1], 10) - 1;
        continue;
      }
      if (track >= 0) {
        for (const tok of w) {
          if (tok === '@loop') { S.loops[track] = S.tracks[track].length; continue; }
          S.tracks[track].push(tok);
        }
      } else if (block) {
        if (w[0] === 'end') block.push({ end: true });
        else if (w[0] === 'loop') block.push({ loop: hex(w[1]) });
        else if (w[0] === 'set') block.push(w.length > 3 ? { set: hex(w[2]), mode: w[1], res: hex(w[3]), mask: hex(w[4]) } : { set: hex(w[1]), hold: hex(w[2]) });
        else if (w[0] === 'add') block.push({ add: sx(w[1]), n: hex(w[2]) });
        else block.push({ wf: hex(w[0]), abs: w[1][0] === '=', note: w[1][0] === '=' ? hex(w[1].slice(1)) : parseInt(w[1], 10) });
      }
    }
    return S;
  }
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  // a track's note name (C-4, F#3) as its number, A-4 = 57
  const noteNum = s => NOTE[s[0]] + (s[1] === '#' ? 1 : 0) + 12 * parseInt(s.slice(2), 10);
  const freqReg = n => Math.round(440 * Math.pow(2, (n - 57) / 12) * 16777216 / CLOCK);

  // ---- a program (wave, pulse or filter) stepping once a frame ----
  function prog(rows) { return { rows: rows || [], i: 0, left: 0 }; }

  // ---- rendering ----
  // the song's samples at rate for seconds, the three voices mixed, before any level change
  function render(S, seconds, rate) {
    const n = Math.round(seconds * rate), out = new Float32Array(n), perFrame = rate * FRAME;
    const V = [0, 1, 2].map(() => ({ phase: 0, freq: 0, base: 0, wf: 0, gate: false, pw: 0x800, env: 0, stage: 'r', ad: 0, sr: 0, lfsr: 0x7ffff8, noise: 0,
      inst: null, wave: prog(), pulse: prog(), slide: 0, vib: null, vt: 0, note: 0, filt: false, msb: 0 }));
    const F = { mode: 'lp', cut: 0, res: 0, mask: 0, prog: prog(), lp: 0, bp: 0, o: 0 }, outK = 1 - Math.exp(-2 * Math.PI * OUT_LP / rate);
    const pos = [0, 0, 0];
    let frameAt = 0, sample = 0;
    const doFrame = () => {
      for (let v = 0; v < 3; v++) {
        const T = S.tracks[v], vo = V[v];
        if (!T.length) continue;
        if (pos[v] >= T.length) pos[v] = S.loops[v] != null ? S.loops[v] : T.length;
        const tok = pos[v] < T.length ? T[pos[v]] : '+';
        pos[v]++;
        if (tok === '-') vo.gate = false;
        else if (tok !== '+') {
          const [nm, iname, cname] = tok.split(':'), I = S.insts[iname];
          vo.note = noteNum(nm); vo.inst = I; vo.gate = true; vo.slide = 0; vo.vt = 0;
          vo.ad = I.ad; vo.sr = I.sr; vo.stage = 'a'; vo.env = 0;      // a hard restart: the envelope starts from silence
          vo.wave = prog(S.waves[I.wave]); vo.pulse = prog(S.pulses[I.pulse]); vo.filt = I.filteron;
          const C = cname ? S.cmds[cname] : null;
          vo.vib = C && C.vib ? C.vib : null; vo.slideRate = C && C.slide ? C.slide : 0;
          if (I.filter) F.prog = prog(S.filters[I.filter]);
        }
        if (!vo.inst) continue;
        // the wave program: a waveform and a note each frame, until it ends or loops
        const W = vo.wave;
        for (let guard = 0; guard < 4 && W.i < W.rows.length; guard++) {
          const r = W.rows[W.i];
          if (r.loop != null) { W.i = r.loop; continue; }
          if (r.end) break;
          vo.wf = r.wf; vo.base = r.abs ? r.note : vo.note + r.note; W.i++;
          break;
        }
        // the pulse program: set the width, or add to it for a number of frames
        const P = vo.pulse;
        for (let guard = 0; guard < 4 && P.i < P.rows.length; guard++) {
          const r = P.rows[P.i];
          if (r.loop != null) { P.i = r.loop; P.left = 0; continue; }
          if (r.end) break;
          if (r.set != null) { if (!P.left) { vo.pw = r.set; P.left = r.hold; } if (--P.left <= 0) { P.i++; P.left = 0; } break; }
          if (!P.left) P.left = r.n;
          vo.pw = (vo.pw + r.add) & 0xfff;
          if (--P.left <= 0) { P.i++; P.left = 0; }
          break;
        }
        vo.slide += vo.slideRate;
        let semis = 0;
        if (vo.vib) { const per = Math.max(1, vo.vib[0]) * 2, ph = (vo.vt++ % per) / per; semis = (ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4) * 0.3 * vo.vib[1]; }
        vo.freq = Math.max(0, Math.min(0xffff, Math.round(freqReg(vo.base + semis)) + vo.slide));
      }
      // the filter program
      const FP = F.prog;
      for (let guard = 0; guard < 4 && FP.i < FP.rows.length; guard++) {
        const r = FP.rows[FP.i];
        if (r.loop != null) { FP.i = r.loop; FP.left = 0; continue; }
        if (r.end) break;
        if (r.set != null) { F.mode = r.mode; F.cut = r.set; F.res = r.res; F.mask = r.mask; FP.i++; break; }
        if (!FP.left) FP.left = r.n;
        F.cut = Math.max(0, Math.min(0x7ff, F.cut + r.add));
        if (--FP.left <= 0) { FP.i++; FP.left = 0; }
        break;
      }
    };
    const step = CLOCK / rate;   // chip clocks per sample
    for (let i = 0; i < n; i++) {
      if (sample >= frameAt) { doFrame(); frameAt += perFrame; }
      sample++;
      let mix = 0, filt = 0;
      const osc = [0, 0, 0];
      for (let v = 0; v < 3; v++) {
        const vo = V[v], wf = vo.wf, test = wf & 8;
        // the envelope: attack to full, decay to the sustain level, release to silence; decay and release exponential
        const gate = vo.gate && (wf & 1);
        if (!gate && vo.stage !== 'r') vo.stage = 'r';
        if (vo.stage === 'a') { vo.env += 1 / (ATTACK[vo.ad >> 4] * rate); if (vo.env >= 1) { vo.env = 1; vo.stage = 'd'; } }
        else if (vo.stage === 'd') { const s = (vo.sr >> 4) / 15; vo.env = s + (vo.env - s) * Math.exp(-4.6 / (DECAY[vo.ad & 15] * rate)); }
        else vo.env *= Math.exp(-4.6 / (DECAY[vo.sr & 15] * rate));
        // the oscillator: a 24-bit phase; noise steps its shift register whenever phase bit 19 rises
        if (test) { vo.phase = 0; osc[v] = 0; continue; }
        const prev = vo.phase;
        vo.phase = (vo.phase + vo.freq * step) % 16777216;
        if (((prev ^ vo.phase) & 0x80000) && (vo.phase & 0x80000)) {
          const bit = ((vo.lfsr >> 22) ^ (vo.lfsr >> 17)) & 1;
          vo.lfsr = ((vo.lfsr << 1) | bit) & 0x7fffff;
        }
        const ph = vo.phase / 16777216;
        let o = 0, k = 0;
        if (wf & 0x10) { // the triangle, its top bit flipped by the voice before it when ring-modulated
          let t = ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4;
          if (wf & 4) { const m = V[(v + 2) % 3].phase / 16777216; if (m >= 0.5) t = -t; }
          o += t; k++;
        }
        if (wf & 0x20) { o += ph * 2 - 1; k++; }
        if (wf & 0x40) { o += ph < vo.pw / 4096 ? 1 : -1; k++; }
        if (wf & 0x80) { o += (((vo.lfsr >> 15) & 0xff) / 127.5) - 1; k++; }
        o = k ? o / k + DC : 0;
        osc[v] = o * vo.env;
        if (vo.filt && (F.mask >> v) & 1) filt += osc[v]; else mix += osc[v];
      }
      // the filter: a two-pole state-variable filter, its cutoff from the 11-bit register, its resonance 0-15
      const fc = FC0 + FC1 * Math.pow(F.cut / 2047, FCP), g = 2 * Math.sin(Math.PI * Math.min(fc, rate / 6) / rate), q = 1 / (0.707 + F.res / 15 * 2.4);
      const hp = filt - F.lp - q * F.bp;
      F.bp += g * hp; F.lp += g * F.bp;
      const fo = F.mode === 'lp' ? F.lp : F.mode === 'bp' ? F.bp : F.mode === 'hp' ? hp : F.lp + hp;
      const y = (mix + fo) * (S.volume / 15) / 3;
      F.o += outK * (y - F.o);
      out[i] = F.o;
    }
    return out;
  }
  // an effect ready to play, as the game plays it: rendered, its DC taken out, its peak at -1 dB; a one-shot faded
  // at its end; a loop cut from the steady part after two passes, its end crossfaded into its start over 30 ms so
  // it repeats without a seam. Returns { samples, loop }.
  function effect(S, rate) {
    const loop = S.play.loop, lead = loop ? loop * 2 * FRAME : 0, len = loop ? loop * FRAME : S.play.once;
    const x = render(S, lead + len, rate);
    let y = 0, xp = 0, peak = 0;
    for (let i = 0; i < x.length; i++) { const v = x[i]; y = v - xp + 0.995 * y; xp = v; x[i] = y; }   // the DC
    const L = Math.round(lead * rate), N = x.length - L, out = new Float32Array(N), xf = Math.round(0.03 * rate);
    for (let i = 0; i < N; i++) out[i] = x[L + i];
    if (loop) for (let i = 0; i < xf; i++) { const w = i / xf; out[N - xf + i] = x[L + N - xf + i] * (1 - w) + x[L - xf + i] * w; }
    else for (let i = 0; i < xf; i++) out[N - 1 - i] *= i / xf;
    for (let i = 0; i < N; i++) peak = Math.max(peak, Math.abs(out[i]));
    const k = peak ? 0.891 / peak : 1;
    for (let i = 0; i < N; i++) out[i] *= k;
    return { samples: out, loop: !!loop };
  }
  return { parse, render, effect, freqReg };
})();
if (typeof module !== 'undefined') module.exports = SidFx;
