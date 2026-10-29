#!/bin/sh
# The FreeDOS benchmark test (docs/PENTIUM.md, R0): puts the DOS build of p5bench (build_dos.sh), the CWSDPMI
# extender and the assets (no music) on a copy of the FreeDOS 1.3 LiteUSB disk image, boots it in Bochs at a
# fixed 96 million instructions a second (a Pentium 120 at 0.8 instructions a clock), and copies back the
# frame times and the pictures. Test tools only: FreeDOS, CWSDPMI, DJGPP and Bochs are never shipped.
#   tools/p5bench/freedos/run.sh FD13LITE.img CWSDPMI.EXE [out]
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
IMG=$1 CWS=$2 OUT=${3:-$R/build/p5dos/freedos}
mkdir -p "$OUT/w/stage/BENCH/assets"
cp "$IMG" "$OUT/w/bench.img"
cp "$R/build/p5dos/p5bench.exe" "$OUT/w/stage/BENCH/P5BENCH.EXE"
cp "$CWS" "$OUT/w/stage/BENCH/CWSDPMI.EXE"
for d in jets maps intro figure; do cp -r "$R/assets/$d" "$OUT/w/stage/BENCH/assets/"; done
cp "$R/tools/p5bench/freedos/fdauto.bat" "$R/tools/p5bench/freedos/bochsrc.txt" "$OUT/w/"
podman run --rm -v "$OUT/w:/w:Z" a4c-dos:1 sh -c '
  printf "drive c: file=\"/w/bench.img\" offset=32256\nmtools_skip_check=1\n" > /tmp/m; export MTOOLSRC=/tmp/m
  mcopy -o /w/fdauto.bat c:/FDAUTO.BAT && mcopy -s -o /w/stage/BENCH c:/
  printf "c\n" | bochs -q -f /w/bochsrc.txt > /w/bochs_out.txt 2>&1 || true
  mtype c:/BENCH/RESULT.TXT
  for f in DF320 DF640 DF800 DK320 DK640 DK800 CI320 CI640 CI800; do mcopy -o c:/BENCH/$f.BMP /w/$f.bmp; done'
