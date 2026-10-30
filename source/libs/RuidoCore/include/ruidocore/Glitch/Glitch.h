#pragma once

#include "../Sequencer/Sequencer.h"

#include <atomic>
#include <random>
#include <vector>

namespace ruidocore {

// Transform type is independent of transform amount.
//
// Transforms include bursts, cuts, repeats, clock changes and silence.
//
// So the engine has two layers.
//
// TEXTURE is the per-step dice this always had: a nudge here, a re-rolled
// accent there, a CC thrown at something unrelated. It is what makes a
// pattern feel unstable, and on its own it is all this engine ever did -
// which is why wide open it read as one uniform noise rather than as
// something happening.
//
// A glitch gesture spans an interval: chops produce bounded silence runs and
// warps apply temporary part-clock changes. Amount controls intensity separately.
//
// The weights below are RELATIVE, not probabilities - all equal means
// "surprise me", and zeroing one takes it out of the vocabulary.
struct GlitchGestures {
    // A run of silence. What "chop" actually means: the part stops, and
    // then it comes back.
    float chop = 1.0f;
    // A run of ratchet bursts that TIGHTENS as it goes - a machine
    // getting stuck, rather than a constant roll.
    float stutter = 1.0f;
    // THE ONE THAT BREAKS THE BAR. This Part runs at a multiplied or
    // divided clock for the length of the gesture, so it walks out of the
    // time signature and back into it. Nothing is written to the Pattern;
    // see Sequencer::setClockWarp.
    float warp = 1.0f;
    // A run where every hit lands badly off the grid, each one drawn on
    // its own - the unstructured cousin of Rush.
    float scatter = 1.0f;
    // One step repeats verbatim for the whole gesture - the buffer stuck
    // on itself. This is repetition used AS a gesture, which is the
    // opposite of repetition as a texture: bounded, obviously deliberate,
    // and it ends.
    float freeze = 1.0f;
    // The lane's own content plays MIRRORED within its length, so the
    // phrase runs backwards without the cursor ever moving.
    float reverse = 1.0f;
    // A timing RAMP across the gesture rather than random scatter: the
    // lane rushes ahead of the beat or drags behind it, progressively,
    // and snaps back at the end. The other way to break a bar.
    float rush = 1.0f;
    // Hits appear on a lane that had none, densely, for the duration.
    float spray = 1.0f;
    // A pitch ramp across the gesture - a fall or a climb the ear can
    // follow, unlike the per-step pitch dice in the texture layer.
    float dive = 1.0f;
    // Every lane BUT one drops out. The only gesture that is global
    // rather than per-Part, because that is what makes it land - and the
    // reason its default weight is a third of the others: it costs SIX
    // lanes where the rest cost one, so at equal weight it swallowed half
    // the playing time and the pattern became a solo. Measured, not
    // guessed (scratchpad/drumglitch/storm.cpp).
    float spotlight = 0.3f;
};

struct GlitchParams {
    // -1 = inherit the global `amount`; 0..1 = explicit per-part override.
    // Sized/indexed to match whatever Pattern this engine is processing -
    // an out-of-range partIndex is treated as "inherit" (see
    // effectiveAmountFor()), so it is safe to leave this shorter than the
    // real part count.
    std::vector<float> perPartAmount;

    // Region gating: if regionStartStep < 0, glitch applies everywhere;
    // otherwise only within [regionStartStep, regionEndStep] (inclusive,
    // no wraparound).
    int regionStartStep = -1;
    int regionEndStep = -1;

    GlitchGestures gestures;

    // HOW LONG a gesture holds, in steps, before the engine re-rolls.
    // This is the control that answers "glitch is not necessarily fast":
    // at 1 it is a per-step twitch, at 8 a chop is half a bar of silence
    // and a warp lasts long enough to hear the bar come apart and knit
    // itself back together. Drawn 1..gestureSteps rather than fixed, so a
    // long setting still throws short gestures - a constant length is its
    // own kind of repetition.
    int gestureSteps = 4;

    // HOW OFTEN a Part that is between gestures starts a new one, scaled
    // by amount. Below 1 leaves space between gestures, which is what
    // makes each one legible; at 1 a Part is almost always inside one.
    float gestureDensity = 0.6f;

    // Maximum simultaneous independent controller changes per step at full amount.
    // Clamp to kMaxStormWidth.
    int stormWidth = 4;

    // How far a warp may bend the clock: multiplier and divider are drawn
    // from 1..warpDepth. 1 disables warping without having to zero its
    // own weight.
    int warpDepth = 4;
};

// Host capability profile and hooks for audible transform effects.
//
// Hooks are plain function pointers: process() runs on the audio thread.
struct GlitchProfile {
    bool accent = true;            // re-roll a step's accentAmount
    bool pitch = true;             // shift a step's note
    bool injectedDynamics = true;  // an injected hit gets a velocity and an accent

    // After accentAmount was re-rolled - e.g. rebuild the step's own
    // accent-derived locks, and ONLY those.
    void (*afterAccent)(StepEvent& ev) = nullptr;
    // After the note was shifted by `semitones` - e.g. move a host's own
    // pitch field by the same interval.
    void (*afterPitchShift)(StepEvent& ev, int semitones) = nullptr;
    // After an injected hit got its dynamics.
    void (*afterInject)(StepEvent& ev) = nullptr;

    // "Rapid change on an unrelated parameter": host parameter ids picked
    // from this list get a random 0..127 lock on the step. Empty = off.
    //
    // CAUTION for a host sending these to hardware: the lock sets the
    // parameter and nothing sets it back. A host that wants the change to
    // be temporary must restore the parameter itself.
    const int* glitchableParams = nullptr;
    int glitchableParamCount = 0;
};

// Live performance transform, deliberately NOT a Pattern mutation:
// process() reads the Sequencer's true output and the original Pattern,
// and returns a *new* set of steps to actually send - the stored Pattern
// and Sequencer cursor state are never touched. Set amount back to 0 and
// the very next tick plays the untouched original again ("Random is not
// Glitch" - the glitch macro is a live overlay, not a mutation of what's
// stored). amount is atomic so it can be turned live from another thread
// (e.g. a UI or MIDI Learn knob) while the audio/scheduling thread reads
// it every step.
class GlitchEngine {
public:
    // At most this many CCs on one step, whatever stormWidth asks for.
    // Four on six parts at 16ths and 120 bpm is under 200 messages a
    // second, against the DIN's own ceiling near a thousand.
    static constexpr int kMaxStormWidth = 4;

    explicit GlitchEngine(GlitchParams params = {}, std::uint32_t seed = 1, GlitchProfile profile = {});

    std::atomic<float> amount{0.0f}; // 0-1, the GLITCH macro

    void setParams(const GlitchParams& newParams) { params = newParams; }
    const GlitchParams& getParams() const { return params; }

    // trueFired is exactly what Sequencer::tick() returned this step -
    // untouched. pattern is the Sequencer's underlying Pattern, needed to
    // synthesize plausible injected hits (outputId, etc.) on parts that
    // didn't fire this step, and to read a lane's own steps for the
    // gestures that replay them (Freeze, Reverse).
    std::vector<FiredStep> process(const Pattern& pattern, const std::vector<FiredStep>& trueFired,
                                    int globalStepIndex);

    // Realtime form reads and rewrites the caller-owned event buffer without allocation.
    std::size_t processInto(const Pattern& pattern, const FiredStep* trueFired, std::size_t count,
                            int globalStepIndex, std::vector<FiredStep>& out);

    // What each Part is in the middle of. Exposed because a gesture
    // nobody can SEE is a gesture nobody can play with.
    enum class Gesture { None, Chop, Stutter, Warp, Scatter, Freeze, Reverse, Rush, Spray, Dive, Spotlight };
    Gesture gestureFor(int partIndex) const;
    static const char* toString(Gesture gesture);

    // What this Part's clock should read RIGHT NOW, for the Sequencer's
    // own ClockWarp hook. Left alone unless a Warp is running on it.
    void warpClock(int partIndex, int& multiplier, int& divider) const;

    // The trampoline a Sequencer's ClockWarp expects, so a host wires the
    // two together without writing this by hand in six products:
    //   seq.setClockWarp(&GlitchEngine::clockWarpTrampoline, &glitch);
    static void clockWarpTrampoline(void* context, int partIndex, int& multiplier, int& divider) {
        if (auto* self = static_cast<GlitchEngine*>(context))
            self->warpClock(partIndex, multiplier, divider);
    }

private:
    float effectiveAmountFor(int partIndex, int globalStepIndex) const;
    void advanceGestures(const Pattern& pattern, int globalStepIndex);
    Gesture drawGesture(Gesture previous);

    GlitchParams params;
    GlitchProfile profile;
    std::mt19937 rng;

    // Kept, not rebuilt: process() runs on the audio thread, and assign()
    // only allocates when the part count grows.
    std::vector<char> firedThisStep;
    // Working copies reused every call, so a modified or injected step is
    // built in storage that already holds locks capacity - see processInto.
    FiredStep scratchStep;
    FiredStep scratchInjected;

    // Which parameter each Part's last CC storm ended on, so the next one
    // cannot start where the previous ended - the cheapest thing that
    // stops a wide-open GLITCH from sounding like a loop.
    std::vector<int> lastGlitchedParam;

    struct PartGesture {
        Gesture kind = Gesture::None;
        Gesture previousKind = Gesture::None; // never the same gesture twice running
        int stepsLeft = 0;
        int totalSteps = 1;   // so a gesture can know WHERE it is inside itself
        int warpMultiplier = 1;
        int warpDivider = 1;
        int frozenStep = -1;  // the step index a Freeze holds
        int direction = 1;    // which way a Rush or a Dive leans
    };
    std::vector<PartGesture> partGestures;

    // Spotlight is global: one lane survives, the rest drop out.
    int spotlightStepsLeft = 0;
    int spotlightPart = -1;

    // Gestures advance once per STEP. process() can be called more than
    // once for the same step index, and a gesture that shortened itself on
    // every call would never last.
    int lastStepIndex = -1;
};

} //
