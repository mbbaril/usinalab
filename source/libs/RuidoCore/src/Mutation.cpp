#include "ruidocore/Mutation/Mutation.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace ruidocore {

namespace {

// How much a step's position in the bar argues for it being ON, 0..1, on a
// 16-step grid. The downbeat, then the other beats, then the eighths, then
// everything else - the plainest statement of where a hit belongs, which
// is exactly what a flat coin has no idea about.
//
// Deliberately NOT a groove template: this biases the draw, it does not
// dictate a pattern. A 16th can still come up; it is just less likely
// than beat 1.
float metricWeight(int stepIndex) {
    switch (stepIndex % 16) {
        case 0:  return 1.00f;                          // the downbeat
        case 4: case 8: case 12: return 0.80f;          // the other beats
        case 2: case 6: case 10: case 14: return 0.45f; // eighths
        default: return 0.22f;                          // sixteenths
    }
}

// Carried ACROSS the steps of one Part, so a Groove pitch move is a line
// rather than a scatter. Reset per Part.
struct ContourState {
    int direction = 1;
    bool started = false;
    // The line's OWN position. Without it there is no contour at all:
    // every step would walk one degree from ITS OWN previous value, so
    // what the Part shared was a direction and nothing else - sixteen
    // independent nudges. Measured before that was fixed: net displacement
    // grew as sqrt(passes), which is what an unanchored walk does.
    int position = 0;
};

int pitchOf(const StepEvent& ev, const Part& part, MutationProfile::PitchTarget target) {
    return target == MutationProfile::PitchTarget::SemitoneOffset ? ev.pitchSemitoneOffset
                                                                   : resolvedNote(ev, part);
}

void setPitch(StepEvent& ev, MutationProfile::PitchTarget target, int value) {
    if (target == MutationProfile::PitchTarget::SemitoneOffset)
        ev.pitchSemitoneOffset = value;
    else
        ev.explicitNote = value;
}

void mutateStep(std::mt19937& rng, const MutationParams& params, const MutationProfile& profile, const Part& part,
                 StepEvent& ev, int stepIndex, ContourState& contour, bool pitchAllowed) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const bool groove = params.character == MutationCharacter::Groove;
    // Every range below is scaled by this. Groove is not a different
    // algorithm - it is the same one with musical numbers, which keeps
    // exactly one code path to reason about.
    const float ratchetChance = groove ? 0.08f : 0.35f;
    const float microSpan = groove ? 6.0f : static_cast<float>(profile.microtimingSpanMs) * 2.0f;
    const float pitchSpan = groove ? 2.0f : 12.0f;
    const float gateSpan = groove ? 0.15f : 0.60f;

    // At amount=1.0 (what every Randomize action uses) `unit(rng) < amount`
    // is ALWAYS true, so the old `ev.enabled = !ev.enabled` was a
    // guaranteed deterministic FLIP, not a random reroll - a pattern only
    // ever alternated between itself and its exact inversion. A real
    // coin-flip instead.
    if (!params.locks.rhythm && unit(rng) < params.amount) {
        // Groove weights the draw by where the step sits in the bar and by
        // the requested density; Glitch keeps the flat coin. The weight
        // MULTIPLIES density rather than replacing it, so the two controls
        // stay independent: density says how busy, the weight says where
        // the business goes.
        ev.enabled = groove ? (unit(rng) < params.density * metricWeight(stepIndex) * 2.0f)
                            : (unit(rng) < 0.5f);
    }

    if (!ev.enabled)
        return; // nothing audible left to mutate on a disabled step

    if (profile.accent && !params.locks.accent) {
        if (profile.velocityJitter) {
            const int jitter = static_cast<int>((unit(rng) - 0.5f) * 60.0f * params.amount);
            ev.velocity = std::clamp(ev.velocity + jitter, 1, 127);
        }
        const float accentJitter = (unit(rng) - 0.5f) * params.amount;
        if (groove && profile.accentPull) {
            // Pulled toward what the bar wants, never set: the pull is
            // scaled by amount, so a small amount respects what is there.
            const float target = 0.25f + 0.6f * metricWeight(stepIndex);
            ev.accentAmount += (target - ev.accentAmount) * params.amount * 0.6f + accentJitter * 0.4f;
        } else {
            ev.accentAmount += accentJitter;
        }
        ev.accentAmount = std::clamp(ev.accentAmount, 0.0f, 1.0f);
        if (profile.afterAccent != nullptr)
            profile.afterAccent(ev);
    }

    if (!params.locks.microtiming) {
        const int timingJitter = static_cast<int>((unit(rng) - 0.5f) * microSpan * 2.0f * params.amount);
        // Clamped SYMMETRICALLY: microtiming is bipolar, and a floor at
        // zero left half the control - the half that drags ahead of the
        // beat - unreachable from Randomize.
        ev.microtimingMs = std::clamp(ev.microtimingMs + timingJitter, -profile.microtimingSpanMs,
                                       profile.microtimingSpanMs);
    }

    if (!params.locks.ratchet) {
        // An occasional +-1 nudge rather than a fresh draw: ratchet is a
        // strong, very audible property, and re-rolling it on every step
        // turns a pattern into a machine-gun instead of a variation.
        if (unit(rng) < params.amount * ratchetChance)
            ev.ratchet = std::clamp(ev.ratchet + (unit(rng) < 0.5f ? -1 : 1), 1, 8);
    }

    if (profile.pitchTarget != MutationProfile::PitchTarget::None && !params.locks.pitch && pitchAllowed) {
        const int lo = std::min(params.pitchLow, params.pitchHigh);
        const int hi = std::max(params.pitchLow, params.pitchHigh);
        if (groove) {
            // WITH PURPOSE: step by scale DEGREE along a line shared by the
            // whole Part, turning occasionally, instead of jittering each
            // step on its own. That is the difference between a line and a
            // scatter, and it is why a randomized melody can be listened to
            // rather than merely tolerated. Only ENABLED steps advance it -
            // a silent step is not part of the line.
            if (unit(rng) < params.amount) {
                if (!contour.started) {
                    contour.started = true;
                    contour.direction = unit(rng) < 0.5f ? -1 : 1;
                    // WHERE THE LINE STARTS is what gives every value in
                    // the window a real chance. A contour and a uniform
                    // draw pull against each other, so: uniform over where
                    // the line BEGINS, coherent along it. Interpolated by
                    // amount - at 0.2 the line starts near what the pattern
                    // already plays, at 1.0 anywhere in the window.
                    const int existing = std::clamp(pitchOf(ev, part, profile.pitchTarget), lo, hi);
                    const int anywhere = lo + static_cast<int>(unit(rng) * static_cast<float>(hi - lo + 1));
                    const float lerped = static_cast<float>(existing)
                                          + (static_cast<float>(anywhere) - static_cast<float>(existing)) * params.amount;
                    contour.position = std::clamp(static_cast<int>(std::lround(lerped)), lo, hi);
                }
                // HOW FAR IT RUNS comes from the window, not a fixed
                // number. A walk that reverses with probability p has a
                // mean run of 1/p steps and a scale degree averages about
                // two semitones, so p = 2/span makes the mean excursion
                // equal the window asked for. `amount` deliberately does
                // not appear: amount is how much changes, the window is how
                // wide the gesture is.
                const float span = static_cast<float>(std::max(4, hi - lo));
                if (unit(rng) < std::clamp(2.0f / span, 0.05f, 0.5f))
                    contour.direction = -contour.direction;
                int next = stepScaleDegree(params.scale, contour.position, contour.direction);
                // Bounce off the window's edges rather than clamping to
                // them - a clamped walk parks on the boundary and repeats
                // one note until the direction happens to flip.
                if (next > hi) {
                    contour.direction = -1;
                    next = stepScaleDegree(params.scale, contour.position, -1);
                } else if (next < lo) {
                    contour.direction = 1;
                    next = stepScaleDegree(params.scale, contour.position, 1);
                }
                contour.position = std::clamp(next, lo, hi);
                setPitch(ev, profile.pitchTarget, contour.position);
            }
        } else {
            const int shift = static_cast<int>((unit(rng) - 0.5f) * pitchSpan * 2.0f * params.amount);
            if (shift != 0) {
                // Clamped to the user's own window. The jitter ADDS, so
                // without a real bound this is an unbounded walk that ends
                // up parked at an extreme.
                setPitch(ev, profile.pitchTarget,
                         std::clamp(pitchOf(ev, part, profile.pitchTarget) + shift, lo, hi));
            }
        }
    }

    if (profile.gate && !params.locks.gate) {
        const float gateJitter = (unit(rng) - 0.5f) * 2.0f * gateSpan * params.amount;
        ev.gateLength = std::clamp(ev.gateLength + gateJitter, 0.05f, 1.0f);
    }

    if (!params.locks.probability) {
        // A bounded DRAW, not a jitter. `probability += jitter` clamped to
        // 0..1 is a bounded random walk, and a bounded random walk lives at
        // its boundaries: measured on the previous code, after ten
        // Randomize passes 14.2% of steps sat at probability 0 - steps that
        // simply never fire again, with nothing in the UI saying why. Same
        // bug class as the unbounded pitch walk (see MutationParams::
        // pitchLow) and the one-sided microtiming clamp, and the same
        // answer: draw inside a stated range instead of walking.
        //
        // The else branch matters as much as the if: most steps go back to
        // ALWAYS, so a pattern does not accumulate probability nobody asked
        // for, and the category can be randomized back off.
        if (unit(rng) < params.amount * (groove ? 0.25f : 0.70f))
            ev.probability = groove ? (0.60f + unit(rng) * 0.40f) : (0.20f + unit(rng) * 0.80f);
        else
            ev.probability = 1.0f;
    }

    if (!params.locks.condition) {
        // A fresh draw rather than a nudge - the types are not on a scale,
        // so there is no "nearby" condition to drift toward. Weighted
        // heavily toward Always, because a pattern where most steps carry
        // an exotic condition stops being a pattern and becomes a puzzle.
        if (unit(rng) < params.amount * 0.25f) {
            const float roll = unit(rng);
            if (roll < 0.45f) {
                ev.condition.type = TrigConditionType::Always;
            } else if (roll < 0.75f) {
                ev.condition.type = TrigConditionType::Ratio;
                ev.condition.ratioN = 2 + static_cast<int>(unit(rng) * 3.0f); // 2-4 loops
                ev.condition.ratioK = std::clamp(1 + static_cast<int>(unit(rng) * static_cast<float>(ev.condition.ratioN)),
                                                  1, ev.condition.ratioN);
            } else if (roll < 0.85f) {
                ev.condition.type = TrigConditionType::Fill;
            } else if (roll < 0.95f) {
                ev.condition.type = TrigConditionType::FillInverse;
            } else {
                ev.condition.type = TrigConditionType::FirstLoop;
            }
        }
    }
}

} // namespace

Pattern Mutation::mutate(const Pattern& source, const MutationParams& params, const MutationProfile& profile) {
    Pattern result = source;
    if (params.amount <= 0.0f)
        return result; // identity - see the class docs

    for (int partIndex = 0; partIndex < static_cast<int>(result.parts.size()); ++partIndex) {
        const bool partLocked = partIndex < static_cast<int>(params.locks.part.size())
                                 && params.locks.part[static_cast<size_t>(partIndex)];
        if (partLocked)
            continue; // whole Part preserved verbatim

        std::mt19937 rng(params.seed + static_cast<std::uint32_t>(partIndex) * 7919u);
        Part& part = result.parts[static_cast<size_t>(partIndex)];
        const bool pitchAllowed = profile.partHasPitch == nullptr || profile.partHasPitch(partIndex);
        // One line per PART - each voice gets its own, rather than every
        // voice sharing one and moving in lockstep.
        ContourState contour;
        // Only what the sequencer reaches: storage past `length` is not
        // pattern, and randomizing it changes music nobody asked to hear
        // when the Part is later made longer.
        const int count = std::min(static_cast<int>(part.steps.size()), std::max(0, part.length));
        for (int stepIndex = 0; stepIndex < count; ++stepIndex)
            mutateStep(rng, params, profile, part, part.steps[static_cast<size_t>(stepIndex)], stepIndex, contour,
                        pitchAllowed);

        if (profile.afterPart != nullptr)
            profile.afterPart(partIndex, part);
    }

    return result;
}

} // namespace ruidocore
