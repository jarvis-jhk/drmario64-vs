#!/bin/sh
# Build the round-setup oracle against a matching AngheloAlf/drmario64 checkout and
# print the fixture. Usage: tools/oracle/build.sh <decomp dir> > tests/fixtures/setup.txt
set -eu
D=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${ORACLE_OUT:-$(mktemp -d)}
# dm_make_magazine from the CAPSMAGAZINE macros to the end of the function, verbatim.
sed -n '/^#define CAPSMAGAZINE_GET_A/,/^}/p' "$D/src/main_segment/dm_game_main.c" > "$OUT/magazine.inc"
grep -q 'void dm_make_magazine' "$OUT/magazine.inc"
# ultratypes.h makes s32/u32 `long`, which is 64-bit on x86_64 and would hide the
# 32-bit wraparound randomseed relies on. A shim copy, first on the <> path, pins them
# to int.
mkdir -p "$OUT/shim/PR"
sed -E 's/^(typedef +(volatile +)?(unsigned +)?)long( +)(v?[su]32;)/\1int \4\5/' \
  "$D/lib/ultralib/include/PR/ultratypes.h" > "$OUT/shim/PR/ultratypes.h"
[ "$(grep -cE '^typedef +(volatile +)?(unsigned +)?int +v?[su]32;' "$OUT/shim/PR/ultratypes.h")" = 4 ]
CFLAGS="-O1 -fwrapv -w -funsigned-char -std=gnu89 -D_MIPS_SZLONG=32 -D__USE_ISOC99 -DVERSION_US=1
 -DNON_MATCHING=1 -D_LANGUAGE_C -DF3DEX_GBI_2 -DBUILD_VERSION=VERSION_L -DNDEBUG -D_FINALROM
 -I$OUT/shim -I$D/lib/ultralib/include -I$D/lib/ultralib/include/PR -I$D/lib/libmus/include
 -I$D/lib/ultralib/include/gcc -I$D/include -I$D/bin/us -I$D/build/us -I$D -I$OUT"
for f in dm_virus_init calc; do
  gcc $CFLAGS -c "$D/src/main_segment/$f.c" -o "$OUT/$f.o"
done
gcc $CFLAGS -std=gnu99 -c "$HERE/setup_oracle.c" -o "$OUT/setup_oracle.o"
gcc "$OUT/setup_oracle.o" "$OUT/dm_virus_init.o" "$OUT/calc.o" -lm -o "$OUT/setup_oracle"
"$OUT/setup_oracle"
