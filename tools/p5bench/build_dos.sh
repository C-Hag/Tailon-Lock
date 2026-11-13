#!/bin/sh
# Builds p5bench as a DOS program with the DJGPP cross-compiler (i586-pc-msdosdjgpp-gcc on the PATH, or DJGPP=
# its folder), for the FreeDOS test (docs/PENTIUM.md): 32-bit protected mode under the CWSDPMI extender,
# -march=pentium, x87 maths. Test tool only; the compiler and the extender never ship.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$R/build/p5dos}
CC=${DJGPP:+$DJGPP/bin/}i586-pc-msdosdjgpp-gcc
mkdir -p "$OUT"
CORE="world mesh game sim ai jets map defend json glb crypto"
GAME="assets raster render3d stores hud cockpit command hangar intro cic paint sfx synth figure room"
SRC="$R/tools/p5bench/p5bench.c $R/tools/p5bench/stubs.c $R/src/platform/plat_common.c $R/src/platform/plat_posix.c"
for f in $CORE; do SRC="$SRC $R/src/core/$f.c"; done
for f in $GAME; do SRC="$SRC $R/src/$f.c"; done
$CC -std=gnu11 -march=pentium -mtune=pentium -O2 -include "$R/tools/p5bench/dos_compat.h" -I"$R/src" -DTL_VERSION='"p5bench"' -o "$OUT/p5bench.exe" $SRC -lm
echo "$OUT/p5bench.exe"
