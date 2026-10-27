#pragma once

#include <vector>

namespace ruidocore {

// Boolean-logic combination of two parts' rhythms (a trigger-combiner
// concept, 2026-08-18): a simple way to get emergent complexity
// from two plain sources - AND tends to sparsen (only shared hits
// survive), OR tends to thicken (either source hits), XOR keeps only the
// non-overlapping hits (a natural syncopation generator when combining two
// otherwise-simple grids). NAND/NOR/XNOR added 2026-09-03 (ported from
// the Volca/DR-670/Eurorack family's own Logic Combine live-gate
// redesign) - trivial inversions of the boolean ops above.
enum class LogicOp { And, Or, Xor, Nand, Nor, Xnor };

// `a` and `b` must be the same length (typically two parts' own step
// vectors, already the same `length` for the common case) - if lengths
// differ, only the overlapping prefix (min(a.size(), b.size())) is
// combined; positions beyond that are left as `a`'s own value, unchanged.
std::vector<bool> combineLogic(const std::vector<bool>& a, const std::vector<bool>& b, LogicOp op);

// N-input left-to-right fold (2026-09-03, ported from the Volca/DR-670/
// Eurorack family): result = sources[0] OP sources[1] OP sources[2] OP
// ... - the natural generalization of a binary op to N inputs. Each
// source is resized (zero-padded) to `length` before folding, so callers
// don't need to pre-align lengths themselves.
std::vector<bool> combineLogicFold(const std::vector<std::vector<bool>>& sources, LogicOp op, size_t length);

// Continuous-value overload (2026-08-20, "veja como reformar a funcao de
// logica da drum, ja que lanes sao valores nao binarios" - Kick's CC
// lanes carry a real 0-127 value per step, not just on/off, so reusing
// the boolean combine on them threw away everything but which steps
// fired). Same real-world precedent analog/Eurorack "logic" modules use
// for continuous CV instead of gates: AND -> MIN (the lower of the two),
// OR -> MAX (the higher), XOR -> absolute difference |a-b| (emphasizes
// where the two lanes diverge, near-zero where they agree). Values are
// 0-127 ints, same range every CC lane step already uses. NAND/NOR/XNOR
// (2026-09-03) invert against that same 0-127 ceiling, matching Volca
// Kick's own continuous-lane extension.
std::vector<int> combineLogicContinuous(const std::vector<int>& a, const std::vector<int>& b, LogicOp op);
// N-input continuous fold (brought in from the Volca Kick 2026-09-16):
// sources[0] OP sources[1] OP ... left to right, each padded to `length`
// with 0, using combineLogicContinuous for each pair.
std::vector<int> combineLogicContinuousFold(const std::vector<std::vector<int>>& sources, LogicOp op, size_t length);

} // namespace ruidocore
