// How often a hand wins if the cards are simply dealt out to the end.
#pragma once

#include <array>
#include <span>

#include "game/cards.hpp"
#include "game/random.hpp"

namespace poher::game {

// The share of the pot the hand gets on average against `opponents` unknown
// hands: 1 for a sure win, 0.5 for a coin flip one on one. A tie counts as its
// part of the pot. Found by dealing the missing cards `samples` times.
double estimate_equity(std::array<Card, 2> cards, std::span<const Card> board, int opponents, int samples,
                       Random& random);

}  // namespace poher::game
