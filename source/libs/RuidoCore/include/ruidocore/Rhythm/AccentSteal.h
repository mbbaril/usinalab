#pragma once

#include "../Core/Event.h"

#include <algorithm>
#include <vector>

namespace ruidocore {

// A strong hit borrows loudness from the hit after it, so the follower
// comes out weaker. CONSERVATION rather than addition - which is what
// makes a groove breathe instead of merely hitting harder.
//
// It is not a per-step value and cannot be one: a step's accent depends
// on its neighbour's, so this only makes sense where a whole Part is in
// hand - which is Generator::generate, Mutation::mutate, and a post-pass
// after a hand edit.
//
// Borrows from the next ENABLED step, not the next slot - a rest between
// two hits is not something to steal from. Deliberately does NOT wrap
// around the end of the Part: the last hit has no follower, and wrapping
// would make the result depend on where the pass happened to start.
//
// `amount` is 0..1. Strict conservation holds until a clamp bites: a
// transfer that would push a neighbour below 0 or a peak above 1 is
// truncated, and the difference is simply lost rather than redistributed.
// Stated rather than hidden - the alternative (a second balancing pass)
// buys accuracy nobody can hear and costs a rule nobody can predict.
//
// Only reaches steps inside `length`: storage past it is not pattern.
inline void applyAccentSteal(std::vector<StepEvent>& steps, int length, float amount) {
    if (amount <= 0.0f)
        return;
    amount = std::clamp(amount, 0.0f, 1.0f);
    const int n = std::min(length, static_cast<int>(steps.size()));
    int previousHit = -1;
    for (int i = 0; i < n; ++i) {
        if (!steps[static_cast<size_t>(i)].enabled)
            continue;
        if (previousHit >= 0) {
            StepEvent& strong = steps[static_cast<size_t>(previousHit)];
            StepEvent& weak = steps[static_cast<size_t>(i)];
            // Only a hit that is ALREADY stronger than its follower
            // borrows. Without this the pass would flatten a crescendo by
            // stealing backwards up the ramp.
            const float gap = strong.accentAmount - weak.accentAmount;
            if (gap > 0.0f) {
                const float transfer =
                    std::min({gap * 0.5f * amount, 1.0f - strong.accentAmount, weak.accentAmount});
                if (transfer > 0.0f) {
                    strong.accentAmount = std::clamp(strong.accentAmount + transfer, 0.0f, 1.0f);
                    weak.accentAmount = std::clamp(weak.accentAmount - transfer, 0.0f, 1.0f);
                }
            }
        }
        previousHit = i;
    }
}

} // namespace ruidocore
