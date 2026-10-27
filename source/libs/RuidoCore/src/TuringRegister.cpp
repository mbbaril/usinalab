#include "ruidocore/Generator/TuringRegister.h"

#include <algorithm>

namespace ruidocore {

std::vector<bool> turingRegisterMask(std::mt19937& rng, int length, float repetition,
                                      const std::function<bool(int stepIndex)>& decideStep) {
    std::vector<bool> active(static_cast<size_t>(length), false);
    const int motifLen = std::clamp(length / 4, 2, 8);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (int i = 0; i < length; ++i) {
        const bool isFirstMotif = i < motifLen;
        if (isFirstMotif || unit(rng) >= repetition) {
            active[static_cast<size_t>(i)] = decideStep(i);
        } else {
            const int sourceIndex = i - motifLen;
            active[static_cast<size_t>(i)] = active[static_cast<size_t>(sourceIndex)];
        }
    }
    return active;
}

} // namespace ruidocore
