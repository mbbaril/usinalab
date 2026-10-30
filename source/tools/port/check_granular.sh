#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compare granular DSP sample-for-sample across all mode/quality scenarios.
# Build reference and adapted variants and record reference hashes.
# Usage: bash tools/port/check_granular.sh UPSTREAM_DIR (the folder holding the upstream clones).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
USINA="$(cd "$HERE/../.." && pwd)"
UP="${1:?usage: bash tools/port/check_granular.sh UPSTREAM_DIR}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FLAGS="-std=c++17 -O2 -w"

echo "upstream: parasites $(git -C "$UP/parasites" rev-parse --short HEAD), stmlib $(git -C "$UP/stmlib" rev-parse --short HEAD)"
# Normalize two platform-dependent reference expressions in temporary copies:
# 32-bit shifts by 32 return zero; powf is evaluated in double and rounded once.
# Use the same expressions as the adapted DSP.
mkdir -p "$WORK/ref/clouds/dsp"
refpatch() {  # FILE (under parasites/clouds/dsp) OLD NEW: the copy at $WORK/ref/clouds/dsp/FILE
    [ "$(grep -cF "$2" "$UP/parasites/clouds/dsp/$1")" = 1 ] || { echo "upstream's $1 changed"; exit 1; }
    awk -v old="$2" -v new="$3" '{ sub(/\r$/, ""); if ($0 == old) print new; else print }' \
        "$UP/parasites/clouds/dsp/$1" > "$WORK/ref/clouds/dsp/$1"
}
refpatch correlator.cc '    destination_bits |= destination[i + 1] >> (32 - offset_bits);' \
    '    destination_bits |= offset_bits ? destination[i + 1] >> (32 - offset_bits) : 0u;'
refpatch resonestor.h '      comb_feedback_[p][voice_] = powf(feedback_[voice_], comb_period_[p][voice_] / 32000.0f);' \
    '      comb_feedback_[p][voice_] = static_cast<float>(std::pow(static_cast<double>(feedback_[voice_]), static_cast<double>(comb_period_[p][voice_] / 32000.0f)));'
UPSRC="$(find "$UP/parasites/clouds/dsp" -name "*.cc" ! -name correlator.cc) $WORK/ref/clouds/dsp/correlator.cc"
g++ $FLAGS -DGRANULAR_UPSTREAM -DTEST -I"$WORK/ref" -I"$UP" -I"$UP/parasites" "$HERE/granular_check_main.cpp" $UPSRC \
    "$UP/parasites/clouds/resources.cc" "$UP/stmlib/dsp/units.cc" "$UP/stmlib/dsp/atan.cc" \
    "$UP/stmlib/utils/random.cc" -o "$WORK/upstream"
PSRC=$(find "$USINA/src/fx/granular" -name "*.cc")
g++ $FLAGS -I"$USINA/src" "$HERE/granular_check_main.cpp" $PSRC \
    "$USINA/src/port/dsputil/dsp/units.cc" "$USINA/src/port/dsputil/dsp/atan.cc" \
    "$USINA/src/port/dsputil/utils/random.cc" -o "$WORK/port"

mkdir -p "$WORK/u" "$WORK/p"
"$WORK/upstream" "$WORK/u" > "$WORK/u.txt"
"$WORK/port" "$WORK/p" > "$WORK/p.txt"

fail=0
while read -r n hash name; do
    if cmp -s "$WORK/u/$n.raw" "$WORK/p/$n.raw"; then
        printf '  ok    %-40s %s\n' "$name" "$hash"
    else
        printf '  DIFF  %-40s\n' "$name"
        fail=1
    fi
done < "$WORK/u.txt"
if [ "$fail" = 0 ]; then
    cp "$WORK/u.txt" "$USINA/tests/data/granular_upstream.txt"
    echo "ALL BIT-EXACT ($(wc -l < "$WORK/u.txt") scenarios); hashes in tests/data/granular_upstream.txt"
else
    echo "the port differs from upstream"
    exit 1
fi
