#pragma once

#include <string>
#include <vector>

namespace ruidocore {

// Trigger conditions evaluable from the part loop counter.
// PRE/PRE- and NEI/NEI- require additional cross-step history and are unsupported.
enum class TrigConditionType {
    Always,      // default - no extra gating beyond probability/enabled
    Ratio,       // fires on occurrence ratioK of every ratioN loops
    Fill,        // fires only while the performer's FILL control is held
    FillInverse, // fires only while FILL is NOT held (sometimes shown as "FILL-")
    FirstLoop,   // fires only the first time this part loops
};

struct TrigCondition {
    TrigConditionType type = TrigConditionType::Always;
    int ratioK = 1; // for Ratio: which occurrence, 1-based (e.g. 1 of "1:2")
    int ratioN = 1; // for Ratio: out of how many loops (e.g. 2 of "1:2")
};

// A single parameter change tied to a step ("Parameter Lock"). `paramId` is
// an opaque integer the host application defines the meaning of (a MIDI CC
// number, an FM operator ratio slot, a wavetable-position target -
// whatever). This struct carries no MIDI/CC-specific knowledge itself.
struct ParameterLock {
    int paramId = 0;
    int value = 0;      // 0-127, host-defined range convention
    std::string name;   // e.g. "pitch1", for logging/UI only
};

// Unset notes inherit Part::baseNote; see StepEvent::explicitNote.
inline constexpr int kNoteUnset = -1;

// A single step in a Part's sequence.
struct StepEvent {
    bool enabled = false;
    // The step's OWN note, or kNoteUnset to use its Part's baseNote.
    // Host-defined meaning - a MIDI note number, a pad selector, a pitch.
    // Read it through resolvedNote() (Core/Pattern.h), never directly,
    // unless the question really is "did someone set a note here".
    //
    // Represent note inheritance explicitly. A shared step cannot assume a
    // host-specific default pitch or pad mapping.
    //
    // Renamed from `note` at the same time, deliberately: the rename made
    // the compiler list every reader in every product, so each one was
    // converted on purpose instead of silently reading -1.
    int explicitNote = kNoteUnset;
    int velocity = 100;          // 1-127
    float probability = 1.0f;    // 0..1, evaluated at playback time only -
                                  // never mutates the stored pattern.
    int microtimingMs = 0;       // offset from the step's nominal time
    int ratchet = 1;             // number of repeats within the step
    float accentAmount = 0.0f;   // 0-1, boosts velocity at playback time,
                                  // kept independent of base velocity.
    int pitchSemitoneOffset = 0; // Relative pitch is quantized against the scale at edit time; zero disables pitch lock.
    // Per-step duration is in milliseconds and may be ignored by edge-triggered hosts.
    float gateMs = 20.0f;
    // Gate duration as a fraction of step time, 0..1; distinct from absolute gateMs.
    float gateLength = 0.95f;
    TrigCondition condition;     // Always = no extra gating (see above)
    std::vector<ParameterLock> locks;
};

} //
