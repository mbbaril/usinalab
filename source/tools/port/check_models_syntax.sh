#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compile each adapted macro-oscillator source for syntax and report initial errors.
# Usage: bash tools/port/check_models_syntax.sh.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
USINA="$(cd "$HERE/../.." && pwd)"
cd "$USINA"
n=0
bad=0
while IFS= read -r f; do
    n=$((n + 1))
    if ! out=$(g++ -std=c++17 -fsyntax-only -I src "$f" 2>&1); then
        bad=$((bad + 1))
        echo "== $f"
        echo "$out" | grep -m 4 "error"
    fi
done < <(find src/engines/macro/models -name "*.cc" ! -name "resources_fm_banks.cc" | sort)
echo "$n sources, $bad with errors"
