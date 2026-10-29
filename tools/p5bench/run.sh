#!/bin/sh
# Runs p5bench under callgrind with a Pentium-like cache (8 KB 2-way L1 code and data, 32-byte lines,
# 256 KB L2), every scene at 320x200, 640x480 and 800x600 (RES= picks), full and retro detail, plus the audio cases.
# Run inside the a4c-p5 container (podman run --arch 386), then callgrind_annotate each .cg and
# tools/p5bench/summarize.py. Test tool only.
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/p5bench
OUT=$B/cg
mkdir -p "$OUT"
export A4C_ROOT=$R GLIBC_TUNABLES=glibc.cpu.hwcaps=-SSE2,-SSE,-SSE3,-SSSE3,-SSE4_1,-SSE4_2,-AVX,-AVX2,-FMA,-AVX512F
CG="valgrind --tool=callgrind --toggle-collect=bench_* --cache-sim=yes --I1=8192,2,32 --D1=8192,2,32 --LL=262144,4,32"
FR=${FRAMES:-4}
for s in dogfight deck cic; do
  for r in ${RES:-320x200 640x480 800x600}; do
    for d in full retro; do
      ( $CG --callgrind-out-file=$OUT/$s-$r-$d.cg $B/p5bench scene $s ${r%x*} ${r#*x} $d $FR > $OUT/$s-$r-$d.log 2>&1 ) &
    done
  done
done
( $CG --callgrind-out-file=$OUT/sfx.cg $B/p5bench sfx 2 > $OUT/sfx.log 2>&1 ) &
( $CG --callgrind-out-file=$OUT/synth-sid3.cg $B/p5bench synth sid3 2 > $OUT/synth-sid3.log 2>&1 ) &
( $CG --callgrind-out-file=$OUT/synth-h2h.cg $B/p5bench synth h2h 2 > $OUT/synth-h2h.log 2>&1 ) &
wait
echo done
