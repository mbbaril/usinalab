#pragma once

#include "../Mutation/Mutation.h"

#include <cstdint>

namespace ruidocore {

struct EvolutionParams {
    float amountPerCycle = 0.1f;  // small, continuous Mutation each cycle
    MutationLocks locks;
    std::uint32_t seed = 1;
};

// Continuous mutation over time: each call to next() applies one small
// Mutation step on top of the current pattern, unless frozen (preserves
// whatever the pattern evolved into). Each cycle uses a different, but
// deterministic, seed derived from the base seed + cycle count, so a given
// seed always evolves the same way.
//
// This class only owns the *data* transformation - it has no notion of
// MIDI, timing, or a live audio thread. Driving it live (swap the running
// scheduler's pattern in place, without stopping playback) is a
// performance-layer concern for the host application.
class Evolution {
public:
    Evolution(Pattern initial, EvolutionParams params);

    // Advances one cycle (mutating, unless frozen) and returns the current
    // pattern. Safe to call repeatedly; frozen calls just return the same
    // pattern without touching the RNG/cycle count.
    const Pattern& next();

    void setFrozen(bool isFrozen) { frozen = isFrozen; }
    bool isFrozen() const { return frozen; }

    const Pattern& current() const { return pattern; }

private:
    Pattern pattern;
    EvolutionParams params;
    std::uint32_t cycleCount = 0;
    bool frozen = false;
};

} // namespace ruidocore
