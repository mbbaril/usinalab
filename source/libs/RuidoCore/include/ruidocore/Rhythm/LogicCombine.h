#pragma once

#include <vector>

namespace ruidocore {

// Combines rhythm gates with Boolean operations. AND retains shared hits,
// OR retains either hit, and XOR retains non-overlapping hits.
// NAND, NOR and XNOR invert the corresponding operation.
enum class LogicOp { And, Or, Xor, Nand, Nor, Xnor };

// `a` and `b` must be the same length (typically two parts' own step
// vectors, already the same `length` for the common case) - if lengths
// differ, only the overlapping prefix (min(a.size(), b.size())) is
// combined; positions beyond that are left as `a`'s own value, unchanged.
std::vector<bool> combineLogic(const std::vector<bool>& a, const std::vector<bool>& b, LogicOp op);

// N-input left-to-right fold : result = sources[0] OP sources[1] OP sources[2] OP
// ... - the natural generalization of a binary op to N inputs. Each
// source is resized (zero-padded) to `length` before folding, so callers
// don't need to pre-align lengths themselves.
std::vector<bool> combineLogicFold(const std::vector<std::vector<bool>>& sources, LogicOp op, size_t length);

// Continuous logic: AND uses MIN, OR uses MAX, and XOR uses |a-b|.
// Values are integers in [0, 127]; NAND, NOR and XNOR invert against 127.
std::vector<int> combineLogicContinuous(const std::vector<int>& a, const std::vector<int>& b, LogicOp op);
// N-input continuous fold :
// sources[0] OP sources[1] OP ... left to right, each padded to `length`
// with 0, using combineLogicContinuous for each pair.
std::vector<int> combineLogicContinuousFold(const std::vector<std::vector<int>>& sources, LogicOp op, size_t length);

} //
