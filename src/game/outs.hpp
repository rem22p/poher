// Outs: the cards that would make a hand better if they came next.
#pragma once

#include <array>
#include <span>

#include "game/cards.hpp"
#include "game/hand_value.hpp"

namespace poher::game {

struct Outs {
    int count = 0;         // cards that improve the hand
    int unseen = 0;        // cards the player has not seen
    int to_flush = 0;      // of the outs: those that make a flush...
    int to_straight = 0;   // ...a straight...
    int to_pair = 0;       // ...a pair higher than anything on the board
    bool flush_draw = false;     // four cards of a suit
    bool open_ended = false;     // a straight comes with eight cards
    bool gutshot = false;        // a straight comes with four cards

    // The chance that the very next card is an out.
    double next_card() const { return unseen > 0 ? static_cast<double>(count) / unseen : 0; }
    // The chance that an out comes on one of the `cards_to_come` cards left (1 or 2).
    double by_the_river(int cards_to_come) const;
};

// Counts the outs on the flop or the turn (a board of three or four cards);
// with any other board the result is empty.
//
// A card is an out when it lifts the hand to a higher kind (a pair to two
// pair, a draw to a flush...) and the player's own cards are the reason: a
// card that pairs the board for everybody is not one. A new pair counts only
// when it is the top pair.
Outs count_outs(std::array<Card, 2> cards, std::span<const Card> board);

}  // namespace poher::game
