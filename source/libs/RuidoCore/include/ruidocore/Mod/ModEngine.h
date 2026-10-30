#pragma once

#include "../Core/Mod.h"

#include <array>
#include <cstdint>
#include <random>

namespace ruidocore {

// Per-generator runtime state. Deliberately NOT part of ModGenerator
// (Core/Mod.h), which is pure save/load data. This is live-performance
// state (phase, RNG, shift register) that resets fresh on every load.
struct ModRuntime {
    double phaseMs = 0.0;
    bool oneShotIdle = true; // true before the first trigger, or once a One-Shot has finished and is holding at rest
    float heldValue = 0.0f;  // current flat target for the stepped shapes (S&H/Random/SlewedRandom/Turing)
    float slewFrom = 0.0f;   // SlewedRandom's glide-from value
    double stepElapsedMs = 0.0;      // time since the last resample, for the stepped shapes
    double nextStepIntervalMs = 0.0; // Random shape's jittered interval (recomputed each step)
    std::uint8_t turingRegister = 0;
    std::mt19937 rng{std::random_device{}()};
    int lastSeenTriggerStep = -1; // edge-detects a trigger source changing - host-defined meaning
    float lagState = 0.0f; // One-pole lag state and lane-rhythm interval measurements.
    double msSinceLastTrigger = 0.0;
    double measuredIntervalMs = 0.0;

    // Ring of prior random targets for repeatable short histories.
    static constexpr int kRandomHistoryLen = 16;
    std::array<float, kRandomHistoryLen> randomHistory{};
    int randomStepIndex = 0;
};

// Advances one generator by deltaMs and returns its current output as a
// raw 0..1 value (0 = trough, 1 = peak) - per-destination range is applied
// by the caller, not here.
//
// `triggerEdge` should be true on exactly the tick where a NEW trigger
// event was detected - only consumed by One-Shot mode (restarts the
// envelope) and by the stepped shapes when in One-Shot (resamples once
// per hit instead of once per cycle).
//
// Mirror returns externalValue directly. Other shapes ignore this argument.
float advanceModGenerator(const ModGenerator& gen, ModRuntime& rt, double deltaMs, bool triggerEdge,
                           float externalValue = 0.0f);

// Deterministic "what this shape looks like" preview at a normalized
// phase (0..1 = one full Rise+Fall cycle), independent of any ModRuntime -
// no RNG, no trigger-edge state. For the stepped/random shapes (S&H/
// Random/SlewedRandom/Turing) there is no true deterministic curve - this
// returns a fixed, phase-hashed staircase that visually reads as
// "stepped/random" without claiming to predict actual runtime values.
// For Mirror, there's no fixed curve to preview at all (see
// advanceModGenerator's own externalValue comment) - this just returns
// externalValue as a flat line, an honest "follows something live"
// placeholder rather than a fake shape.
float previewModShape(const ModGenerator& gen, float phase01, double bpm, float externalValue = 0.0f);

// Resolves the ACTUAL riseMs/fallMs to advance/preview with this tick -
// either the generator's own free-running values, or (when gen.synced
// and cycle == Loop) values derived from the current tempo, gen.rate, and
// gen.shapePercent.
void computeEffectiveRiseFall(const ModGenerator& gen, double bpm, float& outRiseMs, float& outFallMs);

// Output shaping - applied AFTER a shape's raw01 is computed, so they
// work identically regardless of Shape/Sync.
float applyModLag(float raw01, float& lagState, float lagAmount, double deltaMs);
float applyModWavefold(float raw01, float foldAmount);

// No bundled Timer/clock here on purpose - this core library doesn't
// depend on any particular framework's timer facility. A host application
// drives the MOD engine by calling advanceModGenerator() from whatever
// periodic mechanism it already has (a 60Hz UI timer, an audio-thread
// sample counter, etc.) - the original per-product forks this was
// extracted from used a small always-running host framework::Timer wrapper for
// this; that's a one-line thing for any host to reimplement in its own
// framework, so it isn't duplicated here.

} //
