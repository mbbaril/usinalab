#include "ruidocore/Rhythm/LogicCombine.h"

#include <algorithm>
#include <cstdlib>

namespace ruidocore {

std::vector<bool> combineLogic(const std::vector<bool>& a, const std::vector<bool>& b, LogicOp op) {
    std::vector<bool> result = a;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        switch (op) {
            case LogicOp::And: result[i] = a[i] && b[i]; break;
            case LogicOp::Or: result[i] = a[i] || b[i]; break;
            case LogicOp::Xor: result[i] = a[i] != b[i]; break;
            case LogicOp::Nand: result[i] = !(a[i] && b[i]); break;
            case LogicOp::Nor: result[i] = !(a[i] || b[i]); break;
            case LogicOp::Xnor: result[i] = a[i] == b[i]; break;
        }
    }
    return result;
}

std::vector<bool> combineLogicFold(const std::vector<std::vector<bool>>& sources, LogicOp op, size_t length) {
    if (sources.empty())
        return std::vector<bool>(length, false);
    std::vector<bool> result = sources[0];
    result.resize(length, false);
    for (size_t s = 1; s < sources.size(); ++s) {
        std::vector<bool> next = sources[s];
        next.resize(length, false);
        result = combineLogic(result, next, op);
    }
    return result;
}

std::vector<int> combineLogicContinuous(const std::vector<int>& a, const std::vector<int>& b, LogicOp op) {
    std::vector<int> result = a;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        switch (op) {
            case LogicOp::And: result[i] = std::min(a[i], b[i]); break;
            case LogicOp::Or: result[i] = std::max(a[i], b[i]); break;
            case LogicOp::Xor: result[i] = std::abs(a[i] - b[i]); break;
            case LogicOp::Nand: result[i] = 127 - std::min(a[i], b[i]); break;
            case LogicOp::Nor: result[i] = 127 - std::max(a[i], b[i]); break;
            case LogicOp::Xnor: result[i] = 127 - std::abs(a[i] - b[i]); break;
        }
    }
    return result;
}

std::vector<int> combineLogicContinuousFold(const std::vector<std::vector<int>>& sources, LogicOp op, size_t length) {
    if (sources.empty())
        return std::vector<int>(length, 0);
    std::vector<int> result = sources[0];
    result.resize(length, 0);
    for (size_t s = 1; s < sources.size(); ++s) {
        std::vector<int> next = sources[s];
        next.resize(length, 0);
        result = combineLogicContinuous(result, next, op);
    }
    return result;
}

} // namespace ruidocore
