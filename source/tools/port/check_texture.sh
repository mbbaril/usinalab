#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compare texture reference and adapted output sample-for-sample.
# Record reference hashes; the reference build requires compiler extensions.
# Usage: bash tools/port/check_texture.sh UPSTREAM_DIR (the folder holding the upstream clones).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
USINA="$(cd "$HERE/../.." && pwd)"
UP="${1:?usage: bash tools/port/check_texture.sh UPSTREAM_DIR}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FLAGS="-std=c++17 -O2 -w"

E="$UP/eurorack/beads"
echo "upstream: eurorack $(git -C "$UP/eurorack" rev-parse --short HEAD), stmlib $(git -C "$UP/stmlib" rev-parse --short HEAD)"
g++ $FLAGS -DTEST -DTX_UPSTREAM -I"$HERE/stub" -I"$UP" -I"$UP/eurorack" -I"$HERE" \
    "$HERE/texture_check_main.cpp" \
    "$E/dsp/processor.cc" "$E/dsp/trigger_generator.cc" "$E/dsp/delay_clock.cc" "$E/dsp/delay_controller.cc" \
    "$E/dsp/mu_law.cc" "$E/gain_control.cc" "$E/resources.cc" \
    "$UP/stmlib/dsp/units.cc" "$UP/stmlib/utils/random.cc" \
    -o "$WORK/upstream"

T="$USINA/src/fx/texture"
g++ $FLAGS -I"$USINA/src" -I"$HERE" \
    "$HERE/texture_check_main.cpp" \
    "$T/dsp/processor.cc" "$T/dsp/trigger_generator.cc" "$T/dsp/delay_clock.cc" "$T/dsp/delay_controller.cc" \
    "$T/dsp/mu_law.cc" "$T/gain_control.cc" "$T/resources.cc" \
    "$USINA/src/port/dsputil/dsp/units.cc" "$USINA/src/port/dsputil/utils/random.cc" \
    -o "$WORK/port"

mkdir -p "$WORK/u" "$WORK/p"
"$WORK/upstream" "$WORK/u" > "$WORK/u.txt"
"$WORK/port" "$WORK/p" > "$WORK/p.txt"

fail=0
while read -r n hash name; do
    if cmp -s "$WORK/u/$n.raw" "$WORK/p/$n.raw"; then
        printf '  ok    %-40s %s\n' "$name" "$hash"
    else
        printf '  FAIL  %-40s first difference at byte %s\n' "$name" "$(cmp "$WORK/u/$n.raw" "$WORK/p/$n.raw" | awk '{print $5}')"
        fail=1
    fi
done < "$WORK/u.txt"

{
    echo "# upstream's output hashes per scenario (tools/port/texture_scenarios.h), from tools/port/check_texture.sh"
    echo "# upstream $(git -C "$UP/eurorack" rev-parse --short HEAD), $(git -C "$UP/stmlib" rev-parse --short HEAD)"
    cat "$WORK/u.txt"
} > "$USINA/tests/data/texture_upstream.txt"

if [ "$fail" = 0 ]; then
    echo "ALL BIT-EXACT ($(wc -l < "$WORK/u.txt") scenarios); hashes in tests/data/texture_upstream.txt"
else
    echo "DIFFERENCES"
    exit 1
fi
