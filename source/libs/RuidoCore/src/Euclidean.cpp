#include "ruidocore/Rhythm/Euclidean.h"

#include <algorithm>

namespace ruidocore {

std::vector<bool> euclideanRhythm(int steps, int pulses, int rotation) {
    if (steps <= 0)
        return {};

    pulses = std::clamp(pulses, 0, steps);
    if (pulses == 0)
        return std::vector<bool>(static_cast<size_t>(steps), false);
    if (pulses == steps)
        return std::vector<bool>(static_cast<size_t>(steps), true);

    // Bjorklund's algorithm via the standard bucket-merging construction:
    // start with `pulses` groups of [true] and (steps-pulses) groups of
    // [false], then repeatedly pair off groups from the front of each list
    // and merge them, until at most one "remainder" group is left over.
    std::vector<std::vector<bool>> groups;
    groups.reserve(static_cast<size_t>(pulses));
    for (int i = 0; i < pulses; ++i)
        groups.push_back({true});

    std::vector<std::vector<bool>> remainders;
    remainders.reserve(static_cast<size_t>(steps - pulses));
    for (int i = 0; i < steps - pulses; ++i)
        remainders.push_back({false});

    while (remainders.size() > 1) {
        const size_t pairCount = std::min(groups.size(), remainders.size());

        std::vector<std::vector<bool>> mergedGroups;
        mergedGroups.reserve(pairCount);
        for (size_t i = 0; i < pairCount; ++i) {
            auto combined = groups[i];
            combined.insert(combined.end(), remainders[i].begin(), remainders[i].end());
            mergedGroups.push_back(std::move(combined));
        }

        std::vector<std::vector<bool>> leftover;
        if (groups.size() > pairCount) {
            for (size_t i = pairCount; i < groups.size(); ++i)
                leftover.push_back(groups[i]);
        } else {
            for (size_t i = pairCount; i < remainders.size(); ++i)
                leftover.push_back(remainders[i]);
        }

        groups = std::move(mergedGroups);
        remainders = std::move(leftover);
    }

    std::vector<bool> result;
    result.reserve(static_cast<size_t>(steps));
    for (auto& g : groups)
        result.insert(result.end(), g.begin(), g.end());
    for (auto& r : remainders)
        result.insert(result.end(), r.begin(), r.end());

    if (!result.empty()) {
        const int n = static_cast<int>(result.size());
        const int r = ((rotation % n) + n) % n;
        std::rotate(result.begin(), result.begin() + r, result.end());
    }

    return result;
}

} //
