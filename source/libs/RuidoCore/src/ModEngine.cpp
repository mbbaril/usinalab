#include "ruidocore/Mod/ModEngine.h"

#include <algorithm>
#include <cmath>

namespace ruidocore {

namespace {

constexpr float kPi = 3.14159265358979323846f;
// Minimum rise, fall and cycle duration in milliseconds.
constexpr float kMinTimeMs = 0.001f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float easeApply(float t, ModCurveType type) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (type) {
        case ModCurveType::Exponential: return t * t;
        case ModCurveType::Logarithmic: return std::sqrt(t);
        case ModCurveType::Linear:
        default: return t;
    }
}

// Evaluate delay, attack, hold, decay, sustain and release on one timeline.
// Zero-length stages collapse without changing the remaining stage timings.
float riseFallEnvelope(double phaseMs, float delayMs, float riseMs, float holdMs, float decayMs, float sustainLevel01,
                        float sustainMs, float fallMs, ModCurveType riseCurve, ModCurveType fallCurve,
                        bool useCosineEase) {
    delayMs = std::max(0.0f, delayMs);
    riseMs = std::max(kMinTimeMs, riseMs);
    holdMs = std::max(0.0f, holdMs);
    decayMs = std::max(0.0f, decayMs);
    sustainLevel01 = std::clamp(sustainLevel01, 0.0f, 1.0f);
    sustainMs = std::max(0.0f, sustainMs);
    fallMs = std::max(kMinTimeMs, fallMs);

    if (phaseMs < delayMs)
        return 0.0f;
    phaseMs -= delayMs;

    if (phaseMs < riseMs) {
        const float t = static_cast<float>(phaseMs) / riseMs;
        return useCosineEase ? 0.5f - 0.5f * std::cos(t * kPi) : easeApply(t, riseCurve);
    }
    phaseMs -= riseMs;

    if (phaseMs < holdMs)
        return 1.0f;
    phaseMs -= holdMs;

    if (phaseMs < decayMs) {
        const float t = static_cast<float>(phaseMs) / decayMs;
        return useCosineEase ? lerp(1.0f, sustainLevel01, 0.5f - 0.5f * std::cos(t * kPi))
                              : lerp(1.0f, sustainLevel01, easeApply(t, fallCurve));
    }
    phaseMs -= decayMs;

    if (phaseMs < sustainMs)
        return sustainLevel01;
    phaseMs -= sustainMs;

    const float t = static_cast<float>(phaseMs) / fallMs;
    const float fallValue = useCosineEase ? sustainLevel01 * (0.5f + 0.5f * std::cos(t * kPi))
                                           : lerp(sustainLevel01, 0.0f, easeApply(t, fallCurve));
    return std::clamp(fallValue, 0.0f, 1.0f);
}

float evaluateCustomCurve(const std::vector<ModBreakpoint>& curve, float phase01) {
    if (curve.size() < 2)
        return 0.0f;
    phase01 = std::clamp(phase01, 0.0f, 1.0f);
    for (size_t i = 0; i + 1 < curve.size(); ++i) {
        const auto& a = curve[i];
        const auto& b = curve[i + 1];
        if (phase01 >= a.phase && phase01 <= b.phase) {
            const float span = std::max(1e-6f, b.phase - a.phase);
            const float t = (phase01 - a.phase) / span;
            return a.value + (b.value - a.value) * t;
        }
    }
    return curve.back().value;
}

// Warps a normalized phase so it crosses the midpoint (0.5) at `skewPoint`
// instead of always at the linear centre - Saw/Custom's own asymmetry
// control, derived from riseMs/fallMs's own ratio (those shapes have no
// other way to express "leans early" vs "leans late").
float warpPhase01(float phase01, float skewPoint) {
    skewPoint = std::clamp(skewPoint, 0.02f, 0.98f);
    if (phase01 < skewPoint)
        return (phase01 / skewPoint) * 0.5f;
    return 0.5f + ((phase01 - skewPoint) / (1.0f - skewPoint)) * 0.5f;
}

float warpPhaseByRiseFall(float phase01, float riseMs, float fallMs) {
    return warpPhase01(phase01, riseMs / std::max(kMinTimeMs, riseMs + fallMs));
}

// Shared "pick a new random target" step used by S&H/Random/SlewedRandom -
// only Turing has its own bit-register logic.
float resampleUniform(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

// Shared random-family resampling controls.
float resampleRandomFamily(const ModGenerator& gen, ModRuntime& rt) {
    const auto slot = static_cast<size_t>(rt.randomStepIndex % ModRuntime::kRandomHistoryLen);
    float value;
    if (gen.randomDejaVu > 0.0f && resampleUniform(rt.rng) < gen.randomDejaVu) {
        value = rt.randomHistory[slot];
    } else {
        const float raw = resampleUniform(rt.rng);
        value = std::clamp(gen.randomBias + (raw - 0.5f) * gen.randomSpread, 0.0f, 1.0f);
    }
    rt.randomHistory[slot] = value;
    rt.randomStepIndex = (rt.randomStepIndex + 1) % ModRuntime::kRandomHistoryLen;
    return value;
}

// Interpolate targets according to randomSteps; zero glides across the full interval.
float randomFamilyGlide(float from, float to, double elapsedMs, double intervalMs, float steps, ModCurveType curve) {
    const double glideMs = std::max(0.0, 1.0 - static_cast<double>(steps)) * intervalMs;
    if (glideMs < static_cast<double>(kMinTimeMs))
        return to;
    const float t = static_cast<float>(std::clamp(elapsedMs / glideMs, 0.0, 1.0));
    return from + (to - from) * easeApply(t, curve);
}

// Evaluate the segment chain over its total duration in milliseconds.
double segmentChainTotalMs(const std::vector<ModSegment>& segments) {
    double total = 0.0;
    for (const auto& seg : segments)
        total += std::max(static_cast<double>(kMinTimeMs), static_cast<double>(seg.durationMs));
    return std::max(static_cast<double>(kMinTimeMs), total);
}

// Walks the chain to find which segment `phaseMs` currently falls
// inside, then evaluates that ONE segment - Ramp interpolates from
// wherever the PREVIOUS segment left off (0 before the very first
// segment, matching every other shape's own "rest = 0" convention) to
// its own targetLevel01; Hold stays flat. Falls to the LAST segment's
// own level once phaseMs has run past the whole chain (a One-Shot chain
// that's finished holds at its final level for the caller's own idle
// check to catch; a Loop chain never reaches this branch since the
// caller already wraps phaseMs via fmod before calling).
float evaluateSegmentChain(const std::vector<ModSegment>& segments, double phaseMs) {
    if (segments.empty())
        return 0.0f;
    float fromLevel = 0.0f;
    double t = phaseMs;
    for (const auto& seg : segments) {
        const double dur = std::max(static_cast<double>(kMinTimeMs), static_cast<double>(seg.durationMs));
        if (t < dur) {
            if (seg.type == ModSegmentType::Hold)
                return seg.targetLevel01;
            const float frac = static_cast<float>(t / dur);
            return lerp(fromLevel, seg.targetLevel01, easeApply(frac, seg.curve));
        }
        t -= dur;
        fromLevel = seg.targetLevel01;
    }
    return segments.back().targetLevel01;
}

} // namespace

namespace {

// Six envelope-stage durations composing one cycle.
//
// A tempo-SYNCED Loop's cycle IS its rate division: the caller has already
// split the division between rise and fall (computeEffectiveRiseFall), so
// rise + fall is the period. Delay/Hold/Decay/Sustain are literal ms, and
// adding them on top made "synced" drift off the grid; leaving them in while
// shortening the period instead cut the designed shape off mid-way, with a
// step to zero at every wrap. So all six are scaled by ONE ratio into the
// division - the same rule LockedToLane uses for a lane's measured interval -
// and the period and the shape are both right.
//
// Free, rate-modulated and One-Shot generators take all six literally.
struct EnvelopeTimes {
    float delay, rise, hold, decay, sustain, fall;
    double total;
};

EnvelopeTimes envelopeTimes(const ModGenerator& gen, float riseMs, float fallMs) {
    EnvelopeTimes t{std::max(0.0f, gen.delayMs), riseMs, std::max(0.0f, gen.holdMs), std::max(0.0f, gen.decayMs),
                    std::max(0.0f, gen.sustainMs), fallMs, 0.0};
    const double sum = static_cast<double>(t.delay) + t.rise + t.hold + t.decay + t.sustain + t.fall;
    if (gen.synced && gen.cycle == ModCycleMode::Loop) {
        const double period = std::max(1.0, static_cast<double>(riseMs) + static_cast<double>(fallMs));
        const float scale = static_cast<float>(period / std::max(1.0, sum));
        t.delay *= scale;
        t.rise *= scale;
        t.hold *= scale;
        t.decay *= scale;
        t.sustain *= scale;
        t.fall *= scale;
        t.total = period;
    } else {
        t.total = std::max(1.0, sum);
    }
    return t;
}

// Pure shape is a normalized phase function; envelope timings determine cycle duration.
float evaluatePureShape(ModShape shape, float phase01) {
    constexpr float kTwoPi = 6.28318530717958647692f;
    switch (shape) {
        case ModShape::Sine:
            return 0.5f - 0.5f * std::cos(phase01 * kTwoPi); // starts at 0 like the envelope-built one
        case ModShape::Triangle:
            return 1.0f - std::abs(2.0f * phase01 - 1.0f);
        case ModShape::Saw:
            return phase01;
        case ModShape::Square:
            return phase01 < 0.5f ? 1.0f : 0.0f; // a real 50% duty cycle, not riseMs
        default:
            return phase01;
    }
}

bool shapeSupportsPure(ModShape shape) {
    return shape == ModShape::Sine || shape == ModShape::Triangle || shape == ModShape::Saw
           || shape == ModShape::Square || shape == ModShape::Custom;
}

// Continuous waveform evaluation shared by loop and one-shot modes.
float evaluateContinuousShape(const ModGenerator& gen, const EnvelopeTimes& env, float warpedPhase01,
                              double warpedPhaseMs) {
    if (gen.pureShape && shapeSupportsPure(gen.shape)) {
        if (gen.shape == ModShape::Custom)
            return evaluateCustomCurve(gen.customCurve, warpedPhase01);
        return evaluatePureShape(gen.shape, warpedPhase01);
    }
    switch (gen.shape) {
        case ModShape::Saw:
            return warpPhaseByRiseFall(warpedPhase01, env.rise, env.fall);
        case ModShape::Square:
            return warpedPhaseMs < env.rise ? 1.0f : 0.0f; // pulse of width rise within the cycle
        case ModShape::Custom:
            return evaluateCustomCurve(gen.customCurve, warpPhaseByRiseFall(warpedPhase01, env.rise, env.fall));
        case ModShape::SegmentChain:
            return evaluateSegmentChain(gen.segments, warpedPhaseMs);
        case ModShape::Sine:
        case ModShape::Triangle:
        default:
            return riseFallEnvelope(warpedPhaseMs, env.delay, env.rise, env.hold, env.decay, gen.sustainLevel01,
                                     env.sustain, env.fall, gen.riseCurve, gen.fallCurve, gen.shape == ModShape::Sine);
    }
}

} // namespace

float advanceModGenerator(const ModGenerator& gen, ModRuntime& rt, double deltaMs, bool triggerEdge,
                           float externalValue) {
    // Mirror bypasses everything else entirely - no phase, no rise/fall,
    // no trigger handling. See ModShape::Mirror's own comment.
    if (gen.shape == ModShape::Mirror)
        return std::clamp(externalValue, 0.0f, 1.0f);

    // Segment chains determine duration from their segment list, not DAHDSR fields.
    const EnvelopeTimes env = envelopeTimes(gen, gen.riseMs, gen.fallMs);
    const double totalMs = gen.shape == ModShape::SegmentChain ? segmentChainTotalMs(gen.segments) : env.total;

    if (triggerEdge) {
        // Soft synchronization applies only in loop mode. One-shot triggers always restart phase.
        if (gen.cycle == ModCycleMode::Loop && gen.syncBehavior == ModSyncBehavior::Sync) {
            // Soft synchronization gradually pulls loop phase toward the downbeat.
            // One-shot mode remains hard-triggered.
            constexpr double kSyncPullFactor = 0.5;
            rt.phaseMs *= (1.0 - kSyncPullFactor);
        } else {
            rt.phaseMs = 0.0;
            rt.stepElapsedMs = 0.0;
        }
        rt.oneShotIdle = false;
        if (gen.cycle == ModCycleMode::OneShot) {
            // A hit always yields a fresh sample for the stepped shapes too
            // ("random accent per hit" falls naturally out of this) rather
            // than waiting for a full cycle's worth of time to pass.
            rt.slewFrom = rt.heldValue;
            rt.heldValue = gen.shape == ModShape::Turing ? 0.0f : resampleRandomFamily(gen, rt);
        }
    }

    if (gen.cycle == ModCycleMode::OneShot && rt.oneShotIdle)
        return 0.0f; // Hold at rest before the first trigger or after one-shot completion.
        // Stepped random shapes use a resampling clock independent of envelope phase.
    if (gen.shape == ModShape::SampleHold || gen.shape == ModShape::Random || gen.shape == ModShape::SlewedRandom ||
        gen.shape == ModShape::Turing) {
        if (gen.cycle == ModCycleMode::OneShot) {
            // One resample on trigger (handled above), then hold until the
            // envelope's own rise/fall time has elapsed, then go idle.
            rt.phaseMs += deltaMs;
            if (rt.phaseMs >= totalMs) {
                rt.oneShotIdle = true;
                return 0.0f;
            }
            // Apply randomSteps glide to resampled random shapes; register output remains stepped.
            if (gen.shape != ModShape::Turing)
                return randomFamilyGlide(rt.slewFrom, rt.heldValue, rt.phaseMs, static_cast<double>(gen.riseMs),
                                          gen.randomSteps, gen.riseCurve);
            return rt.heldValue;
        }

        // Loop mode: free-running step clock.
        if (rt.nextStepIntervalMs <= 0.0)
            rt.nextStepIntervalMs = totalMs;
        rt.stepElapsedMs += deltaMs;
        if (rt.stepElapsedMs >= rt.nextStepIntervalMs) {
            rt.stepElapsedMs = 0.0;
            rt.slewFrom = rt.heldValue;

            if (gen.shape == ModShape::Turing) {
                std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
                if (chanceDist(rt.rng) < gen.turingChance) {
                    std::uniform_int_distribution<int> bitDist(0, 1);
                    rt.turingRegister = static_cast<std::uint8_t>((rt.turingRegister << 1) | bitDist(rt.rng));
                }
                rt.heldValue = static_cast<float>(rt.turingRegister) / 255.0f;
            } else {
                rt.heldValue = resampleRandomFamily(gen, rt);
            }

            rt.nextStepIntervalMs =
                gen.shape == ModShape::Random
                    ? totalMs * (0.3 + resampleUniform(rt.rng) * 1.4) // jittered, not clocked
                    : totalMs;
        }

        // Use the same randomSteps glide as the one-shot branch.
        if (gen.shape != ModShape::Turing)
            return randomFamilyGlide(rt.slewFrom, rt.heldValue, rt.stepElapsedMs, rt.nextStepIntervalMs, gen.randomSteps,
                                      gen.riseCurve);
        return rt.heldValue;
    }

    // --- Continuous/phase-based shapes ---
    rt.phaseMs += deltaMs;

    // Explicit warp applies to EVERY continuous shape, One-Shot and Loop
    // alike - computed once here, in ms space, so every branch below just
    // uses warpedPhaseMs instead of rt.phaseMs. 0.5 = no-op (warpPhase01
    // with skewPoint 0.5 is the identity).
    const float warpedPhase01 = warpPhase01(static_cast<float>(std::fmod(rt.phaseMs, totalMs) / totalMs), gen.warpAmount);
    const double warpedPhaseMs = static_cast<double>(warpedPhase01) * totalMs;

    if (gen.cycle == ModCycleMode::OneShot) {
        if (rt.phaseMs >= totalMs) {
            rt.oneShotIdle = true;
            return 0.0f;
        }
        return evaluateContinuousShape(gen, env, warpedPhase01, warpedPhaseMs);
    }

    // Loop mode
    rt.phaseMs = std::fmod(rt.phaseMs, totalMs);
    return evaluateContinuousShape(gen, env, warpedPhase01, warpedPhaseMs);
}

float applyModLag(float raw01, float& lagState, float lagAmount, double deltaMs) {
    if (lagAmount <= 0.0f)
        return lagState = raw01;
    // One-pole time-normalized exponential filter - lagAmount maps to a
    // response time roughly 1ms (no lag) to ~2s (very slow glide),
    // independent of deltaMs so it behaves the same regardless of tick
    // jitter.
    const float timeConstantMs = lerp(1.0f, 2000.0f, std::clamp(lagAmount, 0.0f, 1.0f));
    const float coeff = 1.0f - std::exp(static_cast<float>(-deltaMs) / timeConstantMs);
    lagState += (raw01 - lagState) * coeff;
    return lagState;
}

float applyModWavefold(float raw01, float foldAmount) {
    if (foldAmount <= 0.0f)
        return raw01;
    // Classic wavefolder: push the signal past [0,1] proportionally to
    // foldAmount, then reflect it back into range (triangle-wave
    // folding) - turns a smooth shape spikier/more harmonically complex
    // the harder it's pushed.
    const float drive = 1.0f + foldAmount * 6.0f;
    float v = (raw01 - 0.5f) * drive;
    // Reflect repeatedly until back in [-0.5, 0.5].
    while (v > 0.5f || v < -0.5f) {
        if (v > 0.5f)
            v = 1.0f - v;
        else if (v < -0.5f)
            v = -1.0f - v;
    }
    return std::clamp(v + 0.5f, 0.0f, 1.0f);
}

float previewModShape(const ModGenerator& gen, float phase01, double bpm, float externalValue) {
    phase01 = std::clamp(phase01, 0.0f, 1.0f);
    if (gen.shape == ModShape::Mirror)
        return std::clamp(externalValue, 0.0f, 1.0f);
    float riseMs = gen.riseMs, fallMs = gen.fallMs;
    computeEffectiveRiseFall(gen, bpm, riseMs, fallMs);
    // Delay/Hold/Decay/Sustain aren't tempo-synced (only Rise/Fall are,
    // via computeEffectiveRiseFall above) - always the generator's own
    // literal values, same as advanceModGenerator's own totalMs.
    const EnvelopeTimes env = envelopeTimes(gen, riseMs, fallMs);
    const double totalMs = gen.shape == ModShape::SegmentChain ? segmentChainTotalMs(gen.segments) : env.total;
    const float warpedPhase01 = warpPhase01(phase01, gen.warpAmount);

    if (gen.pureShape && shapeSupportsPure(gen.shape)) {
        if (gen.shape == ModShape::Custom)
            return evaluateCustomCurve(gen.customCurve, warpedPhase01);
        return evaluatePureShape(gen.shape, warpedPhase01);
    }

    switch (gen.shape) {
        case ModShape::Saw:
            return warpPhaseByRiseFall(warpedPhase01, env.rise, env.fall);
        case ModShape::Square:
            return warpedPhase01 * static_cast<float>(totalMs) < env.rise ? 1.0f : 0.0f;
        case ModShape::Custom:
            return evaluateCustomCurve(gen.customCurve, warpPhaseByRiseFall(warpedPhase01, env.rise, env.fall));
        case ModShape::SegmentChain:
            return evaluateSegmentChain(gen.segments, static_cast<double>(warpedPhase01) * totalMs);
        case ModShape::SampleHold:
        case ModShape::Random:
        case ModShape::SlewedRandom:
        case ModShape::Turing: {
            // No true deterministic curve for these - a fixed 8-step
            // staircase, hashed from the step index so it looks
            // "stepped/random" without pretending to predict real values.
            const int step = std::clamp(static_cast<int>(phase01 * 8.0f), 0, 7);
            std::mt19937 hashRng(static_cast<unsigned int>(step) * 2654435761u + 1u);
            return resampleUniform(hashRng);
        }
        case ModShape::Sine:
        case ModShape::Triangle:
        default:
            return riseFallEnvelope(static_cast<double>(warpedPhase01) * totalMs, env.delay, env.rise, env.hold,
                                     env.decay, gen.sustainLevel01, env.sustain, env.fall, gen.riseCurve, gen.fallCurve,
                                     gen.shape == ModShape::Sine);
    }
}

void computeEffectiveRiseFall(const ModGenerator& gen, double bpm, float& outRiseMs, float& outFallMs) {
    if (!gen.synced || gen.cycle == ModCycleMode::OneShot) {
        outRiseMs = gen.riseMs;
        outFallMs = gen.fallMs;
        return;
    }
    const double quarterMs = 60000.0 / std::max(20.0, bpm);
    const double cycleMs = quarterMs * static_cast<double>(rateDivisionBeats(gen.rate));
    const float shapePercent = std::clamp(gen.shapePercent, 0.02f, 0.98f); // never fully collapse either side
    outRiseMs = std::max(kMinTimeMs, static_cast<float>(cycleMs) * shapePercent);
    outFallMs = std::max(kMinTimeMs, static_cast<float>(cycleMs) * (1.0f - shapePercent));
}

} //
