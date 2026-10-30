#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compare LPG reference and adapted output sample-for-sample at 48 kHz.
# Usage: bash tools/port/check_lpg.sh UPSTREAM_DIR (the folder holding the upstream clones).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
USINA="$(cd "$HERE/../.." && pwd)"
UP="${1:?usage: bash tools/port/check_lpg.sh UPSTREAM_DIR}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FLAGS="-std=c++17 -O2 -w"

echo "upstream: eurorack $(git -C "$UP/eurorack" rev-parse --short HEAD), stmlib $(git -C "$UP/stmlib" rev-parse --short HEAD)"
g++ $FLAGS -DLPG_UPSTREAM -I"$UP" -I"$UP/eurorack" "$HERE/lpg_check_main.cpp" "$UP/stmlib/dsp/units.cc" -o "$WORK/upstream"
g++ $FLAGS -I"$USINA/src" "$HERE/lpg_check_main.cpp" "$USINA/src/port/dsputil/dsp/units.cc" -o "$WORK/port"

mkdir -p "$WORK/u" "$WORK/p"
"$WORK/upstream" "$WORK/u" > "$WORK/u.txt"
"$WORK/port" "$WORK/p" > "$WORK/p.txt"

fail=0
while read -r n hash name; do
    if cmp -s "$WORK/u/$n.raw" "$WORK/p/$n.raw"; then
        printf '  ok    %-44s %s\n' "$name" "$hash"
    else
        printf '  DIFF  %-44s\n' "$name"
        fail=1
    fi
done < "$WORK/u.txt"
[ "$fail" = 0 ] && echo "the port matches upstream, bit for bit" || { echo "the port differs from upstream"; exit 1; }
