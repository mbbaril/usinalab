#pragma once

#include <string>
#include <vector>

namespace ruidocore {

// Conditional-triggering trig conditions. Only the subset that can be
// evaluated from a single per-part loop counter is implemented - PRE/PRE-
// (did THIS step's condition pass last time it was reached) and NEI/NEI-
// (did the immediately preceding trig fire) both need cross-step history
// beyond a loop count, and are a deliberate, documented gap rather than a
// half-faked approximation.
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

// "This step has no note of its own - play the Part's base note"
// (2026-09-16). See StepEvent::explicitNote and Part::baseNote.
inline constexpr int kNoteUnset = -1;

// A single step in a Part's sequence.
struct StepEvent {
    bool enabled = false;
    // The step's OWN note, or kNoteUnset to use its Part's baseNote.
    // Host-defined meaning - a MIDI note number, a pad selector, a pitch.
    // Read it through resolvedNote() (Core/Pattern.h), never directly,
    // unless the question really is "did someone set a note here".
    //
    // Why unset and not a number (2026-09-16): this field used to default
    // to 60, and that default was STRUCTURAL in more than one product at
    // once, differently - 60 is the only note that triggers a Volca Drum
    // Part, 36 is where a Volca Kick sounds like a kick, and on the
    // DR-670 the note selects which pad fires. A shared struct cannot
    // carry three defaults. The Kick had simply kept its own 36; the
    // DR-670's bass tested `note == 0` for "unset" against a default of
    // 60, so a new bass step played C4 instead of the intended E2. Making
    // "unset" explicit, and the natural note a property of the Part,
    // means a freshly made step can never play the wrong note, in any
    // product, whichever path created it.
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
    int pitchSemitoneOffset = 0; // relative, quantized against a Scale at
                                  // edit time - see Music/Scale.h. 0 = no
                                  // pitch lock for this step at all.
    // Per-step note length in ms (brought in from the Volca family
    // 2026-09-16). A host whose sound source ignores note length - the
    // Volcas trigger on note-on only - may store it and never read it.
    float gateMs = 20.0f;
    // Gate length as a FRACTION of the step, 0..1 (brought in from the
    // Eurorack Trigger Machine 2026-09-16). A different concept from
    // gateMs, not a duplicate: a host driving real gate outputs wants the
    // gate to scale with tempo, so it stores a proportion rather than a
    // duration. Hosts that trigger on an edge only can ignore it.
    float gateLength = 0.95f;
    TrigCondition condition;     // Always = no extra gating (see above)
    std::vector<ParameterLock> locks;
};

} // namespace ruidocore
