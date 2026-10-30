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
// Reusable feedback-register state for pattern and modulation generation.
std::vector<bool> turingRegisterMask(std::mt19937& rng, int length, float repetition,
                                      const std::function<bool(int stepIndex)>& decideStep);

} //
