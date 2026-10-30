#pragma once

#include "../Core/Pattern.h"
#include "../Music/Scale.h"

#include <cstdint>
#include <vector>

namespace ruidocore {

// What MUTATE is allowed to touch. true = locked = preserved exactly.
// One flag per real per-step property, rather than the five broad
// categories this started with: `timing` used to bundle microtiming AND
// ratchet, so neither could be re-rolled without disturbing the other,
// and probability and condition were not reachable at all.
//
// `part` is sized to match the Pattern being mutated; an index past its
// end is treated as unlocked.
struct MutationLocks {
    std::vector<bool> part;       // per-part full lock (overrides every category for that part)
    bool rhythm = false;          // `enabled` - the step on/off pattern
    bool accent = false;          // `accentAmount` - what most instruments hear as dynamics
    bool microtiming = false;     // `microtimingMs`
    bool ratchet = false;         // `ratchet`
    bool pitch = false;           // the host's pitch field (see MutationProfile::pitchTarget)
    // These three default to LOCKED. A category that did not exist must
    // not start mutating patterns just because it was added - anything
    // that wants them says so.
    bool gate = true;             // `gateLength` - real only where the host has gates
    bool probability = true;      // `probability`
    bool condition = true;        // `condition` (Trig Condition)
};

// WHAT KIND of variation, as opposed to how much of it. "Vary my groove"
// and "wreck this into glitch" want contrary distributions, and serving
// both from one range is why neither worked: ratchets and wide pitch
// draws are good for glitch and musically ruinous for a groove. Character
// says what shape the draw has, `amount` says how far it goes.
enum class MutationCharacter {
    // Ratchets stay rare, timing stays tight, pitch stays within a couple
    // of semitones, and the rhythm draw follows the bar instead of a flat
    // coin. Someone asking for a variation gets a variation.
    Groove,
    // Everything wide open - what every product did before this existed.
    Glitch,
};

struct MutationParams {
    float amount = 0.3f;          // 0 = identical output, 1 = maximum change
    MutationCharacter character = MutationCharacter::Groove;
    // How likely an unlocked step is to be ON when rhythm is mutated.
    // Only consulted in Groove; 0.5 would BE the flat coin, so the default
    // sits below it - a musical pattern is mostly space.
    float density = 0.35f;
    // The window the pitch line may occupy: semitone offsets or absolute
    // notes, depending on the host's pitchTarget. A window is what keeps
    // repeated randomizes from being an unbounded random walk.
    int pitchLow = -12;
    int pitchHigh = 12;
    Scale scale;                  // the line steps by DEGREE, not by semitone
    MutationLocks locks;
    std::uint32_t seed = 1;       // deterministic: same source+params -> same result
};

// What a host hears of each category, and how its own fields follow.
// A category switched off draws nothing from the random stream.
struct MutationProfile {
    bool accent = true;
    // Jitter the note's velocity alongside the accent. True where a host
    // records its MIDI output, even if the instrument ignores velocity.
    bool velocityJitter = true;
    // In Groove, pull the accent toward what the bar wants instead of
    // letting each step wander - the relationship between strong and weak
    // beats IS the groove.
    bool accentPull = true;
    // Which field the pitch category moves, if any.
    enum class PitchTarget {
        None,             // the instrument has no pitch (gates, fixed pads)
        SemitoneOffset,   // StepEvent::pitchSemitoneOffset, relative to the voice's own pitch
        ExplicitNote,     // StepEvent::explicitNote, an absolute note
    };
    PitchTarget pitchTarget = PitchTarget::SemitoneOffset;
    bool gate = false;            // gateLength is real on this instrument
    // The span the host's own microtiming control offers. Randomize must
    // not reach values the performer cannot dial back by hand.
    int microtimingSpanMs = 20;
    // After accentAmount changed - e.g. rebuild the accent's own locks.
    void (*afterAccent)(StepEvent& ev) = nullptr;
    // After a Part's steps are done - e.g. accent stealing, or baking.
    void (*afterPart)(int partIndex, Part& part) = nullptr;
    // Which parts have a pitch at all (a bass part, but not the pads,
    // where a note selects the pad). Null = all of them.
    bool (*partHasPitch)(int partIndex) = nullptr;
};

// Produces a variation of an existing Pattern, never a fresh generation
// (see Generator for that). "amount" scales how much each unlocked
// category is perturbed; at amount=0 the output equals the input exactly.
// Only steps the sequencer can reach are touched - storage past a Part's
// length is left alone.
class Mutation {
public:
    static Pattern mutate(const Pattern& source, const MutationParams& params,
                           const MutationProfile& profile = {});
};

} //
