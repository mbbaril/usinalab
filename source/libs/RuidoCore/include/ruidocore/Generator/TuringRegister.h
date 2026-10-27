#pragma once

#include <functional>
#include <random>
#include <vector>

namespace ruidocore {

// Turing-Machine-style evolving register (a shift-register-style motif/
// repeat concept): generates a short motif, then each repeat of that motif
// either holds (probability = `repetition`) or re-rolls independently via
// `decideStep`, producing organic variation instead of a hard cut between
// "identical loop" and "totally random".
//
// Extracted from Generator.cpp (2026-08-18, behavior-preserving refactor)
// so it's independently reusable - e.g. for a future Volca Bass/Kick/
// Sample2 port - decoupled from Generator's own weighted-grid step-
// decision logic via the `decideStep` callback: this module knows nothing
// about styles/grids, only "how to decide one step" is someone else's job.
std::vector<bool> turingRegisterMask(std::mt19937& rng, int length, float repetition,
                                      const std::function<bool(int stepIndex)>& decideStep);

} // namespace ruidocore
