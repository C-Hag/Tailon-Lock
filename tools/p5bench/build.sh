#!/bin/sh
# Builds p5bench, the Pentium benchmark, as a static 32-bit x87 program; run inside
# the tl-p5 container (32-bit Debian with gcc and valgrind). P5_LINK= builds it dynamic, which valgrind
# needs (it cannot run a static 32-bit glibc program). Test tool only.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$R/build/p5bench}
mkdir -p "$OUT"
CORE="world mesh game sim ai jets map defend json glb crypto"
GAME="assets raster render3d stores hud cockpit command hangar intro cic paint sfx synth figure room"
SRC="$R/tools/p5bench/p5bench.c $R/tools/p5bench/stubs.c $R/src/platform/plat_common.c $R/src/platform/plat_posix.c"
for f in $CORE; do SRC="$SRC $R/src/core/$f.c"; done
for f in $GAME; do SRC="$SRC $R/src/$f.c"; done
gcc -std=c11 -D_GNU_SOURCE -m32 -march=pentium -mtune=pentium -mno-mmx -mno-sse -mfpmath=387 -O2 -g ${P5_LINK--static} \
  -I"$R/src" -DTL_VERSION='"p5bench"' -DTL_SOURCE_DIR="\"$R\"" -o "$OUT/p5bench" $SRC -lm
echo "$OUT/p5bench"
