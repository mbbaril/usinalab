#pragma once

#include <vector>

namespace ruidocore {

// Euclidean rhythm generator (Bjorklund's algorithm): distributes `pulses`
// hits as evenly as possible across `steps` positions - the same
// construction behind classic rhythms like the tresillo (E(3,8) =
// X..X..X.) and the standard clave patterns. `rotation` shifts the result
// by that many steps (wrapping), letting the same pulse/step interval
// structure start from a different point in the bar - e.g. E(3,8)
// rotated by 1 becomes .X..X..X, a different feel from the same shape.
//
// Returns a vector of size `steps`, one bool per step (true = hit).
// pulses is clamped to [0, steps]; steps <= 0 returns an empty vector.
std::vector<bool> euclideanRhythm(int steps, int pulses, int rotation = 0);

} //
