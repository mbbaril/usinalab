#pragma once

#include "../Core/Event.h"

namespace ruidocore {

// Evaluates whether a step's TrigCondition permits it to fire this pass,
// given the owning part's current loop count (Sequencer::loopCountForPart)
// and whether the performer's FILL control is currently held. Pure
// function, no RNG/state of its own - a failed condition is treated by the
// caller exactly like a failed probability roll: the whole step (locks +
// ratchet repeats) is skipped.
//
// loopCount is 0-based (the first pass through the part is loop 0).
bool evaluateTrigCondition(const TrigCondition& condition, int loopCount, bool fillActive);

} //
