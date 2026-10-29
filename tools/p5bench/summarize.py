#!/usr/bin/env python3
"""summarize.py - the Pentium benchmark table from the callgrind runs (tools/p5bench/run.sh): instructions per
frame, the share of the sim, estimated Pentium 120 frame rates at 0.5, 0.8 and 1.1 instructions per clock, and at
0.8 plus a memory-stall model; then per-second audio costs; with names, the top functions of those runs.
Run in build/p5bench/cg after callgrind_annotate. Test tool only. Original code for this project."""
import re, glob, os, sys
def rows(path):
    out = {}
    for l in open(path):
        m = re.match(r'\s*([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+([\d,]+) \([^)]*\)\s+(.*)$', l)
        if not m: continue
        v = [int(x.replace(',', '')) for x in m.groups()[:9]]
        fn = m.group(10).split(':')[-1].split(' ')[0]
        out.setdefault(fn, v)
    return out
MHZ = 120e6
res = {}
for f in sorted(glob.glob('*-*-*.incl.txt')):
    sc, r, d = f.replace('.incl.txt', '').split('-')
    R = rows(f)
    work = [k for k in R if k.startswith('bench_')]
    tot = [sum(R[k][i] for k in work) for i in range(9)]
    fr = 4
    ir = tot[0] / fr
    ir_sim = sum(R[k][0] for k in work if k in ('bench_sim', 'bench_intro_update')) / fr
    rd1, wd1, i1 = tot[4] / fr, tot[5] / fr, tot[3] / fr
    llr, llw = (tot[7] + tot[6]) / fr, tot[8] / fr
    # stall model, P54C 120 MHz on a 60 MHz bus: an L1 line fill from L2 about 12 clocks, from DRAM about 30;
    # a write that misses L1 (no write-allocate on the P5) about 3 through the write buffer
    stall = (rd1 + i1) * 12 + llr * 18 + wd1 * 3 + llw * 10
    fps = lambda ipc, st=0: MHZ / (ir / ipc + st)
    res[(sc, r, d)] = (ir, ir_sim, fps(0.5), fps(0.8), fps(1.1), fps(0.8, stall), stall / (ir / 0.8 + stall))
print('%-9s %-8s %-6s %9s %7s %6s %6s %6s %8s %6s' % ('scene', 'res', 'detail', 'Minstr/f', 'sim %', 'fps.5', 'fps.8', 'fps1.1', 'fps.8+mem', 'stall%'))
for k, v in sorted(res.items()):
    print('%-9s %-8s %-6s %9.1f %6.1f%% %6.2f %6.2f %6.2f %8.2f %5.0f%%' % (k + (v[0] / 1e6, 100 * v[1] / v[0], v[2], v[3], v[4], v[5], 100 * v[6])))
for f in ['sfx', 'synth-sid3', 'synth-h2h']:
    R = rows(f + '.incl.txt')
    ir = sum(v[0] for k, v in R.items() if k.startswith('bench_')) / 2.0  # per second of audio at 48 kHz
    print('%-11s %7.1f Minstr per s at 48 kHz; at 22.05 kHz %6.1f M = %4.0f%% of a P120 at IPC 0.8' % (f, ir / 1e6, ir * 22050 / 48000 / 1e6, 100 * ir * 22050 / 48000 / (0.8 * MHZ)))

def self_top(name, n=10):
    R = rows(name + '.self.txt')
    R.pop('TOTALS', None)
    tot = sum(v[0] for k, v in R.items())
    items = sorted(((v[0], k) for k, v in R.items() if k not in ('TOTALS',)), reverse=True)
    # the first row of the self file is the program total
    return items
if len(sys.argv) > 1:
    for name in sys.argv[1:]:
        it = self_top(name)
        total = it[0][0]
        print('==', name)
        for ir, k in it[1:13]:
            print('  %5.1f%%  %s' % (100.0 * ir / total, k))
