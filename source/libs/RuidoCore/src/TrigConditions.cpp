#include "ruidocore/Rhythm/TrigConditions.h"

#include <algorithm>

namespace ruidocore {

bool evaluateTrigCondition(const TrigCondition& condition, int loopCount, bool fillActive) {
    switch (condition.type) {
        case TrigConditionType::Always:
            return true;

        case TrigConditionType::Ratio: {
            const int n = condition.ratioN < 1 ? 1 : condition.ratioN;
            const int k = std::clamp(condition.ratioK, 1, n);
            return (loopCount % n) == (k - 1);
        }

        case TrigConditionType::Fill:
            return fillActive;

        case TrigConditionType::FillInverse:
            return !fillActive;

        case TrigConditionType::FirstLoop:
            return loopCount == 0;
    }
    return true;
}

} //
