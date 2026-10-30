#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compare macro-oscillator reference and adapted output for 24 models.
# Record reference hashes for trigger-patched and unpatched scenarios.
# Usage: bash tools/port/check_models.sh UPSTREAM_DIR (the folder holding the upstream clones).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
USINA="$(cd "$HERE/../.." && pwd)"
UP="${1:?usage: bash tools/port/check_models.sh UPSTREAM_DIR}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FLAGS="-std=c++17 -O2 -w"

echo "upstream: eurorack $(git -C "$UP/eurorack" rev-parse --short HEAD), stmlib $(git -C "$UP/stmlib" rev-parse --short HEAD)"
UPSRC=$(find "$UP/eurorack/plaits/dsp" -name "*.cc" ! -name "voice.cc")
g++ $FLAGS -DMODELS_UPSTREAM -DTEST -I"$UP" -I"$UP/eurorack" "$HERE/models_check_main.cpp" $UPSRC \
    "$UP/eurorack/plaits/resources.cc" "$UP/stmlib/dsp/units.cc" "$UP/stmlib/utils/random.cc" -o "$WORK/upstream"
PSRC=$(find "$USINA/src/engines/macro/models" -name "*.cc")
g++ $FLAGS -I"$USINA/src" "$HERE/models_check_main.cpp" $PSRC \
    "$USINA/src/port/dsputil/dsp/units.cc" "$USINA/src/port/dsputil/utils/random.cc" -o "$WORK/port"

mkdir -p "$WORK/u" "$WORK/p"
"$WORK/upstream" "$WORK/u" > "$WORK/u.txt"
"$WORK/port" "$WORK/p" > "$WORK/p.txt"

fail=0
while read -r n hash name; do
    if cmp -s "$WORK/u/$n.raw" "$WORK/p/$n.raw"; then
        printf '  ok    %-48s %s\n' "$name" "$hash"
    else
        printf '  DIFF  %-48s\n' "$name"
        fail=1
    fi
done < "$WORK/u.txt"
if [ "$fail" = 0 ]; then
    cp "$WORK/u.txt" "$USINA/tests/data/models_upstream.txt"
    echo "ALL BIT-EXACT ($(wc -l < "$WORK/u.txt") scenarios); hashes in tests/data/models_upstream.txt"
else
    echo "the port differs from upstream"
    exit 1
fi
