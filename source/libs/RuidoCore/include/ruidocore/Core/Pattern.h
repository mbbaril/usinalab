#pragma once

#include "Event.h"
#include "../Rhythm/LogicCombine.h"

#include <string>
#include <vector>

namespace ruidocore {

// Fixed per-part step storage, independent of playback length.
constexpr int kStepsPerPart = 64;

// Sentinel for inheriting pattern swing; outside the valid musical range.
constexpr float kSwingInherit = -1000.0f;

// Playback direction for a Part's step cursor.
enum class Direction { Forward, Reverse, PingPong, Random };

// Each part may have at most one active live generator.
enum class LiveGeneratorType { None, LogicCombine, Euclidean };

// One independently-sequenced lane - a drum voice, a synth voice, a
// modulation-only lane targeting a single parameter, whatever the host
// application wants it to mean. Independent length/direction/swing/clock-
// rate per Part is what makes true per-lane polymeter possible.
struct Part {
    // Host-defined output routing identifier.
    int outputId = 1;
    // Host-defined parameter destination for non-note lanes.
    int targetParamId = -1;
    // The note a step plays when it has none of its own (see
    // StepEvent::explicitNote). A STRUCTURAL fact about the Part, like
    // outputId: hosts derive it from the Part's position when they build
    // or load a pattern, rather than storing it - a stored copy is how a
    // copied Part ends up with the wrong one.
    int baseNote = 60;
    int length = 16;                   // step count, independent per part
    Direction direction = Direction::Forward;
    // kSwingInherit = inherit Pattern::swingPercent (the default).
    // Anything else is an explicit per-part override - see kSwingInherit
    // for why the sentinel is not -1 any more.
    float swingPercent = kSwingInherit;
    // Per-part clock rate relative to the pattern's global tick - both
    // default to 1 (no change). clockMultiplier > clockDivider = faster
    // than the pattern (e.g. 2/1 = double-time); clockDivider >
    // clockMultiplier = slower (e.g. 1/2 = half-time). See Sequencer::
    // tick()'s per-part pulse accumulator.
    int clockMultiplier = 1;
    int clockDivider = 1;
    // User-given display name - empty means "no custom name yet", a host
    // UI can fall back to a generated label in that case.
    std::string name;
    // Host-defined icon/category id (0 = none) - deliberately a plain int,
    // not an enum, so this header stays free of any UI-layer dependency.
    int iconKind = 0;
    // Host-defined accent behavior identifier.
    int accentStyle = 0;
    // Hosts may size steps to playback length or full step capacity.
    std::vector<StepEvent> steps;

    // Live-generator configuration.
    LiveGeneratorType liveGenerator = LiveGeneratorType::None;
    std::vector<int> logicSources;
    LogicOp logicOp = LogicOp::And;
    // While LIVE, step logicOp through all six operators on a slow timer.
    bool logicCycleOp = false;

    // What a Euclidean fill writes: the steps themselves, or a second
    // layer (accent, ratchet, pitch) onto the steps already there.
    enum class EuclidTarget { Steps, Accent, Ratchet, Pitch };
    EuclidTarget euclidTarget = EuclidTarget::Steps;

    // Logic combination may merge step attributes as well as presence.
    bool logicCombineRatchet = false;     // a surviving step is as busy as the busiest source
    bool logicCombineAccent = true;       // ...and as strong as the strongest source
    bool logicCombineFromAccents = false; // read the sources' accents instead of their hits

    // How much a strong hit borrows from the one after it, 0..1.
    float accentSteal = 0.0f;

    // Kept only so older saved files still parse - no current reader
    // interprets the enumerators.
    enum class RatchetShape { Flat, Decay, Ramp, AccentFirst };
    float ratchetGateMs = 20.0f;

    int euclidPulses = 4;
    int euclidRotation = 0;
    bool euclidInvert = false;
    // While LIVE, let Rotation wander on its own, bounded around the anchor.
    bool euclidDrift = false;
    int euclidDriftDirection = 1;
    int euclidDriftAnchor = 0;
};

// A full pattern across every lane. Part count is NOT fixed at compile
// time (unlike the per-product forks this was extracted from, which each
// hardcoded a different std::array<Part,N>) - `parts` is a plain vector,
// sized however the host application needs.
struct Pattern {
    double bpm = 120.0;
    int resolutionStepsPerBeat = 4;    // 4 = 1/16 notes
    float swingPercent = 0.0f;         // global swing, 0-100
    std::vector<Part> parts;
};

// The note a step actually plays. The one sanctioned way to read a
// step's note.
// STORAGE IS ALWAYS kStepsPerPart; `length` only decides what PLAYS.
//
// Retain storage beyond playback length. Reducing length must not erase steps
// or invalidate UI access to later pages.
//
// So the vector only ever grows. Call this anywhere a Part arrives from
// outside - a fresh pattern, a loaded file, a recalled slot, a length
// change - and the two failures above become unreachable rather than
// merely unobserved.
inline void ensureStepStorage(Part& part) {
    if (static_cast<int>(part.steps.size()) < kStepsPerPart)
        part.steps.resize(static_cast<size_t>(kStepsPerPart));
}

inline int resolvedNote(const StepEvent& ev, const Part& part) {
    return ev.explicitNote >= 0 ? ev.explicitNote : part.baseNote;
}

} //
