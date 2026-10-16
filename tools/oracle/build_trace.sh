#!/bin/sh
# Build the per-tick trace oracle against a matching AngheloAlf/drmario64 checkout
# (built once with `make`, for the generated *.msg.inc) and write the movies.
# Usage: tools/oracle/build_trace.sh <decomp dir> <out dir> [movie [dumpfrom dumpto]]
set -eu
D=$(cd "$1" && pwd)
MOV=$2
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${ORACLE_OUT:-$(mktemp -d)}
mkdir -p "$OUT/shim/PR"
# ultratypes.h makes s32/u32 `long`, 64-bit on x86_64; pin them to int (see build.sh).
sed -E 's/^(typedef +(volatile +)?(unsigned +)?)long( +)(v?[su]32;)/\1int \4\5/' \
  "$D/lib/ultralib/include/PR/ultratypes.h" > "$OUT/shim/PR/ultratypes.h"
[ "$(grep -cE '^typedef +(volatile +)?(unsigned +)?int +v?[su]32;' "$OUT/shim/PR/ultratypes.h")" = 4 ]
CFLAGS="-O1 -fno-pie -fwrapv -w -funsigned-char -std=gnu99 -D_MIPS_SZLONG=32 -D__USE_ISOC99 -DVERSION_US=1
 -DNON_MATCHING=1 -D_LANGUAGE_C -DF3DEX_GBI_2 -DBUILD_VERSION=VERSION_L -DNDEBUG -D_FINALROM
 -I$OUT/shim -I$D/lib/ultralib/include -I$D/lib/ultralib/include/PR -I$D/lib/libmus/include
 -I$D/lib/ultralib/include/gcc -I$D/include -I$D/bin/us -I$D/build/us
 -I$D/build/us/src/main_segment -I$D"
for f in joy main1x calc dm_virus_init; do
  gcc $CFLAGS -c "$D/src/main_segment/$f.c" -o "$OUT/$f.o"
done
# The falling pill is drawn straight into the framebuffer from key_control_main, with
# textures this build never loads: that one function body is cut.
sed '/^void dm_draw_capsel_by_cpu_tentative(/a\    return;' "$D/src/main_segment/dm_game_main.c" > "$OUT/dm_game_main_oracle.c"
[ "$(grep -c '^    return;$' "$OUT/dm_game_main_oracle.c")" -gt "$(grep -c '^    return;$' "$D/src/main_segment/dm_game_main.c")" ]
gcc $CFLAGS -I"$OUT" -I"$D/src/main_segment" -I"$HERE" -c "$HERE/glue.c" -o "$OUT/glue.o"
gcc -O1 -std=gnu99 -c "$HERE/trace_oracle.c" -o "$OUT/trace_oracle.o"
gcc -O1 -std=gnu11 -c "$HERE/stubs.c" -o "$OUT/stubs.o"
gcc "$OUT"/trace_oracle.o "$OUT"/glue.o "$OUT"/stubs.o "$OUT"/joy.o "$OUT"/main1x.o "$OUT"/calc.o "$OUT"/dm_virus_init.o \
  -no-pie -lm -o "$OUT/trace_oracle"
mkdir -p "$MOV"
"$OUT/trace_oracle" "$MOV" "$@"
