// A plain bot: it weighs its chance to win against the price of staying in.
// It has no character, no memory and no tells; the named bots come later and
// replace it. It is enough to fill a table and to test the game.
#pragma once

#include <array>
#include <vector>

#include "game/hand.hpp"
#include "game/random.hpp"

namespace poher::bots {

// Everything a player can see when it is their turn. A bot gets nothing else:
// no other hands, no deck.
struct Situation {
    std::array<game::Card, 2> cards{};
    std::vector<game::Card> board;
    game::Chips pot = 0;          // all chips in the middle, bets of this street included
    game::Chips bet = 0;          // our own chips in on this street
    game::Chips big_blind = 0;
    game::Choices choices;
    int opponents = 1;            // players still in the hand, besides us
};

// Reads the situation of the player to act off a hand.
Situation situation_of(const game::Hand& hand);

game::Action simple_move(const Situation& situation, game::Random& random);

}  // namespace poher::bots
