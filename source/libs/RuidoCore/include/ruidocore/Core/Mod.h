#pragma once

#include <array>
#include <vector>

namespace ruidocore {

// MOD data model - a unified rise/fall function generator: one engine, not
// two separate "LFO" and "Envelope" source types. Every generator is
// defined by a Rise time + a Fall time and a Shape family; a Cycle switch
// decides whether that shape repeats forever (Loop, reads as an LFO) or
// fires once per trigger event and holds at rest until the next one
// (One-Shot, reads as an envelope) - same underlying math either way.
enum class ModShape {
    Sine,
    Saw,
    Triangle,
    Square,
    SampleHold,   // new random value every cycle, held flat (classic S&H)
    Random,       // like S&H but the interval between new values is itself
                   // randomized, so it doesn't feel clocked/mechanical
    SlewedRandom, // S&H's target, but glided into smoothly instead of
                   // stepped (classic "smooth random"/noise-lag)
    Turing,       // Feedback shift register: repeat or reroll bits according to turingChance.
    Custom,       // user-drawn breakpoint curve, see ModGenerator::customCurve
    SegmentChain, // arbitrary N-stage envelope, see ModGenerator::segments'
                   // own comment - a genuine escape hatch beyond the fixed
                   // Delay-Attack-Hold-Decay-Sustain-Release shape every
                   // other shape's own timing fields describe
    Mirror        // Read the host-supplied external value instead of generating a curve.
    // mirrorPart identifies a host-defined source.
};

enum class ModCycleMode { Loop, OneShot };

// Per-generator trigger selection; no implicit shared anchor lane.
enum class ModTriggerSourceType {
    None,        // free-running - no external trigger ever reaches this generator
    Global,      // host-defined "musical boundary" tick (e.g. once per bar)
    SpecificLane // a single chosen lane/part's own trigger hits - see ModGenerator::triggerSourceOutputId
};

// Loop generators may hard-reset phase or softly synchronize toward the downbeat.
// One-shot generators always restart on trigger.
enum class ModSyncBehavior { Sync, Reset };

// Select exactly one rate source. Inactive mode settings remain stored.
// Free uses configured timings; Synced uses tempo divisions; ModulatedByGenerator
// uses the selected rate source and depth. LockedToLane uses the selected lane
// and measuredIntervalMs. Default Free preserves unsynchronized timing.
enum class ModRateSource {
    Free,                // free-running - riseMs/fallMs/DAHDSR taken literally, nothing else adjusts cycle length
    Synced,              // tempo/rate-division locked - see `synced`/`rate`/`shapePercent` below
    ModulatedByGenerator, // another generator's output speeds/slows this one - see `rateModSource`/`rateModDepth` below
    LockedToLane         // cycle length continuously matches the REAL measured gap between a chosen lane's own hits (triggerSourceType must be SpecificLane)
};

// Only meaningful for the Triangle shape (and Custom's fallback ramp) -
// Sine/Square/Saw have their own fixed curve math and ignore this.
enum class ModCurveType { Linear, Exponential, Logarithmic };

// Maximum simultaneous modulation generators.
constexpr int kModGeneratorCount = 32;
// Independent destination slots per generator, each storing outputId and paramId.
constexpr int kModDestinationSlots = 12;

struct ModDestination {
    bool active = false;
    // Host-defined output/voice routing id this destination targets (see
    // Core/Pattern.h's Part::outputId for the same convention).
    int outputId = 1;
    // Host-defined opaque parameter id (see Core/Event.h's ParameterLock).
    // -1 = unassigned.
    int paramId = -1;
    // Absolute 0-127 range this destination sweeps between. The
    // generator's own 0..1 waveform output maps directly onto [rangeLow,
    // rangeHigh] - no separate polarity flag needed, since the range
    // itself already expresses "swings around a centre" vs. "only adds
    // upward" vs. a tiny nudge, etc. Meant to be applied as a live offset
    // from whatever the destination's CURRENT base value is, so it stays
    // additive-composable with other live modulation sources (recorded
    // automation, a performance-pad macro, ...) through a shared
    // base+offset composition mechanism - see Param/LiveParameterState.h.
    int rangeLow = 64;
    int rangeHigh = 64;
};

// A single breakpoint on a hand-drawn Custom curve: phase in [0,1]
// (position within one cycle), value in [0,1] (0 = trough, 1 = peak).
// Always kept sorted by phase; endpoints at phase 0.0 and 1.0 always
// exist and can't be deleted, only moved vertically.
struct ModBreakpoint {
    float phase = 0.0f;
    float value = 0.0f;
};

// Typed segment in an envelope chain. Ramp approaches a target; Hold retains it.
// Ramp/Hold segments can represent DAHDSR without a separate envelope machine.
enum class ModSegmentType {
    Ramp, // interpolates from wherever the PREVIOUS segment ended toward targetLevel01, over durationMs, using `curve`
    Hold  // stays flat at targetLevel01 for durationMs (curve is unused)
};

struct ModSegment {
    ModSegmentType type = ModSegmentType::Ramp;
    float durationMs = 100.0f;
    ModCurveType curve = ModCurveType::Linear;
    float targetLevel01 = 1.0f; // Ramp: level reached by the END of this segment; Hold: the flat level held throughout
};

// Tempo-sync rate divisions. Only meaningful when ModGenerator::synced is
// true AND cycle == Loop (One-Shot already syncs implicitly to trigger
// hits). Beat = a quarter note.
enum class ModRateDivision {
    ThirtySecond,
    Sixteenth,
    Eighth,
    Quarter,
    Half,
    Whole,
    TwoBars,
    FourBars,
    EighthTriplet,
    QuarterTriplet,
    HalfTriplet,
    EighthDotted,
    QuarterDotted,
    HalfDotted
};

// Cycle length in beats (quarter notes) for one rate division - the pure
// conversion math, shared by both the live engine
// (ModEngine::computeEffectiveRiseFall) and any UI, so they never drift
// apart.
inline float rateDivisionBeats(ModRateDivision d) {
    switch (d) {
        case ModRateDivision::ThirtySecond: return 0.125f;
        case ModRateDivision::Sixteenth: return 0.25f;
        case ModRateDivision::Eighth: return 0.5f;
        case ModRateDivision::Quarter: return 1.0f;
        case ModRateDivision::Half: return 2.0f;
        case ModRateDivision::Whole: return 4.0f;
        case ModRateDivision::TwoBars: return 8.0f;
        case ModRateDivision::FourBars: return 16.0f;
        case ModRateDivision::EighthTriplet: return 0.5f * (2.0f / 3.0f);
        case ModRateDivision::QuarterTriplet: return 1.0f * (2.0f / 3.0f);
        case ModRateDivision::HalfTriplet: return 2.0f * (2.0f / 3.0f);
        case ModRateDivision::EighthDotted: return 0.5f * 1.5f;
        case ModRateDivision::QuarterDotted: return 1.0f * 1.5f;
        case ModRateDivision::HalfDotted: return 2.0f * 1.5f;
    }
    return 1.0f;
}

inline const char* rateDivisionLabel(ModRateDivision d) {
    switch (d) {
        case ModRateDivision::ThirtySecond: return "1/32";
        case ModRateDivision::Sixteenth: return "1/16";
        case ModRateDivision::Eighth: return "1/8";
        case ModRateDivision::Quarter: return "1/4";
        case ModRateDivision::Half: return "1/2";
        case ModRateDivision::Whole: return "1/1";
        case ModRateDivision::TwoBars: return "2 bar";
        case ModRateDivision::FourBars: return "4 bar";
        case ModRateDivision::EighthTriplet: return "1/8t";
        case ModRateDivision::QuarterTriplet: return "1/4t";
        case ModRateDivision::HalfTriplet: return "1/2t";
        case ModRateDivision::EighthDotted: return "1/8.";
        case ModRateDivision::QuarterDotted: return "1/4.";
        case ModRateDivision::HalfDotted: return "1/2.";
    }
    return "?";
}
constexpr std::array<ModRateDivision, 14> kRateDivisionOrder{
    ModRateDivision::ThirtySecond,   ModRateDivision::Sixteenth,     ModRateDivision::Eighth,
    ModRateDivision::Quarter,        ModRateDivision::Half,          ModRateDivision::Whole,
    ModRateDivision::TwoBars,        ModRateDivision::FourBars,      ModRateDivision::EighthTriplet,
    ModRateDivision::QuarterTriplet, ModRateDivision::HalfTriplet,   ModRateDivision::EighthDotted,
    ModRateDivision::QuarterDotted,  ModRateDivision::HalfDotted,
};

struct ModGenerator {
    bool enabled = false;
    ModShape shape = ModShape::Sine;
    ModCycleMode cycle = ModCycleMode::Loop;
    float riseMs = 20.0f;  // Attack
    float fallMs = 300.0f; // DAHDSR stages share the phase timeline: Delay -> Attack -> Hold -> Decay
    // -> Sustain -> Release. Zero delay/hold/decay/sustain time and unit sustain level
    // reduce to the default attack/release envelope.
    float delayMs = 0.0f;
    float holdMs = 0.0f;
    float decayMs = 0.0f;
    float sustainLevel01 = 1.0f;
    float sustainMs = 0.0f;
    ModCurveType riseCurve = ModCurveType::Linear;
    ModCurveType fallCurve = ModCurveType::Exponential; // exponential fall reads as a natural percussive decay by default
    float turingChance = 0.5f; // Register randomization probability: zero freezes, one rerolls every step.
    // Other random shapes share continuous resampling, glide, distribution and history axes;
    // shape presets initialize those axes but do not restrict subsequent edits.
    float randomSteps = 1.0f;  // 0 = fully smooth/glided (like the old SlewedRandom), 1 = fully stepped (like the old SampleHold) - continuous between
    float randomSpread = 1.0f; // 0 = flat/no movement, 1 = full 0..1 range - how WIDE the random draw is
    float randomBias = 0.5f;   // 0..1, skews the distribution up or down - 0.5 = neutral/uniform (matches every pre-existing generator's own default exactly)
    float randomDejaVu = 0.0f; // 0 = always a fresh random value, 1 = always replays the exact same sequence as last cycle - genuinely new, nothing in the old system could express this
    std::vector<ModBreakpoint> customCurve{{0.0f, 0.0f}, {1.0f, 0.0f}}; // Custom-curve points and segment-chain configuration.
    // The default chain matches the default attack/release envelope. The shared cycle
    // setting selects repetition or one-shot playback; segments do not own loop modes.
    std::vector<ModSegment> segments{{ModSegmentType::Ramp, 20.0f, ModCurveType::Linear, 1.0f},
                                      {ModSegmentType::Ramp, 300.0f, ModCurveType::Exponential, 0.0f}};
    std::array<ModDestination, kModDestinationSlots> destinations{};

    // Explicit rate-source selection; defaults to Free.
    ModRateSource rateSource = ModRateSource::Free;

    // In Synced loop mode, derive rise/fall from tempo, rate and shapePercent.
    // Keep stored free-running times unchanged so disabling sync restores them.
    // The UI keeps synced consistent with rateSource for computeEffectiveRiseFall().
    bool synced = false;
    ModRateDivision rate = ModRateDivision::Eighth;
    float shapePercent = 0.5f; // Pure-shape mode uses normalized phase directly. Envelope times still determine
    // cycle length but no longer distort the waveform. Disabled by default.
    bool pureShape = false;

    // Global trigger division, meaningful only for a global trigger source.
    // Default Whole is four beats.
    ModRateDivision globalTriggerRate = ModRateDivision::Whole;

    // Explicit phase warp. 0.5 = no warp; shifts WHERE the peak sits
    // across the whole cycle, applied uniformly to every continuous shape
    // (Sine/Triangle/Saw/Square/Custom) - a genuinely different axis from
    // Rise/Fall (which control curve STEEPNESS/timing, not overall peak
    // position).
    float warpAmount = 0.5f;

    // Output shaping - applied to the generator's raw01 output AFTER the
    // shape itself is evaluated, so they work identically regardless of
    // Shape/Sync. Lag = a one-pole slew (glide) toward each new value,
    // turning any shape smooth/laggy; Fold = classic wavefolder distortion
    // (amplifies then reflects back into range), turning any shape
    // spikier/more harmonically complex. Both 0 = no effect.
    float lagAmount = 0.0f;  // 0..1
    float foldAmount = 0.0f; // 0..1

    // Cross-modulation - "LFO modulating LFO". Only actually APPLIED
    // when rateSource == ModulatedByGenerator. rateModSource (-1 = none)
    // names ANOTHER generator whose most recent output speeds up/slows
    // down THIS generator's effective cycle time, scaled by rateModDepth
    // (0..1).
    int rateModSource = -1;
    float rateModDepth = 0.0f;

    // Trigger-source selection; None leaves the generator free-running.
    ModTriggerSourceType triggerSourceType = ModTriggerSourceType::None;
    int triggerSourceOutputId = -1; // meaningful only when triggerSourceType == SpecificLane
    ModSyncBehavior syncBehavior = ModSyncBehavior::Reset; // Trigger synchronization applies only to loop mode. Mirror uses a host-defined source index.
    int mirrorPart = -1;
    bool mirrorUseFilterEnvelope = false;
};

} //
