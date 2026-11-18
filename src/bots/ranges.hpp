// Chances against the hands opponents are likely to hold, rather than against
// any two cards: someone who raised before the flop and bet twice after it
// rarely shows up with seven-two.
#pragma once

#include <array>
#include <span>

#include "game/cards.hpp"
#include "game/random.hpp"

namespace poher::bots {

// What is believed about one opponent's hand.
struct Foe {
    double range = 1;  // the share of starting hands they play this way: 1 = any two cards, 0.1 = the best tenth
    double story = 0;  // strength told by their bets after the flop: 0 = none, 1 = one solid bet, 3 = bets all the way
};

// How fast belief in a hand fades as the story grows: 0 for a strong hand
// (always believable), about 1 for nothing at all. Two pair or better made
// with own cards is strong; then come top pair, lower pairs, draws, air.
double fade_rate(std::array<game::Card, 2> cards, std::span<const game::Card> board);

// Our share of the pot at showdown, found by dealing the opponents likely
// hands and the rest of the board `samples` times.
double equity_against(std::array<game::Card, 2> cards, std::span<const game::Card> board, std::span<const Foe> foes,
                      int samples, game::Random& random);

}  // namespace poher::bots
