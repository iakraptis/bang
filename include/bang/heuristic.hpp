#pragma once

#include "bang/game.hpp"

namespace bang {

int ChooseHeuristicAction(const GameState& state, FastRng& rng);

}  // namespace bang
