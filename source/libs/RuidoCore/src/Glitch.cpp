#include "ruidocore/Glitch/Glitch.h"

#include <algorithm>
#include <cmath>

namespace ruidocore {

namespace {

// Map amount to transform probability, increasing strongly near the upper range.
//
// Every roll used to be a flat `amount * k` with k at most 0.4, so at
// FULL GLITCH the strongest transform still left six steps in ten
// untouched. The top of the knob was timid, and a knob whose top is timid
// has no top.
//
// `a * (k + (1 - k) * a^2)` leaves the low and middle of the range where
// they were - at a = 0.5 it is within a few percent of the old 0.5k - and
// drives a transform toward certainty as a approaches 1. The cubic is
// what makes the last quarter of the knob a different instrument rather
// than more of the same one.
float glitchChance(float amount, float k) {
    return amount * (k + (1.0f - k) * amount * amount);
}

// The same shape for MAGNITUDE: the old linear span at the bottom, twice
// it at the top.
float glitchSpan(float amount, float base) {
    return base * amount + base * amount * amount * amount;
}

// ...except where certainty is the wrong answer. Removing a step is the
// one transform that must NOT approach 1: at certainty GLITCH deletes the
// music instead of wrecking it, and silence is not violence, it is
// nothing. Caught by the core's own profile test the moment the curve
// above was applied to it blindly.
float boundedChance(float amount, float k, float ceiling) {
    return std::min(glitchChance(amount, k), ceiling);
}

} // namespace

GlitchEngine::GlitchEngine(GlitchParams paramsIn, std::uint32_t seed, GlitchProfile profileIn)
    : params(std::move(paramsIn)), profile(profileIn), rng(seed) {}

float GlitchEngine::effectiveAmountFor(int partIndex, int globalStepIndex) const {
    const bool regionActive = params.regionStartStep < 0 ||
                               (globalStepIndex >= params.regionStartStep && globalStepIndex <= params.regionEndStep);
    if (!regionActive)
        return 0.0f;
    if (partIndex < 0 || partIndex >= static_cast<int>(params.perPartAmount.size()))
        return amount.load();
    const float perPart = params.perPartAmount[static_cast<size_t>(partIndex)];
    return perPart >= 0.0f ? perPart : amount.load();
}

const char* GlitchEngine::toString(Gesture gesture) {
    switch (gesture) {
        case Gesture::None: return "-";
        case Gesture::Chop: return "Chop";
        case Gesture::Stutter: return "Stutter";
        case Gesture::Warp: return "Warp";
        case Gesture::Scatter: return "Scatter";
        case Gesture::Freeze: return "Freeze";
        case Gesture::Reverse: return "Reverse";
        case Gesture::Rush: return "Rush";
        case Gesture::Spray: return "Spray";
        case Gesture::Dive: return "Dive";
        case Gesture::Spotlight: return "Spotlight";
    }
    return "-";
}

GlitchEngine::Gesture GlitchEngine::gestureFor(int partIndex) const {
    if (spotlightStepsLeft > 0 && partIndex != spotlightPart)
        return Gesture::Spotlight;
    if (partIndex < 0 || partIndex >= static_cast<int>(partGestures.size()))
        return Gesture::None;
    return partGestures[static_cast<size_t>(partIndex)].kind;
}

void GlitchEngine::warpClock(int partIndex, int& multiplier, int& divider) const {
    if (partIndex < 0 || partIndex >= static_cast<int>(partGestures.size()))
        return;
    const auto& g = partGestures[static_cast<size_t>(partIndex)];
    if (g.kind != Gesture::Warp || g.stepsLeft <= 0)
        return;
    // Multiply temporary clock factors by the stored part multiplier and divider.
    multiplier = std::max(1, multiplier * g.warpMultiplier);
    divider = std::max(1, divider * g.warpDivider);
}

// Weighted pick over the vocabulary, refusing to repeat the gesture that
// just ended on this Part. "Se repetir e a coisa mais anti-glitch" - and
// the repetition an ear catches first is the same trick twice running.
GlitchEngine::Gesture GlitchEngine::drawGesture(Gesture previous) {
    const GlitchGestures& w = params.gestures;
    const Gesture kinds[] = {Gesture::Chop,   Gesture::Stutter, Gesture::Warp,  Gesture::Scatter,
                              Gesture::Freeze, Gesture::Reverse, Gesture::Rush,  Gesture::Spray,
                              Gesture::Dive,   Gesture::Spotlight};
    float weights[] = {w.chop,    w.stutter, w.warp,  w.scatter, w.freeze,
                        w.reverse, w.rush,    w.spray, w.dive,    w.spotlight};
    constexpr int count = 10;
    // A warp with a depth of 1 would be a gesture that does nothing at
    // all, and a silent no-op is worse than an absent option: the control
    // still seems to promise something.
    if (params.warpDepth <= 1)
        weights[2] = 0.0f;
    // Disable pitch dives for profiles without pitch control.
    if (!profile.pitch)
        weights[8] = 0.0f;
    // One spotlight at a time. Two lanes each claiming the stage back to
    // back is not a stronger gesture, it is a longer one with a seam in
    // the middle.
    if (spotlightStepsLeft > 0)
        weights[9] = 0.0f;

    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (kinds[i] == previous)
            weights[i] *= 0.15f; // strongly discouraged, not forbidden - "never" is its own pattern
        weights[i] = std::max(0.0f, weights[i]);
        total += weights[i];
    }
    if (total <= 0.0f)
        return Gesture::None;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    float roll = unit(rng) * total;
    for (int i = 0; i < count; ++i) {
        roll -= weights[i];
        if (roll <= 0.0f)
            return kinds[i];
    }
    return kinds[count - 1];
}

void GlitchEngine::advanceGestures(const Pattern& pattern, int globalStepIndex) {
    if (globalStepIndex == lastStepIndex)
        return; // one advance per step, however often process() is called
    lastStepIndex = globalStepIndex;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const int partCount = static_cast<int>(pattern.parts.size());

    if (spotlightStepsLeft > 0)
        --spotlightStepsLeft;

    for (int i = 0; i < partCount; ++i) {
        auto& g = partGestures[static_cast<size_t>(i)];
        if (g.stepsLeft > 0) {
            --g.stepsLeft;
            if (g.stepsLeft == 0) {
                g.previousKind = g.kind;
                g.kind = Gesture::None;
            }
            continue;
        }

        const float a = effectiveAmountFor(i, globalStepIndex);
        if (a <= 0.0f) {
            g.kind = Gesture::None;
            continue;
        }
        const float density = std::clamp(params.gestureDensity, 0.0f, 1.0f);
        if (unit(rng) >= glitchChance(a, 0.25f) * density)
            continue; // the space BETWEEN gestures is what makes each one legible

        const Gesture drawn = drawGesture(g.previousKind);
        if (drawn == Gesture::None)
            continue;

        // 1..gestureSteps, never a fixed length - a constant length is its
        // own kind of repetition, and the ear finds it fast.
        const int maxSteps = std::max(1, params.gestureSteps);
        g.totalSteps = std::min(1 + static_cast<int>(unit(rng) * static_cast<float>(maxSteps)), maxSteps);
        g.stepsLeft = g.totalSteps;
        g.direction = unit(rng) < 0.5f ? -1 : 1;

        if (drawn == Gesture::Spotlight) {
            // Global, and it takes the whole pattern with it - stored once
            // rather than per Part, and this Part carries no gesture of its
            // own while it runs.
            spotlightPart = i;
            spotlightStepsLeft = g.totalSteps;
            g.previousKind = Gesture::Spotlight;
            g.kind = Gesture::None;
            g.stepsLeft = 0;
            continue;
        }

        g.kind = drawn;

        if (drawn == Gesture::Warp) {
            const int depth = std::max(2, params.warpDepth);
            // Either multiply or divide, never both - one of them stays 1,
            // so the ratio against the other lanes is always a clean one.
            const bool faster = unit(rng) < 0.5f;
            const int factor = 2 + static_cast<int>(unit(rng) * static_cast<float>(depth - 1));
            g.warpMultiplier = faster ? std::min(factor, depth) : 1;
            g.warpDivider = faster ? 1 : std::min(factor, depth);
        } else {
            g.warpMultiplier = 1;
            g.warpDivider = 1;
        }

        if (drawn == Gesture::Freeze) {
            const Part& part = pattern.parts[static_cast<size_t>(i)];
            const int length = std::max(1, std::min(static_cast<int>(part.steps.size()), part.length));
            // Drawn from the lane's OWN content, so a Freeze repeats
            // something that belongs to the pattern rather than a
            // synthetic hit.
            g.frozenStep = static_cast<int>(unit(rng) * static_cast<float>(length)) % length;
        }
    }
}

std::vector<FiredStep> GlitchEngine::process(const Pattern& pattern, const std::vector<FiredStep>& trueFired,
                                              int globalStepIndex) {
    std::vector<FiredStep> out;
    out.resize(processInto(pattern, trueFired.data(), trueFired.size(), globalStepIndex, out));
    return out;
}

std::size_t GlitchEngine::processInto(const Pattern& pattern, const FiredStep* trueFiredData, std::size_t trueCount,
                                      int globalStepIndex, std::vector<FiredStep>& output) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Written over, never cleared: an existing slot keeps its locks capacity.
    std::size_t emitted = 0;
    const auto emit = [&output, &emitted](const FiredStep& s) {
        if (emitted == output.size())
            output.emplace_back();
        output[emitted++] = s;
    };
    struct FiredRange {
        const FiredStep* first;
        std::size_t n;
        const FiredStep* begin() const { return first; }
        const FiredStep* end() const { return first + n; }
    } const trueFired{trueFiredData, trueCount};

    firedThisStep.assign(pattern.parts.size(), 0);
    // RESIZED, never re-assigned: these two have to REMEMBER across steps.
    // Clearing them every block would quietly turn the anti-repetition
    // rule and every gesture off - and a rule that is silently off is the
    // hardest kind of cause to hear.
    if (lastGlitchedParam.size() != pattern.parts.size())
        lastGlitchedParam.resize(pattern.parts.size(), -1);
    if (partGestures.size() != pattern.parts.size())
        partGestures.resize(pattern.parts.size());

    advanceGestures(pattern, globalStepIndex);

    for (const auto& step : trueFired)
        if (step.partIndex >= 0 && step.partIndex < static_cast<int>(firedThisStep.size()))
            firedThisStep[static_cast<size_t>(step.partIndex)] = 1;

    for (const auto& original : trueFired) {
        const float a = effectiveAmountFor(original.partIndex, globalStepIndex);
        if (a <= 0.0f) {
            emit(original);
            continue;
        }

        // ---- STRUCTURE: what this Part is in the middle of ----
        const Gesture gesture = gestureFor(original.partIndex);

        // Spotlight silences every lane but its own, and outranks whatever
        // else a lane was doing - that is what makes it land.
        if (gesture == Gesture::Spotlight)
            continue;
        if (gesture == Gesture::Chop)
            continue; // the run of silence

        FiredStep& step = scratchStep;
        step = original; // into storage that keeps its locks capacity
        const PartGesture& g = partGestures[static_cast<size_t>(std::max(0, original.partIndex))];
        // 0 at the start of a gesture, 1 at its end - what lets Rush, Dive
        // and Stutter have a SHAPE instead of a constant value. A gesture
        // with no shape is a switch, not a gesture.
        const float progress = g.totalSteps > 1
            ? 1.0f - static_cast<float>(std::max(0, g.stepsLeft - 1)) / static_cast<float>(g.totalSteps - 1)
            : 1.0f;

        // FREEZE and REVERSE replay content from another step. Change the
        // note only for pitched profiles: drum machines may select pads by
        // note, so replaying another note would address a different pad.
        // Report pitch intervals through the host hook to keep any mirrored
        // pitch field synchronized.
        if ((gesture == Gesture::Freeze || gesture == Gesture::Reverse) && original.partIndex >= 0
            && original.partIndex < static_cast<int>(pattern.parts.size())) {
            const Part& part = pattern.parts[static_cast<size_t>(original.partIndex)];
            const int length = std::max(1, std::min(static_cast<int>(part.steps.size()), part.length));
            const int source = gesture == Gesture::Freeze
                ? g.frozenStep
                : length - 1 - (std::max(0, step.stepIndex) % length);
            if (source >= 0 && source < static_cast<int>(part.steps.size())) {
                const int noteBefore = resolvedNote(original.event, part);
                step.event = part.steps[static_cast<size_t>(source)];
                step.event.enabled = true;
                if (profile.pitch) {
                    const int noteAfter = resolvedNote(step.event, part);
                    step.event.explicitNote = noteAfter;
                    if (noteAfter != noteBefore && profile.afterPitchShift != nullptr)
                        profile.afterPitchShift(step.event, noteAfter - noteBefore);
                } else {
                    step.event.explicitNote = original.event.explicitNote;
                }
            }
        }

        if (gesture == Gesture::Stutter) {
            // TIGHTENS across the gesture: two hits at the start, eight by
            // the end. A constant roll is a drum fill; one that closes up
            // is a machine seizing.
            const int tight = 2 + static_cast<int>(progress * 6.0f);
            step.event.ratchet = std::clamp(std::max(step.event.ratchet, tight), 1, 8);
        } else if (gesture == Gesture::Rush) {
            // A RAMP, not a scatter: the lane pulls progressively ahead of
            // or behind the beat, and snaps back when the gesture ends.
            const float span = glitchSpan(a, 60.0f);
            step.event.microtimingMs += static_cast<int>(static_cast<float>(g.direction) * progress * span);
        } else if (gesture == Gesture::Scatter) {
            step.event.microtimingMs += static_cast<int>((unit(rng) - 0.5f) * 2.0f * glitchSpan(a, 60.0f));
        } else if (gesture == Gesture::Dive && profile.pitch && original.partIndex >= 0
                    && original.partIndex < static_cast<int>(pattern.parts.size())) {
            const int semitones =
                static_cast<int>(static_cast<float>(g.direction) * progress * glitchSpan(a, 18.0f));
            step.event.explicitNote = std::clamp(
                resolvedNote(step.event, pattern.parts[static_cast<size_t>(original.partIndex)]) + semitones, 0, 127);
            if (profile.afterPitchShift != nullptr)
                profile.afterPitchShift(step.event, semitones);
        }

        // ---- TEXTURE: the per-step dice, on top of whatever is running ----

        // Removal keeps a CEILING. Every other transform may approach
        // certainty at full amount; this one may not, or GLITCH deletes
        // the music instead of wrecking it. Chop is where real silence
        // comes from now, and a Chop has a beginning and an end.
        if (unit(rng) < boundedChance(a, 0.2f, 0.35f))
            continue;

        if (unit(rng) < glitchChance(a, 0.4f)) // temporal displacement
            step.event.microtimingMs += static_cast<int>((unit(rng) - 0.5f) * glitchSpan(a, 60.0f));

        if (unit(rng) < glitchChance(a, 0.3f)) // burst/stutter via ratchet
            step.event.ratchet = std::clamp(step.event.ratchet + 1 + static_cast<int>(a * a * 6.0f), 1, 8);

        if (profile.accent && unit(rng) < glitchChance(a, 0.4f)) { // temporary accent change
            step.event.accentAmount = unit(rng);
            if (profile.afterAccent != nullptr)
                profile.afterAccent(step.event);
        }

        if (profile.pitch && gesture != Gesture::Dive && unit(rng) < glitchChance(a, 0.2f)) {
            // A fired step's note is already resolved by the Sequencer;
            // resolving again is the safe reading either way. Skipped
            // during a Dive, which owns the pitch for its duration - two
            // writers on one field would erase the ramp the ear is
            // following.
            const int semitones = static_cast<int>((unit(rng) - 0.5f) * glitchSpan(a, 24.0f));
            step.event.explicitNote = std::clamp(
                resolvedNote(step.event, pattern.parts[static_cast<size_t>(step.partIndex)]) + semitones, 0, 127);
            if (profile.afterPitchShift != nullptr)
                profile.afterPitchShift(step.event, semitones);
        }

        // Emit several independently selected controller changes per step; amount controls density.
        //
        // Avoid repeating the same part and transform consecutively; keep independent selections.
        if (profile.glitchableParamCount > 0 && unit(rng) < glitchChance(a, 0.25f)) {
            const auto count = static_cast<size_t>(profile.glitchableParamCount);
            const int width = std::clamp(params.stormWidth, 1, kMaxStormWidth);
            const int wanted = 1 + static_cast<int>(a * a * static_cast<float>(width - 1));
            int previous = -1;
            if (step.partIndex >= 0 && step.partIndex < static_cast<int>(lastGlitchedParam.size()))
                previous = lastGlitchedParam[static_cast<size_t>(step.partIndex)];
            int sentThisStep[kMaxStormWidth] = {-1, -1, -1, -1};
            for (int n = 0; n < wanted && n < kMaxStormWidth; ++n) {
                size_t pick = static_cast<size_t>(unit(rng) * static_cast<float>(count)) % count;
                int param = profile.glitchableParams[pick];
                bool clash = (param == previous);
                for (int m = 0; m < n && !clash; ++m)
                    clash = (sentThisStep[m] == param);
                if (clash && count > 1) {
                    pick = (pick + 1u + static_cast<size_t>(unit(rng) * static_cast<float>(count - 1))) % count;
                    param = profile.glitchableParams[pick];
                }
                sentThisStep[n] = param;
                previous = param;
                step.event.locks.push_back({param, static_cast<int>(unit(rng) * 127.0f), "glitch"});
            }
            if (step.partIndex >= 0 && step.partIndex < static_cast<int>(lastGlitchedParam.size()))
                lastGlitchedParam[static_cast<size_t>(step.partIndex)] = previous;
        }

        emit(step);
    }

    // Duplication: inject a hit on a part that was originally silent this
    // step. A SPRAY gesture is this turned up - the lane that had nothing
    // suddenly has everything, for as long as the gesture runs.
    for (int partIndex = 0; partIndex < static_cast<int>(pattern.parts.size()); ++partIndex) {
        if (firedThisStep[static_cast<size_t>(partIndex)] != 0)
            continue;
        const Gesture gesture = gestureFor(partIndex);
        if (gesture == Gesture::Spotlight || gesture == Gesture::Chop)
            continue; // a lane that is deliberately silent stays silent
        const float a = effectiveAmountFor(partIndex, globalStepIndex);
        if (a <= 0.0f)
            continue;
        const float chance =
            gesture == Gesture::Spray ? std::max(0.75f, glitchChance(a, 0.15f)) : glitchChance(a, 0.15f);
        if (unit(rng) >= chance)
            continue;

        const Part& part = pattern.parts[static_cast<size_t>(partIndex)];
        FiredStep& injected = scratchInjected;
        injected = FiredStep{}; // every field back to default, locks capacity kept
        injected.partIndex = partIndex;
        injected.outputId = part.outputId;
        injected.event.enabled = true;
        injected.event.explicitNote = part.baseNote;
        if (profile.injectedDynamics) {
            injected.event.velocity = 80 + static_cast<int>(unit(rng) * 40.0f);
            injected.event.accentAmount = unit(rng);
            if (profile.afterInject != nullptr)
                profile.afterInject(injected.event);
        }
        if (unit(rng) < glitchChance(a, 0.3f))
            injected.event.ratchet = 2 + static_cast<int>(unit(rng) * glitchSpan(a, 2.0f));
        emit(injected);
    }

    return emitted;
}

} //
