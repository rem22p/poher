// How good two starting cards are.
#pragma once

#include <array>

#include "game/cards.hpp"

namespace poher::bots {

// The share of all starting hands that are worse than this one: close to 1
// for a pair of aces, close to 0 for seven-two. A hand with 0.8 is in the top
// fifth. See tools/preflop_table.cpp for how the numbers were made.
double start_strength(const std::array<game::Card, 2>& cards);

}  // namespace poher::bots
