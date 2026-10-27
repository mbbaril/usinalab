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
    Turing,       // Turing-Machine-style shift register: each new step
                   // either repeats the running pattern or re-rolls a bit,
                   // weighted by turingChance
    Custom,       // user-drawn breakpoint curve, see ModGenerator::customCurve
    SegmentChain, // arbitrary N-stage envelope, see ModGenerator::segments'
                   // own comment - a genuine escape hatch beyond the fixed
                   // Delay-Attack-Hold-Decay-Sustain-Release shape every
                   // other shape's own timing fields describe
    Mirror        // reads a HOST-supplied external value instead of
                   // generating its own curve at all (2026-08-28,
                   // "criar a opcao de envelopes" - lets a generator
                   // follow some OTHER live value - e.g. a host Part's
                   // own real hardwired envelope - instead of shaping
                   // one itself). See advanceModGenerator's own
                   // externalValue param and ModGenerator::mirrorPart
                   // below - RuidoCore itself has no idea what's being
                   // mirrored, that meaning is entirely host-defined.
};

enum class ModCycleMode { Loop, OneShot };

// Trigger source for restarting/syncing a generator (2026-08-28,
// "sera otimo usar nas volcas e aqui um modelo que permita triggar
// envelopes ou do global ou duma lane especifica" - shared here at the
// RuidoCore level, not Percutere-only, so the rest of the Volca family
// benefits too). Replaces each host's own previous ad hoc single-anchor
// convention (e.g. Volca Drum's `tickModGenerators()` hardcoding
// "Part 1's fired-step index" as the ONE trigger source for every
// generator) with a real per-generator choice.
enum class ModTriggerSourceType {
    None,        // free-running - no external trigger ever reaches this generator
    Global,      // host-defined "musical boundary" tick (e.g. once per bar)
    SpecificLane // a single chosen lane/part's own trigger hits - see ModGenerator::triggerSourceOutputId
};

// Only meaningful when cycle == Loop AND triggerSourceType != None (a
// One-Shot generator only ever makes sense as "Trigger" - starting a
// fresh envelope from the top - so it doesn't get a choice here).
// Reset = hard-restart phase to 0 on every trigger (classic retrigger).
// Sync = pull phase PARTWAY toward the downbeat on every trigger instead
// of snapping - stays musically locked to the trigger source without an
// audible jump/glitch in the generator's own continuous output. See
// advanceModGenerator()'s own implementation for the exact pull math.
enum class ModSyncBehavior { Sync, Reset };

// Rate Source (2026-09-01, "reduzir a complexidade do mod sem perder
// poder" -> studied Mutable Instruments Tides 2's own real firmware
// source, `tides2/poly_slope_generator.h` - a slope generator there is
// defined by ORTHOGONAL axes chosen independently, not one flat enum
// trying to do everything). Real gap this closes: before this field
// existed, THREE separate mechanisms could all determine a generator's
// effective cycle length at once - `synced` (tempo/rate-division lock),
// `rateModSource` (another generator speeds/slows this one), and (once
// ported per-host) a "lock to a lane's measured rhythm" feature - with
// precedence resolved silently in each host's own tick function
// ("takes priority over X when both are set"), not visible anywhere in
// the UI. `rateSource` makes exactly ONE of these active at a time,
// structurally - the UI shows a single choice instead of N independent
// toggles whose interaction the user has to reverse-engineer.
// `synced`/`rate`/`shapePercent` (Synced) and `rateModSource`/
// `rateModDepth` (ModulatedByGenerator) stay as their own real storage
// fields below, unchanged - `rateSource` just decides which one
// actually gets APPLIED each tick; the fields themselves are harmless
// to leave populated even when not the active source (matches this
// project's own "keep prior config even while it's not driving
// anything, so switching back doesn't lose what you had" convention,
// same as `riseMs`/`fallMs` staying intact while `synced` is on).
// LockedToLane reuses the EXISTING triggerSourceType/
// triggerSourceOutputId fields (must be SpecificLane for this to mean
// anything) plus new host-tick-owned runtime state (ModRuntime's own
// `measuredIntervalMs`) - no new ModGenerator field needed for it
// beyond this enum value itself. Default Free preserves every existing
// generator's behavior exactly (matches the old "neither synced nor
// rateModSource set" starting point).
enum class ModRateSource {
    Free,                // free-running - riseMs/fallMs/DAHDSR taken literally, nothing else adjusts cycle length
    Synced,              // tempo/rate-division locked - see `synced`/`rate`/`shapePercent` below
    ModulatedByGenerator, // another generator's output speeds/slows this one - see `rateModSource`/`rateModDepth` below
    LockedToLane         // cycle length continuously matches the REAL measured gap between a chosen lane's own hits (triggerSourceType must be SpecificLane)
};

// Only meaningful for the Triangle shape (and Custom's fallback ramp) -
// Sine/Square/Saw have their own fixed curve math and ignore this.
enum class ModCurveType { Linear, Exponential, Logarithmic };

// Raised 10->32 (2026-08-28, "mod e um engine custoso computacionalmente?"
// -> confirmed negligible cost (10 generators tick at 200Hz on the
// message thread, plain float math, no per-sample DSP) -> "estamos
// sendo conservadores no numero de moduladores entao" - user chose 32
// via explicit options. Every consumer of this constant (Percutere's
// `modGenerators`/`modRuntimes`/`modScopeHistory` arrays, ModPanel's
// own row table - now scrollable to fit 32 rows) already sizes off
// this constant dynamically, so raising it here is the whole change.
constexpr int kModGeneratorCount = 32;
// Per-generator destination slot count - each generator can target up to
// this many parameters at once, freely assigned (outputId + paramId
// picked together), not a fixed 1:1 mapping to any particular parameter
// list. Independent of voice/part count.
// Raised 10->12 (2026-09-01, "aumente para 12 destinos de modulacao" ->
// "12 destinations para todos os apps" - family-wide, matching Volca
// Drum/Sample2/Kick's own local Mod.h copies, part of the Volca Drum
// MOD-port plan). ModPanel's own DEST popup already sizes off this
// constant generically.
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

// One stage of a SegmentChain shape (2026-09-01, Phase C of "reduzir a
// complexidade do mod sem perder poder" - studied Mutable Instruments
// Stages' own real firmware source, `stages/segment_generator.h`, for
// the idea: an envelope isn't a fixed 6-field DAHDSR struct, it's a
// CHAIN of typed segments, each independently timed/curved). Kept
// deliberately smaller than Stages' own 3-type model (Ramp/Step/Hold,
// up to 36) - Step is mainly useful for Stages' own sequencer mode,
// out of scope here; Ramp/Hold alone already generalize DAHDSR fully
// (a "Decay" stage IS a Ramp toward the sustain level, a "Sustain"
// stage IS a Hold at that level).
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
    float fallMs = 300.0f; // Release
    // Full DAHDSR shaping (2026-08-28, "criar varios formatos de adsr
    // tradicionais e outros mais diferenciados" / "delay, attack, decay,
    // sustain e release" / "and hold" / "mantenha a elegancia, so
    // aprimore") - NOT a separate envelope machine bolted on: these are
    // just 4 more segments on the exact same phaseMs timeline Rise+Fall
    // already walks (see riseFallEnvelope() in ModEngine.cpp), the same
    // way Rise+Fall already ARE "the shape" for every continuous
    // ModShape. All default to values that collapse EXACTLY back to the
    // plain "AD envelope" this engine has always been - delayMs=0,
    // holdMs=0, decayMs=0, sustainLevel01=1.0, sustainMs=0 - so every
    // existing generator/preset behaves identically until deliberately
    // given a real DAHDSR shape:
    //   Delay (silence) -> Attack (riseMs, 0->1) -> Hold (holdMs, flat
    //   at 1) -> Decay (decayMs, 1->sustainLevel01) -> Sustain (sustainMs,
    //   flat at sustainLevel01) -> Release (fallMs, sustainLevel01->0).
    float delayMs = 0.0f;
    float holdMs = 0.0f;
    float decayMs = 0.0f;
    float sustainLevel01 = 1.0f;
    float sustainMs = 0.0f;
    ModCurveType riseCurve = ModCurveType::Linear;
    ModCurveType fallCurve = ModCurveType::Exponential; // exponential fall reads as a natural percussive decay by default
    float turingChance = 0.5f; // 0 = always repeats (frozen), 1 = always re-rolls (pure random) - only used when shape == Turing

    // Random family axes (2026-09-01, Phase B of "reduzir a
    // complexidade do mod sem perder poder" - studied Mutable
    // Instruments Marbles' own real firmware source,
    // `marbles/random/x_y_generator.h`, for the idea). Before this,
    // SampleHold/Random/SlewedRandom were 3 hardcoded, mutually
    // exclusive algorithms with no shared controls - now they're all
    // the SAME resample engine, shaped by 4 continuous axes (only
    // meaningful when shape is one of those 3 - Turing keeps its own
    // separate `turingChance`/bit-register mechanism entirely
    // untouched, a genuinely different algorithm, not folded in here).
    // Shape still picks a quick-start PRESET that seeds sensible
    // starting values for these (SampleHold -> steps=1, SlewedRandom ->
    // steps=0, Random -> steps=1 + its own pre-existing jittered-
    // interval behavior, unaffected by this) - but the user can then
    // move these sliders continuously regardless of which Shape name
    // is currently selected, same "presets are just starting points for
    // a richer continuous space" idea Marbles itself uses.
    float randomSteps = 1.0f;  // 0 = fully smooth/glided (like the old SlewedRandom), 1 = fully stepped (like the old SampleHold) - continuous between
    float randomSpread = 1.0f; // 0 = flat/no movement, 1 = full 0..1 range - how WIDE the random draw is
    float randomBias = 0.5f;   // 0..1, skews the distribution up or down - 0.5 = neutral/uniform (matches every pre-existing generator's own default exactly)
    float randomDejaVu = 0.0f; // 0 = always a fresh random value, 1 = always replays the exact same sequence as last cycle - genuinely new, nothing in the old system could express this
    std::vector<ModBreakpoint> customCurve{{0.0f, 0.0f}, {1.0f, 0.0f}}; // only used when shape == Custom

    // Segment chain (2026-09-01, Phase C - only used when shape ==
    // SegmentChain, see ModSegment's own comment for the design). A
    // fresh generator's default 5-segment chain is a plain AD envelope
    // (Attack 20ms to 1.0, Release 300ms to 0.0) - the SAME shape every
    // other generator already defaults to via riseMs/fallMs, so
    // switching TO SegmentChain from a default generator doesn't
    // silently change what it sounds like. `cycle` (already exists,
    // shared with every other shape) decides whether this chain
    // repeats (Loop) or plays once per trigger (One-Shot) - deliberately
    // NOT a separate loop concept per-segment (Stages' own model has
    // that; scoped out here to keep one less axis for the user to
    // reason about, matching this whole redesign's own "one axis per
    // decision" principle).
    std::vector<ModSegment> segments{{ModSegmentType::Ramp, 20.0f, ModCurveType::Linear, 1.0f},
                                      {ModSegmentType::Ramp, 300.0f, ModCurveType::Exponential, 0.0f}};
    std::array<ModDestination, kModDestinationSlots> destinations{};

    // Rate Source (2026-09-01) - see ModRateSource's own comment for
    // the full "why" (replaces silent multi-flag precedence with one
    // explicit choice). Default Free.
    ModRateSource rateSource = ModRateSource::Free;

    // Tempo sync. Only actually APPLIED when rateSource == Synced (see
    // above) - when true (and cycle == Loop), riseMs/fallMs are
    // IGNORED and instead derived every tick from the current tempo,
    // `rate`, and `shapePercent` (where within the synced cycle the peak
    // sits - 0.5 = symmetric rise/fall). riseMs/fallMs themselves are
    // left untouched by syncing so turning Sync back off restores
    // whatever free-running values were there before. `synced` itself
    // is kept in sync with `rateSource` by the UI (true iff rateSource
    // == Synced) - both exist because `computeEffectiveRiseFall()`'s
    // own signature only takes the ModGenerator, not a separately-
    // threaded rateSource check, so this stays the simplest way for
    // that pure-math helper to know whether to engage.
    bool synced = false;
    ModRateDivision rate = ModRateDivision::Eighth;
    float shapePercent = 0.5f; // 0..1

    // PURE SHAPE (ported from Volca Drum, 2026-09-23 here). Sine/Triangle/
    // Saw/Square are normally BUILT from the envelope fields - Sine and
    // Triangle are the envelope itself, Saw is skewed by Rise/Fall, a
    // Square's pulse width is riseMs. That is waveshaping, and it means
    // picking "Sine" and editing Decay changes the curve rather than the
    // rate. With this on the shape is a pure function of phase; the
    // envelope times still set the cycle LENGTH. Default false keeps every
    // existing generator and preset as it was.
    bool pureShape = false;

    // Global trigger RATE (2026-09-23, ported from Volca Drum). Global used
    // to fire once per bar and nothing else; this is the same
    // ModRateDivision picker Synced generators use, so Global can fire every
    // beat, every two bars, on triplets. Only meaningful when
    // triggerSourceType == Global. Default Whole (4 beats) is exactly the
    // old once-a-bar, so existing generators do not move.
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

    // Trigger source (2026-08-28) - see ModTriggerSourceType's own
    // comment. Default None preserves every existing host's own prior
    // behavior exactly (nothing sync/reset-triggers this generator
    // unless explicitly configured).
    ModTriggerSourceType triggerSourceType = ModTriggerSourceType::None;
    int triggerSourceOutputId = -1; // meaningful only when triggerSourceType == SpecificLane
    ModSyncBehavior syncBehavior = ModSyncBehavior::Reset; // meaningful only when cycle == Loop and triggerSourceType != None

    // Mirror shape only (2026-08-28) - which external value to follow.
    // Opaque to RuidoCore itself, same "host-defined meaning" convention
    // triggerSourceOutputId already uses (1-based id, -1 = none) -
    // Percutere interprets mirrorPart as its own Part::outputId and
    // mirrorUseFilterEnvelope as "which of that Part's two separate
    // hardwired envelopes (engine vs. filter)". A different host is
    // free to interpret these fields however suits it, or ignore them
    // entirely if it never sets shape == Mirror.
    int mirrorPart = -1;
    bool mirrorUseFilterEnvelope = false;
};

} // namespace ruidocore
