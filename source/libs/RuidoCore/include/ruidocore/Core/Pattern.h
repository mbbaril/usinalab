#pragma once

#include "Event.h"
#include "../Rhythm/LogicCombine.h"

#include <string>
#include <vector>

namespace ruidocore {

// Real step storage per Part (brought in from the Volca family
// 2026-09-16). Those products always size `Part::steps` to this and treat
// `Part::length` purely as a playback range, because a paging UI can
// reach steps past the current length; a vector sized to `length` was a
// real out-of-range crash there. Hosts that size steps to `length` - as
// this library's own Generator does - are unaffected by the constant.
constexpr int kStepsPerPart = 64;

// "This Part has no swing override of its own." Deliberately far outside
// any musical range (brought in 2026-09-16). This library used -1.0f
// until then, the exact sentinel the Volca family had to abandon: -1 is
// an ordinary negative swing amount, so a sentinel inside the value
// range makes "swing back by 1%" unrepresentable and silently means
// "inherit" instead. A host that tests the SIGN rather than this value
// (Percutere does) keeps treating both -1 and this as inherit, so old
// files and the new default read identically there.
constexpr float kSwingInherit = -1000.0f;

// Playback direction for a Part's step cursor.
enum class Direction { Forward, Reverse, PingPong, Random };

// A Part can have at most ONE live-recomputed generator active at a time
// (2026-09-03, ported from the Volca/DR-670/Eurorack family - see the
// family's own Core/Pattern.h for the full reasoning: a Part being a live
// Logic Combine target AND a live Euclidean source simultaneously isn't
// coherent).
enum class LiveGeneratorType { None, LogicCombine, Euclidean };

// One independently-sequenced lane - a drum voice, a synth voice, a
// modulation-only lane targeting a single parameter, whatever the host
// application wants it to mean. Independent length/direction/swing/clock-
// rate per Part is what makes true per-lane polymeter possible.
struct Part {
    // Host-defined output routing id - was a hardcoded MIDI channel in the
    // Volca-family projects this was extracted from; kept as a plain int
    // here so this struct carries no MIDI-specific assumption. A MIDI-
    // backed host can treat it as a channel number; a synthesis engine can
    // treat it as a voice index.
    int outputId = 1;
    // Which parameter this Part DRIVES instead of triggering a voice
    // (brought in from the Volca Kick and Volca Bass 2026-09-16, where
    // it was `targetCC`). -1 = an ordinary trigger lane. >= 0 = a
    // parameter lane: a fired step writes <this parameter> = the step's
    // value rather than playing a note. What the id means is host
    // business - a CC number for a MIDI host.
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
    // What an accented step does on THIS voice - host-defined, a plain int
    // like iconKind (2026-09-16). It was briefly typed with the Volca
    // Drum's own enum, which put one product's accent vocabulary inside
    // the shared library; the Volca Kick's styles (Harder, Drier,
    // Dirtier) are a different list for a different instrument, so each
    // host keeps its own enumeration and stores its index here. 0 is
    // every current host's "Louder".
    int accentStyle = 0;
    // Host-defined size: either `length` (this library's Generator) or
    // always kStepsPerPart (the Volca family) - see kStepsPerPart.
    std::vector<StepEvent> steps;

    // Live generators (2026-09-03, ported from the Volca/DR-670/Eurorack
    // family - see that family's own Core/Pattern.h for the full
    // field-by-field comment).
    LiveGeneratorType liveGenerator = LiveGeneratorType::None;
    std::vector<int> logicSources;
    LogicOp logicOp = LogicOp::And;
    // While LIVE, step logicOp through all six operators on a slow timer.
    bool logicCycleOp = false;

    // What a Euclidean fill writes: the steps themselves, or a second
    // layer (accent, ratchet, pitch) onto the steps already there.
    enum class EuclidTarget { Steps, Accent, Ratchet, Pitch };
    EuclidTarget euclidTarget = EuclidTarget::Steps;

    // Logic Combine folds more than presence (brought in 2026-09-16; see
    // the Volca Drum's Core/Pattern.h for each one's full reasoning):
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
// Sizing the steps vector by `length` looks tidy and costs two things,
// both found the hard way on 2026-09-17: a UI that pages across the full
// storage indexes past the end of a short lane (the user photographed the
// assertion), and - worse, because nothing reports it - LOWERING a lane's
// length DESTROYS every step past the new one. Write 32 steps, pull the
// length to 8 to hear a shorter loop, push it back: the phrase is gone.
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

} // namespace ruidocore
