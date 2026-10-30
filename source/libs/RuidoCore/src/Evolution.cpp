#include "ruidocore/Evolution/Evolution.h"

namespace ruidocore {

Evolution::Evolution(Pattern initial, EvolutionParams paramsIn)
    : pattern(std::move(initial)), params(std::move(paramsIn)) {}

const Pattern& Evolution::next() {
    if (frozen)
        return pattern;

    MutationParams mutationParams;
    mutationParams.amount = params.amountPerCycle;
    mutationParams.locks = params.locks;
    mutationParams.seed = params.seed + cycleCount * 104729u; // distinct prime step per cycle

    pattern = Mutation::mutate(pattern, mutationParams);
    ++cycleCount;
    return pattern;
}

} //
